// EFM-BD's voice code, ported as mdVoiceTrx.h says.
//
// EFM-BD ($102EEF, $102EF3, $102F06), a sample at a time: two sines, the first modulating the second, both at a pitch
// (Y:$01) raised by an envelope that decays by Y:$08. The first runs at Y:$03 of that pitch and hears itself through
// Y:$06, at a level that decays by Y:$05. It bends the second's phase by Y:$04, and the second is the output, at the
// amp level, which a Hit raises and Y:$02 decays.
#include "mdVoiceEfm.h"

namespace md::voice::efm::bd
{
	void init(Context& _c)
	{
		_c.y[0xf] = 0;
	}

	void update(Context& _c)
	{
		auto& y = _c.y;
		hitLevel(y[0xf], testHooks::EfmBdLevelLimited);
		y[9] = y[0xb] = 0xfa0;
		y[0xa] = y[0xc] = y[0x10] = y[0x11] = 0;
		y[0xd] = y[7];
		y[0xe] = 0x7fffff;
	}

	void render(Context& _c, Voice::Block& _out)
	{
		const auto& t = tables(_c);
		auto& y = _c.y;
		auto& ix = _c.shared.scratch.x;
		auto& iy = _c.shared.scratch.y;

		// The packet's words, which the DSP copies to X:$01-$06 and X:$08 and reads from there
		ix[1] = y[1];
		ix[2] = y[2];
		ix[3] = y[3];
		ix[4] = y[4];
		ix[5] = y[5];
		ix[6] = y[6];
		ix[8] = y[8];

		uint32_t pitchLevel = y[0xd], modLevel = y[0xe], amp = y[0xf];
		// Both phases keep 48 bits, B2 never read; the feedback, an L word, adds to the first for its read only
		uint64_t mod = static_cast<uint64_t>(acc(y[9], y[0xa]));
		uint64_t car = static_cast<uint64_t>(acc(y[0xb], y[0xc])) & 0xffffffffffffull;
		uint32_t feedbackHi = y[0x10], feedbackLo = y[0x11];
		for(size_t k = 0; k < 32; ++k)
		{
			const uint32_t pitch = hi(shl(mul(pitchLevel, y[1]), 3) + acc(y[1]));
			pitchLevel = hi(mul(y[8], pitchLevel));

			const int64_t feedback = acc(feedbackHi, feedbackLo);
			const uint64_t read = add48(mod, feedback) & 0x007fffffffffull;
			const uint32_t modSine = t.sine[read >> 24];
			mod = add48(add48(read, -feedback), mul(pitch, y[3]) >> 5);
			limL(mul(modSine, y[6]) >> 8, feedbackHi, feedbackLo);

			const uint32_t modOut = hi(round(mul(modLevel, modSine)));
			modLevel = hi(mul(y[5], modLevel));

			const uint32_t bend = hi(round(mul(modOut, pitch)));
			const uint32_t carSine = word(t.sine, static_cast<uint32_t>(car >> 24));
			car = add48(car, (shl(mul(bend, y[4]), 3) + acc(pitch)) >> 8) & 0x007fffffffffull;

			_out[k] = sx(hi(round(mul(amp, carSine))));
			amp = hi(mul(y[2], amp));
		}

		y[9] = ix[9] = hi(static_cast<int64_t>(mod));
		y[0xa] = iy[9] = lo(static_cast<int64_t>(mod));
		y[0xb] = ix[0xb] = hi(static_cast<int64_t>(car));
		y[0xc] = iy[0xb] = lo(static_cast<int64_t>(car));
		y[0xd] = ix[0xd] = pitchLevel;
		y[0xe] = ix[0xe] = modLevel;
		y[0xf] = ix[0xf] = amp;
		y[0x10] = ix[0x10] = feedbackHi;
		y[0x11] = iy[0x10] = feedbackLo;
	}
}
