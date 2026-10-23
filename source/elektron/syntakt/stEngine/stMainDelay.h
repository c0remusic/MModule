#pragma once

#include <cstdint>

#include "stMainEmac.h"
#include "stMainMemory.h"

// CPU #1's delay, 0x40096890 (cpu1.md, section 4), on the two taps channel 30 read into SRAM (stMainFx.h). MACSR 0xa0
namespace syntakt::main
{
	// 0x40096890(_out, _in, _params = the smoothed copy's delay words, TIME X WID FDBK HPF LPF): per frame, the two
	// taps crossfaded over the block (the last block's time fading out, the new one in), soft-clipped (2x - x|x|),
	// scaled by 1/32 (ramped in from the state), then a high-pass and a low-pass one-pole from the pole table
	// 0x4029bffc (HPF >> 6, (HPF + LPF) >> 6) and x 16: that is _out. The buffer at 0x8000d6b0's pointer then gets
	// the ring's next input: FDBK (x -66053, saturated, ramped) times the filtered signal, L and R crossed when X is
	// set, plus the input spread by WID (four ramped gains). Channel 42 (writeLines) writes it to the ring
	void delay(Memory& _mem, Emac4& _acc, uint32_t _out, uint32_t _in, uint32_t _params);

	// 0x40096802: the ring's write position 32 frames on (0x8000dac8, modulo 2^19), then channel 42: the 32 frames at
	// 0x8000d6b0's pointer into the ring at the old position, then by scatter-gather the reverb's 17 lines' inputs
	void writeLines(Memory& _mem);
}
