// The ported routines against the original routines of the Syntakt's voice CPU (OS 1.41, section 7), word for word.
//
// The vectors are recorded from the original code run on SyntHack OS's harness (.scratch/elektron-engines): each
// call of a routine with its arguments, the value it returned, the noise generator's state and the 64 KB of SRAM
// (voice states, scratch buffers, RAM tables) before and after. Each case loads SRAM before into the port, runs the
// ported routine, and compares the whole SRAM, the generator and the returned value with what the original left. The
// vectors hold firmware-derived data and stay outside the repository: SYNTAKT_SECTION7 names the user's section 7,
// SYNTAKT_VECTORS the vector files (';'-separated). Exit 77 without them.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "stSection7.h"
#include "stEngine.h"
#include "stEngines.h"
#include "stStages.h"
#include "stTrack.h"
#include "stVoice.h"

namespace
{
	using syntakt::Sram;
	using syntakt::Voice;
	using syntakt::be32;

	struct Case
	{
		uint32_t routine = 0;
		std::vector<uint32_t> args;
		uint32_t d0 = 0;
		uint32_t rngBefore[2]{}, rngAfter[2]{};
		std::vector<uint8_t> before, after;
	};

	// Reads the cases _keep accepts into _cases; _routines receives every case's routine, kept or not, in order
	template<typename Keep>
	bool readCases(const std::string& _path, std::vector<Case>& _cases, const Keep& _keep,
		std::vector<uint32_t>& _routines)
	{
		std::ifstream f(_path, std::ios::binary | std::ios::ate);
		const auto size = f.tellg();
		f.seekg(0);
		char magic[4];
		uint32_t count = 0;
		if(!f.read(magic, 4) || std::string(magic, 4) != "STV3" || !f.read(reinterpret_cast<char*>(&count), 4))
			return false;
		for(uint32_t i = 0; i < count; ++i)
		{
			Case c;
			uint32_t nargs = 0;
			f.read(reinterpret_cast<char*>(&c.routine), 4);
			f.read(reinterpret_cast<char*>(&nargs), 4);
			if(!f || nargs > 16)
				return false;
			c.args.resize(nargs);
			f.read(reinterpret_cast<char*>(c.args.data()), 4 * nargs);
			f.read(reinterpret_cast<char*>(&c.d0), 4);
			f.read(reinterpret_cast<char*>(c.rngBefore), 8);
			f.read(reinterpret_cast<char*>(c.rngAfter), 8);
			if(!f)
				return false;
			_routines.push_back(c.routine);
			if(!_keep(c.routine))
			{
				// seekg past the end does not fail the stream: a truncated file shows in the position
				f.seekg(2 * Sram::Size, std::ios::cur);
				if(!f || f.tellg() > size)
					return false;
				continue;
			}
			c.before.resize(Sram::Size);
			c.after.resize(Sram::Size);
			f.read(reinterpret_cast<char*>(c.before.data()), Sram::Size);
			f.read(reinterpret_cast<char*>(c.after.data()), Sram::Size);
			if(!f)
				return false;
			_cases.push_back(std::move(c));
		}
		return true;
	}

	void load(Sram& _sram, const std::vector<uint8_t>& _image)
	{
		for(size_t i = 0; i < _sram.words.size(); ++i)
			_sram.words[i] = static_cast<int32_t>(be32(_image.data() + 4 * i));
	}

	struct Context
	{
		Sram& sram;
		syntakt::Globals& globals;
		const syntakt::Rom& rom;
		const std::vector<uint32_t>& args;
		uint32_t d0 = 0;

		Voice voice(const size_t _arg) const { return Voice{sram, args[_arg]}; }
		int32_t* block(const size_t _arg) const { return sram.ptr(args[_arg]); }
	};

	struct Ported
	{
		std::string name;
		bool returns;	// the routine's d0 is compared too
		std::function<void(Context&)> run;
	};

	// _routines with each engine's update and render, from the one list of the engines ported
	std::map<uint32_t, Ported> withEngines(std::map<uint32_t, Ported> _routines)
	{
		for(const auto& e : syntakt::PortedEngines)
		{
			_routines.emplace(e.updateAddress, Ported{std::string(e.name) + "Update", false, [u = e.update](Context& _c)
				{
					auto v = _c.voice(1);
					u(static_cast<int32_t>(_c.args[0]), v, _c.args[2], _c.rom);
				}});
			_routines.emplace(e.renderAddress, Ported{std::string(e.name) + "Render", false, [r = e.render](Context& _c)
				{
					auto v = _c.voice(1);
					r(_c.block(0), v, _c.rom);
				}});
		}
		return _routines;
	}

	const std::map<uint32_t, Ported>& ported()
	{
		static const std::map<uint32_t, Ported> routines = withEngines({
			{0x40004dc6, {"interpolate", true, [](Context& _c)
				{
					_c.d0 = static_cast<uint32_t>(syntakt::interpolate(_c.rom.ptr(_c.args[0]),
						static_cast<int32_t>(_c.args[1])));
				}}},
			{0x40003c0e, {"unitEnvelopes", false, [](Context& _c)
				{
					auto v = _c.voice(0);
					syntakt::unitEnvelopes(v, static_cast<int32_t>(_c.args[1]));
				}}},
			{0x40003d42, {"ampEnvelope", false, [](Context& _c)
				{
					auto v = _c.voice(0);
					syntakt::ampEnvelope(v);
				}}},
			{0x400045a0, {"vca", false, [](Context& _c)
				{
					auto v = _c.voice(1);
					syntakt::vca(_c.block(0), v);
				}}},
			{0x400047ea, {"driveClip", false, [](Context& _c)
				{
					auto v = _c.voice(1);
					syntakt::driveClip(_c.block(0), v);
				}}},
			{0x40003ee0, {"initVoice", false, [](Context& _c)
				{
					auto v = _c.voice(0);
					syntakt::initVoice(v, _c.globals, _c.rom);
				}}},
			{0x4000255e, {"restartPhases", false, [](Context& _c)
				{
					auto v = _c.voice(0);
					syntakt::restartPhases(v);
				}}},
			{0x400046dc, {"sum", false, [](Context& _c)
				{
					syntakt::bdModern::sum(_c.block(0), _c.block(1), _c.block(2));
				}}},
			{0x40004e6c, {"onePole", false, [](Context& _c)
				{
					syntakt::onePole(_c.block(0), _c.block(1));
				}}},
			{0x400049f4, {"mix", false, [](Context& _c)
				{
					// The gains' indices are on the caller's stack, outside SRAM: BD MODERN passes {0, 1}
					auto v = _c.voice(4);
					syntakt::mix(_c.block(0), _c.block(1), _c.block(2), 0, 1, v);
				}}},
			{0x40004674, {"modulatorVca", false, [](Context& _c)
				{
					auto v = _c.voice(1);
					syntakt::modulatorVca(_c.block(0), v);
				}}},
			{0x40003df2, {"modulatorEnvelope", false, [](Context& _c)
				{
					auto v = _c.voice(0);
					syntakt::modulatorEnvelope(v);
				}}},
			{0x40003cda, {"sweepEnvelope", false, [](Context& _c)
				{
					auto v = _c.voice(0);
					syntakt::sweepEnvelope(v);
				}}},
			{0x40002596, {"phaseIncr48k", false, [](Context& _c)
				{
					auto v = _c.voice(1);
					syntakt::phaseIncrements48k(static_cast<int32_t>(_c.args[0]), v, _c.args[2],
						static_cast<int32_t>(_c.args[3]));
				}}},
			{0x400035de, {"fbOscillator", false, [](Context& _c)
				{
					auto v = _c.voice(1);
					syntakt::feedbackOscillator(static_cast<int32_t>(_c.args[0]), v, _c.rom);
				}}},
			{0x4000290e, {"pmOscillator", false, [](Context& _c)
				{
					auto v = _c.voice(1);
					syntakt::pmOscillator(static_cast<int32_t>(_c.args[0]), v, _c.rom);
				}}},
			{0x40004ee6, {"click", false, [](Context& _c)
				{
					auto v = _c.voice(1);
					syntakt::bdModern::click(_c.block(0), v);
				}}},
			{0x4000112c, {"trackDrive", false, [](Context& _c)
				{
					syntakt::track::drive(_c.sram, _c.args[0], _c.args[1]);
				}}},
			{0x40002320, {"trackBaseWidth", false, [](Context& _c)
				{
					syntakt::track::baseWidth(_c.sram, _c.rom, _c.args[0], _c.args[1]);
				}}},
			{0x400019d6, {"trackFilter", false, [](Context& _c)
				{
					syntakt::track::filter(_c.sram, _c.rom, _c.args[0], _c.args[1], static_cast<int32_t>(_c.args[2]),
						static_cast<int32_t>(_c.args[3]), static_cast<int32_t>(_c.args[4]),
						static_cast<int32_t>(_c.args[5]));
				}}},
			{0x4000168e, {"trackAmp", false, [](Context& _c)
				{
					syntakt::track::ampEnvelopes(_c.sram, _c.rom, _c.args[0], _c.args[1],
						static_cast<int32_t>(_c.args[2]), static_cast<int32_t>(_c.args[3]),
						static_cast<int32_t>(_c.args[4]));
				}}},
			{0x40004324, {"renderVoices", false, [](Context& _c)
				{
					syntakt::renderVoices(_c.sram, _c.globals, _c.rom, _c.args[0], _c.args[1],
						static_cast<int32_t>(_c.args[2]), static_cast<int32_t>(_c.args[3]));
				}}},
		});
		return routines;
	}
}

int main()
{
	setvbuf(stdout, nullptr, _IONBF, 0);

	const char* section7Path = std::getenv("SYNTAKT_SECTION7");
	const char* vectorPaths = std::getenv("SYNTAKT_VECTORS");
	if(!section7Path || !*section7Path || !vectorPaths || !*vectorPaths)
	{
		std::printf("SKIP: set SYNTAKT_SECTION7 and SYNTAKT_VECTORS\n");
		return 77;
	}
	const auto romWords = syntakt::loadSection7(section7Path);
	if(romWords.empty())
	{
		std::printf("FAIL: %s is not OS 1.41's section 7\n", section7Path);
		return 1;
	}
	const syntakt::Rom rom{romWords.data()};

	std::vector<Case> cases;
	// Each file's cases [first, end), and whether the file is a run: nothing but consecutive blocks of renderVoices
	// (vectors.py 40004324 --prob 1), judged on every case it holds, kept or not
	struct File
	{
		size_t first, end;
		bool isRun;
	};
	std::vector<File> files;
	std::map<uint32_t, int> skipped;	// the cases of routines not ported, by address
	std::stringstream paths(vectorPaths);
	for(std::string path; std::getline(paths, path, ';');)
	{
		const size_t first = cases.size();
		std::vector<uint32_t> routines;
		if(!readCases(path, cases, [](const uint32_t _r) { return ported().count(_r) != 0; }, routines))
		{
			std::printf("FAIL: cannot read the vectors in %s\n", path.c_str());
			return 1;
		}
		if(cases.size() == first)
		{
			std::printf("FAIL: %s holds no case of a ported routine\n", path.c_str());
			return 1;
		}
		bool isRun = routines.size() > 1;
		for(const uint32_t r : routines)
		{
			isRun = isRun && r == 0x40004324;
			if(!ported().count(r))
				++skipped[r];
		}
		files.push_back({first, cases.size(), isRun});
	}
	for(const auto& [routine, count] : skipped)
		std::printf("skipped %5d cases of %08x, not ported\n", count, routine);

	auto sram = std::make_unique<Sram>();
	auto expected = std::make_unique<Sram>();
	std::map<uint32_t, int> passed, failed;
	for(const auto& c : cases)
	{
		const auto it = ported().find(c.routine);
		if(it == ported().end())
			continue;
		load(*sram, c.before);
		load(*expected, c.after);
		syntakt::Globals globals;
		globals.rng = {static_cast<int32_t>(c.rngBefore[0]), static_cast<int32_t>(c.rngBefore[1])};
		Context ctx{*sram, globals, rom, c.args};
		it->second.run(ctx);

		int diffs = 0;
		char first[128] = "";
		auto differ = [&](const char* _what, const uint32_t _got, const uint32_t _want)
		{
			if(!diffs++)
				std::snprintf(first, sizeof first, "%s: %08x, original %08x", _what, _got, _want);
		};
		if(it->second.returns && ctx.d0 != c.d0)
			differ("d0", ctx.d0, c.d0);
		for(int i = 0; i < 2; ++i)
			if(static_cast<uint32_t>(globals.rng[i]) != c.rngAfter[i])
				differ("generator", static_cast<uint32_t>(globals.rng[i]), c.rngAfter[i]);
		for(size_t i = 0; i < sram->words.size(); ++i)
		{
			if(sram->words[i] == expected->words[i])
				continue;
			char where[16];
			std::snprintf(where, sizeof where, "%08x", Sram::Base + 4 * static_cast<uint32_t>(i));
			differ(where, static_cast<uint32_t>(sram->words[i]), static_cast<uint32_t>(expected->words[i]));
		}
		if(diffs)
		{
			if(failed[c.routine]++ < 3)
				std::printf("%s(%08x, %08x): %d words differ, first %s\n", it->second.name.c_str(), c.args[0],
					c.args.size() > 1 ? c.args[1] : 0, diffs, first);
		}
		else
		{
			++passed[c.routine];
		}
	}

	bool ok = true;
	for(const auto& [routine, p] : ported())
	{
		const int pass = passed[routine], fail = failed[routine];
		if(!pass && !fail)
			continue;
		std::printf("%-14s %5d cases, %4d differ\n", p.name.c_str(), pass + fail, fail);
		ok = ok && fail == 0;
	}
	// The voices run on: consecutive blocks of renderVoices where the port carries its own voice states and decimator
	// histories from block to block, and only the inputs (parameters, pitch, trigs) come from the recording. An error
	// that accumulates over blocks shows here, where each case alone starts from the original's state
	int continuous = 0, continuousFailed = 0;
	auto carried = std::make_unique<Sram>();
	syntakt::Globals carriedGlobals;
	for(const auto& [first, end, isRun] : files)
	{
		if(!isRun)
			continue;
		for(size_t i = 0; i < end - first; ++i)
		{
			const Case& c = cases[first + i];
			load(*sram, c.before);
			load(*expected, c.after);
			if(i == 0)
			{
				*carried = *sram;
				carriedGlobals.rng = {static_cast<int32_t>(c.rngBefore[0]), static_cast<int32_t>(c.rngBefore[1])};
			}
			// The voice states and the histories are the port's own; everything else is this block's input
			const uint32_t ownedEnd = Voice::addressOf(syntakt::Voices);
			constexpr uint32_t histories = syntakt::Histories;
			constexpr uint32_t historiesEnd = histories + syntakt::HistoryStride * syntakt::Voices;
			for(uint32_t a = Sram::Base; a < Sram::Base + Sram::Size; a += 4)
			{
				const bool owned = a < ownedEnd || (a >= histories && a < historiesEnd);
				if(!owned)
					carried->at(a) = sram->at(a);
			}
			syntakt::renderVoices(*carried, carriedGlobals, rom, c.args[0], c.args[1],
				static_cast<int32_t>(c.args[2]), static_cast<int32_t>(c.args[3]));
			++continuous;
			for(size_t w = 0; w < carried->words.size(); ++w)
			{
				if(carried->words[w] == expected->words[w])
					continue;
				if(continuousFailed++ < 3)
					std::printf("continuous block %zu: %08x: %08x, original %08x\n", i,
						Sram::Base + 4 * static_cast<uint32_t>(w), static_cast<uint32_t>(carried->words[w]),
						static_cast<uint32_t>(expected->words[w]));
				break;
			}
		}
	}
	if(continuous)
	{
		std::printf("%-14s %5d blocks, %4d differ\n", "continuous", continuous, continuousFailed);
		ok = ok && continuousFailed == 0;
	}

	std::printf(ok && !passed.empty() ? "PASS\n" : "FAIL\n");
	return ok && !passed.empty() ? 0 : 1;
}
