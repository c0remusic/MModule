#include "edDrumsAnalogVoice.h"

#include <algorithm>
#include <cmath>

// The kicks' circuits (tracks 9-11), our own: a sine body whose pitch and level follow the envelope generator the
// control drives, the click the control sends, and a noise burst from the control's noise settings. The laws from the
// control's words to Hz, seconds and levels are chosen here (see each), not the original's.
//
// Names (ticket 27), Machine 1-8 as kickParams lays them out:
//   HARD THUMP  TUNE, DECAY, SWEEP, SWEEP TIME, HOLD, CLICK, TONE/CLICK, DRIVE
//   NOISE KICK  TUNE, DECAY, SWEEP, SWEEP TIME, HOLD, NOISE STYLE, TONE/CLICK, DRIVE
//   PHASE KICK  TUNE, DECAY, SWEEP TIME, FM AMOUNT, FM FREQ, FM DECAY, DRIVE, (none)
//   TOY KICK    TUNE, DECAY, SWEEP, SWEEP TIME, TONE, CLICK, OVERDRIVE, DRIVE
//   SOFT KICK   TUNE, DECAY, SWEEP, SWEEP TIME, HOLD, NOISE, TONE, DRIVE
//   SNAP KICK   TUNE, DECAY, SWEEP, SWEEP TIME, HOLD, CLICK, CLICK DAMP, DRIVE
//   BODY KICK   TUNE, DECAY, SWEEP, SWEEP TIME, HOLD, BODY, OVERDRIVE, DRIVE
namespace elektron::drums::analog::circuit
{
	namespace
	{
		constexpr double Pi = 3.14159265358979323846;

		// The circuit's state, by slot
		namespace slot
		{
			constexpr int Phase = 0;		// the body's phase, in turns
			constexpr int Phase2 = 1;		// the second oscillator's (overtone or FM modulator)
			constexpr int Seg = 2;			// 4: the envelope segments' values
			constexpr int Sweep = 6;		// the pitch sweep, in octaves
			constexpr int Index = 7;		// FM: the index envelope
			constexpr int Click = 8;		// the click's lowpass
			constexpr int NoiseHp = 9;		// the noise's highpass memory (input, output)
			constexpr int NoiseHpOut = 10;
			constexpr int NoiseLp1 = 11;	// its two lowpass poles
			constexpr int NoiseLp2 = 12;
			constexpr int NoiseEnv = 13;	// its burst envelope
			constexpr int LastClick = 14;	// the last 48 kHz click sample of the tick before
			constexpr int Ring1 = 15;		// the click's resonator, its two last outputs
			constexpr int Ring2 = 16;

			constexpr int Cmd = 0;			// u: 4, the segments' command longs of the tick before
			constexpr int Seed = 4;			// u: the noise generator
			constexpr int NoiseOn = 5;		// u: a burst has started
			constexpr int Struck = 6;		// u: the body's level segment has had its attack
		}

		// What differs between the kicks' circuits
		struct Patch
		{
			int amp;			// the envelope segment that sets the body's level
			int pitch;			// the segment that sweeps its pitch
			bool sweepOnAttack;	// the sweep falls from the attack on (else it holds through the attack, falls at release)
			float levelFull;	// the pitch segment's level byte (signed) that sweeps sweepOctaves
			float sweepOctaves;
			int bodyCv;			// the CV word of the body's pitch
			int secondCv;		// the second oscillator's CV word, -1 for none
			float second;		// its level (overtone) or the FM index at full scale
			bool fm;			// the second oscillator phase-modulates the body
			bool toneSecond;	// the fourth CV (the tone) sets the second oscillator's level
			float ring;			// the click rings a resonator tuned here (Hz), 0 for none
			float clickGain;
			float noiseGain;
			float bodyGain;
		};

		const Patch& patchOf(const Machine _m)
		{
			static const Patch patches[] = {
				// Hard: held pitch then a falling sweep, an overtone at the third CV's 3:2, the click and a thin noise
				{3, 1, false, 127, 4.0f, 1, 0, 0.12f, false, false, 2500, 2.0f, 1.0f, 0.75f},
				// Classic: the sweep falls from the attack; its noise styles do the work of a click
				{3, 1, true, 63, 3.0f, 1, 0, 0.08f, false, false, 1800, 2.0f, 1.2f, 0.7f},
				// FM: a two-operator body, its modulator on the second CV, the index from the FM depth CV
				{2, 0, false, -128, 2.0f, 0, 1, 6.0f, true, false, 3000, 2.0f, 1.0f, 0.7f},
				// Plastic: a short trapezoid click and two equal oscillators slightly apart
				{2, 0, true, 15, 3.0f, 1, 0, 1.0f, false, true, 0, 2.5f, 1.2f, 0.7f},
				// Silky: no click, a soft body and a long breath of noise
				{2, 0, true, 15, 2.0f, 0, -1, 0.0f, false, false, 0, 0.0f, 2.4f, 0.8f},
				// Sharp: the click, a tight body
				{3, 1, true, 15, 3.0f, 1, -1, 0.0f, false, false, 4000, 1.5f, 1.2f, 0.8f},
				// Acoustic: the sampled body the control sends, under a soft sine
				{2, 1, true, 15, 2.0f, 1, -1, 0.0f, false, false, 0, 1.2f, 1.2f, 0.6f},
			};
			return patches[std::min(static_cast<int>(_m), 6)];
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

		float flush(const float _v) { return std::fabs(_v) < 1e-9f ? 0.0f : _v; }

		float wrap(const float _turns) { return _turns >= 1.0f ? _turns - std::floor(_turns) : _turns; }

		// sin(2 pi _x) for _x in turns (0-1): a parabola, refined once (within 0.1 %)
		float sineTurn(const float _x)
		{
			const float t = _x - 0.5f;
			const float y = 8 * t - 16 * t * std::fabs(t);
			return -(0.225f * (y * std::fabs(y) - y) + y);
		}

		// The body's saturation: tanh's shape as a rational curve, flat past 3
		float saturate(const float _x)
		{
			const float x = std::clamp(_x, -3.0f, 3.0f);
			return x * (27 + x * x) / (27 + 9 * x * x);
		}
	}

	// A Hit restarts all of a kick's circuit: phases, segments, sweep, click, noise and its generator
	void kickTrig(Machine, Circuit& _c)
	{
		_c = Circuit{};
		_c.u[slot::Seed] = 0x2545f491u;
	}

	// Nothing of a kick runs free between Hits: its phases restart at each one
	void kickAdvance(Machine, Circuit&, int64_t, double) {}

	void kickRender(const Machine _m, const VoiceIo& _io, Circuit& _c, float* _out, const int _n, const double _rate)
	{
		const Patch& p = patchOf(_m);
		auto& f = _c.f;
		const double perHost = 48000.0 / _rate;		// 48 kHz samples a host sample
		const double stepsPerHost = StepRate / _rate;	// the envelope generator's steps a host sample

		// the envelope segments: each slews toward its level byte at its rate while its command is an attack (0xf) or
		// a release (0xb); any other command clears it
		const uint8_t* env = _io.frame.data() + 8;
		float keep[4], target[4];
		bool attackStarts[4], releaseStarts[4];
		for(int s = 0; s < 4; ++s)
		{
			const uint8_t* seg = env + 8 * s;
			const uint32_t lng = be32(seg + 4);
			const uint32_t cmd = lng >> 28;
			// a new attack or release: the command long written anew (its level or rate changed with it)
			const uint32_t prev = _c.u[static_cast<size_t>(slot::Cmd + s)];
			attackStarts[s] = cmd == 0xf && lng != prev;
			releaseStarts[s] = cmd == 0xb && lng != prev;
			_c.u[static_cast<size_t>(slot::Cmd + s)] = lng;
			if(cmd == 0xf && s == p.amp)
				_c.u[slot::Struck] = 1;
			if(cmd == 0xf || cmd == 0xb)
			{
				keep[s] = static_cast<float>(std::pow(keepPerSample(lng & 0xfffff), stepsPerHost));
				target[s] = seg[2] / 127.0f;
			}
			else
			{
				keep[s] = 0;
				target[s] = 0;
				f[static_cast<size_t>(slot::Seg + s)] = 0;
			}
		}

		// the pitch sweep: from the pitch segment's level, in octaves, falling at its rate from the attack or the release
		const uint8_t* ps = env + 8 * p.pitch;
		const float level = std::min(1.0f, static_cast<int8_t>(ps[2]) / p.levelFull);
		if(attackStarts[p.pitch])
			f[slot::Sweep] = p.sweepOctaves * level;
		const bool sweeping = p.sweepOnAttack || _c.u[static_cast<size_t>(slot::Cmd + p.pitch)] >> 28 == 0xb;
		if(!p.sweepOnAttack && releaseStarts[p.pitch])
			f[slot::Sweep] = std::max(f[slot::Sweep], p.sweepOctaves * level);
		// in octaves the sweep falls 2 times slower than a level at the same rate: to a thousandth of its depth in twice
		// the 60 dB time (chosen by ear: a 60 ms code sweeps for about 120 ms, falling by e in 17)
		const float sweepKeep = sweeping ? std::sqrt(keep[p.pitch]) : 1.0f;
		// FM: the index envelope starts whole at the attack of the segment after the pitch one, falls at its rate
		const int indexSeg = (p.pitch + 1) & 3;
		if(p.fm && attackStarts[indexSeg])
			f[slot::Index] = 1.0f;
		const float indexKeep = p.fm ? keep[indexSeg] : 1.0f;

		const double bodyHz = cvHz(be16(_io.frame.data() + 2 * p.bodyCv));
		// the second oscillator on its CV; a tone-set one (two equal CVs) beats a little against the body, 0.7 % above it
		const double secondHz = p.secondCv >= 0 ? cvHz(be16(_io.frame.data() + 2 * p.secondCv)) * (p.toneSecond ? 1.007 : 1.0) : 0.0;
		// the level CVs: the one the accent scales (23274 at its middle; the third or the fourth word by machine, the other
		// a tone or 0) drives the body's saturation from 1 to 4
		const unsigned cv2 = be16(_io.frame.data() + 4), cv3 = be16(_io.frame.data() + 6);
		// the switches the control sets: 7 lets the click through whole, 8 doubles the second oscillator, 4 lets the
		// noise through whole and, off, drives the body twice as hard
		const auto sw = [&](const int _k) { return (_io.switches >> _k & 1) != 0; };
		const float drive = (1.0f + 3.0f * static_cast<float>(std::max(cv2, cv3)) / 65535.0f) * (sw(4) ? 1.0f : 2.0f);
		const float clickGain = p.clickGain * (sw(7) ? 1.0f : 0.35f);
		const float secondLevel = p.second * (sw(8) ? 1.8f : 1.0f) * (p.toneSecond ? cv3 / 8192.0f : 1.0f);
		const float noiseGain = p.noiseGain * (sw(4) ? 1.0f : 0.3f);
		const float driveNorm = 1.0f / saturate(drive);
		// FM: the depth CV (FM AMOUNT, 0 to about 13000) sets the index, p.second radians at 13000
		const float fmDepth = p.fm ? p.second * be16(_io.frame.data() + 6) / 13000.0f : 0.0f;

		// the noise: its level from heads (Q31), shaped by the control's four settings: lowpass at 200 Hz 2^(s/16) for
		// the first two, a burst falling 60 dB in 1 ms 2^(s/16) for the third, a highpass at 20 Hz 2^(s/16) for the
		// fourth; the burst starts with the noise level
		const float noiseLevel = std::fabs(static_cast<float>(static_cast<int32_t>(be32(_io.heads.data())))) / 2147483648.0f;
		if(noiseLevel > 0 && _c.u[slot::NoiseOn] == 0)
		{
			_c.u[slot::NoiseOn] = 1;
			f[slot::NoiseEnv] = 1;
		}
		const float ampTarget = target[p.amp];
		const float ampKeep = keep[p.amp];
		float amp = f[static_cast<size_t>(slot::Seg + p.amp)];
		const float sweep = f[slot::Sweep];
		float index = f[slot::Index];
		float phase = f[slot::Phase], phase2 = f[slot::Phase2];
		// the sweep's factor at the tick's start and end, the frequency between them linearly
		const auto sweepEnd = static_cast<float>(sweep * std::pow(static_cast<double>(sweepKeep), _n));
		const auto bendFrom = static_cast<float>(std::exp2(sweep));
		const float bendStep = (static_cast<float>(std::exp2(sweepEnd)) - bendFrom) / static_cast<float>(_n);
		const auto inc = static_cast<float>(bodyHz / _rate), inc2 = static_cast<float>(secondHz / _rate);
		const float fmTurns = fmDepth * static_cast<float>(1 / (2 * Pi));

		// the body, and its level kept per sample for the noise
		float levels[64];
		for(int i = 0; i < _n; ++i)
		{
			amp = ampTarget + (amp - ampTarget) * ampKeep;
			levels[i] = amp;
			const float bend = bendFrom + bendStep * static_cast<float>(i);
			phase = wrap(phase + inc * bend);
			float x = phase, second = 0;
			if(p.secondCv >= 0)
			{
				phase2 = wrap(phase2 + inc2 * bend);
				const float s2 = sineTurn(phase2);
				if(p.fm)
				{
					index *= indexKeep;
					x = wrap(x + fmTurns * index * s2 + 1.0f);
				}
				else
					second = secondLevel * s2;
			}
			_out[i] += p.bodyGain * amp * saturate(drive * (sineTurn(x) + second)) * driveNorm;
		}

		// the click: outA across the tick at 48 kHz, linearly, through a lowpass at 6 kHz, and ringing a resonator that
		// falls 60 dB in 10 ms
		float click = f[slot::Click], lastClick = f[slot::LastClick];
		float r1 = f[slot::Ring1], r2 = f[slot::Ring2];
		const float last = static_cast<float>(static_cast<int32_t>(be32(_io.outA.data() + 124)));
		if(clickGain > 0 && (click != 0 || lastClick != 0 || last != 0 || be32(_io.outA.data()) != 0 || r1 != 0 || r2 != 0))
		{
			float in[33];
			in[0] = lastClick;
			for(int k = 0; k < 32; ++k)
				in[k + 1] = static_cast<float>(static_cast<int32_t>(be32(_io.outA.data() + 4 * k))) * (1.0f / 2147483648.0f);
			in[0] *= 1.0f / 2147483648.0f;
			const float clickLp = onePole(6000, _rate);
			const auto radius = static_cast<float>(std::pow(1e-3, 1 / (0.01 * _rate)));
			const auto a1 = static_cast<float>(2 * radius * std::cos(2 * Pi * p.ring / _rate)), a2 = radius * radius;
			const float g = (1 - a2) * 2.0f;
			for(int i = 0; i < _n; ++i)
			{
				const double pos = i * perHost;
				const int k = static_cast<int>(pos);
				const float c = in[k] + static_cast<float>(pos - k) * (in[k + 1] - in[k]);
				click += clickLp * (c - click);
				float ring = 0;
				if(p.ring > 0)
				{
					ring = a1 * r1 - a2 * r2 + g * c;
					r2 = r1;
					r1 = ring;
				}
				_out[i] += clickGain * (click + ring);
			}
		}
		lastClick = last;

		// the noise: its level from heads (Q31), shaped by the control's four settings: lowpass at 200 Hz 2^(s/16) for
		// the first two, a burst falling 60 dB in 1 ms 2^(s/16) for the third, a highpass at 20 Hz 2^(s/16) for the
		// fourth; the burst starts with the noise level, and the body's level gates it
		float hpIn = f[slot::NoiseHp], hpOut = f[slot::NoiseHpOut], n1 = f[slot::NoiseLp1], n2 = f[slot::NoiseLp2];
		float nEnv = f[slot::NoiseEnv];
		uint32_t seed = _c.u[slot::Seed];
		if(noiseGain > 0 && noiseLevel > 0 && nEnv > 1e-6f)
		{
			const auto bytesHz = [](const int _b, const double _base) { return _base * std::exp2(_b / 16.0); };
			const float lp1 = onePole(bytesHz(_io.noise[0], 200), _rate);
			const float lp2 = onePole(bytesHz(_io.noise[1], 200), _rate);
			const auto noiseKeep = static_cast<float>(std::pow(1e-3, 1.0 / (bytesHz(_io.noise[2], 1e-3) * _rate)));
			const auto hp = static_cast<float>(std::exp(-2 * Pi * bytesHz(_io.noise[3], 20) / _rate));
			const float gain = noiseGain * noiseLevel;
			for(int i = 0; i < _n; ++i)
			{
				const float w = noiseSample(seed);
				hpOut = hp * (hpOut + w - hpIn);
				hpIn = w;
				n1 += lp1 * (hpOut - n1);
				n2 += lp2 * (n1 - n2);
				nEnv *= noiseKeep;
				_out[i] += gain * nEnv * levels[i] * n2;
			}
		}
		else
			nEnv = hpIn = hpOut = n1 = n2 = 0;

		// a sound ended leaves nothing: no tail under the floor, no denormals
		// the body's level under -120 dB is over: the voice has ended once nothing else sounds
		f[static_cast<size_t>(slot::Seg + p.amp)] = std::fabs(amp) < 1e-6f ? 0.0f : amp;
		f[slot::Sweep] = flush(sweepEnd);
		f[slot::Index] = flush(index);
		f[slot::Phase] = static_cast<float>(phase);
		f[slot::Phase2] = static_cast<float>(phase2);
		f[slot::Click] = flush(click);
		f[slot::Ring1] = flush(r1);
		f[slot::Ring2] = flush(r2);
		f[slot::LastClick] = lastClick;
		f[slot::NoiseHp] = flush(hpIn);
		f[slot::NoiseHpOut] = flush(hpOut);
		f[slot::NoiseLp1] = flush(n1);
		f[slot::NoiseLp2] = flush(n2);
		f[slot::NoiseEnv] = flush(nEnv);
		_c.u[slot::Seed] = seed;
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
		case Machine::KickHard:		return {64, 50, 95, 40, 16, 80, 0, 40};
		case Machine::KickClassic:	return {64, 50, 100, 50, 16, 40, 0, 40};
		case Machine::KickFm:		return {64, 50, 50, 40, 70, 30, 40, 0};
		case Machine::KickPlastic:	return {66, 76, 100, 40, 64, 80, 0, 40};
		case Machine::KickSilky:	return {64, 80, 96, 40, 20, 50, 64, 30};
		case Machine::KickSharp:	return {64, 72, 100, 36, 16, 90, 0, 40};
		default:					return {64, 74, 90, 40, 16, 90, 110, 30};
		}
	}

	bool kickEnded(const Machine _m, const VoiceIo& _io, const Circuit& _c)
	{
		// every envelope at zero after the attack: the body's level, the noise burst, the click and what the control sends
		const Patch& p = patchOf(_m);
		if(_c.u[slot::Struck] == 0 || _c.f[static_cast<size_t>(slot::Seg + p.amp)] != 0 || _c.f[slot::NoiseEnv] != 0
			|| _c.f[slot::Click] != 0 || _c.f[slot::Ring1] != 0 || _c.f[slot::Ring2] != 0)
			return false;
		for(size_t i = 0; i < 128; i += 4)
			if(be32(_io.outA.data() + i) != 0)
				return false;
		return true;
	}
}
