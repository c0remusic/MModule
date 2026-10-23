#pragma once

#include <cstdint>

// Inlined whatever the compiler's heuristics say: the mix's and the memory's hottest calls, which MSVC leaves as calls
#ifndef ST_FORCE_INLINE
#	ifdef _MSC_VER
#		define ST_FORCE_INLINE __forceinline
#	else
#		define ST_FORCE_INLINE inline __attribute__((always_inline))
#	endif
#endif

// CPU #1's EMAC as its mix runs it (cpu1.md, section 4): four accumulators and a MACSR the routines change as they go
// (0x20, 0xa0, 0x80), so that what one routine leaves in an accumulator reaches the next, as on the CPU. A product
// is (y x) << 1 truncated to Q39 in fractional mode (MACSR bit 5), y x in integer mode; OMC (bit 7) saturates, and an
// accumulator that overflowed then holds until it is loaded or cleared (its PAV flag). Rounding and the unsigned and
// 16-bit store modes are not used here. As tools/emu/emac.py of SyntHack, which the oracle runs
namespace syntakt::main
{
	class Emac4
	{
	public:
		static constexpr uint32_t Fractional = 0x20;
		static constexpr uint32_t Omc = 0x80;

		uint32_t macsr() const { return m_macsr; }
		void setMacsr(const uint32_t _v) { m_macsr = _v; }

		// mac.l Ry,Rx,ACCn and msac.l
		void mac(const int _n, const int32_t _y, const int32_t _x) { accumulate(_n, _y, _x, false); }
		void msac(const int _n, const int32_t _y, const int32_t _x) { accumulate(_n, _y, _x, true); }

		// move.l Ry,ACCn
		void load(const int _n, const int32_t _v)
		{
			m_acc[_n] = fractional() ? static_cast<int64_t>(_v) * 256 : _v;
			m_pav[_n] = false;
		}

		// move.l ACCn,Rx
		int32_t store(const int _n) const
		{
			const int64_t v = fractional() ? m_acc[_n] >> 8 : m_acc[_n];
			if(m_macsr & Omc)
			{
				if(v > INT32_MAX)
					return INT32_MAX;
				if(v < INT32_MIN)
					return INT32_MIN;
			}
			return static_cast<int32_t>(static_cast<uint32_t>(v));
		}

		// movclr.l ACCn,Rx
		int32_t storeClear(const int _n)
		{
			const int32_t v = store(_n);
			m_acc[_n] = 0;
			m_pav[_n] = false;
			return v;
		}

	private:
		bool fractional() const { return (m_macsr & Fractional) != 0; }

		static int64_t wrap48(const int64_t _v)
		{
			return static_cast<int64_t>(static_cast<uint64_t>(_v) << 16) >> 16;
		}

		ST_FORCE_INLINE void accumulate(const int _n, const int32_t _y, const int32_t _x, const bool _sub)
		{
			const bool omc = (m_macsr & Omc) != 0;
			if(omc && m_pav[_n])
				return;
			m_pav[_n] = false;
			constexpr int64_t limit = int64_t(1) << 47;
			int64_t r;
			if(fractional())
			{
				const int64_t p = (static_cast<int64_t>(_y) * _x) >> 23;
				r = _sub ? m_acc[_n] - p : m_acc[_n] + p;
				if(r >= limit || r < -limit)
				{
					m_pav[_n] = true;
					r = omc ? (r > 0 ? 0x007fffffff00ll : -(int64_t(1) << 39)) : wrap48(r);
				}
			}
			else
			{
				const int64_t p = static_cast<int64_t>(_y) * _x;
				if(p >= (int64_t(1) << 39) || p < -(int64_t(1) << 39))
				{
					m_pav[_n] = true;
					if(omc)
					{
						m_acc[_n] = (p < 0) != _sub ? INT32_MIN : INT32_MAX;
						return;
					}
				}
				r = _sub ? m_acc[_n] - p : m_acc[_n] + p;
				if(r >= limit || r < -limit)
				{
					m_pav[_n] = true;
					r = omc ? (r >= limit ? INT32_MAX : INT32_MIN) : wrap48(r);
				}
			}
			m_acc[_n] = r;
		}

		int64_t m_acc[4] = {};
		bool m_pav[4] = {};
		uint32_t m_macsr = Fractional;
	};
}
