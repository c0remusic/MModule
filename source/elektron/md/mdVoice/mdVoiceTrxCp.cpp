// TRX-CP's voice code, ported as mdVoiceTrx.h says.
//
// TRX-CP ($10194F, $101966, $101972), each block: a noise through two two-pole filters, at a level that decays a
// block at a time; a second noise, its level raised by a burst while each of three counters runs and decaying after,
// through two high-pass filters, then two two-pole filters; their sum, a quarter of it.
#include "mdVoiceTrx.h"

#include <algorithm>

namespace md::voice::trx::cp
{
	namespace
	{
		using Block = Voice::Block;
		using testHooks::probe;
	}

	void init(Context& _c)
	{
		auto& y = _c.y;
		y[0x15] = 0x29a;
		y[0x16] = 1;
		for(uint32_t w = 0x17; w <= 0x24; ++w)
			y[w] = 0;
	}

	void update(Context& _c)
	{
		auto& y = _c.y;
		y[0x14] = 0;
		y[0xf] = y[2];
		y[0x10] = 0x333333;
		y[0x11] = y[0x12] = y[0x13] = y[3];
	}

	void render(Context& _c, Block& _out)
	{
		const auto& t = tables(_c);
		auto& y = _c.y;
		auto& iy = _c.shared.scratch.y;
		auto& ix = _c.shared.scratch.x;
		// The modulo-32 ring X:$60 (R5): a noise, then the clap's noise; Y:$60 the first noise filtered
		uint32_t* const ring = &ix[0x60];

		// A noise through two two-pole filters by Y:$09 and Y:$0A, kept at Y:$60
		fibonacci(ring, y[0x15], y[0x16]);
		{
			const auto p = poles(offsetPole(t, y[9], y[0xa]));
			iy[0] = p.a2;
			iy[1] = p.g;
			ix[0x5e] = y[0x24];
			ix[0x5f] = y[0x23];
			uint32_t h2 = y[0x21], h1 = y[0x22];
			twoPoles(p, ring, h2, h1);
			ix[0] = h2;
			ix[1] = h1;
			y[0x21] = h2;
			y[0x22] = h1;
			y[0x23] = ring[31];
			y[0x24] = ring[30];
		}
		std::copy(ring, ring + 32, &iy[0x60]);

		// The clap's noise, its level raised by a burst each time a counter (Y:$0C, then $0D, then $0E) runs, by a word
		// the burst then clears, and decaying at the burst's rate; past the three the last word and rate
		fibonacci(ring, y[0x15], y[0x16]);
		{
			int64_t b = acc(y[0x14]);
			uint32_t rate;
			const auto burst = [&](const uint32_t _counter, const uint32_t _add, const uint32_t _rate)
			{
				y[_counter] = lim(acc(y[_counter]) - acc(1));
				b += acc(y[_add]);
				y[_add] = 0;
				rate = y[_rate];
			};
			if(y[0xc] != 0)
			{
				probe(testHooks::CpBurstA);
				burst(0xc, 1, 3);
			}
			else if(y[0xd] != 0)
			{
				probe(testHooks::CpBurstB);
				burst(0xd, 2, 0x11);
			}
			else if(y[0xe] != 0)
			{
				probe(testHooks::CpBurstC);
				burst(0xe, 0xf, 0x12);
			}
			else
			{
				probe(testHooks::CpDecay);
				b += acc(y[0x10]);
				y[0x10] = 0;
				rate = y[0x13];
			}
			if(b > kMax48 || b < kMin48)
				probe(testHooks::CpLevelLimited);
			uint32_t level = lim(b);
			envelope(ring, level, rate);
			y[0x14] = level;
		}

		// The clap's noise through two high-pass filters in turn, 1/2 - z^-1 + 1/2 z^-2 over the poles by Y:$05 and
		// Y:$06, the first's last two outputs at X:$00-$01, the second's written from X:$22 on after its last two
		{
			const auto p = poles(offsetPole(t, y[5], y[6]));
			iy[0] = p.a2;
			highPassTaps(iy);
			ix[0x5e] = y[0x1c];
			ix[0x5f] = y[0x1b];
			ix[0x20] = y[0x1a];
			ix[0x21] = y[0x19];
			uint32_t* const out = &ix[0x22];
			uint32_t h2 = y[0x18], h1 = y[0x17], o2 = y[0x1a], o1 = y[0x19];
			for(int k = 0; k < 32; ++k)
			{
				const int64_t in = highPassZeros(ring[k - 2], ring[k - 1], ring[k]);
				const uint32_t s = lim(shl(mul(p.a2, h2) + mul(p.a1, h1) + in, 1));
				const uint32_t o = lim(shl(mul(p.a2, o2) + mul(p.a1, o1) + highPassZeros(h2, h1, s), 1));
				out[k] = o;
				o2 = o1;
				o1 = o;
				h2 = h1;
				h1 = s;
			}
			ix[0] = h2;
			ix[1] = h1;
			y[0x17] = h1;
			y[0x18] = h2;
			y[0x19] = out[31];
			y[0x1a] = out[30];
			y[0x1b] = ring[31];
			y[0x1c] = ring[30];
		}

		// Two two-pole filters by Y:$07 and Y:$08 over X:$20-$3F: the high-passed noise two samples late
		{
			const auto p = poles(offsetPole(t, y[7], y[8]));
			iy[0] = p.a2;
			iy[1] = p.g;
			uint32_t* const in = &ix[0x20];
			ix[0x1e] = y[0x20];
			ix[0x1f] = y[0x1f];
			uint32_t h2 = y[0x1d], h1 = y[0x1e];
			twoPoles(p, in, h2, h1);
			ix[0] = h2;
			ix[1] = h1;
			y[0x1d] = h2;
			y[0x1e] = h1;
			y[0x1f] = in[31];
			y[0x20] = in[30];
		}

		// The output: the clap plus the filtered noise at a level that decays by Y:$04 a block, a quarter of the sum
		const uint32_t level = y[0xb];
		y[0xb] = lim(mul(level, y[4]));
		for(int k = 0; k < 32; ++k)
			_out[static_cast<size_t>(k)] = sx(lim((acc(ix[0x20 + k]) + mul(level, iy[0x60 + k])) >> 2));
	}
}
