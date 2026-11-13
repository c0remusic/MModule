// The snares' and rims' circuits (edDrumsAnalogVoice.h): models of our own, each machine its own structure after the
// classic circuit its control evokes (.scratch/elektron-drums/research/28-circuits-classiques.md), playing what the
// control (edDrumsAnalogSnare.cpp) writes each tick. No word of the original's sound is here.
//
// - twin snare: two VCOs at the control's fixed fifth, triangles rounded by a diode shaper, the second's phase pushed
//   by the first (the coupling of the classic 909-style snare), both swept down a few ms at the strike; the noise
//   low-passed, then in two branches of their own colour and length.
// - beat snare: two damped sines (the 808-style shells), the second an octave above the first and moved by DETUNE,
//   summed through a soft saturation; the noise high-passed alone (snappy).
// - mod snare: a two-operator FM voice, the modulator inharmonic and fed back on itself, its index falling at the
//   strike; the noise phase-modulates the carrier (a grainy, not a separate, noise).
// - snap snare: one VCO (a triangle) and a noise through a four-pole resonant low-pass whose cutoff the envelope
//   sweeps down, the small electronic snare's voice.
// - body snare: the body sample (outA) with two membrane modes; its wires are a band of noise the body's own movement
//   gates (an envelope follower of the body), not a separate envelope.
// - click rim: three high, very damped resonators at fixed ratios (two from the control's pitches, a third above),
//   struck by the control's pulse and swept down at the strike, then a symmetric hard clip.
// - ring rim: two independent resonators struck by the pulse, crossfaded by BALANCE, through an asymmetric diode stage,
//   a very short VCA and a low-pass.
//
// The laws, from the control's words to the circuit (chosen here; the measures behind them are in the notes):
// - Pitch: a pitch CV is linear in frequency (the control has taken the exponential), f = cv * HzPerCv of the
//   machine, so that the defaults at note 60 sit where the classic circuit does (named per machine below). Tune counts
//   in semitones, 64 the centre.
// - Envelopes: the frame's four 8-byte commands are four one-shot envelopes. A command is new when its bytes 2-7
//   change: nibble 0xf at byte 4 jumps to the peak (byte 2, of 127) and decays; 0xb and 0x9 decay from where the
//   envelope is. The decay is the rate word's (bits 0-19) fall per tick, (1 - mantissa / 2^17) / 2^octave, a tick
//   being 2 samples at 48 kHz as the control's decayTicks counts them. Switch 3 (the control's "done") closes all
//   four in 2 ms. The first command of a Hit is its strike: an attack envelope of 4 ms starts there.
// - A resonator's damping is the time constant of the envelope its machine names: its Q is pi f tau.
// - Noise: white noise of our own, set by the control's four noise settings: highpass 60 Hz * 2^(5 s0/127), resonance
//   0.35 + s1/32, centre 300 Hz * 2^(6 s2/127), level 0.25 + s3/127. COLOR (the first parameter byte) moves the
//   centre up to two octaves.
// - Levels: the heads' first long (a Q31 level the control holds through the Hit) scales the noise (the rims' ring);
//   outA (32 samples a tick at 48 kHz) is the click, the body or the rims' strike, read across the tick linearly.

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
			ClickLast,				// the last outA sample of the previous tick
			Dc, DcIn,				// the output's DC blocker
			Body,					// the body's own decay (bodyWord), 1 at the Hit
			Inc1, Inc2,				// the oscillators' last steps a host sample, in cycles (snareAdvance)
			Strike,					// the attack envelope, 1 at the Hit's first command
			Feedback,				// the FM modulator's last output
			Follow,					// the body's envelope follower
			Lp,						// a one-pole low-pass (ring rim's output, twin's narrow branch)
			Svf0,					// four state-variable filters, two states each
			Svf3 = Svf0 + 7,
			Res0,					// three resonators, two states each
			Res2 = Res0 + 5,
			Count
		};
		static_assert(Count <= 192, "the circuit's state");
		// Circuit::u
		enum U
		{
			Seed,
			Command0,				// the four commands' bytes 2-7 as two words each, last tick
			Fired = Command0 + 8,	// an envelope has started since the Hit
		};

		// A sine of a phase in cycles, by a parabola corrected to within 0.1 %; without a branch, which a high pitch
		// would mispredict every few samples
		float sine(float _x)
		{
			const float t = _x - std::floor(_x) - 0.5f;
			const float y = -16 * t * (0.5f - std::fabs(t));
			return y * (0.775f + 0.225f * std::fabs(y));
		}

		// A triangle through a diode shaper: odd harmonics falling fast, a rounded top
		float shapedTriangle(float _x)
		{
			_x -= std::floor(_x);
			const float t = 4 * std::fabs(_x - 0.5f) - 1;
			return t * (1.5f - 0.5f * t * t);
		}

		float softClip(const float _x) { return _x / std::sqrt(1 + _x * _x); }

		// A state-variable filter (trapezoidal), its two states at _s
		struct Svf
		{
			float a1, a2, a3, k;
			void set(const float _hz, const float _q, const float _inv)
			{
				const float g = std::tan(Pi * std::min(_hz * _inv, 0.45f));
				k = 1 / _q;
				a1 = 1 / (1 + g * (g + k));
				a2 = g * a1;
				a3 = g * a2;
			}
			// low, band, high of _x
			void run(float* _s, const float _x, float& _low, float& _band, float& _high) const
			{
				const float v3 = _x - _s[1];
				const float v1 = a1 * _s[0] + a2 * v3;
				const float v2 = _s[1] + a2 * _s[0] + a3 * v3;
				_s[0] = 2 * v1 - _s[0];
				_s[1] = 2 * v2 - _s[1];
				_low = v2;
				_band = v1;
				_high = _x - k * v1 - v2;
			}
			// a resonator: the band at unity peak gain
			float ring(float* _s, const float _x) const
			{
				float l, b, h;
				run(_s, _x, l, b, h);
				return b * k;
			}
		};

		// The rate word's fall per tick, as a per-host-sample multiplier
		float fallOf(const uint32_t _rate, const double _hostRate)
		{
			const double d = (1 - (_rate & 0xffff) / 131072.0) / std::ldexp(1.0, static_cast<int>(_rate >> 16 & 0xf));
			if(d >= 1)
				return 0;
			return static_cast<float>(std::exp(24000.0 / _hostRate * std::log1p(-d)));
		}

		// An envelope's time constant in seconds from its per-host-sample fall
		float tauOf(const float _fall, const float _inv)
		{
			return _fall <= 0 ? _inv : -_inv / std::log(std::min(_fall, 0.999999f));
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
					if(!_c.u[Fired])
						_c.f[Strike] = 1;
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

		float word(const VoiceIo& _io, const int _w) { return _io.params[static_cast<size_t>(2 * _w)] / 127.0f; }
	}

	// Ended once an envelope has started and all four have reached zero, whatever phase the control is in (the body
	// snare's control parks in its last phase for good). The click's own ramp never settles on zero words (its rounding
	// leaves a few LSB around zero), and the engine waits 64 quiet blocks before it releases the voice anyway
	bool snareEnded(Machine, const VoiceIo&, const Circuit& _c)
	{
		return _c.u[Fired] && _c.f[Env0] + _c.f[Env1] + _c.f[Env2] + _c.f[Env3] == 0;
	}

	// A Hit restarts the whole circuit: phases, envelopes, filters, resonators, the commands seen, the noise
	void snareTrig(Machine, Circuit& _c)
	{
		_c = Circuit{};
		_c.u[Seed] = 0x9e3779b9u;
		_c.f[Body] = 1;
	}

	// What runs free while the voice sleeps: the two oscillators at their last pitch and the noise generator; the
	// envelopes, filters and resonators have come to rest at zero before a voice sleeps
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
		commands(_io, _c, _rate, _m != Machine::SnareFm && (_io.switches & 1u << 3) != 0);
		auto& f = _c.f;
		const uint8_t* frame = _io.frame.data();
		const float inv = static_cast<float>(1 / _rate);
		const float cv0 = be16(frame), cv1 = be16(frame + 2);
		const float heads = std::fabs(q31(_io.heads.data()));

		// the noise's settings, as every machine reads them
		const auto& nz = _io.noise;
		const float colour = std::exp2(_io.params[0] / 63.5f);
		const float centre = std::min(settingHz(nz[2], 300, 6) * colour, static_cast<float>(_rate * 0.2));
		const float resonance = 0.35f + nz[1] / 32.0f;
		const float highpass = settingHz(nz[0], 60, 5);
		const float noiseLevel = 0.25f + nz[3] / 127.0f;
		// the body's decay (snap and body snares): a time constant of 10 ms * 2^(7 v/127), none at 127
		const int bodyWord = _m == Machine::SnareNatural || _m == Machine::SnareAcoustic ? 3 : -1;
		float bodyFall = 1;
		if(bodyWord >= 0 && _io.params[2 * bodyWord] < 127)
			bodyFall = std::exp(-inv / (0.01f * std::exp2(word(_io, bodyWord) * 7)));
		const float strikeFall = std::exp(-inv / 0.004f);

		float* e = &f[Env0];
		float* svf = &f[Svf0];
		float* res = &f[Res0];
		Svf filter[4];
		Svf ring[3];
		float inc1 = 0, inc2 = 0;

		// what each machine sets for the tick
		switch(_m)
		{
		case Machine::SnareHard:
			// 265 and 177 Hz at the defaults, a fifth apart: within the classic 909-style snare's 1.47-1.83
			inc1 = cv0 * 0.065f * inv;
			inc2 = cv1 * 0.065f * inv;
			filter[0].set(centre * 0.6f, 0.7f, inv);			// the noise's low-pass
			filter[1].set(centre * 1.4f, 4.0f, inv);			// its narrow branch
			break;
		case Machine::SnareClassic:
			// 238 Hz at the defaults, the classic 808-style shell; the second shell an octave above, DETUNE moving it
			inc1 = cv0 * 0.0625f * inv;
			inc2 = 2 * cv1 * 0.0625f * inv;
			filter[0].set(std::max(centre * 2.0f, highpass), 0.6f, inv);	// snappy: high-passed alone
			break;
		case Machine::SnareFm:
			// a 185 Hz carrier, the modulator at 1.61 times it (inharmonic)
			inc1 = cv0 * 0.0486f * inv;
			inc2 = cv1 * 0.066f * inv;
			filter[0].set(centre, 0.7f, inv);
			break;
		case Machine::SnareNatural:
			inc2 = cv1 * 0.0908f * inv;							// 190 Hz at the defaults
			break;
		case Machine::SnareAcoustic:
			// the membrane's (0,1) and (1,1) modes at 200 and 300 Hz
			inc1 = cv0 * 0.0956f * inv;
			inc2 = cv1 * 0.0956f * inv;
			filter[0].set(centre * 1.5f, resonance, inv);		// the wires' band
			break;
		case Machine::RimHard:
		{
			// 1.7 kHz, 464 Hz and 2.5 kHz (1.47 times the first), damped by the tone envelope (A0)
			const float tau = std::min(tauOf(f[Coef0], inv), 0.03f);
			const float hz[3] = {cv0 * 0.0637f, cv1 * 0.0637f, cv0 * 0.0637f * 1.47f};
			for(int k = 0; k < 3; ++k)
				ring[k].set(hz[k] * (1 + 0.25f * f[Strike]), std::max(1.0f, Pi * hz[k] * tau), inv);
			filter[0].set(centre * 2, 0.8f, inv);
			break;
		}
		default:	// RimClassic
		{
			// 1.7 kHz and 656 Hz, two independent tunes, damped by the decay envelope (B0)
			const float tau = std::min(tauOf(f[Coef2], inv), 0.03f);
			const float hz[2] = {cv0 * 0.0637f, cv1 * 0.0637f};
			for(int k = 0; k < 2; ++k)
				ring[k].set(hz[k], std::max(1.0f, Pi * hz[k] * tau), inv);
			filter[0].set(centre * 2, 0.8f, inv);
			break;
		}
		}
		if(_m == Machine::SnareNatural)
		{
			// the resonant low-pass's cutoff follows the envelope down: from 4 times the centre at the strike
			const float sweep = centre * (0.5f + 3.5f * e[3] * e[3]);
			filter[0].set(sweep, resonance * 1.5f, inv);
			filter[1].set(sweep, 0.8f, inv);
		}
		f[Inc1] = inc1;
		f[Inc2] = inc2;

		const float dcA = 1 - 2 * Pi * 20 * inv;
		const auto step = static_cast<float>(48000.0 / _rate);
		const uint8_t* clickWords = _io.outA.data();
		const float lpA = std::exp(-2 * Pi * std::min(6000.0f * colour, 18000.0f) * inv);
		const float followA = std::exp(-inv / 0.003f);
		uint32_t seed = _c.u[Seed];
		for(int i = 0; i < _n; ++i)
		{
			// outA at its own rate: sample (i + 1) step - 1 of the tick, the previous tick's last before 0
			const float at = (i + 1) * step - 1;
			const int j = static_cast<int>(at);
			const float t = at - j;
			const float a = j < 0 ? f[ClickLast] : q31(clickWords + 4 * std::min(j, 31));
			const float b = q31(clickWords + 4 * std::min(j + 1, 31));
			const float click = a + (b - a) * t;
			seed = seed * 1664525u + 1013904223u;
			const float white = static_cast<int32_t>(seed) / 2147483648.0f;
			const float strike = f[Strike];

			float x = 0;
			switch(_m)
			{
			case Machine::SnareHard:
			{
				// the coupled, swept VCOs
				const float sweep = 1 + 1.0f * strike;
				f[Phase1] += inc1 * sweep;
				const float o1 = shapedTriangle(f[Phase1]);
				f[Phase2] += inc2 * sweep + 0.01f * o1;
				const float o2 = shapedTriangle(f[Phase2]);
				const float tone = 0.4f * (o1 * e[0] + o2 * e[1]);
				// the noise: low-passed, then a narrow branch on the long envelope and a wide one on the shorter
				float l, bd, h, l2, b2, h2;
				filter[0].run(svf, white, l, bd, h);
				filter[1].run(svf + 2, l, l2, b2, h2);
				// the noise's VCA overdriven by its contour: held at full for about one time constant, then the
				// exponential (the classic 909-style snare's noise holds 40-70 ms)
				const float held3 = std::min(1.0f, 2.5f * e[3]), held2 = std::min(1.0f, 2.5f * e[2]);
				const float noise = 10 * heads * noiseLevel * (1.5f * b2 * held3 + 0.8f * l * held2);
				x = softClip(tone + noise + 2 * click);
				break;
			}
			case Machine::SnareClassic:
			{
				// two damped sines, the balance knob from them to the snappy noise
				const float balance = word(_io, 7);
				f[Phase1] += inc1;
				f[Phase2] += inc2;
				const float upper = frame[8 + 2] / 127.0f;		// A0's peak: the TONE word
				const float shells = 0.7f * (sine(f[Phase1]) * e[2] + upper * sine(f[Phase2]) * e[3]);
				float l, bd, h;
				filter[0].run(svf, white, l, bd, h);
				const float noise = 3 * heads * noiseLevel * h * e[2];
				x = 2 * (1 - balance) * softClip(1.4f * shells) + 2 * balance * noise + 0.8f * click;
				break;
			}
			case Machine::SnareFm:
			{
				// the carrier, its modulator fed back, the index falling with MOD DECAY (B1), noise in the phase
				const float index = std::fabs(static_cast<float>(static_cast<int16_t>(be16(frame + 6)))) / 4096.0f;
				const float blip = 1 + 0.6f * e[0];
				f[Phase2] += inc2 * blip;
				const float m = sine(f[Phase2] + 0.35f * f[Feedback]);
				f[Feedback] = m;
				float l, bd, h;
				filter[0].run(svf, white, l, bd, h);
				const float grain = 3 * heads * noiseLevel * bd;
				f[Phase1] += inc1 * blip;
				const float carrier = sine(f[Phase1] + index * e[3] * m + grain * e[2]);
				x = 0.7f * carrier * e[2] + 0.5f * h * heads * noiseLevel * e[2] + click;
				break;
			}
			case Machine::SnareNatural:
			{
				// one triangle VCO and a noise through the swept, resonant four-pole low-pass
				const float snap = word(_io, 4);
				f[Phase2] += inc2;
				const float tone = (1.2f - snap) * 0.6f * shapedTriangle(f[Phase2]) * e[3] * f[Body];
				float l1, b1, h1, l2, b2, h2;
				filter[0].run(svf, white, l1, b1, h1);
				filter[1].run(svf + 2, l1, l2, b2, h2);
				const float noise = 2.5f * heads * noiseLevel * l2 * e[3];
				x = softClip(tone + noise + 0.8f * click);
				break;
			}
			case Machine::SnareAcoustic:
			{
				// the body sample and two modes; the wires gated by the body's movement
				const float body = click * f[Body];
				f[Phase1] += inc1;
				f[Phase2] += inc2;
				const float modes = 0.4f * (sine(f[Phase1]) * e[0] + 0.7f * sine(f[Phase2]) * e[1]);
				f[Follow] = std::max(std::fabs(body + modes), f[Follow] * followA);
				float l, bd, h;
				filter[0].run(svf, white, l, bd, h);
				const float wires = 10 * heads * noiseLevel * bd * (0.3f * e[3] + 2 * f[Follow]);
				x = softClip(2.5f * body + modes + wires);
				break;
			}
			case Machine::RimHard:
			{
				// three damped resonators struck by the pulse, then a hard clip
				const float pulse = 40 * click;
				const float r = frame[8 + 2] / 127.0f * ring[0].ring(res, pulse) + 6 * heads * ring[1].ring(res + 2, pulse) +
					0.5f * ring[2].ring(res + 4, pulse);
				float l, bd, h;
				filter[0].run(svf, white, l, bd, h);
				const float noise = word(_io, 6) * bd * e[3];
				const float y = 3.5f * r + noise;
				x = 0.7f * std::max(-1.0f, std::min(1.0f, y));
				break;
			}
			default:	// RimClassic
			{
				// two resonators crossfaded, an asymmetric diode, a very short VCA, a low-pass
				const float balance = word(_io, 3);
				const float pulse = 40 * click;
				const float r = 2 * (1 - balance) * ring[0].ring(res, pulse) +
					12 * heads * balance * ring[1].ring(res + 2, pulse);
				float l, bd, h;
				filter[0].run(svf, white, l, bd, h);
				const float y = 2 * r + word(_io, 5) * bd * e[2];
				const float diode = y > 0 ? y : 0.25f * y;
				const float vca = softClip(1.5f * diode) * (0.3f + 0.7f * e[2]);
				f[Lp] = lpA * f[Lp] + (1 - lpA) * vca;
				x = f[Lp];
				break;
			}
			}
			f[Phase1] -= std::floor(f[Phase1]);
			f[Phase2] -= std::floor(f[Phase2]);
			f[Body] *= bodyFall;
			f[Strike] = strike * strikeFall;

			// the output stage saturates softly, as an overdriven mixer does: within 1.2 of full scale
			const float sat = x / std::sqrt(1 + x * x * (1 / 1.44f));
			f[Dc] = dcA * (f[Dc] + sat - f[DcIn]);
			f[DcIn] = sat;
			_out[i] += f[Dc];
			for(int k = 0; k < 4; ++k)
				e[k] *= f[static_cast<size_t>(Coef0 + k)];
		}
		_c.u[Seed] = seed;
		f[ClickLast] = q31(clickWords + 124);
		// a sound that has ended rests at exact zero: no denormal tail, no noise floor
		for(int k = 0; k < 4; ++k)
			if(e[k] < 1e-7f)
				e[k] = 0;
		if(f[Strike] < 1e-7f)
			f[Strike] = 0;
		// a resonator or follower that has rung out would sink into denormals while its envelope still runs
		for(int k = Feedback; k < Count; ++k)
			if(std::fabs(f[static_cast<size_t>(k)]) < 1e-20f)
				f[static_cast<size_t>(k)] = 0;
		bool rest = e[0] + e[1] + e[2] + e[3] == 0 && std::fabs(f[Dc]) < 1e-9f;
		for(int k = Res0; k <= Res2 + 1 && rest; ++k)
			rest = std::fabs(f[static_cast<size_t>(k)]) < 1e-9f;
		if(rest)
			for(int k = Dc; k < Count; ++k)
				if(k != Body && k != Inc1 && k != Inc2)
					f[static_cast<size_t>(k)] = 0;
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
		case Machine::SnareClassic:		return {64, 60, 0, 80, 90, 64, 30, 30};
		case Machine::SnareFm:			return {64, 48, 70, 60, 80, 30, 40, 30};
		case Machine::SnareNatural:		return {64, 60, 64, 64, 80, 40, 90, 30};
		case Machine::SnareAcoustic:	return {64, 105, 64, 64, 40, 80, 100, 30};
		case Machine::RimHard:			return {64, 70, 90, 60, 110, 64, 30, 20};
		default:						return {64, 66, 70, 64, 80, 90, 30, 20};
		}
	}
}
