#include "X20Os.h"
#include "Lzma.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <string>

namespace md::fw {

namespace {
uint32_t be32(const uint8_t* p) { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3]; }
uint32_t le24(const uint8_t* p) { return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16); }

std::string hex(uint32_t _v)
{
    static const char* digits = "0123456789abcdef";
    std::string s = "$";
    bool started = false;
    for (int shift = 28; shift >= 0; shift -= 4) {
        const auto d = (_v >> shift) & 0xF;
        if (d || started || shift == 0) { s += digits[d]; started = true; }
    }
    return s;
}

// A 1.63-style section: [size BE32][byte sum BE32][stream], the stream depacked
std::vector<uint8_t> depackSection(const uint8_t* _p, size_t _avail, const char* _what)
{
    if (_avail < 8) throw FirmwareError(std::string(_what) + " is truncated");
    const uint32_t size = be32(_p), sum = be32(_p + 4);
    if (size > _avail - 8) throw FirmwareError(std::string(_what) + " is truncated");
    uint32_t s = 0;
    for (uint32_t i = 0; i < size; ++i) s += _p[8 + i];
    if (s != sum) throw FirmwareError(std::string(_what) + "'s byte sum does not match");
    return aplibDepack(_p + 8, size);
}
}

uint32_t crc32(const uint8_t* _data, size_t _size)
{
    static const auto table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < _size; ++i) c = table[(c ^ _data[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

DspImage parseDsu(const std::vector<uint8_t>& _blob, std::vector<uint8_t>* _tableAndData)
{
    constexpr size_t kHeader = 0x1e, kRecordsAt = 18, kRecordSize = 9;
    if (_blob.size() < kHeader || std::memcmp(_blob.data(), "DSU", 3) != 0) throw FirmwareError("no DSU container");
    const uint32_t outLen = le24(&_blob[18]), packed = le24(&_blob[21]);
    if (packed > _blob.size() - kHeader) throw FirmwareError("DSU: LZMA stream larger than the container");
    const size_t lzAt = _blob.size() - packed;
    const auto data = lzmaDecode(&_blob[lzAt], packed, outLen, {0, 0, 0});
    if (data.size() != outLen) throw FirmwareError("DSU: LZMA output shorter than its header says");
    const uint8_t* table = &_blob[kHeader];
    const size_t tableSize = lzAt - kHeader;
    // The second of the three CRCs the table starts with, each a 24-bit then an 8-bit word: the LZMA output's
    if (tableSize < kRecordsAt || (le24(table + 6) | (uint32_t(table[9]) << 24)) != crc32(data.data(), data.size()))
        throw FirmwareError("DSU: the words' CRC does not match");

    DspImage image;
    size_t pos = 0;   // in words
    for (size_t t = kRecordsAt; t + kRecordSize <= tableSize; t += kRecordSize) {
        const uint32_t space = le24(table + t), a = le24(table + t + 3), count = le24(table + t + 6);
        if (space > 2) throw FirmwareError("DSU: record for memory space " + std::to_string(space));
        if ((pos + count) * 3 > data.size()) throw FirmwareError("DSU: records hold more words than the data");
        const uint32_t mode = a >> 21;
        DspRecord r{Space(space), a & 0x1FFFFF, std::vector<uint32_t>(count)};
        const uint8_t* b = &data[pos * 3];
        for (uint32_t i = 0; i < count; ++i)
            r.words[i] = uint32_t(b[i]) | (uint32_t(b[count + i]) << 8) | (uint32_t(b[2 * count + i]) << 16);
        const int sums = mode == 0 ? 1 : mode == 2 ? 2 : 0;
        for (int s = 0; s < sums; ++s) {
            uint32_t acc = 0;
            for (auto& w : r.words) { acc = (acc + w) & 0xFFFFFF; w = acc; }
        }
        image.records.push_back(std::move(r));
        pos += count;
    }
    if (_tableAndData) {
        _tableAndData->assign(table, table + tableSize);
        _tableAndData->insert(_tableAndData->end(), data.begin(), data.end());
    }
    return image;
}

X20Os parseX20Os(const std::vector<uint8_t>& syx)
{
    const auto flash = parseSysex(syx);
    const auto& img = flash.bytes;
    const auto stage1 = depackSection(img.data(), img.size(), "the OS's section 0");

    // The loader closes its image with the S2MD header: version, size $60, its own CRC-32 at +$5c
    constexpr size_t kHeaderSize = 0x60;
    const char magic[] = {'S', '2', 'M', 'D'};
    const auto at = std::search(stage1.begin(), stage1.end(), std::begin(magic), std::end(magic));
    if (at == stage1.end() || size_t(stage1.end() - at) < kHeaderSize)
        throw FirmwareError("not an X.20 OS: no S2MD header (OS 1.63 and older have none)");
    const uint8_t* h = &*at;
    if (crc32(h, 0x5c) != be32(h + 0x5c)) throw FirmwareError("the S2MD header's CRC does not match");
    const uint32_t start = be32(h + 12), end = be32(h + 16);
    if (start < flash.base || end > flash.base + img.size() || start >= end)
        throw FirmwareError("the S2MD header's payload bounds lie outside the file");

    X20Os os;
    uint32_t pos = start;
    for (int i = 1; i <= 4; ++i) {
        const uint8_t* e = h + 4 + 16 * i;
        const uint32_t packed = be32(e), unpacked = be32(e + 4), crcPacked = be32(e + 8), crcUnpacked = be32(e + 12);
        if (pos + packed > end) throw FirmwareError("payload " + std::to_string(i) + " runs past the payloads' end");
        const std::vector<uint8_t> blob(img.begin() + (pos - flash.base), img.begin() + (pos - flash.base) + packed);
        if (crc32(blob.data(), blob.size()) != crcPacked)
            throw FirmwareError("payload " + std::to_string(i) + " at " + hex(pos) + ": CRC does not match");
        std::vector<uint8_t> unpackedBytes;
        switch (i) {
        case 1:
        case 2:
            (i == 1 ? os.voice : os.mixer) = parseDsu(blob, &unpackedBytes);
            break;
        case 3:
            unpackedBytes = lzmaDecode(blob.data(), blob.size(), unpacked, {0, 0, 1});
            os.os = unpackedBytes;
            break;
        default:
            unpackedBytes = depackSection(blob.data(), blob.size(), "the DSP kernel");
            os.kernel = parseDspRecords(unpackedBytes);
            break;
        }
        // A DSU's unpacked CRC covers what is not known (research/01 §3): its own CRC of the words stands for it
        const bool crcKnown = i > 2;
        if (unpackedBytes.size() != unpacked
            || (crcKnown && crc32(unpackedBytes.data(), unpackedBytes.size()) != crcUnpacked))
            throw FirmwareError("payload " + std::to_string(i) + " unpacked: size or CRC does not match");
        pos = (pos + packed + 3) & ~3u;
    }
    return os;
}

} // namespace md::fw
