#include "edDrumsAnalogPort.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace elektron::drums::analog
{
	namespace port
	{
		void Emac::accumulate(const int _n, const int32_t _y, const int32_t _x, const bool _sub, const bool _long,
			const bool _upperY, const bool _upperX, const int _scale)
		{
			const bool omc = (m_macsr & Omc) != 0;
			if(omc && m_pav[_n])
				return;
			m_pav[_n] = false;
			constexpr int64_t limit = int64_t(1) << 47;
			const auto half = [](const int32_t _r, const bool _upper)
			{
				return static_cast<int16_t>(_upper ? static_cast<uint32_t>(_r) >> 16 : static_cast<uint32_t>(_r));
			};
			int64_t r;
			if(fractional())
			{
				// a word operand is its half in the top 16 bits; the product (y x) << 1 truncated at bit 24
				const int64_t y = _long ? _y : static_cast<int64_t>(half(_y, _upperY)) * 65536;
				const int64_t x = _long ? _x : static_cast<int64_t>(half(_x, _upperX)) * 65536;
				const int64_t p = (y * x) >> 23;
				r = _sub ? m_acc[_n] - p : m_acc[_n] + p;
				if(r >= limit || r < -limit)
				{
					m_pav[_n] = true;
					r = omc ? (r > 0 ? 0x007fffffff00ll : -(int64_t(1) << 39)) : wrap48(r);
				}
			}
			else
			{
				const int64_t y = _long ? _y : half(_y, _upperY);
				const int64_t x = _long ? _x : half(_x, _upperX);
				int64_t p = y * x;
				if(p >= (int64_t(1) << 39) || p < -(int64_t(1) << 39))
				{
					m_pav[_n] = true;
					if(omc)
					{
						const bool negative = (p < 0) != _sub;
						m_acc[_n] = negative ? -(int64_t(1) << 31) : INT32_MAX;
						return;
					}
				}
				if(_scale == 1)
					p *= 2;
				else if(_scale == 3)
					p >>= 1;
				r = _sub ? m_acc[_n] - p : m_acc[_n] + p;
				if(r >= limit || r < -limit)
				{
					m_pav[_n] = true;
					r = omc ? (r >= limit ? INT32_MAX : -(int64_t(1) << 31)) : wrap48(r);
				}
			}
			m_acc[_n] = r;
		}

		int ff1(const uint32_t _v)
		{
			for(int i = 0; i < 32; ++i)
				if(_v >> (31 - i) & 1)
					return i;
			return 32;
		}

		namespace
		{
			// 2^(k/2048) in Q30, rounded, k = 0..2048: one octave, the exponential's table. A segment is read as its two
			// ends negated, which 2^31 at k = 2048 wraps to INT32_MIN, as the CPU's words hold it
			const std::array<int32_t, 2049>& exp2Table()
			{
				static const std::array<int32_t, 2049> t = []
				{
					std::array<int32_t, 2049> r{};
					for(int k = 0; k <= 2048; ++k)
					{
						const auto v = static_cast<int64_t>(std::llround(std::ldexp(std::exp2(k / 2048.0), 30)));
						r[static_cast<size_t>(k)] = static_cast<int32_t>(static_cast<uint32_t>(-v));
					}
					return r;
				}();
				return t;
			}

			constexpr int32_t TwoThirds = q31(2, 3);
			constexpr int32_t OneThird = q31(1, 3);
			// The pitch words' offsets: a note word of 0 an octave and a fraction below the exponential's range
			constexpr int32_t PitchOffset = -0x0a555555;
			constexpr int32_t ModPitchOffset = -0x07aaaaaa;
			// The knob words' scale: 1/63 in Q31, a step of the knob 1/63 of half a range
			constexpr int32_t KnobScale = 0x41041041;
		}

		int32_t pitchWord(Emac& _e, const int32_t _tune, const int32_t _note)
		{
			_e.mac(0, TwoThirds, lsl(_tune, 13));
			_e.mac(0, TwoThirds, lsl(_note, 5));
			return add(_e.storeClear(0), PitchOffset);
		}

		int32_t modPitchWord(Emac& _e, const int32_t _tune, const int32_t _note)
		{
			_e.mac(0, OneThird, lsl(_tune, 13));
			_e.mac(0, TwoThirds, lsl(_note, 5));
			return add(_e.storeClear(0), ModPitchOffset);
		}

		uint32_t expPitch(Emac& _e, const int32_t _word)
		{
			int32_t d = add(_word, static_cast<int32_t>(0xf8000000));
			if(d < static_cast<int32_t>(0xf0000000))
				d = static_cast<int32_t>(0xf0000000);
			else if(d >= 0)
				d = -1;
			return exp2(_e, d);
		}

		uint32_t exp2(Emac& _e, const int32_t _x)
		{
			const auto& t = exp2Table();
			const auto k = static_cast<size_t>(static_cast<uint32_t>(_x) >> 13 & 0x7ff);
			const int32_t lo = t[k], hi = t[k + 1];
			const int32_t frac = lsl(static_cast<int32_t>(static_cast<uint32_t>(_x) & 0x1fff), 18);
			_e.mac(0, frac, lo);
			_e.msac(0, frac, hi);
			const int shift = ~static_cast<int32_t>(static_cast<int8_t>(static_cast<uint32_t>(_x) >> 24));
			return static_cast<uint32_t>(lsr(sub(_e.storeClear(0), lo), shift));
		}

		int32_t log2(Emac& _e, const int32_t _x)
		{
			if(_x <= 0)
				return _x == 0 ? static_cast<int32_t>(0xe0000000) : 0;
			int32_t n = ff1(static_cast<uint32_t>(_x)) - 1;
			const int32_t m = lsl(_x, n);					// the mantissa, bit 30 set
			_e.load(0, 0x40000000);
			_e.msac(0, m, q31(1, 6));
			n += 2;
			_e.load(1, static_cast<int32_t>(0xf5555556));
			const int32_t a = _e.storeClear(0);
			_e.mac(1, m, a);
			const int32_t whole = -byteRev(n);				// the octave in the top byte
			return add(whole, asr(_e.storeClear(1), 4));
		}

		int32_t decayTicks(Emac& _e, const int32_t _level, const int32_t _rate)
		{
			const auto rate = static_cast<uint32_t>(_rate);
			const int32_t mantissa = static_cast<int32_t>((rate & 0xffff) << 14);
			const int shift = static_cast<int>(rate >> 16 & 0xf);
			const int32_t step = asr(sub(INT32_MAX, mantissa), shift);
			const int32_t den = log2(_e, sub(static_cast<int32_t>(0x80007fff), step));
			if(den == 0)
				return INT32_MAX;
			const int32_t num = log2(_e, _level);
			if(num == INT32_MIN && den == -1)
				return num;									// divs.l's overflow leaves its destination
			return num / den;
		}

		int32_t decayTicksDefault(Emac& _e, const int32_t _rate)
		{
			return decayTicks(_e, 0x10945, _rate);
		}

		int32_t velocitySquare(Emac& _e, const int32_t _a, const int32_t _b)
		{
			int32_t d = static_cast<int16_t>(_a) * static_cast<int16_t>(_b);
			d = add(d, d);
			_e.mac(0, d, d);
			return _e.storeClear(0);
		}

		int32_t knob(Emac& _e, const int32_t _word, int32_t& _other)
		{
			int32_t d = static_cast<int32_t>(static_cast<uint32_t>(_word) << 16);
			d = add(d, static_cast<int32_t>(0xff000000));
			_e.mac(0, d, KnobScale);
			d = add(_e.storeClear(0), static_cast<int32_t>(0xe0000000));
			d = add(d, d);
			const int32_t twice = add(d, d);
			const bool overflow = ((d ^ twice) & 0x80000000) != 0;
			d = asr(sats(twice, overflow), 1);
			d = add(d, static_cast<int32_t>(0xc0000000));
			_other = sub(INT32_MIN, d);
			return d;
		}

		void knobPair(Emac& _e, const int32_t _word, int32_t& _rising, int32_t& _falling)
		{
			const int32_t d = add(static_cast<int32_t>(static_cast<uint32_t>(_word) << 16), static_cast<int32_t>(0xc0000000));
			_e.mac(0, KnobScale, d);
			_e.msac(1, KnobScale, d);
			const auto bound = [](int32_t _v)
			{
				_v = _v < 0 ? 0 : _v > 0x20000000 ? 0x20000000 : _v;
				return lsl(add(_v, static_cast<int32_t>(0xe0000000)), 2);
			};
			_rising = bound(_e.storeClear(0));
			_falling = bound(_e.storeClear(1));
		}
	}

	uint8_t* regionOf(VoiceIo& _io, const Region _region, size_t& _size)
	{
		switch(_region)
		{
		case Region::Params:	_size = _io.params.size(); return _io.params.data();
		case Region::State:		_size = _io.state.size(); return _io.state.data();
		case Region::Smooth:	_size = _io.smooth.size(); return _io.smooth.data();
		case Region::OutA:		_size = _io.outA.size(); return _io.outA.data();
		case Region::Heads:		_size = _io.heads.size(); return _io.heads.data();
		case Region::OutB:		_size = _io.outB.size(); return _io.outB.data();
		case Region::Frame:		_size = _io.frame.size(); return _io.frame.data();
		case Region::Noise:		_size = _io.noise.size(); return _io.noise.data();
		default:				_size = 0; return nullptr;
		}
	}

	void trig(VoiceIo& _io)
	{
		std::fill_n(_io.state.begin(), 8, uint8_t(0));
	}

	void endBlock(VoiceIo& _io)
	{
		const auto s0 = static_cast<int32_t>(be32(_io.state.data()));
		const auto s1 = be32(_io.state.data() + 4);
		setBe32(_io.state.data() + 4, s0 < 0 ? 0x7fffffffu : s1 - 16);
	}

	void control(const Machine _m, VoiceIo& _io)
	{
		using Routine = void (*)(VoiceIo&);
		static constexpr Routine routines[Machines] = {
			port::kickHard, port::kickClassic, port::kickFm, port::kickPlastic, port::kickSilky, port::kickSharp,
			port::kickAcoustic, port::snareHard, port::snareClassic, port::snareFm, port::snareNatural,
			port::snareAcoustic, port::rimHard, port::rimClassic, port::synthDual, port::synthRaw, port::synthChip,
			port::closedClassic, port::openClassic, port::cymbalClassic, port::bellClassic, port::closedMetallic,
			port::openMetallic, port::cymbalMetallic, port::bellMetallic, port::ride, port::hatBasic, port::hatLab,
			port::noise, port::impulse};
		routines[static_cast<size_t>(_m)](_io);
	}

	void setup(const Machine _m, VoiceIo& _io)
	{
		port::setupMetal(_m, _io);
	}
}
