// syntakt::fast::tables, the tables BD MODERN reads made by formula, against OS 1.41's section 7 word for word: per
// table, exact or the number of words that differ and the largest error (in LSB of Q31, and of the table's own unit:
// the time constant of a decay, the share of full scale of a level). A table its header calls exact that differs fails.
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
		const bool _exact, const bool _decay)
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
			// A decay's error as its time constant's, -1 / ln(c) of the coefficient -c
			const double unit = _decay
				? std::abs(std::log(-fw[i] / Q31) / std::log(-_made[i] / Q31) - 1)
				: static_cast<double>(e) / Q31;
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
	};
	const Entry parameters[] = {
		{"SweepDepth", 0x4001ec00, Parameter::SweepDepth, false},
		{"SweepDecay", 0x4001ee00, Parameter::SweepDecay, true},
		{"ClickLevel", 0x4001f400, Parameter::ClickLevel, false},
		{"ModDecay", 0x4001f600, Parameter::ModDecay, true},
		{"AmpDecay", 0x4001f800, Parameter::AmpDecay, true},
		{"ModTarget", 0x4001fa00, Parameter::ModTarget, false},
		{"Feedback", 0x4001fc00, Parameter::Feedback, false},
		{"LevelU0", 0x4001fe00, Parameter::LevelU0, false},
		{"LevelU2", 0x40020000, Parameter::LevelU2, false},
	};
	std::printf("BD MODERN's tables by formula against section 7:\n");
	for(const auto& p : parameters)
		compare(s7, p.name, p.address, syntakt::fast::tables::parameter(p.parameter),
			syntakt::fast::tables::exact(p.parameter), p.decay);
	compare(s7, "Wave", 0x4001e7fc, syntakt::fast::tables::wave(), true, false);
	compare(s7, "Exp2", 0x40059ef8, syntakt::fast::tables::exp2(), true, false);

	std::printf(g_failures ? "%d FAILED\n" : "all passed\n", g_failures);
	return g_failures ? 1 : 0;
}
