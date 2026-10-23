#pragma once

#include <cstdint>

#include "stMainEmac.h"
#include "stMainMemory.h"

// CPU #1's reverb, 0x40096e3c (cpu1.md, section 4), on the line buffers channel 30 read into SRAM (stMainFx.h): a
// pre-delay line, eight allpass diffusers, then two halves of four lines each, mixed by a 4 x 4 sign matrix scaled
// by DEC, through a shelving filter, one line of each half read at a sine-modulated position. MACSR 0xa0
namespace syntakt::main
{
	// 0x40096e3c(_out, _in, _params = the smoothed copy's reverb words, PRE DEC FREQ GAIN HPF LPF): the mono input
	// (L + R) / 2 into line 0 (the pre-delay: (PRE^2 >> 16) + 37 samples, at most 16384), its output low-passed and
	// high-passed (poles 0x4029bffc), then the 8 diffusers in series (allpass, gains DEC's 0x4029b6fc x 0x8000d630),
	// the two halves (lines 1-4, 5-8): their shelving (FREQ 0x4029b7fc, GAIN), the modulated tap (interpolation
	// 0x4029b9fc), the sums into _out, the lines' inputs by the matrix 0x401df8e0 + 32 DEC
	void reverb(Memory& _mem, Emac4& _acc, uint32_t _out, uint32_t _in, uint32_t _params);

	// 0x40096dba: the diffusers' and the lines' write positions into channel 42's descriptors, then 32 samples on
	void advanceReverbLines(Memory& _mem);
}
