// The DSP56300's arithmetic as the voice code uses it, for the ported routines: 24-bit words, 56-bit accumulators
// (A2:A1:A0, the value as an int64_t whose low 24 bits are A0), limiting where an accumulator is stored as a word
#pragma once
#include <cstdint>

namespace md::voice::dsp
{
	inline int32_t sx(const uint32_t _w)
	{
		return static_cast<int32_t>(_w << 8) >> 8;
	}

	// A 56-bit result as the accumulator holds it, sign-extended from bit 55
	inline int64_t wrap56(const int64_t _v)
	{
		return static_cast<int64_t>(static_cast<uint64_t>(_v) << 8) >> 8;
	}

	// A word into an accumulator: A1 = the word, A0 = 0, A2 its sign
	inline int64_t acc(const uint32_t _w)
	{
		return static_cast<int64_t>(sx(_w)) * 0x1000000;
	}

	// An L move, or Y1:Y0 as a 48-bit operand
	inline int64_t acc(const uint32_t _hi, const uint32_t _lo)
	{
		return acc(_hi) | (_lo & 0xffffff);
	}

	inline uint32_t hi(const int64_t _a)
	{
		return static_cast<uint32_t>(_a >> 24) & 0xffffff;
	}

	inline uint32_t lo(const int64_t _a)
	{
		return static_cast<uint32_t>(_a) & 0xffffff;
	}

	constexpr int64_t kMax48 = 0x7fffffffffffLL, kMin48 = -0x800000000000LL;

	// An accumulator stored as a word: limited when its extension is in use
	inline uint32_t lim(const int64_t _a)
	{
		return _a > kMax48 ? 0x7fffff : _a < kMin48 ? 0x800000 : hi(_a);
	}

	// ... and as an L word pair
	inline void limL(const int64_t _a, uint32_t& _hi, uint32_t& _lo)
	{
		const int64_t v = _a > kMax48 ? kMax48 : _a < kMin48 ? kMin48 : _a;
		_hi = hi(v);
		_lo = lo(v);
	}

	// MPY: the fractional product
	inline int64_t mul(const uint32_t _s1, const uint32_t _s2)
	{
		return static_cast<int64_t>(sx(_s1)) * sx(_s2) * 2;
	}

	inline int64_t shl(const int64_t _a, const int _n)
	{
		return wrap56(static_cast<int64_t>(static_cast<uint64_t>(_a) << _n));
	}

	// MPYR's rounding, convergent at bit 23 (SR's RM clear)
	inline int64_t round(int64_t _a)
	{
		_a += 0x800000;
		if((_a & 0xffffff) == 0)
			_a &= ~0x1000000LL;
		return wrap56(_a & ~0xffffffLL);
	}

	// INSERT: _control's width (bits 12-17) of _src at its offset (bits 0-5)
	inline int64_t insert(const int64_t _d, const uint32_t _control, const uint32_t _src)
	{
		const uint32_t width = (_control >> 12) & 0x3f;
		const uint32_t offset = _control & 0x3f;
		const uint64_t mask = (1ull << width) - 1;
		const uint64_t d = (static_cast<uint64_t>(_d) & ~(mask << offset)) | ((_src & mask) << offset);
		return wrap56(static_cast<int64_t>(d));
	}

	// MOVE #xx into a data register: the byte in its top eight bits
	constexpr uint32_t imm8(const uint32_t _v)
	{
		return (_v << 16) & 0xffffff;
	}
}
