#include "edDrumsRytm.h"

#include "edDrumsAnalogPort.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

// The clap's, the big tom's and the toms' control routines (edDrumsRytm.h), each as the CPU computes it
namespace elektron::drums::rytm
{
	namespace
	{
		using analog::be16;
		using analog::be32;
		using analog::setBe16;
		using analog::setBe32;
		using analog::port::Emac;
		using analog::port::add;
		using analog::port::asr;
		using analog::port::lsl;
		using analog::port::sub;

		struct Voice
		{
			VoiceIo& io;
			Emac e;

			explicit Voice(VoiceIo& _io) : io(_io), e(_io.saturate) {}

			int16_t word(const int _i) const { return static_cast<int16_t>(be16(io.params.data() + 2 * _i)); }
			// a word's value byte, signed as the CPU reads it
			int8_t value(const int _i) const { return static_cast<int8_t>(io.params[static_cast<size_t>(2 * _i)]); }

			int32_t state(const int _i) const { return static_cast<int32_t>(be32(io.state.data() + 4 * _i)); }
			void setState(const int _i, const int32_t _v) { setBe32(io.state.data() + 4 * _i, static_cast<uint32_t>(_v)); }
			int32_t head(const int _i) const { return static_cast<int32_t>(be32(io.heads.data() + 4 * _i)); }
			void setHead(const int _i, const int32_t _v) { setBe32(io.heads.data() + 4 * _i, static_cast<uint32_t>(_v)); }
			int32_t trim(const int _i) const { return static_cast<int32_t>(be32(io.trim.data() + 4 * _i)); }
			void setFrameWord(const int _at, const int32_t _v) { setBe16(io.frame.data() + _at, static_cast<uint32_t>(_v)); }

			int32_t mul(const int32_t _a, const int32_t _b)
			{
				e.mac(0, _a, _b);
				return e.storeClear(0);
			}
		};

		// divs.l: truncated toward zero; an overflow leaves the dividend, as the CPU leaves its register. A zero divisor
		// traps on the CPU (a parameter word of -256 there): here it gives the dividend
		int32_t divs(const int32_t _n, const int32_t _d)
		{
			if(_d == 0 || (_d == -1 && _n == INT32_MIN))
				return _n;
			return _n / _d;
		}

		int32_t clamp(const int32_t _v, const int32_t _lo, const int32_t _hi) { return _v < _lo ? _lo : _v > _hi ? _hi : _v; }
		// the value, or 0 where it is negative
		int32_t positive(const int32_t _v) { return _v < 0 ? 0 : _v; }
		// (a b 2) of the low words, as muls.w and an add of the result to itself
		int32_t squareWord(const int32_t _v)
		{
			const auto w = static_cast<int16_t>(_v);
			return static_cast<int32_t>(static_cast<uint32_t>(w * w) * 2u);
		}

		// A table of 128 decay coefficients by formula; index 0 holds 0. A value byte past 127 (negative as the CPU
		// reads it) reads the memory before the table there: here it is bounded to 0-127
		struct Decays
		{
			std::array<int32_t, 128> t{};
			Decays(const double _scale, const double _base)
			{
				++tablesBuilt();
				for(int k = 1; k < 128; ++k)
					t[static_cast<size_t>(k)] = decayCoefficient(_scale, _base, k);
			}
			int32_t operator[](const int _k) const { return t[static_cast<size_t>(clamp(_k, 0, 127))]; }
		};
		// The toms' pitch fall, and their level's
		const Decays& tomSweep() { static const Decays d(1000.0, 33.0); return d; }
		const Decays& tomLevel() { static const Decays d(312.5, 33.0); return d; }
		// The clap's decay
		const Decays& clapDecay() { static const Decays d(250.0, 5.0); return d; }

		// The pitch of a tom: its tune word and the note into the multiplier of its oscillator, bounded to its range
		int32_t pitchWord(Emac& _e, const int32_t _tune, const int32_t _note, const int32_t _lo, const int32_t _hi)
		{
			return clamp(analog::port::modPitchWord(_e, _tune, _note), _lo, _hi);
		}

		// A CV word from a pitch through the voice's calibration (scale, offset): ((x << 8) - offset) scale, squared
		// with a half, bounded to 0-0xffff
		int32_t calibratedCv(Voice& _v, const int32_t _x, const int32_t _scale, const int32_t _offset)
		{
			const int32_t a = _v.mul(sub(lsl(_x, 8), _offset), _scale);
			return clamp(_v.mul(a, asr(a, 1)), 0, 0xffff);
		}
		// The damping CV, 16 times over: the pitch word through the second pair (scale, offset) and the decay word's share
		int32_t dampingCv(Voice& _v, const int32_t _x, const int32_t _scale, const int32_t _offset, const int16_t _decay)
		{
			const int32_t a = _v.mul(lsl(_scale, 7), _x);
			const int32_t off = (_offset < 0 ? add(_offset, 0xfff) : _offset) >> 12;
			const int32_t share = divs(lsl(-_scale, 9), _decay + 0x100);
			return add(add(share, off), a);
		}

		// The block of 32 longs the voice is sent, from _start by equal steps toward _start _ratio
		int32_t rampBlock(Voice& _v, uint8_t* _out, const int _stride, const int32_t _start, const int32_t _ratio)
		{
			const int32_t step = asr(sub(_v.mul(_ratio, _start), _start), 5);
			int32_t x = _start;
			for(int i = 0; i < 32; ++i)
			{
				setBe32(_out + _stride * i, static_cast<uint32_t>(x));
				x = add(x, step);
			}
			return add(_start, lsl(step, 5));
		}

		// The big tom's noise settings, fixed
		constexpr std::array<uint8_t, 4> BigTomNoise{2, 31, 55, 99};

		void bigTom(VoiceIo& _io)
		{
			if(_io.voice != BigTomVoice)
				return;
			Voice v(_io);
			int32_t sweep = v.state(4);

			const int32_t tune = asr(v.word(1) - 0x4000, 1) + 0x4000;
			int32_t p = pitchWord(v.e, tune, add(_io.pitch, sweep), -0x1555555, 0x1500000);
			const int32_t hz = v.mul(0x3dbd70a, static_cast<int32_t>(analog::port::expPitch(v.e, p)));
			v.setFrameWord(0, calibratedCv(v, hz, v.trim(0), v.trim(1)));

			uint8_t& flags = _io.frame[4];
			if(!(flags & 0x08))
				p = add(p, 0x1000000);
			v.setFrameWord(2, asr(dampingCv(v, add(p, 0x2aaaaa), v.trim(2), v.trim(3), v.word(2)), 4));


			const int32_t phase = v.state(0);
			const bool waiting = v.state(1) > 15;
			if(phase == 1)
			{
				if(!waiting)
				{
					if(v.word(0) == 0)
						flags |= 0x08;
					else
						flags &= ~0x08;
					v.setState(3, 0);
					v.setState(2, asr(squareWord(v.word(0)), 9));
					const int32_t level = v.mul(squareWord(v.word(4)), static_cast<int32_t>(static_cast<uint32_t>(v.word(0)) << 16));
					v.setHead(1, level);
					sweep = static_cast<int32_t>(static_cast<uint32_t>(v.word(6)) << 5);
					// the pitch's sweep rate: the low word of the level's register holds SYN6, its top the level's
					const int32_t d1 = static_cast<int32_t>((static_cast<uint32_t>(level) & 0xffff0000u) | static_cast<uint16_t>(v.word(5)));
					int32_t rate = 0;
					if(hz != 0)
					{
						const int32_t num = lsl(static_cast<int16_t>(asr(static_cast<int16_t>(d1), 8)) * 0x5dc0, 12);
						const auto den = static_cast<uint32_t>(add(hz, hz));
						rate = den ? static_cast<int32_t>(static_cast<uint32_t>(num) / den) : num;
					}
					v.setState(1, add(v.state(1), rate));
					v.setState(0, v.state(0) + 1);
				}
			}
			else if(phase == 2)
			{
				if(!waiting)
				{
					flags |= 0x08;
					v.setState(0, -1);
					v.setState(1, INT32_MAX);
				}
			}
			else if(phase != 0)
			{
				sweep = v.mul(0x7b091a45, sweep);
				if(!waiting)
				{
					v.setState(0, -1);
					v.setState(1, INT32_MAX);
				}
			}
			else if(!waiting)
			{
				v.setState(0, 1);
				sweep = 0;
				v.setState(1, 16);
			}

			v.setState(2, rampBlock(v, _io.stream.data(), 4, v.state(2), 0x40000000));
			// the noise generator runs with saturation on and leaves it off
			_io.noise = BigTomNoise;
			v.e.setMacsr(Emac::Fractional);
			v.setHead(0, v.mul(0x6d55140b, v.head(1)));
			std::fill(_io.outA.begin(), _io.outA.begin() + 128, uint8_t(0));
			v.setState(4, sweep);
		}

		void tom(VoiceIo& _io)
		{
			if(_io.voice != 3 && _io.voice != 5)
				return;
			Voice v(_io);
			int32_t sweep = v.state(2);
			int32_t click = v.state(3);
			int32_t ticks = v.state(4);

			const int32_t p = pitchWord(v.e, v.word(1), add(_io.pitch, sweep), -0x2aaaaaa, 0x2a00000);
			const int32_t hz = v.mul(0x326f694, static_cast<int32_t>(analog::port::expPitch(v.e, p)));

			int phase = v.state(0);
			if(phase == 0)
			{
				v.setFrameWord(2, 0x7fff);
				v.setFrameWord(0, 0xffff);
				if(v.word(0) > 0)
				{
					v.setState(0, 1);
					v.setState(1, 16);
				}
				else
				{
					v.setState(0, -1);
					phase = -1;
				}
			}
			if(phase != 0 && phase != 1 && phase != 2)
			{
				v.setFrameWord(2, 0x7fff);
				click = 0;
				v.setHead(1, 0);
				sweep = 0;
				v.setFrameWord(0, 0xffff);
			}
			else if(phase == 1 || phase == 2)
			{
				if(phase == 1)
				{
					v.e.macWord(0, v.word(6), false, v.word(6), false);
					const int32_t sq = v.e.storeClear(0);
					ticks = 0;
					v.setHead(1, v.mul(sq, static_cast<int32_t>(static_cast<uint32_t>(v.word(0)) << 16)));
					v.setState(0, 2);
					click = static_cast<int32_t>(static_cast<uint32_t>(v.word(0)) << 7);
					sweep = static_cast<int32_t>(static_cast<uint32_t>(v.word(3)) << 5);
				}
				sweep = v.mul(tomSweep()[v.value(4)], sweep);
				ticks = std::min(ticks + 1, 0x12327);

				// the pitch word: through the second pair, its tune's share, a slow rise after 4500 blocks
				const int32_t w = add(asr(dampingCv(v, add(p, -0x200000), v.trim(2), v.trim(3), v.word(2)), 4), positive(ticks - 4500));

				v.setFrameWord(2, clamp(w, -0x8000, 0x7fff));
				// the frequency word through the first pair, and a slow fall after 9000 blocks
				const int32_t f = calibratedCv(v, hz, v.trim(0), v.trim(1));
				v.setFrameWord(0, clamp(add(f, positive(ticks - 9000)), 0, 0xffff));
				if(v.word(0) == 0)
					v.setState(0, -1);
			}

			click = rampBlock(v, _io.stream.data(), 4, click, 0x0ccccccd);

			// the noise's settings: a band from SYN2 and SYN8, 0-16
			const int32_t band = std::min(positive(add(asr(v.word(1), 12), asr(v.word(7) - 0x4000, 12))), 16);
			_io.noise = {static_cast<uint8_t>(band + 3), static_cast<uint8_t>(band + 4), static_cast<uint8_t>(band + 8), 0};
			v.setHead(0, v.mul(tomLevel()[v.value(5)], v.head(1)));
			v.setState(2, sweep);
			v.setState(3, click);
			v.setState(4, ticks);
		}

		// The clap: a burst of claps (SYN4's count), each SYN5's gap apart with a random spread, then a decaying tail. Its
		// envelope scales the noise block the generator wrote, in place; the heads carry the tail's level to the voice
		void clap(VoiceIo& _io)
		{
			// the noise generator runs first, with saturation on, and leaves it off
			_io.noise = {_io.params[2], _io.params[3], 0, 0};
			if(_io.voice != ClapVoice)
				return;
			Voice v(_io);
			v.e.setMacsr(Emac::Fractional);
			std::fill(_io.frame.begin(), _io.frame.end(), uint8_t(0));

			auto random = [&](const int _i) { return static_cast<int32_t>(be32(_io.random.data() + 4 * _i)); };
			auto setRandom = [&](const int _i, const int32_t _x) { setBe32(_io.random.data() + 4 * _i, static_cast<uint32_t>(_x)); };

			// the envelope's start (env), its ratio over a block, its peak and the level the voice is sent
			int32_t peak = v.state(2);
			int32_t ratio = v.state(3);
			int32_t level = v.state(4);
			int32_t env = v.state(5);
			const int32_t count = v.state(1);
			if(count <= 0)
			{
				const int32_t phase = v.state(0);
				if(phase == 0)
				{
					v.setState(1, count + 16);
					v.setState(0, 1);
				}
				else
				{
					// the gap to the next clap: 240 blocks less SYN5's share, then a random spread by SYN7
					int32_t d = (v.word(4) - 0x4000) * 240;
					if(d < 0)
						d += 0x3fff;
					const int32_t gap = 240 - (d >> 14);
					const int32_t claps = (v.word(3) >> 8) + 1;
					const int32_t spread = static_cast<int32_t>(static_cast<uint32_t>(v.word(6)) << 16);
					const int32_t r1 = random(1);
					const int32_t r0 = random(0);
					const int32_t loud = -squareWord(v.word(0));
					int32_t pick = 0;
					bool next = true;
					if(phase > 0 && phase < claps)
					{
						// another clap: silent until it starts at its peak
						pick = r1;
						level = loud;
						peak = add(lsl(v.word(7), 15), 0x40000000);
						env = 0;
						ratio = 0;
					}
					else if(phase == claps)
					{
						// the tail: SYN6's level, SYN3's decay
						const int k = std::max<int>(v.value(2), 10);
						peak = clapDecay()[k - 10];
						ratio = clapDecay()[k];
						env = v.mul(static_cast<int32_t>(static_cast<uint32_t>(v.word(5)) << 16), loud);
						level = v.mul(0x1999999a, env);
						pick = r0;
					}
					else
					{
						v.setState(1, INT32_MAX);
						v.setState(0, -1);
						next = false;
					}
					if(next)
					{
						v.e.mac(0, pick, spread);
						v.e.load(1, gap);
						v.e.mac(1, v.e.storeClear(0), gap);
						const int32_t wait = v.e.storeClear(1);
						const int32_t n1 = add(r1, r0);
						setRandom(1, n1);
						setRandom(0, add(r0, n1));
						v.setState(1, wait);
						v.setState(0, phase + 1);
					}
				}
			}

			// the noise block scaled by the envelope, falling from env by ratio over the block
			const int32_t step = asr(sub(v.mul(env, ratio), env), 5);
			int32_t g = env;
			for(int i = 0; i < 32; ++i)
			{
				uint8_t* s = _io.outA.data() + 4 * i;
				v.e.msac(0, g, static_cast<int32_t>(be32(s)));
				g = add(g, step);
				setBe32(s, static_cast<uint32_t>(v.e.storeClear(0)));
			}
			const int32_t sent = v.mul(level, peak);
			v.setHead(1, level);
			v.setState(2, peak);
			v.setState(3, ratio);
			v.setHead(0, sent);
			v.setState(4, sent);
			v.setState(5, g);
		}
	}
	int& tablesBuilt()
	{
		static int n = 0;
		return n;
	}

	int32_t decayCoefficient(const double _scale, const double _base, const int _k)
	{
		const double n = _scale * (std::pow(_base, _k / 127.0) - 1.0);
		return static_cast<int32_t>(std::floor(std::ldexp(std::pow(100.0, -1.0 / n), 31)));
	}

	void warmUp()
	{
		tomSweep();
		tomLevel();
		clapDecay();
	}

	uint8_t* regionOf(VoiceIo& _io, const Region _region, size_t& _size)
	{
		switch(_region)
		{
		case Region::Params:	_size = _io.params.size(); return _io.params.data();
		case Region::State:		_size = _io.state.size(); return _io.state.data();
		case Region::OutA:		_size = _io.outA.size(); return _io.outA.data();
		case Region::Heads:		_size = _io.heads.size(); return _io.heads.data();
		case Region::Stream:	_size = _io.stream.size(); return _io.stream.data();
		case Region::Frame:		_size = _io.frame.size(); return _io.frame.data();
		case Region::Noise:		_size = _io.noise.size(); return _io.noise.data();
		case Region::Trim:		_size = _io.trim.size(); return _io.trim.data();
		case Region::Random:	_size = _io.random.size(); return _io.random.data();
		default:				_size = 0; return nullptr;
		}
	}

	void trig(VoiceIo& _io) { std::fill(_io.state.begin(), _io.state.begin() + 8, uint8_t(0)); }

	void control(const Machine _m, VoiceIo& _io)
	{
		switch(_m)
		{
		case Machine::Clap:		clap(_io); break;
		case Machine::BigTom:	bigTom(_io); break;
		case Machine::Tom:		tom(_io); break;
		default:				break;
		}
	}

	void endBlock(VoiceIo& _io)
	{
		const auto s0 = static_cast<int32_t>(be32(_io.state.data()));
		if(s0 >= 0)
			setBe32(_io.state.data() + 4, be32(_io.state.data() + 4) - 16u);
	}
}
