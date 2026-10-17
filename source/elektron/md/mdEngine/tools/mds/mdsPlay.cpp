// Plays a User Machine of MDX X.20 on a bare DSP56300, outside the plug-in: the first sound of the map
// "User Machines from X.20 in MoDule" (.scratch/user-machines/, ticket 03). The host is what
// docs/MDX_MachineDumpStandard.txt of jmamma/MDS describes: the package relocated into P, the imports patched, the
// eight parameters converted into Y:(R6+1..8), INIT, then TRIG on each Hit and RENDER every block of 32 samples on
// one Track (md::mds::BareHost). It writes a 24-bit mono WAV at 44.1 kHz and the cycles each routine took.
//
//   mdsPlay <machine.syx|.mds> [-o out.wav] [--seconds 2] [--hits 1] [--every 0.5] [--set NAME=value]... [--info]
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "MdsHost.h"
#include "MdsPackage.h"

namespace
{
	std::vector<uint8_t> readFile(const std::string& _path)
	{
		std::ifstream f(_path, std::ios::binary);
		if(!f)
			throw std::runtime_error("cannot read " + _path);
		return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
	}

	void writeWav(const std::string& _path, const std::vector<int32_t>& _samples)
	{
		std::ofstream f(_path, std::ios::binary);
		const auto u32 = [&](const uint32_t _v) { f.put(char(_v)); f.put(char(_v >> 8)); f.put(char(_v >> 16)); f.put(char(_v >> 24)); };
		const auto u16 = [&](const uint32_t _v) { f.put(char(_v)); f.put(char(_v >> 8)); };
		const auto bytes = static_cast<uint32_t>(_samples.size() * 3);
		f.write("RIFF", 4); u32(36 + bytes); f.write("WAVE", 4);
		f.write("fmt ", 4); u32(16); u16(1); u16(1); u32(44100); u32(44100 * 3); u16(3); u16(24);
		f.write("data", 4); u32(bytes);
		for(const auto s : _samples)
		{
			f.put(char(s)); f.put(char(s >> 8)); f.put(char(s >> 16));
		}
		if(!f)
			throw std::runtime_error("cannot write " + _path);
	}

	const char* conversionName(const md::mds::Conversion _c)
	{
		static const char* names[] = {"raw14", "unipolar", "squared", "bipolar", "pitch", "boolean", "index", "decay"};
		return names[static_cast<int>(_c)];
	}
}

int main(const int _argc, char* _argv[])
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	if(_argc < 2)
	{
		std::fprintf(stderr, "usage: mdsPlay <machine.syx|.mds> [-o out.wav] [--seconds 2] [--hits 1] [--every 0.5] "
			"[--set NAME=value]... [--info]\n");
		return 2;
	}
	try
	{
		const std::string input = _argv[1];
		std::string output;
		double seconds = 2.0, every = 0.5;
		int hits = 1;
		bool infoOnly = false, disasm = false;
		std::vector<std::pair<std::string, int>> sets;
		for(int i = 2; i < _argc; ++i)
		{
			const std::string a = _argv[i];
			const auto next = [&]() -> std::string
			{
				if(i + 1 >= _argc)
					throw std::runtime_error(a + " needs a value");
				return _argv[++i];
			};
			if(a == "-o") output = next();
			else if(a == "--seconds") seconds = std::stod(next());
			else if(a == "--hits") hits = std::stoi(next());
			else if(a == "--every") every = std::stod(next());
			else if(a == "--info") infoOnly = true;
			else if(a == "--disasm") disasm = true;
			else if(a == "--set")
			{
				const auto s = next();
				const auto eq = s.find('=');
				if(eq == std::string::npos)
					throw std::runtime_error("--set takes NAME=value");
				sets.emplace_back(s.substr(0, eq), std::stoi(s.substr(eq + 1)));
			}
			else throw std::runtime_error("unknown option " + a);
		}

		auto bytes = readFile(input);
		if(!bytes.empty() && bytes[0] == 0xf0)
			bytes = md::mds::packageFromSysex(bytes);
		const auto package = md::mds::parsePackage(bytes);

		std::printf("%s \"%s\" %s v%d.%d: %zu words, %zu relocated, imports", package.displayName().c_str(),
			package.name.c_str(), package.id.c_str(), package.versionMajor, package.versionMinor, package.program.size(),
			static_cast<size_t>(std::count(package.relocate.begin(), package.relocate.end(), true)));
		for(const auto& im : package.imports)
			std::printf(" %d", im.symbol);
		std::printf("; X %d Y %d words; flags $%02x; declared %d cycles\n", package.xWords, package.yWords, package.flags,
			package.declaredCycles);
		std::vector<int> settings;
		for(const auto& p : package.parameters)
		{
			std::printf("  %-4s %3d (%d-%d) %s\n", p.name.c_str(), p.defaultValue, p.min, p.max, conversionName(p.conversion));
			settings.push_back(p.defaultValue);
		}
		for(const auto& [name, value] : sets)
		{
			bool found = false;
			for(size_t k = 0; k < package.parameters.size(); ++k)
			{
				if(package.parameters[k].name == name)
				{
					settings[k] = value;
					found = true;
				}
			}
			if(!found)
				throw std::runtime_error("no parameter " + name);
		}
		if(infoOnly)
			return 0;

		md::mds::BareHost host(package);
		if(disasm)
		{
			host.disassemble();
			return 0;
		}
		for(size_t k = 0; k < settings.size(); ++k)
			host.setParameter(k, settings[k]);
		const auto stubCycles = host.overhead();
		const auto initCycles = host.init();

		const auto blocks = static_cast<int>(seconds * 44100.0 / 32.0);
		const auto everyBlocks = std::max(1, static_cast<int>(every * 44100.0 / 32.0));
		std::vector<int32_t> out(static_cast<size_t>(blocks) * 32);
		uint64_t renderMax = 0, renderSum = 0, trigMax = 0, hitBlockMax = 0;
		int hit = 0;
		const auto started = std::chrono::steady_clock::now();
		for(int b = 0; b < blocks; ++b)
		{
			uint64_t trigCycles = 0;
			if(hit < hits && b == hit * everyBlocks)
			{
				trigCycles = host.trig() - stubCycles;
				trigMax = std::max(trigMax, trigCycles);
				++hit;
			}
			const auto r = host.render(&out[static_cast<size_t>(b) * 32]) - stubCycles;
			renderMax = std::max(renderMax, r);
			renderSum += r;
			if(trigCycles)
				hitBlockMax = std::max(hitBlockMax, trigCycles + r);
		}
		int32_t peak = 0;
		for(const auto s : out)
			peak = std::max(peak, std::abs(s));
		std::printf("cycles: INIT %llu, TRIG %llu, RENDER max %llu mean %.0f, TRIG+RENDER max %llu (declared %d, limit 3951)\n",
			static_cast<unsigned long long>(initCycles - stubCycles), static_cast<unsigned long long>(trigMax),
			static_cast<unsigned long long>(renderMax), blocks ? double(renderSum) / blocks : 0.0,
			static_cast<unsigned long long>(hitBlockMax), package.declaredCycles);
		const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
		std::printf("peak %.1f dBFS over %.2f s; host %.2f us per block, %.2f %% of real time for one Track\n",
			peak ? 20.0 * std::log10(peak / 8388608.0) : -999.0, seconds, blocks ? elapsed * 1e6 / blocks : 0.0,
			100.0 * elapsed / seconds);
		if(!output.empty())
			writeWav(output, out);
		return 0;
	}
	catch(const std::exception& _e)
	{
		std::fprintf(stderr, "mdsPlay: %s\n", _e.what());
		return 1;
	}
}
