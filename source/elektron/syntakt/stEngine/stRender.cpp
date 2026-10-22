// Renders one Syntakt machine from the C++ port alone, no emulator, through syntakt::VoiceCpu: the scenario of
// SyntHack OS's `cli.py render` (every parameter byte at 0x40, the machine on track 1, its note, a trig at blocks 0
// and 2). Writes track 1's output as the voice CPU sends it to CPU #1, after the track's stages (drive, BASE/WIDTH,
// filter, amp envelope), as a WAV: what `cli.py render --raw` writes.
//
//	stRender SECTION7 OUT.wav MACHINE NOTE SECONDS

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <vector>

#include "stSection7.h"
#include "stVoiceCpu.h"

namespace
{
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
		std::printf("usage: stRender SECTION7 OUT.wav MACHINE NOTE SECONDS\n");
		return 2;
	}
	const auto romWords = syntakt::loadSection7(_argv[1]);
	if(romWords.empty())
	{
		std::printf("%s is not OS 1.41's section 7\n", _argv[1]);
		return 1;
	}
	using syntakt::VoiceCpu;
	VoiceCpu cpu(romWords);

	const int machine = std::atoi(_argv[3]);
	const int note = std::atoi(_argv[4]);
	const int blocks = static_cast<int>(std::atof(_argv[5]) * VoiceCpu::SampleRate / syntakt::BlockSize);

	// cli.py make_params: every parameter and machine byte at 0x40, track 1's machine and note
	for(int t = 0; t < VoiceCpu::Tracks; ++t)
	{
		cpu.setMachine(t, 0x40);
		for(const auto page : {VoiceCpu::Page::Machine, VoiceCpu::Page::Filter, VoiceCpu::Page::Amp})
			for(int i = 0; i < VoiceCpu::parameterCount(page); ++i)
				cpu.setParameter(t, page, i, 0x40, 0x40);
	}
	cpu.setMachine(0, static_cast<uint8_t>(machine));
	cpu.setNote(0, note);

	std::vector<int32_t> out(static_cast<size_t>(blocks) * syntakt::BlockSize);
	std::array<int32_t, syntakt::BlockSize> others{};
	for(int b = 0; b < blocks; ++b)
	{
		if(b == 0 || b == 2)
			cpu.trig(0);
		int32_t* outputs[VoiceCpu::Tracks] = {out.data() + syntakt::BlockSize * b};
		for(int t = 1; t < VoiceCpu::Tracks; ++t)
			outputs[t] = others.data();
		cpu.render(outputs, syntakt::BlockSize);
	}
	writeWav(_argv[2], out);
	std::printf("%s: %d blocks\n", _argv[2], blocks);
	return 0;
}
