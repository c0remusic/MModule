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

	// The TRX machines' routines (mdVoiceTrx.cpp), by type (TRX-BD 17 to TRX-S2 30): nullptr for any other type
	namespace trx
	{
		const Routines* routines(uint32_t _type);
	}

	// The EFM machines' routines (mdVoiceEfm.cpp), by type (EFM-BD 33 to EFM-CY 40): nullptr for one not ported
	namespace efm
	{
		const Routines* routines(uint32_t _type);
	}

	// GND-SN's and GND-NS's routines (mdVoiceGnd.cpp)
	namespace gnd
	{
		void updateSn(Context& _c);
		void renderSn(Context& _c, Voice::Block& _out);
		void initNs(Context& _c);
		void updateNs(Context& _c);
		void renderNs(Context& _c, Voice::Block& _out);
	}

	// What mdVoicePortTest sees inside the port; a product leaves them empty
	namespace testHooks
	{
		// The ported renders' branches the test must reach
		enum Probe
		{
			EfmCpClap,				// EFM-CP's level jumping back up, a clap
			EfmCpLastClap,			// EFM-CP's last clap, its decay Y:$02 from there
			EfmHhLevelLimited,		// a Hit that adds EFM-HH's level past full scale
			EfmCyLevelLimited,		// a Hit that adds EFM-CY's level past full scale
			EfmXtLevelLimited,		// a Hit that adds EFM-XT's level past full scale
			EfmSdLevelLimited,		// a Hit that adds EFM-SD's level past full scale
			EfmBdLevelLimited,		// a Hit that adds EFM-BD's level past full scale
			S2First,				// TRX-S2's first block, the ramp alone
			S2SweepRise,			// ... a sine's sweep rising
			S2SweepDecay,			// ... decaying
			S2Burst,				// ... a noise word past its threshold
			S2BurstBuild,			// ... its burst's level building
			S2BurstRateLate,		// ... its burst's second rate
			S2RateEarly,			// ... its amp envelope's first rate
			B2QuarterA,				// TRX-B2's phase in its first quarter
			B2QuarterB,				// ... second
			B2QuarterC,				// ... third
			B2QuarterD,				// ... fourth
			B2SineWrapped,			// ... its offset wrapped at the sine's period
			B2SineOutside,			// ... its offset outside the sine
			B2RateLow,				// ... its slower level run out
			B2RiseEnd,				// ... its amp envelope past one half
			XcSilent,				// TRX-XC's second block after a Hit, silent
			XcWave,					// ... the third, its waveform
			ClSine,					// TRX-CL's first sine, a whole block
			ClClick,				// ... its click, a whole block with more to come
			ClClickRest,			// ... the click's last words, then the sine
			ClClickEnd,				// ... the click ending with the block, no word of the sine
			ClLevelLimited,			// a Hit that adds TRX-CL's level past full scale
			MaRise,					// TRX-MA's level rising
			MaHold,					// ... held
			MaDecay,				// ... falling
			MaSilent,				// ... silent after all three
			MaPeak,					// TRX-MA's follower reaching its peak
			MaFall,					// ... falling from it
			CyLevelALimited,		// a Hit that adds TRX-CY's first level past full scale
			CyLevelBLimited,		// ... its second
			ChHold,					// TRX-CH's level held while its first counter runs
			ChRamp,					// ... falling by a step a sample while its second runs
			ChDecay,				// ... decaying after both
			ChLevelLimited,			// a Hit that adds TRX-CH's level past full scale
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
				case EfmCpClap:				return "EFM-CP's clap";
				case EfmCpLastClap:			return "EFM-CP's last clap";
				case EfmHhLevelLimited:		return "EFM-HH's level limited at a Hit";
				case EfmCyLevelLimited:		return "EFM-CY's level limited at a Hit";
				case EfmXtLevelLimited:		return "EFM-XT's level limited at a Hit";
				case EfmSdLevelLimited:		return "EFM-SD's level limited at a Hit";
				case EfmBdLevelLimited:		return "EFM-BD's level limited at a Hit";
				case S2First:				return "TRX-S2's first block";
				case S2SweepRise:			return "TRX-S2's sweep rising";
				case S2SweepDecay:			return "TRX-S2's sweep decaying";
				case S2Burst:				return "TRX-S2's noise past its threshold";
				case S2BurstBuild:			return "TRX-S2's burst building";
				case S2BurstRateLate:		return "TRX-S2's burst's second rate";
				case S2RateEarly:			return "TRX-S2's first amp rate";
				case B2QuarterA:			return "TRX-B2's first quarter";
				case B2QuarterB:			return "TRX-B2's second quarter";
				case B2QuarterC:			return "TRX-B2's third quarter";
				case B2QuarterD:			return "TRX-B2's fourth quarter";
				case B2SineWrapped:			return "TRX-B2's offset wrapped";
				case B2SineOutside:			return "TRX-B2's offset outside the sine";
				case B2RateLow:				return "TRX-B2's rate after its slower level";
				case B2RiseEnd:				return "TRX-B2's rise ended";
				case XcSilent:				return "TRX-XC's silent block";
				case XcWave:				return "TRX-XC's waveform";
				case ClSine:				return "TRX-CL's first sine";
				case ClClick:				return "TRX-CL's click";
				case ClClickRest:			return "TRX-CL's click ending in a block";
				case ClClickEnd:			return "TRX-CL's click ending with a block";
				case ClLevelLimited:		return "TRX-CL's level limited at a Hit";
				case MaRise:				return "TRX-MA's level rising";
				case MaHold:				return "TRX-MA's level held";
				case MaDecay:				return "TRX-MA's level falling";
				case MaSilent:				return "TRX-MA silent";
				case MaPeak:				return "TRX-MA's follower at its peak";
				case MaFall:				return "TRX-MA's follower falling";
				case CyLevelALimited:		return "TRX-CY's first level limited at a Hit";
				case CyLevelBLimited:		return "TRX-CY's second level limited at a Hit";
				case ChHold:				return "TRX-CH's level held";
				case ChRamp:				return "TRX-CH's level ramping down";
				case ChDecay:				return "TRX-CH's level decaying";
				case ChLevelLimited:		return "TRX-CH's level limited at a Hit";
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
			// retrigger leaves unwritten (a sample read as an address), through the decoding ring's last two words
			// and outside TRX-B2's sine; nullptr reads 0. The test reads its oracle's memory there, the emulator's
			uint32_t (*stray)(const void* _user, uint32_t _addr) = nullptr;
			const void* strayUser = nullptr;
			std::array<int64_t, ProbeCount>* probes = nullptr;
		};

		// Inline: a probe sits on some render's path for every sample
		inline Hooks g_hooks;

		inline Hooks& hooks()
		{
			return g_hooks;
		}

		inline void probe(const Probe _p)
		{
			if(auto* probes = hooks().probes)
				++(*probes)[_p];
		}
	}

	// The external memory, which X and Y share: the decoding table, the bank's directory and samples
	inline uint32_t external(const Context& _c, const uint32_t _addr)
	{
		if(_addr - 0x146000 < 0x1000 && _c.shared.tables)
			return _c.shared.tables->decode[_addr - 0x146000] & 0xffffff;
		const auto* bank = _c.shared.bank;
		if(bank && _addr - RomBank::DirectoryAddr < RomBank::DirectoryWords)
			return bank->directory[_addr - RomBank::DirectoryAddr] & 0xffffff;
		if(bank && _addr - RomBank::DataAddr < bank->dataWords)
			return bank->data[_addr - RomBank::DataAddr] & 0xffffff;
		const auto& hooks = testHooks::hooks();
		return hooks.stray ? hooks.stray(hooks.strayUser, _addr) & 0xffffff : 0;
	}
}
