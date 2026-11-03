// TRX-BD's voice code, ported as mdVoiceTrx.h says.
//
// TRX-BD ($1003DA, $1003E1, $100408), each block: a noise of two words that add as a Fibonacci sequence (Y:$00-$1F),
// scaled by a decaying level; for the eight blocks after the one that follows a Hit, the attack, two sampled
// waveforms weighted by two packet words plus the noise; otherwise two sines from the sine table, the second an
// octave below, both swept down by a decaying pitch envelope and the second scaled by its own decay, then a clipper
// whose two thresholds move by a step each block, the amp envelope (with the noise mixed in), a shaper that bends the
// signal past two thresholds, and the output, twice the shaped signal plus the unshaped one. The block after a Hit
// fades the last block's last sample out (x 0.9 a sample) instead.
#include "mdVoiceTrx.h"

namespace md::voice::trx::bd
{
	namespace
	{
		using Block = Voice::Block;
		using testHooks::probe;
	}

	void init(Context& _c)
	{
		_c.y[0x1c] = 0;
		_c.y[0x1d] = 1;
	}

	void update(Context& _c)
	{
		auto& y = _c.y;
		hitLevel(y[0x11], testHooks::BdLevelLimited);
		y[0x12] = 0xa;
		y[0x13] = 0x7ffcb9;
		y[0x16] = y[0x17] = y[0x19] = y[0x1f] = y[0x20] = 0;
		y[0x1a] = y[0x14] = y[6];
		y[0x1b] = y[0x15] = y[7];
		y[0x1e] = y[0xb];
		y[0x21] = y[0xd];
		y[0xf] = 0x7ffcb9;
		y[0x10] = 0x32;
		y[0xe] = 0x7fdbb4;
	}

	void render(Context& _c, Block& _out)
	{
		const auto& t = tables(_c);
		auto& y = _c.y;
		auto& iy = _c.shared.scratch.y;
		auto& ix = _c.shared.scratch.x;
		// The modulo-32 rings: noise Y:$00 (R5), the sines Y:$20 (R1), the signal X:$20 (R4), the shaped X:$40 (R3)
		uint32_t* const noise = &iy[0];
		uint32_t* const low = &iy[0x20];
		uint32_t* const sig = &ix[0x20];
		uint32_t* const shaped = &ix[0x40];

		// The noise: B and X1 swap as a Fibonacci pair, B1 only (B0 stays 0)
		{
			uint32_t b = y[0x1c], x1 = y[0x1d];
			int k = 0;
			for(int i = 0; i < 10; ++i)
			{
				b = (b + x1) & 0xffffff;
				const uint32_t x0 = x1;
				noise[k++] = b;
				noise[k++] = b;
				noise[k++] = b;
				x1 = b;
				b = x0;
			}
			b = (b + x1) & 0xffffff;
			y[0x1c] = x1;
			y[0x1d] = b;
			noise[k++] = b;
			noise[k++] = b;
		}
		{
			const uint32_t x0 = y[0x1e];
			for(int k = 0; k < 32; ++k)
				noise[k] = lim(mul(noise[k], x0));
			y[0x1e] = lim(mul(y[0xc], x0));
		}

		// The block after a Hit, and the eight of the attack
		if(y[0x12] != 0)
		{
			const int64_t a = acc(y[0x12]) - acc(1);
			y[0x12] = lim(a);
			if(a == acc(9))
			{
				probe(testHooks::BdFade);
				fadeOut(y[0x18], _out);
				return;
			}
			probe(testHooks::BdAttack);
			const uint32_t index = y[0x19];
			y[0x19] = lim(acc(0x20) + acc(index));
			const uint32_t x0 = y[0xf], x1 = y[2];
			for(uint32_t k = 0; k < 32; ++k)
			{
				// The attack reads 256 of the 288 words (update restarts the index with the count)
				int64_t acc1 = shl(mul(word(t.bdWaveB, index + k), x1), 2);
				acc1 += mul(word(t.bdWaveA, index + k), x0) + acc(noise[k]);
				_out[k] = sx(lim(wrap56(acc1)));
			}
			return;
		}

		// The two sines, their steps from PTCH and the pitch envelope
		const auto sweep = [&](const int _shift)
		{
			return (acc(y[3]) >> _shift) + (mul(y[0x13], y[4]) >> 1);
		};
		sines(t, y, 0x16, 0x17, sweep(6), [&](const int _k, const uint32_t _w) { sig[_k] = lim(acc(_w) >> 2); });
		const int64_t lowStep = sweep(7);
		y[0x13] = lim(mul(y[0x13], y[5]));
		sines(t, y, 0x1f, 0x20, lowStep, [&](const int _k, const uint32_t _w) { low[_k] = lim(shl(acc(_w), 1)); });
		{
			const uint32_t x0 = y[0xe], x1 = y[0x21];
			for(int k = 0; k < 32; ++k)
				sig[k] = lim(acc(sig[k]) + (mul(low[k], x1) >> 4));
			y[0x21] = lim(mul(x1, x0));
		}

		// The clipper: past its upper threshold the signal holds the lower one's level less a step, past the lower
		// the upper's plus a step, and those levels move (Y:$14, Y:$15); the thresholds move by the step each block
		{
			const uint32_t x0 = y[0x1a], x1 = y[0x1b];
			y[0x1a] = lim(acc(y[0x10]) + acc(x0));
			y[0x1b] = lim(acc(x1) - acc(y[0x10]));
			ix[6] = x0;
			ix[7] = x1;
			uint32_t y0 = y[0x14], y1 = y[0x15];
			const int64_t a = acc(y[0x10]);
			for(int k = 0; k < 32; ++k)
			{
				int64_t b = (-acc(sig[k])) >> 1;
				if(b > acc(x0))
				{
					probe(testHooks::BdClipAbove);
					b = acc(y0) - a;
					y1 = x1;
					y0 = lim(b);
				}
				else if(b < acc(x1))
				{
					probe(testHooks::BdClipBelow);
					b = acc(y1) + a;
					y0 = x0;
					y1 = lim(b);
				}
				sig[k] = lim(shl(b, 1));
			}
			y[0x14] = y0;
			y[0x15] = y1;
		}

		// The amp envelope, the noise mixed in at its level
		{
			const uint32_t x0 = y[1];
			uint32_t x1 = y[0x11];
			for(int k = 0; k < 32; ++k)
			{
				const int64_t a = mul(x1, sig[k]) + mul(noise[k], x1);
				const int64_t b = mul(x1, x0);
				sig[k] = lim(a);
				x1 = lim(b);
			}
			y[0x11] = x1;
		}

		// The shaper: past X0 (above) or X1 (below) the excess is scaled by X0 before the gain Y:$0A
		{
			const uint32_t y1 = y[0xa], x0 = y[8], x1 = y[9];
			for(int k = 0; k < 32; ++k)
			{
				int64_t a = acc(sig[k]);
				if(a >= acc(x0))
				{
					probe(testHooks::BdShapeAbove);
					a = mul(lim(a - acc(x0)), x0) + acc(x0);
				}
				else if(a <= acc(x1))
				{
					probe(testHooks::BdShapeBelow);
					a = mul(lim(a - acc(x1)), x0) + acc(x1);
				}
				a += mul(y1, lim(a));
				shaped[k] = lim(a);
			}
		}

		for(int k = 0; k < 32; ++k)
			_out[static_cast<size_t>(k)] = sx(lim(shl(acc(shaped[k]) + acc(sig[k]), 1)));
		y[0x18] = static_cast<uint32_t>(_out[31]) & 0xffffff;
	}
}
