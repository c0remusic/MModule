#include "stMainControl.h"

#include <cstring>

#include "stEmac.h"

namespace syntakt::main
{
	namespace
	{
		constexpr uint8_t SpTwinshot = 44;
		constexpr uint8_t SyRaw = 39;
		constexpr uint32_t FxLockBits = LockBits + 12 * LockStride;	// 0x4550e25c
		constexpr uint32_t FxWord = 0x362;								// the FX track's word of internal id 0

		uint32_t stateOf(const uint32_t _word) { return SmoothState + 4 * _word; }

		// A word's state when it snaps: the word in the high half
		int32_t snapped(const int32_t _word) { return asl(_word, 16); }

		// The lowest set bit of a non-zero long: x & -x, then 31 - ff1
		int32_t lowestBit(const uint32_t _bits)
		{
			int32_t b = 0;
			while(!(_bits >> b & 1))
				++b;
			return b;
		}
	}

	uint32_t smooth(Memory& _mem, const uint32_t _target)
	{
		constexpr int32_t TargetWeight = 0x03d70000;
		constexpr int32_t StateWeight = 0x7c290000;

		// Two words a pass, one accumulator each (mac.w on the high and low halves of the target long). The output
		// long of a pass is written in the next one, and the first pass writes back the long before Smoothed
		EmacWrap acc0, acc1;
		uint32_t target = _target, state = SmoothState, out = Smoothed - 4;
		int32_t pair = _mem.word(out);
		int32_t targets = _mem.word(target);
		target += 4;

		// The same passes on the three runs' bytes where the memory's table holds each whole (SRAM, normally): read
		// and written in the same order, they are what word() and setWord() give and take
		constexpr uint32_t Passes = (EngineWords + 1) / 2;
		const uint8_t* targetAt = _mem.bytesAt(target, 4 * Passes);
		uint8_t* stateAt = _mem.writableBytesAt(state, 8 * Passes);
		uint8_t* outAt = _mem.writableBytesAt(out, 4 * (Passes + 1));
		if(targetAt && stateAt && outAt)
		{
			for(uint32_t p = 0; p < Passes; ++p)
			{
				acc0.mac(TargetWeight, static_cast<int32_t>(static_cast<uint32_t>(targets) & 0xffff0000));
				acc0.mac(StateWeight, Memory::wordAt(stateAt));
				Memory::setWordAt(outAt, pair);
				outAt += 4;
				acc1.mac(TargetWeight, asl(targets, 16));
				acc1.mac(StateWeight, Memory::wordAt(stateAt + 4));
				targets = Memory::wordAt(targetAt);
				targetAt += 4;
				const int32_t s0 = acc0.storeClear();
				Memory::setWordAt(stateAt, s0);
				const int32_t s1 = acc1.storeClear();
				Memory::setWordAt(stateAt + 4, s1);
				stateAt += 8;
				const auto high = static_cast<uint32_t>(addWrap(s0, 0x8000)) & 0xffff0000;
				const auto low = static_cast<uint32_t>(addWrap(s1, 0x8000)) >> 16;
				pair = static_cast<int32_t>(high | low);
			}
			out += 4 * Passes;
		}
		else
		{
			for(uint32_t n = EngineWords; static_cast<int32_t>(n) > 0; n -= 2)
			{
				acc0.mac(TargetWeight, static_cast<int32_t>(static_cast<uint32_t>(targets) & 0xffff0000));
				acc0.mac(StateWeight, _mem.word(state));
				_mem.setWord(out, pair);
				out += 4;
				acc1.mac(TargetWeight, asl(targets, 16));
				acc1.mac(StateWeight, _mem.word(state + 4));
				targets = _mem.word(target);
				target += 4;
				const int32_t s0 = acc0.storeClear();
				_mem.setWord(state, s0);
				const int32_t s1 = acc1.storeClear();
				_mem.setWord(state + 4, s1);
				state += 8;
				const auto high = static_cast<uint32_t>(addWrap(s0, 0x8000)) & 0xffff0000;
				const auto low = static_cast<uint32_t>(addWrap(s1, 0x8000)) >> 16;
				pair = static_cast<int32_t>(high | low);
			}
		}
		_mem.setWord(out, pair);

		// memcpy 0x40115f78: at once where one page of the table holds each run and the runs do not overlap, else a
		// byte at a time
		const uint32_t settings = _target + 2 * EngineWords;
		const uint8_t* from = _mem.bytesAt(settings, SettingsBytes);
		uint8_t* to = _mem.writableBytesAt(SettingsCopy, SettingsBytes);
		if(from && to && (from + SettingsBytes <= to || to + SettingsBytes <= from))
			std::memcpy(to, from, SettingsBytes);
		else
			for(uint32_t i = 0; i < SettingsBytes; ++i)
				_mem.setByte(SettingsCopy + i, _mem.byte(settings + i));

		for(uint32_t t = 0; t < 12; ++t)
		{
			const uint32_t src = _target + TrackBytes * t, dst = Smoothed + TrackBytes * t;
			_mem.setHalf(dst + 0x24, _mem.half(src + 0x24));	// LFO1 DEST
			_mem.setHalf(dst + 0x34, _mem.half(src + 0x34));	// LFO2 DEST
			if(_mem.byte(Machines + t) == SpTwinshot)
			{
				_mem.setHalf(dst + 0x42, _mem.half(src + 0x42));
				_mem.setHalf(dst + 0x4a, _mem.half(src + 0x4a));
			}
		}
		for(uint32_t k = 0; k < 3; ++k)
		{
			if(_mem.byte(Machines + 8 + k) != SyRaw)
				continue;
			const uint32_t offset = TrackBytes * k + 0x4b8;	// track 8 + k, +0x48
			_mem.setHalf(Smoothed + offset, _mem.half(_target + offset));
		}
		return Smoothed;
	}

	void lock(Memory& _mem, const int32_t _t, const uint32_t _list, const uint32_t _engine)
	{
		const auto t = static_cast<uint32_t>(_t);
		const uint32_t bits = LockBits + LockStride * t;
		uint32_t entry = _list + 0x14;
		for(int32_t i = 0; i < _mem.word(_list + 8); ++i, entry += 8)
		{
			const int32_t id = _mem.half(entry);
			const int32_t lockWord = (id < 0 ? id + 0x1f : id) >> 5;
			const uint32_t bitsAt = bits + 4 * static_cast<uint32_t>(lockWord);
			_mem.setWord(bitsAt, _mem.word(bitsAt) | asl(1, id & 0x1f));
			const uint32_t word = 14 + static_cast<uint32_t>(id) + 0x47 * t;
			_mem.setHalf(_engine + 2 * word, _mem.half(entry + 2));
			_mem.setWord(stateOf(trackWord(t) + static_cast<uint32_t>(id)), snapped(_mem.half(entry + 2)));
		}
	}

	void restoreFxLocks(Memory& _mem, const uint32_t _dst, const uint32_t _src)
	{
		for(uint32_t k = 0; k < 3; ++k)
		{
			const uint32_t bitsAt = FxLockBits + 4 * k;
			for(uint32_t bits; (bits = static_cast<uint32_t>(_mem.word(bitsAt))) != 0;)
			{
				const int32_t b = lowestBit(bits);
				const uint32_t id = static_cast<uint32_t>(b) + 32 * k;
				_mem.setHalf(_dst + 2 * (id + FxWord), _mem.half(_src + 2 * id));
				_mem.setWord(stateOf(id + FxWord), snapped(_mem.half(_src + 2 * id)));
				_mem.setWord(bitsAt, static_cast<int32_t>(bits & ~(1u << b)));
			}
		}
	}

	void restoreLocks(Memory& _mem, const int32_t _t, const uint32_t _dst, const uint32_t _src)
	{
		const auto t = static_cast<uint32_t>(_t);
		for(uint32_t k = 0; k < 3; ++k)
		{
			const uint32_t bitsAt = LockBits + LockStride * t + 4 * k;
			for(uint32_t bits; (bits = static_cast<uint32_t>(_mem.word(bitsAt))) != 0;)
			{
				const int32_t b = lowestBit(bits);
				const uint32_t id = static_cast<uint32_t>(b) + 32 * k;
				const uint32_t from = _src + 2 * id + 0x14;
				_mem.setHalf(_dst + 2 * (14 + id + 0x47 * t), _mem.half(from));
				_mem.setWord(stateOf(trackWord(t) + id), snapped(_mem.half(from)));
				_mem.setWord(bitsAt, _mem.word(bitsAt) & ~asl(1, b));
			}
		}
	}

	void clearLocks(Memory& _mem, const int32_t _t)
	{
		const uint32_t bitsAt = LockBits + LockStride * static_cast<uint32_t>(_t);
		for(uint32_t k = 0; k < 3; ++k)
			_mem.setWord(bitsAt + 4 * k, 0);
	}

	void setParameter(Memory& _mem, const int32_t _index, const int32_t _value, const uint32_t _engine)
	{
		const auto index = static_cast<uint32_t>(_index);
		_mem.setHalf(_engine + 2 * index, _value);
		_mem.setWord(stateOf(index), snapped(_value));
	}

	void snapTrack(Memory& _mem, const int32_t _t, const uint32_t _src)
	{
		// 0x8000e9e8 + 2 (0x8e t): the state of the track's word 14
		const uint32_t state = SmoothState + 0x38 + 2 * TrackBytes * static_cast<uint32_t>(_t);
		for(uint32_t i = 0; i < TrackBytes / 2; ++i)
			_mem.setWord(state + 4 * i, snapped(_mem.half(_src + 2 * i)));
	}

	void snapRange(Memory& _mem, const int32_t _t, const uint32_t _src, const int32_t _first, const int32_t _count)
	{
		uint32_t state = stateOf(trackWord(static_cast<uint32_t>(_t)) + static_cast<uint32_t>(_first));
		uint32_t src = _src;
		for(int32_t i = 0; i != _count; ++i, src += 2, state += 4)
			_mem.setWord(state, snapped(_mem.half(src)));
	}

	void snapParameter(Memory& _mem, const int32_t _t, const int32_t _value, const int32_t _index)
	{
		_mem.setWord(stateOf(trackWord(static_cast<uint32_t>(_t)) + static_cast<uint32_t>(_index)), snapped(_value));
	}
}
