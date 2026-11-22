// The worst kit's cost, at the gate (spec v2, section 3): red over 15 % of a core.
//
// The worst kit, found afresh at each run so that every machine and type added later takes part in it:
//	- 16 Tracks on the 16 dearest distinct machines of every family, each at its dearest sound of a few, ranked by what
//	  one Track of it adds: the cost follows the machines used, not the Tracks
//	- every chain slot on its dearest type, slot by slot, drives raised and oversampled 4x (the dearest setting), the
//	  amp on, 2 LFOs a Track moving every block on the filter's frequency and the low band
//	- every Track sent at full level to the four send effects, each on its dearest type; the master on, compressing and
//	  driven
// Machine quiet, 44.1 kHz, host blocks of 128, every Track retriggered every 10 ms with its values locked. Each stage is
// the minimum of 5 runs of 4 s, and what it costs is its difference from the stage before: engines, then the chain,
// the sends, the master. The sample layer does not exist yet: its line says so until it does.
//
//	edDrumsWorstKitCost [RUNS] [SECONDS]		the flash image: GEARMULATOR_MD_FIRMWARE_BIN; without it exit 77

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <vector>

#include "elektronTimingTest.h"
#include "edDrumsCostBench.h"

namespace
{
	using namespace elektron::drums;
	using namespace bench;

	constexpr int Oversampling = 4;
	constexpr double SearchSeconds = 0.2;	// a machine's or a type's run while the worst kit is sought
	constexpr int SearchRuns = 2;

	// The guides by block (spec v2, section 3): what each stage should stay under, 15 % together
	constexpr double EnginesGuide = 10, LayerGuide = 1.5, ChainGuide = 2.0, SendsGuide = 1.2, MasterGuide = 0.3;

	// How far the kit goes: each stage adds to the one before
	enum class Stage { Engines, Chain, Sends, Master };

	struct Setup
	{
		Kit kit;
		ChainSettings chain;
		std::array<int, SendCount> sources{};		// SendSource for the delay and reverb, the Type or Mode byte for the others
	};

	std::vector<Machine> allMachines()
	{
		std::vector<Machine> m;
		const auto add = [&](const Family _family, const int _count)
		{
			for(int i = 0; i < _count; ++i)
				m.push_back({_family, static_cast<uint8_t>(i)});
		};
		add(Family::Syntakt, SyntaktMachines);
		for(int id = 0; id < 256; ++id)
			if(md::voice::coefficientsPorted(static_cast<uint8_t>(id)) && playable({Family::Machinedrum, static_cast<uint8_t>(id)}))
				m.push_back({Family::Machinedrum, static_cast<uint8_t>(id)});
		add(Family::OpenDrum, OpenDrumMachines);
		add(Family::Sophie, SophieMachines);
		add(Family::SyntaktAnalog, SyntaktAnalogMachines);
		add(Family::RytmAnalog, RytmAnalogMachines);
		add(Family::FmDrums, FmDrumsMachines);
		return m;
	}

	const char* familyName(const Family _family)
	{
		switch(_family)
		{
		case Family::Syntakt:		return "Syntakt";
		case Family::Machinedrum:	return "MD";
		case Family::OpenDrum:		return "OPEN DRUM";
		case Family::Sophie:		return "SOPHIE";
		case Family::SyntaktAnalog:	return "Syntakt analog";
		case Family::RytmAnalog:	return "Rytm analog";
		case Family::FmDrums:		return "FM DRUMS";
		case Family::Off:			return "off";
		}
		return "?";
	}

	void apply(Engine& _e, const Setup& _s, const Stage _stage)
	{
		_e.setOversampling(Oversampling);
		for(int t = 0; t < Tracks; ++t)
		{
			const auto& sound = _s.kit[static_cast<size_t>(t)];
			_e.setMachine(t, sound.machine);
			for(int i = 0; i < 8; ++i)
				_e.setParameter(t, i, sound.values[static_cast<size_t>(i)]);
			const bool chain = _stage != Stage::Engines;
			_e.setChain(t, chain ? _s.chain : ChainSettings{});
			_e.setLfo(t, 0, chain ? moving(LfoTarget::Frequency, t) : LfoSettings{});
			_e.setLfo(t, 1, chain ? moving(LfoTarget::BandA, t) : LfoSettings{});
			for(int k = 0; k < SendCount; ++k)
				_e.setSend(t, static_cast<Send>(k), _stage >= Stage::Sends ? 127 : 0);
		}
		auto& fx = _e.sends();
		for(int i = 0; i < ChorusBytes; ++i)
			fx.setChorus(i, 100);
		for(int i = 0; i < PhaserBytes; ++i)
			fx.setPhaser(i, 100);
		fx.setSource(Send::Delay, static_cast<SendSource>(_s.sources[0]));
		fx.setSource(Send::Reverb, static_cast<SendSource>(_s.sources[1]));
		fx.setChorus(ChorusType, static_cast<uint8_t>(_s.sources[2]));
		fx.setPhaser(PhaserMode, static_cast<uint8_t>(_s.sources[3]));

		auto& master = _e.master();
		master.set(MasterThreshold, 0);
		master.set(MasterRatio, 100);
		master.set(MasterMix, 127);
		master.set(MasterDrive, 127);
		_e.setMasterOn(_stage == Stage::Master);
	}

	// The least of _runs runs of _seconds, each on a fresh engine
	Result least(const Library& _library, const Setup& _s, const Stage _stage, const int _runs, const double _seconds)
	{
		Result best{1e9, 0, 0};
		for(int r = 0; r < _runs; ++r)
		{
			auto e = std::make_unique<Engine>(_library);
			apply(*e, _s, _stage);
			const auto result = measure(*e, _s.kit, true, _seconds);
			best.pct = std::min(best.pct, result.pct);
			best.worstUs = std::max(best.worstUs, result.worstUs);
			best.others = std::max(best.others, result.others);
		}
		return best;
	}

	// Of _count choices, the one whose _set makes the setup dearest
	int dearestOf(const Library& _library, Setup& _s, const Stage _stage, const int _first, const int _count,
		const std::function<void(Setup&, int)>& _set)
	{
		int best = _first;
		double most = -1;
		for(int k = _first; k < _count; ++k)
		{
			_set(_s, k);
			const double pct = least(_library, _s, _stage, SearchRuns, SearchSeconds).pct;
			if(pct > most)
			{
				most = pct;
				best = k;
			}
		}
		_set(_s, best);
		return best;
	}

	// The 16 dearest distinct machines, ranked by what one Track of each adds at its dearest sound of a few. One Track
	// alone is lost in what the engine costs anyway, so it is measured on more Tracks less the empty engine: a voice per
	// Track on 16, a sixteenth of it; a Syntakt machine on 4, all of it, since one Track already renders the 4 lanes of
	// its first group and its own resampler
	Kit dearestMachines(const Library& _library)
	{
		std::vector<Cost> costs;
		Kit kit;
		Engine e(_library);
		e.setOversampling(Oversampling);
		const auto leastHere = [&]
		{
			double pct = 1e9;
			for(int r = 0; r < SearchRuns; ++r)
				pct = std::min(pct, measure(e, kit, true, SearchSeconds).pct);
			return pct;
		};
		kit.fill({Machine{}, {}});
		for(int t = 0; t < Tracks; ++t)
			e.setMachine(t, Machine{});
		const double empty = leastHere();
		for(const auto& machine : allMachines())
		{
			Cost best{{machine, {}}, -1};
			const int on = machine.family == Family::Syntakt ? 4 : Tracks;
			for(const auto& values : candidates(machine))
			{
				for(int t = 0; t < Tracks; ++t)
				{
					const Sound sound = t < on ? Sound{machine, values} : Sound{Machine{}, {}};
					kit[static_cast<size_t>(t)] = sound;
					e.setMachine(t, sound.machine);
					e.setChain(t, ChainSettings{});		// the machine's own cost, not its family's chain
					for(int i = 0; i < 8; ++i)
						e.setParameter(t, i, sound.values[static_cast<size_t>(i)]);
				}
				const double pct = (leastHere() - empty) / (on == Tracks ? Tracks : 1);
				if(pct > best.pct)
					best = {{machine, values}, pct};
			}
			costs.push_back(best);
		}
		std::sort(costs.begin(), costs.end(), [](const Cost& _a, const Cost& _b) { return _a.pct > _b.pct; });
		for(size_t t = 0; t < Tracks; ++t)
			kit[t] = costs[t].sound;
		useTwinshotWorst(kit);
		std::printf("worst kit, %zu machines searched:", costs.size());
		for(size_t t = 0; t < Tracks; ++t)
		{
			const auto& m = kit[t].machine;
			if(m.family == Family::Syntakt)
				std::printf(" %s (%.2f %%),", syntaktMachineNames[m.id], costs[t].pct);
			else
				std::printf(" %s %d (%.2f %%),", familyName(m.family), m.id, costs[t].pct);
		}
		std::printf("\n");
		return kit;
	}

	ChainSettings raised()
	{
		ChainSettings s;
		s.drive[0] = {DriveType::Syntakt, false, 80, 127};
		s.drive[1] = {DriveType::Syntakt, true, 80, 127};
		s.band.type = BandType::ThreeBand;
		s.band.values = {40, 80, 70, 50, 100, 70};
		s.filter.type = FilterType::Syntakt;
		s.filter.mode = 0;
		s.filter.frequency = 90;
		s.filter.resonance = 60;
		s.filter.depth = 100;
		s.filter.envelope.decay = 60;
		s.amp.on = true;
		s.amp.hold = 40;
		s.amp.decay = 80;
		return s;
	}

	void line(const char* _name, const double _pct, const double _guide)
	{
		std::printf("  %-8s %6.2f %% of a core, guide %.1f %%%s\n", _name, _pct, _guide,
			_pct > _guide ? ": over its guide" : "");
	}
}

int main(const int _argc, char** _argv)
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	elektron::raiseTimingTestPriority();
	const int runs = _argc > 1 ? std::max(1, std::atoi(_argv[1])) : 5;
	const double seconds = _argc > 2 ? std::atof(_argv[2]) : 4;
	const auto image = Library::findFlashImage({});
	if(image.empty())
	{
		std::printf("no flash image: set GEARMULATOR_MD_FIRMWARE_BIN\n");
		return 77;
	}
	Library library(image);
	syntakt::fast::TwinshotSamples samples;
	loadTwinshotNoise(samples);
	library.twinshot = &samples;
	// FMD CHORD plays its waves, or its cost would count as silence's
	const char* section3 = std::getenv("CYCLES_SECTION3");
	library.chord = section3 ? Library::loadChordWaves(section3) : nullptr;
	if(!library.chord)
	{
		std::printf("no FMD CHORD waves: set CYCLES_SECTION3 to the Model:Cycles OS 1.13's section 3\n");
		return 77;
	}

	Setup s;
	s.kit = dearestMachines(library);
	s.chain = raised();

	const int driveA = dearestOf(library, s, Stage::Chain, 1, static_cast<int>(DriveType::Count),
		[](Setup& _s, const int _k) { _s.chain.drive[0].type = static_cast<DriveType>(_k); });
	const int driveB = dearestOf(library, s, Stage::Chain, 1, static_cast<int>(DriveType::Count),
		[](Setup& _s, const int _k) { _s.chain.drive[1].type = static_cast<DriveType>(_k); });
	const int band = dearestOf(library, s, Stage::Chain, 1, static_cast<int>(BandType::Count),
		[](Setup& _s, const int _k) { _s.chain.band.type = static_cast<BandType>(_k); });
	const int filter = dearestOf(library, s, Stage::Chain, 1, static_cast<int>(FilterType::Count),
		[](Setup& _s, const int _k) { _s.chain.filter.type = static_cast<FilterType>(_k); });
	for(int k = 0; k < SendCount; ++k)
		dearestOf(library, s, Stage::Sends, 0, SendTypes[static_cast<size_t>(k)],
			[k](Setup& _s, const int _v) { _s.sources[static_cast<size_t>(k)] = _v; });
	std::printf("dearest chain: drives %d and %d, band %d, filter %d; sends' types: delay %d, reverb %d, chorus %d, "
		"phaser %d\n", driveA, driveB, band, filter, s.sources[0], s.sources[1], s.sources[2], s.sources[3]);

	std::array<Result, 4> stage{};
	for(int k = 0; k < 4; ++k)
		stage[static_cast<size_t>(k)] = least(library, s, static_cast<Stage>(k), runs, seconds);

	const double total = stage[3].pct;
	std::printf("worst kit, %d runs of %.0f s, the least of each:\n", runs, seconds);
	line("engines", stage[0].pct, EnginesGuide);
	std::printf("  %-8s not built yet, guide %.1f %%\n", "layer", LayerGuide);
	line("chain", stage[1].pct - stage[0].pct, ChainGuide);
	line("sends", stage[2].pct - stage[1].pct, SendsGuide);
	line("master", stage[3].pct - stage[2].pct, MasterGuide);
	double worst = 0, others = 0;
	for(const auto& r : stage)
	{
		worst = std::max(worst, r.worstUs);
		others = std::max(others, r.others);
	}
	std::printf("  total    %6.2f %% of a core, ceiling %.0f %% (margin %+.2f points); worst block %.0f us of %.0f; "
		"others' load at most %.2f cores\n", total, Budget, Budget - total, worst, BlockUs, others);
	if(total > Budget)
	{
		std::printf("FAIL: the worst kit costs %.2f %% of a core, over %.0f %%\n", total, Budget);
		return 1;
	}
	std::printf("PASS\n");
	return 0;
}
