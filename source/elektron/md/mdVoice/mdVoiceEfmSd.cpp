// EFM-SD's voice code, ported as mdVoiceTrx.h says.
//
// EFM-SD ($102F68, $102F6F, $102F85), each block: a noise through a one-pole filter (Y:$09), then a sample at a time
// EFM-BD's two sines at the pitch Y:$01, with no envelope on it, the second at the amp level, plus the noise at Y:$07,
// at a level that decays by Y:$08. The result goes through a second one-pole filter (Y:$0A).
#include "mdVoiceEfm.h"

namespace md::voice::efm::sd
{
	namespace
	{
		// A one-pole filter in place over 32 words, by a coefficient word: (1 + c) / 2 (1 - z^-1) over 1 - c z^-1. Its
		// state from Y:_state: the last input scaled by the gain, which the scratch keeps too, and the last output
		void onePole(Context& _c, uint32_t* const _io, const uint32_t _coef, const uint32_t _state)
		{
			const uint32_t gain = hi((acc(_coef) >> 1) + acc(0x400000));
			uint32_t scaled = _c.y[_state], output = _c.y[_state + 1];
			for(int k = 0; k < 32; ++k)
			{
				const int64_t a = mul(_io[k], gain);
				output = lim(a - acc(scaled) + mul(output, _coef));
				scaled = hi(a);
				_io[k] = output;
			}
			_c.y[_state] = _c.shared.scratch.x[_state] = scaled;
			_c.y[_state + 1] = output;
		}
	}

	void init(Context& _c)
	{
		auto& y = _c.y;
		y[0x10] = y[0x11] = 0;
		y[0x14] = 0x789abc;
	}

	void update(Context& _c)
	{
		auto& y = _c.y;
		hitLevel(y[0x10], testHooks::EfmSdLevelLimited);
		y[0xb] = y[0xd] = 0x1f4;
		y[0xc] = y[0xe] = y[0x12] = y[0x13] = y[0x16] = y[0x17] = y[0x18] = y[0x19] = 0;
		y[0xf] = y[0x11] = 0x7fffff;
	}

	void render(Context& _c, Voice::Block& _out)
	{
		const auto& t = tables(_c);
		auto& y = _c.y;
		auto& ix = _c.shared.scratch.x;

		// The packet's words, which the DSP copies to X:$01-$08 and reads from there
		for(uint32_t w = 1; w <= 8; ++w)
			ix[w] = y[w];

		// The noise, X:$1A-$39, filtered
		uint32_t* const noise = &ix[0x1a];
		fibonacci(noise, y[0x14], y[0x15]);
		onePole(_c, noise, y[9], 0x16);

		uint32_t modLevel = y[0xf], amp = y[0x10], noiseLevel = y[0x11];
		uint64_t mod = static_cast<uint64_t>(acc(y[0xb], y[0xc]));
		uint64_t car = static_cast<uint64_t>(acc(y[0xd], y[0xe])) & 0xffffffffffffull;
		int64_t feedback = acc(y[0x12], y[0x13]);
		const uint32_t pitch = y[1];
		const int64_t modStep = mul(pitch, y[3]) >> 5;
		uint32_t out[32];
		for(size_t k = 0; k < 32; ++k)
		{
			const uint32_t modSine = sineFedBack(t, mod, feedback, modStep, y[6]);
			const uint32_t modOut = hi(round(mul(modLevel, modSine)));
			modLevel = hi(mul(y[5], modLevel));

			const uint32_t carSine = sineStep(t, car, fmStep(modOut, pitch, y[4]));
			const uint32_t tone = hi(round(mul(amp, carSine)));
			amp = hi(mul(y[2], amp));

			const uint32_t hiss = hi(round(mul(noiseLevel, noise[k])));
			noiseLevel = hi(mul(y[8], noiseLevel));
			out[k] = lim(round(mul(hiss, y[7])) + acc(tone));
		}

		onePole(_c, out, y[0xa], 0x18);
		for(size_t k = 0; k < 32; ++k)
			_out[k] = sx(out[k]);

		storeL(_c, 0xb, static_cast<int64_t>(mod));
		storeL(_c, 0xd, static_cast<int64_t>(car));
		y[0xf] = ix[0xf] = modLevel;
		y[0x10] = ix[0x10] = amp;
		y[0x11] = ix[0x11] = noiseLevel;
		storeL(_c, 0x12, feedback);
		ix[0x14] = y[0x14];
		ix[0x15] = y[0x15];
	}
}
