#include "edDrumsAnalogVoice.h"

#include <algorithm>
#include <cmath>

#include <emmintrin.h>

#include "edDrumsAnalogPort.h"

// Track 12's metal circuits (edDrumsAnalogVoice.h): square oscillators, ring modulators, band-pass and high-pass
// filters and four envelope stages, playing what the metal control (edDrumsAnalogMetal.cpp) writes each tick. Our own
// models after the classic six-square metal voice; nothing of the original's sound.
//
// Names (ticket 27) and Machine 1-8, as metalParams reads them:
//   SIX CLOSED  TUNE DECAY TONE HIGHPASS RESONANCE NOISE CLICK LEVEL
//   SIX OPEN    TUNE DECAY TONE HIGHPASS RESONANCE NOISE DRIVE LEVEL
//   SIX CYMBAL  TUNE DECAY TONE BAND MIX RESONANCE NOISE DRIVE LEVEL
//   DUO BELL    TUNE DECAY DETUNE BAND RESONANCE NOISE STRIKE LEVEL
//   RING HAT    TUNE DECAY BAND HIGHPASS RESONANCE NOISE DRIVE LEVEL
//   RING OPEN   TUNE DECAY BAND HIGHPASS RESONANCE NOISE DRIVE LEVEL
//   RING CRASH  TUNE DECAY COLOR FIRST DECAY RESONANCE NOISE CRASH LEVEL
//   RING BELL   TUNE DECAY INTERVAL BAND RESONANCE NOISE STRIKE LEVEL
//   BAND RIDE   TUNE DECAY PARTIALS TOP DECAY HIGH BAND MID BAND LOW BAND LEVEL
//   BEAT HAT    TUNE DECAY TONE FIRST DECAY SYNC NOISE BURST LEVEL
//   TUNED HAT   OSC 1 DECAY OSC 2 OSC 3 OSC 4 OSC 5 OSC 6 LEVEL
//
// The laws, from the control words (as the control writes them, see analog-notes/metal.md):
// - An oscillator's rate word holds, in its low 22 bits, its frequency as hz * 2^16 / 1500: the default tune at note 60
//   gives the six-square set 540, 800, 522, 304, 370, 205 Hz.
// - An envelope stage is two longs: a level word (bytes 0-1 a gain, 0x4000 unity; byte 2 a start level, byte 3 a
//   target, each 0x80 for none and 0x7f for full) and a command word. Its top nibble: bit 2 starts the stage at its
//   start level when the nibble changes, bit 1 moves it to its target, else to zero (0xf an attack, 0xb a move, 0x9 a
//   fall); bits 16-19 (e) and 0-15 (m) a rate, the stage moving (1 - m / 2^17) / 2^e of the way a tick of a 24 kHz
//   clock: the time base the control implies, whose countdown runs 16 ticks a block of 32 samples at 48 kHz.
// - Below -74 dB a falling stage closes in 5 ms, as a VCA's bias cuts a quiet signal: the voice freezes.
// - The level word (Cv + 8, unsigned) scales the output, unity at 0x8000, times a trim per machine chosen so that the
//   defaults peak near 0.5; the tone word (Cv + 0, about zero) moves the band-passes an octave each way.
// - Each Hz above is clamped below 0.45 of the host's rate; squares are band-limited at their edges (polyBLEP).
namespace elektron::drums::analog::circuit
{
	namespace
	{
		using port::metalFrame::Cv;
		using port::metalFrame::Envelope;
		using port::metalFrame::Oscillators;

		// Circuit::f slots
		constexpr int Phase = 0;		// eight oscillator phases, 0-1, as two lanes of four (aligned)
		constexpr int Stage = 12;		// four envelope stages' values, 0-1
		constexpr int Coef = 16;		// ... their approach a host sample
		constexpr int Target = 20;		// ... their targets
		constexpr int Filter = 24;		// three band filters, two states each
		constexpr int NoiseFilter = 30;	// the noise filter, two states
		constexpr int OutHp = 32;		// the output high-pass: its output and its last input
		constexpr int LastA = 34;
		constexpr int Tilt = 35;		// the 606 tone filter's low-pass state
		constexpr int LastNoise = 45;	// the fourth lane's last noise sample
		constexpr int Swell = 46;		// the ring cymbal's body: its VCA's slow attack
		constexpr int BandHp = 64;		// the 808 cymbal's high-passes after its VCAs, four lanes as Bands
		constexpr int Bands = 48;		// the 808 cymbal's band filters as four lanes: two states of four floats
		constexpr int Increment = 36;	// the eight oscillators' phase steps a host sample, as the last tick ran them
		constexpr int IncrementRate = 44;	// ... at this host rate		// outA's last value of the previous tick
		// Circuit::u slots
		constexpr int Command = 0;		// four stages' last command words
		constexpr int Levels = 4;		// ... and their start and target bytes
		constexpr int Seed = 8;			// the noise generator
		constexpr int Frozen = 9;		// 1 once every stage has ended and nothing is left to sound
		constexpr int Running = 10;		// 1 once the oscillators run: they keep their phases across Hits
		constexpr int Fresh = 11;		// 1 until a Hit's first tick has been rendered

		// The controls our circuits read that the control does not: parameter bytes 16-19, which no metal routine reads
		constexpr int ToneByte = 16, ResoByte = 17, NoiseByte = 18, SnapByte = 19;

		float level(const uint8_t _b) { return static_cast<float>(_b ^ 0x80) / 255.0f; }

		// The four stages, from the commands the control just wrote
		void readStages(const VoiceIo& _io, Circuit& _c, const double _rate)
		{
			for(int s = 0; s < 4; ++s)
			{
				const uint8_t* p = _io.frame.data() + Envelope + 8 * s;
				const uint32_t cw = be32(p + 4);
				const uint32_t lv = static_cast<uint32_t>(p[2]) << 8 | p[3];
				if(cw == _c.u[Command + s] && lv == _c.u[Levels + s])
					continue;
				// the command's top nibble: bit 2 starts the stage at its start level when the nibble changes, bit 1
				// moves it to its target (else to zero); its low nibble and the rest only retime or rerate the stage
				const uint32_t op = cw >> 28;
				if((op & 4) && op != _c.u[Command + s] >> 28)
					_c.f[Stage + s] = level(p[2]);
				_c.f[Target + s] = (op & 2) ? level(p[3]) : 0.0f;
				_c.u[Command + s] = cw;
				_c.u[Levels + s] = lv;
				const uint32_t mantissa = (cw & 0xffff) << 14;
				const auto shifted = static_cast<double>((0x7fffffffu - mantissa) >> (cw >> 16 & 0xf));
				const double step = std::min(1.0, shifted / 2147483648.0);
				_c.f[Coef + s] = static_cast<float>(step >= 1.0 ? 1.0 : 1.0 - std::pow(1.0 - step, 24000.0 / _rate));
			}
		}
		float stageGain(const VoiceIo& _io, const int _s)
		{
			return static_cast<float>(static_cast<int16_t>(be16(_io.frame.data() + Envelope + 8 * _s))) / 16384.0f;
		}
		// The stages' per-sample factors for a tick: e = t + (e - t) m, a falling stage under -74 dB closing faster
		void stageFactors(const Circuit& _c, const float _cut, float* _m)
		{
			for(int s = 0; s < 4; ++s)
			{
				_m[s] = 1.0f - _c.f[Coef + s];
				if(_c.f[Target + s] == 0.0f && _c.f[Stage + s] < 2e-4f)
					_m[s] *= _cut;
			}
		}
		// ... and at the tick's end what has arrived snaps to its target
		void settleStages(Circuit& _c)
		{
			for(int s = 0; s < 4; ++s)
			{
				float& e = _c.f[Stage + s];
				const float t = _c.f[Target + s];
				if(std::fabs(t - e) < 1e-7f || (t == 0.0f && e < 1e-7f))
					e = t;
			}
		}

		double hzOf(const VoiceIo& _io, const int _word)
		{
			return (be32(_io.frame.data() + Oscillators + 4 * _word) & 0x3fffff) * (1500.0 / 65536.0);
		}

		// Eight square oscillators as two lanes of four, band-limited at their edges (polyBLEP); the phases live in the
		// circuit, the increments and their inverses for the tick here
		struct Squares
		{
			__m128 dt[2], inv[2];

			static __m128 blep(const __m128 _ph, const __m128 _inv)
			{
				const __m128 one = _mm_set1_ps(1.0f);
				const __m128 t = _mm_mul_ps(_ph, _inv);
				const __m128 rise = _mm_and_ps(_mm_cmplt_ps(t, one),
					_mm_sub_ps(_mm_sub_ps(_mm_add_ps(t, t), _mm_mul_ps(t, t)), one));
				const __m128 u = _mm_mul_ps(_mm_sub_ps(_ph, one), _inv);
				const __m128 fall = _mm_and_ps(_mm_cmpgt_ps(u, _mm_set1_ps(-1.0f)),
					_mm_add_ps(_mm_add_ps(_mm_mul_ps(u, u), _mm_add_ps(u, u)), one));
				return _mm_add_ps(rise, fall);
			}
			// one sample of lane _l, its phases at _ph
			__m128 run(float* _ph, const int _l) const
			{
				const __m128 one = _mm_set1_ps(1.0f), half = _mm_set1_ps(0.5f);
				__m128 ph = _mm_add_ps(_mm_load_ps(_ph), dt[_l]);
				ph = _mm_sub_ps(ph, _mm_and_ps(_mm_cmpge_ps(ph, one), one));
				_mm_store_ps(_ph, ph);
				const __m128 low = _mm_cmplt_ps(ph, half);
				const __m128 naive = _mm_or_ps(_mm_and_ps(low, one), _mm_andnot_ps(low, _mm_set1_ps(-1.0f)));
				__m128 h = _mm_add_ps(ph, half);
				h = _mm_sub_ps(h, _mm_and_ps(_mm_cmpge_ps(h, one), one));
				return _mm_sub_ps(_mm_add_ps(naive, blep(ph, inv[_l])), blep(h, inv[_l]));
			}
		};
		float sum(const __m128 _v)
		{
			const __m128 pair = _mm_add_ps(_v, _mm_movehl_ps(_v, _v));
			return _mm_cvtss_f32(_mm_add_ss(pair, _mm_shuffle_ps(pair, pair, _MM_SHUFFLE(1, 1, 1, 1))));
		}

		// A state-variable filter (trapezoidal), its coefficients for a tick
		struct Svf
		{
			float a1 = 0, a2 = 0, a3 = 0, k = 0;
			void set(double _hz, const double _q, const double _rate)
			{
				_hz = std::clamp(_hz, 20.0, 0.45 * _rate);
				const double g = std::tan(3.14159265358979 * _hz / _rate);
				k = static_cast<float>(1.0 / _q);
				const double d = 1.0 / (1.0 + g * (g + k));
				a1 = static_cast<float>(d);
				a2 = static_cast<float>(g * d);
				a3 = static_cast<float>(g * g * d);
			}
			// band-pass (_hp false) or high-pass of _x, state at _s
			float run(float* _s, const float _x, const bool _hp) const
			{
				const float v3 = _x - _s[1];
				const float v1 = a1 * _s[0] + a2 * v3;
				const float v2 = _s[1] + a2 * _s[0] + a3 * v3;
				_s[0] = 2 * v1 - _s[0];
				_s[1] = 2 * v2 - _s[1];
				return _hp ? _x - k * v1 - v2 : v1;
			}
		};

		// Four state-variable filters side by side, each lane a band-pass or a high-pass
		struct Svf4
		{
			__m128 a1, a2, a3, k, highPass;
			void set(const Svf* _f, const bool* _hp)
			{
				alignas(16) float x[4][4] = {};
				for(int l = 0; l < 4; ++l)
				{
					x[0][l] = _f[l].a1;
					x[1][l] = _f[l].a2;
					x[2][l] = _f[l].a3;
					x[3][l] = _f[l].k;
				}
				a1 = _mm_load_ps(x[0]);
				a2 = _mm_load_ps(x[1]);
				a3 = _mm_load_ps(x[2]);
				k = _mm_load_ps(x[3]);
				highPass = _mm_castsi128_ps(_mm_set_epi32(0, _hp[2] ? -1 : 0, _hp[1] ? -1 : 0, _hp[0] ? -1 : 0));
			}
			__m128 run(float* _s, const __m128 _x) const
			{
				const __m128 s0 = _mm_load_ps(_s), s1 = _mm_load_ps(_s + 4);
				const __m128 v3 = _mm_sub_ps(_x, s1);
				const __m128 v1 = _mm_add_ps(_mm_mul_ps(a1, s0), _mm_mul_ps(a2, v3));
				const __m128 v2 = _mm_add_ps(_mm_add_ps(s1, _mm_mul_ps(a2, s0)), _mm_mul_ps(a3, v3));
				_mm_store_ps(_s, _mm_sub_ps(_mm_add_ps(v1, v1), s0));
				_mm_store_ps(_s + 4, _mm_sub_ps(_mm_add_ps(v2, v2), s1));
				const __m128 hp = _mm_sub_ps(_mm_sub_ps(_x, _mm_mul_ps(k, v1)), v2);
				return _mm_or_ps(_mm_and_ps(highPass, hp), _mm_andnot_ps(highPass, v1));
			}
			// every lane a high-pass
			__m128 runHighPass(float* _s, const __m128 _x) const
			{
				const __m128 s0 = _mm_load_ps(_s), s1 = _mm_load_ps(_s + 4);
				const __m128 v3 = _mm_sub_ps(_x, s1);
				const __m128 v1 = _mm_add_ps(_mm_mul_ps(a1, s0), _mm_mul_ps(a2, v3));
				const __m128 v2 = _mm_add_ps(_mm_add_ps(s1, _mm_mul_ps(a2, s0)), _mm_mul_ps(a3, v3));
				_mm_store_ps(_s, _mm_sub_ps(_mm_add_ps(v1, v1), s0));
				_mm_store_ps(_s + 4, _mm_sub_ps(_mm_add_ps(v2, v2), s1));
				return _mm_sub_ps(_mm_sub_ps(_x, _mm_mul_ps(k, v1)), v2);
			}
		};

		// The circuits, by the classic each evokes (research 28, section 5): three combiners of the oscillators (the
		// 808's six squares through band-passes at 3.4 and 7.1 kHz, the 606's six squares simply summed, the ring
		// modulator's pairs) and the 808's two-square bell
		enum class Kind : uint8_t
		{
			Hat808,		// the 808's hats: the bank, its 7.1 kHz band, a swing VCA, a Sallen-Key high-pass
			Cymbal808,	// the 808's cymbal: three bands (3.4 kHz, 7.1 kHz, the top above 10.5 kHz), each its envelope
						// and swing VCA, the tone cutting the top band
			Ride808,	// the 808's three bands, narrow: they ring through plain VCAs, each at its level; its
						// oscillators XORed in pairs (the ride's logical interaction), the stick's click
			Bell808,	// the 808's bell: two squares, their VCA's strike and tail, a 2.64 kHz band-pass after
			BellRing,	// two squares ring-modulated (their XOR), a narrower and higher band
			Hat606,		// the 606's hats: the bank summed, a tilt tone filter, a plain VCA, a high-pass; noise
			RingHat,	// the ring hats: the ring-modulated pairs' three narrow high clusters, one of them picked by a
						// band-pass, a high-pass, a plain VCA. The closed hat's band the top cluster (13 kHz, the
						// 1458 x 13500 pair), short and bright; the open hat rings the two lower pairs alone through a
						// narrow band on the lowest (8.5 kHz, the 272 x 8529 pair): a darker ring under a longer tail
			RingCrash,	// a cymbal, a path of its own: the three low tones also ring each other (a dense spread of
						// partials from 300 Hz up) through a broad 3.4 kHz band whose VCA swells in over about 12 ms,
						// the high pairs high-passed above 8 kHz as its shimmer, and the noise transient
			HatLab		// six free squares, no band-pass (research 28, 7.4: HH LAB has none), a high-pass, a plain
						// VCA
		};
		struct Voicing
		{
			Kind kind;
			float trim;			// the output's level at unity
			float noise;		// the noise path's level at the noise byte's top
			float burst;		// outA's level at the trig, which the burst's noise scales to one: its peak's inverse
			bool drive;			// the snap byte drives the output (a soft clip)
		};
		const Voicing& voicingOf(const Machine _m)
		{
			static const Voicing v[] = {
				{Kind::Hat808, 1.3f, 0.4f, 0, false},		// SIX CLOSED: stage D, stage C its click
				{Kind::Hat808, 2.0f, 0.4f, 0, true},		// SIX OPEN: stage D
				{Kind::Cymbal808, 4.5f, 0.4f, 0, true},		// SIX CYMBAL
				{Kind::Bell808, 2.2f, 0.35f, 0, false},		// DUO BELL
				{Kind::RingHat, 1.0f, 0.4f, 0, true},		// RING HAT
				{Kind::RingHat, 1.1f, 0.4f, 0, true},		// RING OPEN
				{Kind::RingCrash, 0.32f, 0.4f, 300, false},	// RING CRASH
				{Kind::BellRing, 1.0f, 0.6f, 0, false},		// RING BELL
				{Kind::Ride808, 4.0f, 0.0f, 70, false},		// BAND RIDE
				{Kind::Hat606, 2.8f, 0.6f, 200, false},		// BEAT HAT
				{Kind::HatLab, 0.95f, 0.0f, 0, false},		// TUNED HAT: stage C
			};
			return v[static_cast<int>(_m) - FirstMetal];
		}

		// The 808's swing VCA: a common-emitter stage whose supply is the envelope, so the positive half is amplified
		// and clips, the negative is squashed; what it adds of the envelope itself the high-pass after takes out
		float swing(const float _x, const float _env)
		{
			float s = _x * (_x > 0.0f ? 4.0f : 0.25f);
			s = s / (1.0f + std::fabs(s));
			return (s + 0.2f) * _env;
		}
		// ... the three bands' at once
		__m128 swing(const __m128 _x, const __m128 _env)
		{
			const __m128 positive = _mm_cmpgt_ps(_x, _mm_setzero_ps());
			const __m128 slope = _mm_or_ps(_mm_and_ps(positive, _mm_set1_ps(4.0f)),
				_mm_andnot_ps(positive, _mm_set1_ps(0.25f)));
			const __m128 s = _mm_mul_ps(_x, slope);
			const __m128 magnitude = _mm_andnot_ps(_mm_set1_ps(-0.0f), s);
			const __m128 d = _mm_add_ps(_mm_set1_ps(1.0f), magnitude);
			// a 12-bit reciprocal: enough for a clip's curve
			const __m128 clipped = _mm_mul_ps(s, _mm_rcp_ps(d));
			return _mm_mul_ps(_mm_add_ps(clipped, _mm_set1_ps(0.2f)), _env);
		}

		float noiseSample(Circuit& _c)
		{
			_c.u[Seed] = _c.u[Seed] * 1664525u + 1013904223u;
			return static_cast<float>(static_cast<int32_t>(_c.u[Seed])) * (1.0f / 2147483648.0f);
		}
	}

	void metalTrig(Machine, Circuit& _c)
	{
		// A Hit restarts the stages, filters and noise; the oscillators run free, as analog ones do, and keep their
		// phases unless the control restarts them (the restart bit of its rate words, see metalRender)
		float phases[8], increments[9];
		std::copy_n(_c.f.begin() + Phase, 8, phases);
		std::copy_n(_c.f.begin() + Increment, 9, increments);
		const bool running = _c.u[Running] != 0;
		_c = Circuit{};
		std::copy_n(increments, 9, _c.f.begin() + Increment);
		if(running)
			std::copy_n(phases, 8, _c.f.begin() + Phase);
		else
		{
			// as from power-on: each oscillator somewhere in its cycle, spread by the golden ratio
			for(int i = 0; i < 8; ++i)
				_c.f[static_cast<size_t>(Phase + i)] = static_cast<float>(std::fmod(0.618033988749895 * (i + 1), 1.0));
		}
		_c.u[Running] = 1;
		_c.u[Fresh] = 1;
		_c.u[Seed] = 0x2545f491u;
	}

	void metalRender(const Machine _m, const VoiceIo& _io, Circuit& _c, float* _out, const int _n, const double _rate)
	{
		const Voicing& v = voicingOf(_m);
		const Kind kind = v.kind;
		readStages(_io, _c, _rate);
		const auto& p = _io.params;

		// the tone: the control's tone word (about zero) where it computes one, else our own byte; an octave each way
		const bool toneFromCv = _m == Machine::ClosedClassic || _m == Machine::OpenClassic ||
			_m == Machine::CymbalClassic || _m == Machine::HatBasic;
		const double tone = toneFromCv ? static_cast<int16_t>(be16(_io.frame.data() + Cv)) / 32768.0
			: (p[ToneByte] - 64) / 64.0;
		const double reso = std::exp2((p[ResoByte] - 64) / 48.0);
		const double hpShift = std::exp2((p[8] - 64) / 32.0);		// HIGHPASS, where the control leaves byte 8 free
		const bool hpFree = _m == Machine::ClosedClassic || _m == Machine::OpenClassic ||
			_m == Machine::ClosedMetallic || _m == Machine::OpenMetallic;
		Svf noiseBand;
		// the noise's settings, where the control gives them: a band (byte 2 in hundreds of Hz), its Q (byte 0 / 10)
		// and its level (byte 3 of 64)
		const bool settings = _io.noise[3] != 0;
		noiseBand.set(settings ? _io.noise[2] * 100.0 : 7000.0, settings ? _io.noise[0] / 10.0 : 1.0, _rate);
		Svf f[4], hp;
		Svf4 bands, bandHp;
		float topGain = 1.0f, onePole = 0.0f;
		switch(kind)
		{
		case Kind::Hat808:
			f[0].set(7100 * std::exp2(tone), 2.0 * reso, _rate);
			hp.set(6000.0 * (hpFree ? hpShift : 1.0), 0.8, _rate);
			break;
		case Kind::HatLab:
			// its six oscillators heard as tuned, above the hats' usual low end
			hp.set(3000.0, 0.7, _rate);
			break;
		case Kind::Cymbal808:
		case Kind::Ride808:
		{
			// the 808's three bands: the cymbal's broad (a wash), the ride's narrow (they ring). The cymbal's tone sets
			// its top band
			const double q = (kind == Kind::Ride808 ? 5.0 : 1.6) * reso;
			f[0].set(3440, q, _rate);
			f[1].set(7100, q, _rate);
			f[2].set(10500, 1.5, _rate);
			if(kind == Kind::Cymbal808)
				topGain = static_cast<float>(std::exp2(2.0 * tone));
			static constexpr bool modes[4] = {false, false, true, false};
			f[3] = noiseBand;
			bands.set(f, modes);
			if(kind == Kind::Cymbal808)
			{
				// the 808 cymbal's Sallen-Key high-passes after each band's VCA (Werner 2014): they take out what the
				// swing VCA adds of its envelope and the band-passes' low skirts, the low band's below 2 kHz, the
				// mid's below 4.3 kHz, the top's again at 10.5 kHz
				Svf h[4];
				h[0].set(2000, 0.7, _rate);
				h[1].set(4300, 0.7, _rate);
				h[2].set(10500, 0.9, _rate);
				static constexpr bool highPasses[4] = {true, true, true, false};
				bandHp.set(h, highPasses);
			}
			onePole = static_cast<float>(
				1.0 / (1.0 + 2 * 3.14159265358979 * (kind == Kind::Ride808 ? 1500.0 : 2500.0) / _rate));
			break;
		}
		case Kind::Bell808:
			f[0].set(2640 * std::exp2(tone), 0.9 * reso, _rate);
			hp.set(300, 0.7, _rate);
			break;
		case Kind::BellRing:
			f[0].set(3600 * std::exp2(tone), 2.2 * reso, _rate);
			hp.set(900, 0.7, _rate);
			break;
		case Kind::Hat606:
			hp.set(6500.0 * std::exp2(0.5 * tone), 0.9 * reso, _rate);
			break;
		case Kind::RingHat:
		{
			const bool closed = _m == Machine::ClosedMetallic;
			f[0].set((closed ? 13000 : 8500) * std::exp2(tone), (closed ? 1.5 : 4.0) * reso, _rate);
			hp.set((closed ? 9000.0 : 6500.0) * hpShift, 0.7, _rate);
			break;
		}
		case Kind::RingCrash:
			f[0].set(3400, 0.8 * reso, _rate);
			f[1].set(8000, 0.7, _rate);
			hp.set(1500, 0.7, _rate);
			break;
		}
		// the 606's tilt: its tone lifts the top against the low-passed rest (+ shrill, - dull)
		const auto tiltA = static_cast<float>(1.0 - std::exp(-2 * 3.14159265358979 * 3000.0 / _rate));
		const auto tilt = static_cast<float>(tone);
		const float tiltNorm = 1.0f / (1.0f + std::fabs(tilt));		// the tilt moves the balance, not the level

		const float noiseLevel = v.noise * (p[NoiseByte] / 127.0f) * (settings ? _io.noise[3] / 64.0f : 1.0f);
		const float snap = p[SnapByte] / 127.0f;
		const float drive = v.drive ? 3.0f * snap : 0.0f;
		const float out = v.trim * static_cast<float>(be16(_io.frame.data() + Cv + 8)) / 32768.0f;

		// the oscillators: the six of the bank, or the three pairs of the second bank, or the bell's two
		int count = 6, at = 0;
		if(kind == Kind::RingHat || kind == Kind::RingCrash || kind == Kind::Bell808 || kind == Kind::BellRing)
			at = 6;
		if(kind == Kind::Bell808 || kind == Kind::BellRing)
			count = 2;
		// the ride combines its oscillators by logic, as its type says ("inter-oscillator logical interaction"): each
		// pair XORed (ring-modulated), whichever set its type picks
		const bool ring = kind == Kind::RingHat || kind == Kind::RingCrash || kind == Kind::Ride808;
		// the cymbal also rings the three low tones with each other (the lane-0 oscillators by pairs)
		const bool cross = kind == Kind::RingCrash;
		// lane 0 the even oscillators, lane 1 the odd: a ring pair is a lane-0 and a lane-1 oscillator
		alignas(16) float dt[8] = {}, inv[8] = {}, weight[8] = {};
		for(int i = 0; i < count; ++i)
		{
			const int lane = (i & 1) * 4 + i / 2;
			dt[lane] = static_cast<float>(std::min(hzOf(_io, at + i) / _rate, 0.45));
			inv[lane] = dt[lane] > 0.0f ? 1.0f / dt[lane] : 0.0f;
			weight[lane] = 1.0f / static_cast<float>(count);
		}
		// the control's restart (bit 31 of the rate words, at a Hit's first tick): the oscillators start in phase
		if(_c.u[Fresh] != 0)
		{
			if(be32(_io.frame.data() + Oscillators + 4 * at) >> 31)
				std::fill_n(_c.f.begin() + Phase, 8, 0.0f);
			_c.u[Fresh] = 0;
		}
		std::copy_n(dt, 8, _c.f.begin() + Increment);
		_c.f[IncrementRate] = static_cast<float>(_rate);
		Squares sq;
		for(int l = 0; l < 2; ++l)
		{
			sq.dt[l] = _mm_load_ps(dt + 4 * l);
			sq.inv[l] = _mm_load_ps(inv + 4 * l);
		}
		const __m128 w0 = _mm_load_ps(weight), w1 = _mm_load_ps(weight + 4);
		// a lane without an oscillator holds still and counts for nothing
		// (the open ring hat rings its two lower pairs alone: the top pair is the closed one's)
		const __m128 pairs = _m == Machine::OpenMetallic ? _mm_set_ps(0.0f, 0.0f, 0.5f, 0.5f)
			: _mm_set_ps(0.0f, 1.0f / 3, 1.0f / 3, 1.0f / 3);
		float gain[4];
		for(int s = 0; s < 4; ++s)
			gain[s] = stageGain(_io, s);

		// outA, a level a 48 kHz sample: the ride's stick, the beating hat's burst, the metallic cymbal's attack
		float a[33];
		a[0] = _c.f[LastA];
		bool burst = a[0] != 0.0f;
		for(int i = 0; i < 32; ++i)
		{
			// the control's falling lines end on a residue of a few LSB that never reaches zero (an arithmetic shift
			// rounds down): below 2^-20 of full scale, -120 dB, it is no burst
			const auto x = static_cast<int32_t>(be32(_io.outA.data() + 4 * i));
			a[i + 1] = std::abs(x) < (1 << 11) ? 0.0f : std::fabs(static_cast<float>(x) / 2147483648.0f);
			burst = burst || a[i + 1] != 0.0f;
		}
		_c.f[LastA] = a[32];
		// the voice is frozen once every stage has ended, or once stage D has where every band passes it
		const bool timesD = kind == Kind::Cymbal808 || kind == Kind::RingHat || kind == Kind::RingCrash;
		const auto ended = [&](const int _s) { return _c.f[Stage + _s] == 0.0f && _c.f[Target + _s] == 0.0f; };
		const bool quiet = !burst && ((ended(0) && ended(1) && ended(2) && (timesD || ended(3))) ||
			(timesD && ended(3)));
		_c.u[Frozen] = quiet ? 1u : 0u;
		if(quiet)
		{
			// a frozen voice: nothing sounds and nothing is left to ring; its oscillators run on
			std::fill(_c.f.begin() + Filter, _c.f.begin() + LastA, 0.0f);
			std::fill_n(_c.f.begin() + Bands, 8, 0.0f);
			std::fill_n(_c.f.begin() + BandHp, 8, 0.0f);
			_c.f[LastNoise] = 0.0f;
			_c.f[Tilt] = 0.0f;
			_c.f[Swell] = 0.0f;
			for(int i = 0; i < 8; ++i)
			{
				const double ph = _c.f[static_cast<size_t>(Phase + i)] + static_cast<double>(dt[i]) * _n;
				_c.f[static_cast<size_t>(Phase + i)] = static_cast<float>(ph - std::floor(ph));
			}
			return;
		}
		const bool noisy = burst || noiseLevel > 0.0f;
		// the cymbal and ride run their noise band in their filters' fourth lane, its sample a sample late
		const bool laned = kind == Kind::Cymbal808 || kind == Kind::Ride808;
		if(laned && !noisy)
		{
			// no noise this tick: its lane holds nothing (left to decay it would sink into denormals)
			_c.f[Bands + 3] = _c.f[Bands + 7] = _c.f[LastNoise] = 0.0f;
		}
		// the burst at its snap byte (the ride's at a fixed half)
		const float burstLevel = v.burst * (v.noise > 0 ? snap : 0.5f);
		const float cut = static_cast<float>(std::exp(-1.0 / (0.005 * _rate)));
		const auto swellA = static_cast<float>(1.0 - std::exp(-1.0 / (0.012 * _rate)));
		const float step = 32.0f / static_cast<float>(_n);
		const __m128 bandScale = _mm_set_ps(0.0f, topGain, 1.0f, 1.0f);
		alignas(16) float m[4];
		stageFactors(_c, cut, m);
		// the four stages as one vector: e = t + (e - t) m a sample, each scaled by its gain word
		const __m128 tv = _mm_load_ps(&_c.f[Target]), mv = _mm_load_ps(m);
		alignas(16) float gains[4] = {gain[0], gain[1], gain[2], gain[3]};
		const __m128 gv = _mm_load_ps(gains);
		__m128 ev = _mm_load_ps(&_c.f[Stage]);
		for(int i = 0; i < _n; ++i)
		{
			const __m128 even = sq.run(&_c.f[Phase], 0), odd = sq.run(&_c.f[Phase + 4], 1);
			float src, dense = 0;
			if(ring)
				src = sum(_mm_mul_ps(_mm_mul_ps(even, odd), pairs));
			else if(kind == Kind::BellRing)
				src = _mm_cvtss_f32(_mm_mul_ss(even, odd));
			else
				src = sum(_mm_add_ps(_mm_mul_ps(even, w0), _mm_mul_ps(odd, w1)));
			if(cross)
				dense = sum(_mm_mul_ps(_mm_mul_ps(even, _mm_shuffle_ps(even, even, _MM_SHUFFLE(3, 0, 2, 1))), pairs));
			float noise = 0, level = 0;
			if(noisy)
			{
				noise = laned ? _c.f[LastNoise] : noiseBand.run(&_c.f[NoiseFilter], noiseSample(_c), false);
				// outA across the tick at its own rate: 32 values over _n host samples, between the last tick's
				const float pos = static_cast<float>(i + 1) * step;
				const int k0 = std::min(static_cast<int>(pos), 31);
				level = a[k0] + (a[k0 + 1] - a[k0]) * (pos - static_cast<float>(k0));
			}
			ev = _mm_add_ps(tv, _mm_mul_ps(_mm_sub_ps(ev, tv), mv));
			const __m128 egv = _mm_mul_ps(ev, gv);
			alignas(16) float eg[4];
			_mm_store_ps(eg, egv);
			const float eb = eg[1], ec = eg[2], ed = eg[3];
			const float x = src + noise * noiseLevel;
			float y = 0;
			switch(kind)
			{
			case Kind::Hat808:
			{
				const float env = ed + (_m == Machine::ClosedClassic ? snap * ec : 0.0f);
				y = hp.run(&_c.f[OutHp], swing(f[0].run(&_c.f[Filter], x, false), env), true);
				break;
			}
			case Kind::Cymbal808:
			case Kind::Ride808:
			{
				// low and mid band-passes and the top's high-pass side by side, each through its swing VCA: stages A,
				// B, C (the cymbal's also through D), the fourth lane the noise's band
				__m128 env = _mm_mul_ps(egv, bandScale);
				if(kind == Kind::Cymbal808)
					env = _mm_mul_ps(env, _mm_shuffle_ps(egv, egv, _MM_SHUFFLE(3, 3, 3, 3)));
				const __m128 in = _mm_set_ps(noisy ? noiseSample(_c) : 0.0f, x, x, x);
				const __m128 filtered = bands.run(&_c.f[Bands], in);
				_c.f[LastNoise] = _mm_cvtss_f32(_mm_shuffle_ps(filtered, filtered, _MM_SHUFFLE(3, 3, 3, 3)));
				// the cymbal through the 808's swing VCAs; the ride's bands ring through plain ones
				y = kind == Kind::Cymbal808 ? sum(bandHp.runHighPass(&_c.f[BandHp], swing(filtered, env)))
					: sum(_mm_mul_ps(filtered, env));
				// the 808 cymbal's output stage drifts at +6 dB an octave: a first-order high-pass
				float* h = &_c.f[OutHp];
				h[0] = onePole * (h[0] + y - h[1]);
				h[1] = y;
				y = h[0];
				break;
			}
			case Kind::Bell808:
			case Kind::BellRing:
			{
				// the VCAs before the filter, as the 808's bell: the strike (stage C) over the tail (stage D)
				const float env = snap * ec + 0.45f * ed;
				y = hp.run(&_c.f[OutHp], f[0].run(&_c.f[Filter], x * env, false), true);
				break;
			}
			case Kind::Hat606:
			{
				float& lp = _c.f[Tilt];
				lp += tiltA * (x - lp);
				const float tilted = (lp * (1.0f - tilt) + (x - lp) * (1.0f + tilt)) * tiltNorm;
				y = hp.run(&_c.f[OutHp], tilted, true) * ec;
				break;
			}
			case Kind::RingHat:
				y = hp.run(&_c.f[OutHp], f[0].run(&_c.f[Filter], x, false), true) * ec * ed;
				break;
			case Kind::HatLab:
				y = hp.run(&_c.f[OutHp], x, true) * ec;
				break;
			case Kind::RingCrash:
			{
				float& swell = _c.f[Swell];
				swell += swellA * (eb - swell);
				const float body = f[0].run(&_c.f[Filter], dense + noise * noiseLevel, false) * swell;
				const float shimmer = f[1].run(&_c.f[Filter + 2], x, true) * ec;
				y = hp.run(&_c.f[OutHp], body + shimmer, true) * ed;
				break;
			}
			}
			// the bursts: noise opened by outA
			y += noise * level * burstLevel;
			y = y * (1.0f + drive) / (1.0f + drive * std::fabs(y));
			_out[i] += y * out;
		}
		_mm_store_ps(&_c.f[Stage], ev);
		settleStages(_c);
	}

	// What the oscillators ran while the engine left the voice frozen: _samples host samples at _rate in one step, at
	// the frequencies of the voice's last tick
	void metalAdvance(Machine, Circuit& _c, const int64_t _samples, const double _rate)
	{
		if(_c.u[Running] == 0 || _c.f[IncrementRate] <= 0.0f)
			return;
		const double scale = static_cast<double>(_c.f[IncrementRate]) / _rate;
		for(int i = 0; i < 8; ++i)
		{
			const double ph = _c.f[static_cast<size_t>(Phase + i)] +
				static_cast<double>(_c.f[static_cast<size_t>(Increment + i)]) * scale * static_cast<double>(_samples);
			_c.f[static_cast<size_t>(Phase + i)] = static_cast<float>(ph - std::floor(ph));
		}
	}

	// Whether the circuit has ended: its stages have all fallen and nothing rings. Five of the eleven machines' control
	// never ends its state machine (it rests at step 2 or 3, its last stages left to fall), so the control's state
	// alone cannot say when the voice is done
	bool metalEnded(Machine, const VoiceIo&, const Circuit& _c)
	{
		return _c.u[Frozen] != 0;
	}

	void metalParams(const Machine _m, const uint8_t* _c, std::array<uint8_t, 20>& _p)
	{
		_p.fill(0);
		const auto word = [&](const int _w, const uint8_t _v) { _p[static_cast<size_t>(2 * _w)] = _v; };
		word(0, _c[7]);						// LEVEL
		word(1, _c[0]);						// TUNE (the tuned hat: its first oscillator)
		word(2, _c[1]);						// DECAY (the beating hat: its RELEASE, below)
		_p[ToneByte] = _p[ResoByte] = 64;
		switch(_m)
		{
		case Machine::Ride:
			_p[6] = static_cast<uint8_t>(_c[2] >> 5);	// PARTIALS: the oscillators' set, 0-3
			word(4, _c[3]);					// TOP DECAY: the high band's
			word(5, _c[4]);					// the bands' levels: HIGH, MID, LOW
			word(6, _c[5]);
			word(7, _c[6]);
			break;
		case Machine::HatLab:
			for(int i = 0; i < 5; ++i)		// OSC 2-6
				word(3 + i, _c[2 + i]);
			break;
		case Machine::HatBasic:
			word(3, _c[2]);					// TONE
			word(4, _c[3]);					// FIRST DECAY: the first fall, to about a third, before DECAY's tail
			word(5, _c[4] >= 64 ? 1 : 0);	// SYNC: the oscillators restart in phase at a Hit, else run free
			_p[NoiseByte] = _c[5];
			_p[SnapByte] = _c[6];			// BURST
			break;
		default:
			// word 3: TONE (six-square hats and cymbal, through the control's tone word), DETUNE and INTERVAL (the
			// bells' second oscillator), COLOR (the ring cymbal); the ring hats' BAND is our own byte
			word(3, _c[2]);
			// word 4: the six-square cymbal's BAND MIX, the ring cymbal's FIRST DECAY; the hats' HIGHPASS (our own use
			// of it)
			word(4, _c[3]);
			if(_m == Machine::ClosedMetallic || _m == Machine::OpenMetallic)
				_p[ToneByte] = _c[2];
			if(_m == Machine::BellClassic || _m == Machine::BellMetallic)
				_p[ToneByte] = _c[3];		// BAND
			_p[ResoByte] = _c[4];			// RESONANCE
			_p[NoiseByte] = _c[5];			// NOISE
			_p[SnapByte] = _c[6];			// CLICK, STRIKE, CRASH, or DRIVE
			break;
		}
	}

	std::array<uint8_t, 8> metalDefaults(const Machine _m)
	{
		switch(_m)
		{
		case Machine::ClosedClassic:	return {64, 28, 64, 64, 64, 30, 60, 100};
		case Machine::OpenClassic:		return {64, 30, 64, 64, 64, 30, 40, 100};
		case Machine::CymbalClassic:	return {64, 60, 64, 64, 64, 30, 40, 100};
		case Machine::BellClassic:		return {64, 30, 72, 64, 64, 0, 80, 100};
		case Machine::ClosedMetallic:	return {64, 10, 64, 64, 64, 20, 40, 100};
		case Machine::OpenMetallic:		return {64, 30, 64, 64, 64, 20, 40, 100};
		case Machine::CymbalMetallic:	return {64, 50, 64, 64, 64, 20, 40, 100};
		case Machine::BellMetallic:		return {64, 30, 80, 64, 64, 0, 80, 100};
		case Machine::Ride:				return {64, 50, 0, 40, 64, 64, 64, 100};
		case Machine::HatBasic:			return {64, 30, 64, 30, 0, 40, 60, 100};
		case Machine::HatLab:			return {64, 30, 70, 77, 90, 100, 110, 100};
		default:						return {64, 64, 64, 64, 64, 64, 64, 100};
		}
	}
}
