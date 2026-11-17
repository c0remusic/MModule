// What the cost benches share (edDrumsCost by hand, edDrumsWorstKitCost at the gate): an engine played in host blocks of
// 128 at 44.1 kHz, every Track retriggered with its 8 parameters locked every 15 blocks of 32 (the Syntakt's fastest
// retrig, 10 ms), timed block by block, and the load of the other processes (GetSystemTimes minus this process).
#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <random>
#include <vector>

#define NOMINMAX
#include <windows.h>

#include "edDrumsEngine.h"

namespace elektron::drums::bench
{
	using Clock = std::chrono::steady_clock;

	constexpr int HostBlock = 128;
	constexpr int RetrigSamples = 15 * 32;
	constexpr double BlockUs = HostBlock / SampleRate * 1e6;
	constexpr double Budget = 15;	// % of a core: the ceiling of the worst kit

	struct Sound
	{
		Machine machine;
		std::array<uint8_t, 8> values;
	};
	using Kit = std::array<Sound, Tracks>;

	// A machine's dearest sound and what it costs
	struct Cost
	{
		Sound sound;
		double pct;
	};

	struct Load
	{
		static uint64_t ticks(const FILETIME& _f) { return uint64_t(_f.dwHighDateTime) << 32 | _f.dwLowDateTime; }

		Load() { sample(m_idle, m_busy, m_self); }

		double otherCores() const
		{
			uint64_t idle, busy, self;
			sample(idle, busy, self);
			const double wall = std::chrono::duration<double>(Clock::now() - m_t0).count() * 1e7;
			return std::max(0.0, (static_cast<double>((busy - m_busy) - (idle - m_idle)) - static_cast<double>(self - m_self)) / wall);
		}

	private:
		static void sample(uint64_t& _idle, uint64_t& _busy, uint64_t& _self)
		{
			FILETIME idle, kernel, user, c, e, pk, pu;
			GetSystemTimes(&idle, &kernel, &user);
			GetProcessTimes(GetCurrentProcess(), &c, &e, &pk, &pu);
			_idle = ticks(idle);
			_busy = ticks(kernel) + ticks(user);
			_self = ticks(pk) + ticks(pu);
		}

		uint64_t m_idle = 0, m_busy = 0, m_self = 0;
		Clock::time_point m_t0 = Clock::now();
	};

	struct Result
	{
		double pct, worstUs, others;
	};

	// _engine, its Tracks on _kit's machines and values already, played for _seconds after up to a second of warm-up:
	// with _hits every Track retriggered on its values locked
	inline Result measure(Engine& _engine, const Kit& _kit, const bool _hits, const double _seconds)
	{
		std::array<std::array<float, HostBlock>, 2 * Outputs> buffers;
		std::array<float*, 2 * Outputs> p{};
		for(size_t c = 0; c < p.size(); ++c)
			p[c] = buffers[c].data();
		int64_t at = 0, nextHit = 0;
		const auto block = [&]
		{
			int done = 0;
			while(done < HostBlock)
			{
				if(_hits && at == nextHit)
				{
					for(int t = 0; t < Tracks; ++t)
					{
						for(int i = 0; i < 8; ++i)
							_engine.lock(t, i, _kit[static_cast<size_t>(t)].values[static_cast<size_t>(i)]);
						_engine.noteOn(t, 127);
					}
					nextHit += RetrigSamples;
				}
				const int run = static_cast<int>(std::min<int64_t>(HostBlock - done, _hits ? nextHit - at : HostBlock - done));
				std::array<float*, 2 * Outputs> q = p;
				for(auto*& x : q)
					x += done;
				_engine.render(q.data(), run);
				at += run;
				done += run;
			}
		};
		for(int b = 0; b < static_cast<int>(std::min(_seconds, 1.0) * SampleRate) / HostBlock; ++b)
			block();
		const auto blocks = static_cast<int64_t>(_seconds * SampleRate / HostBlock);
		double worst = 0, total = 0;
		const Load load;
		for(int64_t b = 0; b < blocks; ++b)
		{
			const auto t0 = Clock::now();
			block();
			const double us = std::chrono::duration<double, std::micro>(Clock::now() - t0).count();
			total += us;
			worst = std::max(worst, us);
		}
		return {100 * total / (static_cast<double>(blocks) * BlockUs), worst, load.otherCores()};
	}

	// One LFO's worth of what moves every block: a sine of 1/64, each Track's its own speed
	inline LfoSettings moving(const LfoTarget _target, const int _track)
	{
		LfoSettings s;
		s.rate.division = _track % 3;
		s.shape = LfoShape::Sine;
		s.depth = 40;
		s.target = _target;
		return s;
	}

	// SP TWINSHOT's worst on Track _track: both players at the top rate on distinct slots
	inline std::array<uint8_t, 8> twinshotWorst(const int _track)
	{
		return {127, static_cast<uint8_t>(64 + _track), 64, 64, 127, static_cast<uint8_t>(80 + _track), 127, 127};
	}

	// Every SP TWINSHOT Track of _kit on its worst, each Track on its own slots
	inline void useTwinshotWorst(Kit& _kit)
	{
		for(size_t t = 0; t < Tracks; ++t)
			if(_kit[t].machine == Machine{Family::Syntakt, SyntaktTwinshot})
				_kit[t].values = twinshotWorst(static_cast<int>(t));
	}

	// A few sounds of _machine to find its dearest among
	inline std::vector<std::array<uint8_t, 8>> candidates(const Machine& _machine)
	{
		std::vector<std::array<uint8_t, 8>> c;
		std::mt19937 random(_machine.id * 2u + 7);
		for(const uint8_t v : {uint8_t(0), uint8_t(64), uint8_t(127)})
			c.push_back({v, v, v, v, v, v, v, v});
		for(int k = 0; k < 3; ++k)
		{
			std::array<uint8_t, 8> s{};
			for(auto& v : s)
				v = static_cast<uint8_t>(random() % 128);
			c.push_back(s);
		}
		if(_machine.family == Family::Syntakt)
		{
			for(auto& s : c)
				s[6] = s[7] = 127;	// HOLD, DECAY
			if(_machine.id == SyntaktTwinshot)
				c.push_back(twinshotWorst(0));
		}
		return c;
	}

	// SP TWINSHOT's samples: 32 of near full-scale noise, each the longest a slot holds, on 64 slots
	inline void loadTwinshotNoise(syntakt::fast::TwinshotSamples& _samples)
	{
		constexpr uint32_t Length = 0x7ffff;
		_samples.base = 0x100000;
		_samples.data.resize(32 * static_cast<size_t>(Length));
		std::mt19937 rng(1);
		for(auto& x : _samples.data)
		{
			const auto r = static_cast<int32_t>(rng() % 8192);
			x = static_cast<int16_t>(rng() & 1 ? 24576 + r : -24576 - r);
		}
		for(int i = 0; i < 64; ++i)
			_samples.slots[static_cast<size_t>(i)] = {Length, _samples.base + 2 * static_cast<uint32_t>(i % 32) * Length};
	}
}
