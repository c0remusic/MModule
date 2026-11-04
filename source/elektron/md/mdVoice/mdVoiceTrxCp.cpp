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
		twoPoleStage(_c, poles(offsetPole(t, y[9], y[0xa])), 0x60, 0x21);
		std::copy(ring, ring + 32, &iy[0x60]);

		// The clap's noise, its level raised by a burst each time a counter (Y:$0C, then $0D, then $0E) runs, by a word
		// the burst then clears, and decaying at the burst's rate; past the three the last word and rate
		fibonacci(ring, y[0x15], y[0x16]);
		{
			int64_t b = acc(y[0x14]);
			uint32_t rate;
			const auto burst = [&](const uint32_t _add, const uint32_t _rate)
			{
				b += acc(y[_add]);
				y[_add] = 0;
				rate = y[_rate];
			};
			if(countDown(y[0xc]))
			{
				probe(testHooks::CpBurstA);
				burst(1, 3);
			}
			else if(countDown(y[0xd]))
			{
				probe(testHooks::CpBurstB);
				burst(2, 0x11);
			}
			else if(countDown(y[0xe]))
			{
				probe(testHooks::CpBurstC);
				burst(0xf, 0x12);
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
		highPassStage(_c, poles(offsetPole(t, y[5], y[6])), 0x60, 0x00, 0x20, 0x17);

		// Two two-pole filters by Y:$07 and Y:$08 over X:$20-$3F: the high-passed noise two samples late
		twoPoleStage(_c, poles(offsetPole(t, y[7], y[8])), 0x20, 0x1d);

		// The output: the clap plus the filtered noise at a level that decays by Y:$04 a block, a quarter of the sum
		const uint32_t level = y[0xb];
		y[0xb] = lim(mul(level, y[4]));
		for(int k = 0; k < 32; ++k)
			_out[static_cast<size_t>(k)] = sx(lim((acc(ix[0x20 + k]) + mul(level, iy[0x60 + k])) >> 2));
	}
}
