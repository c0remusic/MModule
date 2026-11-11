#pragma once

#include <array>
#include <cstdint>

#include "sophieVoice.h"

// Sophie's 19 parameters as the 0-127 values of a machine page (Machine 1-19), README.md gives each law
namespace sophie
{
	constexpr int MachineParamCount = 19;

	// Factory pad 1 (the kick) to the nearest value of each law
	constexpr std::array<uint8_t, MachineParamCount> MachineDefaults =
	{
		0,	// Model: Fuse
		54,	// Tune: -5 semitones
		30,	// Color: 23.6 (24)
		10,	// Metal: 7.9 (8)
		8,	// Feedback: 6.3 (6)
		99,	// Sweep: 55.6 (55)
		58,	// Decay: 0.280 s (0.28)
		0,	// Crush: 0
		74,	// Drive: 58.3 (58)
		127,// Cutoff: 100
		0,	// Resonance: 0
		0,	// Filter Type: LPF
		77,	// Ring Time: 5.98 ms (6)
		0,	// Ring Feedback: 0
		0,	// Ring Mix: 0
		89,	// Ring Tone: 70.1 (70)
		0,	// Metal Decay: off
		0,	// Feedback Mode: Raw
		0	// Tail Mode: Hit
	};

	// Sophie's value of machine parameter _index (0-18, sophie_param_t's order) at _value (0-127)
	float machineValue(int _index, uint8_t _value);

	// Sets parameter _index of _sophie from its machine value
	void setMachineValue(sophie_t& _sophie, int _index, uint8_t _value);
}
