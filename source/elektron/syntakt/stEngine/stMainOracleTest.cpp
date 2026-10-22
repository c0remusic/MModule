// The ported routines of the Syntakt's main CPU (CPU #1, OS 1.41, section 3) against the original ones, word for word.
//
// The vectors are recorded from the original code run alone on Unicorn (tools/vectors3.py over tools/oracle3.py,
// outside the repository): each call of a routine with its stack arguments and MACSR, the bytes it read with their
// value before the call, the bytes it wrote with their value after, and d0/d1 at return. Each case loads the read set
// into the port's memory, runs the ported routine, and compares the whole memory with the read set overlaid with the
// write set, and the returned registers. A sequence file (tools/sequence3.py) runs the interrupt's control routines
// block after block on one memory the port carries over, so that a drift shows at the first block it changes a byte
// the original reads. SYNTAKT_SECTION3 names the user's section 3, SYNTAKT_MAIN_VECTORS the vector
// files (';'-separated): both hold firmware-derived data and stay outside the repository. Exit 77 without them.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#include "stMainControl.h"
#include "stMainFilter.h"
#include "stMainLfo.h"
#include "stMainMemory.h"
#include "stMainMix.h"
#include "stMainModulation.h"
#include "stMainPitch.h"

namespace
{
	using syntakt::main::Memory;
	using syntakt::main::Pair;

	struct Run
	{
		uint32_t address = 0;
		std::vector<uint8_t> bytes;
	};

	struct Case
	{
		uint32_t routine = 0;
		uint32_t macsr = 0;
		std::vector<uint32_t> args;
		uint32_t d0 = 0, d1 = 0;
		std::vector<Run> reads, writes;
		bool sequence = false;	// from an ST1S file: the memory carries over from the case before
	};

	bool readU32(std::ifstream& _f, uint32_t& _v) { return static_cast<bool>(_f.read(reinterpret_cast<char*>(&_v), 4)); }

	bool readRuns(std::ifstream& _f, std::vector<Run>& _runs)
	{
		uint32_t n = 0;
		if(!readU32(_f, n))
			return false;
		for(uint32_t i = 0; i < n; ++i)
		{
			Run r;
			uint32_t length = 0;
			if(!readU32(_f, r.address) || !readU32(_f, length) || length > 0x10000)
				return false;
			r.bytes.resize(length);
			if(!_f.read(reinterpret_cast<char*>(r.bytes.data()), length))
				return false;
			_runs.push_back(std::move(r));
		}
		return true;
	}

	// The ST1V format of vectors3.py, or the ST1S of sequence3.py
	bool readCases(const std::string& _path, std::vector<Case>& _cases)
	{
		std::ifstream f(_path, std::ios::binary);
		char magic[4];
		uint32_t count = 0;
		if(!f.read(magic, 4) || !readU32(f, count))
			return false;
		const std::string kind(magic, 4);
		if(kind != "ST1V" && kind != "ST1S")
			return false;
		for(uint32_t i = 0; i < count; ++i)
		{
			Case c;
			uint32_t nargs = 0;
			if(!readU32(f, c.routine) || !readU32(f, c.macsr) || !readU32(f, nargs) || nargs > 16)
				return false;
			c.args.resize(nargs);
			for(auto& a : c.args)
				readU32(f, a);
			if(!readU32(f, c.d0) || !readU32(f, c.d1) || !readRuns(f, c.reads) || !readRuns(f, c.writes))
				return false;
			c.sequence = kind == "ST1S";
			_cases.push_back(std::move(c));
		}
		return true;
	}

	struct Context
	{
		Memory& mem;
		const Case& c;
		Pair out;

		int32_t arg(const size_t _i) const { return static_cast<int32_t>(c.args[_i]); }
		uint32_t addr(const size_t _i) const { return c.args[_i]; }
	};

	enum class Returns
	{
		Nothing,
		D0,
		D0D1
	};

	struct Ported
	{
		std::string name;
		Returns returns;
		std::function<void(Context&)> run;
	};

	const std::map<uint32_t, Ported>& ported()
	{
		namespace m = syntakt::main;
		static const std::map<uint32_t, Ported> routines = {
			{0x4008ed48, {"gain", Returns::D0D1, [](Context& _c)
				{
					// The mix calls it with MACSR 0x20 in its track loop and 0xa0 for the meters
					const auto& a = _c.c.args;
					_c.out = (_c.c.macsr & 0x80) ? m::trackGain<true>(_c.mem, a[0], a[1], a[2], a[3])
						: m::trackGain<false>(_c.mem, a[0], a[1], a[2], a[3]);
				}}},
			{0x400a171a, {"pan", Returns::D0D1, [](Context& _c) { _c.out = m::pan(_c.arg(0)); }}},
			{0x400a16bc, {"sine", Returns::D0, [](Context& _c) { _c.out.d0 = m::parabolicSine(_c.arg(0)); }}},
			{0x40097952, {"smooth", Returns::D0, [](Context& _c)
				{
					_c.out.d0 = static_cast<int32_t>(m::smooth(_c.mem, _c.addr(0)));
				}}},
			{0x400978b4, {"lock", Returns::Nothing, [](Context& _c)
				{
					m::lock(_c.mem, _c.arg(0), _c.addr(1), _c.addr(2));
				}}},
			{0x40097802, {"restoreFxLocks", Returns::Nothing, [](Context& _c)
				{
					m::restoreFxLocks(_c.mem, _c.addr(0), _c.addr(1));
				}}},
			{0x40097760, {"restoreLocks", Returns::Nothing, [](Context& _c)
				{
					m::restoreLocks(_c.mem, _c.arg(0), _c.addr(1), _c.addr(2));
				}}},
			{0x40097874, {"clearLocks", Returns::Nothing, [](Context& _c) { m::clearLocks(_c.mem, _c.arg(0)); }}},
			{0x40097894, {"setParameter", Returns::Nothing, [](Context& _c)
				{
					m::setParameter(_c.mem, _c.arg(0), _c.arg(1), _c.addr(2));
				}}},
			{0x400976b4, {"snapTrack", Returns::Nothing, [](Context& _c)
				{
					m::snapTrack(_c.mem, _c.arg(0), _c.addr(1));
				}}},
			{0x400976ee, {"snapRange", Returns::Nothing, [](Context& _c)
				{
					m::snapRange(_c.mem, _c.arg(0), _c.addr(1), _c.arg(2), _c.arg(3));
				}}},
			{0x40097732, {"snapParameter", Returns::Nothing, [](Context& _c)
				{
					m::snapParameter(_c.mem, _c.arg(0), _c.arg(1), _c.arg(2));
				}}},
			{0x40097aac, {"modulate", Returns::Nothing, [](Context& _c)
				{
					m::modulate(_c.mem, _c.addr(0), _c.arg(1), _c.addr(2));
				}}},
			{0x40097afc, {"modulationMatrix", Returns::Nothing, [](Context& _c)
				{
					m::modulationMatrix(_c.mem, _c.addr(0));
				}}},
			{0x40115ec8, {"exp2", Returns::D0, [](Context& _c) { _c.out.d0 = m::exp2(_c.arg(0)); }}},
			{0x40118c74, {"lfoFadeStep", Returns::D0, [](Context& _c) { _c.out.d0 = m::lfoFadeStep(_c.arg(0)); }}},
			{0x40118cf0, {"lfoFadeStart", Returns::D0, [](Context& _c) { _c.out.d0 = m::lfoFadeStart(_c.arg(0)); }}},
			{0x40118d8e, {"lfo TRI", Returns::D0, [](Context& _c) { _c.out.d0 = m::lfoWave(0, _c.arg(0)); }}},
			{0x40118da4, {"lfo SIN", Returns::D0, [](Context& _c) { _c.out.d0 = m::lfoWave(1, _c.arg(0)); }}},
			{0x40118d70, {"lfo SQR", Returns::D0, [](Context& _c) { _c.out.d0 = m::lfoWave(2, _c.arg(0)); }}},
			{0x40118d82, {"lfo SAW", Returns::D0, [](Context& _c) { _c.out.d0 = m::lfoWave(3, _c.arg(0)); }}},
			{0x40118dfe, {"lfo EXP", Returns::D0, [](Context& _c) { _c.out.d0 = m::lfoWave(4, _c.arg(0)); }}},
			{0x40118dee, {"lfo RMP", Returns::D0, [](Context& _c) { _c.out.d0 = m::lfoWave(5, _c.arg(0)); }}},
			{0x40118ef8, {"lfoRandom", Returns::D0, [](Context& _c) { _c.out.d0 = m::lfoRandom(_c.mem); }}},
			{0x40119266, {"lfos", Returns::Nothing, [](Context& _c)
				{
					m::lfos(_c.mem, _c.addr(0), _c.arg(1), _c.arg(2));
				}}},
			{0x40119fe8, {"portamento", Returns::D0, [](Context& _c)
				{
					_c.out.d0 = m::portamento(_c.mem, _c.arg(0), _c.arg(1), _c.arg(2), _c.arg(3), _c.addr(4));
				}}},
			{0x400a4ade, {"pitch loop", Returns::Nothing, [](Context& _c)
				{
					m::pitches(_c.mem, _c.addr(0), _c.arg(1));
				}}},
			{0x4009652c, {"filterEnvelopes", Returns::Nothing, [](Context& _c)
				{
					m::filterEnvelopes(_c.mem, _c.addr(0), _c.arg(1), _c.arg(2), _c.arg(3));
				}}},
			{0x4009664e, {"filterEnvelope", Returns::D0, [](Context& _c)
				{
					_c.out.d0 = m::filterEnvelope(_c.mem, _c.arg(0));
				}}},
			{0x400956bc, {"cutoff", Returns::D0, [](Context& _c)
				{
					_c.out.d0 = m::cutoff(_c.mem, _c.addr(0), _c.arg(1), _c.arg(2), _c.arg(3));
				}}},
		};
		return routines;
	}

	// Writes _runs into _mem; false when a byte lies outside the port's memory
	bool apply(Memory& _mem, const std::vector<Run>& _runs)
	{
		for(const auto& r : _runs)
			for(size_t i = 0; i < r.bytes.size(); ++i)
				_mem.setByte(r.address + static_cast<uint32_t>(i), r.bytes[i]);
		return _mem.fault() == 0;
	}
}

int main()
{
	setvbuf(stdout, nullptr, _IONBF, 0);

	const char* section3Path = std::getenv("SYNTAKT_SECTION3");
	const char* vectorPaths = std::getenv("SYNTAKT_MAIN_VECTORS");
	if(!section3Path || !*section3Path || !vectorPaths || !*vectorPaths)
	{
		std::printf("SKIP: set SYNTAKT_SECTION3 and SYNTAKT_MAIN_VECTORS\n");
		return 77;
	}
	const auto rom = syntakt::main::loadSection3(section3Path);
	if(rom.bytes.empty())
	{
		std::printf("FAIL: %s is not OS 1.41's section 3\n", section3Path);
		return 1;
	}

	std::vector<Case> cases;
	std::stringstream paths(vectorPaths);
	for(std::string path; std::getline(paths, path, ';');)
	{
		if(!readCases(path, cases))
		{
			std::printf("FAIL: cannot read the vectors in %s\n", path.c_str());
			return 1;
		}
	}
	// A sequence's memory: what the port left, and the bytes its cases wrote, which their read sets then check
	// rather than set. A case of routine 0 holds the bytes the harness wrote; its argument 1 starts a sequence
	constexpr uint32_t Inputs = 0;
	std::unique_ptr<Memory> carried;
	std::unordered_set<uint32_t> owned;
	int blocks = 0;

	std::map<uint32_t, int> skipped, passed, failed, seqPassed, seqFailed;
	for(const auto& c : cases)
	{
		if(c.sequence && c.routine == Inputs)
		{
			if(!c.args.empty() || !carried)
			{
				carried = std::make_unique<Memory>(rom);
				owned.clear();
			}
			for(const auto& r : c.writes)
				for(size_t i = 0; i < r.bytes.size(); ++i)
					owned.erase(r.address + static_cast<uint32_t>(i));
			apply(*carried, c.writes);
			++blocks;
			continue;
		}
		const auto it = ported().find(c.routine);
		if(it == ported().end())
		{
			++skipped[c.routine];
			continue;
		}
		Memory mem = c.sequence && carried ? *carried : Memory(rom);
		int drift = 0;
		uint32_t driftAt = 0;
		for(const auto& r : c.reads)
		{
			for(size_t i = 0; i < r.bytes.size(); ++i)
			{
				const uint32_t a = r.address + static_cast<uint32_t>(i);
				if(c.sequence && owned.count(a))
				{
					if(mem.byte(a) != r.bytes[i] && !drift++)
						driftAt = a;
					continue;
				}
				mem.setByte(a, r.bytes[i]);
			}
		}
		if(mem.fault())
		{
			std::printf("FAIL: a case of %s reads %08x, outside the port's memory\n", it->second.name.c_str(),
				mem.fault());
			return 1;
		}
		Memory expected = mem;
		apply(expected, c.writes);
		Context ctx{mem, c, {}};
		it->second.run(ctx);

		int diffs = 0;
		char first[128] = "";
		auto differ = [&](const char* _what, const uint32_t _got, const uint32_t _want)
		{
			if(!diffs++)
				std::snprintf(first, sizeof first, "%s: %08x, original %08x", _what, _got, _want);
		};
		if(drift)
			differ("state carried over that the original reads otherwise at", driftAt, 0);
		if(mem.fault())
			differ("access outside the memory", mem.fault(), 0);
		if(it->second.returns != Returns::Nothing && static_cast<uint32_t>(ctx.out.d0) != c.d0)
			differ("d0", static_cast<uint32_t>(ctx.out.d0), c.d0);
		if(it->second.returns == Returns::D0D1 && static_cast<uint32_t>(ctx.out.d1) != c.d1)
			differ("d1", static_cast<uint32_t>(ctx.out.d1), c.d1);
		auto compare = [&](const std::vector<uint8_t>& _got, const std::vector<uint8_t>& _want, const uint32_t _base)
		{
			for(size_t i = 0; i < _got.size(); i += 4)
			{
				const uint32_t g = uint32_t(_got[i]) << 24 | _got[i + 1] << 16 | _got[i + 2] << 8 | _got[i + 3];
				const uint32_t w = uint32_t(_want[i]) << 24 | _want[i + 1] << 16 | _want[i + 2] << 8 | _want[i + 3];
				if(g == w)
					continue;
				char where[16];
				std::snprintf(where, sizeof where, "%08x", _base + static_cast<uint32_t>(i));
				differ(where, g, w);
			}
		};
		for(size_t r = 0; r < mem.regions().size(); ++r)
			compare(mem.regions()[r].bytes, expected.regions()[r].bytes, mem.regions()[r].base);
		if(c.sequence)
		{
			*carried = mem;
			for(const auto& r : c.writes)
				for(size_t i = 0; i < r.bytes.size(); ++i)
					owned.insert(r.address + static_cast<uint32_t>(i));
		}
		if(diffs)
		{
			if((c.sequence ? seqFailed : failed)[c.routine]++ < 3)
				std::printf("%s%s(%08x, %08x): %d differ, first %s\n", c.sequence ? "sequence block, " : "",
					it->second.name.c_str(), c.args.empty() ? 0 : c.args[0], c.args.size() > 1 ? c.args[1] : 0, diffs,
					first);
		}
		else
		{
			++(c.sequence ? seqPassed : passed)[c.routine];
		}
	}

	for(const auto& [routine, count] : skipped)
		std::printf("skipped %5d cases of %08x, not ported\n", count, routine);
	bool ok = true;
	for(const auto& [routine, p] : ported())
	{
		const int pass = passed[routine], fail = failed[routine];
		if(!pass && !fail)
			continue;
		std::printf("%-16s %5d cases, %4d differ\n", p.name.c_str(), pass + fail, fail);
		ok = ok && fail == 0;
	}
	if(blocks)
	{
		int pass = 0, fail = 0;
		for(const auto& [routine, n] : seqPassed)
			pass += n;
		for(const auto& [routine, n] : seqFailed)
			fail += n;
		std::printf("%-16s %5d blocks, %5d calls, %4d differ\n", "sequence", blocks, pass + fail, fail);
		ok = ok && fail == 0 && pass > 0;
	}
	ok = ok && (!passed.empty() || blocks);
	std::printf(ok ? "PASS\n" : "FAIL\n");
	return ok ? 0 : 1;
}
