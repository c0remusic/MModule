#pragma once

#include <array>
#include <cstdint>

#include "sophieVoice.h"

// Sophie's 19 parameters as the 0-127 values of a machine page (Machine 1-19), README.md gives each law, then BODY
// (Machine 20), the kick body that upstream plays on pad 1 alone
namespace sophie
{
	constexpr int MachineParamCount = 19;
	constexpr int Body = MachineParamCount;		// below 64 off, else on
	constexpr int MachineSlots = MachineParamCount + 1;

	// What the instrument's interface names Machine 1-20
	constexpr std::array<const char*, MachineSlots> MachineNames{"MODEL", "TUNE", "COLOR", "METAL", "FEEDBACK", "SWEEP",
		"DECAY", "CRUSH", "DRIVE", "CUTOFF", "RESO", "FILTER", "RING TIME", "RING FDBK", "RING MIX", "RING TONE",
		"METAL DEC", "FB MODE", "TAIL", "BODY"};

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

	// The 16 factory pads as sounds of one voice whose base note is pad 1's: sophie_factory_patch's order
	constexpr int Sounds = SOPHIE_PADS;
	constexpr std::array<const char*, Sounds> SoundNames{"KICK", "RIM", "SNARE", "CLAP", "SNARE 2", "LOW TOM",
		"CLOSED HH", "FLOOR TOM", "PEDAL HH", "MID TOM", "OPEN HH", "LOW-MID TOM", "HIGH-MID TOM", "CRASH", "HIGH TOM",
		"RIDE"};

	// Sophie's value of machine parameter _index (0-18, sophie_param_t's order) at _value (0-127)
	float machineValue(int _index, uint8_t _value);

	// Sets parameter _index (0-19, BODY last) of _sophie from its machine value
	void setMachineValue(sophie_t& _sophie, int _index, uint8_t _value);

	// Machine 1-20 of factory pad _sound (0-15) on pad 1's base note: each patch value to the nearest of its law, Tune
	// raised by the pad's semitones (upstream plays pad n at note 36 + n), BODY on for the kick alone, as upstream
	std::array<uint8_t, MachineSlots> soundValues(int _sound);
}
