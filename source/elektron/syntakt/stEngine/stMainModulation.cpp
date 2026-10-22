#include "stMainModulation.h"

#include "stEmac.h"

namespace syntakt::main
{
	namespace
	{
		constexpr int32_t Bias = 0x40000000;

		// The product of a list's pair: msac.w then movclr, so its negation
		int32_t negatedProduct(const int32_t _source, const int32_t _pair)
		{
			EmacWrap acc;
			acc.msac(asl(_source, 16), static_cast<int32_t>(static_cast<uint32_t>(_pair) & 0xffff0000));
			return acc.storeClear();
		}

		// The word _params[id] minus _negated, saturated: the word less 0x4000 in the high half, then the bias back
		void apply(Memory& _mem, const uint32_t _params, const int32_t _pair, const int32_t _negated)
		{
			const uint32_t at = _params + 2 * static_cast<uint32_t>(static_cast<int16_t>(_pair));
			int32_t v = subWrap(asl(static_cast<uint16_t>(_mem.half(at)), 16), Bias);
			v = subSat(v, _negated);
			v = addSat(v, v) >> 1;
			_mem.setHalf(at, addWrap(v, Bias) >> 16);
		}
	}

	void modulate(Memory& _mem, const uint32_t _params, const int32_t _source, const uint32_t _list)
	{
		// msac.w reads the next pair (a fifth after the list) before the word is written
		int32_t pair = _mem.word(_list);
		for(uint32_t k = 1; k <= 4; ++k)
		{
			const int32_t next = _mem.word(_list + 4 * k);
			apply(_mem, _params, pair, negatedProduct(_source, pair));
			pair = next;
		}
	}

	void modulationMatrix(Memory& _mem, const uint32_t _p)
	{
		const uint32_t settings = _p + 2 * EngineWords;
		for(uint32_t t = 0; t < 12; ++t)
			modulate(_mem, _p + 0x1c + TrackBytes * t, _mem.half(Velocity + 2 * t), settings + SettingsStride * t);

		for(uint32_t t = 0; t < 13; ++t)
		{
			const uint32_t params = _p + 0x1c + TrackBytes * t;
			const uint32_t lists = settings + SettingsStride * t;
			modulate(_mem, params, _mem.half(PitchBend + 2 * t), lists + 0x10);
			modulate(_mem, params, _mem.half(Source472c + 2 * t), lists + 0x20);
			modulate(_mem, params, _mem.half(Source4712 + 2 * t), lists + 0x30);
			modulate(_mem, params, _mem.half(Source46f8 + 2 * t), lists + 0x40);

			// The note in Q8 from 60: its products saturate as they count 16 times
			const int32_t note = addWrap(_mem.word(Notes + 4 * t) >> 8, -0x3c00);
			int32_t pair = _mem.word(lists + 0x50);
			for(uint32_t k = 1; k <= 4; ++k)
			{
				const int32_t next = _mem.word(lists + 0x50 + 4 * k);
				int32_t negated = negatedProduct(note, pair);
				for(int i = 0; i < 4; ++i)
					negated = addSat(negated, negated);
				apply(_mem, params, pair, negated);
				pair = next;
			}
		}
	}
}
