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

	// The E12 machines' routines (mdVoiceE12.cpp), by type (E12-BD 49 to E12-BC 64): nullptr for any other type
	namespace e12
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
			E12StepClamped,			// an E12 machine's step past 4 samples
			E12EndOfSample,			// ... its position past the end
			E12Retrigger,			// ... a retrigger
			E12RetriggerNoWords,	// ... on the block's last sample, a DO of no words
			E12RetriggerEnds,		// ... with a time under a block: the zeros before it, and its count carried
			E12RetriggerLast,		// ... none left
			E12TapOutside,			// ... a tap outside the internal memory, a retrigger's stray position
			E12OddPosition,			// ... a window from a word's second half
			E12SecondSample,		// E12-RS's and the mixed machines' second sample mixed in
			E12SecondEnded,			// ... past its end
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
				case E12StepClamped:		return "E12's step clamped";
				case E12EndOfSample:		return "E12's end of sample";
				case E12Retrigger:			return "E12's retrigger";
				case E12RetriggerNoWords:	return "E12's retrigger of no words";
				case E12RetriggerEnds:		return "E12's retrigger time under a block";
				case E12RetriggerLast:		return "E12's retriggers run out";
				case E12TapOutside:			return "E12's tap outside the internal memory";
				case E12OddPosition:		return "E12's odd position";
				case E12SecondSample:		return "E12's second sample";
				case E12SecondEnded:		return "E12's second sample ended";
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

		// The memory a stray read means
		enum Area
		{
			AreaX,
			AreaY,
			AreaCount
		};

		struct Hooks
		{
			// A word external() cannot compute, which a render reads: in Y through the pointer a reversed odd retrigger
			// leaves unwritten (a sample read as an address), through the decoding ring's last two words and outside
			// TRX-B2's sine; in X through a tap of an E12 machine that a retrigger left outside the window (see
			// mdVoiceE12.cpp) or past the end of its samples, and through a filter's index past its tables. nullptr
			// reads 0, as a product does. The test reads its oracle's memory there, the emulator's
			uint32_t (*stray)(const void* _user, Area _area, uint32_t _addr) = nullptr;
			const void* strayUser = nullptr;
			// The address bits external() keeps: the voice DSP's 2M words, a stray address wrapping into them, as the
			// emulator's JIT has it. Its interpreter reads 0 past them instead, for the test's hand-made phase
			uint32_t addressMask = 0x1fffff;
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

	// The external memory, which X and Y share: the decoding table, the bank's directory and samples, the E12 machines',
	// the sine and TRX-CH's waves; else what the test's hook knows
	inline uint32_t external(const Context& _c, const uint32_t _address, const testHooks::Area _area = testHooks::AreaY)
	{
		const auto& hooks = testHooks::hooks();
		const uint32_t addr = _address & hooks.addressMask;
		if(addr - 0x146000 < 0x1000 && _c.shared.tables)
			return _c.shared.tables->decode[addr - 0x146000] & 0xffffff;
		const auto* bank = _c.shared.bank;
		if(bank && addr - RomBank::DirectoryAddr < RomBank::DirectoryWords)
			return bank->directory[addr - RomBank::DirectoryAddr] & 0xffffff;
		if(bank && addr - RomBank::DataAddr < bank->dataWords)
			return bank->data[addr - RomBank::DataAddr] & 0xffffff;
		if(const auto* e12 = _c.shared.e12; e12 && addr - E12Bank::Addr < E12Bank::Words)
			return e12[addr - E12Bank::Addr] & 0xffffff;
		if(const auto* trx = _c.shared.trx; trx && addr - TrxTables::SineAddr < trx->sine.size())
			return trx->sine[addr - TrxTables::SineAddr];
		if(const auto* trx = _c.shared.trx; trx && addr - TrxTables::ChWavesAddr < trx->chWaves.size())
			return trx->chWaves[addr - TrxTables::ChWavesAddr];
		return hooks.stray ? hooks.stray(hooks.strayUser, _area, addr) & 0xffffff : 0;
	}
}
