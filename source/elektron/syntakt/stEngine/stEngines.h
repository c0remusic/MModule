#pragma once

#include <cstdint>

#include "stEngine.h"
#include "stVoice.h"

// The Syntakt's digital drum engines, ported from OS 1.41's voice CPU, each in its own file: update(pitch, voice,
// P + 0x1c) every block, then render(out, voice). .scratch/elektron-engines describes them.
namespace syntakt
{
	// BD MODERN: a sine at the note, phase-modulated by an enveloped mix of two oscillators an octave up, under a pitch
	// sweep, with a filtered click, a drive stage and the amp envelope
	namespace bdModern
	{
		void update(int32_t _pitch, Voice& _v, uint32_t _params, const Rom& _rom);
		void render(int32_t* _out, Voice& _v, const Rom& _rom);

		// 0x40004ee6: the click, a 10 ms ramp, high-passed then low-passed, into _buf
		void click(int32_t* _buf, Voice& _v);

		// 0x400046dc: _out = _b + _a, saturated
		void sum(const int32_t* _a, const int32_t* _b, int32_t* _out);
	}

	namespace sdBasic
	{
		void update(int32_t _pitch, Voice& _v, uint32_t _params, const Rom& _rom);
		void render(int32_t* _out, Voice& _v, const Rom& _rom);
	}

	namespace cyAlloy
	{
		void update(int32_t _pitch, Voice& _v, uint32_t _params, const Rom& _rom);
		void render(int32_t* _out, Voice& _v, const Rom& _rom);
	}

	namespace pcCarbon
	{
		void update(int32_t _pitch, Voice& _v, uint32_t _params, const Rom& _rom);
		void render(int32_t* _out, Voice& _v, const Rom& _rom);
	}

	namespace sdVintage
	{
		void update(int32_t _pitch, Voice& _v, uint32_t _params, const Rom& _rom);
		void render(int32_t* _out, Voice& _v, const Rom& _rom);
	}

	namespace cpVintage
	{
		void update(int32_t _pitch, Voice& _v, uint32_t _params, const Rom& _rom);
		void render(int32_t* _out, Voice& _v, const Rom& _rom);
	}

	// The engines ported, the one list engine() and the oracle read: the engine's id, its name, the firmware's update
	// and render routines, and the port's
	inline constexpr EngineRoutines PortedEngines[] = {
		{0, "bdModern", 0x40006968, 0x40006c9e, bdModern::update, bdModern::render},
		{1, "sdBasic", 0x40007c68, 0x40007f9e, sdBasic::update, sdBasic::render},
		{2, "cyAlloy", 0x40006d7c, 0x40006ff2, cyAlloy::update, cyAlloy::render},
		{3, "pcCarbon", 0x40007280, 0x400075a8, pcCarbon::update, pcCarbon::render},
		{6, "sdVintage", 0x40008074, 0x4000847a, sdVintage::update, sdVintage::render},
		{7, "cpVintage", 0x40008580, 0x40008988, cpVintage::update, cpVintage::render},
	};
}
