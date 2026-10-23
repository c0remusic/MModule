#pragma once

#include <cstdint>

#include "stEmac.h"

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
		int32_t store(const int _n) const { return storeOf(m_acc[_n], m_macsr); }

		// What move.l ACCn,Rx gives of an accumulator holding _acc at MACSR _macsr
		static int32_t storeOf(const int64_t _acc, const uint32_t _macsr)
		{
			const int64_t v = (_macsr & Fractional) ? _acc >> 8 : _acc;
			if(_macsr & Omc)
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

		bool fractional() const { return (m_macsr & Fractional) != 0; }

		// Accumulator _n holds 0 and has not overflowed, as a movclr leaves it
		bool clear(const int _n) const { return m_acc[_n] == 0 && !m_pav[_n]; }
		bool allClear() const { return clear(0) && clear(1) && clear(2) && clear(3); }

		// The mac.l and msac.l of a run (Mac, Msac) on accumulator _n, in order: at once from a clear accumulator in
		// fractional mode (sumRun), else step by step
		template<typename... Steps>
		ST_FORCE_INLINE void macs(const int _n, const Steps&... _steps)
		{
			if(fractional() && clear(_n))
				sumRun(_n, runTerm(_steps)...);
			else
				(step(_n, _steps), ...);
		}

		// move.l, then the mac.l and msac.l of a run: at once in fractional mode, where the load leaves PAV clear
		// and the word x 256, a term as a product is; else step by step
		template<typename... Steps>
		ST_FORCE_INLINE void loadMacs(const int _n, const int32_t _v, const Steps&... _steps)
		{
			if(!fractional())
			{
				load(_n, _v);
				(step(_n, _steps), ...);
				return;
			}
			m_acc[_n] = 0;
			m_pav[_n] = false;
			sumRun(_n, static_cast<int64_t>(_v) * 256, runTerm(_steps)...);
		}

		// Accumulator _n after a run of steps from clear, given what each adds (runTerm in fractional mode, y x in
		// integer mode, negated for msac.l): their sum. Each term is at most 2^39 in magnitude in fractional mode
		// and under it in integer mode (where a product's own check sets PAV at 2^39), so no partial sum of at most
		// MaxRunTerms reaches the 2^47 the steps check: none sets PAV or saturates. Debug builds check the run's
		// length, each term's bound and that the accumulator is clear
		template<typename... Terms>
		ST_FORCE_INLINE void sumRun(const int _n, const Terms... _terms)
		{
			static_assert(sizeof...(Terms) <= MaxRunTerms, "a run that may overflow");
			assert(clear(_n));
			assert((inRun(_terms) && ...));
			m_acc[_n] = (int64_t(0) + ... + _terms);
		}

		// The same for a run summed in a loop, in fractional mode
		ST_FORCE_INLINE void sumRun(const int _n, const RunSum& _run)
		{
			assert(fractional());
			assert(clear(_n));
			m_acc[_n] = _run.sum();
		}

	private:
		void step(const int _n, const Mac& _m) { mac(_n, _m.y, _m.x); }
		void step(const int _n, const Msac& _m) { msac(_n, _m.y, _m.x); }

		bool inRun(const int64_t _term) const
		{
			if(fractional())
				return _term >= -MaxRunTerm && _term <= MaxRunTerm;
			return _term > -MaxRunTerm && _term < MaxRunTerm;
		}

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
				const int64_t p = fractionalProduct(_y, _x);
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

		friend class ProvedEmac4;

		int64_t m_acc[4] = {};
		bool m_pav[4] = {};
		uint32_t m_macsr = Fractional;
	};

	// Emac4 for a routine whose structure proves that no step overflows: it starts in fractional mode with every
	// accumulator clear, stays fractional, and clears or loads each accumulator again within MaxRunTerms steps. Each
	// mac.l and msac.l then adds its product alone, no PAV to test and no 48-bit range to check (sumRun's argument,
	// step by step); a store saturates as OMC says. Debug builds count each accumulator's steps. The routine takes
	// the Emac4 it was given back with done()
	class ProvedEmac4
	{
	public:
		explicit ProvedEmac4(Emac4& _acc) : m_emac(_acc), m_macsr(_acc.macsr())
		{
			assert(proves(_acc));
		}

		// Where the routines' proof holds at their start
		static bool proves(const Emac4& _acc) { return _acc.fractional() && _acc.allClear(); }

		uint32_t macsr() const { return m_macsr; }

		void setMacsr(const uint32_t _v)
		{
			assert(_v & Emac4::Fractional);
			m_macsr = _v;
		}

		bool fractional() const { return true; }

		ST_FORCE_INLINE void mac(const int _n, const int32_t _y, const int32_t _x) { add(_n, runTerm(Mac{_y, _x})); }
		ST_FORCE_INLINE void msac(const int _n, const int32_t _y, const int32_t _x) { add(_n, runTerm(Msac{_y, _x})); }

		template<typename... Steps>
		ST_FORCE_INLINE void macs(const int _n, const Steps&... _steps)
		{
			(add(_n, runTerm(_steps)), ...);
		}

		void load(const int _n, const int32_t _v)
		{
			m_acc[_n] = static_cast<int64_t>(_v) * 256;
			count(_n, true);
		}

		template<typename... Steps>
		ST_FORCE_INLINE void loadMacs(const int _n, const int32_t _v, const Steps&... _steps)
		{
			load(_n, _v);
			macs(_n, _steps...);
		}

		ST_FORCE_INLINE int32_t store(const int _n) const { return Emac4::storeOf(m_acc[_n], m_macsr); }

		ST_FORCE_INLINE int32_t storeClear(const int _n)
		{
			const int32_t v = store(_n);
			m_acc[_n] = 0;
			count(_n, false);
			return v;
		}

		// The Emac4 given, as the steps would leave it: the accumulators and MACSR, none overflowed
		void done()
		{
			for(int n = 0; n < 4; ++n)
			{
				m_emac.m_acc[n] = m_acc[n];
				m_emac.m_pav[n] = false;
			}
			m_emac.m_macsr = m_macsr;
		}

	private:
		ST_FORCE_INLINE void add(const int _n, const int64_t _term)
		{
			m_acc[_n] += _term;
#ifndef NDEBUG
			assert(++m_steps[_n] <= MaxRunTerms);
#endif
		}

		// A movclr leaves no step since, a load one term
		void count(const int _n, const bool _loaded)
		{
#ifndef NDEBUG
			m_steps[_n] = _loaded ? 1 : 0;
#else
			(void)_n;
			(void)_loaded;
#endif
		}

		Emac4& m_emac;
		int64_t m_acc[4] = {};
		uint32_t m_macsr;
#ifndef NDEBUG
		int m_steps[4] = {};
#endif
	};
}
