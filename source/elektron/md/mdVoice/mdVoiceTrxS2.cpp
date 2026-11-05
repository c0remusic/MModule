// TRX-S2's voice code, ported as mdVoiceTrx.h says.
//
// TRX-S2 ($102D06, $102D31, $102D49): the first block after a Hit is a ramp alone, with what X:$A0-$BF held. Each block
// after it: two sines whose pitches move fast for the first Y:$34 blocks, then decay; a burst, a level that noise
// words past Y:$12 raise and that decays by Y:$3A a sample, scaled by the noise, held two samples at a time with noise
// added, through two high-pass filters and two two-pole filters, at a level that builds for Y:$3C blocks and falls.
// The sines are halved and added to the ramp, the amp envelope Y:$35 scales the result, and that plus the burst is
// doubled.
#include "mdVoiceTrx.h"

#include <utility>

namespace md::voice::trx::s2
{
	namespace
	{
		using Block = Voice::Block;
		using testHooks::probe;

		// The block count from the Hit past a word of the state
		bool past(const Voice::State& _y, const uint32_t _word)
		{
			return acc(_y[0x26]) > acc(_y[_word]);
		}

		// Sine _i, 0 or 1, into _dst at its level. Its step comes from a word plus a sweep; the sweep rises by twice a
		// word for the first Y:$34 blocks, then decays by another
		void sine(Context& _c, const TrxTables& _t, const uint32_t _i, uint32_t* const _dst)
		{
			auto& y = _c.y;
			auto& sweep = y[0x15 + _i];
			const int64_t step = ((acc(y[7 + _i]) >> 1) + acc(sweep)) >> 4;
			if(past(y, 0x34))
			{
				probe(testHooks::S2SweepDecay);
				sweep = lim(mul(y[0xe + _i], sweep));
			}
			else
			{
				probe(testHooks::S2SweepRise);
				sweep = lim(shl(acc(y[0xa + _i]), 1) + acc(sweep));
			}
			const uint32_t level = y[3 + _i];
			const uint32_t phase = 0x18 + 2 * _i;
			sinesMaskedAfter(_t, y, phase, phase + 1, step,
				[&](const int _k, const uint32_t _w) { _dst[_k] = lim(mul(_w, level)); });
		}

		// The burst, X:$A0-$BF: Y:$00-$5F hold the noise
		void burst(Context& _c, uint32_t* const _b)
		{
			auto& y = _c.y;
			const auto& iy = _c.shared.scratch.y;

			const int64_t threshold = acc(y[0x12]);
			int64_t a = acc(y[0x38]);
			for(uint32_t k = 0; k < 32; ++k)
			{
				if(acc(iy[k]) > threshold)
				{
					probe(testHooks::S2Burst);
					a += acc(y[0x39]);
				}
				_b[k] = lim(a);
				a = mul(_b[k], y[0x3a]);
			}
			y[0x38] = lim(a);

			// Every other word, scaled by the noise and held, with noise added at Y:$09, halved
			for(uint32_t k = 0; k < 16; ++k)
			{
				const uint32_t w = lim(mul(iy[0x20 + 2 * k], _b[2 * k]));
				_b[2 * k] = _b[2 * k + 1] = lim((acc(w) + mul(y[9], iy[0x40 + k])) >> 1);
			}
		}
	}

	void init(Context& _c)
	{
		auto& y = _c.y;
		y[0x1d] = 0x29a;
		y[0x1e] = 1;
		y[0x36] = y[0x37] = 0;
		y[0x27] = 0x7ffb11;
		y[0x3b] = 0x7fa33a;
		y[0x28] = 0x7ce567;
		y[0x38] = 0x7eb852;
		y[0x39] = 0x0a3d71;
		y[0x3a] = 0x79999a;
		y[0xe] = 0x74cb2b;
		y[0xf] = 0x75fc33;
		y[0x34] = 0xc;
		y[0x3c] = 0x20;
	}

	void update(Context& _c)
	{
		auto& y = _c.y;
		y[0x25] = y[0x17] = 0;
		y[0x35] = 0x50c335;
		y[0x15] = y[0x16] = y[0x26] = y[0x33] = 0;
		y[0x18] = y[0x19] = y[0x1a] = y[0x1b] = 0;
	}

	void render(Context& _c, Block& _out)
	{
		const auto& t = tables(_c);
		auto& y = _c.y;
		auto& ix = _c.shared.scratch.x;
		auto& iy = _c.shared.scratch.y;
		uint32_t* const low = &ix[0x60];
		uint32_t* const high = &ix[0x40];
		uint32_t* const noisy = &ix[0xa0];

		if(!past(y, 0x33))
		{
			// The first block: the ramp alone. X:$A0-$BF hold what they held, the last voice's words
			probe(testHooks::S2First);
			uint32_t ramp = y[0x25];
			for(uint32_t k = 0; k < 32; ++k)
			{
				const int64_t a = mul(y[0x28], ramp) + acc(y[6]);
				ramp = lim(a);
				low[k] = lim(shl(a, 1));
			}
			y[0x25] = ramp;
		}
		else
		{
			sine(_c, t, 0, low);
			sine(_c, t, 1, high);

			// The noise, Y:$00-$5F: two words that add as a Fibonacci pair, the two stored swapped
			fibonacci(&iy[0x00], y[0x1d], y[0x1e]);
			fibonacci(&iy[0x20], y[0x1d], y[0x1e]);
			fibonacci(&iy[0x40], y[0x1d], y[0x1e]);
			std::swap(y[0x1d], y[0x1e]);

			burst(_c, noisy);

			highPassStage(_c, poles(offsetPole(_c, y[1], y[0x10])), 0xa0, 0x00, 0xc2, 0x29);
			twoPoleStage(_c, poles(offsetPole(_c, y[0x11], y[0x13])), 0xc2, 0x2f, true);

			// The burst's level: it builds for Y:$3C blocks, and decays by a rate that changes past Y:$14
			{
				int64_t a = acc(y[0x17]);
				if(!past(y, 0x3c))
				{
					probe(testHooks::S2BurstBuild);
					a += acc(y[0xc]);
				}
				uint32_t rate = y[0x3b];
				if(past(y, 0x14))
				{
					probe(testHooks::S2BurstRateLate);
					rate = y[0xd];
				}
				y[0x17] = lim(mul(rate, lim(a)));
				for(uint32_t k = 0; k < 32; ++k)
					noisy[k] = lim(mul(y[0x17], ix[0xc2 + k]));
			}

			// The sines halved, on the ramp, which decays by Y:$28
			uint32_t ramp = y[0x25];
			for(uint32_t k = 0; k < 32; ++k)
			{
				const int64_t b = mul(y[0x28], ramp);
				ramp = lim(b);
				low[k] = lim((acc(low[k]) >> 1) + (acc(high[k]) >> 1) + shl(b, 1));
			}
			y[0x25] = ramp;
		}

		// The amp envelope, then twice the sum with the burst
		uint32_t rate = y[2];
		if(!past(y, 5))
		{
			probe(testHooks::S2RateEarly);
			rate = y[0x27];
		}
		y[0x26] = lim(acc(y[0x26]) + acc(1));
		envelope(low, y[0x35], rate);
		uint32_t last = 0;
		for(uint32_t k = 0; k < 32; ++k)
		{
			last = lim(shl(acc(low[k]) + acc(noisy[k]), 1));
			_out[k] = sx(last);
		}
		y[0x1c] = last;
	}
}
