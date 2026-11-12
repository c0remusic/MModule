#pragma once

#include <cstdint>

// The synths' tables that no formula gives exactly (registered in synthTables): the envelope generator's rate codes by
// knob value (each a 60 dB time on a smooth curve, a shift in bits 16-19 and a 16-bit mantissa below), here best fits,
// and what two routines read at a negative knob value before a table whose neighbour has no formula either. Inside
// edDrums only.
namespace elektron::drums::analog::port::rates
{
	// The decay and release of the dual and raw synths, k 0-127
	uint32_t synthEnvelope(int _k);
	// The chip synth's, k 0-127
	uint32_t chipEnvelope(int _k);

	// Below the start of a table, k -128 to -1: the raw synth's level, the synths' envelope rate
	int32_t belowRawLevel(int _k);
	uint32_t belowSynthEnvelope(int _k);
}
