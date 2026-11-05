// TRX-CY's voice code, ported as mdVoiceTrx.h says.
//
// TRX-CY ($1023C9, $1023EB, $1023FA), each block: TRX-CH's six waves (Y:$02 to Y:$07 their gains), without its noise,
// through two paths: TRX-CP's two high-pass filters by Y:$11 and Y:$0C then a two-pole filter twice by Y:$08 and
// Y:$0E, and the high-pass filters by Y:$09 and Y:$0F then the two-pole filters by Y:$0A and Y:$10, each two-pole input
// its high-pass output two samples late. Each path at its own level, which a Hit raises and which decays a block at a
// time (by Y:$0D and Y:$01); then half their sum, weighted by Y:$0B and Y:$12.
#include "mdVoiceTrx.h"

#include <algorithm>

namespace md::voice::trx::cy
{
	namespace
	{
		using Block = Voice::Block;
	}

	void init(Context& _c)
	{
		auto& y = _c.y;
		y[0x11] = 0x680;
		y[0x12] = 0x400000;
		for(uint32_t w = 0x15; w <= 0x2e; ++w)
			y[w] = 0;
	}

	void update(Context& _c)
	{
		auto& y = _c.y;
		hitLevel(y[0x13], testHooks::CyLevelALimited);
		hitLevel(y[0x14], testHooks::CyLevelBLimited);
		y[0xe] = y[0xf] = y[0x10] = y[0xc];
	}

	void render(Context& _c, Block& _out)
	{
		const auto& t = tables(_c);
		auto& y = _c.y;
		auto& ix = _c.shared.scratch.x;

		// The six waves into the modulo-32 buffer X:$60 (R5), the first not added to what was there
		uint32_t* const sig = &ix[0x60];
		std::fill(sig, sig + 32, 0u);
		addChWaves(t, y, {y[2], y[3], y[4], y[5], y[6], y[7]}, 0x15, sig);

		// The first path's outputs from X:$04, the second's from X:$26
		highPassStage(_c, poles(offsetPole(_c, y[0x11], y[0xc])), 0x60, 0x00, 0x04, 0x1b);
		twoPoleStage(_c, poles(offsetPole(_c, y[8], y[0xe])), 0x04, 0x21);
		highPassStage(_c, poles(offsetPole(_c, y[9], y[0xf])), 0x60, 0x00, 0x26, 0x25);
		twoPoleStage(_c, poles(offsetPole(_c, y[0xa], y[0x10])), 0x26, 0x2b);

		// Each path at its level, written back as the DSP does, then half their sum weighted by Y:$0B and Y:$12 (the
		// DSP's two passes, a sample at a time); then the levels decay
		uint32_t* const a = &ix[0x04];
		uint32_t* const b = &ix[0x26];
		const uint32_t levelA = y[0x13], levelB = y[0x14], weightA = y[0xb], weightB = y[0x12];
		for(int k = 0; k < 32; ++k)
		{
			a[k] = lim(mul(a[k], levelA));
			b[k] = lim(mul(b[k], levelB));
			_out[static_cast<size_t>(k)] = sx(lim((mul(a[k], weightA) + mul(b[k], weightB)) >> 1));
		}
		y[0x13] = lim(mul(y[0xd], levelA));
		y[0x14] = lim(mul(y[1], levelB));
	}
}
