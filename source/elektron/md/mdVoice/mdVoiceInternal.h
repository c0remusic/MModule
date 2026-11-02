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

	// A type's three entries, as the dispatch tables at Y:$145AF5, $145BB6 and $145C77 give them
	struct Routines
	{
		void (*init)(Context&);
		void (*update)(Context&);
		void (*render)(Context&, Voice::Block&);
	};

	// The TRX machines' routines (mdVoiceTrx.cpp), by type (TRX-BD 17 to TRX-S2 30): nullptr for a type not ported
	namespace trx
	{
		const Routines* routines(uint32_t _type);
	}

	// What mdVoicePortTest sees inside the port; a product leaves them empty
	namespace testHooks
	{
		// The ROM and TRX renders' branches the test must reach
		enum Probe
		{
			XtFade,					// TRX-XT's block after a Hit: the last sample faded out
			XtWaveA,				// TRX-XT's first sampled block
			XtWaveB,				// ... its second
			XtClipAbove,			// TRX-XT's clipper past its upper threshold
			XtClipAt,				// ... on it, which counts as past
			XtClipBelow,			// ... past its lower
			SdPitchRise,			// TRX-SD's pitch envelope while it rises
			SdPitchDecay,			// ... once it decays
			SdWave,					// TRX-SD's sampled waveform added
			SdTail,					// ... past its end
			SdLevelLimited,			// a Hit that adds TRX-SD's level past full scale
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

		// Their names, in the enum's order, which the test prints
		constexpr const char* kProbeNames[] = {"TRX-XT's fade after a Hit", "TRX-XT's waveform A",
			"TRX-XT's waveform B", "TRX-XT's clipper above", "TRX-XT's clipper on its upper threshold",
			"TRX-XT's clipper below", "TRX-SD's pitch rising", "TRX-SD's pitch decaying", "TRX-SD's waveform",
			"TRX-SD's tail past the waveform", "TRX-SD's level limited at a Hit", "TRX-BD's fade after a Hit",
			"TRX-BD's attack", "TRX-BD's clipper above",
			"TRX-BD's clipper below", "TRX-BD's shaper above", "TRX-BD's shaper below", "TRX-BD's level limited at a Hit",
			"ROM forward fill's odd first sample", "ROM reverse fill's even first sample", "ROM last sample held",
			"ROM reversed stop at the start", "ROM reversed retrigger with an odd first part", "ROM stall"};
		static_assert(std::size(kProbeNames) == ProbeCount, "a probe without its name");

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
