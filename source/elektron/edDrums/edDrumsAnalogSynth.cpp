// The synths of tracks 9-11 (edDrumsAnalog.h): dual, raw and chip, a block of each into the voice's memory, as the main
// CPU computes it
#include <algorithm>
#include <array>
#include <cmath>

#include "edDrumsAnalogPort.h"
#include "edDrumsAnalogRates.h"

namespace elektron::drums::analog::port
{
	namespace
	{
		int16_t word(const VoiceIo& _io, const int _k)
		{
			return static_cast<int16_t>(be16(_io.params.data() + 2 * _k));
		}
		// A word's top byte, the value of its knob, as the CPU reads it: signed
		int knobValue(const VoiceIo& _io, const int _k)
		{
			return static_cast<int8_t>(_io.params[static_cast<size_t>(2 * _k)]);
		}

		int32_t stateLong(const VoiceIo& _io, const int _k)
		{
			return static_cast<int32_t>(be32(_io.state.data() + 4 * _k));
		}
		void setStateLong(VoiceIo& _io, const int _k, const int32_t _v)
		{
			setBe32(_io.state.data() + 4 * _k, static_cast<uint32_t>(_v));
		}

		void setFrame16(VoiceIo& _io, const int _at, const int32_t _v)
		{
			setBe16(_io.frame.data() + _at, static_cast<uint32_t>(_v));
		}
		void setFrame32(VoiceIo& _io, const int _at, const uint32_t _v) { setBe32(_io.frame.data() + _at, _v); }
		// An envelope command byte: its top nibble the command, its bottom nibble kept
		void setCommand(VoiceIo& _io, const int _at, const uint8_t _command)
		{
			uint8_t& b = _io.frame[static_cast<size_t>(frame::Envelope + _at)];
			b = static_cast<uint8_t>((b & 0x0f) | _command);
		}

		void setSwitch(VoiceIo& _io, const int _k, const bool _on)
		{
			if(_on)
				_io.switches |= 1u << _k;
			else
				_io.switches &= ~(1u << _k);
		}

		int32_t zext16(const int32_t _v) { return static_cast<int32_t>(static_cast<uint32_t>(_v) & 0xffff); }
		int32_t clampCv(const int32_t _v) { return std::clamp(_v, 0, 0x7fff); }

		// An oscillator's waveform, three switches of the voice: code 1-6 picks one of six settings, any other the
		// seventh
		void setWave(VoiceIo& _io, const int32_t _code, const int _a, const int _b, const int _c)
		{
			bool a = false, b = true, c = true;
			switch(_code)
			{
			case 1: c = false; break;
			case 2: a = true; break;
			case 3: b = false; break;
			case 4: b = false; c = false; break;
			case 5: a = true; b = false; break;
			case 6: a = true; c = false; break;
			default: break;
			}
			setSwitch(_io, _a, a);
			setSwitch(_io, _b, b);
			setSwitch(_io, _c, c);
		}

		// A waveform code's level, negative in Q31: the square and the saw full scale, the others 1 or 2.5 dB down
		int32_t waveGain(const int32_t _code)
		{
			switch(_code)
			{
			case 2:
			case 5: return static_cast<int32_t>(0xa0037770);	// -2.5 dB
			case 3:
			case 6: return static_cast<int32_t>(0x8deb7d41);	// -1 dB
			default: return INT32_MIN;
			}
		}

		// The track's level by its byte: 0 to 18 dB in 127 steps, negative in Q31 / 8. A byte past 127 reads the table
		// before it, a time scale: 32 steps of 1, then octaves of 16 steps, in units of 168750, its last 0
		int32_t levelScale(const int _k)
		{
			static const std::array<int32_t, 128> t = []
			{
				std::array<int32_t, 128> r{};
				for(int k = 0; k < 128; ++k)
				{
					const double gain = std::pow(10.0, 0.9 * k / 127);
					r[static_cast<size_t>(k)] = static_cast<int32_t>(std::llround(-std::ldexp(gain, 28)));
				}
				return r;
			}();
			if(_k >= 0)
				return t[static_cast<size_t>(_k)];
			const int i = _k + 128;
			if(i == 127)
				return 0;
			if(i < 31)
				return (i + 2) * 168750;
			const int oct = (i - 31) / 16;
			return ((32 << oct) + (2 << oct) * ((i - 31) % 16 + 1)) * 168750;
		}

		// The voice's pitch CV of a tune word and the note: the exponential of the pitch word, 15 bits
		int32_t pitchCv(Emac& _e, const int32_t _tune, const int32_t _note)
		{
			const auto x = static_cast<int32_t>(expPitch(_e, pitchWord(_e, _tune, _note)));
			_e.mac(0, 0x82cf5, add(x, 1));
			return clampCv(_e.storeClear(0));
		}

		// What the synths' level CVs start from: 8191, 13 bits of full scale
		int32_t levelBase(Emac& _e)
		{
			_e.mac(0, 0x8206, 0x1f802e29);
			return _e.storeClear(0);
		}

		// -(a b) as msac leaves it in a cleared accumulator
		int32_t negMul(Emac& _e, const int32_t _a, const int32_t _b)
		{
			_e.msac(0, _a, _b);
			return _e.storeClear(0);
		}

		// The envelope's two halves at the start of a decay: the decay's level 0x40007f80, its rate code below the
		// command 0xf01, and 0x7f in the level's low byte for a knob at 127 (a hold), set before the rate (_holdBytes)
		// or after
		void startDecay(VoiceIo& _io, const uint32_t _rateA, const bool _holdA, const uint32_t _rateB,
			const bool _holdB, const bool _holdBytes)
		{
			setFrame32(_io, frame::Envelope + 0x10, 0x40007f80);
			if(_holdBytes)
				_io.frame[frame::Envelope + 0x13] = 0x7f;
			setFrame32(_io, frame::Envelope + 0x14, (_rateA & 0xfffff) | 0xf0100000);
			if(_holdA)
				_io.frame[frame::Envelope + 0x13] = 0x7f;
			setFrame32(_io, frame::Envelope + 0x18, 0x40007f80);
			setFrame32(_io, frame::Envelope + 0x1c, (_rateB & 0xfffff) | 0xf0100000);
			if(_holdB)
				_io.frame[frame::Envelope + 0x1b] = 0x7f;
		}

		// The dual and raw synths' decay rate by knob value (a negative one reads what lies before the table)
		uint32_t synthRate(const int _k) { return _k >= 0 ? rates::synthEnvelope(_k) : rates::belowSynthEnvelope(_k); }

		// The ramp the voice is sent: from _from, falling to _from * _factor over the block, a straight line of 32
		// longs
		int32_t sendRamp(VoiceIo& _io, Emac& _e, const int32_t _from, const int32_t _factor)
		{
			_e.mac(0, _factor, _from);
			const int32_t step = asr(sub(_e.storeClear(0), _from), 5);
			int32_t v = _from;
			for(int i = 0; i < 32; ++i, v = add(v, step))
				setBe32(_io.outA.data() + 4 * i, static_cast<uint32_t>(v));
			return add(_from, lsl(step, 5));
		}
	}

	void synthDual(VoiceIo& _io)
	{
		Emac e(_io.saturate);
		// the mode knob, 0-79: its lowest bit the attack's click, then five of the second oscillator's waves for each
		// of eight settings of the first's, the sub switch and the level CVs' mode
		int mode = std::clamp(knobValue(_io, 7), 0, 79);
		const bool click = (mode & 1) == 0;
		mode >>= 1;
		const int setting = mode / 5;
		const int32_t waveA = (setting & 1) ? 3 : 0;
		const int32_t waveB = std::array<int32_t, 5>{0, 1, 2, 3, 5}[static_cast<size_t>(mode - 5 * setting)];
		const bool noSub = (setting >> 1 & 1) != 0;
		const bool fixedLevel = (setting >> 2) != 0;

		setWave(_io, waveA, 6, 1, 5);
		setWave(_io, waveB, 8, 2, 7);
		setSwitch(_io, 4, !noSub);
		const int32_t step = stateLong(_io, 0);
		setSwitch(_io, 3, step == 3 || step < 0 || fixedLevel);
		setSwitch(_io, 9, true);

		// the pitches: each oscillator's tune, the first swept down from its trig by the seventh word, the second
		// detuned from it
		int32_t sweep = stateLong(_io, 2);
		int32_t ramp = stateLong(_io, 3);
		int32_t cvA = 0, cvB = 0;
		if(step >= 0)
		{
			if(step == 0)
				sweep = lsl(word(_io, 6) - 0x4000, 16);
			const int32_t note = std::min(_io.pitch, 0x540000);
			const int32_t tuneA = add(asr(sweep, 17), word(_io, 1));
			const int32_t tuneB = add(word(_io, 3) - 0x4000, tuneA);
			cvA = pitchCv(e, tuneA, note);
			cvB = pitchCv(e, tuneB, note);
			e.mac(0, 0x7eb851ec, sweep);	// 0.99 a block
			sweep = e.storeClear(0);
		}
		setFrame16(_io, frame::Cv, cvA);
		setFrame16(_io, frame::Cv + 2, cvB);
		// the unit's trims of the two levels: zero, untrimmed
		setFrame32(_io, frame::Level, 0);
		setFrame32(_io, frame::Level + 4, 0);

		// the two oscillators' levels: the sixth word as a balance, or in the fixed mode its square on one of them
		int32_t levelA = levelBase(e);
		int32_t levelB;
		if(fixedLevel)
		{
			const int32_t w = word(_io, 5);
			e.mac(0, 0x1040c, w * w);
			const int32_t sq = e.storeClear(0);
			e.mac(0, 0x27904a79, sq);
			levelB = e.storeClear(0);
		}
		else
		{
			int32_t other;
			const int32_t k = knob(e, word(_io, 5), other);
			e.msac(0, k, levelA);
			e.msac(1, other, levelA);
			levelA = e.storeClear(0);
			levelB = e.storeClear(1);
		}
		setFrame16(_io, frame::Cv + 4, levelA);
		setFrame16(_io, frame::Cv + 6, levelB);
		const int32_t track = levelScale(knobValue(_io, 0));
		levelA = negMul(e, zext16(levelA), waveGain(waveA));
		setFrame16(_io, frame::Cv + 4, levelA);
		if(!fixedLevel)
		{
			levelB = negMul(e, zext16(levelB), waveGain(waveB));
			setFrame16(_io, frame::Cv + 6, levelB);
		}
		levelA = negMul(e, lsl(zext16(levelA), 3), track);
		setFrame16(_io, frame::Cv + 4, levelA);
		if(!fixedLevel)
		{
			levelB = negMul(e, lsl(zext16(levelB), 3), track);
			setFrame16(_io, frame::Cv + 6, levelB);
		}

		// the envelope
		bool settled = true;
		switch(step)
		{
		case 0:
			setSwitch(_io, 0, click);
			setFrame16(_io, frame::Envelope, 0);
			setFrame16(_io, frame::Envelope + 8, 0);
			setCommand(_io, 4, 0x90);
			setCommand(_io, 12, 0x90);
			setStateLong(_io, 1, 16);
			setStateLong(_io, 0, 1);
			break;
		case 1:
		{
			setSwitch(_io, 0, false);
			// the click's ramp: the seventh knob above 64, squared
			ramp = 0;
			if(click)
			{
				const int k = std::clamp(knobValue(_io, 6) - 64, 0, 16);
				const int32_t r = negMul(e, 0x59cd, -k * (1 << 27));
				e.macWord(0, r, false, r, false);
				ramp = asr(e.storeClear(0), 4);
			}
			const int kA = knobValue(_io, 2), kB = knobValue(_io, 4);
			startDecay(_io, synthRate(kA), kA == 127, synthRate(kB), kB == 127, false);
			setFrame32(_io, frame::Envelope + 4, 0xf0100000);
			setFrame32(_io, frame::Envelope + 12, 0xf0100000);
			setFrame32(_io, frame::Envelope, 0x40000000);
			setFrame32(_io, frame::Envelope + 8, 0x40000000);
			const int32_t wA = word(_io, 2), wB = word(_io, 4);
			if(asr(wA, 8) == 127 || asr(wB, 8) == 127)
				setStateLong(_io, 1, 16);
			else
			{
				// the countdown to the longer decay's end
				const int32_t longer = wA > wB ? wA : wB;
				const auto rate = static_cast<int32_t>(synthRate(asr(longer, 8)));
				setStateLong(_io, 1, add(stateLong(_io, 1), decayTicksDefault(e, rate)));
				settled = false;
			}
			setStateLong(_io, 0, 2);
			break;
		}
		case 2:
		case 3:
			break;
		default:
			setStateLong(_io, 0, -1);
			break;
		}

		ramp = sendRamp(_io, e, ramp, 0x9eb8a5);
		setStateLong(_io, 2, sweep);
		setStateLong(_io, 3, ramp);
		setStateLong(_io, 4, settled ? 1 : 0);
		std::fill(_io.outB.begin(), _io.outB.end(), uint8_t(0));
	}

	namespace
	{
		// The raw synth's levels by its eighth knob: a linear ramp to 0.7 of full scale, and -3 to -6 dB in 127 steps
		// (negative in Q31). Below each, the table before: the -6 to 0 dB one, then the rates and the small tables
		int32_t rawLevelA(const int _k)
		{
			static const std::array<int32_t, 128> t = []
			{
				std::array<int32_t, 128> r{};
				for(int k = 0; k < 128; ++k)
				{
					const double gain = std::pow(10.0, -(3 + 3.0 * k / 127) / 20);
					r[static_cast<size_t>(k)] = static_cast<int32_t>(std::llround(-std::ldexp(gain, 31)));
				}
				return r;
			}();
			return _k >= 0 ? t[static_cast<size_t>(_k)] : rates::belowRawLevel(_k);
		}
		// -6 to 0 dB in 127 steps: the output's level by the track's level byte
		int32_t rawLevelB(const int _k)
		{
			static const std::array<int32_t, 128> t = []
			{
				std::array<int32_t, 128> r{};
				for(int k = 0; k < 128; ++k)
				{
					const double gain = std::pow(10.0, -6 * (127 - k) / 127.0 / 20);
					r[static_cast<size_t>(k)] = static_cast<int32_t>(std::llround(-std::ldexp(gain, 31)));
				}
				return r;
			}();
			return _k >= 0 ? t[static_cast<size_t>(_k)] : rawLevelA(128 + _k);
		}
		int32_t rawRamp(const int _k)
		{
			return _k >= 0 ? static_cast<int32_t>(std::lround(_k * 0x59cd / 127.0)) : rawLevelB(128 + _k);
		}
		// The raw synth's decay rate: the dual's table; below it the ramp's
		uint32_t rawRate(const int _k)
		{
			return _k >= 0 ? rates::synthEnvelope(_k) : static_cast<uint32_t>(rawRamp(128 + _k));
		}
	}

	void synthRaw(VoiceIo& _io)
	{
		Emac e(_io.saturate);
		// the two oscillators' waves: the sixth knob 0-11 (its six waves twice), the seventh 0-5, or 6 for the sub off
		const int oscKnob = knobValue(_io, 5);
		static constexpr int32_t Waves[] = {0, 1, 2, 3, 5, 6};
		const int32_t waveA = Waves[std::clamp(oscKnob, 0, 11) % 6];
		const int subKnob = knobValue(_io, 6);
		const bool noSub = subKnob == 6;
		const int32_t waveB = Waves[noSub ? 0 : std::clamp(subKnob, 0, 5)];

		setWave(_io, waveA, 6, 1, 5);
		setWave(_io, waveB, 8, 2, 7);
		setSwitch(_io, 4, !noSub);
		setSwitch(_io, 3, false);
		setSwitch(_io, 9, true);

		const int32_t step = stateLong(_io, 0);
		int32_t cvA = 0, cvB = 0;
		if(step >= 0)
		{
			const int32_t tuneB = add(word(_io, 3) - 0x4000, word(_io, 1));
			cvA = pitchCv(e, word(_io, 1), _io.pitch);
			cvB = pitchCv(e, tuneB, _io.pitch);
		}
		setFrame16(_io, frame::Cv, cvA);
		setFrame16(_io, frame::Cv + 2, cvB);
		setFrame32(_io, frame::Level, 0);
		setFrame32(_io, frame::Level + 4, 0);

		// the levels: the first oscillator's by the eighth knob's level, the second's by the square of its ramp
		const int balance = knobValue(_io, 7);
		const int32_t base = levelBase(e);
		int32_t levelA = negMul(e, base, rawLevelA(balance));
		setFrame16(_io, frame::Cv + 4, levelA);
		const int32_t r = rawRamp(balance);
		e.mac(0, 0x8206, static_cast<int32_t>(static_cast<uint32_t>(static_cast<int64_t>(r) * r)));
		int32_t levelB = zext16(negMul(e, e.storeClear(0), rawLevelA(balance)));
		levelA = negMul(e, zext16(levelA), waveGain(waveA));
		setFrame16(_io, frame::Cv + 4, levelA);
		levelB = negMul(e, levelB, waveGain(waveB));
		setFrame16(_io, frame::Cv + 6, levelB);
		const int32_t track = levelScale(knobValue(_io, 0));
		levelA = negMul(e, lsl(zext16(levelA), 3), track);
		setFrame16(_io, frame::Cv + 4, levelA);
		levelB = negMul(e, lsl(zext16(levelB), 3), track);
		setFrame16(_io, frame::Cv + 6, levelB);

		bool settled = true;
		switch(step)
		{
		case 0:
			setSwitch(_io, 0, oscKnob > 5);
			setStateLong(_io, 1, 16);
			setStateLong(_io, 0, 1);
			break;
		case 1:
		{
			setSwitch(_io, 0, false);
			const int k = knobValue(_io, 2);
			startDecay(_io, rawRate(k), false, rawRate(k), k == 127, true);
			if(k == 127)
				setStateLong(_io, 1, 16);
			else
			{
				setStateLong(_io, 1, add(stateLong(_io, 1), decayTicksDefault(e, static_cast<int32_t>(rawRate(k)))));
				settled = false;
			}
			setStateLong(_io, 0, 2);
			break;
		}
		case 2:
		case 3:
			break;
		default:
			setStateLong(_io, 0, -1);
			break;
		}
		setStateLong(_io, 4, settled ? 1 : 0);

		// the noise's settings and its level, the fifth word squared at the track's level
		const int32_t w = word(_io, 4);
		const int32_t sq = add(w * w, w * w);
		setBe32(_io.heads.data(), static_cast<uint32_t>(negMul(e, sq, rawLevelB(knobValue(_io, 0)))));
		_io.noise = {0x02, 0x02, 0x44, 0x00};
	}

	namespace
	{
		// The chip synth's programs, by its seventh knob: up to four steps, each a sound (chipSounds), played in turn
		// at the step length; flags: 1 the first steps free-running on noise, 2 the steps loop, 4 the pulse's width
		// swept (8: from a narrow one)
		struct ChipProgram
		{
			int32_t count;
			int32_t steps[4];
			int32_t flags;
		};
		constexpr ChipProgram ChipPrograms[30] = {
			{4, {0, 0, 0, 0}, 0}, {4, {1, 1, 1, 1}, 0}, {4, {2, 2, 2, 2}, 0}, {4, {3, 3, 3, 3}, 0},
			{4, {4, 4, 4, 4}, 0}, {4, {5, 5, 5, 5}, 0}, {4, {10, 10, 10, 10}, 0}, {4, {6, 6, 6, 6}, 0},
			{4, {7, 7, 7, 7}, 0}, {4, {8, 8, 8, 8}, 0}, {4, {9, 9, 9, 9}, 0}, {3, {2, 4, 5, 0}, 2},
			{4, {11, 11, 11, 11}, 4}, {4, {11, 11, 11, 11}, 12}, {4, {2, 2, 2, 2}, 1}, {4, {10, 2, 2, 2}, 0},
			{4, {2, 10, 2, 2}, 0}, {4, {2, 10, 10, 2}, 0}, {4, {4, 4, 4, 4}, 1}, {4, {10, 4, 4, 4}, 0},
			{4, {4, 10, 4, 4}, 0}, {4, {4, 10, 10, 4}, 0}, {4, {5, 5, 5, 5}, 1}, {4, {10, 5, 5, 5}, 0},
			{4, {5, 10, 5, 5}, 0}, {4, {5, 10, 10, 5}, 0}, {4, {5, 5, 10, 10}, 1}, {4, {2, 5, 10, 10}, 1},
			{4, {5, 5, 2, 2}, 1}, {4, {11, 11, 11, 11}, 0}};

		// A step's sound: the two oscillators' waves, the level mode (0: the second alone, 1: the first, 2: both at a
		// fixed level), or a digital wave the CPU renders itself (1 noise, 2 pulse)
		struct ChipSound
		{
			int32_t waveA, waveB, mode, digital;
		};
		constexpr ChipSound ChipSounds[12] = {{0, 0, 0, 0}, {0, 1, 0, 0}, {0, 2, 0, 0}, {0, 3, 0, 0}, {0, 5, 0, 0},
			{0, 0, 2, 0}, {0, 3, 1, 0}, {3, 3, 1, 0}, {0, 0, 1, 0}, {3, 0, 1, 0}, {0, 0, 0, 1}, {0, 0, 0, 2}};

		// The pitch steps of the arpeggio: the fourth to sixth words, the offset of the steps after the first
		int32_t stepOffset(const VoiceIo& _io, const int32_t _n)
		{
			if(_n < 1 || _n > 3)
				return 0;
			return lsl(word(_io, _n + 2) - 0x4000, 8);
		}

		// The free-running step lengths by the eighth knob, 24-65: periods of these rates in Hz, in the timer's units
		// (28800 a block); 0-23 are the tempo-synced ones: 4, 6 and 9 times a power of two, sorted, in units of 56250
		int32_t stepLength(const int _k)
		{
			static constexpr double Rates[42] = {1, 1.5625, 1.875, 2, 3.125, 3.75, 4, 5, 6.25, 7.5, 10, 12.5, 15, 20,
				25, 30, 40, 50, 60, 75, 100, 120, 150, 180, 200, 240, 250, 300, 350, 360, 400, 420, 480, 240, 200, 150,
				120, 100, 60, 50, 30, 25};
			static const std::array<int32_t, 66> t = []
			{
				std::array<int32_t, 66> r{};
				size_t at = 0;
				for(int n = 0; n < 8; ++n)
					for(const int f : {4, 6, 9})
						r[at++] = (f << n) * 56250;
				std::sort(r.begin(), r.begin() + 24);
				for(int i = 0; i < 42; ++i)
					r[static_cast<size_t>(24 + i)] = static_cast<int32_t>(43200000 / Rates[i]);
				return r;
			}();
			return t[static_cast<size_t>(_k)];
		}

		// The digital waves' pitch, by a pitch index of 1/8 semitone from C0: the phase step of a 24-bit phase a sample
		// at 48 kHz, and the pulse's slope scale, 40000 / f
		double chipFrequency(const int _i) { return 440 * std::pow(2.0, (_i / 8.0 - 57) / 12); }
		int32_t phaseStep(const int _i)
		{
			static const std::array<int32_t, 768> t = []
			{
				std::array<int32_t, 768> r{};
				for(int i = 0; i < 768; ++i)
				{
					const double step = std::ldexp(chipFrequency(i), 24) / 48000;
					r[static_cast<size_t>(i)] = static_cast<int32_t>(std::lround(step));
				}
				return r;
			}();
			return t[static_cast<size_t>(_i)];
		}
		int32_t slopeScale(const int _i) { return static_cast<int32_t>(std::lround(40000 / chipFrequency(_i))); }

		// The chip's decay per sample, 30 s at the top in whole microseconds, 127 holding. Below it, the step lengths,
		// then the phase steps' top
		int32_t chipDecay(const int _k)
		{
			static const std::array<int32_t, 128> t = []
			{
				std::array<int32_t, 128> r{};
				for(int k = 1; k < 127; ++k)
				{
					const double time = std::round(30e6 * (std::pow(257.0, k / 127.0) - 1) / 256) / 1e6;
					const double c = std::exp(-std::log(1000.0) / (time * 48000));
					r[static_cast<size_t>(k)] = static_cast<int32_t>(std::ldexp(c, 31));
				}
				r[127] = INT32_MAX;
				return r;
			}();
			if(_k >= 0)
				return t[static_cast<size_t>(_k)];
			return _k >= -66 ? stepLength(66 + _k) : phaseStep(834 + _k);
		}
		// The chip's envelope rate: its own table; below it the decay's
		uint32_t chipRate(const int _k)
		{
			return _k >= 0 ? rates::chipEnvelope(_k) : static_cast<uint32_t>(chipDecay(128 + _k));
		}

		// The noise's output bits: eight taps of the shift register, reversed into the top byte
		int32_t noiseBits(const uint32_t _r)
		{
			return static_cast<int32_t>((_r & 0x100000) << 10 | (_r & 0x400000) << 9 | (_r & 0x10000) << 13 |
				(_r & 0x2000) << 15 | (_r & 0x800) << 16 | (_r & 0x80) << 19 | (_r & 0x10) << 21 | (_r & 0x4) << 22);
		}

		// The free-running timer's step a block (the synced one is the tempo's, VoiceIo::tempo)
		constexpr int32_t FreeTick = 28800;
	}

	void synthChip(VoiceIo& _io)
	{
		Emac e(_io.saturate);
		setSwitch(_io, 9, true);
		const int programKnob = std::clamp(knobValue(_io, 6), 0, 29);
		const ChipProgram& program = ChipPrograms[programKnob];

		// the program's place: the pitch step (bits 0-1), the program step (2-3) and the steps played (4 on)
		int32_t step = stateLong(_io, 0);
		int32_t pitchStep = 0, programStep = 0, played = 0;
		if(step != 0)
		{
			const int32_t place = stateLong(_io, 2);
			pitchStep = place & 3;
			played = asr(place, 4);
			programStep = std::min(place >> 2 & 3, program.count - 1);
		}
		const int lengthKnob = std::clamp(knobValue(_io, 7), 0, 65);

		uint32_t length;
		if(step <= 2 && (program.flags & 1))
		{
			setStateLong(_io, 5, add(stateLong(_io, 5), FreeTick));
			length = 0x46500;		// 10 blocks
		}
		else
		{
			length = static_cast<uint32_t>(stepLength(lengthKnob));
			setStateLong(_io, 5, add(stateLong(_io, 5), lengthKnob <= 23 ? _io.tempo : FreeTick));
		}

		switch(step)
		{
		case 0:
			setSwitch(_io, 3, false);
			setSwitch(_io, 0, false);
			setStateLong(_io, 0, 1);
			setStateLong(_io, 6, stateLong(_io, 6) ^ 0x5a5a5a);
			setStateLong(_io, 7, stateLong(_io, 7) & 0xffffff);
			setStateLong(_io, 1, 16);
			setFrame16(_io, frame::Envelope, 0);
			setCommand(_io, 4, 0x90);
			setFrame16(_io, frame::Envelope + 8, 0);
			setCommand(_io, 12, 0x90);
			break;
		case 1:
		{
			if(stateLong(_io, 1) > 15)
				break;
			const int k = knobValue(_io, 2);
			startDecay(_io, chipRate(k), false, chipRate(k), false, false);
			if(k == 127)
			{
				_io.frame[frame::Envelope + 0x13] = 0x7f;
				_io.frame[frame::Envelope + 0x1b] = 0x7f;
			}
			setStateLong(_io, 5, 0);
			setStateLong(_io, 3, INT32_MAX);
			setStateLong(_io, 0, (program.flags & 1) ? 2 : 3);
			e.mac(0, static_cast<int32_t>(0xc0ffa3ae), 0x19999999);
			setStateLong(_io, 4, e.storeClear(0));
			break;
		}
		case 2:
		case 3:
		{
			const auto timer = static_cast<uint32_t>(stateLong(_io, 5));
			if(timer < length)
				break;
			setStateLong(_io, 5, static_cast<int32_t>(timer - length));
			if(step == 2)
			{
				setStateLong(_io, 0, 3);
				break;
			}
			// the next pitch step, back to the first past the last set (or past the third where the length is long)
			const int32_t next = pitchStep + 1;
			if(stepOffset(_io, next) == 0)
				pitchStep = lengthKnob > 56 ? pitchStep : 0;
			else
				pitchStep = next;
			++played;
			if(++programStep >= program.count)
				programStep = (program.flags & 2) ? 0 : program.count - 1;
			break;
		}
		default:
			setStateLong(_io, 0, -1);
			break;
		}

		const ChipSound& sound = ChipSounds[program.steps[programStep]];
		step = stateLong(_io, 0);
		const bool freeNoise = step <= 2 && (program.flags & 1);
		uint8_t* const out = _io.outA.data();
		if(!freeNoise && sound.digital == 0)
		{
			// an analog sound: the two oscillators, their waves, levels and pitch
			const int32_t offset = stepOffset(_io, pitchStep);
			std::fill_n(_io.outA.begin(), 128, uint8_t(0));
			const int32_t base = zext16(levelBase(e));
			const int32_t track = levelScale(knobValue(_io, 0));
			if(sound.mode == 0)
			{
				setFrame16(_io, frame::Cv + 4, 0);
				// the second alone: -4.3 dB for waves 2 and 5
				const int32_t g = (sound.waveB == 2 || sound.waveB == 5) ? static_cast<int32_t>(0xb23d70a4) : INT32_MIN;
				int32_t l = negMul(e, base, g);
				setFrame16(_io, frame::Cv + 6, l);
				l = negMul(e, lsl(zext16(l), 3), track);
				setFrame16(_io, frame::Cv + 6, l);
			}
			else
			{
				// the first: -1.16 dB, or -1 dB for wave 3
				const auto g = static_cast<int32_t>(sound.waveA == 3 ? 0x8deb7d41u : 0x90000000u);
				int32_t l = negMul(e, base, g);
				setFrame16(_io, frame::Cv + 4, l);
				setFrame16(_io, frame::Cv + 6, 0);
				l = negMul(e, lsl(zext16(l), 3), track);
				setFrame16(_io, frame::Cv + 4, l);
			}
			setWave(_io, sound.waveA, 6, 1, 5);
			setWave(_io, sound.waveB, 8, 2, 7);
			setSwitch(_io, 4, sound.mode == 0);
			const int32_t cv = pitchCv(e, word(_io, 1), add(_io.pitch, offset));
			setFrame16(_io, frame::Cv, cv);
			setFrame16(_io, frame::Cv + 2, cv);
			if(sound.mode == 2)
			{
				setFrame16(_io, frame::Level, 0x7530);
				setFrame16(_io, frame::Level + 2, 0x7530);
			}
			else
				setFrame32(_io, frame::Level, 0);
			setFrame32(_io, frame::Level + 4, 0);
		}
		else
		{
			// a digital wave the CPU renders into the voice's block: noise or a pulse
			const int32_t digital = freeNoise ? 1 : sound.digital;
			const int32_t offset = freeNoise ? 0 : stepOffset(_io, pitchStep);
			setFrame16(_io, frame::Cv + 4, 0);
			setFrame16(_io, frame::Cv + 6, 0);
			if(step > 0)
			{
				uint32_t width = 0x800000;
				if(digital == 2)
				{
					if(program.flags & 4)
					{
						// the width swept with the steps played, up and down
						const int32_t from = (program.flags & 8) ? 0x18 : 0x80;
						const int32_t at = played & 0xff;
						const int32_t w = (add(played, from) & 0x100) ? 0x1ff - from - at : from + at;
						width = static_cast<uint32_t>(w & 0xff) << 16;
					}
					else if(programKnob == 29)
						width = static_cast<uint32_t>((word(_io, 6) - 0x1c00) * 0x28f);
				}
				const int32_t tune = freeNoise ? 0x4000 : word(_io, 1);
				const int32_t note = add(freeNoise ? 0x5a0000 : _io.pitch, offset);
				const int32_t index = asr(add(note, lsl(tune - 0x4000, 8)), 13) - 0xc0;
				auto phase = static_cast<uint32_t>(stateLong(_io, 7));
				if(digital == 1)
				{
					// noise: a shift register clocked at the phase's bit 19, sampled every other sample
					auto reg = static_cast<uint32_t>(stateLong(_io, 6));
					int32_t bits = noiseBits(reg);
					const auto inc = static_cast<uint32_t>(phaseStep(std::clamp(index, 0, 0x2d2))) >> 1;
					uint8_t* p = out;
					for(int i = 0; i < 64; ++i)
					{
						const uint32_t next = inc + phase;
						if((next & 0x80000) && !(phase & 0x80000))
						{
							reg = reg << 1 | ((reg >> 17 ^ reg >> 22) & 1);
							bits = noiseBits(reg);
						}
						if(i & 1)
						{
							setBe32(p, static_cast<uint32_t>(bits) + 0x80000000u);
							p += 4;
						}
						phase = next & 0xffffff;
					}
					setStateLong(_io, 6, static_cast<int32_t>(reg));
				}
				else if(digital == 2)
				{
					// a pulse with sloped edges: its width bounded to a step of the phase from either end
					const int i = std::clamp(index, 0, 0x2ff);
					const int32_t scale = slopeScale(i);
					const auto inc = static_cast<uint32_t>(phaseStep(i));
					if(width <= 0x7fffff)
						width = std::max(width, inc);
					else
						width = 0x1000000 - std::max(0x1000000 - width, inc);
					// muls.l: the product's low 32 bits
					const auto times = [scale](const uint32_t _x)
					{
						return static_cast<int32_t>(_x * static_cast<uint32_t>(scale));
					};
					const uint32_t w = width >> 5;
					const auto top =
						static_cast<int32_t>(std::min(static_cast<uint32_t>(times(0x7ffff - w)), 0x7ffffu));
					const auto rise = static_cast<int32_t>(std::min(static_cast<uint32_t>(times(w)), 0x7ffffu));
					for(int k = 0; k < 32; ++k)
					{
						phase = (phase + inc) & 0xffffff;
						const uint32_t p = phase >> 5;
						int32_t v;
						if(w > p)
						{
							v = add(times(p - w), top);
							v = v >= 0 ? v : 0;
						}
						else
						{
							const int32_t d = sub(times(0x7ffff - p), rise);
							v = d >= 0 ? 0x7ffff : (d & 0x7ffff);
						}
						setBe32(out + 4 * k, static_cast<uint32_t>(lsl(v, 13)) + 0x80000000u);
					}
				}
				setStateLong(_io, 7, static_cast<int32_t>(phase));
			}
		}

		if(stateLong(_io, 0) > 0)
		{
			// the amplitude: a decay a sample, times a level that ramps over the block to its target
			const int32_t decay = chipDecay(knobValue(_io, 2));
			e.mac(0, static_cast<int32_t>(0xc0ffa3ae), 0x19999999);
			const int32_t target = e.storeClear(0);
			int32_t gain = stateLong(_io, 4);
			const int32_t diff = sub(target, gain);
			const int32_t gainStep = asr(sats(diff, ((target ^ gain) & (target ^ diff) & 0x80000000) != 0), 5);
			int32_t env = stateLong(_io, 3);
			e.mac(2, decay, env);
			auto in = static_cast<int32_t>(be32(out));
			for(int k = 0; k < 16; ++k)
			{
				uint8_t* p = out + 8 * k;
				e.mac(0, env, in);
				in = static_cast<int32_t>(be32(p + 4));
				env = e.storeClear(2);
				e.mac(3, decay, env);
				e.mac(1, env, in);
				in = static_cast<int32_t>(be32(p + 8));
				int32_t v = e.storeClear(0);
				e.msac(0, gain, v);
				gain = add(gain, gainStep);
				env = e.storeClear(3);
				e.mac(2, decay, env);
				v = e.storeClear(1);
				e.msac(1, gain, v);
				gain = add(gain, gainStep);
				setBe32(p, static_cast<uint32_t>(e.storeClear(0)));
				setBe32(p + 4, static_cast<uint32_t>(e.storeClear(1)));
			}
			e.storeClear(2);
			setStateLong(_io, 3, env);
			setStateLong(_io, 4, target);
		}
		setBe32(_io.heads.data(), 0);
		setBe32(_io.heads.data() + 4, 0);
		setStateLong(_io, 2, pitchStep + 4 * programStep + 16 * played);
	}
}
