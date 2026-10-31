// md::voice's inside: what a machine's routines see, the ROM routines the dispatcher calls, and the hooks only
// mdVoicePortTest sets
#pragma once
#include "mdVoice.h"

namespace md::voice
{
	// A machine's routines see the voice's two blocks, its type and what the voices share
	struct Context
	{
		Voice::State& y;
		Voice::State& x;
		uint32_t type;
		Shared& shared;
	};

	// The ROM machines' routines (mdVoiceRom.cpp): init ($13D), update ($15A) and render ($16C)
	namespace rom
	{
		void init(Context& _c);
		void update(Context& _c);
		void render(Context& _c, Voice::Block& _out);
	}

	// TRX-BD's routines (mdVoiceTrx.cpp): init ($1003DA), update ($1003E1) and render ($100408)
	namespace trxBd
	{
		void init(Context& _c);
		void update(Context& _c);
		void render(Context& _c, Voice::Block& _out);
	}

	// What mdVoicePortTest sees inside the port; a product leaves them empty
	namespace testHooks
	{
		// The ROM and TRX-BD renders' branches the test must reach
		enum Probe
		{
			BdFade,					// TRX-BD's block after a Hit: the last sample faded out
			BdAttack,				// TRX-BD's attack, the sampled waveforms
			BdClipAbove,			// TRX-BD's clipper past its upper threshold
			BdClipBelow,			// ... past its lower
			BdShapeAbove,			// TRX-BD's shaper past X0
			BdShapeBelow,			// ... past X1
			BdLevelLimited,			// a Hit that adds the level past full scale
			ForwardOdd,				// fillForward's first sample alone (the position in a word's second half)
			ReverseEven,			// fillReverse's first sample alone (the position in a word's first half)
			HeldLastSample,			// the forward window padded with the last pointer (no loop, past the end)
			BackwardStop,			// a reversed voice stopped at the sample's start
			ReverseOddRetrigger,	// a reversed retrigger whose first part has an odd length: a pointer left unwritten
			Stall,					// a fill count past $7FFFFF, where the DSP stalls: the block silent
			ProbeCount
		};

		struct Hooks
		{
			// A Y word the voice holds nowhere else, which the render reads through the pointer a reversed odd
			// retrigger leaves unwritten (a sample read as an address) and through the decoding ring's last two
			// words; nullptr reads 0. The test reads its oracle's memory there, the emulator's
			uint32_t (*stray)(const void* _user, uint32_t _addr) = nullptr;
			const void* strayUser = nullptr;
			std::array<int64_t, ProbeCount>* probes = nullptr;
		};

		Hooks& hooks();

		inline void probe(const Probe _p)
		{
			if(auto* probes = hooks().probes)
				++(*probes)[_p];
		}
	}
}
