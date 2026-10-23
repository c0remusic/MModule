#include "stMainBus.h"

#include "stEmac.h"

namespace syntakt::main
{
	namespace
	{
		constexpr uint32_t AnalogLevels = 0x401e08e0;	// a long per machine id
	}

	void sumChannels(Memory& _mem, Emac4& _acc, const int _channels, const uint32_t _dst, const uint32_t _src,
		const uint32_t _gains, const int32_t* _steps)
	{
		int32_t gains[10];
		for(int k = 0; k < _channels; ++k)
			gains[k] = _mem.word(_gains + 4 * static_cast<uint32_t>(k));
		for(uint32_t i = 0; i < BlockFrames; ++i)
		{
			for(int k = 0; k < _channels; ++k)
				_acc.msac(0, gains[k], _mem.word(_src + 128 * static_cast<uint32_t>(k) + 4 * i));
			for(int k = 0; k < _channels; ++k)
				gains[k] = addWrap(gains[k], _steps[k]);
			_mem.setWord(_dst + 8 * i, _acc.storeClear(0));
		}
		for(int k = 0; k < _channels; ++k)
			_mem.setWord(_gains + 4 * static_cast<uint32_t>(k), gains[k]);
	}

	void copySide(Memory& _mem, const uint32_t _dst, const uint32_t _src)
	{
		for(uint32_t i = 0; i < BlockFrames; ++i)
			_mem.setWord(_dst + 4 * i, _mem.word(_src + 8 * i));
	}

	void mixBuses(Memory& _mem, Emac4& _acc, const uint32_t _dst, const uint32_t _src, int32_t* _gains,
		const int32_t* _steps)
	{
		for(uint32_t i = 0; i < BlockFrames; ++i)
		{
			// Both sides are read before they are written: _dst may be _src
			const int32_t l = _mem.word(_src + 8 * i), r = _mem.word(_src + 8 * i + 4);
			_acc.msac(0, _gains[0], l);
			_acc.msac(0, _gains[1], _mem.word(_src + 256 + 8 * i));
			_acc.msac(1, _gains[0], r);
			_acc.msac(1, _gains[1], _mem.word(_src + 256 + 8 * i + 4));
			_gains[0] = addWrap(_gains[0], _steps[0]);
			_gains[1] = addWrap(_gains[1], _steps[1]);
			_mem.setWord(_dst + 8 * i, _acc.storeClear(0));
			_mem.setWord(_dst + 8 * i + 4, _acc.storeClear(1));
		}
	}

	void scaleBus(Memory& _mem, Emac4& _acc, const uint32_t _bus, const int32_t _gain)
	{
		const uint32_t macsr = _acc.macsr();
		_acc.setMacsr(Emac4::Omc);
		for(uint32_t i = 0; i < 2 * BlockFrames; i += 4)
		{
			for(int k = 0; k < 4; ++k)
				_acc.mac(k, _gain, _mem.word(_bus + 4 * (i + static_cast<uint32_t>(k))));
			for(int k = 0; k < 4; ++k)
				_mem.setWord(_bus + 4 * (i + static_cast<uint32_t>(k)), _acc.storeClear(k));
		}
		_acc.setMacsr(macsr);
	}

	void rampBuffer(Memory& _mem, Emac4& _acc, const uint32_t _buffer, const int32_t _from, const int32_t _to)
	{
		const int32_t step = subWrap(_to, _from) >> 5;
		int32_t gain = _from;
		for(uint32_t i = 0; i < BlockFrames; i += 4)
		{
			for(int k = 0; k < 4; ++k)
			{
				_acc.msac(k, _mem.word(_buffer + 4 * (i + static_cast<uint32_t>(k))), gain);
				gain = addWrap(gain, step);
			}
			for(int k = 0; k < 4; ++k)
				_mem.setWord(_buffer + 4 * (i + static_cast<uint32_t>(k)), _acc.storeClear(k));
		}
	}

	int32_t analogLevel(const Memory& _mem, Emac4& _acc, const int32_t _machine, const int32_t _word)
	{
		const int32_t level = _mem.word(AnalogLevels + 4 * static_cast<uint32_t>(_machine));
		_acc.msac(0, level, static_cast<int16_t>(_word));
		return _acc.storeClear(0);
	}
}
