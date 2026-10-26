#include "stFastTables.h"

#include <algorithm>
#include <cmath>
#include <iterator>

// The parameter tables are designed curves: round knots (SweepDecay, ModDecay, ClickLevel) and long stretches of a
// line or an exponential, each in the table's own unit, joined at corners of one to three entries that follow
// neither. The stretches' constants are fitted to the firmware's words (LP on each word's rounding interval, by
// scripts kept outside the repository); a corner takes the nearer stretch, extended, and is approximate. The units:
// - SweepDepth: the Q31 word itself
// - the decays (SweepDecay, ModDecay, AmpDecay): the time constant tau in 48 kHz blocks of the per-block coefficient
//   -exp(-1/tau)
// - the levels (ModTarget, LevelU0, LevelU2, Feedback, ClickLevel): a value with six decimals, as printed by the tool
//   that made them, then scaled to Q31 (Feedback by 1/4, ClickLevel by its own gain)
namespace syntakt::fast::tables
{
	namespace
	{
		constexpr double Q31 = 2147483648.0;

		enum class Kind
		{
			Linear,		// p0 + p1 t
			Exp,		// p0 + p1 exp(p2 t)
			Poly		// p0 + p1 t + p2 t^2 + p3 t^3
		};

		// Entries _from to _to, t = i - _origin (a corner's stretch starts before its origin)
		struct Segment
		{
			int from, to, origin;
			Kind kind;
			double p[4];

			double at(const int _i) const
			{
				const double t = _i - origin;
				switch(kind)
				{
				case Kind::Linear:	return p[0] + p[1] * t;
				case Kind::Exp:		return p[0] + p[1] * std::exp(p[2] * t);
				case Kind::Poly:	return p[0] + t * (p[1] + t * (p[2] + t * p[3]));
				}
				return 0;
			}
		};

		enum class Unit
		{
			Word,	// the Q31 word
			Tau,	// -exp(-1 / tau), tau = y times a scale
			Micro	// round(y) millionths, times a scale in words per millionth
		};

		int32_t clamp(const double _v)
		{
			return static_cast<int32_t>(std::clamp(static_cast<double>(std::llround(_v)), -Q31, Q31 - 1));
		}

		int32_t toWord(const Unit _unit, const double _scale, const double _y)
		{
			switch(_unit)
			{
			case Unit::Word:	return clamp(_y);
			case Unit::Tau:		return clamp(-std::exp(-1 / (_scale * _y)) * Q31);
			case Unit::Micro:	return clamp(std::round(_y) * _scale);
			}
			return 0;
		}

		// Approximate: 122 of 128, 1 LSB off where the last stretch's rounding wavers
		const Segment SweepDepth[] = {
			{0, 10, 0, Kind::Linear, {67108864.083333358, 16434823.833333325}},
			{11, 20, 11, Kind::Linear, {239674514, 8217412}},
			{21, 30, 21, Kind::Linear, {317739927.55555552, 4108705.8888888834}},
			{31, 40, 31, Kind::Linear, {362250908.85714293, 7532627.5714285495}},
			{41, 50, 41, Kind::Linear, {426620635.4375, -3423921.6250000009}},
			{51, 60, 51, Kind::Linear, {399914047.39999992, 4108705.8000000119}},
			{61, 70, 61, Kind::Linear, {438261969.00000006, 1369568.6666666507}},
			{71, 80, 71, Kind::Linear, {455381577.14285713, 4793490.2857143283}},
			{81, 90, 81, Kind::Linear, {507425185.87499946, 8902196.2500000838}},
			{91, 100, 91, Kind::Linear, {597131932.60000002, 9586980.5999999996}},
			{101, 110, 101, Kind::Linear, {717653974.42857134, 34239216.285714246}},
			{111, 127, 111, Kind::Linear, {1049975779.6666669, 24168858.583333321}},
		};

		// The decays' time constants are KnotTau times a designer's value: SweepDecay's knots every 10 entries (and
		// 127) are round, 12 12 18 25 30 46 52 64 48 40 25.4 19.2 15, and so are ModDecay's plateaus, 400 and 3.
		// Between the knots, SweepDecay is the knots' chord up to 30 and from 110; between 30 and 110 each interval's
		// inside is a line of its own (fitted, through a knot or a round value: 39.6 at 46, 60.94 at 67), joined to
		// the knots by 1 or 2 entries that follow neither (approximate, up to 1.24 % of the time constant). Knot
		// entries are within 1 LSB: the knots disagree with a single KnotTau by about 1e-8
		constexpr double KnotTau = 1.15142326;

		const Segment SweepDecay[] = {
			{0, 10, 0, Kind::Linear, {12, 0}},
			{11, 20, 10, Kind::Linear, {12, 0.6}},
			{21, 30, 20, Kind::Linear, {18, 0.7}},
			{31, 39, 31, Kind::Linear, {25.503657758779177, 0.50365800466212574}},
			{40, 40, 40, Kind::Linear, {30, 0}},
			{41, 49, 43, Kind::Linear, {34.48268120115155, 1.7057955911802209}},
			{50, 50, 50, Kind::Linear, {46, 0}},
			{51, 59, 53, Kind::Linear, {48.203418275248055, 0.54236865371648402}},
			{60, 60, 60, Kind::Linear, {52, 0}},
			{61, 69, 63, Kind::Linear, {55.736370301242687, 1.3008683796632627}},
			{70, 70, 70, Kind::Linear, {64, 0}},
			{71, 79, 73, Kind::Linear, {59.795329522859888, -1.8428967132182239}},
			{80, 80, 80, Kind::Linear, {48, 0}},
			{81, 89, 81, Kind::Linear, {47.177832167936423, -0.82216709870214644}},
			{90, 90, 90, Kind::Linear, {40, 0}},
			{91, 99, 91, Kind::Linear, {38.447056885650355, -1.5529428352809205}},
			{100, 100, 100, Kind::Linear, {25.4, 0}},
			{101, 109, 101, Kind::Linear, {24.715673941122869, -0.68432592593524122}},
			{110, 127, 110, Kind::Linear, {19.2, -4.2 / 17}},
		};

		// Approximate: knots 0.4, 0.8, 1.0 at 32, 64, 96 and 1.0 at 127, built as SweepDecay is: each interval's
		// inside a line of its own, 31 and 33 following neither (0.03 % of full scale)
		const Segment ClickLevel[] = {
			{0, 31, 0, Kind::Linear, {150000, 7723}},
			{32, 32, 32, Kind::Linear, {400000, 0}},
			{33, 63, 34, Kind::Linear, {427478.48076923063, 12490.423076923129}},
			{64, 95, 64, Kind::Linear, {800000.02777777787, 6287.0555555555484}},
			{96, 96, 96, Kind::Linear, {1000000, 0}},
			{97, 127, 99, Kind::Linear, {1002401.6153846163, -85.769230769252985}},
		};
		constexpr double ClickGain = 921.0481118557876;	// words per millionth, fitted

		// Approximate, in KnotTau: plateaus 400 and 3, a line and two exponentials between, corners at 31-33,
		// 107-109 and 118-120 (time constant 0.5 % off)
		const Segment ModDecay[] = {
			{0, 31, 0, Kind::Linear, {400, 0}},
			{32, 60, 34, Kind::Linear, {380.89376306344707, -9.363018616638854}},
			{61, 72, 61, Kind::Exp, {76.348506161100815, 52.934234693985964, -0.14251505402330666}},
			{73, 119, 73, Kind::Exp, {255.51575598414087, -169.60199400135556, 0.008468492889752809}},
			{120, 127, 120, Kind::Linear, {3, 0}},
		};

		// Approximate: one exponential, 125 of 128 (entries 3, 22 and 39 up to 5 LSB off)
		const Segment AmpDecay[] = {
			{0, 127, 0, Kind::Exp, {-195.92707915695189, 206.35014699180758, 0.022184752523281899}},
		};

		const Segment ModTarget[] = {
			{0, 27, 0, Kind::Linear, {999999.96875000023, -20240.812500000018}},
			{28, 40, 28, Kind::Exp, {290694.58051467169, 143090.69297947583, -0.11704576788611416}},
			{41, 70, 41, Kind::Exp, {140461.9409592131, 181455.27409979308, -0.020865448880465355}},
			{71, 114, 71, Kind::Linear, {237743.86206896536, -1750.7241379310256}},
			{115, 127, 115, Kind::Exp, {170734.46405409821, -10019.053147004353, 0.18809092107438372}},
		};

		const Segment Feedback[] = {
			{0, 32, 0, Kind::Linear, {-110000, 0}},
			{33, 87, 33, Kind::Exp, {-100341.15409257269, -10163.687184214215, 0.050943462880931781}},
			{88, 124, 88, Kind::Exp, {-511529.20316496375, 243872.93473981434, -0.033160221523988016}},
			{125, 127, 125, Kind::Linear, {-440000, 0}},
		};

		// Approximate: 88-115 steps in threes that no smooth curve follows, a cubic there (0.12 % of full scale)
		const Segment LevelU0[] = {
			{0, 79, 0, Kind::Exp, {46025.872943924325, -46025.91824317912, 0.018756423691816008}},
			{80, 87, 80, Kind::Exp, {-187462.60220990109, 27131.002167140174, -0.13567758998665569}},
			{88, 115, 88, Kind::Poly,
				{-175976.24741500936, -473.56077033803632, 89.16357662648079, 1.0283439231022806}},
			{116, 127, 116, Kind::Linear, {-98966.625, 6110.7500000000018}},
		};

		const Segment LevelU2[] = {
			{0, 127, 0, Kind::Exp, {-162679.45091294113, 162679.45369267819, 0.011741606585722399}},
		};

		struct Curve
		{
			const Segment* begin;
			const Segment* end;
			Unit unit;
			double scale;
		};

		template<size_t N> Curve curve(const Segment (&_s)[N], const Unit _unit, const double _scale = 0)
		{
			return {_s, _s + N, _unit, _scale};
		}
	}

	std::array<int32_t, 128> parameter(const Parameter _p)
	{
		constexpr double Micro = Q31 / 1e6;
		const Curve curves[] = {
			curve(SweepDepth, Unit::Word), curve(SweepDecay, Unit::Tau, KnotTau),
			curve(ClickLevel, Unit::Micro, ClickGain), curve(ModDecay, Unit::Tau, KnotTau),
			curve(AmpDecay, Unit::Tau, 1), curve(ModTarget, Unit::Micro, Micro),
			curve(Feedback, Unit::Micro, Micro / 4), curve(LevelU0, Unit::Micro, Micro),
			curve(LevelU2, Unit::Micro, Micro),
		};
		static_assert(std::size(curves) == static_cast<size_t>(Parameter::Count));

		const Curve& c = curves[static_cast<int>(_p)];
		std::array<int32_t, 128> t{};
		for(const Segment* s = c.begin; s != c.end; ++s)
			for(int i = s->from; i <= s->to; ++i)
				t[i] = toWord(c.unit, c.scale, s->at(i));
		return t;
	}

	std::array<int32_t, 257> wave()
	{
		// floor of five harmonics: sines 128 : 22 : 11 : 7 : 3 and cosines ~1e-6 of the fundamental, the generator's
		// own rounding, without which 6 samples miss by 1
		constexpr double Sin[5] = {24528.0552, 4215.756863, 2107.902651, 1341.393287, 574.8762462};
		constexpr double Cos[5] = {0.01511846303, -0.007283459111, -0.009724665057, 0.007283459111, 0.008834185068};
		std::array<int32_t, 257> t{};
		for(int i = 0; i < 257; ++i)
		{
			const double x = 2 * 3.14159265358979323846 * i / 256;
			double v = 0;
			for(int k = 0; k < 5; ++k)
				v += Sin[k] * std::sin((k + 1) * x) + Cos[k] * std::cos((k + 1) * x);
			t[i] = static_cast<int32_t>(static_cast<uint32_t>(static_cast<int32_t>(std::floor(v))) << 16);
		}
		return t;
	}

	std::array<int32_t, 2048> exp2()
	{
		// 2^(i / 2047) / 2 in Q31, the last saturated (research 18, section 1.6)
		std::array<int32_t, 2048> t{};
		for(int i = 0; i < 2048; ++i)
			t[i] = clamp(std::exp2(i / 2047.0) * 1073741824.0);
		return t;
	}
}
