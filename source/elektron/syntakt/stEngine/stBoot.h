#pragma once

#include <cstdint>

#include "stVoice.h"

namespace syntakt
{
	// The voice CPU's boot as far as the voices need it: SRAM's initialized data copied from section 7 (the RAM sine
	// and exp2 tables among them), and each voice's history pointer and init, the noise generator seeded anew before
	// each voice (0x40000fd6, 0x40002544)
	void boot(Sram& _sram, Globals& _g, const Rom& _rom);

	// The audio interrupt's first part (0x40000934): CPU #1's block copied to 0x80008000 and spread into the per-track
	// parameter array (0x80003c40, 0x8e bytes per track); the voices' trig and gate-off bits returned
	void receiveBlock(Sram& _sram, const uint8_t* _block, int32_t& _trig, int32_t& _gateOff);

	// One block as the interrupt runs it: receiveBlock, renderVoices into 0x80003840, then each track's stages in place
	// (drive, BASE/WIDTH, filter, amp envelope). The block then holds what the voice CPU sends CPU #1
	void runBlock(Sram& _sram, Globals& _g, const Rom& _rom, const uint8_t* _block);
}
