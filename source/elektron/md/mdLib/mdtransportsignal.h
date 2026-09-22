#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>

namespace md
{
	// Edge-triggered wake between the audio thread and a DSP worker
	// (parallel-transport spec §2/§3). Publishers call notify() after every
	// atomic publication; it costs one relaxed load unless somebody is
	// parked, so the steady state never touches the mutex (the lesson of the
	// per-quantum handoff prototype). Waiters re-check their predicate on
	// every wake and bound the wait so a stalled peer can never hang them.
	class TransportSignal
	{
	public:
		void notify()
		{
			if(m_waiters.load(std::memory_order_acquire) == 0)
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
			std::unique_lock<std::mutex> lock(m_mutex);
			m_waiters.fetch_add(1, std::memory_order_acq_rel);
			const bool ready = m_cv.wait_for(lock, _timeout, [&]
			{
				return _ready();
			});
			m_waiters.fetch_sub(1, std::memory_order_acq_rel);
			return ready;
		}

	private:
		std::mutex m_mutex;
		std::condition_variable m_cv;
		std::atomic<uint32_t> m_waiters{0};
		uint64_t m_generation = 0;
	};
}
