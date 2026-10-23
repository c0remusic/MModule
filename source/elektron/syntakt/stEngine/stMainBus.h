#pragma once

#include <cstdint>

#include "stMainEmac.h"
#include "stMainMemory.h"

// The routines CPU #1's mix 0x4008f1ca sums, copies and scales its 32-sample buffers with (cpu1.md, section 4). A
// track buffer is 32 longs; channel k of a set of them starts 128 bytes after channel k - 1. A bus is 32 stereo
// frames of two longs, L then R; a routine that writes one side of it is given the side's first long and steps 8
// bytes. Gains are in the mix's negative form (INT32_MIN full): a sum is -(sum of gain x sample), so positive gains
// invert. Each gain ramps by its step after every sample, wrapping. The EMAC's mode is the caller's
namespace syntakt::main
{
	constexpr int BlockFrames = 32;

	// 0x4008ef14 (10 channels), 0x4008efac (9), 0x4008f03e (8): one side of a bus, the sum of _channels track buffers
	// from _src at the gains held at _gains (a long each, read, ramped by _steps and written back)
	void sumChannels(Memory& _mem, Emac4& _acc, int _channels, uint32_t _dst, uint32_t _src, uint32_t _gains,
		const int32_t* _steps);

	// 0x4008f1b4: one side of a bus at _src into the 32 longs of a track buffer at _dst
	void copySide(Memory& _mem, uint32_t _dst, uint32_t _src);

	// 0x4008ee80: the bus _src plus the bus 256 bytes after it, at two gains (_gains[0] for _src, _gains[1] for the
	// other, ramped by _steps and updated), into the bus _dst, which may be _src
	void mixBuses(Memory& _mem, Emac4& _acc, uint32_t _dst, uint32_t _src, int32_t* _gains, const int32_t* _steps);

	// 0x4008eeca: the 64 longs of the bus at _bus times the integer _gain, saturated (MACSR 0x80, then the caller's)
	void scaleBus(Memory& _mem, Emac4& _acc, uint32_t _bus, int32_t _gain);

	// 0x4008febc: the 32 longs at _buffer, in place, times a gain that ramps from _from by (_to - _from) >> 5
	void rampBuffer(Memory& _mem, Emac4& _acc, uint32_t _buffer, int32_t _from, int32_t _to);

	// 0x40097a90: -(table 0x401e08e0[_machine] x _word), the level an analog machine's Overbridge stem is taken at
	int32_t analogLevel(const Memory& _mem, Emac4& _acc, int32_t _machine, int32_t _word);
}
