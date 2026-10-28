#pragma once

#include "stFastBench.h"

namespace syntakt::fast::bench
{
	// BD MODERN's 16 Hits, stFastBdModernTest's, which stFastAbRender, stFastSweep and stFastResampleTest play too:
	// TUNE, U2 level, transient, SWEEP, CONTOUR, drive, HOLD, DECAY. The retrigger sets SWEEP and the drive to 127; BD
	// MODERN is the exact port's machine 0
	inline const HitSet BdModernHits = {{
		{{88, 64, 64, 64, 64, 0, 0, 64}, {}, 36, false, false},
		{{88, 0, 0, 0, 0, 0, 0, 40}, {}, 36, false, false},
		{{88, 127, 127, 127, 127, 1, 0, 90}, {}, 36, false, false},
		{{100, 32, 96, 20, 100, 0, 0, 110}, {}, 40, false, false},
		{{76, 96, 32, 110, 10, 1, 0, 70}, {}, 30, false, false},
		{{88, 64, 127, 64, 64, 1, 0, 127}, {}, 36, false, false},
		{{88, 127, 0, 127, 0, 0, 0, 100}, {}, 48, false, false},
		{{64, 80, 50, 30, 120, 0, 0, 80}, {}, 60, false, false},
		{{88, 64, 64, 64, 64, 0, 1, 90}, {}, 36, false, true},
		{{110, 20, 110, 90, 40, 1, 0, 60}, {}, 24, false, false},
		{{88, 50, 70, 50, 70, 0, 0, 96}, {128, 77, 200, 0, 33, 0, 0, 0}, 36, false, false},
		{{88, 64, 64, 64, 64, 1, 0, 100}, {}, 36, true, false},
		{{127, 40, 40, 40, 40, 0, 0, 80}, {}, 50, false, false},
		{{0, 90, 90, 90, 90, 1, 0, 85}, {}, 100, false, false},
		{{88, 110, 20, 80, 60, 1, 1, 120}, {}, 33, false, true},
		{{88, 64, 100, 0, 64, 0, 0, 100}, {}, 36, true, false},
	}, {3, 5}, 0};
}
