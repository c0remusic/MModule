#include "stMainFilter.h"

#include "stEmac.h"

namespace syntakt::main
{
	namespace
	{
		constexpr uint32_t Coefficients = 0x401decc0;	// decay and release, by the parameter's value
		constexpr uint32_t Increments = 0x401deec0;		// attack, by ATK >> 8
		constexpr uint32_t Delays = 0x401deac0;			// delay length in samples, by DEL's value
		constexpr int32_t Unit = 0x40810204;			// 64/127: a word of 0x7f00 << 16, times 2, is full scale
		constexpr int32_t KeyScale = 0x483244b3;

		enum Stage
		{
			Release,
			Decay,
			Attack,
			Delay
		};

		uint32_t entry(const uint32_t _table, const int32_t _index)
		{
			return _table + 4 * static_cast<uint32_t>(_index);
		}

		// msac then movclr, MACSR 0x20
		int32_t negFrac(const int32_t _y, const int32_t _x)
		{
			EmacWrap acc;
			acc.msac(_y, _x);
			return acc.storeClear();
		}
	}

	void filterEnvelopes(Memory& _mem, const uint32_t _p, const int32_t _trigs, const int32_t _gateOffs,
		const int32_t _mute)
	{
		const int32_t mute = static_cast<int8_t>(_mute);
		for(uint32_t t = 0; t < 8; ++t)
		{
			if(mute >> t & 1)
				continue;
			const uint32_t params = _p + TrackBytes * t + 0x5c;	// ATK, the others around it
			const uint32_t stage = FilterEnvelopes + 12 * t;
			const uint32_t value = stage + 4;
			const uint32_t counter = stage + 8;
			if(_trigs >> t & 1)
			{
				_mem.setWord(counter, 0);
				_mem.setWord(stage, _mem.half(params - 2) == 0 ? Attack : Delay);
				if(_mem.half(params + 8) != 0)
					_mem.setWord(value, 0);
				continue;
			}
			if(_gateOffs >> t & 1)
				_mem.setWord(stage, Release);

			switch(_mem.word(stage))
			{
			case Delay:
			{
				const int32_t c = addWrap(_mem.word(counter), 0x20);
				_mem.setWord(counter, c);
				if(c >= _mem.word(entry(Delays, static_cast<int8_t>(_mem.byte(params - 2)))))
					_mem.setWord(stage, Attack);
				break;
			}
			case Attack:
			{
				const int32_t v = addSat(_mem.word(value), _mem.word(entry(Increments, _mem.half(params) >> 8)));
				_mem.setWord(value, v);
				if(v == INT32_MIN)
					_mem.setWord(stage, Decay);
				break;
			}
			case Decay:
			{
				const int32_t c = _mem.word(entry(Coefficients, static_cast<int8_t>(_mem.byte(params + 2))));
				int32_t sustain = negFrac(Unit, asl(_mem.half(params + 4), 16));
				sustain = addSat(sustain, sustain);
				EmacWrap acc;
				acc.msac(c, _mem.word(value));
				acc.msac(subWrap(INT32_MIN, c), sustain);
				_mem.setWord(value, acc.storeClear());
				break;
			}
			default:
			{
				const int32_t c = _mem.word(entry(Coefficients, static_cast<int8_t>(_mem.byte(params + 6))));
				_mem.setWord(value, negFrac(c, _mem.word(value)));
				break;
			}
			}
		}
	}

	int32_t filterEnvelope(const Memory& _mem, const int32_t _t)
	{
		return _mem.word(FilterEnvelopes + 4 + 12 * static_cast<uint32_t>(_t));
	}

	int32_t cutoff(Memory& _mem, const uint32_t _filter, const int32_t _keyTrack, const int32_t _pitch,
		const int32_t _t)
	{
		const int32_t freq = asl(_mem.half(_filter + 2), 16);
		const int32_t depth = asl(addWrap(_mem.half(_filter + 6), -0x4000), 17);
		int32_t key = frac(asl(addWrap(_pitch, static_cast<int32_t>(0xffd00000)), 8), asl(_keyTrack, 24));
		key = frac(KeyScale, addSat(key, key));
		const int32_t c = addSat(freq, key);

		Emac acc;
		acc.msac(filterEnvelope(_mem, _t), depth);
		const int32_t amount = acc.storeClear();
		_mem.setWord(EnvelopeAmounts + 4 * static_cast<uint32_t>(_t), amount);
		int32_t r = addSat(c, amount);
		if(r < 0)
			r = 0;
		return r > 0x7f000000 ? 0x7f000000 : r;
	}
}
