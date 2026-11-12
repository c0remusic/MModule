// The analog voices' control (edDrumsAnalog.h) against vectors of the original routines, recorded on an emulator from
// the user's own Syntakt OS 1.41 and kept outside the repository (ANALOG_VECTORS: the folder that holds helpers.sah and
// syntakt.sav; exit 77 without it). Every helper's results, then every byte and switch each machine's block writes, from
// the memory it read; a byte it neither read nor wrote holds a poison value it must leave.
//
//   edDrumsAnalogControlTest [machine index] [--all]   (--all: every failing case, not only the first of each machine)

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "edDrumsAnalogPort.h"

using namespace elektron::drums::analog;

namespace
{
	std::vector<uint8_t> readFile(const std::string& _path)
	{
		std::ifstream f(_path, std::ios::binary);
		return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	}

	struct Reader
	{
		const std::vector<uint8_t>& d;
		size_t at = 0;
		bool ok() const { return at <= d.size(); }
		uint32_t u(const int _bytes)
		{
			uint32_t v = 0;
			for(int i = 0; i < _bytes; ++i)
				v |= static_cast<uint32_t>(at + i < d.size() ? d[at + i] : 0) << (8 * i);
			at += static_cast<size_t>(_bytes);
			return v;
		}
	};

	const char* const helperNames[] = {"pitchWord", "modPitchWord", "expPitch", "exp2", "log2", "decayTicks",
		"decayTicksDefault", "velocitySquare", "knob", "knobPair"};

	int testHelpers(const std::vector<uint8_t>& _d)
	{
		Reader r{_d};
		if(r.u(4) != 0x48415453)	// "STAH"
		{
			std::printf("helpers: not a helper vector file\n");
			return 1;
		}
		const uint32_t count = r.u(4);
		int failures[10] = {}, cases[10] = {};
		for(uint32_t c = 0; c < count && r.ok(); ++c)
		{
			const int h = static_cast<int>(r.u(1));
			const int n = static_cast<int>(r.u(1));
			const bool saturate = (r.u(2) & port::Emac::Omc) != 0;
			int32_t a[2] = {};
			for(int i = 0; i < n; ++i)
				a[i] = static_cast<int32_t>(r.u(4));
			const auto d0 = static_cast<int32_t>(r.u(4));
			const auto d1 = static_cast<int32_t>(r.u(4));
			port::Emac e(saturate);
			int32_t got0 = 0, got1 = d1;
			switch(h)
			{
			case 0: got0 = port::pitchWord(e, a[0], a[1]); break;
			case 1: got0 = port::modPitchWord(e, a[0], a[1]); break;
			case 2: got0 = static_cast<int32_t>(port::expPitch(e, a[0])); break;
			case 3: got0 = static_cast<int32_t>(port::exp2(e, a[0])); break;
			case 4: got0 = port::log2(e, a[0]); break;
			case 5: got0 = port::decayTicks(e, a[0], a[1]); break;
			case 6: got0 = port::decayTicksDefault(e, a[0]); break;
			case 7: got0 = port::velocitySquare(e, a[0], a[1]); break;
			case 8: got0 = port::knob(e, a[0], got1); break;
			case 9: port::knobPair(e, a[0], got0, got1); break;
			default: std::printf("helpers: unknown helper %d\n", h); return 1;
			}
			++cases[h];
			if(got0 != d0 || got1 != d1)
			{
				if(failures[h]++ < 3)
					std::printf("helper %s(%08x, %08x): %08x %08x, the CPU %08x %08x\n", helperNames[h],
						static_cast<uint32_t>(a[0]), static_cast<uint32_t>(a[1]), static_cast<uint32_t>(got0),
						static_cast<uint32_t>(got1), static_cast<uint32_t>(d0), static_cast<uint32_t>(d1));
			}
		}
		int bad = 0;
		for(int h = 0; h < 10; ++h)
		{
			std::printf("helper %-18s %5d cases, %d differ\n", helperNames[h], cases[h], failures[h]);
			bad += failures[h] + (cases[h] == 0 ? 1 : 0);
		}
		return bad;
	}

	struct Run
	{
		Region region;
		uint16_t offset;
		std::vector<uint8_t> bytes;
	};

	std::vector<Run> readRuns(Reader& _r)
	{
		std::vector<Run> runs(_r.u(4));
		for(auto& run : runs)
		{
			run.region = static_cast<Region>(_r.u(1));
			_r.u(1);
			run.offset = static_cast<uint16_t>(_r.u(2));
			run.bytes.resize(_r.u(2));
			for(auto& b : run.bytes)
				b = static_cast<uint8_t>(_r.u(1));
		}
		return runs;
	}

	bool apply(VoiceIo& _io, const std::vector<Run>& _runs)
	{
		for(const auto& run : _runs)
		{
			if(run.region == Region::Switch)
			{
				if(run.offset >= 32 || run.bytes.size() != 1)
					return false;
				_io.switches = (_io.switches & ~(1u << run.offset)) | (run.bytes[0] ? 1u << run.offset : 0);
				continue;
			}
			size_t size = 0;
			uint8_t* p = regionOf(_io, run.region, size);
			if(!p || run.offset + run.bytes.size() > size)
				return false;
			std::memcpy(p + run.offset, run.bytes.data(), run.bytes.size());
		}
		return true;
	}

	// The first difference of _got from _want, or an empty string
	std::string differ(VoiceIo& _got, VoiceIo& _want)
	{
		char buf[160];
		for(int r = 0; r < static_cast<int>(Region::Count); ++r)
		{
			size_t size = 0;
			const uint8_t* g = regionOf(_got, static_cast<Region>(r), size);
			const uint8_t* w = regionOf(_want, static_cast<Region>(r), size);
			for(size_t i = 0; i < size; ++i)
			{
				if(g[i] == w[i])
					continue;
				const size_t at = i & ~size_t(3);
				std::snprintf(buf, sizeof(buf), "region %d byte %zu: %02x, the CPU %02x (long at %zu: %08x, the CPU %08x)", r, i,
					g[i], w[i], at, at + 4 <= size ? be32(g + at) : 0, at + 4 <= size ? be32(w + at) : 0);
				return buf;
			}
		}
		if(_got.switches != _want.switches)
		{
			std::snprintf(buf, sizeof(buf), "switches %03x, the CPU %03x", _got.switches, _want.switches);
			return buf;
		}
		return {};
	}

	void poison(VoiceIo& _io, uint32_t _seed)
	{
		for(int r = 0; r < static_cast<int>(Region::Count); ++r)
		{
			size_t size = 0;
			uint8_t* p = regionOf(_io, static_cast<Region>(r), size);
			for(size_t i = 0; i < size; ++i)
			{
				_seed = _seed * 1664525u + 1013904223u;
				p[i] = static_cast<uint8_t>(_seed >> 24);
			}
		}
		_io.switches = (_seed >> 8) & 0x3ff;
	}

	// Every case of the machines (_only: one), _failures[m] the cases of machine m that differ; returns what is wrong besides
	int testMachines(const std::vector<uint8_t>& _d, const int _only, const bool _all, std::vector<int>& _failures)
	{
		Reader r{_d};
		if(r.u(4) != 0x56415453)	// "STAV"
		{
			std::printf("machines: not a vector file\n");
			return 1;
		}
		const uint32_t count = r.u(4);
		std::vector<int> cases(Machines + 1);
		auto& failures = _failures;
		failures.assign(Machines + 1, 0);
		int setupCases = 0, setupFailures = 0;
		std::vector<int> outsideCases(Machines + 1), outsideDiffer(Machines + 1);
		for(uint32_t c = 0; c < count && r.ok(); ++c)
		{
			const int m = static_cast<int>(r.u(1));
			const int voice = static_cast<int>(r.u(1));
			const uint32_t macsr = r.u(2);
			const auto pitch = static_cast<int32_t>(r.u(4));
			const auto reads = readRuns(r);
			const auto writes = readRuns(r);
			const bool isSetup = (m & 0x80) != 0;
			const int machine = m & 0x7f;
			if(machine >= Machines || (_only >= 0 && machine != _only))
				continue;
			VoiceIo got;
			poison(got, c * 2654435761u + 1);
			got.voice = voice;
			got.saturate = (macsr & port::Emac::Omc) != 0;
			got.pitch = pitch;
			if(!apply(got, reads))
			{
				std::printf("case %u: a read outside the regions\n", c);
				return 1;
			}
			VoiceIo want = got;
			if(!apply(want, writes))
			{
				std::printf("case %u: a write outside the regions\n", c);
				return 1;
			}
			if(isSetup)
				setup(static_cast<Machine>(machine), got);
			else
				control(static_cast<Machine>(machine), got);
			const std::string d = differ(got, want);
			// A value byte of 128 or more (a lock or modulation out of the knob's range): the original reads past its tables
			// there, into its neighbours, where the port bounds the value. Counted apart, not compared
			bool outside = false;
			for(const auto& run : reads)
				for(size_t i = 0; run.region == Region::Params && i < run.bytes.size(); ++i)
					outside = outside || ((run.offset + i) % 2 == 0 && run.bytes[i] >= 0x80);
			if(outside && !isSetup)
			{
				++outsideCases[static_cast<size_t>(machine)];
				outsideDiffer[static_cast<size_t>(machine)] += d.empty() ? 0 : 1;
				continue;
			}
			int& n = isSetup ? setupFailures : failures[static_cast<size_t>(machine)];
			++(isSetup ? setupCases : cases[static_cast<size_t>(machine)]);
			if(!d.empty() && (n++ == 0 || _all))
				std::printf("machine %d%s case %u voice %d: %s\n", machine, isSetup ? " setup" : "", c, voice, d.c_str());
		}
		int bad = 0;
		for(int m = 0; m < Machines; ++m)
		{
			if(_only >= 0 && m != _only)
				continue;
			std::printf("machine %2d: %5d cases, %d differ; values of 128 or more: %d cases, %d differ\n", m,
				cases[static_cast<size_t>(m)], failures[static_cast<size_t>(m)], outsideCases[static_cast<size_t>(m)],
				outsideDiffer[static_cast<size_t>(m)]);
			bad += cases[static_cast<size_t>(m)] == 0 ? 1 : 0;
		}
		std::printf("track 12's setup: %d cases, %d differ\n", setupCases, setupFailures);
		return bad + setupFailures + (_only < 0 && setupCases == 0 ? 1 : 0);
	}
}

namespace
{
	// Where the original holds each table the port fits or replaces (section 3 loads at Section3Base): the test alone knows
	struct Original
	{
		const char* name;
		uint32_t address;
	};
	constexpr uint32_t Section3Base = 0x40000400;
	const Original originals[] = {
		{"kick.decayRates", 0x401e0f44}, {"kick.longRates", 0x401e82c0}, {"kick.sharpClick", 0x401e11c0},
		{"kick.acousticBody", 0x4029f5ac},
		{"snareClassic.decay", 0x401e5098}, {"snareClassic.decayBelow", 0x401e4e98}, {"snareNatural.rate", 0x401e5318},
		{"snareNatural.decay", 0x401e5518}, {"snareAcoustic.attack", 0x401e5718}, {"snareAcoustic.rate", 0x401e5918},
		{"snareAcoustic.decay", 0x401e5b18}, {"snareNatural.attackSample", 0x401e5298},
		{"snareAcoustic.bodySample", 0x4029d024},
		{"synthEnvelopeRate", 0x401e5e60}, {"chipEnvelopeRate", 0x401e8210}, {"rawLevelBelow", 0x401e5f18},
		{"synthEnvelopeRateBelow", 0x401e5c60}, {"noiseDecayBelow", 0x401e29f0},
		{"metal.closedClassicDecay", 0x401e4638}, {"metal.openClassicDecay", 0x401e4e98},
		{"metal.cymbalClassicDecay", 0x401e1f28}, {"metal.bellClassicDecay", 0x401e1928},
		{"metal.metallicDecay", 0x401e4418}, {"metal.cymbalMetallicDecay", 0x401e2148},
		{"metal.cymbalMetallicRelease", 0x401e2348}, {"metal.rideBell", 0x401e25f0}, {"metal.rideDecay", 0x401e27f0},
		{"metal.rideRelease", 0x401e29f0}, {"metal.hatBasicFeedback", 0x401e2df0}
	};

	// With the fits in place, how many cases of each machine may differ from the original: those the fits' and our own
	// samples' differences reach, as measured on these vectors (a lower count passes; re-recorded vectors are measured again)
	constexpr int FittedDiffer[Machines] = {0, 0, 0, 24, 21, 129, 440, 0, 5, 0, 89, 784, 0, 0, 40, 17, 23, 172, 442, 11, 158, 168, 39,
		36, 157, 55, 183, 14, 0, 0};

	struct TableSet
	{
		const port::Table* table;
		int count;
	};

	std::vector<TableSet> allTables()
	{
		std::vector<TableSet> r;
		for(const auto f : {port::kickTables, port::snareTables, port::synthTables, port::metalTables, port::utilityTables})
		{
			int n = 0;
			const port::Table* t = f(n);
			r.push_back({t, n});
		}
		return r;
	}

	int64_t valueAt(const void* _data, const int _width, const int _i)
	{
		return _width == 2 ? static_cast<const int16_t*>(_data)[_i] : static_cast<const int32_t*>(_data)[_i];
	}

	void setValue(void* _data, const int _width, const int _i, const int64_t _v)
	{
		if(_width == 2)
			static_cast<int16_t*>(_data)[_i] = static_cast<int16_t>(_v);
		else
			static_cast<int32_t*>(_data)[_i] = static_cast<int32_t>(_v);
	}

	// Each table against the original's words: a fit within its bound. Then, with _swap, the original's words put in place
	// of every table (their fits kept in _saved), or the fits put back
	int tables(const std::vector<uint8_t>& _section3, const bool _swap, std::vector<std::vector<int64_t>>& _saved)
	{
		int bad = 0;
		size_t k = 0;
		for(const auto& set : allTables())
			for(int i = 0; i < set.count; ++i, ++k)
			{
				const port::Table& t = set.table[i];
				const Original* o = nullptr;
				for(const auto& c : originals)
					if(std::strcmp(c.name, t.name) == 0)
						o = &c;
				if(!o)
				{
					std::printf("table %s: no original known to the test\n", t.name);
					++bad;
					continue;
				}
				if(k >= _saved.size())
					_saved.resize(k + 1);
				auto& saved = _saved[k];
				if(!_swap)
				{
					for(int j = 0; j < t.count && j < static_cast<int>(saved.size()); ++j)
						setValue(t.data, t.width, j, saved[static_cast<size_t>(j)]);
					continue;
				}
				const size_t at = o->address - Section3Base;
				if(at + static_cast<size_t>(t.count * t.width) > _section3.size())
				{
					std::printf("table %s: outside section 3\n", t.name);
					++bad;
					continue;
				}
				saved.resize(static_cast<size_t>(t.count));
				int64_t worst = 0;
				for(int j = 0; j < t.count; ++j)
				{
					const uint8_t* p = _section3.data() + at + static_cast<size_t>(j * t.width);
					const int64_t orig = t.width == 2 ? static_cast<int16_t>(be16(p)) : static_cast<int32_t>(be32(p));
					const int64_t fit = valueAt(t.data, t.width, j);
					saved[static_cast<size_t>(j)] = fit;
					worst = std::max(worst, fit > orig ? fit - orig : orig - fit);
					setValue(t.data, t.width, j, orig);
				}
				const bool ok = t.bound < 0 || worst <= t.bound;
				std::printf("table %-26s %4d x %d: %s, worst |ours - original| %lld%s\n", t.name, t.count, t.width,
					t.bound < 0 ? "our own sample" : "a fit", static_cast<long long>(worst), ok ? "" : ", OVER ITS BOUND");
				bad += ok ? 0 : 1;
			}
		return bad;
	}
}

int main(int _argc, char** _argv)
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	const char* dir = std::getenv("ANALOG_VECTORS");
	const char* s3 = std::getenv("SYNTAKT_SECTION3");
	if(!dir || !*dir || !s3 || !*s3)
	{
		std::printf("ANALOG_VECTORS or SYNTAKT_SECTION3 is not set: skipped\n");
		return 77;
	}
	int only = -1;
	bool all = false;
	for(int i = 1; i < _argc; ++i)
	{
		if(std::strcmp(_argv[i], "--all") == 0)
			all = true;
		else
			only = std::atoi(_argv[i]);
	}
	const auto helpers = readFile(std::string(dir) + "/helpers.sah");
	const auto machines = readFile(std::string(dir) + "/syntakt.sav");
	const auto section3 = readFile(s3);
	if(helpers.empty() || machines.empty() || section3.empty())
	{
		std::printf("no helpers.sah or syntakt.sav in %s, or no section 3: skipped\n", dir);
		return 77;
	}
	int bad = testHelpers(helpers);

	// The logic: with the original's words in every table, every case of every machine as the CPU wrote it
	std::vector<std::vector<int64_t>> saved;
	bad += tables(section3, true, saved);
	std::vector<int> failures;
	std::printf("with the original's tables:\n");
	bad += testMachines(machines, only, all, failures);
	for(const int f : failures)
		bad += f;

	// What plays: the fits and our own samples back, each machine within the cases they reach
	tables(section3, false, saved);
	std::printf("with the fits and our own samples:\n");
	bad += testMachines(machines, only, false, failures);
	for(int m = 0; m < Machines; ++m)
		if(failures[static_cast<size_t>(m)] > FittedDiffer[m])
		{
			std::printf("machine %d: %d cases differ with the fits, more than %d\n", m, failures[static_cast<size_t>(m)],
				FittedDiffer[m]);
			++bad;
		}
	std::printf(bad ? "FAILED\n" : "OK\n");
	return bad ? 1 : 0;
}