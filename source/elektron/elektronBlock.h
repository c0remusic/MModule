#pragma once

// What every family's engine renders: the 16 Tracks side by side, each field an array of 16 lanes, in blocks of 32
// samples at the family's own rate. A vector op covers 4 lanes.
namespace elektron
{
	constexpr int Lanes = 16;		// one per Track
	constexpr int BlockSize = 32;	// samples per block

	// The 16 Tracks' samples of a block, sample-major: s[n] holds sample n of every Track. A stage computes lanes 0 to
	// lanes - 1 of the block it writes (a multiple of 4: a vector's) and leaves the others as they are
	struct alignas(16) Block
	{
		float s[BlockSize][Lanes];
		int lanes = Lanes;
	};
}
