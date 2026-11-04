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

	void pair(Context& _c, const uint32_t _state, uint32_t* const _out)
	{
		const auto& t = tables(_c);
		auto& y = _c.y;
		auto& ix = _c.shared.scratch.x;

		// The packet's words, which the DSP copies to X:$01-$06 and X:$08 and reads from there
		ix[1] = y[1];
		ix[2] = y[2];
		ix[3] = y[3];
		ix[4] = y[4];
		ix[5] = y[5];
		ix[6] = y[6];
		ix[8] = y[8];

		uint64_t mod = static_cast<uint64_t>(acc(y[_state], y[_state + 1]));
		uint64_t car = static_cast<uint64_t>(acc(y[_state + 2], y[_state + 3])) & 0xffffffffffffull;
		uint32_t pitchLevel = y[_state + 4], modLevel = y[_state + 5], amp = y[_state + 6];
		int64_t feedback = acc(y[_state + 7], y[_state + 8]);
		for(size_t k = 0; k < 32; ++k)
		{
			const uint32_t pitch = hi(shl(mul(pitchLevel, y[1]), 3) + acc(y[1]));
			pitchLevel = hi(mul(y[8], pitchLevel));

			const uint32_t modSine = sineFedBack(t, mod, feedback, mul(pitch, y[3]) >> 5, y[6]);
			const uint32_t modOut = hi(round(mul(modLevel, modSine)));
			modLevel = hi(mul(y[5], modLevel));

			const uint32_t carSine = sineStep(t, car, fmStep(modOut, pitch, y[4]));
			_out[k] = hi(round(mul(amp, carSine)));
			amp = hi(mul(y[2], amp));
		}

		storeL(_c, _state, static_cast<int64_t>(mod));
		storeL(_c, _state + 2, static_cast<int64_t>(car));
		y[_state + 4] = ix[_state + 4] = pitchLevel;
		y[_state + 5] = ix[_state + 5] = modLevel;
		y[_state + 6] = ix[_state + 6] = amp;
		storeL(_c, _state + 7, feedback);
	}

	void render(Context& _c, Voice::Block& _out)
	{
		uint32_t out[32];
		pair(_c, 9, out);
		for(size_t k = 0; k < 32; ++k)
			_out[k] = sx(out[k]);
	}
}
