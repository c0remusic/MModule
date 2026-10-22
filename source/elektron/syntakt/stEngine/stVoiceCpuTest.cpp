// syntakt::VoiceCpu against the original's own results, through its interface:
//	(a) once made, its SRAM is the original's after its boot, word for word;
//	(b) BD MODERN at note 60, in the scenario of SyntHack OS's `cli.py render` (every parameter and machine byte at
//		0x40, the machine on track 1, its note, a trig at blocks 0 and 2, 0.5 s), gives track 1's output sample for
//		sample as the original's voice CPU sends it to CPU #1;
//	(c) every machine is ported, and the first machine of each engine but SP TWINSHOT sounds on track 1.
//
// SYNTAKT_SECTION7 names the user's OS 1.41 section 7, SYNTAKT_REFERENCES a directory outside the repository that holds:
//	boot_sram.bin			the original's 64 KB of SRAM after its boot, big-endian, dumped from SyntHack OS's emulator
//	bd_modern_note60.wav	the original's full output of track 1 in (b), 24,000 samples of 32-bit PCM, mono, 48 kHz,
//							as .scratch/elektron-engines/tools/full_out.py 0 60 0.5 PORT.wav ORIGINAL.wav writes it
// Exit 77 without them.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "stSection7.h"
#include "stVoiceCpu.h"

namespace syntakt
{
	// The one look inside the module the test takes: the SRAM, for (a)
	struct VoiceCpuTest
	{
		static const Sram& sram(const VoiceCpu& _cpu) { return *_cpu.m_sram; }
	};
}

namespace
{
	using syntakt::VoiceCpu;

	int g_failures = 0;

	void check(const bool _ok, const char* _what)
	{
		std::printf("%s: %s\n", _ok ? "pass" : "FAIL", _what);
		if(!_ok)
			++g_failures;
	}

	// The eight tracks' outputs over _blocks blocks of cli.py render's scenario for _machine at _note. _chunked: the
	// host's calls of varied sizes under a block (7, 13, 1, 31 samples) instead of a block each
	std::vector<std::vector<int32_t>> renderCli(VoiceCpu& _cpu, const int _machine, const int _note, const int _blocks,
		const bool _chunked = false)
	{
		for(int t = 0; t < VoiceCpu::Tracks; ++t)
		{
			_cpu.setMachine(t, 0x40);
			for(const auto page : {VoiceCpu::Page::Machine, VoiceCpu::Page::Filter, VoiceCpu::Page::Amp})
				for(int i = 0; i < VoiceCpu::parameterCount(page); ++i)
					_cpu.setParameter(t, page, i, 0x40, 0x40);
		}
		_cpu.setMachine(0, static_cast<uint8_t>(_machine));
		_cpu.setNote(0, _note);

		std::vector<std::vector<int32_t>> out(VoiceCpu::Tracks,
			std::vector<int32_t>(static_cast<size_t>(_blocks) * syntakt::BlockSize));
		const size_t total = static_cast<size_t>(_blocks) * syntakt::BlockSize;
		constexpr size_t chunks[] = {7, 13, 1, 31};
		for(size_t n = 0, c = 0; n < total; ++c)
		{
			const size_t count = std::min(_chunked ? chunks[c % 4] : size_t(syntakt::BlockSize), total - n);
			// A trig goes with the call whose samples start block 0 or 2: under a block, a call starts at most one
			const size_t start = (n + syntakt::BlockSize - 1) / syntakt::BlockSize * syntakt::BlockSize;
			if(start < n + count && (start == 0 || start == 2 * syntakt::BlockSize))
				_cpu.trig(0);
			int32_t* outputs[VoiceCpu::Tracks];
			for(int t = 0; t < VoiceCpu::Tracks; ++t)
				outputs[t] = out[t].data() + n;
			_cpu.render(outputs, count);
			n += count;
		}
		return out;
	}

	// A WAV's samples as 32-bit little-endian words, from its data chunk
	std::vector<int32_t> readWav32(const std::string& _path)
	{
		const auto bytes = syntakt::readFile(_path);
		for(size_t i = 12; i + 8 <= bytes.size(); ++i)
		{
			if(std::memcmp(bytes.data() + i, "data", 4) != 0)
				continue;
			std::vector<int32_t> samples((bytes.size() - i - 8) / 4);
			for(size_t s = 0; s < samples.size(); ++s)
			{
				const uint8_t* p = bytes.data() + i + 8 + 4 * s;
				samples[s] = static_cast<int32_t>(uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 |
					uint32_t(p[3]) << 24);
			}
			return samples;
		}
		return {};
	}
}

int main()
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	const char* section7 = std::getenv("SYNTAKT_SECTION7");
	const char* references = std::getenv("SYNTAKT_REFERENCES");
	if(!section7 || !*section7 || !references || !*references)
	{
		std::printf("SYNTAKT_SECTION7 and SYNTAKT_REFERENCES are not set: skipped\n");
		return 77;
	}
	const auto romWords = syntakt::loadSection7(section7);
	const auto bootImage = syntakt::readFile(std::string(references) + "/boot_sram.bin");
	const auto bdModern = readWav32(std::string(references) + "/bd_modern_note60.wav");
	if(romWords.empty() || bootImage.size() != syntakt::Sram::Size || bdModern.empty())
	{
		std::printf("OS 1.41's section 7, boot_sram.bin or bd_modern_note60.wav is missing: skipped\n");
		return 77;
	}

	// (a)
	{
		const VoiceCpu cpu(romWords);
		const auto& sram = syntakt::VoiceCpuTest::sram(cpu);
		int differ = 0;
		for(uint32_t i = 0; i < syntakt::Sram::Size / 4; ++i)
		{
			const uint32_t want = syntakt::be32(bootImage.data() + 4 * i);
			if(static_cast<uint32_t>(sram.words[i]) == want)
				continue;
			if(differ++ < 5)
				std::printf("boot %08x: %08x, original %08x\n", syntakt::Sram::Base + 4 * i,
					static_cast<uint32_t>(sram.words[i]), want);
		}
		std::printf("boot: %d words differ from the original's\n", differ);
		check(differ == 0, "the SRAM after construction is the original's after its boot");
	}

	// (b)
	{
		VoiceCpu cpu(romWords);
		check(cpu.isPorted(0), "BD MODERN is ported");
		const int blocks = static_cast<int>(bdModern.size() / syntakt::BlockSize);
		const auto out = renderCli(cpu, 0, 60, blocks);
		int differ = 0;
		for(size_t i = 0; i < bdModern.size(); ++i)
		{
			if(out[0][i] == bdModern[i])
				continue;
			if(differ++ < 5)
				std::printf("sample %zu: %d, original %d\n", i, out[0][i], bdModern[i]);
		}
		std::printf("BD MODERN note 60: %zu samples, %d differ\n", bdModern.size(), differ);
		check(differ == 0 && bdModern.size() == 24000, "BD MODERN's track 1 is the original's, sample for sample");

		// The same through calls of other sizes than a block: the internal buffer gives the same samples
		VoiceCpu chunked(romWords);
		const auto outChunked = renderCli(chunked, 0, 60, blocks, true);
		check(outChunked == out, "calls of 7, 13, 1 and 31 samples give the same eight outputs");
	}

	// (c)
	{
		VoiceCpu cpu(romWords);
		bool all = true;
		for(int m = 0; m <= 45; ++m)
			all = all && cpu.isPorted(m);
		check(all, "every machine 0-45 is ported");

		// The first machine of each engine but SP TWINSHOT (44), which plays samples and has none here
		for(const int machine : {0, 1, 2, 3, 4, 5, 6, 7, 36, 37, 38})
		{
			VoiceCpu one(romWords);
			const auto out = renderCli(one, machine, 60, 375);
			int nonZero = 0;
			for(const int32_t s : out[0])
				nonZero += s != 0;
			std::printf("machine %d: %d of %zu samples not 0\n", machine, nonZero, out[0].size());
			check(nonZero > 0, "the machine sounds on track 1");
		}
	}

	std::printf("%s\n", g_failures ? "FAIL" : "PASS");
	return g_failures ? 1 : 0;
}
