#include "mdasyncrender.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>

#include "dsp56kBase/threadtools.h"

namespace md
{
	namespace
	{
		constexpr size_t g_reserveFrames = 8192;
		constexpr size_t g_reserveMidi = 256;
	}

	AsyncRender::AsyncRender(RenderFunc _render) : m_render(std::move(_render))
	{
		if(const char* const spin = std::getenv("MDMM_RENDER_SPIN_US"))
			m_idleSpin = std::chrono::microseconds(std::strtoul(spin, nullptr, 10));
		for(auto& job : m_jobs)
		{
			for(auto& in : job.in)
				in.reserve(g_reserveFrames);
			job.midiIn.reserve(g_reserveMidi);
			job.midiOut.reserve(g_reserveMidi);
		}
		for(auto& out : m_renderOut)
			out.reserve(g_reserveFrames);
		for(auto& fifo : m_fifo)
			fifo.assign(FifoFrames, 0.0f);
	}

	AsyncRender::~AsyncRender()
	{
		stop();
	}

	void AsyncRender::start(const uint32_t _latency)
	{
		stop();
		m_submitted.store(0, std::memory_order_relaxed);
		m_done.store(0, std::memory_order_relaxed);
		m_harvested = 0;
		for(auto& fifo : m_fifo)
			std::fill(fifo.begin(), fifo.end(), 0.0f);
		// The latency is served as silence before the first rendered frame.
		const auto latency = std::min<size_t>(_latency, FifoFrames / 2);
		m_fifoRead.store(0, std::memory_order_relaxed);
		m_fifoWrite.store(latency, std::memory_order_relaxed);
		m_exit.store(false, std::memory_order_release);
		m_thread = std::thread([this] { threadFunc(); });
	}

	void AsyncRender::stop()
	{
		if(!m_thread.joinable())
			return;
		// The thread renders every job already handed over before it exits,
		// so the device is idle and consistent afterwards.
		m_exit.store(true, std::memory_order_release);
		m_jobSignal.notify();
		m_thread.join();
		for(auto& job : m_jobs)
		{
			job.midiIn.clear();
			job.midiOut.clear();
		}
	}

	void AsyncRender::waitIdle()
	{
		while(!isIdle())
			m_doneSignal.waitFor(std::chrono::milliseconds(10), [this] { return isIdle(); });
	}

	void AsyncRender::harvest(const size_t _frames, std::vector<synthLib::SMidiEvent>& _midiOut)
	{
		// MIDI produced while rendering an earlier block leaves with the block
		// that plays its audio; its offset is clamped into this block.
		const uint64_t done = m_done.load(std::memory_order_acquire);
		while(m_harvested < done)
		{
			auto& job = m_jobs[m_harvested % JobCount];
			for(auto& ev : job.midiOut)
			{
				ev.offset = std::min<uint32_t>(ev.offset, _frames ? static_cast<uint32_t>(_frames - 1) : 0);
				_midiOut.push_back(std::move(ev));
			}
			job.midiOut.clear();
			++m_harvested;
		}
	}

	void AsyncRender::process(const synthLib::TAudioInputs& _inputs, const synthLib::TAudioOutputs& _outputs,
		const size_t _frames, const std::vector<synthLib::SMidiEvent>& _midiIn,
		std::vector<synthLib::SMidiEvent>& _midiOut)
	{
		const auto nowNs = []
		{
			return std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::steady_clock::now().time_since_epoch()).count();
		};
		auto& timeline = m_timeline[m_submitted.load(std::memory_order_relaxed) % JobTimeCount];
		timeline.hostEntry = nowNs();
		_midiOut.clear();
		harvest(_frames, _midiOut);
		{
			const uint64_t backlog = m_submitted.load(std::memory_order_relaxed) - m_done.load(std::memory_order_acquire);
			m_statBacklogSum.fetch_add(backlog, std::memory_order_relaxed);
			if(backlog > m_statBacklogMax.load(std::memory_order_relaxed))
				m_statBacklogMax.store(backlog, std::memory_order_relaxed);
		}

		// Hand this block over. A slot is reused only once its MIDI output was
		// harvested; all slots busy means the renderer is JobCount blocks late.
		const uint64_t submitted = m_submitted.load(std::memory_order_relaxed);
		while(submitted - m_harvested >= JobCount)
		{
			m_doneSignal.waitFor(std::chrono::milliseconds(10), [&]
			{
				return m_done.load(std::memory_order_acquire) > m_harvested;
			});
			harvest(_frames, _midiOut);
		}
		auto& job = m_jobs[submitted % JobCount];
		for(size_t c = 0; c < ChannelsIn; ++c)
		{
			job.in[c].resize(_frames);
			if(_inputs[c])
				std::copy_n(_inputs[c], _frames, job.in[c].data());
			else
				std::fill_n(job.in[c].data(), _frames, 0.0f);
		}
		job.midiIn.assign(_midiIn.begin(), _midiIn.end());
		job.frames = _frames;
		job.submittedAt = std::chrono::duration_cast<std::chrono::nanoseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count();
		m_submitted.store(submitted + 1, std::memory_order_release);
		m_jobSignal.notify();

		// Take this block's audio: rendered earlier, or the initial silence.
		if(fifoAvailable() < _frames)
		{
			m_lateBlocks.fetch_add(1, std::memory_order_relaxed);
			const auto waitStart = std::chrono::steady_clock::now();
			while(fifoAvailable() < _frames)
				m_doneSignal.waitFor(std::chrono::milliseconds(10), [&] { return fifoAvailable() >= _frames; });
			m_statWaitNs.fetch_add(static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::steady_clock::now() - waitStart).count()), std::memory_order_relaxed);
		}
		timeline.hostResume = nowNs();
		const uint64_t read = m_fifoRead.load(std::memory_order_relaxed);
		for(size_t c = 0; c < ChannelsOut; ++c)
		{
			if(!_outputs[c])
				continue;
			const auto& fifo = m_fifo[c];
			for(size_t i = 0; i < _frames; ++i)
				_outputs[c][i] = fifo[(read + i) & (FifoFrames - 1)];
		}
		m_fifoRead.store(read + _frames, std::memory_order_release);
		m_jobSignal.notify();	// room in the FIFO for a renderer waiting on it
		harvest(_frames, _midiOut);
	}

	void AsyncRender::threadFunc()
	{
		// Same scheduling band as the host's audio threads (MMCSS "Pro Audio"):
		// the host waits on this thread whenever it falls behind.
		void* const task = dsp56k::ThreadTools::joinProAudioTask();
		dsp56k::ThreadTools::setCurrentThreadName("MD render");
		for(;;)
		{
			const uint64_t index = m_done.load(std::memory_order_relaxed);
			if(m_submitted.load(std::memory_order_acquire) == index)
			{
				if(m_exit.load(std::memory_order_acquire))
					break;
				const auto idleStart = std::chrono::steady_clock::now();
				const auto jobReady = [&]
				{
					return m_exit.load(std::memory_order_acquire)
						|| m_submitted.load(std::memory_order_acquire) != index;
				};
				// The next block arrives within one host period: waiting for it
				// on a CPU costs that core but no wake-up latency, which would
				// otherwise eat into the one period the render has.
				while(!jobReady() && std::chrono::steady_clock::now() - idleStart < m_idleSpin)
				{
					for(int i = 0; i < 64 && !jobReady(); ++i)
						TransportSignal::cpuPause();
				}
				if(!jobReady())
					m_jobSignal.waitFor(std::chrono::milliseconds(100), jobReady);
				m_statIdleNs.fetch_add(static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
					std::chrono::steady_clock::now() - idleStart).count()), std::memory_order_relaxed);
				continue;
			}
			auto& job = m_jobs[index % JobCount];
			synthLib::TAudioInputs ins{};
			for(size_t c = 0; c < ChannelsIn; ++c)
				ins[c] = job.in[c].data();
			synthLib::TAudioOutputs outs{};
			for(size_t c = 0; c < ChannelsOut; ++c)
			{
				m_renderOut[c].resize(job.frames);
				outs[c] = m_renderOut[c].data();
			}
			job.midiOut.clear();
			const auto renderStart = std::chrono::steady_clock::now();
			m_statQueueNs.fetch_add(static_cast<uint64_t>(std::max<int64_t>(0,
				std::chrono::duration_cast<std::chrono::nanoseconds>(renderStart.time_since_epoch()).count()
				- job.submittedAt)), std::memory_order_relaxed);
			m_render(ins, outs, job.frames, job.midiIn, job.midiOut);
			const auto renderEnd = std::chrono::steady_clock::now();
			const auto renderNs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
				renderEnd - renderStart).count());
			{
				auto& timeline = m_timeline[index % JobTimeCount];
				timeline.renderStart = std::chrono::duration_cast<std::chrono::nanoseconds>(renderStart.time_since_epoch()).count();
				timeline.renderEnd = std::chrono::duration_cast<std::chrono::nanoseconds>(renderEnd.time_since_epoch()).count();
			}
			m_statRenderNs.fetch_add(renderNs, std::memory_order_relaxed);
			const auto jobNumber = m_statJobs.fetch_add(1, std::memory_order_relaxed);
			m_jobTimesNs[jobNumber % JobTimeCount] = static_cast<uint32_t>(std::min<uint64_t>(renderNs, 0xffffffffu));
			if(job.frames > m_statMaxFrames.load(std::memory_order_relaxed))
				m_statMaxFrames.store(job.frames, std::memory_order_relaxed);

			// The host drains the FIFO every block; it only fills up if the host
			// stops calling, and then the thread waits (or leaves on exit).
			while(FifoFrames - fifoAvailable() < job.frames && !m_exit.load(std::memory_order_acquire))
			{
				m_jobSignal.waitFor(std::chrono::milliseconds(10), [&]
				{
					return FifoFrames - fifoAvailable() >= job.frames || m_exit.load(std::memory_order_acquire);
				});
			}
			const uint64_t write = m_fifoWrite.load(std::memory_order_relaxed);
			for(size_t c = 0; c < ChannelsOut; ++c)
			{
				auto& fifo = m_fifo[c];
				const auto& out = m_renderOut[c];
				for(size_t i = 0; i < job.frames; ++i)
					fifo[(write + i) & (FifoFrames - 1)] = out[i];
			}
			m_fifoWrite.store(write + job.frames, std::memory_order_release);
			m_done.store(index + 1, std::memory_order_release);
			m_doneSignal.notify();
		}
		dsp56k::ThreadTools::leaveProAudioTask(task);
	}
}
