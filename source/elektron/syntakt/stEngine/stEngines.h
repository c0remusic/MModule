#pragma once

#include <cstdint>

#include "stVoice.h"

// The Syntakt's digital drum engines after BD MODERN (stBdModern.h), ported from OS 1.41's voice CPU, each in its own
// file: update(pitch, voice, P + 0x1c) every block, then render(out, voice). .scratch/elektron-engines describes them.
#define SYNTAKT_ENGINE(_name) \
	namespace syntakt::_name \
	{ \
		void update(int32_t _pitch, Voice& _v, uint32_t _params, const Rom& _rom); \
		void render(int32_t* _out, Voice& _v, const Rom& _rom); \
	}

SYNTAKT_ENGINE(sdBasic)		// engine 1: update 0x40007c68, render 0x40007f9e
SYNTAKT_ENGINE(cyAlloy)		// engine 2: update 0x40006d7c, render 0x40006ff2
SYNTAKT_ENGINE(pcCarbon)	// engine 3: update 0x40007280, render 0x400075a8
SYNTAKT_ENGINE(sdVintage)	// engine 6: update 0x40008074, render 0x4000847a
SYNTAKT_ENGINE(cpVintage)	// engine 7: update 0x40008580, render 0x40008988

#undef SYNTAKT_ENGINE
