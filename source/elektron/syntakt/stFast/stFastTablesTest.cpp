// syntakt::fast::tables, the tables BD MODERN, SD BASIC, CY ALLOY, PC CARBON, SD VINTAGE and CP VINTAGE read made by
// formula, against OS 1.41's section 7 word for word: per table, exact or the number of words that differ and the
// largest error (in LSB of Q31, and of the table's own unit: the time constant of a decay, the share of full scale of
// a level). A table its header calls exact that differs fails.
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

	// The largest error entries _from to _to may have, where a table's stretches differ much (a bound per stretch)
	struct Bound
	{
		size_t from, to;
		int64_t maxLsb;
	};

	template<typename Table>
	void compare(const std::vector<int32_t>& _s7, const char* _name, const uint32_t _address, const Table& _made,
		const bool _exact, const bool _decay, const int64_t _maxLsb, const std::vector<Bound>& _ranges = {})
	{
		// Each range's worst against its own bound, the rest against _maxLsb
		const int32_t* fw = &_s7[(_address - Base) >> 2];
		for(const Bound& b : _ranges)
		{
			int64_t worst = 0;
			for(size_t i = b.from; i <= b.to; ++i)
				worst = std::max(worst, std::abs(static_cast<int64_t>(_made[i]) - fw[i]));
			std::printf("  %-11s %4zu-%-4zu max %lld LSB (bound %lld)\n", _name, b.from, b.to,
				static_cast<long long>(worst), static_cast<long long>(b.maxLsb));
			if(worst > b.maxLsb)
			{
				std::printf("FAIL: %s %zu-%zu beyond its largest error\n", _name, b.from, b.to);
				++g_failures;
			}
		}
		const auto ranged = [&](const size_t _i)
		{
			return std::any_of(_ranges.begin(), _ranges.end(),
				[&](const Bound& _b) { return _i >= _b.from && _i <= _b.to; });
		};
		int64_t worstOutside = 0;
		for(size_t i = 0; i < _made.size(); ++i)
			if(!ranged(i))
				worstOutside = std::max(worstOutside, std::abs(static_cast<int64_t>(_made[i]) - fw[i]));

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
		if(worstOutside > _maxLsb)
		{
			std::printf("FAIL: %s beyond its largest error, %lld LSB\n", _name, static_cast<long long>(_maxLsb));
			++g_failures;
		}
	}

	// A table of a machine's enum Table, where it is in section 7, and how far its formula may be
	template<typename Table>
	struct Entry
	{
		const char* name;
		uint32_t address;
		Table table;
		bool decay;
		int64_t maxLsb;	// the largest error its formula's comment states, in LSB, with a margin
		std::vector<Bound> ranges;	// stretches bound apart
	};

	// A machine's tables, each made by _make(table), against section 7
	template<typename Table, size_t N, typename Make>
	void compareAll(const std::vector<int32_t>& _s7, const char* _machine, const Entry<Table> (&_entries)[N],
		Make&& _make)
	{
		std::printf("%s's tables by formula against section 7:\n", _machine);
		for(const auto& e : _entries)
			compare(_s7, e.name, e.address, _make(e.table), syntakt::fast::tables::exact(e.table), e.decay, e.maxLsb,
				e.ranges);
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

	const Entry<Parameter> parameters[] = {
		{"SweepDepth", 0x4001ec00, Parameter::SweepDepth, false, 2},
		{"SweepDecay", 0x4001ee00, Parameter::SweepDecay, true, 2, {{28, 32, 8}, {38, 42, 27000}, {48, 52, 30500},
			{58, 62, 17500}, {68, 72, 15000}, {78, 82, 18000}, {88, 92, 18200}, {98, 102, 22500},
			{108, 112, 31500}}},	// the knots' joins apart, every other entry within 2 LSB
		{"ClickLevel", 0x4001f400, Parameter::ClickLevel, false, 700000, {{32, 32, 2400000}, {96, 96, 1200000}}},
		{"ModDecay", 0x4001f600, Parameter::ModDecay, true, 26000},
		{"AmpDecay", 0x4001f800, Parameter::AmpDecay, true, 6},
		{"ModTarget", 0x4001fa00, Parameter::ModTarget, false, 0},
		{"Feedback", 0x4001fc00, Parameter::Feedback, false, 0},
		{"LevelU0", 0x4001fe00, Parameter::LevelU0, false, 160000},
		{"LevelU2", 0x40020000, Parameter::LevelU2, false, 0},
	};
	compareAll(s7, "BD MODERN", parameters, syntakt::fast::tables::parameter);
	compare(s7, "Wave", 0x4001e7fc, syntakt::fast::tables::wave(), true, false, 0);
	compare(s7, "Exp2", 0x40059ef8, syntakt::fast::tables::exp2(), true, false, 0);

	using syntakt::fast::tables::SdBasic;
	const Entry<SdBasic> sdBasic[] = {
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
	compareAll(s7, "SD BASIC", sdBasic, syntakt::fast::tables::sdBasic);

	using syntakt::fast::tables::CyAlloy;
	const Entry<CyAlloy> cyAlloy[] = {
		{"DecayOdd", 0x40020200, CyAlloy::DecayOdd, true, 4200},
		{"Target", 0x40020400, CyAlloy::Target, false, 0},
		{"DecayEven", 0x40020600, CyAlloy::DecayEven, true, 7700},
		{"Ratio", 0x40020800, CyAlloy::Ratio, false, 0, {{68, 72, 250}}},
		{"Spread", 0x40020a00, CyAlloy::Spread, false, 0},
		{"Depth", 0x40020c00, CyAlloy::Depth, false, 0},
		{"AmpDecay", 0x40020e00, CyAlloy::AmpDecay, true, 0},
	};
	compareAll(s7, "CY ALLOY", cyAlloy, syntakt::fast::tables::cyAlloy);
	compare(s7, "Wave", 0x40021000, syntakt::fast::tables::cyAlloyWave(), false, false, 7000);

	using syntakt::fast::tables::PcCarbon;
	const Entry<PcCarbon> pcCarbon[] = {
		{"DecayPos", 0x40023004, PcCarbon::DecayPosition, false, 0},
		{"SweepDecay", 0x40023204, PcCarbon::SweepDecay, true, 1300},
		{"DepthU1", 0x40023404, PcCarbon::DepthU1, false, 600},
		{"FactorU1", 0x40023604, PcCarbon::FactorU1, false, 0, {{43, 43, 3500}, {53, 53, 7000}, {87, 87, 13000},
			{97, 97, 10000}, {107, 107, 6500}, {117, 117, 3500}}},
		{"TargetU2", 0x40023804, PcCarbon::TargetU2, false, 0},
		{"SweepDepth", 0x40023a04, PcCarbon::SweepDepth, false, 40, {{63, 65, 0}, {94, 97, 17000}, {111, 114, 17000}}},
		{"TargetU0", 0x40023c04, PcCarbon::TargetU0, false, 0},
		{"FactorU2", 0x40023e04, PcCarbon::FactorU2, false, 0, {{29, 31, 0}, {47, 49, 0}, {60, 60, 12000},
			{71, 71, 2500}}},
		{"DecayU2", 0x40024004, PcCarbon::DecayU2, true, 1400},
		{"FactorU0", 0x40024204, PcCarbon::FactorU0, false, 70, {{81, 81, 7000}, {85, 85, 6000}}},
		{"DecayU0", 0x40024404, PcCarbon::DecayU0, true, 750},
		{"DepthU3", 0x40024604, PcCarbon::DepthU3, false, 600},
	};
	compareAll(s7, "PC CARBON", pcCarbon, syntakt::fast::tables::pcCarbon);
	compare(s7, "DecayCurve", 0x40022004, syntakt::fast::tables::pcCarbonDecayCurve(), true, true, 0);
	compare(s7, "Wave", 0x40024804, syntakt::fast::tables::pcCarbonWave(), false, false, 3500);

	using syntakt::fast::tables::SdVintage;
	const Entry<SdVintage> sdVintage[] = {
		{"Ratio0", 0x40038438, SdVintage::Ratio0, false, 0},
		{"Ratio1", 0x40038638, SdVintage::Ratio1, false, 0},
		{"SweepDecay", 0x40038838, SdVintage::SweepDecay, true, 0},
		{"SweepDepth", 0x40038a38, SdVintage::SweepDepth, false, 0},
		{"Depth2", 0x40038c38, SdVintage::Depth2, false, 0},
		{"Depth1", 0x40038e38, SdVintage::Depth1, false, 0},
		{"Depth0", 0x40039038, SdVintage::Depth0, false, 0},
		{"NoiseLp", 0x40039238, SdVintage::NoiseLp, false, 0},
		{"Level3", 0x40039438, SdVintage::Level3, false, 0},
		{"Level2", 0x40039638, SdVintage::Level2, false, 0},
		{"Level1", 0x40039838, SdVintage::Level1, false, 0},
		{"Feedback", 0x40039a38, SdVintage::Feedback, false, 0},
		{"ModDecay", 0x40039c38, SdVintage::ModDecay, true, 0},
		{"Decay2", 0x40039e38, SdVintage::Decay2, true, 0},
		{"Decay01", 0x4003a038, SdVintage::Decay01, true, 0},
	};
	compareAll(s7, "SD VINTAGE", sdVintage, syntakt::fast::tables::sdVintage);
	compare(s7, "Decay3", 0x40028438, syntakt::fast::tables::sdVintageDecay3(), true, true, 0);
	// The waves' entry 128, the firmware's -1 where the law gives 0, bound apart; every other word exact
	compare(s7, "WaveU2", 0x40059af4, syntakt::fast::tables::sdVintageWaveU2(), false, false, 0, {{128, 128, 1}});
	compare(s7, "WaveU3", 0x400596f0, syntakt::fast::tables::sdVintageWaveU3(), false, false, 0, {{128, 128, 1}});

	// CP VINTAGE's, each stretch of a law bound apart, every word exact
	using syntakt::fast::tables::CpVintage;
	const std::vector<Bound> halves = {{0, 64, 0}, {65, 127, 0}};
	const Entry<CpVintage> cpVintage[] = {
		{"ModDecay", 0x4003a238, CpVintage::ModDecay, true, 0},
		{"ToneDecay", 0x4003a438, CpVintage::ToneDecay, true, 0},
		{"SampleRate", 0x4003a838, CpVintage::SampleRate, false, 0, halves},
		{"Period", 0x4003aa38, CpVintage::Period, false, 0, halves},
		{"RepeatDecay", 0x4003ac38, CpVintage::RepeatDecay, true, 0, halves},
		{"ClapLevel", 0x4003ae38, CpVintage::ClapLevel, false, 0, {{0, 63, 0}, {64, 127, 0}}},
		{"ToneLevel", 0x4003b038, CpVintage::ToneLevel, false, 0, {{0, 63, 0}, {64, 127, 0}}},
		{"ToneDepth", 0x4003b238, CpVintage::ToneDepth, false, 0},
		{"ToneFeedback", 0x4003b438, CpVintage::ToneFeedback, false, 0, {{0, 125, 0}, {126, 127, 0}}},
		{"ToneRatio", 0x4003b638, CpVintage::ToneRatio, false, 0},
		{"ClapDecay", 0x4003b838, CpVintage::ClapDecay, true, 0, halves},
		{"CrackleDecay", 0x4003ba38, CpVintage::CrackleDecay, true, 0, halves},
		{"HighPass", 0x4003bc38, CpVintage::HighPass, false, 0, {{0, 22, 0}, {23, 95, 0}, {96, 127, 0}}},
	};
	compareAll(s7, "CP VINTAGE", cpVintage, syntakt::fast::tables::cpVintage);
	compare(s7, "ResonatorA", 0x40014b80, syntakt::fast::tables::cpVintageResonator(false), true, false, 0);
	compare(s7, "ResonatorB", 0x40015b80, syntakt::fast::tables::cpVintageResonator(true), true, false, 0);
	compare(s7, "Log2", 0x4000f48c, syntakt::fast::tables::cpVintageLog2(), true, false, 0);
	compare(s7, "Exp2Half", 0x4000e488, syntakt::fast::tables::cpVintageExp2(), true, false, 0);

	std::printf(g_failures ? "%d FAILED\n" : "all passed\n", g_failures);
	return g_failures ? 1 : 0;
}
