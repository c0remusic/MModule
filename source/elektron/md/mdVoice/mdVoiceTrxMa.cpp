// TRX-MA's voice code, ported as mdVoiceTrx.h says.
//
// TRX-MA ($1028F4, $102914, $10291F), each block: TRX-CH's noise through TRX-CP's high-pass stages. The first (Y:$0A,
// Y:$10) keeps the noise's top. The noise also drives a level follower, read backwards through the block, that the
// second stage (Y:$07, Y:$08) filters; an eighth of that goes through the third (Y:$09, Y:$0F), and the first stage's
// output adds to it at a level that rises by Y:$0B a block from each Hit. Then the Hit's envelope: a level that rises
// for Y:$01 blocks, a quarter of the signal for Y:$03, and a level that falls for Y:$04.
#include "mdVoiceTrx.h"

#include <algorithm>
#include <cstdlib>

namespace md::voice::trx::ma
{
	namespace
	{
		using Block = Voice::Block;
		using testHooks::probe;

		// The level follower, over the noise at X:$02-$21 from its last word to its first, into X:$22-$41: a word past
		// the threshold Y:$06 starts a rise from $FFFCB9 towards its size, x * Y:$0D + x a sample, and from there the
		// level falls by Y:$0C
		void follow(Context& _c)
		{
			auto& y = _c.y;
			auto& ix = _c.shared.scratch.x;
			const uint32_t fall = y[0xc], rise = y[0xd];
			const int64_t threshold = acc(y[6]);
			int64_t b = acc(y[0x13]);
			uint32_t x0 = y[0x12], x1 = y[0x14];
			for(int k = 0; k < 32; ++k)
			{
				const int64_t in = acc(ix[0x21 - k]);
				const bool above = in >= threshold;
				if(above)
				{
					x0 = 0xfffcb9;
					b = -std::abs(in);
					x1 = rise;
				}
				const bool rising = above || b != 0;
				int64_t a = mul(x1, x0);
				if(rising)
					a += acc(x0);
				x0 = lim(a);
				if(!rising)
				{
					probe(testHooks::MaFall);
				}
				else if(a <= b)
				{
					probe(testHooks::MaPeak);
					x1 = fall;
					x0 = lim(-b);
					b = 0;
				}
				ix[0x22 + k] = lim(a);
			}
			y[0x13] = lim(b);
			y[0x12] = x0;
			y[0x14] = x1;
		}

		// A quarter of the signal at X:$02-$21 at Y:$0E, which moves by _step a sample
		void ramp(Context& _c, const int64_t _step, Block& _out)
		{
			const auto& ix = _c.shared.scratch.x;
			int64_t a = acc(_c.y[0xe]);
			for(int k = 0; k < 32; ++k)
			{
				a += _step;
				_out[static_cast<size_t>(k)] = sx(lim(mul(ix[0x02 + k], lim(a)) >> 2));
			}
			_c.y[0xe] = lim(a);
		}
	}

	void init(Context& _c)
	{
		auto& y = _c.y;
		y[0xf] = 0;
		y[0x10] = 0x7f;
		y[0x15] = 0x29a;
		y[0x16] = 1;
		for(uint32_t w = 0x17; w <= 0x28; ++w)
			y[w] = 0;
	}

	void update(Context& _c)
	{
		auto& y = _c.y;
		y[0xc] = 0x7a3e22;
		y[0xd] = 0x450d02;
		y[0xe] = y[0x11] = y[0x12] = 0;
	}

	void render(Context& _c, Block& _out)
	{
		const auto& t = tables(_c);
		auto& y = _c.y;
		auto& ix = _c.shared.scratch.x;
		auto& iy = _c.shared.scratch.y;

		fibonacci(&ix[0x02], y[0x15], y[0x16]);

		highPassStage(_c, poles(offsetPole(_c, y[0xa], y[0x10])), 0x02, 0x70, 0x46, 0x23);
		// From its two history words: two samples late, its last two lost
		std::copy_n(&ix[0x46], 32, &iy[0x20]);

		follow(_c);

		// The second stage's input history lands on the follower's first two words, so it reads two words past them:
		// X:$42-$43, which nothing here writes, hold what the last voice to write them left, as on the DSP
		highPassStage(_c, poles(offsetPole(_c, y[7], y[8])), 0x24, 0x70, 0x46, 0x17);
		for(uint32_t k = 0; k < 32; ++k)
			ix[0x46 + k] = lim(acc(ix[0x46 + k]) >> 3);
		highPassStage(_c, poles(offsetPole(_c, y[9], y[0xf])), 0x46, 0x70, 0x02, 0x1d);

		y[0x11] = lim(acc(y[0x11]) + acc(y[0xb]));
		for(uint32_t k = 0; k < 32; ++k)
			ix[0x02 + k] = lim(acc(ix[0x02 + k]) + mul(iy[0x20 + k], y[0x11]));

		if(countDown(y[1]))
		{
			probe(testHooks::MaRise);
			ramp(_c, acc(y[2]), _out);
		}
		else if(countDown(y[3]))
		{
			probe(testHooks::MaHold);
			for(int k = 0; k < 32; ++k)
				_out[static_cast<size_t>(k)] = sx(lim(acc(ix[0x02 + k]) >> 2));
		}
		else if(countDown(y[4]))
		{
			probe(testHooks::MaDecay);
			ramp(_c, -acc(y[5]), _out);
		}
		else
		{
			probe(testHooks::MaSilent);
			_out.fill(0);
		}
	}
}
