// TRX-B2's voice code, ported as mdVoiceTrx.h says.
//
// TRX-B2 ($102C31, $102C4A, $102C5A), each block: a 23-bit phase, stepped by Y:$01 plus a pitch envelope that decays by
// Y:$0D, read through the sine by a shape that bends each quarter of the phase by its own word (Y:$05 to Y:$08) and
// holds it at a quarter of the sine's period. The sine loses its low bits (Y:$09, after adding Y:$0A), then an amp
// envelope that rises by Y:$10 to one half and falls by a rate that drops once a slower level (Y:$18) runs out. To that
// a sparse noise, a word every four, at a level that decays by Y:$12, and a click that decays by 0.9 a sample.
#include "mdVoiceTrx.h"

#include <algorithm>

namespace md::voice::trx::b2
{
	namespace
	{
		using Block = Voice::Block;
		using testHooks::probe;

		// The sine's word at an offset as the DSP addresses it, R2 at the table's start, M2 its period: the offset
		// wraps once at most, and not at all when it is a multiple of the period, which reads the memory outside the
		// table
		uint32_t sineAt(const Context& _c, const TrxTables& _t, const uint32_t _offset)
		{
			int32_t r = sx(_offset);
			if(static_cast<uint32_t>(r) >= 0x8000 && (r & 0x7fff))
			{
				probe(testHooks::B2SineWrapped);
				r += r < 0 ? 0x8000 : -0x8000;
			}
			if(static_cast<uint32_t>(r) < 0x8000)
				return _t.sine[static_cast<size_t>(r)];
			probe(testHooks::B2SineOutside);
			return external(_c, (TrxTables::SineAddr + r) & 0xffffff);
		}

		// The word of the phase's quarter that bends it, past the DSP's floor of -$1FFF, which holds the offset a
		// quarter of the period from where the quarter starts
		int64_t bend(const uint32_t _word, const uint32_t _phase)
		{
			return std::max(mul(_word, _phase), acc(0xffe001));
		}

		// The phase through the shape: an offset into the sine by the phase's quarter
		uint32_t shape(const Voice::State& _y, const uint32_t _phase)
		{
			const uint32_t quarter = _phase & 0x600000;
			if(quarter == 0)
			{
				probe(testHooks::B2QuarterA);
				return hi(-bend(_y[5], _phase));
			}
			if(quarter == 0x200000)
			{
				probe(testHooks::B2QuarterB);
				return hi(bend(_y[6], _phase - 0x400000) + acc(0x4000));
			}
			if(quarter == 0x400000)
			{
				probe(testHooks::B2QuarterC);
				return hi(-bend(_y[7], _phase - 0x400000) + acc(0x4000));
			}
			probe(testHooks::B2QuarterD);
			return hi(bend(_y[8], _phase - 0x800000) + acc(0x8000));
		}
	}

	void init(Context& _c)
	{
		auto& y = _c.y;
		y[0xd] = 0x7fef9e;
		y[0x12] = 0x75c28f;
		y[0x13] = 0x12e70c;
		y[0x14] = 0x1914a5;
		y[0x15] = y[0x16] = y[0x19] = 0;
	}

	void update(Context& _c)
	{
		auto& y = _c.y;
		y[0xe] = y[2];
		y[0x10] = 0x147ae;
		y[0x11] = y[0xb];
		y[0x17] = y[0xc];
		y[0xf] = 0x7fffff;
		y[0x18] = 0x7fffff;
	}

	void render(Context& _c, Block& _out)
	{
		const auto& t = tables(_c);
		auto& y = _c.y;
		auto& ix = _c.shared.scratch.x;

		// The phase, X:$40-$5F, modulo 2^23
		{
			uint32_t phase = y[0x19], pitch = y[0xe];
			for(uint32_t k = 0; k < 32; ++k)
			{
				pitch = lim(mul(y[0xd], pitch));
				phase = (phase + y[1] + pitch) & 0x7fffff;
				ix[0x40 + k] = phase;
			}
			y[0xe] = pitch;
			y[0x19] = phase;
		}

		// The sine through the shape, X:$00-$1F
		for(uint32_t k = 0; k < 32; ++k)
			ix[k] = sineAt(_c, t, shape(y, ix[0x40 + k]));

		// Its low bits lost, back in X:$40-$5F. The DSP's shifts take six bits of Y:$09
		{
			const uint32_t shift = y[9] & 0x3f;
			for(uint32_t k = 0; k < 32; ++k)
			{
				const int64_t b = (acc(ix[k]) + acc(y[0xa])) >> shift & ~int64_t{0xffffff};
				ix[0x40 + k] = lim(shl(b, shift));
			}
		}

		// The noise, a word every four of X:$20-$3F: two words that add as a Fibonacci pair
		{
			uint32_t prev = y[0x13], cur = y[0x14];
			for(uint32_t k = 0; k < 32; k += 4)
			{
				const uint32_t next = (prev + cur) & 0xffffff;
				ix[0x20 + k] = next;
				ix[0x21 + k] = ix[0x22 + k] = ix[0x23 + k] = 0;
				prev = cur;
				cur = next;
			}
			y[0x13] = prev;
			y[0x14] = cur;
		}

		y[0x11] = lim(mul(y[0x11], y[0x12]));

		// The slower level picks the amp envelope's rate
		const int64_t slow = mul(y[0x18], y[4]);
		y[0x18] = lim(slow);
		uint32_t rate = 0x7ffff8;
		if(slow < acc(0x147ae))
		{
			probe(testHooks::B2RateLow);
			rate = y[3];
		}

		// The amp envelope, which adds Y:$10 a sample until it reaches one half
		{
			uint32_t level = y[0x15];
			for(uint32_t k = 0; k < 32; ++k)
			{
				const int64_t b = mul(level, rate) + acc(y[0x10]);
				if(b >= acc(0x400000))
				{
					probe(testHooks::B2RiseEnd);
					y[0x10] = 0;
				}
				level = lim(b);
				ix[0x40 + k] = lim(mul(ix[0x40 + k], level));
			}
			y[0x15] = level;
		}

		// The output, with the noise at its level and the click, which decays by 0.9 a sample
		{
			uint32_t click = y[0x17];
			for(uint32_t k = 0; k < 32; ++k)
			{
				const int64_t a = mul(click, 0x733333);
				click = lim(a);
				_out[k] = sx(lim(acc(ix[0x40 + k]) + shl(a, 3) + mul(ix[0x20 + k], y[0x11])));
			}
			y[0x17] = click;
		}
	}
}
