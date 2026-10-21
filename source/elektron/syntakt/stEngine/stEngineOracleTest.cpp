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

#include "stBdModern.h"
#include "stEngine.h"
#include "stStages.h"
#include "stVoice.h"

namespace
{
	using syntakt::Sram;
	using syntakt::Voice;

	uint32_t be32(const uint8_t* _p)
	{
		return uint32_t(_p[0]) << 24 | uint32_t(_p[1]) << 16 | uint32_t(_p[2]) << 8 | _p[3];
	}

	std::vector<uint8_t> readFile(const std::string& _path)
	{
		std::ifstream f(_path, std::ios::binary);
		return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
	}

	struct Case
	{
		uint32_t routine = 0;
		std::vector<uint32_t> args;
		uint32_t d0 = 0;
		uint32_t rngBefore[2]{}, rngAfter[2]{};
		std::vector<uint8_t> before, after;
	};

	bool readCases(const std::string& _path, std::vector<Case>& _cases)
	{
		std::ifstream f(_path, std::ios::binary);
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
			c.args.resize(nargs);
			f.read(reinterpret_cast<char*>(c.args.data()), 4 * nargs);
			f.read(reinterpret_cast<char*>(&c.d0), 4);
			f.read(reinterpret_cast<char*>(c.rngBefore), 8);
			f.read(reinterpret_cast<char*>(c.rngAfter), 8);
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
		const char* name;
		bool returns;	// the routine's d0 is compared too
		std::function<void(Context&)> run;
	};

	const std::map<uint32_t, Ported>& ported()
	{
		static const std::map<uint32_t, Ported> routines = {
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
					syntakt::phaseIncrements48k(static_cast<int32_t>(_c.args[0]), v, _c.sram, _c.args[2],
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
			{0x40006968, {"bdUpdate", false, [](Context& _c)
				{
					auto v = _c.voice(1);
					syntakt::bdModern::update(static_cast<int32_t>(_c.args[0]), v, _c.args[2], _c.sram, _c.globals,
						_c.rom);
				}}},
			{0x40006c9e, {"bdRender", false, [](Context& _c)
				{
					auto v = _c.voice(1);
					syntakt::bdModern::render(_c.block(0), v, _c.sram, _c.globals, _c.rom);
				}}},
			{0x40004324, {"renderVoices", false, [](Context& _c)
				{
					syntakt::renderVoices(_c.sram, _c.globals, _c.rom, _c.args[0], _c.args[1],
						static_cast<int32_t>(_c.args[2]), static_cast<int32_t>(_c.args[3]));
				}}},
		};
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
	const auto section7 = readFile(section7Path);
	if(section7.size() != 383760)
	{
		std::printf("FAIL: %s is not OS 1.41's section 7 (%zu bytes)\n", section7Path, section7.size());
		return 1;
	}
	std::vector<int32_t> romWords(section7.size() / 4);
	for(size_t i = 0; i < romWords.size(); ++i)
		romWords[i] = static_cast<int32_t>(be32(section7.data() + 4 * i));
	const syntakt::Rom rom{romWords.data()};

	std::vector<Case> cases;
	std::vector<std::pair<size_t, size_t>> files;	// each file's cases, [first, end)
	std::stringstream paths(vectorPaths);
	for(std::string path; std::getline(paths, path, ';');)
	{
		const size_t first = cases.size();
		if(!readCases(path, cases))
		{
			std::printf("FAIL: cannot read the vectors in %s\n", path.c_str());
			return 1;
		}
		files.emplace_back(first, cases.size());
	}

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
				std::printf("%s(%08x, %08x): %d words differ, first %s\n", it->second.name, c.args[0],
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
		std::printf("%-14s %5d cases, %4d differ\n", p.name, pass + fail, fail);
		ok = ok && fail == 0;
	}
	// The voices run on: consecutive blocks of renderVoices where the port carries its own voice states and decimator
	// histories from block to block, and only the inputs (parameters, pitch, trigs) come from the recording. An error
	// that accumulates over blocks shows here, where each case alone starts from the original's state. A file is such a
	// run when it holds nothing but consecutive blocks of renderVoices (vectors.py 40004324 --prob 1)
	int continuous = 0, continuousFailed = 0;
	auto carried = std::make_unique<Sram>();
	syntakt::Globals carriedGlobals;
	for(const auto& [first, end] : files)
	{
		bool isRun = end - first > 1;
		for(size_t i = first; i < end; ++i)
			isRun = isRun && cases[i].routine == 0x40004324;
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
			constexpr uint32_t ownedEnd = 0x80003840, histories = 0x80008a60, historiesEnd = 0x80008be0;
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
