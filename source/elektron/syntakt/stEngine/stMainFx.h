#pragma once

#include <cstdint>

#include "stMainEmac.h"
#include "stMainMemory.h"

// What CPU #1's delay and reverb keep and how their lines move (cpu1.md, section 4): the lines live in DDR (the
// delay's ring of 2^19 stereo frames at 0x4fc00000, the reverb's 9 lines of 64 KB at 0x4d570000 and 8 of 4 KB at
// 0x4d600000) and reach the routines through SRAM buffers that the eDMA fills and empties. At the start of each block
// 0x40097414 aims channel 30 at the delay's two taps and the reverb's 17 read positions and starts it; at the end
// 0x40096802 starts channel 42, which writes the block's delay input and the reverb lines' inputs back. The port moves
// the bytes as the channel would (runChannel), at the point the code starts it: nothing else touches them between.
namespace syntakt::main
{
	constexpr uint32_t DelayRing = 0x4fc00000;		// 2^19 frames of L, R longs, modulo 4 MB
	constexpr uint32_t ReverbLines = 0x4d570000;	// 9 x 64 KB, mono longs
	constexpr uint32_t ReverbDiffusers = 0x4d600000;	// 8 x 4 KB, mono longs

	// An eDMA transfer control descriptor, as in the channel's registers or in memory (scatter-gather)
	struct Tcd
	{
		uint32_t saddr = 0;
		uint16_t attr = 0;		// SMOD (15-11), SSIZE (10-8), DMOD (7-3), DSIZE (2-0)
		int16_t soff = 0;
		uint32_t nbytes = 0;
		int32_t slast = 0;
		uint32_t daddr = 0;
		uint16_t citer = 0;		// bit 15 ELINK: then 9 bits of count
		int16_t doff = 0;
		uint32_t dlastSga = 0;
		uint16_t biter = 0;
		uint16_t csr = 0;		// bit 0 START, bit 4 ESG, bit 7 DONE
	};

	Tcd readTcd(const Memory& _mem, uint32_t _address);

	// A channel started with _tcd: its major loop, then while ESG is set the descriptor at DLAST_SGA, loaded from
	// memory, if its START is set. A minor loop moves NBYTES as SSIZE reads (SADDR += SOFF modulo 2^SMOD) and DSIZE
	// writes (DADDR += DOFF modulo 2^DMOD). Returns what the channel's registers hold at the end: the last descriptor,
	// START clear, DONE set
	Tcd runChannel(Memory& _mem, Tcd _tcd);

	// CPU #1's SRAM as the OS's start copies it from section 3 (0x4000045e): its initialized data, the delay's and the
	// reverb's line lengths and gains among them
	void loadSramImage(Memory& _mem);

	// 0x40096668 and 0x40096b86, the delay's and the reverb's inits: their state, lines and descriptors (the channels'
	// own registers are the port's runChannel arguments, not memory)
	void delayInit(Memory& _mem);
	void reverbInit(Memory& _mem);

	// A modulated line's position (0x40097414 for line 4, 0x40096e3c for line 8): its length at _base << 18 plus
	// _sine x 0x200000, its whole samples at _at, their step from the last block, a 5-bit interpolation index and the
	// fraction below it at 0x8000c51c, 0x8000c520, 0x8000c524
	void modulatedPosition(Memory& _mem, Emac4& _acc, uint32_t _base, int32_t _sine, uint32_t _at);

	// 0x40097414(_p, the smoothed copy 0x80006348): the reverb's diffusers' and lines' read positions
	// (write position less length) into channel 30's descriptors, its sine oscillator (0x8000c514/18, its output
	// 0x4550e1c8) and the first modulated line's position, the delay's time from TIME (+0x6e6) and the tempo,
	// smoothed, its two taps (the new time and the last block's), then channel 30 runs. MACSR 0xa0, then 0x20
	void prepareEffects(Memory& _mem, Emac4& _acc, uint32_t _p);
}
