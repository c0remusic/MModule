// TRX-XT's voice code, ported as mdVoiceTrx.h says.
//
// TRX-XT ($101882, $101883, $101891), each block: a sine swept by a decaying pitch envelope, whose step takes the last
// sine word read as its low word, scaled by the amp envelope, then a clipper whose levels move by a step past its two
// thresholds. The block after a Hit fades the last block's last sample out (x 0.9 a sample), the two after it play a
// sampled waveform each, scaled by a packet word.
#include "mdVoiceTrx.h"

namespace md::voice::trx::xt
{
	namespace
	{
		using Block = Voice::Block;
		using testHooks::probe;
	}

	void init(Context&) {}

	void update(Context& _c)
	{
		auto& y = _c.y;
		y[0xb] = 3;
		y[0xa] = 0x7ffcb9;
		y[0xe] = y[0xf] = 0;
		y[0xc] = y[4];
		y[0xd] = y[5];
	}

	void render(Context& _c, Block& _out)
	{
		const auto& t = tables(_c);
		auto& y = _c.y;
		auto& ix = _c.shared.scratch.x;
		// The modulo-32 ring X:$20 (R1): the sine, then the signal
		uint32_t* const sig = &ix[0x20];

		// The three blocks after a Hit: the last sample faded out, then the two sampled waveforms
		if(y[0xb] != 0)
		{
			const int64_t a = acc(y[0xb]) - acc(1);
			y[0xb] = lim(a);
			if(a != 0 && a != acc(1))
			{
				probe(testHooks::XtFade);
				fadeOut(y[0x10], _out);
				return;
			}
			probe(a == 0 ? testHooks::XtWaveB : testHooks::XtWaveA);
			const uint32_t* const wave = &t.xtWave[a == 0 ? 32 : 0];
			for(size_t k = 0; k < 32; ++k)
				_out[k] = sx(lim(mul(wave[k], y[9])));
			return;
		}

		// The sine, its step from PTCH and the pitch envelope. Past the first sample the step's low word is the last
		// sine word read: the loop loads it into X0, which the add uses
		{
			const int64_t step = wrap56((acc(y[2]) >> 6) + mul(y[0xa], y[3]));
			y[0xa] = lim(mul(y[0xa], y[7]));
			uint64_t b = static_cast<uint64_t>(acc(y[0xe], y[0xf])) & 0xffffffffffffull;
			const uint32_t x1 = hi(step);
			uint32_t x0 = lo(step);
			for(int k = 0; k < 32; ++k)
			{
				b &= 0x007fffffffffull;
				const uint32_t index = static_cast<uint32_t>(b >> 24);
				b = add48(b, acc(x1, x0));
				x0 = t.sine[index];
				sig[k] = x0;
			}
			y[0xe] = static_cast<uint32_t>(b >> 24) & 0xffffff;
			y[0xf] = static_cast<uint32_t>(b) & 0xffffff;
		}

		envelope(sig, y[8], y[1]);

		// The clipper: from its upper threshold up the inverted signal holds the lower one's level less a step, from
		// the lower down the upper's plus a step, and those levels move (Y:$0C, Y:$0D). Unlike TRX-BD's, the signal is
		// not halved, a threshold reached counts as past, and the thresholds stay
		{
			const uint32_t x0 = y[4], x1 = y[5];
			ix[4] = x0;
			ix[5] = x1;
			uint32_t y0 = y[0xc], y1 = y[0xd];
			const int64_t a = acc(y[6]);
			for(int k = 0; k < 32; ++k)
			{
				int64_t b = -acc(sig[k]);
				if(b >= acc(x0))
				{
					probe(b == acc(x0) ? testHooks::XtClipAt : testHooks::XtClipAbove);
					b = acc(y0) - a;
					y1 = x1;
					y0 = lim(b);
				}
				else if(b <= acc(x1))
				{
					probe(testHooks::XtClipBelow);
					b = acc(y1) + a;
					y0 = x0;
					y1 = lim(b);
				}
				_out[static_cast<size_t>(k)] = sx(lim(b));
			}
			y[0x10] = static_cast<uint32_t>(_out[31]) & 0xffffff;
			y[0xc] = y0;
			y[0xd] = y1;
		}
	}
}
