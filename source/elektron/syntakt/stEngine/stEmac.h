#pragma once

#include <cassert>
#include <cstdint>

// Inlined whatever the compiler's heuristics say: the mix's and the memory's hottest calls, which MSVC leaves as calls
#ifndef ST_FORCE_INLINE
#	ifdef _MSC_VER
#		define ST_FORCE_INLINE __forceinline
#	else
#		define ST_FORCE_INLINE inline __attribute__((always_inline))
#	endif
#endif

namespace syntakt
{
	// (y x) << 1 truncated to Q39, what mac.l adds and msac.l subtracts in fractional mode: never overflows int64,
	// since |y x| <= 2^62, and at most 2^39 in magnitude
	inline int64_t fractionalProduct(const int32_t _y, const int32_t _x)
	{
		return (static_cast<int64_t>(_y) * _x) >> 23;
	}

	// A run of terms summed at once from a clear accumulator (sumRun) leaves it where the steps leave it while no
	// partial sum reaches the 2^47 where an accumulator saturates or wraps. Each term is at most 2^39 in magnitude (a
	// fractional product, or a word loaded, x 256), so a run of at most 255 never does: 256 x 2^39 is 2^47
	constexpr int MaxRunTerms = 255;
	constexpr int64_t MaxRunTerm = int64_t(1) << 39;

	// One step of a run: a mac.l or an msac.l of y and x
	struct Mac
	{
		int32_t y;
		int32_t x;
	};

	struct Msac
	{
		int32_t y;
		int32_t x;
	};

	// What a step adds to the accumulator in fractional mode
	inline int64_t runTerm(const Mac& _m) { return fractionalProduct(_m.y, _m.x); }
	inline int64_t runTerm(const Msac& _m) { return -fractionalProduct(_m.y, _m.x); }

	// A run's sum built a term at a time, for a run a loop sums (sumRun): debug builds count its terms and check
	// each one's bound
	class RunSum
	{
	public:
		ST_FORCE_INLINE void add(const int64_t _term)
		{
			assert(_term >= -MaxRunTerm && _term <= MaxRunTerm);
#ifndef NDEBUG
			assert(++m_count <= MaxRunTerms);
#endif
			m_sum += _term;
		}

		int64_t sum() const { return m_sum; }

	private:
		int64_t m_sum = 0;
#ifndef NDEBUG
		int m_count = 0;
#endif
	};

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
		int32_t store() const { return storeOf(m_acc); }

		// What move.l ACC,Rx gives of an accumulator holding _acc
		static int32_t storeOf(const int64_t _acc)
		{
			const int64_t v = _acc >> 8;
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

		// The accumulator holds 0 and has not overflowed, as a movclr leaves it
		bool clear() const { return m_acc == 0 && !m_overflow; }

		// The mac.l and msac.l of a run (Mac, Msac), in order: from a clear accumulator at once (sumRun), else step
		// by step
		template<typename... Steps>
		ST_FORCE_INLINE void macs(const Steps&... _steps)
		{
			if(clear())
				sumRun(runTerm(_steps)...);
			else
				(step(_steps), ...);
		}

		// move.l, then the mac.l and msac.l of a run, at once: the load leaves no overflow and the word x 256, a
		// term as a product is
		template<typename... Steps>
		ST_FORCE_INLINE void loadMacs(const int32_t _v, const Steps&... _steps)
		{
			m_acc = 0;
			m_overflow = false;
			sumRun(static_cast<int64_t>(_v) * 256, runTerm(_steps)...);
		}

		// The accumulator after a run of steps from clear, given what each adds (runTerm): their sum, since no
		// partial sum of at most MaxRunTerms reaches the 2^47 where Emac saturates and EmacWrap wraps. Debug builds
		// check the run's length, each term's bound and that the accumulator is clear
		template<typename... Terms>
		ST_FORCE_INLINE void sumRun(const Terms... _terms)
		{
			static_assert(sizeof...(Terms) <= MaxRunTerms, "a run that may overflow");
			assert(clear());
			assert(((_terms >= -MaxRunTerm && _terms <= MaxRunTerm) && ...));
			m_acc = (int64_t(0) + ... + _terms);
		}

	private:
		void step(const Mac& _m) { mac(_m.y, _m.x); }
		void step(const Msac& _m) { msac(_m.y, _m.x); }

		static int64_t product(const int32_t _y, const int32_t _x) { return fractionalProduct(_y, _x); }

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

	// EmacT for a loop whose structure proves that no step overflows: the accumulator starts clear and is cleared or
	// loaded again within MaxRunTerms steps. A mac.l or msac.l then adds its product alone, without the overflow
	// test and the 48-bit check (sumRun's argument, step by step), and a store is EmacT's. Debug builds count the
	// steps
	template<bool Saturate>
	class ProvedEmacT
	{
	public:
		void load(const int32_t _v)
		{
			m_acc = static_cast<int64_t>(_v) * 256;
			count(1);
		}

		ST_FORCE_INLINE void mac(const int32_t _y, const int32_t _x) { add(runTerm(Mac{_y, _x})); }
		ST_FORCE_INLINE void msac(const int32_t _y, const int32_t _x) { add(runTerm(Msac{_y, _x})); }

		int32_t store() const { return EmacT<Saturate>::storeOf(m_acc); }

		int32_t storeClear()
		{
			const int32_t v = store();
			m_acc = 0;
			count(0);
			return v;
		}

		template<typename... Steps>
		ST_FORCE_INLINE void loadMacs(const int32_t _v, const Steps&... _steps)
		{
			load(_v);
			(add(runTerm(_steps)), ...);
		}

	private:
		ST_FORCE_INLINE void add(const int64_t _term)
		{
			m_acc += _term;
#ifndef NDEBUG
			assert(++m_steps <= MaxRunTerms);
#endif
		}

		void count(const int _steps)
		{
#ifndef NDEBUG
			m_steps = _steps;
#else
			(void)_steps;
#endif
		}

		int64_t m_acc = 0;
#ifndef NDEBUG
		int m_steps = 0;
#endif
	};

	using ProvedEmac = ProvedEmacT<true>;
	using ProvedEmacWrap = ProvedEmacT<false>;

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
