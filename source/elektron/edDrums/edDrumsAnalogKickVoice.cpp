#include "edDrumsAnalogVoice.h"

#include <algorithm>
#include <cmath>

// The kicks' circuits (tracks 9-11), our own, one structure each (research/28-circuits-classiques.md): what differs
// between them is the topology, the waveshape, the resonances and where the transient comes from, not only constants.
// Each plays what its control computes: the CV words (pitch), the envelope segments (level, sweep, index), the click the
// control sends (outA), the noise level (heads) and its settings, the switches. The laws from those words to Hz,
// seconds and levels are chosen here (see each), not the original's.
//
//   HARD THUMP  a free triangle VCO rounded by a diode shaper, a held then falling sweep, a transient of its own: the
//               control's pulse low-passed at 5 kHz plus a burst of low-passed noise, into a saturating VCA (909-like)
//   NOISE KICK  a pinged bridged-T resonator, no VCO: the trig pulse's two edges ring it, its pitch jumps up for about
//               6 ms at the attack then sighs half a semitone; its noise styles are a short noise transient (808-like)
//   PHASE KICK  two-operator FM at an inharmonic ratio (the modulator at 1/sqrt 2 of its CV), the index falling fast:
//               the attack is the FM spectrum itself
//   TOY KICK    two oscillators at the same tune an octave up, FM (and ring modulation with switch 4 off), a quick drop
//               and a high-passed tick
//   SOFT KICK   a pinged resonator rung by a smooth half-sine (an oscillator's soft click, no edge), a slow sweep, faint
//               dust gated by the body (606/808-like, silky)
//   SNAP KICK   a rich saw or square VCO, free running or reset by the trig switch, saturated and low-passed, with a
//               click of band-passed noise over the control's click
//   BODY KICK   an acoustic kick: two close membrane modes beating, four short inharmonic upper modes, a shell resonance,
//               all struck by the control's body sample and a beater's burst of noise; almost no sweep
//
// Names (ticket 27), Machine 1-8 as kickParams lays them out:
//   HARD THUMP  TUNE, DECAY, SWEEP, SWEEP TIME, HOLD, CLICK, TONE/CLICK, DRIVE
//   NOISE KICK  TUNE, DECAY, SWEEP, SWEEP TIME, HOLD, NOISE STYLE, TONE/CLICK, DRIVE
//   PHASE KICK  TUNE, DECAY, SWEEP TIME, FM AMOUNT, FM FREQ, FM DECAY, DRIVE, (none)
//   TOY KICK    TUNE, DECAY, SWEEP, SWEEP TIME, TONE, CLICK, OVERDRIVE, DRIVE
//   SOFT KICK   TUNE, DECAY, SWEEP, SWEEP TIME, HOLD, NOISE, TONE, DRIVE
//   SNAP KICK   TUNE, DECAY, SWEEP, SWEEP TIME, HOLD, CLICK, WAVE/CLICK, DRIVE
//   BODY KICK   TUNE, DECAY, SWEEP, SWEEP TIME, HOLD, BODY, OVERDRIVE, DRIVE
namespace elektron::drums::analog::circuit
{
	namespace
	{
		constexpr double Pi = 3.14159265358979323846;

		// The circuit's state, by slot
		namespace slot
		{
			constexpr int Phase = 0;		// the VCO's phase, in turns
			constexpr int Phase2 = 1;		// the second oscillator's
			constexpr int Seg = 2;			// 4: the envelope segments' values
			constexpr int Sweep = 6;		// the pitch sweep, in octaves
			constexpr int Index = 7;		// FM: the index envelope
			constexpr int Click = 8;		// the click path's two filter states
			constexpr int Click2 = 9;
			constexpr int NoiseA = 10;		// the noise path's four filter states
			constexpr int NoiseB = 11;
			constexpr int NoiseC = 12;
			constexpr int NoiseD = 13;
			constexpr int NoiseEnv = 14;	// the noise burst's envelope
			constexpr int LastClick = 15;	// the last 48 kHz click sample of the tick before
			constexpr int Rise = 16;		// resonators: the attack's pitch rise, in octaves
			constexpr int Sigh = 17;		// their slow sigh, in octaves
			constexpr int Excite = 18;		// SOFT KICK: its half-sine's phase (0-1, 1 when done)
			constexpr int Tone = 19;		// the output's lowpass
			constexpr int Fb = 20;			// FM: the modulator's last output (feedback)
			constexpr int LastHz = 21;		// the body's last pitch the control gave
			constexpr int Leak = 22;		// NOISE KICK: the trig pulse's leak to the output, its high-pass (output, input)
			constexpr int LeakIn = 23;
			constexpr int Res = 24;			// 2 x 6: the resonators' two last outputs
			constexpr int Resonators = 6;

			constexpr int Cmd = 0;			// u: 4, the segments' command longs of the tick before
			constexpr int Seed = 4;			// u: the noise generator
			constexpr int NoiseOn = 5;		// u: a burst has started
			constexpr int Struck = 6;		// u: the body's level segment has had its attack
			constexpr int Since = 7;		// u: host samples since that attack (for the trig pulse's second edge)
		}

		// Which envelope segments each kick's control drives (from its control's notes)
		struct Wiring
		{
			int amp;			// the body's level
			int pitch;			// the sweep
			float levelFull;	// the pitch segment's level byte (signed) that sweeps the machine's full depth
			int bodyCv;			// the CV word of the body's pitch
		};

		const Wiring& wiringOf(const Machine _m)
		{
			static const Wiring wirings[] = {
				{3, 1, 127, 1}, {3, 1, 63, 1}, {2, 0, -128, 0}, {2, 0, 15, 1}, {2, 0, 15, 0}, {3, 1, 15, 1}, {2, 1, 15, 1},
			};
			return wirings[std::min(static_cast<int>(_m), 6)];
		}

		// A CV word (0-32767, linear in frequency, as the control's exponential gives it) in Hz: 1046, a default kick at
		// note 60, is 50 Hz
		double cvHz(const int _cv) { return _cv * (50.0 / 1046.0); }

		// The envelope generator's rate code (octave << 16 | mantissa) as what a segment keeps of its distance to its
		// target per step of the generator: the step below full scale, less the 2^-16 the generator adds back, as the
		// control's decayTicks measures it. A step is 2 samples of 48 kHz (24 kHz): the control adds a decay's step count
		// to its countdown, which falls 16 a 32-sample block. So a 60 dB fall takes 20 ms at octave 6, 5 s at octave 13,
		// and the code 0xfffff holds, as the control's countdown then never ends
		constexpr double StepRate = 24000.0;
		double keepPerSample(const uint32_t _code)
		{
			const auto step = static_cast<double>((0x7fffffffu - ((_code & 0xffffu) << 14)) >> (_code >> 16 & 0xf));
			return std::clamp(1.0 - (step - 0x7fff) / 2147483648.0, 0.0, 1.0);
		}

		float noiseSample(uint32_t& _seed)
		{
			_seed = _seed * 1664525u + 1013904223u;
			return static_cast<float>(static_cast<int32_t>(_seed)) * (1.0f / 2147483648.0f);
		}

		// A one-pole lowpass coefficient for _hz at _rate
		float onePole(const double _hz, const double _rate)
		{
			return static_cast<float>(1.0 - std::exp(-2 * Pi * std::min(_hz, 0.45 * _rate) / _rate));
		}
		// What a sound keeps a host sample to fall 60 dB in _seconds
		float keepFor(const double _seconds, const double _rate)
		{
			return static_cast<float>(std::pow(1e-3, 1.0 / (_seconds * _rate)));
		}

		float flush(const float _v) { return std::fabs(_v) < 1e-9f ? 0.0f : _v; }

		// NOISE KICK: the trig pulse's level at the output, past the tone filter, and what of the noise burst rings the
		// bridge (a unit sample rings it at unit amplitude: a burst of hundreds of samples needs a small share)
		constexpr float PulseLeak = 0.5f;
		constexpr float NoiseIntoBridge = 0.01f;

		float wrap(const float _turns) { return _turns >= 1.0f ? _turns - std::floor(_turns) : _turns; }

		// sin(2 pi _x) for _x in turns (0-1): a parabola, refined once (within 0.1 %)
		float sineTurn(const float _x)
		{
			const float t = _x - 0.5f;
			const float y = 8 * t - 16 * t * std::fabs(t);
			return -(0.225f * (y * std::fabs(y) - y) + y);
		}
		float cosTurn(const float _x) { return sineTurn(wrap(_x + 0.25f)); }

		// tanh's shape as a rational curve, flat past 3
		float saturate(const float _x)
		{
			const float x = std::clamp(_x, -3.0f, 3.0f);
			return x * (27 + x * x) / (27 + 9 * x * x);
		}

		// A resonator (a damped sinusoid struck by its input) in the coupled form, a rotation by the pitch's angle scaled
		// by what it keeps a sample: at a low pitch the direct form's rounding keeps a resonance alive (a limit cycle at
		// -120 dB), this one decays to nothing. A unit impulse rings at unit amplitude. State: the real and imaginary parts
		struct Resonator
		{
			float c = 0, s = 0;
			void tune(const double _hz, const float _keep, const double _rate)
			{
				const double w = 2 * Pi * std::min(_hz, 0.45 * _rate) / _rate;
				c = static_cast<float>(_keep * std::cos(w));
				s = static_cast<float>(_keep * std::sin(w));
			}
			// one sample with input _x: returns the real part
			float step(float* _r, const float _x) const
			{
				const float re = c * _r[0] - s * _r[1] + _x;
				_r[1] = s * _r[0] + c * _r[1];
				_r[0] = re;
				return re;
			}
		};

		// What a tick of every kick reads from its control
		struct Tick
		{
			float keep[4], target[4];
			bool attackStarts[4], releaseStarts[4];
			float sweepLevel;		// the pitch segment's level byte, signed, over the machine's full
			double bodyHz, secondHz;
			unsigned cv2, cv3;
			uint32_t switches;
			float noiseLevel;		// heads, Q31
			float click[33];		// outA across the tick, the last sample of the tick before first, in full scale
			bool clicking;
			bool sw(const int _k) const { return (switches >> _k & 1) != 0; }
			// the accent CV (23274 at its middle) as a saturation drive, 1 to 4
			float drive() const { return 1.0f + 3.0f * static_cast<float>(std::max(cv2, cv3)) / 65535.0f; }
			// outA at host sample _i of the tick, linearly between the 48 kHz samples
			float clickAt(const int _i, const double _perHost) const
			{
				const double pos = _i * _perHost;
				const int k = static_cast<int>(pos);
				return click[k] + static_cast<float>(pos - k) * (click[k + 1] - click[k]);
			}
		};

		Tick readTick(const Machine _m, const VoiceIo& _io, Circuit& _c, const double _rate)
		{
			const Wiring& w = wiringOf(_m);
			Tick t{};
			// the envelope segments: each slews toward its level byte at its rate while its command is an attack (0xf)
			// or a release (0xb); any other command clears it
			const uint8_t* env = _io.frame.data() + 8;
			const double stepsPerHost = StepRate / _rate;
			for(int s = 0; s < 4; ++s)
			{
				const uint8_t* seg = env + 8 * s;
				const uint32_t lng = be32(seg + 4);
				const uint32_t cmd = lng >> 28;
				// a new attack or release: the command long written anew (its level or rate changed with it)
				const uint32_t prev = _c.u[static_cast<size_t>(slot::Cmd + s)];
				t.attackStarts[s] = cmd == 0xf && lng != prev;
				t.releaseStarts[s] = cmd == 0xb && lng != prev;
				_c.u[static_cast<size_t>(slot::Cmd + s)] = lng;
				if(cmd == 0xf || cmd == 0xb)
				{
					t.keep[s] = static_cast<float>(std::pow(keepPerSample(lng & 0xfffff), stepsPerHost));
					t.target[s] = seg[2] / 127.0f;
				}
				else
				{
					t.keep[s] = 0;
					t.target[s] = 0;
					_c.f[static_cast<size_t>(slot::Seg + s)] = 0;
				}
			}
			if(t.attackStarts[w.amp])
			{
				_c.u[slot::Struck] = 1;
				_c.u[slot::Since] = 0;
			}
			t.sweepLevel = std::min(1.0f, static_cast<int8_t>(env[8 * w.pitch + 2]) / w.levelFull);
			t.bodyHz = cvHz(be16(_io.frame.data() + 2 * w.bodyCv));
			// the control zeroes its CVs once its envelope has ended: what still rings keeps the tune it had (a
			// resonator at 0 Hz would be a double pole that swells before it dies)
			if(t.bodyHz > 0)
				_c.f[slot::LastHz] = static_cast<float>(t.bodyHz);
			else
				t.bodyHz = _c.f[slot::LastHz];
			t.secondHz = cvHz(be16(_io.frame.data() + (w.bodyCv == 0 ? 2 : 0)));
			t.cv2 = be16(_io.frame.data() + 4);
			t.cv3 = be16(_io.frame.data() + 6);
			t.switches = _io.switches;
			t.noiseLevel = std::fabs(static_cast<float>(static_cast<int32_t>(be32(_io.heads.data())))) / 2147483648.0f;
			if(t.noiseLevel > 0 && _c.u[slot::NoiseOn] == 0)
			{
				_c.u[slot::NoiseOn] = 1;
				_c.f[slot::NoiseEnv] = 1;
			}
			t.click[0] = _c.f[slot::LastClick];
			t.clicking = t.click[0] != 0;
			for(int k = 0; k < 32; ++k)
			{
				t.click[k + 1] = static_cast<float>(static_cast<int32_t>(be32(_io.outA.data() + 4 * k))) * (1.0f / 2147483648.0f);
				t.clicking = t.clicking || t.click[k + 1] != 0;
			}
			_c.f[slot::LastClick] = t.click[32];
			return t;
		}

		// The body's level segment, sample by sample
		struct Level
		{
			float value, target, keep;
			Level(const Tick& _t, Circuit& _c, const int _seg)
				: value(_c.f[static_cast<size_t>(slot::Seg + _seg)]), target(_t.target[_seg]), keep(_t.keep[_seg]) {}
			float step() { return value = target + (value - target) * keep; }
			// under -120 dB it is over
			void store(Circuit& _c, const int _seg) const
			{
				_c.f[static_cast<size_t>(slot::Seg + _seg)] = std::fabs(value) < 1e-6f ? 0.0f : value;
			}
		};

		// The VCO sweep most kicks share: from the pitch segment's level, falling at its rate from the attack (or held
		// through it and falling at the release); in octaves it falls _slower times slower than a level at the same rate
		// (2, by ear: a 60 ms code sweeps for about 120 ms). Returns the pitch factor at the tick's start and its step
		struct Bend
		{
			float from, step, end;
		};
		Bend sweepOf(const Tick& _t, Circuit& _c, const Machine _m, const float _octaves, const bool _onAttack,
			const float _slower, const int _n)
		{
			const Wiring& w = wiringOf(_m);
			float& sweep = _c.f[slot::Sweep];
			if(_t.attackStarts[w.pitch])
				sweep = _octaves * _t.sweepLevel;
			const bool falling = _onAttack || _c.u[static_cast<size_t>(slot::Cmd + w.pitch)] >> 28 == 0xb;
			if(!_onAttack && _t.releaseStarts[w.pitch])
				sweep = std::max(sweep, _octaves * _t.sweepLevel);
			const float keep = falling ? std::pow(_t.keep[w.pitch], 1.0f / _slower) : 1.0f;
			const auto end = static_cast<float>(sweep * std::pow(static_cast<double>(keep), _n));
			Bend b;
			b.from = static_cast<float>(std::exp2(sweep));
			b.step = (static_cast<float>(std::exp2(end)) - b.from) / static_cast<float>(_n);
			b.end = end;
			sweep = flush(end);
			return b;
		}

		// A noise burst from the control's settings: lowpass at 200 Hz 2^(s/16) for the first two, a burst falling 60 dB
		// in 1 ms 2^(s/16) for the third, a highpass at 20 Hz 2^(s/16) for the fourth; added at _gain x the noise level x
		// _gate (the body's level, or 1)
		void noiseBurst(const VoiceIo& _io, Circuit& _c, const Tick& _t, float* _out, const float* _gate, const int _n,
			const double _rate, const float _gain)
		{
			auto& f = _c.f;
			float& env = f[slot::NoiseEnv];
			if(_gain <= 0 || _t.noiseLevel <= 0 || env < 1e-6f)
			{
				env = f[slot::NoiseA] = f[slot::NoiseB] = f[slot::NoiseC] = f[slot::NoiseD] = 0;
				return;
			}
			const auto bytesHz = [](const int _b, const double _base) { return _base * std::exp2(_b / 16.0); };
			const float lp1 = onePole(bytesHz(_io.noise[0], 200), _rate);
			const float lp2 = onePole(bytesHz(_io.noise[1], 200), _rate);
			const float keep = keepFor(bytesHz(_io.noise[2], 1e-3), _rate);
			const auto hp = static_cast<float>(std::exp(-2 * Pi * bytesHz(_io.noise[3], 20) / _rate));
			float hpIn = f[slot::NoiseA], hpOut = f[slot::NoiseB], n1 = f[slot::NoiseC], n2 = f[slot::NoiseD];
			uint32_t seed = _c.u[slot::Seed];
			const float gain = _gain * _t.noiseLevel;
			for(int i = 0; i < _n; ++i)
			{
				const float x = noiseSample(seed);
				hpOut = hp * (hpOut + x - hpIn);
				hpIn = x;
				n1 += lp1 * (hpOut - n1);
				n2 += lp2 * (n1 - n2);
				env *= keep;
				_out[i] += gain * env * (_gate ? _gate[i] : 1.0f) * n2;
			}
			f[slot::NoiseA] = flush(hpIn);
			f[slot::NoiseB] = flush(hpOut);
			f[slot::NoiseC] = flush(n1);
			f[slot::NoiseD] = flush(n2);
			env = flush(env);
			_c.u[slot::Seed] = seed;
		}

		float* res(Circuit& _c, const int _k) { return _c.f.data() + slot::Res + 2 * _k; }
		void storeResonators(Circuit& _c)
		{
			for(int k = 0; k < 2 * slot::Resonators; ++k)
				_c.f[static_cast<size_t>(slot::Res + k)] = std::fabs(_c.f[static_cast<size_t>(slot::Res + k)]) < 1e-7f
					? 0.0f : _c.f[static_cast<size_t>(slot::Res + k)];
		}

		// HARD THUMP: a 909-like VCO body and a transient on a path of its own
		void hard(const VoiceIo& _io, Circuit& _c, const Tick& _t, float* _out, const int _n, const double _rate)
		{
			auto& f = _c.f;
			const double perHost = 48000.0 / _rate;
			Level level(_t, _c, 3);
			const Bend bend = sweepOf(_t, _c, Machine::KickHard, 4.0f, false, 2.0f, _n);
			// the VCO starts a quarter period in, at its peak, as a held triangle would
			if(_t.attackStarts[3])
				f[slot::Phase] = 0.25f;
			float phase = f[slot::Phase];
			const auto inc = static_cast<float>(_t.bodyHz / _rate);
			const float drive = _t.drive();
			// the transistor VCA saturates with the level: the body hits hard at the attack, rounds as it falls
			float gate[64];
			for(int i = 0; i < _n; ++i)
			{
				const float a = level.step();
				gate[i] = a;
				phase = wrap(phase + inc * (bend.from + bend.step * static_cast<float>(i)));
				// a triangle, rounded toward a sine by a diode: a cubic, which leaves weak odd harmonics
				const float tri = 4 * std::fabs(phase - 0.5f) - 1;
				const float body = 1.5f * tri - 0.5f * tri * tri * tri;
				// switch 8 (TONE/CLICK at its top) leaves more of the triangle's edge
				const float shaped = _t.sw(8) ? 0.6f * body + 0.4f * tri : body;
				_out[i] += 0.8f * saturate(drive * a * shaped) / saturate(drive);
			}
			f[slot::Phase] = phase;
			level.store(_c, 3);

			// the transient: the control's pulse through a lowpass at 5 kHz with a Q of 2 (switch 7 off damps it), plus
			// the control's noise, low-passed by its settings, through a VCA of its own
			float lp = f[slot::Click], bp = f[slot::Click2];
			if(_t.clicking || lp != 0 || bp != 0)
			{
				const auto g = static_cast<float>(2 * std::sin(Pi * 5000 / _rate));
				const float gain = _t.sw(7) ? 3.0f : 1.0f;
				for(int i = 0; i < _n; ++i)
				{
					const float x = _t.clickAt(i, perHost);
					lp += g * bp;
					bp += g * (x - lp - 0.5f * bp);
					_out[i] += gain * lp;
				}
			}
			f[slot::Click] = flush(lp);
			f[slot::Click2] = flush(bp);
			noiseBurst(_io, _c, _t, _out, nullptr, _n, _rate, 0.6f);
			(void)gate;
		}

		// The 808-like pinged resonator NOISE KICK and SOFT KICK share: a two-pole resonator at the body's pitch, no
		// VCO, its decay the level segment's (60 dB in what its release sets; 10 s while the attack holds: HOLD sustains it). A new Hit
		// rings it again without stopping it. Returns the resonator's output added to _out at _gain
		void pinged(Circuit& _c, const Tick& _t, const int _seg, float* _out, const float* _excite, const float* _octaves,
			const int _n, const double _rate, const float _gain, const float _drive)
		{
			float* r = res(_c, 0);
			const float keep = _c.u[static_cast<size_t>(slot::Cmd + _seg)] >> 28 == 0xb ? _t.keep[_seg]
				: keepFor(10.0, _rate);
			const auto base = static_cast<float>(_t.bodyHz / _rate);
			Resonator body;
			for(int i = 0; i < _n; ++i)
			{
				// its pitch per sample: the rise and the sigh bend the resonator itself
				const float turns = std::min(0.45f, base * std::exp2(_octaves[i]));
				body.c = keep * cosTurn(turns);
				body.s = keep * sineTurn(turns);
				const float y = body.step(r, _excite[i]);
				_out[i] += _gain * saturate(_drive * y) / saturate(_drive);
			}
		}

		// NOISE KICK: the trig pulse's two edges (a full step up, back down a diode's drop 1 ms later) ring the
		// resonator; its pitch rises 1.2 octaves x SWEEP for about 6 ms, then sighs down half a semitone at the rate
		// SWEEP TIME gives. The attack is the 808's: the pulse also leaks to the output past the tone filter, a click of
		// its two edges. Its noise styles' burst rings the bridge too, as any input of the 808's bridged-T does, at the
		// control's level: no path of its own to the output
		void classic(const VoiceIo& _io, Circuit& _c, const Tick& _t, float* _out, const int _n, const double _rate)
		{
			auto& f = _c.f;
			const double perHost = 48000.0 / _rate;
			Level level(_t, _c, 3);
			for(int i = 0; i < _n; ++i)
				level.step();
			level.store(_c, 3);
			if(_t.attackStarts[1])
			{
				f[slot::Rise] = 1.2f * std::max(0.0f, _t.sweepLevel);
				f[slot::Sigh] = 1.0f / 24;
			}
			const float riseKeep = keepFor(0.006, _rate);
			const float sighKeep = std::pow(_t.keep[1] > 0 ? _t.keep[1] : 1.0f, 0.25f);
			float excite[64], octaves[64];
			uint32_t& since = _c.u[slot::Since];
			const auto edge = static_cast<uint32_t>(0.001 * _rate);
			float tone = f[slot::Tone];
			// the noise styles' burst, at the control's level, into the bridge
			float burst[64] = {};
			noiseBurst(_io, _c, _t, burst, nullptr, _n, _rate, 1.0f);
			// the pulse's leak: through the output's coupling, a high-pass at 1.5 kHz
			const auto leakA = static_cast<float>(std::exp(-2 * Pi * 1500 / _rate));
			float leak[64];
			for(int i = 0; i < _n; ++i)
			{
				const float pulse = _c.u[slot::Struck] != 0 && since <= edge ? 1.0f : 0.0f;
				f[slot::Leak] = leakA * (f[slot::Leak] + pulse - f[slot::LeakIn]);
				f[slot::LeakIn] = pulse;
				leak[i] = f[slot::Leak];
				f[slot::Rise] *= riseKeep;
				f[slot::Sigh] *= sighKeep;
				octaves[i] = f[slot::Rise] + f[slot::Sigh];
				float e = 0.1f * _t.clickAt(i, perHost);
				if(_c.u[slot::Struck] != 0)
				{
					if(since == 0)
						e += 1.0f;
					else if(since == edge)
						e -= 0.17f;
					if(since <= edge)
						++since;
				}
				excite[i] = e + NoiseIntoBridge * burst[i];
			}
			f[slot::Leak] = flush(f[slot::Leak]);
			f[slot::Rise] = flush(f[slot::Rise]);
			f[slot::Sigh] = flush(f[slot::Sigh]);
			float body[64] = {};
			pinged(_c, _t, 3, body, excite, octaves, _n, _rate, 0.8f, _t.drive());
			// the tone control after the sum: a lowpass at 4 f0, which takes the edges' click down; switch 8 opens it
			// (the control zeroes its CVs once its envelope has ended: the filter keeps a floor of 40 Hz to settle)
			const float lp = onePole(std::max(40.0, _t.bodyHz) * (_t.sw(8) ? 16 : 4), _rate);
			for(int i = 0; i < _n; ++i)
			{
				tone += lp * (body[i] - tone);
				_out[i] += tone + 0.15f * (body[i] - tone) * (_t.sw(7) ? 1.0f : 0.3f) + PulseLeak * leak[i];
			}
			f[slot::Tone] = flush(tone);
			storeResonators(_c);
		}

		// SOFT KICK: the resonator rung by a smooth half-sine of 3 ms, an oscillator's soft click with no edge; a slow
		// VCO-like sweep bends it; dust: sparse soft crackles, as tape static, at the noise level, gated by the body
		void silky(const VoiceIo& _io, Circuit& _c, const Tick& _t, float* _out, const int _n, const double _rate)
		{
			auto& f = _c.f;
			Level level(_t, _c, 2);
			float gate[64];
			for(int i = 0; i < _n; ++i)
				gate[i] = level.step();
			level.store(_c, 2);
			const Bend bend = sweepOf(_t, _c, Machine::KickSilky, 2.0f, true, 4.0f, _n);
			if(_t.attackStarts[2])
				f[slot::Excite] = 0;
			const auto exciteInc = static_cast<float>(1.0 / (0.003 * _rate));
			const float sweepFrom = std::log2(bend.from);
			// its noise is the dust below, not a burst
			f[slot::NoiseEnv] = 0;
			float excite[64], octaves[64];
			for(int i = 0; i < _n; ++i)
			{
				float e = 0;
				if(_c.u[slot::Struck] != 0 && f[slot::Excite] < 1)
				{
					e = 0.06f * sineTurn(0.5f * f[slot::Excite]);
					f[slot::Excite] = std::min(1.0f, f[slot::Excite] + exciteInc);
				}
				excite[i] = e;
				octaves[i] = sweepFrom + (bend.end - sweepFrom) * static_cast<float>(i) / static_cast<float>(_n);
			}
			float body[64] = {};
			pinged(_c, _t, 2, body, excite, octaves, _n, _rate, 0.75f, _t.drive());
			// the tone CV (cv3) rounds it: a lowpass from 3 to 12 f0
			const float lp = onePole(std::max(40.0, _t.bodyHz) * (3 + 9 * _t.cv3 / 65535.0), _rate);
			float tone = f[slot::Tone];
			for(int i = 0; i < _n; ++i)
			{
				tone += lp * (body[i] - tone);
				_out[i] += tone;
			}
			f[slot::Tone] = flush(tone);
			storeResonators(_c);

			// dust: about 300 crackles a second, each a tiny step through a lowpass at 3 kHz
			float& d = f[slot::NoiseC];
			uint32_t seed = _c.u[slot::Seed];
			if(_t.noiseLevel > 0 && level.value > 0)
			{
				const auto chance = static_cast<uint32_t>(300.0 / _rate * 4294967296.0);
				const float dl = onePole(3000, _rate);
				for(int i = 0; i < _n; ++i)
				{
					seed = seed * 1664525u + 1013904223u;
					const float pop = seed < chance ? (seed & 1 ? 1.0f : -1.0f) : 0.0f;
					d += dl * (pop - d);
					_out[i] += 2.5f * _t.noiseLevel * gate[i] * d;
				}
			}
			else
				d = 0;
			d = flush(d);
			_c.u[slot::Seed] = seed;
			(void)_io;
		}

		// PHASE KICK: the carrier on the body CV, the modulator on the second CV at 1/sqrt 2 (an inharmonic ratio), its
		// own feedback; the index starts whole at its segment's attack and falls at FM DECAY's rate; FM AMOUNT (cv3,
		// 0 to about 13000) sets its depth, up to 10 radians
		void fm(const VoiceIo& _io, Circuit& _c, const Tick& _t, float* _out, const int _n, const double _rate)
		{
			auto& f = _c.f;
			Level level(_t, _c, 2);
			const Bend bend = sweepOf(_t, _c, Machine::KickFm, 2.0f, false, 2.0f, _n);
			if(_t.attackStarts[1])
				f[slot::Index] = 1.0f;
			const float indexKeep = _t.keep[1];
			float index = f[slot::Index], phase = f[slot::Phase], phase2 = f[slot::Phase2], fb = f[slot::Fb];
			const auto inc = static_cast<float>(_t.bodyHz / _rate);
			const auto inc2 = static_cast<float>(_t.secondHz / std::sqrt(2.0) / _rate);
			const float depth = 10.0f * static_cast<float>(_t.cv3) / 13000.0f / static_cast<float>(2 * Pi);
			const float drive = _t.drive();
			for(int i = 0; i < _n; ++i)
			{
				const float a = level.step();
				index *= indexKeep;
				const float b = bend.from + bend.step * static_cast<float>(i);
				phase = wrap(phase + inc * b);
				phase2 = wrap(phase2 + inc2 * b);
				const float m = sineTurn(wrap(phase2 + 0.15f * fb + 1.0f));
				fb = m;
				const float x = sineTurn(wrap(phase + depth * index * m + 2.0f));
				_out[i] += 0.75f * saturate(drive * a * x) / saturate(drive);
			}
			level.store(_c, 2);
			f[slot::Index] = flush(index);
			f[slot::Phase] = phase;
			f[slot::Phase2] = phase2;
			f[slot::Fb] = flush(fb);
			// the control's pulse, faint, low-passed: the FM does the attack
			const double perHost = 48000.0 / _rate;
			float lp = f[slot::Click];
			if(_t.clicking || lp != 0)
			{
				const float g = onePole(3000, _rate);
				for(int i = 0; i < _n; ++i)
				{
					lp += g * (_t.clickAt(i, perHost) - lp);
					_out[i] += 0.5f * lp;
				}
			}
			f[slot::Click] = flush(lp);
			noiseBurst(_io, _c, _t, _out, nullptr, _n, _rate, 0.3f);
		}

		// TOY KICK: two oscillators at the same tune an octave up (a high body), a quick drop, FM between them (the
		// tone CV the depth), ring modulation as well with switch 4 off; the control's trapezoid click, high-passed at
		// 1.5 kHz, a tick
		void plastic(const VoiceIo& _io, Circuit& _c, const Tick& _t, float* _out, const int _n, const double _rate)
		{
			auto& f = _c.f;
			const double perHost = 48000.0 / _rate;
			Level level(_t, _c, 2);
			const Bend bend = sweepOf(_t, _c, Machine::KickPlastic, 3.0f, true, 1.0f, _n);
			float phase = f[slot::Phase], phase2 = f[slot::Phase2];
			const auto inc = static_cast<float>(2 * _t.bodyHz / _rate), inc2 = inc * 1.007f;
			const float depth = static_cast<float>(_t.cv3) / 8192.0f * 0.25f;
			const bool ring = !_t.sw(4);
			const float drive = _t.drive();
			for(int i = 0; i < _n; ++i)
			{
				const float a = level.step();
				const float b = bend.from + bend.step * static_cast<float>(i);
				phase = wrap(phase + inc * b);
				phase2 = wrap(phase2 + inc2 * b);
				const float m = phase2 < 0.5f ? 1.0f : -1.0f;	// the second oscillator, a square
				float x = sineTurn(wrap(phase + depth * m + 1.0f));
				if(ring)
					x = 0.5f * x + 0.5f * x * m;
				_out[i] += 0.7f * saturate(drive * a * x) / saturate(drive);
			}
			level.store(_c, 2);
			f[slot::Phase] = phase;
			f[slot::Phase2] = phase2;
			float hpIn = f[slot::Click], hpOut = f[slot::Click2];
			if(_t.clicking || hpIn != 0 || hpOut != 0)
			{
				const auto hp = static_cast<float>(std::exp(-2 * Pi * 1500 / _rate));
				for(int i = 0; i < _n; ++i)
				{
					const float x = _t.clickAt(i, perHost);
					hpOut = hp * (hpOut + x - hpIn);
					hpIn = x;
					_out[i] += 4.0f * hpOut;
				}
			}
			f[slot::Click] = flush(hpIn);
			f[slot::Click2] = flush(hpOut);
			noiseBurst(_io, _c, _t, _out, nullptr, _n, _rate, 0.5f);
		}

		// SNAP KICK: a saw (or, with switch 8, a square) VCO, free running unless the trig switch resets it, saturated
		// hard and low-passed at 8 f0 opening with the level; its click: the control's, plus band-passed noise at 3.5 kHz
		void sharp(const VoiceIo& _io, Circuit& _c, const Tick& _t, float* _out, const int _n, const double _rate)
		{
			auto& f = _c.f;
			const double perHost = 48000.0 / _rate;
			Level level(_t, _c, 3);
			const Bend bend = sweepOf(_t, _c, Machine::KickSharp, 3.0f, true, 2.0f, _n);
			if(_t.attackStarts[3] && _t.sw(0))
				f[slot::Phase] = 0;
			float phase = f[slot::Phase], tone = f[slot::Tone], tone2 = f[slot::Fb];
			const auto inc = static_cast<float>(_t.bodyHz / _rate);
			const bool square = _t.sw(8);
			const float drive = 1.5f * _t.drive();
			for(int i = 0; i < _n; ++i)
			{
				const float a = level.step();
				const float dt = inc * (bend.from + bend.step * static_cast<float>(i));
				phase = wrap(phase + dt);
				// a saw or a square, its steps smoothed over a sample (polyBLEP) so the top does not fold
				const auto blep = [dt](float _t2)
				{
					if(_t2 < dt)
					{
						_t2 /= dt;
						return _t2 + _t2 - _t2 * _t2 - 1;
					}
					if(_t2 > 1 - dt)
					{
						_t2 = (_t2 - 1) / dt;
						return _t2 * _t2 + _t2 + _t2 + 1;
					}
					return 0.0f;
				};
				float x = 2 * phase - 1 - blep(phase);
				if(square)
					x = (phase < 0.5f ? 1.0f : -1.0f) + blep(phase) - blep(wrap(phase + 0.5f));
				const float lp = std::clamp(inc * 8 * (1 + 4 * a) * 6.28f, 0.01f, 0.9f);
				tone += lp * (x - tone);
				tone2 += lp * (tone - tone2);
				_out[i] += 0.7f * saturate(drive * a * tone2) / saturate(drive);
			}
			level.store(_c, 3);
			f[slot::Phase] = phase;
			// the VCO runs on, its filter with it: once the level is over, nothing of it is heard, so the filter clears
			const bool over = f[slot::Seg + 3] == 0;
			f[slot::Tone] = over ? 0.0f : flush(tone);
			f[slot::Fb] = over ? 0.0f : flush(tone2);
			// the click: the control's, as it is (switch 7 off damps it), and noise through a band-pass at 3.5 kHz, Q 1.5
			float lp = f[slot::Click], bp = f[slot::Click2];
			if(_t.clicking || lp != 0 || bp != 0)
			{
				const float gain = _t.sw(7) ? 1.5f : 0.5f;
				for(int i = 0; i < _n; ++i)
					_out[i] += gain * _t.clickAt(i, perHost);
			}
			float burst[64] = {};
			noiseBurst(_io, _c, _t, burst, nullptr, _n, _rate, 1.0f);
			const auto g = static_cast<float>(2 * std::sin(Pi * 3500 / _rate));
			for(int i = 0; i < _n; ++i)
			{
				lp += g * bp;
				bp += g * (burst[i] - lp - bp / 1.5f);
				_out[i] += 2.0f * bp;
			}
			f[slot::Click] = flush(lp);
			f[slot::Click2] = flush(bp);
		}

		// BODY KICK: membrane modes struck by the control's body sample and a beater's 2 ms of noise: two close
		// modes (1, 1.025: a slow beat) that last the level segment's time, four upper modes at the membrane's
		// air-loaded ratios (1.59, 2.14, 2.30, 2.65) that last a fifth of it, and a shell resonance at 180 Hz lasting
		// 40 ms; a tenth of the sweep (a skin's tension)
		void acoustic(const VoiceIo& _io, Circuit& _c, const Tick& _t, float* _out, const int _n, const double _rate)
		{
			auto& f = _c.f;
			const double perHost = 48000.0 / _rate;
			Level level(_t, _c, 2);
			for(int i = 0; i < _n; ++i)
				level.step();
			level.store(_c, 2);
			const Bend bend = sweepOf(_t, _c, Machine::KickAcoustic, 0.2f, true, 2.0f, _n);
			const float keep = _c.u[static_cast<size_t>(slot::Cmd + 2)] >> 28 == 0xb ? _t.keep[2] : keepFor(1.0, _rate);
			const double f0 = _t.bodyHz * bend.from;
			constexpr double ratios[] = {1.0, 1.025, 1.59, 2.14, 2.30, 2.65};
			constexpr float levels[] = {1.0f, 0.8f, 0.35f, 0.25f, 0.2f, 0.15f};
			Resonator modes[slot::Resonators];
			for(int k = 0; k < slot::Resonators; ++k)
				modes[k].tune(f0 * ratios[k], k < 2 ? keep : std::pow(keep, 5.0f), _rate);
			Resonator shell;
			shell.tune(180, keepFor(0.04, _rate), _rate);
			// the input to each mode, so that its share of the strike rings at its level (a resonator at pitch w answers
			// a broad strike about 1 / w louder: the gains carry sin w), the whole at about 0.6 of full scale
			float in[slot::Resonators];
			for(int k = 0; k < slot::Resonators; ++k)
				in[k] = 2.5f * levels[k] * static_cast<float>(std::sin(2 * Pi * std::min(f0 * ratios[k], 0.45 * _rate) / _rate));
			const auto shellIn = static_cast<float>(std::sin(2 * Pi * 180 / _rate));
			if(_t.attackStarts[2])
				f[slot::Excite] = 0;
			uint32_t seed = _c.u[slot::Seed];
			const auto beaterInc = static_cast<float>(1.0 / (0.002 * _rate));
			float& s1 = f[slot::NoiseA];
			float& s2 = f[slot::NoiseB];
			const float drive = _t.drive() * (_t.sw(4) ? 1.0f : 2.0f);
			// its noise is the beater's, not a burst
			f[slot::NoiseEnv] = 0;
			for(int i = 0; i < _n; ++i)
			{
				float x = 0.3f * _t.clickAt(i, perHost);
				if(_c.u[slot::Struck] != 0 && f[slot::Excite] < 1)
				{
					x += 0.08f * noiseSample(seed) * (1 - f[slot::Excite]);
					f[slot::Excite] = std::min(1.0f, f[slot::Excite] + beaterInc);
				}
				float sum = 0;
				for(int k = 0; k < slot::Resonators; ++k)
					sum += modes[k].step(res(_c, k), in[k] * x);
				float shellState[2] = {s1, s2};
				const float sh = shell.step(shellState, shellIn * x);
				s1 = shellState[0];
				s2 = shellState[1];
				sum += 0.5f * sh + 0.4f * _t.clickAt(i, perHost);
				_out[i] += 0.6f * saturate(drive * sum) / saturate(drive);
			}
			s1 = flush(s1);
			s2 = flush(s2);
			_c.u[slot::Seed] = seed;
			storeResonators(_c);
			(void)_io;
		}
	}

	// A Hit restarts a kick's circuit, but a pinged resonator (NOISE KICK, SOFT KICK) and a free-running VCO (SNAP KICK)
	// keep what they hold: a new Hit rings the resonator again over what still sounds, the VCO goes on from its phase
	void kickTrig(const Machine _m, Circuit& _c)
	{
		const Circuit was = _c;
		_c = Circuit{};
		_c.u[slot::Seed] = 0x2545f491u;
		if(_m == Machine::KickClassic || _m == Machine::KickSilky)
			for(int k = 0; k < 2; ++k)
				_c.f[static_cast<size_t>(slot::Res + k)] = was.f[static_cast<size_t>(slot::Res + k)];
		if(_m == Machine::KickSharp)
			_c.f[slot::Phase] = was.f[slot::Phase];
	}

	// SNAP KICK's VCO runs free between Hits: it moves on by the samples the voice slept
	void kickAdvance(const Machine _m, Circuit& _c, const int64_t _samples, const double _rate)
	{
		if(_m != Machine::KickSharp)
			return;
		// at the pitch it last had: the CV is not known here, so 50 Hz, a default kick's (its phase only needs to differ)
		const double turns = _c.f[slot::Phase] + static_cast<double>(_samples) * 50.0 / _rate;
		_c.f[slot::Phase] = static_cast<float>(turns - std::floor(turns));
	}

	void kickRender(const Machine _m, const VoiceIo& _io, Circuit& _c, float* _out, const int _n, const double _rate)
	{
		const Tick t = readTick(_m, _io, _c, _rate);
		switch(_m)
		{
		case Machine::KickHard:		hard(_io, _c, t, _out, _n, _rate); break;
		case Machine::KickClassic:	classic(_io, _c, t, _out, _n, _rate); break;
		case Machine::KickFm:		fm(_io, _c, t, _out, _n, _rate); break;
		case Machine::KickPlastic:	plastic(_io, _c, t, _out, _n, _rate); break;
		case Machine::KickSilky:	silky(_io, _c, t, _out, _n, _rate); break;
		case Machine::KickSharp:	sharp(_io, _c, t, _out, _n, _rate); break;
		default:					acoustic(_io, _c, t, _out, _n, _rate); break;
		}
	}

	void kickParams(const Machine _m, const uint8_t* _controls, std::array<uint8_t, 20>& _params)
	{
		// word w gets control value v as v << 8; words no control sets keep the value given here
		std::array<int, 10> words{0, 64, 64, 64, 64, 64, 64, 64, 0, 0};
		const auto set = [&](const int _word, const int _control) { words[static_cast<size_t>(_word)] = _controls[_control]; };
		// a control of 0-127 as a choice of _n
		const auto choice = [&](const int _control, const int _n) { return _controls[_control] * _n / 128; };
		switch(_m)
		{
		case Machine::KickHard:
		case Machine::KickClassic:
			set(1, 0);	// tune
			set(2, 1);	// decay
			set(5, 2);	// sweep depth
			set(4, 3);	// sweep time
			set(3, 4);	// hold
			if(_m == Machine::KickHard)
				set(7, 5);	// click
			else
				words[7] = choice(5, 56);	// noise style and level
			words[6] = choice(6, 3);	// mode: switches 8 and 7
			set(0, 7);	// drive (the accent byte)
			break;
		case Machine::KickFm:
			set(1, 0);	// tune
			set(2, 1);	// decay
			set(4, 2);	// sweep time
			set(3, 3);	// FM amount
			set(7, 4);	// FM frequency
			set(5, 5);	// FM decay
			set(0, 6);	// drive (c7 unused: word 6, the hold of a segment this circuit does not play, stays 64)
			break;
		case Machine::KickPlastic:
			set(1, 0);	// tune
			set(2, 1);	// decay
			set(6, 2);	// sweep depth
			set(5, 3);	// sweep time
			set(4, 4);	// tone
			set(7, 5);	// click
			words[3] = _controls[6] >= 64 ? 1 : 0;	// mode: switch 4
			set(0, 7);	// drive
			break;
		case Machine::KickSilky:
			set(1, 0);	// tune
			set(2, 1);	// decay
			set(5, 2);	// sweep depth
			set(4, 3);	// sweep time
			set(3, 4);	// hold
			set(6, 5);	// noise
			set(7, 6);	// tone
			set(0, 7);	// drive
			break;
		default:	// Sharp and Acoustic
			set(1, 0);	// tune
			set(2, 1);	// decay
			set(5, 2);	// sweep depth
			set(4, 3);	// sweep time
			set(3, 4);	// hold
			set(7, 5);	// click (Sharp), body level (Acoustic)
			words[6] = choice(6, 12);	// sound: trig switch, switches 8, 2, 7 (Acoustic 10-11: the sampled body)
			set(0, 7);	// drive
			break;
		}
		for(size_t w = 0; w < words.size(); ++w)
		{
			_params[2 * w] = static_cast<uint8_t>(words[w]);
			_params[2 * w + 1] = 0;
		}
	}

	std::array<uint8_t, 8> kickDefaults(const Machine _m)
	{
		switch(_m)
		{
		case Machine::KickHard:		return {64, 42, 110, 50, 16, 90, 0, 50};
		case Machine::KickClassic:	return {62, 70, 100, 60, 16, 66, 0, 30};
		case Machine::KickFm:		return {64, 32, 50, 110, 70, 64, 40, 0};
		case Machine::KickPlastic:	return {66, 76, 100, 40, 64, 80, 0, 40};
		case Machine::KickSilky:	return {64, 96, 90, 60, 20, 40, 40, 20};
		case Machine::KickSharp:	return {64, 72, 100, 36, 16, 90, 0, 40};
		default:					return {64, 74, 90, 40, 16, 90, 110, 30};
		}
	}

	bool kickEnded(const Machine _m, const VoiceIo& _io, const Circuit& _c)
	{
		// every envelope and resonance at zero after the attack: the body's level, the resonators, the noise burst,
		// the click path, what the control sends
		const Wiring& w = wiringOf(_m);
		if(_c.u[slot::Struck] == 0 || _c.f[static_cast<size_t>(slot::Seg + w.amp)] != 0 || _c.f[slot::NoiseEnv] != 0)
			return false;
		for(const int s : {slot::Click, slot::Click2, slot::Tone, slot::NoiseA, slot::NoiseB, slot::NoiseC, slot::NoiseD})
			if(_c.f[static_cast<size_t>(s)] != 0)
				return false;
		for(int k = 0; k < 2 * slot::Resonators; ++k)
			if(_c.f[static_cast<size_t>(slot::Res + k)] != 0)
				return false;
		for(size_t i = 0; i < 128; i += 4)
			if(be32(_io.outA.data() + i) != 0)
				return false;
		return true;
	}
}
