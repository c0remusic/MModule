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
// SYNTAKT_MAIN_OUTPUTS, optional, names what the original voice CPU played of each block a sequence's original segment
// built (tools/block3.py and tools/mix3.py sequence; several files ';'-separated, in the vectors' order): the port's
// own blocks then play on the ported voice CPU (SYNTAKT_SECTION7, SYNTAKT_SAMPLES), and its eight tracks must match
// sample for sample: the stems from the kit down. The voice outputs a sequence's next mix (0x4008f1ca) reads are then
// the ported voice CPU's, not the original's the harness wrote, so that the buses, the effects and the Overbridge
// frame are checked from the kit's parameters through the port alone.

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

#include "stBoot.h"
#include "stMainBlock.h"
#include "stMainBus.h"
#include "stMainControl.h"
#include "stMainDelay.h"
#include "stMainFilter.h"
#include "stMainFx.h"
#include "stMainLfo.h"
#include "stMainMemory.h"
#include "stMainMix.h"
#include "stMainModulation.h"
#include "stMainOutput.h"
#include "stMainPitch.h"
#include "stMainReverb.h"
#include "stMemoryMap.h"

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
		syntakt::main::Emac4 acc;	// the mix's routines share it; it starts cleared at the case's MACSR

		int32_t arg(const size_t _i) const { return static_cast<int32_t>(c.args[_i]); }
		uint32_t addr(const size_t _i) const { return c.args[_i]; }

		// _n longs of the memory at argument _i: a case's gains or steps
		std::vector<int32_t> longs(const size_t _i, const size_t _n) const
		{
			std::vector<int32_t> v(_n);
			for(size_t k = 0; k < _n; ++k)
				v[k] = mem.word(addr(_i) + 4 * static_cast<uint32_t>(k));
			return v;
		}
	};

	void sum(Context& _c, const int _channels)
	{
		const auto steps = _c.longs(3, static_cast<size_t>(_channels));
		syntakt::main::sumChannels(_c.mem, _c.acc, _channels, _c.addr(0), _c.addr(1), _c.addr(2), steps.data());
	}

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
			{0x400a49a6, {"block", Returns::Nothing, [](Context& _c)
				{
					// The masks as block3.py records them: trigs, gate-offs, LFO trigs, filter trigs and gate-offs
					m::buildBlock(_c.mem, {_c.arg(0), _c.arg(1), _c.arg(2), _c.arg(3), _c.arg(4)});
				}}},
			// The mix and its effects (tools/mix3.py): gains and steps at the arguments' addresses
			{0x4008ef14, {"sum10", Returns::Nothing, [](Context& _c) { sum(_c, 10); }}},
			{0x4008efac, {"sum9", Returns::Nothing, [](Context& _c) { sum(_c, 9); }}},
			{0x4008f03e, {"sum8", Returns::Nothing, [](Context& _c) { sum(_c, 8); }}},
			{0x4008f1b4, {"copySide", Returns::Nothing, [](Context& _c)
				{
					m::copySide(_c.mem, _c.addr(0), _c.addr(1));
				}}},
			{0x4008ee80, {"mixBuses", Returns::Nothing, [](Context& _c)
				{
					auto gains = _c.longs(2, 2);
					const auto steps = _c.longs(3, 2);
					m::mixBuses(_c.mem, _c.acc, _c.addr(0), _c.addr(1), gains.data(), steps.data());
					_c.mem.setWord(_c.addr(2), gains[0]);
					_c.mem.setWord(_c.addr(2) + 4, gains[1]);
				}}},
			{0x4008eeca, {"scaleBus", Returns::Nothing, [](Context& _c)
				{
					m::scaleBus(_c.mem, _c.acc, _c.addr(0), _c.arg(1));
				}}},
			{0x4008febc, {"rampBuffer", Returns::Nothing, [](Context& _c)
				{
					m::rampBuffer(_c.mem, _c.acc, _c.addr(0), _c.arg(1), _c.arg(2));
				}}},
			{0x40097a90, {"analogLevel", Returns::D0, [](Context& _c)
				{
					_c.out.d0 = m::analogLevel(_c.mem, _c.acc, _c.arg(0), _c.arg(1));
				}}},
			{0x40097414, {"prepareEffects", Returns::Nothing, [](Context& _c)
				{
					m::prepareEffects(_c.mem, _c.acc, _c.addr(0));
				}}},
			{0x40096890, {"delay", Returns::Nothing, [](Context& _c)
				{
					m::delay(_c.mem, _c.acc, _c.addr(0), _c.addr(1), _c.addr(2));
				}}},
			{0x40096802, {"writeLines", Returns::Nothing, [](Context& _c) { m::writeLines(_c.mem); }}},
			{0x40096e3c, {"reverb", Returns::Nothing, [](Context& _c)
				{
					m::reverb(_c.mem, _c.acc, _c.addr(0), _c.addr(1), _c.addr(2));
				}}},
			{0x40096dba, {"advanceLines", Returns::Nothing, [](Context& _c) { m::advanceReverbLines(_c.mem); }}},
			{0x40096668, {"delayInit", Returns::Nothing, [](Context& _c) { m::delayInit(_c.mem); }}},
			{0x40096b86, {"reverbInit", Returns::Nothing, [](Context& _c) { m::reverbInit(_c.mem); }}},
			{0x4008f1de, {"trackSends", Returns::Nothing, [](Context& _c)
				{
					m::MixFrame frame;
					m::trackSends(_c.mem, _c.acc, _c.addr(0), frame);
				}}},
			{0x4008f1ca, {"mixOutput", Returns::D0, [](Context& _c)
				{
					_c.out.d0 = m::mixOutput(_c.mem, _c.acc, _c.addr(2));
				}}},
		};
		return routines;
	}

	// A sample set as twinshot_samples.py writes it (as stEngineOracleTest reads it): "STS1", the 64 directory entries
	// (u32 length, u32 pointer), u32 base, u32 size, size bytes of data, big-endian
	bool readSamples(const std::string& _path, syntakt::Samples& _samples)
	{
		const std::vector<uint8_t> blob = syntakt::readFile(_path);
		constexpr size_t header = 4 + 8 * 64 + 8;
		if(blob.size() < header || std::string(blob.begin(), blob.begin() + 4) != "STS1")
			return false;
		for(size_t i = 0; i < _samples.directory.size(); ++i)
			_samples.directory[i] = {syntakt::be32(&blob[4 + 8 * i]), syntakt::be32(&blob[8 + 8 * i])};
		_samples.base = syntakt::be32(&blob[4 + 8 * 64]);
		const uint32_t size = syntakt::be32(&blob[8 + 8 * 64]);
		if(blob.size() != header + size)
			return false;
		_samples.data.assign(blob.begin() + header, blob.end());
		return true;
	}

	// block3.py's ST1O: u32 count, then per block the original voice CPU's 8 x 32 samples, little-endian
	bool readOutputs(const std::string& _path, std::vector<int32_t>& _samples)
	{
		const std::vector<uint8_t> blob = syntakt::readFile(_path);
		if(blob.size() < 8 || std::string(blob.begin(), blob.begin() + 4) != "ST1O")
			return false;
		const uint32_t count = uint32_t(blob[4]) | blob[5] << 8 | blob[6] << 16 | uint32_t(blob[7]) << 24;
		constexpr size_t perBlock = 4 * syntakt::Voices * syntakt::BlockSize;
		if(blob.size() != 8 + perBlock * count)
			return false;
		for(size_t i = 0; i < blob.size() / 4 - 2; ++i)
		{
			const uint8_t* p = &blob[8 + 4 * i];
			_samples.push_back(static_cast<int32_t>(uint32_t(p[0]) | p[1] << 8 | p[2] << 16 | uint32_t(p[3]) << 24));
		}
		return true;
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
	// End to end, SYNTAKT_MAIN_OUTPUTS (optional): the original voice CPU's output of each block the original segment
	// built in a sequence (block3.py sequence). Each block the port builds there then plays on the ported voice CPU
	// (SYNTAKT_SECTION7, SYNTAKT_SAMPLES), booted at each sequence's start, and its eight tracks must match
	constexpr uint32_t BlockSegment = 0x400a49a6;
	std::vector<int32_t> outputs;
	std::vector<int32_t> section7;
	syntakt::Samples samples;
	std::unique_ptr<syntakt::Sram> voiceSram;
	syntakt::Globals voiceGlobals;
	const char* outputsPath = std::getenv("SYNTAKT_MAIN_OUTPUTS");
	if(outputsPath && *outputsPath)
	{
		const char* section7Path = std::getenv("SYNTAKT_SECTION7");
		const char* samplesPath = std::getenv("SYNTAKT_SAMPLES");
		if(!section7Path || !*section7Path || !samplesPath || !*samplesPath)
		{
			std::printf("FAIL: SYNTAKT_MAIN_OUTPUTS needs SYNTAKT_SECTION7 and SYNTAKT_SAMPLES\n");
			return 1;
		}
		section7 = syntakt::loadSection7(section7Path);
		// Several files (';'-separated) follow each other, in the order of their sequences in the vector files
		bool read = !section7.empty() && readSamples(samplesPath, samples);
		std::stringstream outputPaths(outputsPath);
		for(std::string path; read && std::getline(outputPaths, path, ';');)
			read = readOutputs(path, outputs);
		if(!read)
		{
			std::printf("FAIL: cannot read section 7, the samples or the outputs\n");
			return 1;
		}
		voiceSram = std::make_unique<syntakt::Sram>();
	}
	const syntakt::Rom voiceRom{section7.data(), &samples};
	constexpr size_t blockSamples = syntakt::Voices * syntakt::BlockSize;
	size_t played = 0;
	int playedDiffer = 0;
	std::vector<int32_t> previousVoice(blockSamples);

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
				if(voiceSram)
					syntakt::boot(*voiceSram, voiceGlobals, voiceRom);
			}
			for(const auto& r : c.writes)
				for(size_t i = 0; i < r.bytes.size(); ++i)
					owned.erase(r.address + static_cast<uint32_t>(i));
			apply(*carried, c.writes);
			if(voiceSram)
			{
				// End to end, the voice outputs the mix reads are the ported voice CPU's of the block before, as the
				// eDMA brings them, not the original's that the harness wrote: the mix's read set then checks them
				if(!c.args.empty())
					previousVoice.assign(blockSamples, 0);
				for(uint32_t i = 0; i < blockSamples; ++i)
				{
					carried->setWord(syntakt::main::VoiceOutputs + 4 * i, previousVoice[i]);
					for(uint32_t b = 0; b < 4; ++b)
						owned.insert(syntakt::main::VoiceOutputs + 4 * i + b);
				}
			}
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
		ctx.acc.setMacsr(c.macsr);
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
		// The lines' pages, either side's: a page one side lacks holds zeros
		const std::vector<uint8_t> zeros(Memory::PageSize);
		for(const auto& [base, page] : mem.lines())
		{
			const auto w = expected.lines().find(base);
			compare(page, w == expected.lines().end() ? zeros : w->second, base);
		}
		for(const auto& [base, page] : expected.lines())
			if(!mem.lines().count(base))
				compare(zeros, page, base);
		if(c.sequence)
		{
			*carried = mem;
			for(const auto& r : c.writes)
				for(size_t i = 0; i < r.bytes.size(); ++i)
					owned.insert(r.address + static_cast<uint32_t>(i));

			// The port's own block, played by the ported voice CPU against the original's playing the original's
			if(voiceSram && c.routine == BlockSegment)
			{
				const size_t first = blockSamples * played;
				if(first + blockSamples > outputs.size())
				{
					std::printf("FAIL: more blocks than the outputs hold\n");
					return 1;
				}
				uint8_t block[syntakt::main::BlockBytes];
				for(uint32_t i = 0; i < syntakt::main::BlockBytes; ++i)
					block[i] = mem.byte(syntakt::main::Block + i);
				syntakt::runBlock(*voiceSram, voiceGlobals, voiceRom, block);
				int differ = 0;
				for(size_t i = 0; i < blockSamples; ++i)
				{
					const int32_t got = voiceSram->at(syntakt::VoiceOut + 4 * static_cast<uint32_t>(i));
					previousVoice[i] = got;
					if(got != outputs[first + i] && !differ++ && playedDiffer < 3)
						std::printf("end to end, block %zu: track %zu sample %zu: %08x, original %08x\n", played,
							i / syntakt::BlockSize, i % syntakt::BlockSize, static_cast<uint32_t>(got),
							static_cast<uint32_t>(outputs[first + i]));
				}
				playedDiffer += differ != 0;
				++played;
			}
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
	if(voiceSram)
	{
		// Every block the outputs hold must have been played
		std::printf("%-16s %5zu blocks, %4d differ (8 tracks x 32 samples each)\n", "end to end", played, playedDiffer);
		ok = ok && playedDiffer == 0 && played > 0 && played * blockSamples == outputs.size();
	}
	ok = ok && (!passed.empty() || blocks);
	std::printf(ok ? "PASS\n" : "FAIL\n");
	return ok ? 0 : 1;
}
