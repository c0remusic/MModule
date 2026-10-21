#include "Lzma.h"
#include "Firmware.h"

#include <array>

namespace md::fw {

namespace {
constexpr int kNumBitModelTotalBits = 11, kNumMoveBits = 5;
constexpr uint16_t kProbInit = 1 << (kNumBitModelTotalBits - 1);
constexpr uint32_t kTopValue = 1u << 24;
constexpr int kNumStates = 12, kNumPosBitsMax = 4, kNumLenToPosStates = 4, kNumAlignBits = 4;
constexpr int kStartPosModelIndex = 4, kEndPosModelIndex = 14, kNumFullDistances = 1 << (kEndPosModelIndex >> 1);
constexpr int kMatchMinLen = 2;

struct RangeDecoder {
    const uint8_t* in; size_t size, pos = 0;
    uint32_t range = 0xFFFFFFFF, code = 0;

    RangeDecoder(const uint8_t* _in, size_t _size) : in(_in), size(_size)
    {
        if (size < 5 || in[0] != 0) throw FirmwareError("lzma: bad stream start");
        pos = 1;
        for (int i = 0; i < 4; ++i) code = (code << 8) | in[pos++];
        if (code == range) throw FirmwareError("lzma: corrupt stream");
    }
    uint8_t next()
    {
        if (pos >= size) throw FirmwareError("lzma: stream ends early");
        return in[pos++];
    }
    void normalize()
    {
        if (range < kTopValue) { range <<= 8; code = (code << 8) | next(); }
    }
    uint32_t directBits(int _n)
    {
        uint32_t res = 0;
        do {
            range >>= 1;
            code -= range;
            const uint32_t t = 0u - (code >> 31);
            code += range & t;
            if (code == range) throw FirmwareError("lzma: corrupt stream");
            normalize();
            res = (res << 1) + (t + 1);
        } while (--_n);
        return res;
    }
    uint32_t bit(uint16_t& _prob)
    {
        const uint32_t bound = (range >> kNumBitModelTotalBits) * _prob;
        uint32_t symbol;
        if (code < bound) {
            _prob = uint16_t(_prob + (((1u << kNumBitModelTotalBits) - _prob) >> kNumMoveBits));
            range = bound;
            symbol = 0;
        } else {
            _prob = uint16_t(_prob - (_prob >> kNumMoveBits));
            code -= bound;
            range -= bound;
            symbol = 1;
        }
        normalize();
        return symbol;
    }
};

uint32_t treeDecode(uint16_t* _probs, int _numBits, RangeDecoder& _rc)
{
    uint32_t m = 1;
    for (int i = 0; i < _numBits; ++i) m = (m << 1) + _rc.bit(_probs[m]);
    return m - (1u << _numBits);
}

uint32_t treeReverseDecode(uint16_t* _probs, int _numBits, RangeDecoder& _rc)
{
    uint32_t m = 1, symbol = 0;
    for (int i = 0; i < _numBits; ++i) {
        const uint32_t b = _rc.bit(_probs[m]);
        m = (m << 1) + b;
        symbol |= b << i;
    }
    return symbol;
}

struct LenDecoder {
    uint16_t choice = kProbInit, choice2 = kProbInit;
    std::array<std::array<uint16_t, 1 << 3>, 1 << kNumPosBitsMax> low, mid;
    std::array<uint16_t, 1 << 8> high;
    LenDecoder()
    {
        for (auto& a : low) a.fill(kProbInit);
        for (auto& a : mid) a.fill(kProbInit);
        high.fill(kProbInit);
    }
    uint32_t decode(RangeDecoder& _rc, uint32_t _posState)
    {
        if (_rc.bit(choice) == 0) return treeDecode(low[_posState].data(), 3, _rc);
        if (_rc.bit(choice2) == 0) return 8 + treeDecode(mid[_posState].data(), 3, _rc);
        return 16 + treeDecode(high.data(), 8, _rc);
    }
};

uint32_t literalState(uint32_t _s) { return _s < 4 ? 0 : _s < 10 ? _s - 3 : _s - 6; }
uint32_t matchState(uint32_t _s) { return _s < 7 ? 7 : 10; }
uint32_t repState(uint32_t _s) { return _s < 7 ? 8 : 11; }
uint32_t shortRepState(uint32_t _s) { return _s < 7 ? 9 : 11; }
}

std::vector<uint8_t> lzmaDecode(const uint8_t* _data, size_t _size, size_t _unpacked, LzmaProperties _props)
{
    if (_props.lc > 8 || _props.lp > 4 || _props.pb > 4) throw FirmwareError("lzma: properties out of range");
    RangeDecoder rc(_data, _size);
    std::vector<uint16_t> literals(size_t(0x300) << (_props.lc + _props.lp), kProbInit);
    std::array<uint16_t, kNumStates << kNumPosBitsMax> isMatch, isRep0Long;
    std::array<uint16_t, kNumStates> isRep, isRepG0, isRepG1, isRepG2;
    std::array<std::array<uint16_t, 1 << 6>, kNumLenToPosStates> posSlot;
    std::array<uint16_t, 1 + kNumFullDistances - kEndPosModelIndex> posDecoders;
    std::array<uint16_t, 1 << kNumAlignBits> align;
    isMatch.fill(kProbInit); isRep0Long.fill(kProbInit);
    isRep.fill(kProbInit); isRepG0.fill(kProbInit); isRepG1.fill(kProbInit); isRepG2.fill(kProbInit);
    for (auto& a : posSlot) a.fill(kProbInit);
    posDecoders.fill(kProbInit); align.fill(kProbInit);
    LenDecoder lenDecoder, repLenDecoder;

    std::vector<uint8_t> out;
    out.reserve(_unpacked);
    uint32_t rep0 = 0, rep1 = 0, rep2 = 0, rep3 = 0, state = 0;
    const uint32_t pbMask = (1u << _props.pb) - 1, lpMask = (1u << _props.lp) - 1;
    const auto back = [&](uint32_t _dist) { return out[out.size() - _dist - 1]; };

    while (out.size() < _unpacked) {
        const uint32_t posState = uint32_t(out.size()) & pbMask;
        if (rc.bit(isMatch[(state << kNumPosBitsMax) + posState]) == 0) {
            const uint32_t prev = out.empty() ? 0 : out.back();
            const uint32_t litState = ((uint32_t(out.size()) & lpMask) << _props.lc) + (prev >> (8 - _props.lc));
            uint16_t* probs = &literals[size_t(0x300) * litState];
            uint32_t symbol = 1;
            if (state >= 7) {
                uint32_t matchByte = back(rep0);
                do {
                    const uint32_t matchBit = (matchByte >> 7) & 1;
                    matchByte <<= 1;
                    const uint32_t b = rc.bit(probs[((1 + matchBit) << 8) + symbol]);
                    symbol = (symbol << 1) | b;
                    if (matchBit != b) break;
                } while (symbol < 0x100);
            }
            while (symbol < 0x100) symbol = (symbol << 1) | rc.bit(probs[symbol]);
            out.push_back(uint8_t(symbol - 0x100));
            state = literalState(state);
            continue;
        }
        uint32_t len;
        if (rc.bit(isRep[state]) != 0) {
            if (out.empty()) throw FirmwareError("lzma: repeat before any byte");
            if (rc.bit(isRepG0[state]) == 0) {
                if (rc.bit(isRep0Long[(state << kNumPosBitsMax) + posState]) == 0) {
                    state = shortRepState(state);
                    out.push_back(back(rep0));
                    continue;
                }
            } else {
                uint32_t dist;
                if (rc.bit(isRepG1[state]) == 0) dist = rep1;
                else {
                    if (rc.bit(isRepG2[state]) == 0) dist = rep2;
                    else { dist = rep3; rep3 = rep2; }
                    rep2 = rep1;
                }
                rep1 = rep0;
                rep0 = dist;
            }
            len = repLenDecoder.decode(rc, posState);
            state = repState(state);
        } else {
            rep3 = rep2; rep2 = rep1; rep1 = rep0;
            len = lenDecoder.decode(rc, posState);
            state = matchState(state);
            const uint32_t lenState = len < kNumLenToPosStates - 1 ? len : kNumLenToPosStates - 1;
            const uint32_t slot = treeDecode(posSlot[lenState].data(), 6, rc);
            if (slot < kStartPosModelIndex) rep0 = slot;
            else {
                const int numDirectBits = int(slot >> 1) - 1;
                uint32_t dist = (2 | (slot & 1)) << numDirectBits;
                if (slot < kEndPosModelIndex) dist += treeReverseDecode(&posDecoders[dist - slot], numDirectBits, rc);
                else {
                    dist += rc.directBits(numDirectBits - kNumAlignBits) << kNumAlignBits;
                    dist += treeReverseDecode(align.data(), kNumAlignBits, rc);
                }
                rep0 = dist;
            }
            if (rep0 == 0xFFFFFFFF) break;   // the end marker
        }
        len += kMatchMinLen;
        if (rep0 >= out.size()) throw FirmwareError("lzma: match before the output's start");
        for (uint32_t i = 0; i < len && out.size() < _unpacked; ++i) out.push_back(back(rep0));
    }
    return out;
}

} // namespace md::fw
