// TRX-RS's voice code, ported as mdVoiceTrx.h says.
//
// TRX-RS ($101AB0, $101AB8, $101ACD), each block: a sine at PTCH / 32, folded back past a threshold either way by a
// level that grows while it stays past, halved; for 24 blocks after a Hit a sampled waveform added; the amp envelope;
// then two high-pass filters in double precision, and twice their output, two samples late.
#include "mdVoiceTrx.h"

#include <algorithm>

namespace md::voice::trx::rs
{
	namespace
	{
		using Block = Voice::Block;
		using testHooks::probe;
	}

	void init(Context& _c)
	{
		auto& y = _c.y;
		y[9] = 0;
		y[0xb] = 0x2000;
		y[0xc] = 0;
	}

	void update(Context& _c)
	{
		auto& y = _c.y;
		hitLevel(y[9], testHooks::RsLevelLimited);
		y[5] = 0x400000;
		y[6] = 0x200;
		// The filters' low words, Y:$13 and Y:$14, stay from the last Hit, as the DSP leaves them
		y[7] = y[8] = y[0xa] = y[0xd] = y[0xe] = y[0xf] = y[0x10] = y[0x11] = y[0x12] = 0;
	}

	void render(Context& _c, Block& _out)
	{
		const auto& t = tables(_c);
		auto& y = _c.y;
		auto& iy = _c.shared.scratch.y;
		auto& ix = _c.shared.scratch.x;
		// The modulo-32 rings: the signal X:$00 (R1), then X:$60 (R4)
		uint32_t* const sig = &ix[0];
		uint32_t* const ring = &ix[0x60];

		// The sine, its step PTCH / 32, the phase masked to a period before each step
		sines(t, y, 0xb, 0xc, acc(y[2]) >> 5, [&](const int _k, const uint32_t _w) { sig[_k] = _w; });

		// The fold: past the threshold Y:$03 either way, the sine's peak less a level that grows by Y:$04 a sample
		// while it stays past (Y:$0A), and back to 0 once inside; then scaled by Y:$05
		{
			int64_t b = acc(y[0xa]);
			const int64_t threshold = acc(y[3]), grow = acc(y[4]);
			const uint32_t scale = y[5];
			for(int k = 0; k < 32; ++k)
			{
				int64_t a = acc(sig[k]);
				if(a >= threshold)
				{
					probe(testHooks::RsFoldAbove);
					a = wrap56(acc(0x7eb852) - b);
					b = wrap56(b + grow);
				}
				else if(-a >= threshold)
				{
					probe(testHooks::RsFoldBelow);
					a = wrap56(acc(0x8147ae) + b);
					b = wrap56(b + grow);
				}
				else
				{
					b = 0;
				}
				sig[k] = lim(mul(lim(a), scale));
			}
			y[0xa] = lim(b);
		}

		// The sampled waveform added, 32 words a block from its start, then the amp envelope, into X:$60
		const uint32_t index = y[8];
		if(acc(index) >= acc(0x300))
		{
			probe(testHooks::RsTail);
			std::copy(sig, sig + 32, ring);
		}
		else
		{
			probe(testHooks::RsWave);
			y[8] = lim(acc(index) + acc(0x20));
			for(uint32_t k = 0; k < 32; ++k)
				ring[k] = lim(acc(sig[k]) + acc(word(t.rsWave, (index + k) & 0xffffff)));
		}
		envelope(ring, y[9], y[1]);

		// Two high-pass filters in turn, 1/2 - z^-1 + 1/2 z^-2 over the poles by Y:$06 and Y:$07, in double precision:
		// each keeps its accumulator from one sample to the next (the last output, its low word at Y:$14 and Y:$13),
		// adds -2 r^2 times the output before and twice 2 r c - 1/2 times the last, and stores each output as a long
		// word. That is TRX-CP's high-pass, 2 (2 r c h1 - r^2 h2), its accumulator kept to 48 bits. The first's last
		// two at X:$42-$43 (and Y), the second's from X:$22 on after its last two
		{
			const auto pl = offsetPole(t, y[6], y[7]);
			const uint32_t a2 = lim(shl(-mul(pl.r, pl.r), 1));
			const uint32_t a1 = lim(wrap56(shl(mul(pl.r, pl.c), 1) + acc(0xc00000)));
			iy[0] = a2;
			highPassTaps(iy);
			const auto feedback = [&](const uint32_t _h2, const uint32_t _h1)
			{
				return mul(a2, _h2) + mul(_h1, a1) + mul(_h1, a1);
			};
			ix[0x5e] = y[0x12];
			ix[0x5f] = y[0x11];
			uint32_t* const out = &ix[0x20];
			out[0] = y[0x10];
			out[1] = y[0xf];
			uint32_t h2 = y[0xe], h1 = y[0xd], h2lo = 0, h1lo = y[0x14];
			uint32_t o2 = y[0x10], o1 = y[0xf], o1lo = y[0x13];
			int64_t a = wrap56(acc(h1, h1lo) + feedback(h2, h1));
			int64_t b = acc(o1, o1lo);
			for(int k = 0; k < 32; ++k)
			{
				a = wrap56(a + highPassZeros(ring[k - 2], ring[k - 1], ring[k]));
				b = wrap56(b + feedback(o2, o1) + highPassZeros(h2, h1, hi(a)));
				uint32_t s, sLo;
				limL(a, s, sLo);
				a = wrap56(a + feedback(h1, s));
				h2 = h1;
				h1 = s;
				h2lo = h1lo;
				h1lo = sLo;
				uint32_t o;
				limL(b, o, o1lo);
				out[k + 2] = o;
				iy[0x22 + k] = o1lo;
				o2 = o1;
				o1 = o;
			}
			// The first's long words alternate between X:$42 and X:$43, the last at $43
			ix[0x42] = h2;
			iy[0x42] = h2lo;
			ix[0x43] = h1;
			iy[0x43] = h1lo;
			y[0xd] = h1;
			y[0x14] = h1lo;
			y[0xe] = h2;
			y[0xf] = o1;
			y[0x13] = o1lo;
			y[0x10] = o2;
			y[0x11] = ring[31];
			y[0x12] = ring[30];
		}

		// Twice the second filter's output, two samples late
		for(int k = 0; k < 32; ++k)
			_out[static_cast<size_t>(k)] = sx(lim(shl(acc(ix[0x20 + k]), 1)));
	}
}
