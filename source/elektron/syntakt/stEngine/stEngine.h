#pragma once

#include <cstdint>

#include "stVoice.h"

namespace syntakt
{
	constexpr int Voices = 8;
	constexpr int Engines = 12;

	// An engine's two routines: update (block rate, from the track's parameters) then render (the block)
	struct EngineRoutines
	{
		// _pitch: the track's note in semitones Q16; _params: the track's parameter words, P + 0x1c
		void (*update)(int32_t _pitch, Voice& _v, uint32_t _params, Sram& _sram, Globals& _g, const Rom& _rom) = nullptr;
		void (*render)(int32_t* _out, Voice& _v, Sram& _sram, Globals& _g, const Rom& _rom) = nullptr;
	};

	// The engines ported so far, by the voice CPU's engine number (0 BD MODERN ... 11 SP TWINSHOT); null where not
	const EngineRoutines& engine(int32_t _id);

	// 0x40004324: one block of the eight voices. _out receives 8 x 32 samples, voice v at _out + 128 v; _params is the
	// per-track parameter array (0x80003c40, 0x8e bytes per track); _trig and _gateOff hold one bit per voice. A trig is
	// seen in its block as pending (the voice fades out) and starts the voice in the next block; an engine changes only
	// there. Voices render grouped by machine, as in the original. Voices on an engine not ported stay silent.
	void renderVoices(Sram& _sram, Globals& _g, const Rom& _rom, uint32_t _out, uint32_t _params, int32_t _trig,
		int32_t _gateOff);
}
