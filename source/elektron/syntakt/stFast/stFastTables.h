#pragma once

#include <array>
#include <cstdint>

// The tables syntakt::fast::BdModern, SdBasic, CyAlloy, PcCarbon, SdVintage, CpVintage, SyToy and SyBits read,
// made by formula rather than read from the firmware
// (ticket 07 of elektron-drums-v1), as the firmware's Q31 words. stFastTablesTest compares each with OS 1.41's
// section 7; the exact ones were checked with MSVC's runtime, and another libm may round a word 1 LSB apart.
namespace syntakt::fast::tables
{
	// BD MODERN's 128-entry parameter tables (section 7 at 0x4001ec00 to 0x40020000)
	enum class Parameter
	{
		SweepDepth, SweepDecay, ClickLevel, ModDecay, AmpDecay, ModTarget, Feedback, LevelU0, LevelU2, Count
	};

	// Whether the formula gives the firmware's every word; the others are fits, their error printed by the test
	constexpr bool exact(const Parameter _p)
	{
		return _p == Parameter::ModTarget || _p == Parameter::Feedback || _p == Parameter::LevelU2;
	}

	std::array<int32_t, 128> parameter(Parameter _p);

	// SD BASIC's 128-entry tables (section 7 at 0x40026038 to 0x40028238)
	enum class SdBasic
	{
		DecayU0, DecayU1, DecayU2, DecayU3, AmpDecay, ModDecay, AmpAttack, SweepDepth, SweepDecay, Feedback,
		TargetU0, TargetU1, ScaleU0, ScaleU1, ScaleU3, DepthU3, LowPass, Count
	};

	constexpr bool exact(const SdBasic _t)
	{
		switch(_t)
		{
		case SdBasic::DecayU0: case SdBasic::AmpDecay: case SdBasic::Feedback: case SdBasic::TargetU0:
		case SdBasic::TargetU1: case SdBasic::ScaleU0: case SdBasic::ScaleU1: case SdBasic::ScaleU3:
			return true;
		default:
			return false;
		}
	}

	std::array<int32_t, 128> sdBasic(SdBasic _t);

	// CY ALLOY's 128-entry tables (section 7 at 0x40020200 to 0x40020e00)
	enum class CyAlloy
	{
		DecayOdd, Target, DecayEven, Ratio, Spread, Depth, AmpDecay, Count
	};

	constexpr bool exact(const CyAlloy _t)
	{
		return _t == CyAlloy::Target || _t == CyAlloy::Spread || _t == CyAlloy::Depth || _t == CyAlloy::AmpDecay;
	}

	std::array<int32_t, 128> cyAlloy(CyAlloy _t);

	// Units 1 and 3's wave (0x40021000): 257 full words. Approximate
	std::array<int32_t, 257> cyAlloyWave();

	// PC CARBON's 128-entry tables (section 7 at 0x40023004 to 0x40024604)
	enum class PcCarbon
	{
		DecayPosition, SweepDecay, DepthU1, FactorU1, TargetU2, SweepDepth, TargetU0, FactorU2, DecayU2, FactorU0,
		DecayU0, DepthU3, Count
	};

	constexpr bool exact(const PcCarbon _t)
	{
		return _t == PcCarbon::DecayPosition || _t == PcCarbon::TargetU2 || _t == PcCarbon::TargetU0;
	}

	std::array<int32_t, 128> pcCarbon(PcCarbon _t);

	// PC CARBON's decay curve for units 1 and 3 (0x40022004), which it reads at a Q31 value's top 10 bits: 1024
	// words. Exact
	std::array<int32_t, 1024> pcCarbonDecayCurve();

	// PC CARBON's unit 0 wave (0x40024804): 257 full words. Approximate
	std::array<int32_t, 257> pcCarbonWave();

	// SD VINTAGE's 128-entry tables (section 7 at 0x40038438 to 0x4003a238): every one exact
	enum class SdVintage
	{
		Ratio0, Ratio1, SweepDecay, SweepDepth, Depth2, Depth1, Depth0, NoiseLp, Level3, Level2, Level1, Feedback,
		ModDecay, Decay2, Decay01, Count
	};

	constexpr bool exact(SdVintage) { return true; }

	std::array<int32_t, 128> sdVintage(SdVintage _t);

	// SD VINTAGE's unit 3 decay (0x40028438), entry DECAY << 7 | the fifth parameter, both bytes: 16384 words. Exact
	std::array<int32_t, 128 * 128> sdVintageDecay3();

	// SD VINTAGE's units 2 and 3 waves (0x40059af4 and 0x400596f0, which the boot copies to RAM 0x8000a484 and
	// 0x8000a080): 257 words each. Approximate: entry 128, about 2e-9 of full scale, is 1 LSB off
	std::array<int32_t, 257> sdVintageWaveU2();
	std::array<int32_t, 257> sdVintageWaveU3();

	// CP VINTAGE's 128-entry tables (section 7 at 0x4003a238 to 0x4003bc38, but HOLD's periods at 0x4003a638, which
	// it never reads: its HOLD's period runs on the clock step MoDule leaves at 0): every one exact
	enum class CpVintage
	{
		ModDecay, ToneDecay, SampleRate, Period, RepeatDecay, ClapLevel, ToneLevel, ToneDepth, ToneFeedback,
		ToneRatio, ClapDecay, CrackleDecay, HighPass, Count
	};

	constexpr bool exact(CpVintage) { return true; }

	std::array<int32_t, 128> cpVintage(CpVintage _t);

	// CP VINTAGE's resonator, by eighths of a semitone (0x40014b80 and 0x40015b80): -cos of the frequency and the
	// allpass's second coefficient (_radius), 1024 words each. Exact
	std::array<int32_t, 1024> cpVintageResonator(bool _radius);

	// The voice CPU's log2 of a 10-bit mantissa (0x4000f48c, its entries 1024-2047) and its exp2 (0x4000e488), which
	// CP VINTAGE's resonator computes its square roots with: 1024 and 1025 words. Exact
	std::array<int32_t, 1024> cpVintageLog2();
	std::array<int32_t, 1025> cpVintageExp2();

	// SY TOY's 128-entry tables, in section 7's order (0x4003c038 to 0x4003dc38): every one exact
	enum class SyToy
	{
		Target, DecayU4, DecayU3, DecayU2, DecayU1, Gain3, Gain2, Gain1, Depth, Feedback, RatioU4, RatioU3, RatioU2,
		RatioU1, AmpDecay, Count
	};

	constexpr bool exact(SyToy) { return true; }

	std::array<int32_t, 128> syToy(SyToy _t);

	// SY BITS's 128-entry tables, in section 7's order (0x4003de38 to 0x4003ec38): every one exact. AmpDecay
	// (0x4003ec38) is also SY SWARM's and SP TWINSHOT's
	enum class SyBits
	{
		PhaseOffset, Bits, SampleRate, Crossfade, GainNoise, GainB, GainA, AmpDecay, Count
	};

	constexpr bool exact(SyBits) { return true; }

	std::array<int32_t, 128> syBits(SyBits _t);

	// SY BITS's waves, 513 words each (a cycle and its first word again): _n 0 to 7 the band-limited saws its notes
	// take (at or below 35, then by 12 from 36, from 108), 8 the triangle, 9 the sine (section 7's 0x4005c6fc,
	// 0x4005bef8, 0x40056e68 down by 0x804 to 0x40054654, 0x4005cf00 and 0x4005d704). Exact
	std::array<int32_t, 513> syBitsWave(int _n);

	// Unit 2's wave (0x4001e7fc): 257 words, 16-bit samples in the top half. Exact
	std::array<int32_t, 257> wave();

	// The pitch map's exp2 (0x40059ef8, which the boot copies to RAM 0x8000a888): 2048 words. Exact
	std::array<int32_t, 2048> exp2();
}
