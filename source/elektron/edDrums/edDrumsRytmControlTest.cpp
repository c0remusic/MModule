// The Rytm's own analog voices' control (edDrumsRytm.h) against vectors of the original routines, recorded on an
// emulator from the user's own Rytm MKII OS 1.74 and kept outside the repository (RYTM_VECTORS: the folder that holds
// rytm.rav; RYTM_SECTION3: its section 3, for the decay tables; exit 77 without them). Every byte each machine's block
// writes, from the memory it read; a byte it neither read nor wrote holds a poison value it must leave.
//
//   edDrumsRytmControlTest [machine index] [--all]   (--all: every failing case, not only the first of each machine)

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "edDrumsAnalog.h"
#include "edDrumsRytm.h"

using namespace elektron::drums::rytm;
using elektron::drums::analog::be32;

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
			size_t size = 0;
			uint8_t* p = regionOf(_io, run.region, size);
			if(!p || run.offset + run.bytes.size() > size)
				return false;
			std::memcpy(p + run.offset, run.bytes.data(), run.bytes.size());
		}
		return true;
	}

	std::string differ(VoiceIo& _got, VoiceIo& _want)
	{
		char buf[160];
		for(int r = 0; r < static_cast<int>(Region::Count); ++r)
		{
			size_t size = 0;
			const uint8_t* g = regionOf(_got, static_cast<Region>(r), size);
			const uint8_t* w = regionOf(_want, static_cast<Region>(r), size);
			for(size_t i = 0; g && i < size; ++i)
			{
				if(g[i] == w[i])
					continue;
				const size_t at = i & ~size_t(3);
				std::snprintf(buf, sizeof(buf), "region %d byte %zu: %02x, the CPU %02x (long at %zu: %08x, the CPU %08x)", r, i,
					g[i], w[i], at, at + 4 <= size ? be32(g + at) : 0, at + 4 <= size ? be32(w + at) : 0);
				return buf;
			}
		}
		return {};
	}

	void poison(VoiceIo& _io, uint32_t _seed)
	{
		for(int r = 0; r < static_cast<int>(Region::Count); ++r)
		{
			size_t size = 0;
			uint8_t* p = regionOf(_io, static_cast<Region>(r), size);
			for(size_t i = 0; p && i < size; ++i)
			{
				_seed = _seed * 1664525u + 1013904223u;
				p[i] = static_cast<uint8_t>(_seed >> 24);
			}
		}
	}

	int testMachines(const std::vector<uint8_t>& _d, const int _only, const bool _all)
	{
		Reader r{_d};
		if(r.u(4) != 0x56415952)	// "RYAV"
		{
			std::printf("not a vector file\n");
			return 1;
		}
		const uint32_t count = r.u(4);
		std::vector<int> cases(Machines), failures(Machines), outsideCases(Machines), outsideDiffer(Machines);
		for(uint32_t c = 0; c < count && r.ok(); ++c)
		{
			const int machine = static_cast<int>(r.u(1));
			const int voice = static_cast<int>(r.u(1));
			const uint32_t macsr = r.u(2);
			const auto pitch = static_cast<int32_t>(r.u(4));
			const auto reads = readRuns(r);
			const auto writes = readRuns(r);
			if(machine >= Machines || (_only >= 0 && machine != _only))
				continue;
			VoiceIo got;
			poison(got, c * 2654435761u + 1);
			got.voice = voice;
			got.saturate = (macsr & 0x80) != 0;
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
			control(static_cast<Machine>(machine), got);
			const std::string d = differ(got, want);
			// A value byte of 128 or more (a lock or modulation out of the knob's range): the original reads before its
			// tables there, where the port bounds the value. Counted apart, not compared
			bool outside = false;
			for(const auto& run : reads)
				for(size_t i = 0; run.region == Region::Params && i < run.bytes.size(); ++i)
					outside = outside || ((run.offset + i) % 2 == 0 && run.bytes[i] >= 0x80);
			const auto m = static_cast<size_t>(machine);
			if(outside)
			{
				++outsideCases[m];
				outsideDiffer[m] += d.empty() ? 0 : 1;
				continue;
			}
			++cases[m];
			if(!d.empty() && (failures[m]++ == 0 || _all))
				std::printf("machine %d case %u voice %d: %s\n", machine, c, voice, d.c_str());
		}
		int bad = 0;
		for(int m = 0; m < Machines; ++m)
		{
			if(_only >= 0 && m != _only)
				continue;
			const auto i = static_cast<size_t>(m);
			std::printf("machine %d: %5d cases, %d differ; values of 128 or more: %d cases, %d differ\n", m, cases[i],
				failures[i], outsideCases[i], outsideDiffer[i]);
			bad += failures[i] + (cases[i] == 0 ? 1 : 0);
		}
		return bad;
	}

	// The decay tables by formula against the original's words, which they must equal (section 3 loads at 0x40000400)
	int tables(const std::vector<uint8_t>& _section3)
	{
		struct T { const char* name; uint32_t address; double scale, base; };
		const T ts[] = {{"tom sweep", 0x401f819c, 1000.0, 33.0}, {"tom level", 0x401f839c, 312.5, 33.0},
			{"clap decay", 0x401f157c, 250.0, 5.0}};
		int bad = 0;
		for(const auto& t : ts)
		{
			int differ = 0;
			for(int k = 0; k < 128; ++k)
			{
				const size_t at = t.address - 0x40000400 + 4 * static_cast<size_t>(k);
				if(at + 4 > _section3.size())
					return bad + 1;
				const auto orig = static_cast<int32_t>(be32(_section3.data() + at));
				differ += (k == 0 ? 0 : decayCoefficient(t.scale, t.base, k)) != orig ? 1 : 0;
			}
			std::printf("table %-10s: %d of 128 words differ from the original\n", t.name, differ);
			bad += differ;
		}
		return bad;
	}
}

int main(int _argc, char** _argv)
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	const char* dir = std::getenv("RYTM_VECTORS");
	const char* s3 = std::getenv("RYTM_SECTION3");
	if(!dir || !*dir || !s3 || !*s3)
	{
		std::printf("RYTM_VECTORS or RYTM_SECTION3 is not set: skipped\n");
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
	const auto machines = readFile(std::string(dir) + "/rytm.rav");
	const auto section3 = readFile(s3);
	if(machines.empty() || section3.empty())
	{
		std::printf("no rytm.rav in %s, or no section 3: skipped\n", dir);
		return 77;
	}
	int bad = tables(section3);
	bad += testMachines(machines, only, all);
	std::printf(bad ? "FAILED\n" : "OK\n");
	return bad ? 1 : 0;
}
