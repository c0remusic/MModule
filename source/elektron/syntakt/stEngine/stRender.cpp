// Renders one Syntakt machine from the C++ port alone, no emulator: the voice CPU's boot, then a block at a time as CPU
// #1 would send it, the scenario of SyntHack OS's `cli.py render` (every parameter byte at 0x40, the machine on track
// 1, its note, a trig at blocks 0 and 2). Writes track 1's output as the voice CPU sends it to CPU #1, after the
// track's stages (drive, BASE/WIDTH, filter, amp envelope), as a WAV: what `cli.py render --raw` writes.
//
//	stRender SECTION7 OUT.wav MACHINE NOTE SECONDS [BOOT_SRAM]
//
// With BOOT_SRAM (the original's SRAM after its boot, dumped from the emulator) it first compares the port's boot with
// it, word for word.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "stBoot.h"
#include "stEngine.h"

namespace
{
	std::vector<uint8_t> readFile(const char* _path)
	{
		std::ifstream f(_path, std::ios::binary);
		return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
	}

	uint32_t be32(const uint8_t* _p)
	{
		return uint32_t(_p[0]) << 24 | uint32_t(_p[1]) << 16 | uint32_t(_p[2]) << 8 | _p[3];
	}

	void putLe(std::ofstream& _f, const uint32_t _v, const int _bytes)
	{
		for(int i = 0; i < _bytes; ++i)
			_f.put(static_cast<char>(_v >> (8 * i)));
	}

	// 32-bit integer PCM, mono, 48 kHz
	void writeWav(const char* _path, const std::vector<int32_t>& _samples)
	{
		std::ofstream f(_path, std::ios::binary);
		const uint32_t data = static_cast<uint32_t>(_samples.size()) * 4;
		f.write("RIFF", 4);
		putLe(f, 36 + data, 4);
		f.write("WAVEfmt ", 8);
		putLe(f, 16, 4);
		putLe(f, 1, 2);
		putLe(f, 1, 2);
		putLe(f, 48000, 4);
		putLe(f, 48000 * 4, 4);
		putLe(f, 4, 2);
		putLe(f, 32, 2);
		f.write("data", 4);
		putLe(f, data, 4);
		for(const int32_t s : _samples)
			putLe(f, static_cast<uint32_t>(s), 4);
	}
}

int main(const int _argc, char** _argv)
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	if(_argc < 6)
	{
		std::printf("usage: stRender SECTION7 OUT.wav MACHINE NOTE SECONDS [BOOT_SRAM]\n");
		return 2;
	}
	const auto section7 = readFile(_argv[1]);
	if(section7.size() != 383760)
	{
		std::printf("%s is not OS 1.41's section 7\n", _argv[1]);
		return 1;
	}
	std::vector<int32_t> romWords(section7.size() / 4);
	for(size_t i = 0; i < romWords.size(); ++i)
		romWords[i] = static_cast<int32_t>(be32(section7.data() + 4 * i));
	const syntakt::Rom rom{romWords.data()};

	auto sram = std::make_unique<syntakt::Sram>();
	syntakt::Globals globals;
	syntakt::boot(*sram, globals, rom);

	if(_argc > 6)
	{
		const auto original = readFile(_argv[6]);
		int differ = 0;
		for(uint32_t i = 0; i < syntakt::Sram::Size / 4 && 4 * i + 3 < original.size(); ++i)
		{
			const auto want = be32(original.data() + 4 * i);
			if(static_cast<uint32_t>(sram->words[i]) == want)
				continue;
			if(differ++ < 5)
				std::printf("boot %08x: %08x, original %08x\n", syntakt::Sram::Base + 4 * i,
					static_cast<uint32_t>(sram->words[i]), want);
		}
		std::printf("boot: %d words differ from the original's\n", differ);
	}

	const int machine = std::atoi(_argv[3]);
	const int note = std::atoi(_argv[4]);
	const int blocks = static_cast<int>(std::atof(_argv[5]) * 48000 / 32);

	// cli.py make_params: every byte at 0x40, track 1's machine and note
	uint8_t block[syntakt::BlockBytes] = {};
	for(int t = 0; t < syntakt::Voices; ++t)
	{
		for(int i = 0; i < 20; ++i)
			block[20 * t + i] = 0x40;
		for(int i = 0; i < 8; ++i)
			block[0xa0 + 8 * t + i] = 0x40;
		for(int i = 0; i < 14; ++i)
			block[0xe0 + 14 * t + i] = 0x40;
	}
	block[0] = static_cast<uint8_t>(machine);
	const uint32_t pitch = static_cast<uint32_t>(note) << 16;
	for(int i = 0; i < 4; ++i)
		block[0x150 + i] = static_cast<uint8_t>(pitch >> (24 - 8 * i));

	std::vector<int32_t> out;
	for(int b = 0; b < blocks; ++b)
	{
		block[0x180] = b == 0 || b == 2 ? 1 : 0;
		syntakt::runBlock(*sram, globals, rom, block);
		const int32_t* voice = sram->ptr(syntakt::VoiceOut);
		out.insert(out.end(), voice, voice + 32);
	}
	writeWav(_argv[2], out);
	std::printf("%s: %d blocks\n", _argv[2], blocks);
	return 0;
}
