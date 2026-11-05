// EFM-HH's voice code, ported as mdVoiceTrx.h says.
//
// EFM-HH ($10332E, $103332, $103349), a sample at a time: a sine at Y:$03 of the pitch Y:$01 that hears itself through
// Y:$06 and, at a level that Y:$05 decays, bends by Y:$04 a second sine, at Y:$01 itself, that hears itself through
// Y:$07. That second sine plays at the amp level, which a Hit raises and Y:$02 decays, scaled by Y:$0C, an unsigned word,
// and limited. A third sine, at a step of Y:$0B, Y:$09 of it plus Y:$0A, limited, rings it. The result goes through a
// one-pole filter (Y:$08).
#include "mdVoiceEfm.h"

namespace md::voice::efm::hh
{
	namespace
	{
		constexpr uint32_t kHitRingPhase = 0x2000;
		constexpr uint32_t kHitModLevel = 0x7fffff;
	}

	void init(Context& _c)
	{
		_c.y[0x15] = 0;
	}

	void update(Context& _c)
	{
		auto& y = _c.y;
		hitLevel(y[0x15], testHooks::EfmHhLevelLimited);
		y[0xd] = y[0xe] = y[0xf] = y[0x10] = y[0x12] = 0;
		y[0x16] = y[0x17] = y[0x18] = y[0x19] = y[0x1a] = y[0x1b] = 0;
		y[0x11] = kHitRingPhase;
		y[0x14] = kHitModLevel;
	}

	void render(Context& _c, Voice::Block& _out)
	{
		const auto& t = tables(_c);
		auto& y = _c.y;
		auto& ix = _c.shared.scratch.x;

		// The packet's words, which the DSP copies to X:$01-$07 and X:$09-$0C and reads from there
		for(uint32_t w = 1; w <= 7; ++w)
			ix[w] = y[w];
		for(uint32_t w = 9; w <= 0xc; ++w)
			ix[w] = y[w];

		uint64_t mod = static_cast<uint64_t>(acc(y[0xd], y[0xe]));
		uint64_t car = static_cast<uint64_t>(acc(y[0xf], y[0x10]));
		uint64_t ring = static_cast<uint64_t>(acc(y[0x11], y[0x12]));
		int64_t modFeedback = acc(y[0x16], y[0x17]), carFeedback = acc(y[0x18], y[0x19]);
		uint32_t modLevel = y[0x14], amp = y[0x15];
		const int64_t modStep = mul(y[1], y[3]) >> 5, ringStep = acc(y[0xb]) >> 8;
		uint32_t out[32];
		for(size_t k = 0; k < 32; ++k)
		{
			const uint32_t modSine = sineFedBack(t, mod, modFeedback, modStep, y[6]);
			const uint32_t modOut = hi(round(mul(modLevel, modSine)));
			modLevel = hi(mul(y[5], modLevel));

			const uint32_t carSine = sineFedBack(t, car, carFeedback, fmStep(modOut, y[1], y[4]), y[7]);
			const uint32_t carOut = hi(round(mul(lim(mulSU(amp, y[0xc])), carSine)));
			amp = hi(mul(y[2], amp));

			const uint32_t ringSine = sineStep(t, ring, ringStep);
			out[k] = hi(round(mul(lim(round(mul(y[9], ringSine)) + acc(y[0xa])), carOut)));
		}

		storeL(_c, 0xd, static_cast<int64_t>(mod));
		storeL(_c, 0xf, static_cast<int64_t>(car));
		storeL(_c, 0x11, static_cast<int64_t>(ring));
		y[0x14] = ix[0x14] = modLevel;
		y[0x15] = ix[0x15] = amp;
		storeL(_c, 0x16, modFeedback);
		storeL(_c, 0x18, carFeedback);

		onePole(_c, out, y[8], 0x1a, 32);
		for(size_t k = 0; k < 32; ++k)
			_out[k] = sx(out[k]);
	}
}
