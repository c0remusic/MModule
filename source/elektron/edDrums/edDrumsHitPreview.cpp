#include "edDrumsHitPreview.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>

namespace elektron::drums
{
	HitPreview::HitPreview(const Library& _library) : m_library(_library), m_thread([this] { run(); })
	{
	}

	HitPreview::~HitPreview()
	{
		{
			const std::lock_guard<std::mutex> lock(m_mutex);
			m_quit = true;
		}
		m_wake.notify_one();
		m_thread.join();
	}

	uint64_t HitPreview::request(Request _request)
	{
		uint64_t serial;
		{
			const std::lock_guard<std::mutex> lock(m_mutex);
			m_pending = std::make_unique<Request>(std::move(_request));
			serial = ++m_serial;
		}
		m_wake.notify_one();
		return serial;
	}

	uint64_t HitPreview::requested() const
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		return m_serial;
	}

	std::shared_ptr<const HitPreview::Picture> HitPreview::picture() const
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		return m_picture;
	}

	int HitPreview::rendered() const
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		return m_rendered;
	}

	bool HitPreview::ahead() const
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		return m_next != nullptr;
	}

	HitPreview::Picture HitPreview::render(Engine& _engine, const int _track)
	{
		constexpr int Block = 1024;
		const int delay = _engine.hitDelay();
		const int total = delay + MaxLength;
		std::vector<float> left(static_cast<size_t>(total)), right(static_cast<size_t>(total));
		std::array<float*, 2 * Outputs> out{};
		_engine.noteOn(_track, 100);
		// Blocks until the Hit has ended: its voice off and what the engine holds back of it out (delay more samples), or
		// MaxLength rendered
		int done = 0, end = total;
		while(done < end)
		{
			const int count = std::min(Block, end - done);
			out[0] = left.data() + done;
			out[1] = right.data() + done;
			_engine.render(out.data(), count);
			done += count;
			if(end == total && done > delay + MinLength && !(_engine.liveTracks() >> _track & 1))
				end = std::min(total, done + delay);
		}
		Picture p;
		int last = delay;
		for(int n = delay; n < done; ++n)
		{
			const float l = std::abs(left[static_cast<size_t>(n)]), r = std::abs(right[static_cast<size_t>(n)]);
			if(l > Quiet || r > Quiet)
				last = n;
			p.clipped |= l > 1 || r > 1;
		}
		const int length = std::clamp(last + 1 - delay, MinLength, MaxLength);
		p.mid.resize(static_cast<size_t>(length));
		for(int n = 0; n < length && delay + n < done; ++n)
		{
			const auto at = static_cast<size_t>(delay + n);
			p.mid[static_cast<size_t>(n)] = 0.5f * (left[at] + right[at]);
		}
		return p;
	}

	void HitPreview::run()
	{
		std::unique_lock<std::mutex> lock(m_mutex);
		while(!m_quit)
		{
			if(!m_pending)
			{
				// None waits: an engine built ahead, then freed after IdleSeconds without a request
				if(!m_next)
				{
					lock.unlock();
					auto next = build();
					lock.lock();
					if(!next)
					{
						m_wake.wait(lock, [this] { return m_quit || m_pending; });
						continue;
					}
					m_next = std::move(next);
					continue;
				}
				const auto woken = [this] { return m_quit || m_pending; };
				if(!m_wake.wait_for(lock, std::chrono::duration<double>(IdleSeconds), woken))
				{
					auto idle = std::move(m_next);
					lock.unlock();
					idle.reset();
					lock.lock();
					m_wake.wait(lock, woken);
				}
				continue;
			}
			auto request = std::move(m_pending);
			const uint64_t serial = m_serial;
			auto engine = std::move(m_next);
			lock.unlock();
			// Nothing leaves the thread: a request that fails keeps the last picture
			std::shared_ptr<Picture> picture;
			try
			{
				if(!engine)
					engine = std::make_unique<Engine>(m_library);
				request->prepare(*engine);
				picture = std::make_shared<Picture>(render(*engine, request->track));
				picture->serial = serial;
			}
			catch(const std::exception&)
			{
			}
			engine.reset();
			lock.lock();
			if(picture)
				m_picture = std::move(picture);
			++m_rendered;
		}
	}

	std::unique_ptr<Engine> HitPreview::build() const
	{
		try
		{
			return std::make_unique<Engine>(m_library);
		}
		catch(const std::exception&)
		{
			return nullptr;
		}
	}
}
