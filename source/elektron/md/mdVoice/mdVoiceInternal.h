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

	// ROM-01 to -32 (types 129-160) and ROM-33 to -48 (177-192) share them
	inline bool isRom(const uint32_t _type)
	{
		return (_type >= 129 && _type <= 160) || (_type >= 177 && _type <= 192);
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
			CbFade,					// TRX-CB's block after a Hit: the last sample faded out
			CbSilentA,				// ... the next block, silent
			CbSilentB,				// ... and the one after it
			CbClipAbove,			// TRX-CB's square past its threshold
			CbClipBelow,			// ... past its negative
			CbLevelLimited,			// a Hit that adds TRX-CB's level past full scale
			RsFoldAbove,			// TRX-RS's fold past its threshold
			RsFoldBelow,			// ... past its negative
			RsWave,					// TRX-RS's sampled waveform added
			RsTail,					// ... past its end
			RsLevelLimited,			// a Hit that adds TRX-RS's level past full scale
			CpBurstA,				// TRX-CP's first burst, its counter running
			CpBurstB,				// ... its second
			CpBurstC,				// ... its third
			CpDecay,				// ... the last decay
			CpLevelLimited,			// a burst that adds TRX-CP's level past full scale
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

		// Their names, which the test prints; a switch, so that each name goes with its probe whatever the order
		constexpr const char* probeName(const Probe _p)
		{
			switch(_p)
			{
				case CbFade:				return "TRX-CB's fade after a Hit";
				case CbSilentA:				return "TRX-CB's first silent block";
				case CbSilentB:				return "TRX-CB's second silent block";
				case CbClipAbove:			return "TRX-CB's square above";
				case CbClipBelow:			return "TRX-CB's square below";
				case CbLevelLimited:		return "TRX-CB's level limited at a Hit";
				case RsFoldAbove:			return "TRX-RS's fold above";
				case RsFoldBelow:			return "TRX-RS's fold below";
				case RsWave:				return "TRX-RS's waveform";
				case RsTail:				return "TRX-RS's tail past the waveform";
				case RsLevelLimited:		return "TRX-RS's level limited at a Hit";
				case CpBurstA:				return "TRX-CP's first burst";
				case CpBurstB:				return "TRX-CP's second burst";
				case CpBurstC:				return "TRX-CP's third burst";
				case CpDecay:				return "TRX-CP's last decay";
				case CpLevelLimited:		return "TRX-CP's level limited at a burst";
				case XtFade:				return "TRX-XT's fade after a Hit";
				case XtWaveA:				return "TRX-XT's waveform A";
				case XtWaveB:				return "TRX-XT's waveform B";
				case XtClipAbove:			return "TRX-XT's clipper above";
				case XtClipAt:				return "TRX-XT's clipper on its upper threshold";
				case XtClipBelow:			return "TRX-XT's clipper below";
				case SdPitchRise:			return "TRX-SD's pitch rising";
				case SdPitchDecay:			return "TRX-SD's pitch decaying";
				case SdWave:				return "TRX-SD's waveform";
				case SdTail:				return "TRX-SD's tail past the waveform";
				case SdLevelLimited:		return "TRX-SD's level limited at a Hit";
				case BdFade:				return "TRX-BD's fade after a Hit";
				case BdAttack:				return "TRX-BD's attack";
				case BdClipAbove:			return "TRX-BD's clipper above";
				case BdClipBelow:			return "TRX-BD's clipper below";
				case BdShapeAbove:			return "TRX-BD's shaper above";
				case BdShapeBelow:			return "TRX-BD's shaper below";
				case BdLevelLimited:		return "TRX-BD's level limited at a Hit";
				case ForwardOdd:			return "ROM forward fill's odd first sample";
				case ReverseEven:			return "ROM reverse fill's even first sample";
				case HeldLastSample:		return "ROM last sample held";
				case BackwardStop:			return "ROM reversed stop at the start";
				case ReverseOddRetrigger:	return "ROM reversed retrigger with an odd first part";
				case Stall:					return "ROM stall";
				case ProbeCount:			break;
			}
			return nullptr;
		}

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
