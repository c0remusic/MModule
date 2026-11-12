#include "edDrumsAnalogPort.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>

// The kicks' control routines (tracks 9-11): each block, the machine's parameters into the voice's pitch CVs, its
// switches, the envelope generator's commands and the short click the voice is sent at a trig
namespace elektron::drums::analog::port
{
	namespace
	{
		// The voice's memory by name, and the instructions the routines repeat
		struct Kick
		{
			VoiceIo& io;
			Emac e;

			explicit Kick(VoiceIo& _io) : io(_io), e(_io.saturate) {}

			int16_t word(const int _i) const { return static_cast<int16_t>(be16(io.params.data() + 2 * _i)); }
			int8_t byte(const int _i) const { return static_cast<int8_t>(io.params[static_cast<size_t>(_i)]); }

			int32_t state(const int _i) const { return static_cast<int32_t>(be32(io.state.data() + 4 * _i)); }
			void setState(const int _i, const int32_t _v) { setBe32(io.state.data() + 4 * _i, static_cast<uint32_t>(_v)); }

			uint8_t* cv() { return io.frame.data() + frame::Cv; }
			uint8_t* env() { return io.frame.data() + frame::Envelope; }
			uint8_t* level() { return io.frame.data() + frame::Level; }

			// An envelope byte's command in its top nibble, its low nibble kept
			void command(const int _at, const uint8_t _cmd) { env()[_at] = static_cast<uint8_t>((env()[_at] & 0x0f) | _cmd); }
			// An envelope long's low 20 bits (a rate or a level), its top 12 kept
			void field(const int _at, const int32_t _v)
			{
				setBe32(env() + _at, (be32(env() + _at) & 0xfff00000u) | (static_cast<uint32_t>(_v) & 0xfffffu));
			}

			void on(const int _k) { io.switches |= 1u << _k; }
			void off(const int _k) { io.switches &= ~(1u << _k); }

			int32_t head(const int _i) const { return static_cast<int32_t>(be32(io.heads.data() + 4 * _i)); }
			void setHead(const int _i, const int32_t _v) { setBe32(io.heads.data() + 4 * _i, static_cast<uint32_t>(_v)); }

			int32_t mul(const int32_t _a, const int32_t _b)
			{
				e.mac(0, _a, _b);
				return e.storeClear(0);
			}
		};

		// add.l x,x then sats.l: twice _v, saturated where the add overflowed
		int32_t twiceSat(const int32_t _v)
		{
			const int32_t r = add(_v, _v);
			return sats(r, ((_v ^ r) & 0x80000000) != 0);
		}
		// four times, saturated on the second add's overflow only, as the CPU's flags leave it
		int32_t fourSat(const int32_t _v) { return twiceSat(add(_v, _v)); }

		int32_t square(const int32_t _v) { return static_cast<int32_t>(static_cast<uint32_t>(static_cast<int64_t>(_v) * _v)); }

		int clampMode(const int _v) { return _v < 0 ? 0 : _v > 2 ? 2 : _v; }
		int32_t clampCv(const int32_t _v) { return _v < 0 ? 0 : _v > 0x7fff ? 0x7fff : _v; }

		// The accent level by the track's level byte: 0 to 18 dB in 127 steps, negative in Q31 / 8. A byte past 127 reads
		// the table before it, a time scale: 32 steps of 1, then octaves of 16 steps, in units of 168750, its last 0
		int32_t accentScale(const int _index)
		{
			static const std::array<int32_t, 128> t = []
			{
				std::array<int32_t, 128> r{};
				for(int k = 0; k < 128; ++k)
					r[static_cast<size_t>(k)] = static_cast<int32_t>(std::llround(-std::ldexp(std::pow(10.0, 0.9 * k / 127), 28)));
				return r;
			}();
			if(_index >= 0)
				return t[static_cast<size_t>(_index)];
			const int i = _index + 128;
			if(i == 127)
				return 0;
			if(i < 31)
				return (i + 2) * 168750;
			const int oct = (i - 31) / 16;
			return ((32 << oct) + (2 << oct) * ((i - 31) % 16 + 1)) * 168750;
		}

		// The noise generator's settings for the kicks' click: its four bytes
		void noiseSettings(VoiceIo& _io, const uint8_t _a, const uint8_t _b, const uint8_t _c, const uint8_t _d)
		{
			_io.noise = {_a, _b, _c, _d};
		}

		// The switches Hard and Classic set every block: _mode (0-2) picks two of them
		void kickSwitches(Kick& _k, const int _mode)
		{
			_k.off(6);
			_k.on(1);
			_k.on(5);
			if(_mode == 1)
			{
				_k.off(8);
				_k.on(2);
				_k.off(7);
			}
			else
			{
				if(_mode == 2)
					_k.on(8);
				else
					_k.off(8);
				_k.on(2);
				_k.on(7);
			}
			_k.on(4);
			if(_k.state(0) >= 0)
				_k.off(3);
			_k.on(9);
		}

		// While the envelope runs: the two pitch CVs from the tune word and the note, and their trims (zero on an
		// untrimmed unit)
		void kickPitch(Kick& _k)
		{
			const auto cv = [&](const int32_t _scale)
			{
				const auto f = static_cast<int32_t>(expPitch(_k.e, pitchWord(_k.e, _k.word(1), _k.io.pitch)));
				return clampCv(_k.mul(_scale, add(f, 1)));
			};
			const int32_t a = cv(0x62000);
			const int32_t b = cv(0x4168f);
			setBe16(_k.cv(), static_cast<uint32_t>(a));
			setBe16(_k.cv() + 2, static_cast<uint32_t>(b));
			setBe32(_k.level(), 0);
			setBe32(_k.level() + 4, 0);
		}

		// The third CV 0, the fourth a fixed word scaled by the accent level of byte 0
		void kickAccent(Kick& _k)
		{
			setBe16(_k.cv() + 4, 0);
			const int32_t base = _k.mul(0x1040c, 0x0fc04000) & 0xffff;
			_k.e.msac(0, lsl(base, 3), accentScale(_k.byte(0)));
			setBe16(_k.cv() + 6, static_cast<uint32_t>(_k.e.storeClear(0)));
		}

		// Step 0 of the envelope: the trig switch closed, the noise level cleared, the envelope's segments restarted
		void kickTrig(Kick& _k)
		{
			_k.on(0);
			_k.setHead(1, 0);
			_k.setState(1, add(_k.state(1), 16));
			_k.setState(0, add(_k.state(0), 1));
			_k.command(4, 0x90);
			setBe16(_k.env(), 0);
			setBe16(_k.env() + 8, 0);
			_k.command(0xc, 0x90);
		}

		// The decay step: a rate from _word (the decay knob) into the envelope segment at _at, the stage's length in
		// blocks from it
		void kickDecay(Kick& _k, const int16_t _word, const int _at = 0x18)
		{
			const int32_t rate = twiceSat(_k.mul(q31(64, 127), lsl(add(asr(_word, 1), 0x3fff), 16)));
			setBe16(_k.env() + _at, 0x4000);
			_k.env()[_at + 3] = 0x80;
			_k.env()[_at + 2] = 0;
			_k.env()[_at + 4] = static_cast<uint8_t>(0xb0 | (_k.state(1) & 0xf));
			_k.command(_at + 5, 0x10);
			const int32_t r = asr(rate, 11) & 0xfffff;
			_k.field(_at + 4, r);
			const int32_t t = _k.state(1);
			const int32_t ticks = decayTicksDefault(_k.e, r);
			_k.setState(0, add(_k.state(0), 1));
			_k.setState(1, add(t, ticks));
		}

		// The click the voice is sent: its level, then half of it, then silence; the state keeps what is left (0
		// but for the rounding of a negative half)
		void clickPulse(Kick& _k, int32_t _click)
		{
			std::fill(_k.io.outA.begin(), _k.io.outA.begin() + 128, uint8_t(0));
			const int32_t half = asr(sub(0, _click), 1);
			setBe32(_k.io.outA.data(), static_cast<uint32_t>(_click));
			_click = add(_click, half);
			setBe32(_k.io.outA.data() + 4, static_cast<uint32_t>(_click));
			_k.setState(2, add(_click, half));
		}

		// The last step: switch 3 closed again, the envelope stopped
		void kickEnd(Kick& _k)
		{
			_k.on(3);
			_k.setState(0, -1);
		}
	}

	void kickHard(VoiceIo& _io)
	{
		Kick k(_io);

		const int32_t hold = asr(k.word(3), 3);
		const int32_t p5 = k.mul(0x6044d013, add(k.word(5), k.word(5)));
		const int32_t clickLevel = k.mul(k.mul(q31(4, 5), k.word(7)), 0x3f800000);
		const int32_t noiseLevel = k.mul(q31(4, 5), 0x3f80);
		const int32_t p4 = k.mul(0x4f9db22d, asr(k.word(4), 1));

		kickSwitches(k, clampMode(k.byte(12)));
		if(k.state(0) >= 0)
			kickPitch(k);
		kickAccent(k);

		int32_t click = k.state(2);
		const auto step = static_cast<uint32_t>(k.state(0));
		if(step > 4)
			k.setState(0, -1);
		else if(k.state(1) <= 15)
		{
			switch(step)
			{
			case 0:
				kickTrig(k);
				click = 0;
				break;
			case 1:
				k.off(0);
				k.env()[0x1a] = 0x7f;
				k.env()[0x1b] = 0x7f;
				setBe16(k.env() + 0x18, 0x4000);
				k.env()[0x1c] = 0xf0;
				k.env()[0xc] = 0xf0;
				setBe16(k.env() + 8, 0x4000);
				k.command(0x1d, 0x10);
				k.field(0x1c, hold);
				k.env()[0xa] = static_cast<uint8_t>(asr(add(p5, -0x4080), 8));
				k.env()[0xb] = static_cast<uint8_t>(asr(add(asr(k.word(4), 4), 0x71e), 8));
				k.command(0xd, 0x10);
				k.field(0xc, 1 << 18);
				k.setHead(1, fourSat(sub(0, square(noiseLevel))));
				k.setState(1, add(k.state(1), 0x80));
				click = fourSat(square(clickLevel));
				k.setState(0, add(k.state(0), 1));
				break;
			case 2:
				k.env()[0xb] = 0;
				setBe16(k.env() + 8, 0x4000);
				k.command(0xc, 0xb0);
				k.command(0xd, 0x10);
				k.field(0xc, lsl(add(p4, 0x3000), 5));
				k.setState(1, add(k.state(1), hold));
				k.setState(0, add(k.state(0), 1));
				break;
			case 3:
				kickDecay(k, k.word(2));
				break;
			case 4:
				kickEnd(k);
				break;
			}
		}

		k.setHead(0, k.mul(q31(3, 4), k.head(1)));
		clickPulse(k, click);
		noiseSettings(_io, 124, 124, 45, 32);
	}

	namespace
	{
		// Classic's sound byte (0-55): 0-5 a click alone at a level, 6-55 a noise burst in ten styles of five levels
		struct ClassicSound
		{
			int32_t click;
			int32_t noise;
			int style;
		};
		ClassicSound classicSound(const int _i)
		{
			if(_i < 6)
			{
				constexpr int tenths[] = {0, 1, 2, 4, 6, 8};
				return {q31(tenths[_i], 10), 0, 0};
			}
			const int style = (_i - 6) / 5;
			const int step = (_i - 6) % 5;
			// styles 0, 1, 5 and 6 start at 2/5 and reach full scale a step early
			const bool loud = style == 0 || style == 1 || style == 5 || style == 6;
			const int fifths = std::min(5, step + (loud ? 2 : 1));
			return {0, fifths == 5 ? INT32_MAX : q31(fifths, 5), style};
		}
		// How much of the noise level each style keeps from block to block
		int32_t classicNoiseDecay(const int _style)
		{
			constexpr int hundredths[] = {90, 81, 85, 87, 87, 98, 97, 97, 97, 97};
			return q31(hundredths[_style], 100);
		}
		// The noise generator's settings by style: five, the last five styles the first five's
		void classicNoise(VoiceIo& _io, const int _style)
		{
			switch(_style % 5)
			{
			case 0: noiseSettings(_io, 13, 19, 22, 43); break;
			case 1: noiseSettings(_io, 0, 0, 32, 0); break;
			case 2: noiseSettings(_io, 2, 31, 64, 83); break;
			case 3: noiseSettings(_io, 26, 34, 85, 0); break;
			default: noiseSettings(_io, 124, 124, 127, 0); break;
			}
		}
	}

	void kickClassic(VoiceIo& _io)
	{
		Kick k(_io);

		const int s = k.byte(14);
		const ClassicSound sound = classicSound(s < 0 ? 0 : s > 55 ? 55 : s);
		const int32_t clickLevel = k.mul(sound.click, 0x3f80);
		const int32_t noiseLevel = k.mul(sound.noise, 0x3f80);
		const int32_t w5 = static_cast<int16_t>(k.word(5)) * static_cast<int16_t>(k.word(5));
		const int32_t p5 = k.mul(0x8102, add(w5, w5));
		const int32_t p4 = k.mul(0x6fdf3b64, k.word(4));

		kickSwitches(k, clampMode(k.byte(12)));
		if(k.state(0) >= 0)
			kickPitch(k);
		kickAccent(k);

		int32_t click = k.state(2);
		const int32_t step = k.state(0);
		if(step < 0 || step > 3)
			k.setState(0, -1);
		else if(k.state(1) <= 15)
		{
			switch(step)
			{
			case 0:
				kickTrig(k);
				click = 0;
				break;
			case 1:
			{
				k.off(0);
				const int32_t hold = asr(k.word(3), 4);
				k.env()[0x1a] = 0x7f;
				k.env()[0x1b] = 0x7f;
				k.env()[0x1c] = 0xf0;
				setBe16(k.env() + 0x18, 0x4000);
				setBe16(k.env() + 8, 0x4000);
				k.env()[0xb] = 0;
				k.command(0x1d, 0x10);
				k.field(0x1c, hold);
				k.env()[0xa] = static_cast<uint8_t>(asr(p5, 8));
				k.env()[0xc] = 0xf0;
				k.command(0xd, 0x10);
				k.field(0xc, lsl(add(p4, 0x1000), 5));
				k.setState(1, add(k.state(1), hold));
				k.setState(0, add(k.state(0), 1));
				k.setHead(1, fourSat(sub(0, square(noiseLevel))));
				click = fourSat(square(clickLevel));
				break;
			}
			case 2:
				kickDecay(k, k.word(2));
				break;
			case 3:
				kickEnd(k);
				break;
			}
		}

		k.setHead(0, k.mul(classicNoiseDecay(sound.style), k.head(1)));
		// the click falls along the block to about a sixth
		const int32_t slope = asr(sub(k.mul(0x15032885, click), click), 5);
		int32_t c = click;
		for(int i = 0; i < 32; ++i)
		{
			setBe32(_io.outA.data() + 4 * i, static_cast<uint32_t>(c));
			c = add(c, slope);
		}
		k.setState(2, add(click, lsl(slope, 5)));
		classicNoise(_io, sound.style);
	}

	void kickFm(VoiceIo& _io)
	{
		Kick k(_io);
		Emac& e = k.e;

		const int32_t fmCv = k.mul(0x27904a79, k.mul(0x52a54a95, add(k.word(3), k.word(3))));
		setBe16(k.env(), 0);
		const int32_t p48 = k.mul(0x6044d013, 0xfffe);
		const int32_t clickLevel = k.mul(k.mul(q31(4, 5), 0x4000), 0x3f800000);
		const int32_t noiseBase = k.mul(q31(4, 5), 0x3f80);
		const int32_t p4 = k.mul(0x4f9db22d, asr(k.word(4), 1));
		const int32_t p5 = k.mul(0x4f9db22d, asr(k.word(5), 1));
		setBe16(k.env() + 0x10, 0);
		setBe16(k.env() + 0x18, 0);
		setBe16(k.env() + 8, 0);
		const int32_t base = k.mul(0x1040c, 0x0fc04000) & 0xffff;
		setBe16(k.cv() + 6, static_cast<uint32_t>(fmCv));
		e.msac(0, lsl(base, 3), accentScale(k.byte(0)));
		setBe16(k.cv() + 4, static_cast<uint32_t>(e.storeClear(0)));

		k.off(6);
		k.on(1);
		k.on(5);
		k.off(8);
		k.on(2);
		k.on(7);
		k.on(9);

		if(k.state(0) >= 0)
		{
			const int32_t carrier = clampCv(k.mul(0x4168f,
				add(static_cast<int32_t>(expPitch(e, pitchWord(e, k.word(1), _io.pitch))), 1)));
			const int32_t modulator = clampCv(k.mul(0x82d1e,
				add(static_cast<int32_t>(expPitch(e, modPitchWord(e, k.word(7), _io.pitch))), 1)));
			setBe16(k.cv(), static_cast<uint32_t>(carrier));
			setBe16(k.cv() + 2, static_cast<uint32_t>(modulator));
			setBe32(k.level(), 0);
			setBe32(k.level() + 4, 0);
		}

		int32_t click = k.state(2);
		const auto step = static_cast<uint32_t>(k.state(0));
		if(step > 4)
			k.setState(0, -1);
		else if(k.state(1) <= 15)
		{
			switch(step)
			{
			case 0:
				k.on(4);
				k.on(3);
				kickTrig(k);
				click = 0;
				break;
			case 1:
				k.off(0);
				setBe16(k.env(), 0x4000);
				k.env()[0x14] = 0xf0;
				k.env()[0x12] = 0x7f;
				k.env()[0x13] = 0x7f;
				k.env()[0x1a] = 0x7f;
				setBe16(k.env() + 0x10, 0x4000);
				setBe16(k.env() + 0x18, 0x4000);
				k.env()[0x1c] = 0xf0;
				k.env()[4] = 0xf0;
				k.env()[0x1b] = 0x80;
				k.command(0x15, 0x10);
				k.field(0x14, 0x500);
				k.command(0x1d, 0x10);
				k.field(0x1c, lsl(add(asr(k.word(6), 1), 0x3fff), 5));
				k.env()[2] = static_cast<uint8_t>(asr(add(p48, -0x4080), 8));
				k.env()[3] = static_cast<uint8_t>(asr(add(asr(k.word(4), 4), 0x71e), 8));
				k.command(5, 0x10);
				setBe16(k.env() + 8, 0x4000);
				k.field(4, 1 << 18);
				k.env()[0xa] = static_cast<uint8_t>(asr(add(asr(k.word(5), 4), 0x71e), 8));
				k.env()[0xb] = 0;
				k.env()[0xc] |= 0xf0;
				k.command(0xd, 0x10);
				k.field(0xc, lsl(add(p5, 0x3000), 5));
				k.setHead(1, fourSat(sub(0, square(noiseBase))));
				k.setState(1, add(k.state(1), 0x80));
				click = fourSat(square(clickLevel));
				k.setState(0, add(k.state(0), 1));
				break;
			case 2:
				k.command(4, 0xb0);
				setBe16(k.env(), 0x4000);
				k.env()[3] = 0;
				k.command(5, 0x10);
				k.field(4, lsl(add(p4, 0x3000), 5));
				k.setState(1, add(k.state(1), 0x500));
				k.setState(0, add(k.state(0), 1));
				break;
			case 3:
				kickDecay(k, k.word(2), 0x10);
				break;
			case 4:
				k.setState(0, -1);
				break;
			}
		}

		k.setHead(0, k.mul(q31(4, 5), k.head(1)));
		clickPulse(k, click);
		noiseSettings(_io, 124, 124, 45, 32);
	}

	namespace
	{
		constexpr double Pi = 3.14159265358979323846;

		// The decay times of 128 steps: _from ms at 0 to _to ms at 127 along 257^(k/127)
		double decayTime(const int _k, const double _from, const double _to)
		{
			return _from + (_to - _from) * (std::pow(257.0, _k / 127.0) - 1) / 256;
		}

		// The envelope's rate code (octave << 16 | mantissa) for a fall of 60 dB in _ms at 48 kHz: the step below full
		// scale a sample, plus the 2^-16 the envelope adds back, as an octave and a mantissa below it. A fit of the
		// original's codes, not their own computation (kickTables gives its bound)
		int32_t rateCode(const double _ms)
		{
			const double pole = std::pow(1e-3, 1.0 / (48.0 * _ms));
			const double step = std::ldexp(1.0 - pole, 31) + 0x8000;
			int octave = 0;
			while(std::ldexp(step, octave + 1) <= 2147483648.0)
				++octave;
			const auto m = static_cast<int32_t>(std::floor((2147483648.0 - std::ldexp(step, octave)) / 16384.0));
			return octave << 16 | (m & 0xffff);
		}

		// The kicks' decay rates by the decay byte: 10 ms to 2.5 s
		std::array<int32_t, 128> g_decayRates = []
		{
			std::array<int32_t, 128> r{};
			for(int k = 0; k < 128; ++k)
				r[static_cast<size_t>(k)] = rateCode(decayTime(k, 10, 2500));
			return r;
		}();

		// The rates of another machine, 0 to 15 s, whose steps 44-127 a byte past 127 of Acoustic's decay reads
		constexpr int LongRatesFrom = 44;
		std::array<int32_t, 128 - LongRatesFrom> g_longRates = []
		{
			std::array<int32_t, 128 - LongRatesFrom> r{};
			for(int k = LongRatesFrom; k < 128; ++k)
				r[static_cast<size_t>(k - LongRatesFrom)] = rateCode(decayTime(k, 0, 15000));
			return r;
		}();

		// Sharp's click, a sound of our own: 83 samples of a 1204 Hz sine under a half-sine window, scaled to a peak
		// of 0x206b0400 (as long, as bright and as loud as the original's), then five of silence
		constexpr int ClickLength = 83;
		std::array<int32_t, 88> g_sharpClick = []
		{
			std::array<double, 88> x{};
			double peak = 0;
			for(int n = 0; n < ClickLength; ++n)
			{
				x[static_cast<size_t>(n)] = std::sin(2 * Pi * 1204.0 * n / 48000) * std::sin(Pi * (n + 0.5) / ClickLength);
				peak = std::max(peak, std::abs(x[static_cast<size_t>(n)]));
			}
			std::array<int32_t, 88> r{};
			for(size_t n = 0; n < r.size(); ++n)
				r[n] = static_cast<int32_t>(x[n] * (0x206b0400 / peak));
			return r;
		}();

		// Acoustic's body, a sound of our own: 100 ms of a sine falling from 224 Hz to 84 Hz, under a fast and a slow
		// decay, over a short burst of noise at its start, scaled to a peak of 30935 (as long, as bright, as loud and
		// as long to fall 20 and 40 dB as the original's)
		std::array<int16_t, 4800> g_acousticBody = []
		{
			std::array<double, 4800> x{};
			double phase = 0, peak = 0;
			uint32_t seed = 1;
			for(size_t n = 0; n < x.size(); ++n)
			{
				const double t = static_cast<double>(n) / 48000;
				const double freq = 84.1488 + (223.998 - 84.1488) * std::exp(-t / 0.00722071);
				phase += 2 * Pi * freq / 48000;
				const double amp = std::exp(-t / 0.00997145) + 0.0887167 * std::exp(-t / 0.0475378);
				seed = seed * 1664525u + 1013904223u;
				const double noise = (static_cast<double>(seed >> 16) - 32768) / 32768;
				x[n] = -std::sin(phase) * amp + 1.191 * noise * std::exp(-t / 0.0038168);
				peak = std::max(peak, std::abs(x[n]));
			}
			std::array<int16_t, 4800> r{};
			for(size_t n = 0; n < r.size(); ++n)
				r[n] = static_cast<int16_t>(x[n] * (30935 / peak));
			return r;
		}();

		const Table g_tables[] = {
			{"kick.decayRates", g_decayRates.data(), static_cast<int>(g_decayRates.size()), 4, 3},
			{"kick.longRates", g_longRates.data(), static_cast<int>(g_longRates.size()), 4, 1},
			{"kick.sharpClick", g_sharpClick.data(), static_cast<int>(g_sharpClick.size()), 4, -1},
			{"kick.acousticBody", g_acousticBody.data(), static_cast<int>(g_acousticBody.size()), 2, -1},
		};

		int32_t decayRate(const int _k) { return g_decayRates[static_cast<size_t>(_k)]; }
		int32_t longRate(const int _k) { return g_longRates[static_cast<size_t>(_k - LongRatesFrom)]; }
		// The click, then what the routine reads past it: the rate table that follows it in the original
		int32_t sharpClick(const int _i)
		{
			const auto n = static_cast<int>(g_sharpClick.size());
			return _i < n ? g_sharpClick[static_cast<size_t>(_i)] : _i - n < 128 ? decayRate(_i - n) : 0;
		}
		// (the routine reads no further: its position runs in steps of 32 from 0 to the end)
		int32_t acousticSample(const int _i)
		{
			return _i < static_cast<int>(g_acousticBody.size()) ? g_acousticBody[static_cast<size_t>(_i)] : 0;
		}
	}

	const Table* kickTables(int& _count)
	{
		_count = static_cast<int>(std::size(g_tables));
		return g_tables;
	}

	namespace
	{
		// How much of the noise level a block keeps: the factor of a fall of 60 dB in a time from 0 to _to ms along
		// 33^(k/127), in blocks of 32 at 48 kHz, truncated (exact)
		int32_t keepFactor(const int _k, const double _to)
		{
			if(_k == 0)
				return 0;
			const double blocks = _to / 32 * (std::pow(33.0, _k / 127.0) - 1) * 48 / 32;
			return static_cast<int32_t>(std::ldexp(std::exp(std::log(1e-3) / blocks), 31));
		}

		// Silky's factor by its decay byte: to 10 s; a byte past 127 reads the table before it, the same to 15 s
		int32_t silkyKeep(const int _i)
		{
			static const std::array<int32_t, 256> t = []
			{
				std::array<int32_t, 256> r{};
				for(int k = 0; k < 128; ++k)
				{
					r[static_cast<size_t>(k)] = keepFactor(k, 15000);
					r[static_cast<size_t>(128 + k)] = keepFactor(k, 10000);
				}
				return r;
			}();
			return t[static_cast<size_t>(_i + 128)];
		}

		// Seven calibration slots, which other routines pick from: three 3s, three 4s, a 5
		int32_t slots(const int _j) { return 3 + (_j >= 3) + (_j >= 6); }
	}

	namespace
	{
		// Three switches set together by a code 1-6, any other code the default (off, on, on)
		void switchTriple(Kick& _k, const int _code, const int _a, const int _b, const int _c)
		{
			// bits: _a, _b, _c
			int bits;
			switch(_code)
			{
			case 1: bits = 0b010; break;
			case 2: bits = 0b111; break;
			case 3: bits = 0b001; break;
			case 4: bits = 0b000; break;
			case 5: bits = 0b101; break;
			case 6: bits = 0b110; break;
			default: bits = 0b011; break;
			}
			const auto set = [&](const int _sw, const bool _on) { _on ? _k.on(_sw) : _k.off(_sw); };
			set(_a, bits & 4);
			set(_b, bits & 2);
			set(_c, bits & 1);
		}

		// A decay byte past 127 reads the words before the routine's copy of the rate table, which differ by copy: what
		// they hold is written here as what the other tables are

		// Plastic's: the table's entries 22-127, then the slots, a run of three codes and its two configurations
		int32_t plasticRate(const int _i)
		{
			if(_i >= 0)
				return decayRate(_i);
			if(_i < -22)
				return decayRate(_i + 150);
			if(_i < -15)
				return slots(_i + 22);
			if(_i < -8)
				return (_i + 15 >= 3) + (_i + 15 >= 6);
			const int j = _i + 8;
			constexpr int32_t fields[] = {1, 0, 3};
			return j % 4 == 0 ? j / 4 : fields[j % 4 - 1];
		}

		// Silky's: the end of Classic's sounds, their noise decays and settings, the slots, Hard's noise settings
		int32_t silkyRate(const int _i)
		{
			if(_i >= 0)
				return decayRate(_i);
			if(_i >= -2)
				return _i == -2 ? 124 << 24 | 124 << 8 : 45 << 24 | 32 << 8;
			if(_i >= -9)
				return slots(_i + 9);
			if(_i >= -29)
			{
				// two longs a style: bytes 0 and 2, then 4 and 6
				const int j = _i + 29;
				VoiceIo io;
				classicNoise(io, j / 2);
				const int b = (j % 2) * 2;
				return io.noise[static_cast<size_t>(b)] << 24 | io.noise[static_cast<size_t>(b + 1)] << 8;
			}
			if(_i >= -39)
				return classicNoiseDecay(_i + 39);
			const int l = _i + 128 + 79;
			const ClassicSound s = classicSound(l / 3);
			return l % 3 == 0 ? s.click : l % 3 == 1 ? s.noise : s.style;
		}

		// Sharp's: the table's last entries, the slots, its sounds' configurations, then its click
		int32_t sharpRate(const int _i)
		{
			if(_i >= 0)
				return decayRate(_i);
			if(_i >= -88)
				return sharpClick(_i + 88);
			if(_i >= -112)
			{
				const int j = _i + 112;
				constexpr int32_t codes[] = {0, 1, 2, 3, 5, 6};
				return j % 2 == 0 ? (j / 2 + 1) % 2 : codes[j / 4];
			}
			if(_i >= -119)
				return slots(_i + 119);
			return decayRate(_i + 247);
		}

		// Acoustic's: the end of the long rates, the slots, its sounds' configurations, its body's length
		int32_t acousticRate(const int _i)
		{
			if(_i >= 0)
				return decayRate(_i);
			if(_i == -1)
				return 4800;
			if(_i >= -37)
			{
				const int j = _i + 37;
				const int e = j / 3;
				constexpr int32_t codes[] = {0, 1, 2, 3, 5, 0};
				return j % 3 == 0 ? (e + 1) % 2 : j % 3 == 1 ? (e >= 10 ? 1 : 0) : codes[e / 2];
			}
			if(_i >= -44)
				return slots(_i + 44);
			return longRate(_i + 172);
		}

		// _n values from _from towards _to in equal steps (divs.l: truncated), into _out
		void ramp(uint8_t* _out, const int32_t _from, const int32_t _to, const int32_t _n)
		{
			const int32_t step = sub(_to, _from) / _n;
			int32_t v = _from;
			for(int i = 0; i < _n; ++i)
			{
				setBe32(_out + 4 * i, static_cast<uint32_t>(v));
				v = add(v, step);
			}
		}
	}

	void kickPlastic(VoiceIo& _io)
	{
		Kick k(_io);
		Emac& e = k.e;

		// the sound byte: positive or not, which picks switch 4
		const int variant = k.byte(6) > 0 ? 1 : 0;
		switchTriple(k, 0, 6, 1, 5);
		switchTriple(k, 3, 8, 2, 7);
		if(variant == 0)
			k.on(4);
		else
			k.off(4);
		k.on(3);
		k.on(9);

		int32_t level = k.state(2);
		int32_t falloff = k.state(4);
		int32_t rate = k.state(3);

		int32_t a = 0, b = 0;
		if(k.state(0) >= 0)
		{
			const auto cv = [&]
			{
				const auto f = static_cast<int32_t>(expPitch(e, pitchWord(e, k.word(1), _io.pitch)));
				return clampCv(k.mul(0x4168f, add(f, 1)));
			};
			a = cv();
			b = cv();
		}
		setBe16(k.cv(), static_cast<uint32_t>(a));
		setBe16(k.cv() + 2, static_cast<uint32_t>(b));
		setBe32(k.level(), 0);
		setBe32(k.level() + 4, 0);

		const int32_t pulseWord = twiceSat(k.mul(q31(64, 127), 0x48c00000)) & static_cast<int32_t>(0xffff0000);
		const int32_t pulse = k.mul(1 << 30, pulseWord);
		const int16_t w6 = k.word(6);
		const int32_t p5 = k.mul(0x4e560418, asr(k.word(5), 1));
		const int32_t pw = static_cast<int16_t>(static_cast<uint32_t>(pulseWord) >> 16);
		const int32_t base = k.mul(0xc309, square(pw));
		setBe16(k.cv() + 4, static_cast<uint32_t>(base));
		e.msac(0, lsl(base & 0xffff, 3), accentScale(k.byte(0)));
		setBe16(k.cv() + 4, static_cast<uint32_t>(e.storeClear(0)));
		const int32_t tone = static_cast<int16_t>(static_cast<uint32_t>(
			twiceSat(k.mul(q31(64, 127), lsl(k.word(4), 16)))) >> 16);
		setBe16(k.cv() + 6, static_cast<uint32_t>(k.mul(0x7505, square(tone))));

		int32_t click = 0;
		const auto step = static_cast<uint32_t>(k.state(0));
		if(step > 4)
			k.setState(0, -1);
		else
		{
			switch(step)
			{
			case 0:
				k.on(0);
				setBe16(k.env(), 0);
				k.env()[4] = 0x80;
				setBe16(k.env() + 8, 0);
				k.env()[0xc] = 0x80;
				k.setState(1, 16);
				k.setState(0, add(k.state(0), 1));
				break;
			case 1:
			{
				if(k.state(1) > 15)
					break;
				const int32_t p = static_cast<int16_t>(static_cast<uint32_t>(pulse) >> 16);
				k.setHead(1, sub(0, add(square(p), square(p))));
				click = velocitySquare(e, add(asr(k.word(7), 1), 0x2000), 0x48c0);
				k.setState(0, add(k.state(0), 1));
				k.setState(1, 16);
				level = 0x7923af04;
				break;
			}
			case 2:
			{
				if(k.state(1) > 15)
					break;
				k.off(0);
				rate = plasticRate(k.byte(4));
				setBe32(k.env() + 0x14, 0xf0100200);
				setBe32(k.env() + 0x1c, 0xf0100200);
				setBe32(k.env() + 0x10, 0x40007f7f);
				setBe32(k.env() + 0x18, 0x40007f7f);
				const uint32_t hold = (static_cast<uint32_t>(static_cast<int32_t>(w6)) >> 3) & 0xff00;
				const uint32_t decay = static_cast<uint32_t>(lsl(add(p5, 0x3000), 5)) & 0xfffe0;
				setBe32(k.env(), hold | 1u << 30);
				setBe32(k.env() + 8, hold | 1u << 30);
				setBe32(k.env() + 4, decay | 0xf0100000);
				setBe32(k.env() + 0xc, decay | 0xf0100000);
				falloff = 0x4000d1b7;
				k.setState(1, 0x200);
				k.setState(0, add(k.state(0), 1));
				break;
			}
			case 3:
			{
				const int32_t t = k.state(1);
				if(t > 15)
					break;
				const int32_t r = twiceSat(k.mul(falloff, rate));
				if(r == rate)
				{
					const int32_t ticks = decayTicksDefault(e, rate);
					k.setState(0, add(k.state(0), 1));
					k.setState(1, add(t, ticks));
				}
				setBe32(k.env() + 0x10, 0x40000080);
				setBe32(k.env() + 0x14, (static_cast<uint32_t>(r) & 0xfffff) | 0xb0100000);
				rate = r;
				break;
			}
			case 4:
				if(k.state(1) > 15)
					break;
				k.on(3);
				k.setState(0, -1);
				break;
			}
		}

		std::fill(_io.outA.begin(), _io.outA.begin() + 128, uint8_t(0));
		if(click != 0)
		{
			// a pulse of the click, negative: up in 8 samples, held 16, down in 8
			const int32_t c = sub(0, click);
			ramp(_io.outA.data(), 0, c, 8);
			ramp(_io.outA.data() + 0x20, c, c, 16);
			ramp(_io.outA.data() + 0x60, c, 0, 8);
		}
		k.setHead(0, k.mul(level, k.head(1)));
		k.setState(2, twiceSat(k.mul(k.mul(falloff, 0x7ffe5c91), level)));
		noiseSettings(_io, 0x40, 0x40, 0x20, 0x40);
		k.setState(3, rate);
		k.setState(4, sub(falloff, k.mul(0x126e978, add(falloff, static_cast<int32_t>(0xc0000000)))));
	}

	namespace
	{
		// The high word of twice _q31 * 64/127, saturated: a knob word's top byte as a signed fraction
		int16_t knobHigh(Kick& _k, const int32_t _q)
		{
			return static_cast<int16_t>(static_cast<uint32_t>(twiceSat(_k.mul(q31(64, 127), _q))) >> 16);
		}
		// The high word of the product of two words' low halves
		int16_t wordProduct(Kick& _k, const int16_t _a, const int16_t _b)
		{
			_k.e.macWord(0, _a, false, _b, false);
			return static_cast<int16_t>(static_cast<uint32_t>(_k.e.storeClear(0)) >> 16);
		}
	}

	void kickSilky(VoiceIo& _io)
	{
		Kick k(_io);
		Emac& e = k.e;

		k.off(6);
		k.on(1);
		k.on(5);
		k.on(8);
		k.off(2);
		k.on(7);
		k.on(9);
		k.on(4);
		if(k.state(0) >= 0)
			k.off(3);

		int32_t level = k.state(2);
		int32_t keep = k.state(5);
		int32_t rate = k.state(3);
		int32_t falloff = k.state(4);
		const int32_t p4 = k.mul(0x4e560418, asr(k.word(4), 1));

		int32_t carrier = 0;
		if(k.state(0) >= 0)
			carrier = clampCv(k.mul(0x4168f, add(static_cast<int32_t>(expPitch(e, pitchWord(e, k.word(1), _io.pitch))), 1)));
		setBe16(k.cv(), static_cast<uint32_t>(carrier));
		setBe16(k.cv() + 2, 0);
		setBe32(k.level(), 0);
		setBe32(k.level() + 4, 0);

		const int16_t c = knobHigh(k, 0x591a0000);
		const int16_t w7 = wordProduct(k, c, knobHigh(k, lsl(k.word(7), 16)));
		const int32_t cv2 = k.mul(0x8206, c * c);
		const int32_t cv3 = k.mul(0x20006, w7 * w7);
		setBe16(k.cv() + 4, static_cast<uint32_t>(cv2));
		setBe16(k.cv() + 6, static_cast<uint32_t>(cv3));
		e.msac(0, lsl(cv2 & 0xffff, 3), accentScale(k.byte(0)));
		setBe16(k.cv() + 4, static_cast<uint32_t>(e.storeClear(0)));
		e.msac(0, lsl(cv3 & 0xffff, 3), accentScale(k.byte(0)));
		const int32_t a3 = e.storeClear(0);
		setBe16(k.cv() + 6, static_cast<uint32_t>(a3 < 0 ? 0 : a3 > 0xffff ? 0xffff : a3));
		const int16_t w6 = wordProduct(k, c, knobHigh(k, lsl(k.word(6), 16)));
		const int32_t noise = add(sub(0, square(w6)), sub(0, square(w6)));

		const int32_t step = k.state(0);
		switch(step)
		{
		case 0:
			k.on(0);
			setBe32(k.env() + 8, 0x40007f80);
			setBe32(k.env() + 0xc, 0xf0100000);
			setBe32(k.env(), 0x40007f80);
			setBe32(k.env() + 4, 0xf0100000);
			k.setState(1, 16);
			k.setState(0, add(k.state(0), 1));
			break;
		case 1:
		{
			k.off(0);
			const int i = k.byte(4);
			const int32_t hold = asr(k.word(3), 4);
			rate = silkyRate(i);
			keep = silkyKeep(i);
			setBe32(k.env() + 0x10, 0x40007f7f);
			setBe32(k.env() + 8, 0x40007f00);
			setBe32(k.env() + 0xc, 0xf010fde8);
			level = INT32_MAX;
			setBe32(k.env() + 0x14, (static_cast<uint32_t>(hold) & 0xfffff) | 0xf0100000);
			setBe32(k.env(), (static_cast<uint32_t>(static_cast<int32_t>(k.word(5))) >> 3 & 0xff00) | 1u << 30);
			setBe32(k.env() + 0x1c, 0xf013d090);
			setBe32(k.env() + 4, (static_cast<uint32_t>(lsl(add(p4, 0x3000), 5)) & 0xfffff) | 0xf0100000);
			setBe32(k.env() + 0x18, 0x40007f80);
			k.setState(1, hold);
			falloff = 0x4000d1b7;
			k.setState(0, add(k.state(0), 1));
			break;
		}
		case 2:
		{
			const int32_t t = k.state(1);
			if(t > 15)
				break;
			const int32_t r = twiceSat(k.mul(falloff, rate));
			if(r == rate)
			{
				const int32_t ticks = decayTicksDefault(e, rate);
				k.setState(0, add(k.state(0), 1));
				k.setState(1, add(t, ticks));
			}
			setBe32(k.env() + 0x10, 0x40000080);
			setBe32(k.env() + 0x14, (static_cast<uint32_t>(r) & 0xfffff) | 0xb0100000);
			rate = r;
			break;
		}
		case 3:
			if(k.state(1) <= 15)
			{
				k.on(3);
				k.setState(0, -1);
			}
			break;
		default:
			k.setState(0, -1);
			break;
		}

		std::fill(_io.outA.begin(), _io.outA.begin() + 128, uint8_t(0));
		level = k.mul(keep, level);
		k.setHead(0, k.mul(level, noise));
		noiseSettings(_io, 0x7f, 0x7f, 0x11, 0x00);
		k.setState(2, level);
		k.setState(3, rate);
		k.setState(4, sub(falloff, k.mul(0x126e978, add(falloff, static_cast<int32_t>(0xc0000000)))));
		k.setState(5, keep);
	}

	void kickSharp(VoiceIo& _io)
	{
		Kick k(_io);
		Emac& e = k.e;

		// the sound byte (0-11): whether the trig switch closes, and a code for three switches
		const int s = k.byte(12);
		const int sound = s < 0 ? 0 : s > 11 ? 11 : s;
		const bool trigSwitch = (sound & 1) == 0;
		constexpr int codes[] = {0, 1, 2, 3, 5, 6};
		k.off(6);
		k.on(1);
		k.on(5);
		switchTriple(k, codes[sound / 2], 8, 2, 7);
		k.on(4);
		if(k.state(0) >= 0)
			k.off(3);
		k.on(9);

		int32_t level = k.state(2);
		int32_t rate = k.state(3);
		int32_t falloff = k.state(4);
		int32_t clickAt = k.state(5);
		int32_t trigged = k.state(7);

		int32_t carrier = 0;
		if(k.state(0) >= 0)
		{
			const int32_t v = k.mul(0x4168f, add(static_cast<int32_t>(expPitch(e, pitchWord(e, k.word(1), _io.pitch))), 1));
			carrier = v < 0 ? 0 : v > 0x7fff ? 0x7fff : v;
		}
		setBe16(k.cv() + 2, static_cast<uint32_t>(carrier));
		setBe16(k.cv(), 0);
		setBe32(k.level(), 0);
		setBe32(k.level() + 4, 0);

		const int32_t c = twiceSat(k.mul(q31(64, 127), 0x591a0000)) & static_cast<int32_t>(0xffff0000);
		const int32_t clickLevel = velocitySquare(e, k.word(7), 0x591a);
		const int32_t pulse = k.mul(1 << 30, c);
		const int32_t p4 = k.mul(0x4e560418, asr(k.word(4), 1));
		const int32_t ch = static_cast<int16_t>(static_cast<uint32_t>(c) >> 16);
		const int32_t cv3 = k.mul(0x8206, square(ch));
		setBe16(k.cv() + 6, static_cast<uint32_t>(cv3));
		setBe16(k.cv() + 4, 0);
		e.msac(0, lsl(cv3 & 0xffff, 3), accentScale(k.byte(0)));
		setBe16(k.cv() + 6, static_cast<uint32_t>(e.storeClear(0)));

		const auto step = static_cast<uint32_t>(k.state(0));
		if(step > 4)
			k.setState(0, -1);
		else
		{
			switch(step)
			{
			case 0:
				if(trigSwitch)
					k.on(0);
				k.command(0xc, 0x90);
				setBe16(k.env() + 8, 0);
				trigged = trigSwitch ? 1 : 0;
				k.setState(1, 16);
				clickAt = -1;
				k.setState(0, add(k.state(0), 1));
				break;
			case 1:
			{
				if(k.state(1) > 15)
					break;
				k.setState(0, 2);
				clickAt = 0;
				level = 0x7923af04;
				const int32_t p = static_cast<int16_t>(static_cast<uint32_t>(pulse) >> 16);
				k.setHead(1, sub(0, add(square(p), square(p))));
				k.setState(1, 16);
				break;
			}
			case 2:
			{
				if(k.state(1) > 15)
					break;
				if(trigged != 0)
					k.off(0);
				const int32_t hold = asr(k.word(3), 4);
				rate = sharpRate(k.byte(4));
				setBe32(k.env() + 0x18, 0x40007f7f);
				setBe32(k.env() + 0x1c, (static_cast<uint32_t>(hold) & 0xfffff) | 0xf0100000);
				setBe32(k.env() + 0xc, (static_cast<uint32_t>(lsl(add(p4, 0x3000), 5)) & 0xfffff) | 0xf0100000);
				falloff = 0x4000d1b7;
				setBe32(k.env() + 8, (static_cast<uint32_t>(static_cast<int32_t>(k.word(5))) >> 3 & 0xff00) | 1u << 30);
				k.setState(1, hold);
				k.setState(0, add(k.state(0), 1));
				break;
			}
			case 3:
			{
				const int32_t t = k.state(1);
				if(t > 15)
					break;
				const int32_t r = twiceSat(k.mul(falloff, rate));
				if(r == rate)
				{
					const int32_t ticks = decayTicksDefault(e, rate);
					k.setState(0, add(k.state(0), 1));
					k.setState(1, add(t, ticks));
				}
				setBe32(k.env() + 0x18, 0x40000080);
				setBe32(k.env() + 0x1c, (static_cast<uint32_t>(r) & 0xfffff) | 0xb0100000);
				rate = r;
				break;
			}
			case 4:
				if(k.state(1) > 15)
					break;
				k.on(3);
				k.setState(0, -1);
				break;
			}
		}

		std::fill(_io.outA.begin(), _io.outA.begin() + 128, uint8_t(0));
		if(static_cast<uint32_t>(clickAt) <= 0x57)
		{
			for(int j = 0; j < 32; ++j)
				setBe32(_io.outA.data() + 4 * j, static_cast<uint32_t>(k.mul(clickLevel, sharpClick(clickAt + j))));
			clickAt = add(clickAt, 32);
		}
		k.setHead(0, k.mul(level, k.head(1)));
		const int32_t next = twiceSat(k.mul(k.mul(falloff, 0x7ffe5c91), level));
		noiseSettings(_io, 0x40, 0x40, 0x20, 0x40);
		k.setState(5, clickAt);
		k.setState(2, next);
		k.setState(3, rate);
		k.setState(4, sub(falloff, k.mul(0x126e978, add(falloff, static_cast<int32_t>(0xc0000000)))));
		k.setState(7, trigged);
	}

	void kickAcoustic(VoiceIo& _io)
	{
		Kick k(_io);
		Emac& e = k.e;

		k.on(9);
		// the sound byte (0-11): whether the trig switch closes, whether the body is sampled, a code for three switches
		const int s = k.byte(12);
		const int sound = s < 0 ? 0 : s > 11 ? 11 : s;
		const bool trigSwitch = (sound & 1) == 0;
		const bool sampled = sound >= 10;
		constexpr int codes[] = {0, 1, 2, 3, 5, 0};
		k.off(6);
		k.on(1);
		k.on(5);
		switchTriple(k, codes[sound / 2], 8, 2, 7);
		if(sampled)
			k.off(4);
		else
			k.on(4);
		if(k.state(0) >= 0)
			k.off(3);

		int32_t level = k.state(2);
		int32_t rate = k.state(3);
		int32_t falloff = k.state(4);
		int32_t sampleAt = k.state(5);
		int32_t trigged = k.state(7);

		int32_t carrier = 0;
		if(k.state(0) >= 0)
		{
			const int32_t v = k.mul(0x4168f, add(static_cast<int32_t>(expPitch(e, pitchWord(e, k.word(1), _io.pitch))), 1));
			carrier = v < 0 ? 0 : v > 0x7fff ? 0x7fff : v;
		}
		setBe16(k.cv(), 0);
		setBe16(k.cv() + 2, static_cast<uint32_t>(carrier));
		// a sampled body sets its two levels itself; else the unit's trims, zero untrimmed
		setBe32(k.level(), sampled ? 0x75307530u : 0u);
		setBe32(k.level() + 4, 0);

		const int32_t c = twiceSat(k.mul(q31(64, 127), 0x591a0000)) & static_cast<int32_t>(0xffff0000);
		const int32_t v7 = static_cast<int16_t>(add(asr(k.mul(q31(4, 5), k.word(7)), 1), 0x4000));
		const int32_t bodyLevel = velocitySquare(e, v7, 0x591a);
		const int32_t pulse = k.mul(0x26666666, c);
		const int32_t p4 = k.mul(0x4e560418, asr(k.word(4), 1));
		const int32_t ch = static_cast<int16_t>(static_cast<uint32_t>(c) >> 16);
		const int32_t cv = k.mul(0x8206, square(ch));
		// the accent's word goes to the third CV with a sampled body, to the fourth else
		uint8_t* const on = k.cv() + (sampled ? 4 : 6);
		uint8_t* const off = k.cv() + (sampled ? 6 : 4);
		setBe16(on, static_cast<uint32_t>(cv));
		setBe16(off, 0);
		e.msac(0, lsl(cv & 0xffff, 3), accentScale(k.byte(0)));
		setBe16(on, static_cast<uint32_t>(e.storeClear(0)));

		bool play = true;
		const auto step = static_cast<uint32_t>(k.state(0));
		if(step > 4)
			k.setState(0, -1);
		else
		{
			switch(step)
			{
			case 0:
				if(trigSwitch)
					k.on(0);
				k.command(0xc, 0x90);
				setBe16(k.env() + 8, 0);
				trigged = trigSwitch ? 1 : 0;
				k.setState(1, 16);
				sampleAt = -1;
				k.setState(0, add(k.state(0), 1));
				play = false;
				break;
			case 1:
			{
				if(k.state(1) > 15)
					break;
				k.setState(0, 2);
				sampleAt = 0;
				level = q31(97, 100);
				const int32_t p = static_cast<int16_t>(static_cast<uint32_t>(pulse) >> 16);
				k.setHead(1, sub(0, add(square(p), square(p))));
				k.setState(1, 16);
				break;
			}
			case 2:
			{
				if(k.state(1) > 15)
					break;
				if(trigged != 0)
					k.off(0);
				setBe32(k.env() + 0x14, 0xf0000000);
				setBe32(k.env() + 0x1c, 0xf0000000);
				rate = acousticRate(k.byte(4));
				setBe32(k.env() + 0x10, 0x40007f7f);
				setBe32(k.env() + 0x18, 0x40007f7f);
				setBe32(k.env() + 0xc, (static_cast<uint32_t>(lsl(add(p4, 0x3000), 5)) & 0xfffff) | 0xf0100000);
				setBe32(k.env() + 8, (static_cast<uint32_t>(static_cast<int32_t>(k.word(5))) >> 3 & 0xff00) | 1u << 30);
				k.setState(0, add(k.state(0), 1));
				k.setState(1, add(k.state(1), asr(k.word(3), 4)));
				falloff = 0x4000d1b7;
				break;
			}
			case 3:
			{
				if(k.state(1) > 15)
					break;
				setBe32(k.env() + 0x10, 0x40000080);
				setBe32(k.env() + 0x18, 0x40000080);
				const uint32_t r = (static_cast<uint32_t>(rate) & 0xfffff) | 0xb0100000;
				setBe32(k.env() + 0x14, r);
				setBe32(k.env() + 0x1c, r);
				const int32_t t = k.state(1);
				const int32_t ticks = decayTicksDefault(e, rate);
				k.setState(0, add(k.state(0), 1));
				k.setState(1, add(t, ticks));
				break;
			}
			default:
				break;
			}
		}

		if(play && static_cast<uint32_t>(sampleAt) <= 0x12bf)
		{
			// the body: 32 samples of the sampled kick from where it is, at minus its level
			for(int j = 0; j < 32; ++j)
			{
				e.macWord(0, bodyLevel, true, acousticSample(sampleAt + j) << 16, true, true);
				setBe32(_io.outA.data() + 4 * j, static_cast<uint32_t>(e.storeClear(0)));
			}
			sampleAt = add(sampleAt, 32);
		}
		else
			std::fill(_io.outA.begin(), _io.outA.begin() + 128, uint8_t(0));

		k.setHead(0, k.mul(level, k.head(1)));
		const int32_t next = k.mul(k.mul(falloff, 0x7ffe5c91), level);
		noiseSettings(_io, 0x30, 0x30, 0x34, 0x18);
		k.setState(5, sampleAt);
		k.setState(4, sub(falloff, k.mul(0x126e978, add(falloff, static_cast<int32_t>(0xc0000000)))));
		k.setState(2, twiceSat(next));
		k.setState(3, rate);
		k.setState(7, trigged);
	}
}
