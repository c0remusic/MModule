#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>

#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#include <intrin.h>
#elif defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#endif

namespace md
{
	// Edge-triggered wake between the audio thread and a DSP worker
	// (parallel-transport spec §2/§3). Publishers call notify() after every
	// atomic publication; it costs one fence and one load unless somebody is
	// parked, so the steady state never touches the mutex (the lesson of the
	// per-quantum handoff prototype). Waiters spin briefly before parking -
	// the peer usually publishes within microseconds, far below the cost of
	// a kernel wake - re-check their predicate on every wake and bound the
	// wait so a stalled peer can never hang them.
	class TransportSignal
	{
	public:
		// How long a waiter spins on its predicate before it parks.
		static constexpr std::chrono::microseconds SpinDuration{50};

		void notify()
		{
			// Dekker handshake with waitFor(): the publisher's store of its
			// position must be ordered before its read of the waiter count,
			// just as the waiter's registration is ordered before its
			// predicate check. Without the full fence the load can be
			// satisfied before the store is visible (StoreLoad reordering,
			// also on x86): the publisher sees no waiter, the waiter sees the
			// old position, and the wake is lost until the waiter's timeout.
			std::atomic_thread_fence(std::memory_order_seq_cst);
			if(m_waiters.load(std::memory_order_relaxed) == 0)
				return;
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				++m_generation;
			}
			m_cv.notify_all();
		}

		// Returns false when the deadline expired with the predicate still
		// false. The predicate reads atomics only and must be cheap.
		template<typename Predicate>
		bool waitFor(const std::chrono::microseconds _timeout, Predicate&& _ready)
		{
			if(_ready())
				return true;
			const auto start = std::chrono::steady_clock::now();
			const auto spin = std::min(_timeout, std::chrono::duration_cast<std::chrono::microseconds>(SpinDuration));
			for(uint32_t i = 0;; ++i)
			{
				pause();
				if(_ready())
					return true;
				if((i & 63) == 63 && std::chrono::steady_clock::now() - start >= spin)
					break;
			}
			std::unique_lock<std::mutex> lock(m_mutex);
			// The RMW is a full barrier: the registration is visible before
			// the predicate below reads the peer's position (see notify()).
			m_waiters.fetch_add(1, std::memory_order_seq_cst);
			const bool ready = m_cv.wait_for(lock, _timeout - std::chrono::duration_cast<std::chrono::microseconds>(
				std::chrono::steady_clock::now() - start), [&]
			{
				return _ready();
			});
			m_waiters.fetch_sub(1, std::memory_order_acq_rel);
			return ready;
		}

		// Spin-wait hint for callers that poll on their own.
		static void cpuPause() { pause(); }

	private:
		static void pause()
		{
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
			_mm_pause();
#elif defined(__x86_64__) || defined(__i386__)
			_mm_pause();
#elif defined(__aarch64__)
			asm volatile("yield");
#endif
		}

		std::mutex m_mutex;
		std::condition_variable m_cv;
		alignas(64) std::atomic<uint32_t> m_waiters{0};
		uint64_t m_generation = 0;
	};
}
