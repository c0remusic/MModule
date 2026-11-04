// TRX-CL's voice code, ported as mdVoiceTrx.h says.
//
// TRX-CL ($102AA9, $102AB3, $102ABF), each block: a Hit's click of Y:$02 words at Y:$08 where the first sine would sound,
// that sine at Y:$03 after it, plus the second at Y:$04 scaled by Y:$07; an eighth of their sum at the level Y:$09, which
// a Hit raises and Y:$01 decays; through two high-pass filters, as TRX-CP's clap, and out two samples late.
#include "mdVoiceTrx.h"

#include <algorithm>

namespace md::voice::trx::cl
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
		for(uint32_t w = 0xe; w <= 0x13; ++w)
			y[w] = 0;
	}

	void update(Context& _c)
	{
		auto& y = _c.y;
		hitLevel(y[9], testHooks::ClLevelLimited);
		y[0xa] = y[0xb] = y[0xc] = y[0xd] = 0;
	}

	void render(Context& _c, Block& _out)
	{
		const auto& t = tables(_c);
		auto& y = _c.y;
		auto& ix = _c.shared.scratch.x;

		// The first sine, X:$00-$1F, after what is left of the click. The block that ends the click steps the sine by
		// the number of words left, not by Y:$03
		const uint32_t click = y[2];
		if(click == 0)
		{
			probe(testHooks::ClSine);
			sinesFedBack(t, y, 0xa, 0xb, acc(y[3]) >> 1, &ix[0], 32);
		}
		else if(click > 32)
		{
			probe(testHooks::ClClick);
			std::fill_n(&ix[0], 32, y[8]);
			y[2] = click - 32;
		}
		else
		{
			probe(click == 32 ? testHooks::ClClickEnd : testHooks::ClClickRest);
			std::fill_n(&ix[0], click, y[8]);
			sinesFedBack(t, y, 0xa, 0xb, acc(32 - click), &ix[click], 32 - click);
			y[2] = 0;
		}

		// The second, X:$20-$3F, at its level
		sinesFedBack(t, y, 0xc, 0xd, acc(y[4]) >> 1, &ix[0x20], 32);
		for(uint32_t k = 0; k < 32; ++k)
			ix[0x20 + k] = lim(mul(y[7], ix[0x20 + k]));

		// An eighth of their sum at the level, which decays a sample at a time
		uint32_t level = y[9];
		for(uint32_t k = 0; k < 32; ++k)
		{
			const uint32_t sum = lim(acc(ix[k]) + acc(ix[0x20 + k]));
			ix[0x20 + k] = lim(mul(level, sum) >> 3);
			level = lim(mul(level, y[1]));
		}
		y[9] = level;

		highPassStage(_c, poles(offsetPole(t, y[5], y[6])), 0x20, 0x00, 0x40, 0x0e);

		// From the stage's history words: two samples late, its last two kept for the next block
		for(uint32_t k = 0; k < 32; ++k)
			_out[k] = sx(ix[0x40 + k]);
	}
}
