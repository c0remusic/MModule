// Plays a User Machine of MDX X.20 on a bare DSP56300, outside the plug-in: the first sound of the map
// "User Machines from X.20 in MoDule" (.scratch/user-machines/, ticket 03). The host is what
// docs/MDX_MachineDumpStandard.txt of jmamma/MDS describes: the package relocated into P, the imports patched, the
// eight parameters converted into Y:(R6+1..8), INIT, then TRIG on each Hit and RENDER every block of 32 samples on
// one Track. It writes a 24-bit mono WAV at 44.1 kHz and the cycles each routine took.
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

#include "MdsPackage.h"

#include "dsp56kEmu/assembler.h"
#include "dsp56kEmu/disasm.h"
#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/opcodes.h"
#include "dsp56kEmu/memory.h"
#include "dsp56kEmu/peripherals.h"

namespace
{
	using namespace dsp56k;
	using md::mds::Package;

	// A memory map of our own: the documents leave the addresses to the host (the code reaches everything through R6,
	// R7, the patched imports and Y:$07F0)
	constexpr TWord kSizeP = 0x100000, kSizeXY = 0x100000, kBridge = 0x020000;
	constexpr TWord kScratchX = 0x0000;		// import 7, 32 words (SAMPLE_READ's X:$00-$1F)
	constexpr TWord kScratchY = 0x0000;		// import 8, 32 words
	constexpr TWord kDescriptor = 0x07e0;	// a Track's four neighbour buffer addresses
	constexpr TWord kDescriptorPtr = 0x07f0;
	constexpr TWord kOutput = 0x0800;		// Y, 32 words, aligned for M7 = $1F
	constexpr TWord kNeighbours = 0x0820;	// Y, three more buffers of 32
	constexpr TWord kTrack = 0x1000;		// R6: the Track's 64 X and 64 Y words
	constexpr TWord kInputRing = 0x4000;	// import 2, 256 words, silent
	constexpr TWord kInputPos = 0x4100;		// import 3
	constexpr TWord kMainRing = 0x4200;		// import 4, 256 words, silent
	constexpr TWord kMainPos = 0x4300;		// import 5: holds the address of the block to read
	constexpr TWord kSine = 0x8000;			// import 1, 32,768 words
	constexpr TWord kExternal = 0x40000;	// import 6: 16 x 1,536 words where X and Y are the same storage
	// The program in external memory, where P, X and Y are the same storage: packages read their tables with x:
	// moves at addresses inside their own code (TX8-BD's lookups)
	constexpr TWord kProgram = 0x30000;
	constexpr TWord kStubInit = 0x0100, kStubTrig = 0x0120, kStubRender = 0x0140, kStubEmpty = 0x0160;
	constexpr TWord kRts = 0x0180;
	constexpr uint64_t kMaxInstructions = 10'000'000;

	struct Host
	{
		explicit Host(const Package& _package) : m_package(_package)
		{
			m_mem = std::make_unique<Memory>(m_validator, kSizeP, kSizeXY, kBridge);
			m_dsp = std::make_unique<DSP>(*m_mem, &m_periphX, &m_periphY);
			m_dsp->resetHW();
			load();
		}

		void load()
		{
			// The program at kProgram: relocated words get the base, imports their resource's address
			std::vector<uint32_t> words = m_package.program;
			for(size_t i = 0; i < words.size(); ++i)
			{
				if(m_package.relocate[i])
					words[i] = (words[i] + kProgram) & 0xffffff;
			}
			for(const auto& im : m_package.imports)
			{
				TWord base = 0;
				switch(im.symbol)
				{
				case 1: base = kSine; break;
				case 2: base = kInputRing; break;
				case 3: base = kInputPos; break;
				case 4: base = kMainRing; break;
				case 5: base = kMainPos; break;
				case 6: base = kExternal; break;
				case 7: base = kScratchX; break;
				case 8: base = kScratchY; break;
				default: throw std::runtime_error("import of symbol " + std::to_string(im.symbol) + " is not provided");
				}
				words[im.word] = (words[im.word] + base) & 0xffffff;
			}
			for(size_t i = 0; i < words.size(); ++i)
				m_dsp->memWriteP(kProgram + static_cast<TWord>(i), words[i]);

			for(TWord i = 0; i < 32768; ++i)
			{
				const auto v = std::lround(std::sin(2.0 * 3.14159265358979323846 * i / 32768.0) * 8388607.0);
				m_dsp->memWrite(MemArea_X, kSine + i, static_cast<TWord>(v) & 0xffffff);
			}
			m_dsp->memWrite(MemArea_X, kMainPos, kMainRing);
			for(TWord k = 0; k < 4; ++k)
				m_dsp->memWrite(MemArea_Y, kDescriptor + k, k == 0 ? kOutput : kNeighbours + 32 * (k - 1));
			m_dsp->memWrite(MemArea_Y, kDescriptorPtr, kDescriptor);

			m_endInit = stub(kStubInit, kProgram + m_package.initOffset);
			m_endTrig = stub(kStubTrig, kProgram + m_package.trigOffset);
			m_endRender = stub(kStubRender, kProgram + m_package.renderOffset);
			m_dsp->memWriteP(kRts, 0x00000c);	// rts
			m_endEmpty = stub(kStubEmpty, kRts);
		}

		// Registers as the documents give them on entry, then jsr to the routine and stop on a jmp to itself, whose
		// address it returns
		TWord stub(TWord _pc, const TWord _entry)
		{
			Assembler as;
			const auto emit = [&](const std::string& _text)
			{
				const auto r = as.assemble(_text.c_str());
				if(!r.success())
					throw std::runtime_error("cannot assemble '" + _text + "'");
				m_dsp->memWriteP(_pc++, r.word[0]);
				if(r.wordCount > 1)
					m_dsp->memWriteP(_pc++, r.word[1]);
			};
			const auto hex = [](const TWord _v) { char s[16]; std::snprintf(s, sizeof(s), "$%x", _v); return std::string(s); };
			emit("move #>" + hex(kTrack) + ",r6");
			emit("move #>0,r1");	// Track 1
			emit("move #>" + hex(kOutput) + ",r7");
			emit("move #>$1f,m7");
			for(int m = 0; m <= 5; ++m)
				emit("move #>$ffffff,m" + std::to_string(m));
			m_dsp->memWriteP(_pc++, 0x0bf080);	// jsr >entry
			m_dsp->memWriteP(_pc++, _entry);
			const auto self = _pc;
			m_dsp->memWriteP(_pc++, 0x0af080);	// jmp >self
			m_dsp->memWriteP(_pc++, self);
			return self;
		}

		// Cycles from the stub's start to its end, the stub's own cost included
		uint64_t call(const TWord _stub, const TWord _end)
		{
			m_dsp->setPC(_stub);
			const auto start = m_dsp->getCycles();
			const auto instructions = m_dsp->getInstructionCounter();
			while(m_dsp->getPC().toWord() != _end)
			{
				m_dsp->exec();
				if(m_dsp->getInstructionCounter() - instructions > kMaxInstructions)
					throw std::runtime_error("a routine did not return");
			}
			return m_dsp->getCycles() - start;
		}

		void setParameter(const size_t _index, const int _setting)
		{
			m_dsp->memWrite(MemArea_Y, kTrack + 1 + static_cast<TWord>(_index),
				md::mds::convert(m_package.parameters[_index], _setting));
		}

		// The program as loaded, with its routines and patched words marked
		void disassemble() const
		{
			Opcodes opcodes;
			Disassembler disasm(opcodes);
			const auto size = static_cast<TWord>(m_package.program.size());
			for(TWord i = 0; i < size;)
			{
				const auto op = m_mem->get(MemArea_P, kProgram + i);
				const auto opB = i + 1 < size ? m_mem->get(MemArea_P, kProgram + i + 1) : 0;
				std::string text;
				const auto count = std::max<uint32_t>(1, disasm.disassemble(text, op, opB, 0, 0, kProgram + i));
				const char* label = i == m_package.initOffset ? "INIT:" : i == m_package.trigOffset ? "TRIG:" :
					i == m_package.renderOffset ? "RENDER:" : "";
				std::printf("%-8s P:$%04x  %06x %s %s\n", label, kProgram + i, op, count > 1 ? "+1" : "  ", text.c_str());
				i += count;
			}
		}

		uint64_t overhead() { return call(kStubEmpty, m_endEmpty); }
		uint64_t init() { return call(kStubInit, m_endInit); }
		uint64_t trig() { return call(kStubTrig, m_endTrig); }
		uint64_t render(int32_t* _out)
		{
			const auto c = call(kStubRender, m_endRender);
			for(TWord i = 0; i < 32; ++i)
				_out[i] = static_cast<int32_t>(m_mem->get(MemArea_Y, kOutput + i) << 8) >> 8;
			return c;
		}

		const Package& m_package;
		DefaultMemoryValidator m_validator;
		PeripheralsNop m_periphX, m_periphY;
		std::unique_ptr<Memory> m_mem;
		std::unique_ptr<DSP> m_dsp;
		TWord m_endInit = 0, m_endTrig = 0, m_endRender = 0, m_endEmpty = 0;
	};

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

		Host host(package);
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
