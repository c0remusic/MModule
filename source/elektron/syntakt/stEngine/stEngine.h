#pragma once

#include <cstdint>

#include "stVoice.h"

namespace syntakt
{
	// An engine's two routines: update (block rate, from the track's parameters) then render (the block), with the
	// engine's id, its name and the firmware's addresses of the two
	struct EngineRoutines
	{
		uint32_t id;
		const char* name;
		uint32_t updateAddress, renderAddress;

		// _pitch: the track's note in semitones Q16; _params: the track's parameter words, P + 0x1c
		void (*update)(int32_t _pitch, Voice& _v, uint32_t _params, const Rom& _rom);
		void (*render)(int32_t* _out, Voice& _v, const Rom& _rom);
	};

	// The engine numbered _id (0 BD MODERN ... 11 SP TWINSHOT) from PortedEngines (stEngines.h), null where it is not
	// ported yet or out of range
	const EngineRoutines* engine(uint32_t _id);

	// 0x40014950: the engine machine _machine plays, the machine clamped to 0-45 as the voice CPU clamps it
	int32_t engineOf(const Rom& _rom, int32_t _machine);

	// 0x40004324: one block of the eight voices. _out receives 8 x 32 samples, voice v at _out + 128 v; _params is the
	// per-track parameter array (0x80003c40, 0x8e bytes per track); _trig and _gateOff hold one bit per voice. A trig is
	// seen in its block as pending (the voice fades out) and starts the voice in the next block; an engine changes only
	// there. Voices render grouped by machine, as in the original. Voices on an engine not ported stay silent.
	void renderVoices(Sram& _sram, Globals& _g, const Rom& _rom, uint32_t _out, uint32_t _params, int32_t _trig,
		int32_t _gateOff);
}
