// The Syntakt family's fresh amp (syntaktChain): a new Track's Hit ends as its machine's own DECAY ends it. For BD
// MODERN, SD BASIC and CY ALLOY at their machine defaults, the time a Hit takes to fall 40 dB under its peak, through
// the family's chain with the amp off (the machine alone), with the amp as syntaktChain sets it, and with the amp of
// before (HOLD 64 at unity for 1.84 s, DECAY 127 that never closes), which a state that stored it still plays. A sweep of
// DECAY shows what the default was chosen from.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

#include "edDrumsEngine.h"
#include "edDrumsTestSupport.h"
#include "stFastTrack.h"

namespace
{
	using namespace elektron::drums;
	using namespace elektron::drums::test;

	constexpr int Length = 3 * 44100;

	void require(const bool _condition, const std::string& _message)
	{
		if(!_condition)
			throw std::runtime_error(_message);
	}

	// ms from the note to the last sample at or above 1 % of the peak, Main left, Track 0 on _machine through _chain
	double fall40(const Library& _library, const Machine& _machine, const ChainSettings& _chain)
	{
		Engine e(_library);
		e.setMachine(0, _machine);
		e.setLevel(0, 127);
		e.setChain(0, _chain);
		Take take(Length);
		e.noteOn(0, 127);
		take.render(e, 0, Length);
		const auto& x = take.channel(0);
		float peak = 0;
		for(const float v : x)
			peak = std::max(peak, std::abs(v));
		size_t last = 0;
		for(size_t n = 0; n < x.size(); ++n)
			if(std::abs(x[n]) >= peak * 0.01f)
				last = n;
		require(peak > 0, nameOf(_machine) + " is silent");
		return static_cast<double>(last) * 1000.0 / SampleRate;
	}
}

int main()
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	try
	{
		const Library library;
		const Machine machines[]{{Family::Syntakt, 0}, {Family::Syntakt, 1}, {Family::Syntakt, 2}};
		const auto fresh = syntaktChain();
		std::printf("Syntakt amp defaults: ATTACK %d, HOLD %d (%.1f ms), DECAY %d (-40 dB in %.0f ms)\n", fresh.amp.attack,
			fresh.amp.hold, syntakt::fast::ampHoldSamples(fresh.amp.hold) * 1000 / syntakt::fast::MachineRate,
			fresh.amp.decay, 2 * syntakt::fast::ampHoldSamples(fresh.amp.decay) * 1000 / syntakt::fast::MachineRate);
		for(const auto& m : machines)
		{
			auto off = fresh, before = fresh;
			off.amp.on = false;
			before.amp.hold = 64;
			before.amp.decay = 127;
			const double own = fall40(library, m, off), now = fall40(library, m, fresh), old = fall40(library, m, before);
			std::printf("  %-10s -40 dB at %6.1f ms alone, %6.1f ms with the fresh amp (%.1f %%), %6.1f ms with HOLD 64,"
				" DECAY 127\n", nameOf(m).c_str(), own, now, 100.0 * now / own, old);
			std::printf("             DECAY sweep:");
			for(const int d : {64, 96, 112, 120, 122, 124, 125, 126})
			{
				auto s = fresh;
				s.amp.hold = 0;
				s.amp.decay = d;
				std::printf(" %d %.0f", d, fall40(library, m, s));
			}
			std::printf("\n");
			require(now <= own + 1, nameOf(m) + ": the fresh amp lengthens the Hit");
			// DECAY 126, the longest that closes, takes 9 % off BD MODERN's 1.87 s at its machine default
			require(now >= 0.9 * own, nameOf(m) + ": the fresh amp ends the Hit well before its machine's DECAY does");
			require(std::abs(old - own) <= 1, nameOf(m) + ": HOLD 64 and DECAY 127 no longer play as before");
		}
		require(fresh.amp.on && fresh.amp.attack == 0 && fresh.amp.hold == 0 && !fresh.amp.adsr,
			"a fresh Track's amp: AHD, ATTACK 0, HOLD 0");
		std::printf("PASS: a fresh Syntakt Track's Hit falls to -40 dB within its machine's own decay, the amp of before"
			" still plays as it did\n");
		return 0;
	}
	catch(const std::exception& _e)
	{
		std::printf("FAIL: %s\n", _e.what());
		return 1;
	}
}
