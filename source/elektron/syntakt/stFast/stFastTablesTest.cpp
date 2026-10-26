// syntakt::fast::tables, the tables BD MODERN and SD BASIC read made by formula, against OS 1.41's section 7 word for
// word: per table, exact or the number of words that differ and the largest error (in LSB of Q31, and of the table's
// own unit: the time constant of a decay, the share of full scale of a level). A table its header calls exact that
// differs fails.
//
// SYNTAKT_SECTION7 names the user's OS 1.41 section 7. Exit 77 without it.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "stFastTables.h"
#include "stSection7.h"

namespace
{
	using syntakt::fast::tables::Parameter;

	constexpr uint32_t Base = 0x40000400;	// section 7's first word
	constexpr double Q31 = 2147483648.0;

	int g_failures = 0;

	template<typename Table>
	void compare(const std::vector<int32_t>& _s7, const char* _name, const uint32_t _address, const Table& _made,
		const bool _exact, const bool _decay, const int64_t _maxLsb)
	{
		const int32_t* fw = &_s7[(_address - Base) >> 2];
		int differ = 0;
		int64_t worst = 0;
		double worstUnit = 0;
		std::string where;
		for(size_t i = 0; i < _made.size(); ++i)
		{
			const int64_t e = std::abs(static_cast<int64_t>(_made[i]) - fw[i]);
			if(!e)
				continue;
			++differ;
			worst = std::max(worst, e);
			// A decay's error as its time constant's, -1 / ln(c) of the coefficient -c (none for a coefficient of 0,
			// whose time constant is 0 as -1 LSB's nearly is)
			const double unit = !_decay ? static_cast<double>(e) / Q31
				: fw[i] < 0 && _made[i] < 0 ? std::abs(std::log(-fw[i] / Q31) / std::log(-_made[i] / Q31) - 1) : 0;
			worstUnit = std::max(worstUnit, unit);
			if(differ <= 12)
				where += " " + std::to_string(i);
		}
		if(!differ)
			std::printf("  %-11s %4zu words: exact\n", _name, _made.size());
		else
			std::printf("  %-11s %4zu words: %3d differ, max %lld LSB (%.3g %s), at%s%s\n", _name, _made.size(), differ,
				static_cast<long long>(worst), _decay ? 100 * worstUnit : worstUnit,
				_decay ? "% of the time constant" : "of full scale", where.c_str(), differ > 12 ? " ..." : "");
		if(_exact && differ)
		{
			std::printf("FAIL: %s is said exact\n", _name);
			++g_failures;
		}
		if(worst > _maxLsb)
		{
			std::printf("FAIL: %s beyond its largest error, %lld LSB\n", _name, static_cast<long long>(_maxLsb));
			++g_failures;
		}
	}
}

int main()
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	const char* path = std::getenv("SYNTAKT_SECTION7");
	if(!path || !*path)
	{
		std::printf("SYNTAKT_SECTION7 not set: skipped\n");
		return 77;
	}
	const auto s7 = syntakt::loadSection7(path);
	if(s7.empty())
	{
		std::printf("%s is not OS 1.41's section 7: skipped\n", path);
		return 77;
	}

	struct Entry
	{
		const char* name;
		uint32_t address;
		Parameter parameter;
		bool decay;
		int64_t maxLsb;	// the largest error its formula's comment states, in LSB, with a margin
	};
	const Entry parameters[] = {
		{"SweepDepth", 0x4001ec00, Parameter::SweepDepth, false, 2},
		{"SweepDecay", 0x4001ee00, Parameter::SweepDecay, true, 1100000},
		{"ClickLevel", 0x4001f400, Parameter::ClickLevel, false, 700000},
		{"ModDecay", 0x4001f600, Parameter::ModDecay, true, 26000},
		{"AmpDecay", 0x4001f800, Parameter::AmpDecay, true, 6},
		{"ModTarget", 0x4001fa00, Parameter::ModTarget, false, 0},
		{"Feedback", 0x4001fc00, Parameter::Feedback, false, 0},
		{"LevelU0", 0x4001fe00, Parameter::LevelU0, false, 160000},
		{"LevelU2", 0x40020000, Parameter::LevelU2, false, 0},
	};
	std::printf("BD MODERN's tables by formula against section 7:\n");
	for(const auto& p : parameters)
		compare(s7, p.name, p.address, syntakt::fast::tables::parameter(p.parameter),
			syntakt::fast::tables::exact(p.parameter), p.decay, p.maxLsb);
	compare(s7, "Wave", 0x4001e7fc, syntakt::fast::tables::wave(), true, false, 0);
	compare(s7, "Exp2", 0x40059ef8, syntakt::fast::tables::exp2(), true, false, 0);

	using syntakt::fast::tables::SdBasic;
	struct SdEntry
	{
		const char* name;
		uint32_t address;
		SdBasic table;
		bool decay;
		int64_t maxLsb;
	};
	const SdEntry sdBasic[] = {
		{"DecayU0", 0x40027a38, SdBasic::DecayU0, true, 0},
		{"DecayU1", 0x40028038, SdBasic::DecayU1, true, 200},
		{"DecayU2", 0x40026038, SdBasic::DecayU2, true, 600000},
		{"DecayU3", 0x40026a38, SdBasic::DecayU3, true, 2},
		{"AmpDecay", 0x40026838, SdBasic::AmpDecay, true, 0},
		{"ModDecay", 0x40028238, SdBasic::ModDecay, true, 1},
		{"AmpAttack", 0x40026c38, SdBasic::AmpAttack, false, 35000},
		{"SweepDepth", 0x40026638, SdBasic::SweepDepth, false, 250},
		{"SweepDecay", 0x40026438, SdBasic::SweepDecay, true, 30},
		{"Feedback", 0x40026e38, SdBasic::Feedback, false, 0},
		{"TargetU0", 0x40027838, SdBasic::TargetU0, false, 0},
		{"TargetU1", 0x40027e38, SdBasic::TargetU1, false, 0},
		{"ScaleU0", 0x40027238, SdBasic::ScaleU0, false, 0},
		{"ScaleU1", 0x40027c38, SdBasic::ScaleU1, false, 0},
		{"ScaleU3", 0x40027038, SdBasic::ScaleU3, false, 0},
		{"DepthU3", 0x40027438, SdBasic::DepthU3, false, 130000},
		{"LowPass", 0x40026238, SdBasic::LowPass, false, 25000},
	};
	std::printf("SD BASIC's tables by formula against section 7:\n");
	for(const auto& t : sdBasic)
		compare(s7, t.name, t.address, syntakt::fast::tables::sdBasic(t.table), syntakt::fast::tables::exact(t.table),
			t.decay, t.maxLsb);

	std::printf(g_failures ? "%d FAILED\n" : "all passed\n", g_failures);
	return g_failures ? 1 : 0;
}
