#pragma once

#include "mdVoice.h"

#include <cstdint>
#include <vector>

namespace elektron::drums
{
	// The sampled waves of TRX-BD, SD, XT, RS and XC made here from formulas. Their pitches, decays and levels were
	// tuned by ear and against measurements of the Machinedrum's output; no word of it goes into them. TRX-CH's six
	// loops are squares, whose level and the lowest's duty are the two numbers read from its words.
	void makeHouseWaves(md::voice::TrxTables& _tables);

	// What PiBank names, made here the same way: the P-I drums' modes and strikes, and a curve of ours for its one-pole
	// table, whose shape was fitted by eye to the machine's table without taking a word of it
	std::vector<uint32_t> makeHousePi();
}
