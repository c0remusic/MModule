// The snares' and rims' circuits (edDrumsAnalogVoice.h): models of our own, from the classic analog snare (two
// tuned oscillators and a filtered noise under their own envelopes) and rim (a short pair of tones and a click), that
// play what the control (edDrumsAnalogSnare.cpp) writes each tick. No word of the original's sound is here.
//
// The laws, from the control's words to the circuit (chosen here; the measures behind them are in the notes):
// - Pitch: a pitch CV is linear in frequency (the control has taken the exponential), f = cv * HzPerCv of the
//   machine, chosen so that the defaults at note 60 sit where a classic analog snare (about 190 Hz) or rim (about
//   1.7 kHz and 460 Hz) does. Tune counts in semitones, 64 the centre.
// - Envelopes: the frame's four 8-byte commands are four one-shot envelopes. A command is new when its bytes 2-7
//   change: nibble 0xf at byte 4 jumps to the peak (byte 2, of 127) and decays; 0xb and 0x9 decay from where the
//   envelope is. The decay is the rate word's (bits 0-19) fall per tick, (1 - mantissa / 2^17) / 2^octave, a tick
//   being 2 samples at 48 kHz as the control's decayTicks counts them: a decay knob of 60 falls by e in 7 ms and
//   reaches -90 dB in 80 ms, 127 falls by e in 2.4 s. Switch 3 (the control's "done") closes all four in 2 ms.
// - Noise: white noise of our own through a one-pole highpass and a state-variable bandpass, set by the control's
//   four noise settings: highpass 60 Hz * 2^(5 s0/127), resonance 0.35 + s1/32, centre 300 Hz * 2^(6 s2/127),
//   level 0.25 + s3/127. The colour knob (the first parameter byte) moves the centre up to two octaves.
// - Levels: the heads' first long (a Q31 level the control holds through the Hit) scales the noise (the rims' ring
//   tone); outA (32 samples a tick at 48 kHz) is the click, or the body, read across the tick by linear interpolation.

#include "edDrumsAnalogVoice.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace elektron::drums::analog::circuit
{
	namespace
	{
		constexpr float Pi = 3.14159265f;

		// Circuit::f
		enum F
		{
			Phase1, Phase2,			// the two oscillators, in cycles
			Env0, Env1, Env2, Env3,	// the four envelopes' levels
			Coef0, Coef1, Coef2, Coef3,	// their per-host-sample fall
			NoiseLow, NoiseBand,	// the noise's state-variable filter
			NoiseHigh, NoiseHighIn,	// its one-pole highpass
			ClickLast,				// the last outA sample of the previous tick
			Dc, DcIn,				// the output's DC blocker
			Body,					// the body's own decay (bodyWord), 1 at the Hit
			Inc1, Inc2				// the oscillators' last steps a host sample, in cycles (snareAdvance)
		};
		// Circuit::u
		enum U
		{
			Seed,
			Command0,				// the four commands' bytes 2-7 as two words each, last tick
			Fired = Command0 + 8,	// an envelope has started since the Hit
		};

		enum class Mix { Plain, Balance, Fm, Body, Ring };

		struct Shape
		{
			float hz1, hz2;			// Hz per CV word of the two oscillators
			int env1, env2, envNoise;	// which envelope drives each
			int envBlip;			// an envelope that bends the pitch up at the attack (-1: none)
			float tone, noise, click;	// the mix
			Mix mix;
			int knob;				// the parameter word the mix reads (balance, snap, ring noise), -1: none
			int balanceWord;		// a word that crossfades the two tones (the rims'), -1: none
			int bodyWord;			// a word that sets the body's decay (the tone, or outA for the body snare), -1: none
		};

		Shape shapeOf(const Machine _m)
		{
			switch(_m)
			{
			case Machine::SnareHard:		return {0.0466f, 0.0466f, 0, 1, 3, -1, 0.5f, 6.0f, 2.0f, Mix::Plain, -1, -1, -1};
			case Machine::SnareClassic:		return {0.0486f, 0.0486f, 2, 3, 2, 0, 0.4f, 2.0f, 0.8f, Mix::Balance, 7, -1, -1};
			case Machine::SnareFm:			return {0.0486f, 0.0660f, 2, 3, 2, 0, 0.6f, 4.0f, 1.0f, Mix::Fm, -1, -1, -1};
			case Machine::SnareNatural:		return {0.0908f, 0.0908f, 3, 3, 3, -1, 0.6f, 1.5f, 0.8f, Mix::Body, 4, -1, 3};
			case Machine::SnareAcoustic:	return {0.0956f, 0.0956f, 0, 1, 3, -1, 0.4f, 10.0f, 2.5f, Mix::Plain, -1, -1, 3};
			case Machine::RimHard:			return {0.0637f, 0.0637f, 0, 1, 3, -1, 0.6f, 1.0f, 1.5f, Mix::Ring, 6, -1, -1};
			default:						return {0.0637f, 0.0637f, 2, 3, 2, -1, 0.6f, 1.0f, 0.7f, Mix::Ring, 5, 3, -1};
			}
		}

		// A sine of a phase in cycles, by a parabola corrected to within 0.1 %
		float sine(float _x)
		{
			_x -= std::floor(_x);
			const float y = _x < 0.5f ? 8 * _x * (1 - 2 * _x) : 8 * (_x - 0.5f) * (2 * _x - 2);
			return y * (0.775f + 0.225f * std::fabs(y));
		}

		// The rate word's fall per tick, as a per-host-sample multiplier
		float fallOf(const uint32_t _rate, const double _hostRate)
		{
			const double d = (1 - (_rate & 0xffff) / 131072.0) / std::ldexp(1.0, static_cast<int>(_rate >> 16 & 0xf));
			if(d >= 1)
				return 0;
			return static_cast<float>(std::exp(24000.0 / _hostRate * std::log1p(-d)));
		}

		void commands(const VoiceIo& _io, Circuit& _c, const double _rate, const bool _done)
		{
			const uint8_t* e = _io.frame.data() + 8;
			for(int k = 0; k < 4; ++k)
			{
				const uint8_t* p = e + 8 * k;
				const uint32_t a = static_cast<uint32_t>(p[2]) << 8 | p[3];
				const uint32_t b = be32(p + 4);
				uint32_t& la = _c.u[static_cast<size_t>(Command0 + 2 * k)];
				uint32_t& lb = _c.u[static_cast<size_t>(Command0 + 2 * k + 1)];
				if(a == la && b == lb)
					continue;
				la = a;
				lb = b;
				if((p[4] >> 4) == 0xf)
				{
					_c.f[static_cast<size_t>(Env0 + k)] = p[2] / 127.0f;
					_c.u[Fired] = 1;
				}
				_c.f[static_cast<size_t>(Coef0 + k)] = fallOf(b & 0xfffff, _rate);
			}
			if(_done)
			{
				const auto close = static_cast<float>(std::exp(-1 / (0.002 * _rate)));
				for(int k = 0; k < 4; ++k)
				{
					float& c = _c.f[static_cast<size_t>(Coef0 + k)];
					c = std::min(c, close);
				}
			}
		}

		float settingHz(const int _v, const float _low, const float _octaves)
		{
			return _low * std::exp2(_v / 127.0f * _octaves);
		}

		float q31(const uint8_t* _p) { return static_cast<int32_t>(be32(_p)) / 2147483648.0f; }
	}

	// Ended once an envelope has started and all four have reached zero, whatever phase the control is in (the body
	// snare's control parks in its last phase for good). The click's own ramp never settles on zero words (its rounding
	// leaves a few LSB around zero), and the engine waits 64 quiet blocks before it releases the voice anyway
	bool snareEnded(Machine, const VoiceIo&, const Circuit& _c)
	{
		return _c.u[Fired] && _c.f[Env0] + _c.f[Env1] + _c.f[Env2] + _c.f[Env3] == 0;
	}

	// A Hit restarts the whole circuit: phases, envelopes, filters, the commands seen, the noise
	void snareTrig(Machine, Circuit& _c)
	{
		_c = Circuit{};
		_c.u[Seed] = 0x9e3779b9u;
		_c.f[Body] = 1;
	}

	// What runs free while the voice sleeps: the two oscillators at their last pitch and the noise generator; the
	// envelopes and filters have come to rest at zero before a voice sleeps
	void snareAdvance(Machine, Circuit& _c, const int64_t _samples, double)
	{
		for(const auto k : {std::make_pair(Phase1, Inc1), std::make_pair(Phase2, Inc2)})
		{
			const double p = _c.f[k.first] + static_cast<double>(_c.f[k.second]) * static_cast<double>(_samples);
			_c.f[k.first] = static_cast<float>(p - std::floor(p));
		}
		// the generator x' = a x + c, _samples steps at once: (a^n, c (a^(n-1) + ... + 1)) by squaring
		uint32_t mul = 1, add = 0, a = 1664525u, c = 1013904223u;
		for(auto n = static_cast<uint64_t>(_samples); n; n >>= 1)
		{
			if(n & 1)
			{
				mul *= a;
				add = add * a + c;
			}
			c *= a + 1;
			a *= a;
		}
		_c.u[Seed] = _c.u[Seed] * mul + add;
	}

	void snareRender(const Machine _m, const VoiceIo& _io, Circuit& _c, float* _out, const int _n, const double _rate)
	{
		const Shape s = shapeOf(_m);
		commands(_io, _c, _rate, s.mix != Mix::Fm && (_io.switches & 1u << 3) != 0);
		auto& f = _c.f;
		const uint8_t* frame = _io.frame.data();
		const float inv = static_cast<float>(1 / _rate);
		const float inc1 = be16(frame) * s.hz1 * inv, inc2 = be16(frame + 2) * s.hz2 * inv;
		f[Inc1] = inc1;
		f[Inc2] = inc2;
		const float knob = s.knob >= 0 ? _io.params[static_cast<size_t>(2 * s.knob)] / 127.0f : 0;
		// the body's decay: a time constant of 10 ms * 2^(7 v/127), none at 127 (10 ms to 1.3 s)
		float bodyFall = 1;
		if(s.bodyWord >= 0 && _io.params[static_cast<size_t>(2 * s.bodyWord)] < 127)
			bodyFall = std::exp(-1 / (0.01f * std::exp2(_io.params[static_cast<size_t>(2 * s.bodyWord)] / 127.0f * 7) *
				static_cast<float>(_rate)));
		const bool bodyIsClick = s.mix == Mix::Plain;
		const float heads = std::fabs(q31(_io.heads.data()));

		// the mix of the two tones and the noise
		float tone1 = s.tone, tone2 = s.tone, noiseGain = s.noise * heads, index = 0;
		switch(s.mix)
		{
		case Mix::Plain:
			break;
		case Mix::Balance:		// the balance knob from tones to noise
			tone1 *= 2 * (1 - knob);
			tone2 *= 2 * (1 - knob);
			noiseGain *= 2 * knob;
			break;
		case Mix::Fm:			// oscillator 2 bends oscillator 1's phase by cv3, in cycles
			index = std::fabs(static_cast<float>(static_cast<int16_t>(be16(frame + 6)))) / 16384.0f;
			tone2 = 0;
			break;
		case Mix::Body:			// the snap knob from the body tone to the noise; oscillator 1 is silent
			tone2 *= 1.2f - knob;
			tone1 = 0;
			break;
		case Mix::Ring:			// the heads hold the ring tone's level; the knob adds noise
			tone2 *= 6 * heads;
			if(s.balanceWord >= 0)
			{
				const float b = _io.params[static_cast<size_t>(2 * s.balanceWord)] / 127.0f;
				tone1 *= 2 * (1 - b);
				tone2 *= 2 * b;
			}
			noiseGain = s.noise * knob;
			break;
		}

		const auto& nz = _io.noise;
		const float colour = std::exp2(_io.params[0] / 63.5f);
		const float centre = std::min(settingHz(nz[2], 300, 6) * colour, static_cast<float>(_rate * 0.2));
		const float g = std::tan(Pi * centre * inv);
		const float damp = 1 / (0.35f + nz[1] / 32.0f);
		const float hpA = std::exp(-2 * Pi * settingHz(nz[0], 60, 5) * inv);
		noiseGain *= 0.25f + nz[3] / 127.0f;

		const float dcA = 1 - 2 * Pi * 20 * inv;
		const auto step = static_cast<float>(48000.0 / _rate);
		const uint8_t* clickWords = _io.outA.data();
		uint32_t seed = _c.u[Seed];
		for(int i = 0; i < _n; ++i)
		{
			// the click, outA at its own rate: sample (i + 1) step - 1 of the tick, the previous tick's last before 0
			const float at = (i + 1) * step - 1;
			const int j = static_cast<int>(at);
			const float t = at - j;
			const float a = j < 0 ? f[ClickLast] : q31(clickWords + 4 * std::min(j, 31));
			const float b = q31(clickWords + 4 * std::min(j + 1, 31));
			const float click = (a + (b - a) * t) * (bodyIsClick ? f[Body] : 1);

			const float blip = s.envBlip >= 0 ? 1 + 0.6f * f[static_cast<size_t>(Env0 + s.envBlip)] : 1;
			f[Phase1] += inc1 * blip;
			f[Phase2] += inc2 * blip;
			f[Phase1] -= std::floor(f[Phase1]);
			f[Phase2] -= std::floor(f[Phase2]);
			const float e1 = f[static_cast<size_t>(Env0 + s.env1)], e2 = f[static_cast<size_t>(Env0 + s.env2)];
			const float o2 = sine(f[Phase2]);
			const float o1 = sine(f[Phase1] + index * e2 * o2);
			const float tone = (tone1 * e1 * o1 + tone2 * e2 * o2) * (bodyIsClick ? 1 : f[Body]);
			f[Body] *= bodyFall;

			seed = seed * 1664525u + 1013904223u;
			const float white = static_cast<int32_t>(seed) / 2147483648.0f;
			f[NoiseHigh] = hpA * (f[NoiseHigh] + white - f[NoiseHighIn]);
			f[NoiseHighIn] = white;
			const float high = f[NoiseHigh] - f[NoiseLow] - damp * f[NoiseBand];
			f[NoiseBand] += g * high;
			f[NoiseLow] += g * f[NoiseBand];
			const float noise = (f[NoiseBand] + 0.3f * high) * noiseGain * f[static_cast<size_t>(Env0 + s.envNoise)];

			// the output stage saturates softly, as an overdriven mixer does: within 1.2 of full scale whatever the knobs
			const float sum = tone + noise + s.click * click;
			const float x = sum / std::sqrt(1 + sum * sum * (1 / 1.44f));
			f[Dc] = dcA * (f[Dc] + x - f[DcIn]);
			f[DcIn] = x;
			_out[i] += f[Dc];
			for(int k = 0; k < 4; ++k)
				f[static_cast<size_t>(Env0 + k)] *= f[static_cast<size_t>(Coef0 + k)];
		}
		_c.u[Seed] = seed;
		f[ClickLast] = q31(clickWords + 124);
		// a sound that has ended rests at exact zero: no denormal tail, no noise floor
		for(int k = 0; k < 4; ++k)
			if(f[static_cast<size_t>(Env0 + k)] < 1e-7f)
				f[static_cast<size_t>(Env0 + k)] = 0;
		if(std::fabs(f[Dc]) < 1e-9f && f[Env0] + f[Env1] + f[Env2] + f[Env3] == 0)
			f[Dc] = f[DcIn] = f[NoiseBand] = f[NoiseLow] = f[NoiseHigh] = f[NoiseHighIn] = 0;
	}

	namespace
	{
		// Which parameter word each of Machine 1-8 sets (its value << 8); the words no control sets hold 0
		constexpr int words[7][8] = {
			{1, 2, 6, 5, 4, 3, 7, 0},	// twin: tune, decay, noise, noise fall, click, tone, tone decay, colour
			{1, 2, 3, 6, 4, 5, 7, 0},	// beat: tune, decay, detune, snappy, tone, snap decay, balance, colour
			{1, 2, 3, 7, 6, 5, 4, 0},	// mod: tune, decay, mod tune, mod depth, noise, noise fall, mod decay, colour
			{1, 2, 4, 3, 6, 5, 7, 0},	// snap: tune, decay, snap, body decay, noise tone, noise cut, noise level, colour
			{1, 5, 4, 3, 2, 6, 7, 0},	// body: tune, decay, snap, body decay, tail, ring, hit, colour
			{1, 2, 4, 5, 3, 7, 0, 6},	// click rim: tune, decay, click, ring, tone, tone decay, colour, noise
			{4, 2, 1, 3, 6, 7, 0, 5},	// ring rim: tune, decay, tune 2, balance, ring, click, colour, noise
		};
	}

	void snareParams(const Machine _m, const uint8_t* _controls, std::array<uint8_t, 20>& _params)
	{
		_params.fill(0);
		const int row = static_cast<int>(_m) - static_cast<int>(Machine::SnareHard);
		for(int k = 0; k < 8; ++k)
			_params[static_cast<size_t>(2 * words[row][k])] = std::min<uint8_t>(_controls[k], 127);
	}

	std::array<uint8_t, 8> snareDefaults(const Machine _m)
	{
		switch(_m)
		{
		case Machine::SnareHard:		return {64, 84, 80, 30, 70, 100, 80, 30};
		case Machine::SnareClassic:		return {64, 60, 6, 80, 100, 64, 64, 30};
		case Machine::SnareFm:			return {64, 60, 70, 50, 80, 30, 50, 30};
		case Machine::SnareNatural:		return {64, 60, 64, 64, 80, 40, 90, 30};
		case Machine::SnareAcoustic:	return {64, 105, 64, 64, 40, 80, 100, 30};
		case Machine::RimHard:			return {64, 70, 90, 60, 110, 64, 30, 20};
		default:						return {64, 66, 70, 64, 80, 90, 30, 20};
		}
	}
}
