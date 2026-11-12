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
			alignas(16) float x[4];
			_mm_store_ps(x, _v);
			return (x[0] + x[1]) + (x[2] + x[3]);
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

		// What each machine's circuit is made of
		enum class Source : uint8_t { Six, RingPairs, Bell, RingBell, Ride };
		struct Band
		{
			float hz, q;
			bool highPass;
			float w[4];			// the stages that open it, as weights
		};
		struct Recipe
		{
			Source source;
			int bands;
			Band band[3];
			bool timesD;		// every band also through stage D
			bool toneFromCv;	// the tone word comes from the control (Cv + 0), else the tone byte
			float outHp;		// the output high-pass
			float trim;			// the output's level at unity
			float noise;		// the noise path's level at the noise byte's top
			float burst;		// outA's level at the trig, which the burst's noise scales to one: its peak's inverse
			bool hpFromByte8;	// parameter byte 8, which the machine's control does not read, moves the high-pass
			bool drive;			// the snap byte drives the output (a soft clip) instead
		};
		// Stages: 0 A, 1 B, 2 C, 3 D. A snap weight (the snap byte) adds to the stage marked by w = -1
		const Recipe& recipeOf(const Machine _m)
		{
			static const Recipe r[] = {
				// the six-square closed hat: a band at 7 kHz, opened by stage D, stage C a click
				{Source::Six, 1, {{7100, 1.6f, false, {0, 0, -1, 1}}}, false, true, 6000, 1.35f, 0.4f, 0, true, false},
				// the open one: stage D
				{Source::Six, 1, {{7100, 1.6f, false, {0, 0, 0, 1}}}, false, true, 5500, 3.6f, 0.4f, 0, true, true},
				// the cymbal: three bands, each its stage, through D
				{Source::Six, 3, {{3440, 1.2f, false, {1, 0, 0, 0}}, {7100, 1.5f, false, {0, 1, 0, 0}},
					{9000, 0.7f, true, {0, 0, 1, 0}}}, true, true, 2000, 8.0f, 0.4f, 0, false, true},
				// the two-square bell: a broad band, stage C's strike over D's ring
				{Source::Bell, 1, {{2640, 0.9f, false, {0, 0, -1, 0.45f}}}, false, false, 300, 2.0f, 0.2f, 0, false,
					false},
				// ring-modulated closed hat: three pairs, a band at 8.5 kHz, stage C (its attack) times D
				{Source::RingPairs, 1, {{8500, 1.4f, false, {0, 0, 1, 0}}}, true, false, 5000, 0.8f, 0.4f, 0, true,
					true},
				{Source::RingPairs, 1, {{8500, 1.4f, false, {0, 0, 1, 0}}}, true, false, 5000, 2.4f, 0.4f, 0, true,
					true},
				// ring-modulated cymbal: a low and a high band
				{Source::RingPairs, 2, {{4500, 1.0f, false, {0, 1, 0, 0}}, {9500, 0.7f, true, {0, 0, 1, 0}}}, true,
					false,
					2000, 0.48f, 0.4f, 300, false, false},
				// ring-modulated bell
				{Source::RingBell, 1, {{2640, 0.9f, false, {0, 0, -1, 0.45f}}}, false, false, 300, 3.5f, 0.2f, 0, false,
					false},
				// the ride: three bands opened by stages A, B, C, the stick's click in outA
				{Source::Ride, 3, {{3440, 1.2f, false, {1, 0, 0, 0}}, {7100, 1.5f, false, {0, 1, 0, 0}},
					{9000, 0.7f, true, {0, 0, 1, 0}}}, false, false, 2000, 7.7f, 0.0f, 70, false, false},
				// the beating hat: a band at 8 kHz opened by stage C, its noise burst in outA
				{Source::Six, 1, {{8000, 1.4f, false, {0, 0, 1, 0}}}, false, true, 6000, 2.7f, 0.6f, 200, false, false},
				// the tuned hat: six squares at their own knobs, stage C
				{Source::Six, 1, {{8000, 1.2f, false, {0, 0, 1, 0}}}, false, false, 4000, 1.08f, 0.0f, 0, false, false},
			};
			return r[static_cast<int>(_m) - FirstMetal];
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
		const Recipe& r = recipeOf(_m);
		readStages(_io, _c, _rate);
		const auto& p = _io.params;

		// the tone: about an octave each way, from the control's word or our own byte
		const double tone = r.toneFromCv ? static_cast<int16_t>(be16(_io.frame.data() + Cv)) / 32768.0
			: (p[ToneByte] - 64) / 64.0;
		const double reso = std::exp2((p[ResoByte] - 64) / 48.0);
		Svf f[3];
		for(int b = 0; b < r.bands; ++b)
			f[b].set(r.band[b].hz * std::exp2(tone), r.band[b].q * reso, _rate);
		Svf noiseHp;
		// the output's one-pole high-pass: y = a (y + x - x_last)
		const double hpHz = r.outHp * (r.hpFromByte8 ? std::exp2((p[8] - 64) / 32.0) : 1.0);
		const auto hpA = static_cast<float>(1.0 / (1.0 + 2 * 3.14159265358979 * hpHz / _rate));
		// the beating hat's noise: its settings a band (byte 2 in hundreds of Hz) and its level (byte 3 of 64)
		const bool settings = _io.noise[3] != 0;
		noiseHp.set(settings ? _io.noise[2] * 100.0 : 7000.0, settings ? _io.noise[0] / 10.0 : 1.0, _rate);
		const float noiseLevel = r.noise * (p[NoiseByte] / 127.0f) * (settings ? _io.noise[3] / 64.0f : 1.0f);
		const float snap = p[SnapByte] / 127.0f;
		const float drive = r.drive ? 3.0f * snap : 0.0f;
		const float out = r.trim * static_cast<float>(be16(_io.frame.data() + Cv + 8)) / 32768.0f;

		// the oscillators: six, or three pairs, or two
		int count = 6, at = 0;
		if(r.source == Source::RingPairs)
			at = 6;
		else if(r.source == Source::Bell || r.source == Source::RingBell)
			at = 6, count = 2;
		// the ride's sets 2 and 3 are the ring-modulated pairs
		const bool ring = r.source == Source::RingPairs || (r.source == Source::Ride && p[6] >= 2);
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
		const __m128 pairs = _mm_set_ps(0.0f, 1.0f / 3, 1.0f / 3, 1.0f / 3);
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
			const auto v = static_cast<int32_t>(be32(_io.outA.data() + 4 * i));
			a[i + 1] = std::abs(v) < (1 << 11) ? 0.0f : std::fabs(static_cast<float>(v) / 2147483648.0f);
			burst = burst || a[i + 1] != 0.0f;
		}
		_c.f[LastA] = a[32];
		// the voice is frozen once every stage has ended, or once stage D has where every band passes it
		const auto ended = [&](const int _s) { return _c.f[Stage + _s] == 0.0f && _c.f[Target + _s] == 0.0f; };
		const bool quiet = !burst && ((ended(0) && ended(1) && ended(2) && (r.timesD || ended(3))) ||
			(r.timesD && ended(3)));
		_c.u[Frozen] = quiet ? 1u : 0u;
		if(quiet)
		{
			// a frozen voice: nothing sounds and nothing is left to ring; its oscillators run on
			std::fill(_c.f.begin() + Filter, _c.f.begin() + LastA, 0.0f);
			for(int i = 0; i < 8; ++i)
			{
				const double ph = _c.f[static_cast<size_t>(Phase + i)] + static_cast<double>(dt[i]) * _n;
				_c.f[static_cast<size_t>(Phase + i)] = static_cast<float>(ph - std::floor(ph));
			}
			return;
		}
		const bool noisy = burst || noiseLevel > 0.0f;
		// the burst at its snap byte (the ride's at a fixed half)
		const float burstLevel = r.burst * (r.noise > 0 ? snap : 0.5f);
		const float cut = static_cast<float>(std::exp(-1.0 / (0.005 * _rate)));
		float w[3][4];
		for(int b = 0; b < r.bands; ++b)
			for(int s = 0; s < 4; ++s)
				w[b][s] = (r.band[b].w[s] < 0 ? snap : r.band[b].w[s]) * gain[s];
		const float step = 32.0f / static_cast<float>(_n);
		float m[4];
		stageFactors(_c, cut, m);
		const float* t = &_c.f[Target];
		for(int i = 0; i < _n; ++i)
		{
			const __m128 even = sq.run(&_c.f[Phase], 0), odd = sq.run(&_c.f[Phase + 4], 1);
			float src;
			if(ring)
				src = sum(_mm_mul_ps(_mm_mul_ps(even, odd), pairs));
			else if(r.source == Source::RingBell)
				src = 0.7f * sum(_mm_add_ps(_mm_mul_ps(even, w0), _mm_mul_ps(odd, w1))) +
					0.3f * _mm_cvtss_f32(_mm_mul_ss(even, odd));
			else
				src = sum(_mm_add_ps(_mm_mul_ps(even, w0), _mm_mul_ps(odd, w1)));
			float noise = 0, level = 0;
			if(noisy)
			{
				noise = noiseHp.run(&_c.f[NoiseFilter], noiseSample(_c), false);
				// outA across the tick at its own rate: 32 values over _n host samples, between the last tick's
				const float pos = static_cast<float>(i + 1) * step;
				const int k0 = std::min(static_cast<int>(pos), 31);
				level = a[k0] + (a[k0 + 1] - a[k0]) * (pos - static_cast<float>(k0));
			}
			float* e = &_c.f[Stage];
			for(int st = 0; st < 4; ++st)
				e[st] = t[st] + (e[st] - t[st]) * m[st];
			const float x = src + noise * noiseLevel;
			float y = noise * level * burstLevel;
			for(int b = 0; b < r.bands; ++b)
			{
				float open = w[b][0] * e[0] + w[b][1] * e[1] + w[b][2] * e[2] + w[b][3] * e[3];
				if(r.timesD)
					open *= e[3] * gain[3];
				y += f[b].run(&_c.f[Filter + 2 * b], x, r.band[b].highPass) * open;
			}
			y = y * (1.0f + drive) / (1.0f + drive * std::fabs(y));
			float* h = &_c.f[OutHp];
			h[0] = hpA * (h[0] + y - h[1]);
			h[1] = y;
			_out[i] += h[0] * out;
		}
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
		case Machine::ClosedMetallic:	return {64, 28, 64, 64, 64, 20, 40, 100};
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
