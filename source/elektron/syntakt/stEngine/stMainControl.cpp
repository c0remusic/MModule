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

		// A pass for every two engine words, one accumulator each (mac.w on the high and low halves of the target
		// long), over the runs of targets (from the second long), states and outputs (from the long before
		// Smoothed): the output long of a pass is written in the next one. Returns the last pass's
		constexpr uint32_t SmoothPasses = (EngineWords + 1) / 2;

		template<typename Targets, typename States, typename Outs>
		int32_t smoothPasses(const Targets& _targets, const States& _states, const Outs& _outs, int32_t _pair,
			int32_t _target)
		{
			constexpr int32_t TargetWeight = 0x03d70000;
			constexpr int32_t StateWeight = 0x7c290000;
			EmacWrap acc0, acc1;
			for(uint32_t p = 0; p < SmoothPasses; ++p)
			{
				acc0.mac(TargetWeight, static_cast<int32_t>(static_cast<uint32_t>(_target) & 0xffff0000));
				acc0.mac(StateWeight, _states.word(8 * p));
				_outs.setWord(4 * p, _pair);
				acc1.mac(TargetWeight, asl(_target, 16));
				acc1.mac(StateWeight, _states.word(8 * p + 4));
				_target = _targets.word(4 * p);
				const int32_t s0 = acc0.storeClear();
				_states.setWord(8 * p, s0);
				const int32_t s1 = acc1.storeClear();
				_states.setWord(8 * p + 4, s1);
				const auto high = static_cast<uint32_t>(addWrap(s0, 0x8000)) & 0xffff0000;
				const auto low = static_cast<uint32_t>(addWrap(s1, 0x8000)) >> 16;
				_pair = static_cast<int32_t>(high | low);
			}
			return _pair;
		}
	}

	uint32_t smooth(Memory& _mem, const uint32_t _target)
	{
		const uint32_t target = _target + 4, out = Smoothed - 4;
		const int32_t first = _mem.word(out), targets = _mem.word(_target);

		// On the three runs' bytes where the memory's table holds each whole (SRAM, normally), else through the memory
		const uint8_t* targetAt = _mem.bytesAt(target, 4 * SmoothPasses);
		uint8_t* stateAt = _mem.writableBytesAt(SmoothState, 8 * SmoothPasses);
		uint8_t* outAt = _mem.writableBytesAt(out, 4 * (SmoothPasses + 1));
		int32_t pair;
		if(targetAt && stateAt && outAt)
			pair = smoothPasses(BytesRun<const uint8_t>{targetAt}, BytesRun<uint8_t>{stateAt}, BytesRun<uint8_t>{outAt},
				first, targets);
		else
			pair = smoothPasses(MemoryRun{_mem, target}, MemoryRun{_mem, SmoothState}, MemoryRun{_mem, out}, first,
				targets);
		_mem.setWord(out + 4 * SmoothPasses, pair);

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
