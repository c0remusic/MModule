// A measure, by hand, not a test: what the engine costs at its worst, 16 Tracks at 44.1 kHz in host blocks of 128.
//
//	edDrumsCost [RUNS] [SECONDS]	RUNS 5, each of SECONDS 5 of audio by default; the minimum printed, with the median
//
// Each machine on 16 Tracks gives its cost; then every Track is retriggered with its 8 parameters locked, every 15
// blocks of 32 (the Syntakt's fastest retrig, 10 ms) at the machine's dearest sound of a few:
//	- 16 Machinedrum voices, the 16 dearest machines, then all on the dearest
//	- the Syntakt's worst as stFastMixCost makes it: its nine machines, the dearest one on 8 Tracks, so a second group
//	- both families: one Track on each Syntakt machine, 7 on the dearest Machinedrum machine, every instance and every
//	  resampler's lane at work
//	- the same with no Hit: what the idle engine costs
//	- the same kits with every slot off, in their families' chains, and in a chain of ticket 12 at its dearest: two drives
//	  raised 2x (then 4x), three bands, the filter under its envelope, on the Syntakt the amp's: the chain's cost is the
//	  difference from every slot off. Without a chain the numbers are the engine's alone, as ticket 11 gave them
//	- the mixed kit at its dearest with 2 LFOs a Track moving every block on the chain, the machine, level and pan (ticket 13):
//	  the difference from no LFO is what they cost
// and the load of the other processes (GetSystemTimes minus this process). SP TWINSHOT plays near full-scale noise
// at its top rate. The flash image: GEARMULATOR_MD_FIRMWARE_BIN.

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <random>
#include <vector>

#define NOMINMAX
#include <windows.h>

#include "edDrumsEngine.h"

namespace
{
	using namespace elektron::drums;
	using Clock = std::chrono::steady_clock;

	constexpr int HostBlock = 128;
	constexpr int RetrigSamples = 15 * 32;
	constexpr double Budget = 15;

	struct Sound
	{
		Machine machine;
		std::array<uint8_t, 8> values;
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

	enum class Chains
	{
		Bare,		// every slot off
		Family,		// as a machine brings them
		Dearest,
		OneInFour,	// the dearest on one Track of each vector, every slot off on the others: a stage computes for one
		OneInSixteen,
		Mixing		// the dearest, its drives' MIX at 64
	};

	using Aims = std::array<LfoTarget, 2>;		// 2 LFOs a Track, moving every block, on these
	constexpr Aims NoLfo{LfoTarget::None, LfoTarget::None};

	ChainSettings dearest(const Family _family)
	{
		const bool syntakt = _family == Family::Syntakt;
		ChainSettings s;
		s.drive[0] = {syntakt ? DriveType::MdDist : DriveType::Syntakt, false, 80, 127};
		s.drive[1] = {syntakt ? DriveType::Syntakt : DriveType::MdDist, syntakt, 80, 127};
		s.band.type = BandType::ThreeBand;
		s.band.values = {40, 80, 70, 50, 100, 70};
		s.filter.type = syntakt ? FilterType::Syntakt : FilterType::Machinedrum;
		s.filter.mode = 0;
		s.filter.frequency = 90;
		s.filter.resonance = 60;
		s.filter.depth = 100;
		s.filter.envelope.decay = 60;
		s.amp.on = syntakt;
		s.amp.hold = 40;
		s.amp.decay = 80;
		return s;
	}

	// One LFO's worth of what moves every block: a sine of 1/64, each Track's its own speed
	LfoSettings moving(const LfoTarget _target, const int _track)
	{
		LfoSettings s;
		s.rate.division = _track % 3;
		s.shape = LfoShape::Sine;
		s.depth = 40;
		s.target = _target;
		return s;
	}

	// One run: every Track on its sound, _hits or not
	Result once(const Library& _library, const std::array<Sound, Tracks>& _kit, const bool _hits, const double _seconds,
		const Chains _chains = Chains::Bare, const int _factor = 2, const Aims& _lfos = NoLfo)
	{
		auto e = std::make_unique<Engine>(_library);
		e->setOversampling(_factor);
		for(int t = 0; t < Tracks; ++t)
		{
			const auto& s = _kit[static_cast<size_t>(t)];
			e->setMachine(t, s.machine);
			for(int i = 0; i < 8; ++i)
				e->setParameter(t, i, s.values[static_cast<size_t>(i)]);
			if(_chains == Chains::Bare || (_chains == Chains::OneInFour && t % 4) || (_chains == Chains::OneInSixteen && t))
			{
				e->setChain(t, ChainSettings{});
			}
			else if(_chains != Chains::Family)
			{
				auto chain = dearest(s.machine.family);
				if(_chains == Chains::Mixing)
					chain.drive[0].mix = chain.drive[1].mix = 64;
				e->setChain(t, chain);
			}
			for(int i = 0; i < 2; ++i)
				if(_lfos[static_cast<size_t>(i)] != LfoTarget::None)
					e->setLfo(t, i, moving(_lfos[static_cast<size_t>(i)], t));
		}
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
							e->lock(t, i, _kit[static_cast<size_t>(t)].values[static_cast<size_t>(i)]);
						e->noteOn(t, 127);
					}
					nextHit += RetrigSamples;
				}
				const int run = static_cast<int>(std::min<int64_t>(HostBlock - done, _hits ? nextHit - at : HostBlock - done));
				std::array<float*, 2 * Outputs> q = p;
				for(auto*& x : q)
					x += done;
				e->render(q.data(), run);
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
		return {100 * total / (static_cast<double>(blocks) * (HostBlock / SampleRate * 1e6)), worst, load.otherCores()};
	}

	void scenario(const char* _name, const Library& _library, const std::array<Sound, Tracks>& _kit, const bool _hits,
		const int _runs, const double _seconds, const Chains _chains = Chains::Bare, const int _factor = 2, const Aims& _lfos = NoLfo)
	{
		std::vector<double> pct;
		double worst = 0, others = 0;
		for(int r = 0; r < _runs; ++r)
		{
			const auto result = once(_library, _kit, _hits, _seconds, _chains, _factor, _lfos);
			pct.push_back(result.pct);
			worst = std::max(worst, result.worstUs);
			others = std::max(others, result.others);
		}
		std::sort(pct.begin(), pct.end());
		std::printf("%-46s %6.2f %% of a core at least, median %6.2f (margin %+.2f points to %.0f %%), worst block %.0f us of "
			"%.0f; others' load at most %.2f cores\n", _name, pct.front(), pct[pct.size() / 2], Budget - pct.front(), Budget,
			worst, HostBlock / SampleRate * 1e6, others);
	}

	// SP TWINSHOT's worst on Track _track: both players at the top rate on distinct slots
	std::array<uint8_t, 8> twinshotWorst(const int _track)
	{
		return {127, static_cast<uint8_t>(64 + _track), 64, 64, 127, static_cast<uint8_t>(80 + _track), 127, 127};
	}

	std::vector<std::array<uint8_t, 8>> candidates(const Machine& _machine)
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
			if(_machine.id == 8)
				c.push_back(twinshotWorst(0));
		}
		return c;
	}
}

int main(const int _argc, char** _argv)
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	const int runs = _argc > 1 ? std::max(1, std::atoi(_argv[1])) : 5;
	const double seconds = _argc > 2 ? std::atof(_argv[2]) : 5;
	const auto image = Library::findFlashImage({});
	if(image.empty())
	{
		std::printf("set GEARMULATOR_MD_FIRMWARE_BIN\n");
		return 1;
	}
	Library library(image);

	constexpr uint32_t Length = 0x7ffff;
	syntakt::fast::TwinshotSamples samples;
	samples.base = 0x100000;
	samples.data.resize(32 * static_cast<size_t>(Length));
	std::mt19937 rng(1);
	for(auto& x : samples.data)
	{
		const auto r = static_cast<int32_t>(rng() % 8192);
		x = static_cast<int16_t>(rng() & 1 ? 24576 + r : -24576 - r);
	}
	for(int i = 0; i < 64; ++i)
		samples.slots[static_cast<size_t>(i)] = {Length, samples.base + 2 * static_cast<uint32_t>(i % 32) * Length};
	library.twinshot = &samples;

	// Each machine's dearest sound, 16 Tracks of it
	struct Cost
	{
		Sound sound;
		double pct;
	};
	std::vector<Cost> md, st;
	for(int m = 0; m < SyntaktMachines + 256; ++m)
	{
		const Machine machine = m < SyntaktMachines ? Machine{Family::Syntakt, static_cast<uint8_t>(m)}
			: Machine{Family::Machinedrum, static_cast<uint8_t>(m - SyntaktMachines)};
		if(machine.family == Family::Machinedrum && !md::voice::coefficientsPorted(machine.id))
			continue;
		Cost best{{machine, {}}, -1};
		for(const auto& values : candidates(machine))
		{
			std::array<Sound, Tracks> kit;
			kit.fill({machine, values});
			const double pct = once(library, kit, true, 0.1).pct;
			if(pct > best.pct)
				best = {{machine, values}, pct};
		}
		(machine.family == Family::Syntakt ? st : md).push_back(best);
	}
	const auto dearer = [](const Cost& _a, const Cost& _b) { return _a.pct > _b.pct; };
	std::sort(md.begin(), md.end(), dearer);
	std::sort(st.begin(), st.end(), dearer);
	std::printf("the dearest, 16 Tracks each:");
	for(size_t i = 0; i < 6; ++i)
		std::printf(" MD %d %.2f %%,", md[i].sound.machine.id, md[i].pct);
	std::printf(" cheapest MD %d %.2f %%;", md.back().sound.machine.id, md.back().pct);
	for(const auto& c : st)
		std::printf(" %s %.2f %%,", syntaktMachineNames[c.sound.machine.id], c.pct);
	std::printf("\n");

	std::array<Sound, Tracks> kit;
	kit.fill({Machine{}, {}});
	scenario("nothing assigned", library, kit, false, runs, seconds);
	kit.fill(md[0].sound);
	scenario("16 MD on the dearest", library, kit, true, runs, seconds);
	scenario("16 MD on the dearest, no Hit", library, kit, false, runs, seconds);
	for(size_t t = 0; t < Tracks; ++t)
		kit[t] = md[t].sound;
	scenario("16 MD, the 16 dearest machines", library, kit, true, runs, seconds);

	for(size_t t = 0; t < Tracks; ++t)
		kit[t] = st[t < st.size() ? t : 0].sound;
	for(size_t t = 0; t < Tracks; ++t)
		if(kit[t].machine.id == 8)
			kit[t].values = twinshotWorst(static_cast<int>(t));
	scenario("Syntakt: nine machines, 8 Tracks on the dearest", library, kit, true, runs, seconds);

	for(size_t t = 0; t < Tracks; ++t)
		kit[t] = t < st.size() ? st[t].sound : md[0].sound;
	for(size_t t = 0; t < st.size(); ++t)
		if(kit[t].machine.id == 8)
			kit[t].values = twinshotWorst(static_cast<int>(t));
	scenario("mixed: 9 Syntakt machines, 7 MD on the dearest", library, kit, true, runs, seconds);
	scenario("mixed, no Hit", library, kit, false, runs, seconds);
	scenario("mixed, family chains", library, kit, true, runs, seconds, Chains::Family);
	scenario("mixed, dearest chains, drives 2x", library, kit, true, runs, seconds, Chains::Dearest, 2);
	scenario("mixed, dearest, 2x, 2 LFOs on FLTF + 3 bands", library, kit, true, runs, seconds, Chains::Dearest, 2, Aims{LfoTarget::Frequency, LfoTarget::BandA});
	scenario("mixed, dearest, 2x, 2 LFOs on 2 of the 3 bands", library, kit, true, runs, seconds, Chains::Dearest, 2, Aims{LfoTarget::BandA, LfoTarget::BandB});
	scenario("mixed, dearest, 2x, 2 LFOs on SYN1 + SYN2", library, kit, true, runs, seconds, Chains::Dearest, 2, Aims{LfoTarget::Machine1, LfoTarget::Machine2});
	scenario("mixed, dearest, 2x, 2 LFOs on level + pan", library, kit, true, runs, seconds, Chains::Dearest, 2, Aims{LfoTarget::Level, LfoTarget::Pan});
	scenario("mixed, dearest chains, drives 4x", library, kit, true, runs, seconds, Chains::Dearest, 4);

	kit.fill(md[0].sound);
	scenario("16 MD, family chains", library, kit, true, runs, seconds, Chains::Family);
	scenario("16 MD, dearest chains, drives 2x", library, kit, true, runs, seconds, Chains::Dearest, 2);
	scenario("16 MD, dearest, 2x, 2 LFOs on SYN1 + SYN2", library, kit, true, runs, seconds, Chains::Dearest, 2, Aims{LfoTarget::Machine1, LfoTarget::Machine2});
	scenario("16 MD, the dearest on 1 Track of 4, drives 2x", library, kit, true, runs, seconds, Chains::OneInFour, 2);
	scenario("16 MD, the dearest on 1 Track of 16, drives 2x", library, kit, true, runs, seconds, Chains::OneInSixteen, 2);
	scenario("16 MD, dearest chains, MIX 64, drives 2x", library, kit, true, runs, seconds, Chains::Mixing, 2);

	for(size_t t = 0; t < Tracks; ++t)
		kit[t] = st[t < st.size() ? t : 0].sound;
	for(size_t t = 0; t < Tracks; ++t)
		if(kit[t].machine.id == 8)
			kit[t].values = twinshotWorst(static_cast<int>(t));
	scenario("Syntakt, family chains", library, kit, true, runs, seconds, Chains::Family);
	scenario("Syntakt, dearest chains, drives 2x", library, kit, true, runs, seconds, Chains::Dearest, 2);

	// The families on alternate lanes: every vector of both chains computes for half its Tracks
	for(size_t t = 0; t < Tracks; ++t)
		kit[t] = t % 2 ? md[0].sound : st[(t / 2) % st.size()].sound;
	scenario("alternating families, every slot off", library, kit, true, runs, seconds);
	scenario("alternating families, family chains", library, kit, true, runs, seconds, Chains::Family);
	scenario("alternating families, dearest, drives 2x", library, kit, true, runs, seconds, Chains::Dearest, 2);
	scenario("alternating families, dearest, drives 4x", library, kit, true, runs, seconds, Chains::Dearest, 4);
	return 0;
}
