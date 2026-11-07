#pragma once

#include "mdVoice.h"

namespace elektron::drums
{
	// The sampled waves of TRX-BD, SD, XT, RS and XC made here from formulas. Their pitches, decays and levels were
	// tuned by ear and against measurements of the Machinedrum's output; no word of it goes into them.
	void makeHouseWaves(md::voice::TrxTables& _tables);
}
