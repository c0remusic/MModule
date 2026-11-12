// The circuits of tracks 9-11's synths (edDrumsAnalogVoice.h): two oscillators and their VCAs, a noise source, and
// the digital waves the chip synth's control renders itself. Our own models of classic circuits; the control
// (edDrumsAnalogSynth.cpp) gives them their pitches, levels, waveforms, envelopes and streams every tick.
#include <algorithm>
#include <cmath>
#include <utility>

#include "edDrumsAnalogPort.h"
#include "edDrumsAnalogVoice.h"

namespace elektron::drums::analog::circuit
{
	namespace
	{
		// The circuit's state slots
		enum Slot : size_t
		{
			PhaseA, PhaseB,			// the oscillators' phases, 0-1
			EnvA, EnvB,				// the two VCAs' envelopes
			LevelA, LevelB,			// the level CVs the last tick played, for the ramp to this tick's
			StreamLast,				// outA's last sample of the last tick
			NoiseLow, NoiseBand,	// the noise filter's state
			NoiseAmp,				// the noise level the last tick played
			StreamIn, StreamOut,	// the stream's DC blocker: its last input and output
			StepA, StepB			// the oscillators' phase steps the last tick played, for advance()
		};
		enum USlot : size_t { Seed, Ticks, BThrough };	// BThrough: oscillator B was let through on the last tick

		// A control word of the frame: the voice's CVs are 16-bit, unsigned
		float cvWord(const VoiceIo& _io, const int _at) { return static_cast<float>(be16(_io.frame.data() + _at)); }

		// The pitch CVs are linear in frequency (the control takes the exponential): 2093 at note 60 and the tune at its
		// centre, which plays middle C, so a word is 1/8 Hz
		constexpr float HzPerCv = 0.125f;
		// The level CVs: 8191 at the waveform's full scale and the track level at 0 dB (the level byte adds up to 18 dB)
		constexpr float LevelScale = 1.0f / 8191;

		// The waveform a code of three switches selects: (a, b, c) as the control sets them, back into its code
		int waveCode(const VoiceIo& _io, const int _a, const int _b, const int _c)
		{
			const bool a = (_io.switches >> _a & 1) != 0, b = (_io.switches >> _b & 1) != 0, c = (_io.switches >> _c & 1) != 0;
			if(!a && b && !c)
				return 1;
			if(a && b && c)
				return 2;
			if(!a && !b && c)
				return 3;
			if(!a && !b && !c)
				return 4;
			if(a && !b && c)
				return 5;
			if(a && b && !c)
				return 6;
			return 0;
		}

		// PolyBLEP: the step correction of a band-limited edge at phase _p, _dt the phase step
		float blep(float _p, const float _dt)
		{
			if(_p < _dt)
			{
				_p /= _dt;
				return _p + _p - _p * _p - 1;
			}
			if(_p > 1 - _dt)
			{
				_p = (_p - 1) / _dt;
				return _p * _p + _p + _p + 1;
			}
			return 0;
		}
		float pulse(const float _p, const float _dt, const float _width)
		{
			float q = _p - _width;
			if(q < 0)
				q += 1;
			// less its mean, so that a narrow pulse sends no DC
			return (_p < _width ? 1.0f : -1.0f) + blep(_p, _dt) - blep(q, _dt) - (2 * _width - 1);
		}
		// Our waveforms by code: 0 saw, 1 square, 2 triangle, 3 a quarter pulse, 4 off, 5 saw and square, 6 sine. The
		// control already sets their levels apart (the louder waves 1 or 2.5 dB down)
		float wave(const int _code, const float _p, const float _dt)
		{
			switch(_code)
			{
			case 0: return 2 * _p - 1 - blep(_p, _dt);
			case 1: return pulse(_p, _dt, 0.5f);
			case 2: return 4 * std::fabs(_p - 0.5f) - 1;
			case 3: return pulse(_p, _dt, 0.25f);
			case 5: return 0.5f * (2 * _p - 1 - blep(_p, _dt) + pulse(_p, _dt, 0.5f));
			case 6: return std::sin(6.2831853f * _p);
			default: return 0;
			}
		}

		// The envelope generator's half at _at (0x10: A, 0x18: B): its decay as a factor a host sample. Its rate code
		// (bits 0-19, after the command 0xf01) says a decrement (1 - m / 2^17) / 2^s per tick of 24 kHz, the CPU's own
		// countdown; 0x7f in the level's low byte holds it (no decay)
		float decayFactor(const VoiceIo& _io, const int _at, const int _n)
		{
			const uint8_t* env = _io.frame.data() + port::frame::Envelope + _at;
			const uint32_t code = be32(env + 4);
			if((code >> 28) != 0xf || env[3] == 0x7f)
				return 1.0f;
			const double d = (1 - (code & 0xffff) / 131072.0) * std::ldexp(1.0, -static_cast<int>(code >> 16 & 0xf));
			return static_cast<float>(std::pow(1 - d, 16.0 / _n));
		}

		// Envelopes and streams freeze below this: nothing after them is heard
		constexpr float Floor = 1e-6f;

		uint32_t nextRandom(uint32_t& _s)
		{
			_s ^= _s << 13;
			_s ^= _s >> 17;
			_s ^= _s << 5;
			return _s;
		}

		// The oscillators' and the noise's output scale: the level byte at 64 (+9 dB) about 0.4 at the output, its top
		// (+18 dB) under 1.5. The chip's analog steps play one oscillator, at a lower level CV: louder
		constexpr float OutputScale = 0.16f;
		constexpr float ChipScale = 0.3f;
		// The raw synth's noise: white low-passed near 1 kHz keeps a tenth of its power, so it needs its own gain
		constexpr float RawNoiseScale = 1.0f;
		constexpr float DualScale = 0.135f;
		// outA's scale at the output: the dual's click (its peak 0.02 or less as the control leaves it), the chip's
		// digital waves (about 0.1, the level byte not applied)
		constexpr float ClickScale = 3.0f;
		constexpr float DigitalScale = 4.5f;
	}

	void synthTrig(const Machine, Circuit& _c)
	{
		// a Hit restarts everything: the phases at 0 (the oscillators are reset with the trig), the filters, envelopes
		// at 1
		_c = Circuit{};
		_c.f[EnvA] = _c.f[EnvB] = 1;
		_c.u[Seed] = 0x9e3779b9u;
	}

	void synthRender(const Machine _m, const VoiceIo& _io, Circuit& _c, float* _out, const int _n, const double _rate)
	{
		auto& f = _c.f;
		const bool first = _c.u[Ticks]++ == 0;
		const float inv = 1.0f / static_cast<float>(_n);

		// the oscillators: A always, B where switch 4 lets it through
		const int codeA = waveCode(_io, 6, 1, 5), codeB = waveCode(_io, 8, 2, 7);
		const bool hasB = (_io.switches >> 4 & 1) != 0;
		const auto rate = static_cast<float>(_rate);
		const float dtA = std::min(0.45f, cvWord(_io, port::frame::Cv) * HzPerCv / rate);
		const float dtB = std::min(0.45f, cvWord(_io, port::frame::Cv + 2) * HzPerCv / rate);
		const float levelA = cvWord(_io, port::frame::Cv + 4) * LevelScale;
		const float levelB = hasB ? cvWord(_io, port::frame::Cv + 6) * LevelScale : 0.0f;
		if(first)
		{
			f[LevelA] = levelA;
			f[LevelB] = levelB;
		}

		// the VCAs: the dual's two halves one each, the raw's and the chip's half B and A for both
		const bool dual = _m == Machine::SynthDual;
		const int halfA = _m == Machine::SynthRaw ? 0x18 : 0x10;
		const int halfB = _m == Machine::SynthChip ? 0x10 : 0x18;
		const float mulA = first ? 1.0f : decayFactor(_io, halfA, _n);
		const float mulB = first ? 1.0f : decayFactor(_io, halfB, _n);

		// the noise (raw only): white, low-passed where the second setting byte says, at heads' level
		const bool noisy = _m == Machine::SynthRaw;
		const float noiseAmp = noisy ? static_cast<float>(static_cast<int32_t>(be32(_io.heads.data()))) * 4.656613e-10f : 0;
		const float cutoff = 40.0f * std::exp2(_io.noise[2] * (9.0f / 127));
		const float g = std::min(1.0f, 2 * std::sin(3.14159265f * std::min(cutoff, rate * 0.24f) / rate));

		// outA: the dual's click and the chip's digital waves, 32 samples across the tick
		const bool stream = _m != Machine::SynthRaw;
		const float streamScale = dual ? ClickScale : DigitalScale;
		const float oscScale = _m == Machine::SynthChip ? ChipScale : dual ? DualScale : OutputScale;
		const float blocker = 1 - 125.66371f / rate;	// 1 - 2 pi 20 Hz / rate
		const uint8_t* a = _io.outA.data();

		float pA = f[PhaseA], pB = f[PhaseB], eA = f[EnvA], eB = f[EnvB];
		for(int i = 0; i < _n; ++i)
		{
			const float t = static_cast<float>(i) * inv;
			const float lA = f[LevelA] + (levelA - f[LevelA]) * t, lB = f[LevelB] + (levelB - f[LevelB]) * t;
			eA *= mulA;
			eB *= dual ? mulB : mulA;
			float y = wave(codeA, pA, dtA) * lA * eA;
			if(hasB)
				y += wave(codeB, pB, dtB) * lB * (dual ? eB : eA);
			y *= oscScale;
			pA += dtA;
			pA -= pA >= 1 ? 1.0f : 0.0f;
			pB += dtB;
			pB -= pB >= 1 ? 1.0f : 0.0f;
			if(noisy)
			{
				const float white = static_cast<float>(static_cast<int32_t>(nextRandom(_c.u[Seed]))) * 4.656613e-10f;
				f[NoiseLow] += g * f[NoiseBand];
				f[NoiseBand] += g * (white - f[NoiseLow] - 0.7f * f[NoiseBand]);
				const float amp = f[NoiseAmp] + (noiseAmp - f[NoiseAmp]) * t;
				y += f[NoiseLow] * amp * eB * RawNoiseScale;
			}
			if(stream)
			{
				// the 48 kHz stream at this host sample: linear between its two neighbours
				const float x = t * 32 - 1;
				const int k = static_cast<int>(std::floor(x));
				const float fr = x - static_cast<float>(k);
				const float s0 = k < 0 ? f[StreamLast] : static_cast<float>(static_cast<int32_t>(be32(a + 4 * k)));
				const float s1 = static_cast<float>(static_cast<int32_t>(be32(a + 4 * (k + 1))));
				// through a DC blocker at about 20 Hz: the chip's narrow pulses are one-sided
				const float s = (s0 + (s1 - s0) * fr) * 4.656613e-10f * streamScale;
				const float hp = s - f[StreamIn] + blocker * f[StreamOut];
				f[StreamIn] = s;
				f[StreamOut] = std::fabs(hp) < 1e-9f ? 0.0f : hp;
				y += f[StreamOut];
			}
			_out[i] += y;
		}
		_c.u[BThrough] = hasB || noisy ? 1 : 0;
		f[PhaseA] = pA;
		f[PhaseB] = pB;
		f[StepA] = dtA;
		f[StepB] = dtB;
		f[EnvA] = eA < Floor ? 0 : eA;
		f[EnvB] = eB < Floor ? 0 : eB;
		f[LevelA] = levelA;
		f[LevelB] = levelB;
		f[NoiseAmp] = noiseAmp;
		f[StreamLast] = static_cast<float>(static_cast<int32_t>(be32(a + 4 * 31)));
		if(eB == 0 && f[NoiseAmp] == 0)
			f[NoiseLow] = f[NoiseBand] = 0;
	}

	void synthAdvance(const Machine, Circuit& _c, const int64_t _samples, double)
	{
		// what runs free: the two oscillators, at the rates they last played (the steps are per host sample already).
		// The noise's generator needs no advance: any state of it is as white
		for(const auto [phase, step] : {std::pair<size_t, size_t>{PhaseA, StepA}, {PhaseB, StepB}})
		{
			const double p = _c.f[phase] + static_cast<double>(_c.f[step]) * static_cast<double>(_samples);
			_c.f[phase] = static_cast<float>(p - std::floor(p));
		}
	}

	bool synthEnded(const Machine, const VoiceIo&, const Circuit& _c)
	{
		// both VCAs' envelopes at zero, and the stream (the click, the chip's digital waves) back to zero through its
		// DC blocker (the click's integer ramp can rest a word or two from zero: under 1e-6 counts)
		const auto& f = _c.f;
		// (envelope B only where it reaches the output: oscillator B through, or the raw synth's noise)
		return f[EnvA] == 0 && (f[EnvB] == 0 || _c.u[BThrough] == 0) && std::fabs(f[StreamLast]) * 4.656613e-10f < 1e-6f && f[StreamOut] == 0;
	}

	void synthParams(const Machine _m, const uint8_t* _controls, std::array<uint8_t, 20>& _params)
	{
		// the words each Machine control sets (value << 8); the others fixed
		int word[10] = {64, 64, 64, 64, 64, 64, 64, 64, 0, 0};
		const auto c = [&](const int _i) { return static_cast<int>(_controls[_i]); };
		switch(_m)
		{
		case Machine::SynthDual:
			// tune, decay, detune, second decay, balance, sweep, mode (0-79), level
			word[1] = c(0); word[2] = c(1); word[3] = c(2); word[4] = c(3); word[5] = c(4); word[6] = c(5);
			word[7] = c(6) * 79 / 127; word[0] = c(7);
			break;
		case Machine::SynthRaw:
			// tune, decay, detune, noise, first wave (0-11), second wave (0-5, 6 off), balance, level
			word[1] = c(0); word[2] = c(1); word[3] = c(2); word[4] = c(3); word[5] = c(4) * 11 / 127;
			word[6] = c(5) * 6 / 127; word[7] = c(6); word[0] = c(7);
			break;
		default:
			// tune, decay, steps 1-3, program (0-29), speed (0-65), level
			word[1] = c(0); word[2] = c(1); word[3] = c(2); word[4] = c(3); word[5] = c(4);
			word[6] = c(5) * 29 / 127; word[7] = c(6) * 65 / 127; word[0] = c(7);
			break;
		}
		for(int i = 0; i < 10; ++i)
		{
			_params[static_cast<size_t>(2 * i)] = static_cast<uint8_t>(word[i]);
			_params[static_cast<size_t>(2 * i + 1)] = 0;
		}
	}

	std::array<uint8_t, 8> synthDefaults(const Machine _m)
	{
		switch(_m)
		{
		case Machine::SynthDual: return {64, 28, 71, 24, 64, 76, 10, 64};
		case Machine::SynthRaw: return {64, 28, 64, 40, 12, 64, 100, 64};
		default: return {64, 44, 76, 71, 64, 50, 71, 64};
		}
	}
}
