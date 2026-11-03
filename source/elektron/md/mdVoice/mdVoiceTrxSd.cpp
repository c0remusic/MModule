// TRX-SD's voice code, ported as mdVoiceTrx.h says.
//
// TRX-SD ($10074C, $10075B, $100776), each block: two sines swept by a pitch envelope that rises for eleven blocks
// after a Hit and decays after, mixed with a cross term and scaled by the amp envelope; a noise of two words that
// add as a Fibonacci sequence, through two identical two-pole filters whose coefficients a packet word picks in two
// tables, scaled by its own envelope and added; then, for 128 blocks after a Hit, a sampled waveform added.
#include "mdVoiceTrx.h"

namespace md::voice::trx::sd
{
	namespace
	{
		using Block = Voice::Block;
		using testHooks::probe;
	}

	void init(Context& _c)
	{
		auto& y = _c.y;
		y[0x17] = 0x29a;
		y[0x18] = 1;
		y[0x19] = y[0x1a] = y[0x1b] = y[0x1c] = 0;
	}

	void update(Context& _c)
	{
		auto& y = _c.y;
		hitLevel(y[0xe], testHooks::SdLevelLimited);
		y[0xd] = 0x140;
		y[0x1d] = 0x7eb852;
		// The noise's level grows with the last sample's magnitude
		const int64_t last = acc(y[0x16]);
		y[0x10] = 0;
		y[0xf] = lim(acc(0x4ccccd) + shl(last < 0 ? -last : last, 4));
		y[0x11] = y[0x12] = y[0x13] = y[0x14] = y[0x15] = 0;
	}

	void render(Context& _c, Block& _out)
	{
		const auto& t = tables(_c);
		auto& y = _c.y;
		auto& iy = _c.shared.scratch.y;
		auto& ix = _c.shared.scratch.x;
		// The modulo-32 rings: the tone X:$40 (R0, R3), the second sine then the noise X:$20 (R1)
		uint32_t* const tone = &ix[0x40];
		uint32_t* const noise = &ix[0x20];

		// The two sines: each sample the table's word at the phase, then the step added and the phase masked to a
		// period. The steps from PTCH and the pitch envelope Y:$10, as it was before this block
		const auto step = [&](const uint32_t _ptch) { return ((acc(_ptch) >> 1) + acc(y[0x10])) >> 4; };
		const auto sines = [&](const int64_t _step, const uint32_t _amp, const uint32_t _hiAddr, const uint32_t _loAddr,
			uint32_t* const _dst)
		{
			uint64_t b = static_cast<uint64_t>(acc(y[_hiAddr], y[_loAddr])) & 0xffffffffffffull;
			const int64_t x = acc(hi(_step), lo(_step));
			for(int k = 0; k < 32; ++k)
			{
				_dst[k] = lim(mul(word(t.sine, static_cast<uint32_t>(b >> 24)), _amp));
				b = add48(b, x) & 0x007fffffffffull;
			}
			y[_hiAddr] = static_cast<uint32_t>(b >> 24) & 0xffffff;
			y[_loAddr] = static_cast<uint32_t>(b) & 0xffffff;
		};
		sines(step(y[7]), y[3], 0x12, 0x13, tone);
		const int64_t stepB = step(y[8]);
		if(acc(y[0x11]) > acc(y[0xd]))
		{
			probe(testHooks::SdPitchDecay);
			y[0x10] = lim(mul(y[0xc], y[0x10]));
		}
		else
		{
			probe(testHooks::SdPitchRise);
			y[0x10] = lim(shl(acc(y[0xa]), 1) + acc(y[0x10]));
		}
		sines(stepB, y[4], 0x14, 0x15, noise);

		// Their sum, plus their sum scaled by a decaying cross level, then the amp envelope
		{
			const uint32_t x0 = y[0x1d];
			y[0x1d] = lim(mul(y[9], x0));
			for(int k = 0; k < 32; ++k)
			{
				const int64_t b = shl(mul(tone[k], x0) + mul(x0, noise[k]), 6);
				tone[k] = lim(acc(tone[k]) + acc(noise[k]) + b);
			}
		}
		envelope(tone, y[0xe], y[1]);

		fibonacci(noise, y[0x17], y[0x18]);

		// Two two-pole filters in turn by Y:$0B, its angle 3 and its radius $241 below. The first keeps its last two
		// outputs at X:$02-$03, the second reads its own from X:$1E on, in place over the noise; Y:$00-$01 hold g and
		// a2, as the DSP leaves them
		{
			const uint32_t index = y[0xb];
			const auto p = poles(pole(t, (index - 3) & 0xffffff, (index - 0x241) & 0xffffff));
			iy[0] = p.g;
			iy[1] = p.a2;
			ix[0x1e] = y[0x1b];
			ix[0x1f] = y[0x1c];
			uint32_t h2 = y[0x19], h1 = y[0x1a];
			twoPoles(p, noise, h2, h1);
			ix[3] = h2;
			ix[2] = h1;
			y[0x19] = h2;
			y[0x1a] = h1;
			y[0x1b] = noise[30];
			y[0x1c] = noise[31];
		}

		// The noise's envelope, then the noise added to the tone at its level
		{
			envelope(noise, y[0xf], y[2]);
			const uint32_t level = y[6];
			for(int k = 0; k < 32; ++k)
				tone[k] = lim(acc(tone[k]) + mul(noise[k], level));
		}

		// The sampled waveform, 32 words a block from its start, then the tone alone
		const uint32_t index = y[0x11];
		y[0x11] = lim(acc(index) + acc(0x20));
		if(acc(index) > acc(0xfe0))
		{
			probe(testHooks::SdTail);
			for(int k = 0; k < 32; ++k)
				_out[static_cast<size_t>(k)] = sx(tone[k]);
		}
		else
		{
			probe(testHooks::SdWave);
			for(uint32_t k = 0; k < 32; ++k)
				_out[k] = sx(lim(acc(tone[k]) + mul(word(t.sdWave, (index + k) & 0xffffff), y[5])));
		}
		y[0x16] = static_cast<uint32_t>(_out[31]) & 0xffffff;
	}
}
