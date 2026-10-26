#pragma once

#include <array>
#include <cstdint>

// The tables syntakt::fast::BdModern and SdBasic read, made by formula rather than read from the firmware (ticket 07 of
// elektron-drums-v1), as the firmware's Q31 words. stFastTablesTest compares each with OS 1.41's section 7; the exact
// ones were checked with MSVC's runtime, and another libm may round a word 1 LSB apart.
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

	// Unit 2's wave (0x4001e7fc): 257 words, 16-bit samples in the top half. Exact
	std::array<int32_t, 257> wave();

	// The pitch map's exp2 (0x40059ef8, which the boot copies to RAM 0x8000a888): 2048 words. Exact
	std::array<int32_t, 2048> exp2();
}
