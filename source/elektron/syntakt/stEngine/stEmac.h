#pragma once

#include <cstdint>

namespace syntakt
{
	// The ColdFire EMAC as the Syntakt's voice CPU runs it: MACSR = 0xA0 (signed fractional, saturating on overflow,
	// truncating), written once before any engine (0x4000433c). An accumulator holds a Q39 value in 48 bits; a load
	// shifts a Q31 word up by 8, a product is truncated to Q39, a store truncates back to Q31 and saturates.
	// The per-track stages after the voices run two routines with MACSR = 0x20 instead (fractional, no saturation): a
	// store then wraps to 32 bits and an overflowing accumulator wraps to 48. Emac(false) is that mode.
	class Emac
	{
	public:
		explicit Emac(const bool _saturate = true) : m_saturate(_saturate) {}

		void load(const int32_t _v)
		{
			m_acc = static_cast<int64_t>(_v) * 256;
			m_overflow = false;
		}

		// mac.l: acc += y * x (fractional). Frozen while an earlier overflow is pending, as with MACSR.OMC.
		void mac(const int32_t _y, const int32_t _x) { accumulate(product(_y, _x)); }

		// msac.l: acc -= y * x
		void msac(const int32_t _y, const int32_t _x) { accumulate(-product(_y, _x)); }

		// move.l ACC,Rx
		int32_t store() const
		{
			const int64_t v = m_acc >> 8;
			if(!m_saturate)
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
			if(m_overflow)
				return;
			const int64_t r = m_acc + _p;
			constexpr int64_t limit = int64_t(1) << 47;
			if(!m_saturate)
			{
				// 48-bit wrap, sign-extended
				m_acc = static_cast<int64_t>(static_cast<uint64_t>(r) << 16) >> 16;
				return;
			}
			if(r >= limit || r < -limit)
			{
				m_overflow = true;
				m_acc = r > 0 ? 0x007FFFFFFF00ll : -(int64_t(1) << 39);
				return;
			}
			m_acc = r;
		}

		int64_t m_acc = 0;
		bool m_overflow = false;
		bool m_saturate = true;
	};

	// One product alone, as `mac` into a cleared accumulator then `movclr`
	inline int32_t frac(const int32_t _y, const int32_t _x)
	{
		Emac e;
		e.mac(_y, _x);
		return e.storeClear();
	}

	// The same under MACSR 0x20: the store wraps
	inline int32_t fracWrap(const int32_t _y, const int32_t _x)
	{
		Emac e(false);
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
}
