#pragma once

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "edDrumsEngine.h"

// A Track's Hit rendered apart from the engine that plays (ticket 37 of .scratch/elektron-drums-v2): on an engine of its
// own, on a thread of its own, the latest request alone. The engine for the next request is built ahead while none waits,
// and freed after IdleSeconds without one. Without JUCE
namespace elektron::drums
{
	class HitPreview
	{
	public:
		static constexpr int Rate = 44100;
		static constexpr int MinLength = Rate / 20, MaxLength = 2 * Rate;	// 50 ms to 2 s
		static constexpr float Quiet = 0.001f;									// -60 dBFS
		static constexpr double IdleSeconds = 30;

		// What a request rendered: Main's mid from where the Hit sounds (its engine's hitDelay), up to its last sample above
		// Quiet, within MinLength and MaxLength; whether either side passed full scale
		struct Picture
		{
			uint64_t serial = 0;
			std::vector<float> mid;
			bool clipped = false;
		};

		// _prepare gives a fresh engine at its defaults the sound to play on _track; it runs on the preview's thread and
		// holds what it reads (the values, the samples) by value
		struct Request
		{
			int track = 0;
			std::function<void(Engine&)> prepare;
		};

		explicit HitPreview(const Library& _library);
		~HitPreview();
		HitPreview(const HitPreview&) = delete;
		HitPreview& operator=(const HitPreview&) = delete;

		// Any thread: replaces a request not yet taken; its serial
		uint64_t request(Request _request);
		uint64_t requested() const;

		// The last picture rendered, or nullptr
		std::shared_ptr<const Picture> picture() const;

		// What a fresh engine plays as a Hit of _track after _prepare: the picture of serial 0. The thread's own work, open to
		// tests
		static Picture render(Engine& _engine, int _track);

		// Tests: the requests rendered, and whether an engine waits built ahead
		int rendered() const;
		bool ahead() const;

	private:
		void run();
		std::unique_ptr<Engine> build() const;		// nullptr where it could not be made

		const Library& m_library;
		mutable std::mutex m_mutex;
		std::condition_variable m_wake;
		bool m_quit = false;
		std::unique_ptr<Request> m_pending;
		uint64_t m_serial = 0;
		std::shared_ptr<const Picture> m_picture;
		std::unique_ptr<Engine> m_next;
		int m_rendered = 0;
		std::thread m_thread;
	};
}
