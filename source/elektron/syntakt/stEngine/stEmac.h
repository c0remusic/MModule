#pragma once

#include <cstdint>

namespace syntakt
{
	// The ColdFire EMAC as the Syntakt's voice CPU runs it. An accumulator holds a Q39 value in 48 bits; a load shifts a
	// Q31 word up by 8, a product is truncated to Q39, a store truncates back to Q31. MACSR is set per routine, so the
	// mode is the accumulator's type:
	// - Emac, MACSR = 0xA0 (written before any engine, 0x4000433c): saturating. A store saturates to 32 bits; an
	//   overflowing accumulator saturates and freezes until it is cleared (OMC).
	// - EmacWrap, MACSR = 0x20 (two of the track stages): a store wraps to 32 bits, an accumulator wraps to 48.
	template<bool Saturate>
	class EmacT
	{
	public:
		void load(const int32_t _v)
		{
			m_acc = static_cast<int64_t>(_v) * 256;
			m_overflow = false;
		}

		// mac.l: acc += y * x (fractional)
		void mac(const int32_t _y, const int32_t _x) { accumulate(product(_y, _x)); }

		// msac.l: acc -= y * x
		void msac(const int32_t _y, const int32_t _x) { accumulate(-product(_y, _x)); }

		// move.l ACC,Rx
		int32_t store() const
		{
			const int64_t v = m_acc >> 8;
			if constexpr(!Saturate)
				return static_cast<int32_t>(static_cast<uint32_t>(v));
			if(v > INT32_MAX)
				return INT32_MAX;
			if(v < INT32_MIN)
				return INT32_MIN;
			return static_cast<int32_t>(v);
		}

		// movclr.l ACC,Rx
		int32_t storeClear()
		{
			const int32_t v = store();
			m_acc = 0;
			m_overflow = false;
			return v;
		}

	private:
		// (y * x) << 1, truncated to Q39: never overflows int64, since |y * x| <= 2^62
		static int64_t product(const int32_t _y, const int32_t _x)
		{
			return (static_cast<int64_t>(_y) * _x) >> 23;
		}

		void accumulate(const int64_t _p)
		{
			const int64_t r = m_acc + _p;
			if constexpr(!Saturate)
			{
				m_acc = static_cast<int64_t>(static_cast<uint64_t>(r) << 16) >> 16;	// 48-bit wrap, sign-extended
			}
			else
			{
				if(m_overflow)
					return;
				constexpr int64_t limit = int64_t(1) << 47;
				if(r >= limit || r < -limit)
				{
					m_overflow = true;
					m_acc = r > 0 ? 0x007FFFFFFF00ll : -(int64_t(1) << 39);
					return;
				}
				m_acc = r;
			}
		}

		int64_t m_acc = 0;
		bool m_overflow = false;
	};

	using Emac = EmacT<true>;
	using EmacWrap = EmacT<false>;

	// One product alone, as `mac` into a cleared accumulator then `movclr`
	inline int32_t frac(const int32_t _y, const int32_t _x)
	{
		Emac e;
		e.mac(_y, _x);
		return e.storeClear();
	}

	inline int32_t fracWrap(const int32_t _y, const int32_t _x)
	{
		EmacWrap e;
		e.mac(_y, _x);
		return e.storeClear();
	}

	// add.l then sats.l: the 32-bit sum, saturated where it overflows
	inline int32_t addSat(const int32_t _a, const int32_t _b)
	{
		const int64_t r = static_cast<int64_t>(_a) + _b;
		return r > INT32_MAX ? INT32_MAX : r < INT32_MIN ? INT32_MIN : static_cast<int32_t>(r);
	}

	// sub.l then sats.l
	inline int32_t subSat(const int32_t _a, const int32_t _b)
	{
		const int64_t r = static_cast<int64_t>(_a) - _b;
		return r > INT32_MAX ? INT32_MAX : r < INT32_MIN ? INT32_MIN : static_cast<int32_t>(r);
	}

	// add.l alone: the 32-bit sum, wrapping
	inline int32_t addWrap(const int32_t _a, const int32_t _b)
	{
		return static_cast<int32_t>(static_cast<uint32_t>(_a) + static_cast<uint32_t>(_b));
	}

	inline int32_t subWrap(const int32_t _a, const int32_t _b)
	{
		return static_cast<int32_t>(static_cast<uint32_t>(_a) - static_cast<uint32_t>(_b));
	}

	inline int32_t neg(const int32_t _v) { return subWrap(0, _v); }

	// asl.l #n: a constant shift, wrapping
	inline int32_t asl(const int32_t _v, const int _n)
	{
		return static_cast<int32_t>(static_cast<uint32_t>(_v) << _n);
	}

	// asr.l Dn and asl.l Dn: a register's count is taken mod 64, and 32 or more shifts everything out
	inline int32_t asrReg(const int32_t _v, const int32_t _n)
	{
		const int32_t n = _n & 63;
		return n >= 32 ? (_v < 0 ? -1 : 0) : _v >> n;
	}

	inline int32_t aslReg(const int32_t _v, const int32_t _n)
	{
		const int32_t n = _n & 63;
		return n >= 32 ? 0 : asl(_v, n);
	}

	// The step of a ramp over a 32-sample block, three ways the firmware writes it:
	// target - current wrapping, or saturated, then asr #5 (rounding down), or saturated and rounded toward zero
	inline int32_t rampStepWrap(const int32_t _target, const int32_t _current) { return subWrap(_target, _current) >> 5; }
	inline int32_t rampStepSat(const int32_t _target, const int32_t _current) { return subSat(_target, _current) >> 5; }

	inline int32_t rampStepToZero(const int32_t _target, const int32_t _current)
	{
		const int32_t d = subSat(_target, _current);
		return (d < 0 ? d + 0x1f : d) >> 5;
	}
}
