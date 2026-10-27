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
// SD BASIC's add two units: an attack's blocks to full (its step -1 / blocks), and a decay's 60 dB fall in seconds,
// the law of its units 0-2, its amp and its modulator. Its levels with six decimals are 4 times the feedback, 1.1
// times unit 0's target, unit 1's target itself
namespace syntakt::fast::tables
{
	namespace
	{
		constexpr double Q31 = 2147483648.0;

		enum class Kind
		{
			Linear,		// p0 + p1 t
			Exp,		// p0 + p1 exp(p2 t)
			Poly,		// p0 + p1 t + p2 t^2 + p3 t^3
			Power,		// p0 ((t + p1) / p2)^p3
			Poly5		// p0 + p1 x + ... + p5 x^5, x = t / 127
		};

		// Entries _from to _to, t = i - _origin (a corner's stretch starts before its origin)
		struct Segment
		{
			int from, to, origin;
			Kind kind;
			double p[6];

			double at(const int _i) const
			{
				const double t = _i - origin;
				const double x = t / 127;
				switch(kind)
				{
				case Kind::Linear:	return p[0] + p[1] * t;
				case Kind::Exp:		return p[0] + p[1] * std::exp(p[2] * t);
				case Kind::Poly:	return p[0] + t * (p[1] + t * (p[2] + t * p[3]));
				case Kind::Power:	return p[0] * std::pow((t + p[1]) / p[2], p[3]);
				case Kind::Poly5:	return p[0] + x * (p[1] + x * (p[2] + x * (p[3] + x * (p[4] + x * p[5]))));
				}
				return 0;
			}
		};

		enum class Unit
		{
			Word,	// the Q31 word
			Tau,	// -exp(-1 / tau), tau = y times a scale
			Micro,	// round(y) millionths, times a scale in words per millionth
			Rate,	// -1 / (scale y): an attack's step per block, y the blocks it takes to full
			T60		// the decay per block that falls 60 dB in scale y seconds (a 48 kHz block: 1/1500 s)
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
			case Unit::Rate:	return clamp(-Q31 / (_scale * _y));
			case Unit::T60:
				{
					// The exponent e of the coefficient -exp(e), with the generator's own rounding: e^2 10^-6 /
					// (2 ln 10) more (fitted: off it, words differ by up to 250 LSB where e is large). A coefficient
					// rounds to 1 LSB at least
					const double e = -std::log(1000.0) / (1500 * _scale * _y);
					const double c = std::exp(e + e * e * 1e-6 / (2 * std::log(10.0)));
					return -std::max(clamp(c * Q31), 1);
				}
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

		// Approximate: 88-127 steps in twos and threes that no smooth curve follows, five fitted stretches and a line
		// there (6.9e-5 of full scale)
		const Segment LevelU0[] = {
			{0, 79, 0, Kind::Exp, {46025.872943924325, -46025.91824317912, 0.018756423691816008}},
			{80, 87, 80, Kind::Exp, {-187462.60220990109, 27131.002167140174, -0.13567758998665569}},
			{88, 92, 88, Kind::Poly, {-177186.12495809793, 11.666600281950952, -174.2499880492444, 55.83333161969603}},
			{93, 101, 93, Kind::Exp, {-185716.4706773822, 10293.22778829032, 0.0861222995695807}},
			{102, 106, 102, Kind::Poly, {-162784.56235886554, 1786.4160472529068, 466.12537698804,
				-40.416722185881994}},
			{107, 113, 107, Kind::Poly, {-146301.6664816273, 4580.799924830345, 197.43336468119008,
				-17.90000436207952}},
			{114, 122, 114, Kind::Poly, {-110185.12479495262, 5352.833191864261, 167.18753613529367,
				-11.145835742352658}},
			{123, 127, 123, Kind::Linear, {-56191.37518806383, 6110.750022344291}},
		};

		const Segment LevelU2[] = {
			{0, 127, 0, Kind::Exp, {-162679.45091294113, 162679.45369267819, 0.011741606585722399}},
		};

		// ---- SD BASIC ----

		// The DECAY law of units 0-2, the amp and the modulator: a 60 dB fall in T ((i + 1) / 128)^2.5 seconds, T 1.5 s
		// for the units, 3 s for the amp, 0.75 s for the modulator (whose entry 0, 0 in the firmware, is 1 LSB off)
		const Segment DecayLaw[] = {
			{0, 127, 0, Kind::Power, {1.5, 1, 128, 2.5}},
		};

		// Approximate, in 48 kHz blocks: plateaus at 3.68455 and twice it, exponentials between, joined without a
		// corner (the time constant within 8e-7, 157 LSB)
		const Segment DecayU1[] = {
			{0, 21, 0, Kind::Linear, {3.6845545986687305, 0}},
			{22, 31, 22, Kind::Exp, {3.162506173873827, 0.604465457731184, 0.14658432677049277}},
			{32, 42, 32, Kind::Exp, {7.917420309591082, -2.173534197211097, -0.1377266550306203}},
			{43, 84, 43, Kind::Linear, {7.369108986368907, 0}},
			{85, 103, 85, Kind::Exp, {6.366671504846276, 1.1378347343886577, 0.1266934627449704}},
			{104, 127, 104, Kind::Exp, {-32.10468164180708, 51.31771184841358, 0.0342057264161997}},
		};

		// Approximate: an exponential up to a plateau it meets at 41-42, entry 41 following neither (0.12 % of its
		// time constant)
		const Segment DecayU2[] = {
			{0, 41, 0, Kind::Exp, {5.675688555782603, -3.72195338526299, -0.020440528313344627}},
			{42, 127, 42, Kind::Linear, {4.070281385311234, 0}},
		};

		// Approximate: a parabola up to 63, then held (6 words 1 LSB off)
		const Segment DecayU3[] = {
			{0, 63, 0, Kind::Poly, {3.271125129353023, 0.027832593673985124, 0.01391629654248624, 0}},
			{64, 127, 64, Kind::Linear, {60.25835950794198, 0}},
		};

		// Approximate, in blocks to full: an exponential to 42, then a line (4.6e-5 of the time)
		const Segment AmpAttack[] = {
			{0, 42, 0, Kind::Exp, {10.410086234216932, -7.2889435538375835, -0.04911591528027044}},
			{43, 127, 43, Kind::Linear, {9.496476471190064, 0.012476474298819564}},
		};

		// Approximate, the depth's word: a line to 32, an exponential up to 62, a parabola over the peak at 63-65 and
		// an exponential down from 66 (within 100 LSB); 33 follows neither and takes the nearer (19 LSB)
		const Segment SdSweepDepth[] = {
			{0, 32, 0, Kind::Linear, {67108863.70967741, 3133277.290322581}},
			{33, 62, 34, Kind::Exp, {286321607.7121187, -112778773.84748396, -0.026645516821513636}},
			{63, 65, 63, Kind::Poly, {234174888, 998652.5, -648705.5, 0}},
			{66, 127, 66, Kind::Exp, {57458734.40355775, 174152441.59644225, -0.013430499291459473}},
		};

		// Approximate, in 48 kHz blocks: one quintic in i / 127 (20 LSB, the time constant within 2.8e-8)
		const Segment SdSweepDecay[] = {
			{0, 127, 0, Kind::Poly5, {1.730392344544399, 12.927049888636935, 0.0005195940202201575, 0.07005854916400979,
				4.438726749664264, 112.75020205324653}},
		};

		// Four times the feedback, in millionths: two lines (0.525 at 0, 2.8 at 127) and an exponential between
		const Segment SdFeedback[] = {
			{0, 54, 0, Kind::Linear, {525000, 2657.333333333335}},
			{55, 70, 55, Kind::Exp, {649928.7612091185, 21480.030500077548, 0.14618869054890787}},
			{71, 127, 71, Kind::Linear, {873791.375, 34396.583333333336}},
		};

		// 1.1 times the target, in millionths: one exponential, 1 at 0 and 0.001 at 127
		const Segment TargetU0[] = {
			{0, 127, 0, Kind::Exp, {-3351.7568456804847, 1003351.662617761, -0.04283873626261854}},
		};

		const Segment TargetU1[] = {
			{0, 127, 0, Kind::Exp, {3828.444684990208, 421280.8061081604, -0.013855634498097201}},
		};

		// The units' frequency factors, halved: lines through 1 at 0, a designer's value at 64, and 2 (U0, U3) or 0.5
		// (U1) at 127
		constexpr double Half = 1073741824.0;
		const Segment ScaleU0[] = {
			{0, 64, 0, Kind::Linear, {Half, Half * 0.0590625 / 64}},
			{65, 127, 64, Kind::Linear, {Half * 1.0590625, Half * (2 - 1.0590625) / 63}},
		};
		const Segment ScaleU1[] = {
			{0, 64, 0, Kind::Linear, {Half, -Half * 0.01125 / 64}},
			{65, 127, 64, Kind::Linear, {Half * 0.98875, Half * (0.5 - 0.98875) / 63}},
		};
		const Segment ScaleU3[] = {
			{0, 64, 0, Kind::Linear, {Half, Half * 0.054140625 / 64}},
			{65, 127, 64, Kind::Linear, {Half * 1.054140625, Half * (2 - 1.054140625) / 63}},
		};

		// Approximate: exponentials, a line and a cubic (1.8e-5 of the depth, 97-101 following neither: 2.4e-4)
		const Segment DepthU3[] = {
			{0, 27, 0, Kind::Exp, {-6794936.7628639275, 10338266.527947212, 0.05499870833220801}},
			{28, 41, 28, Kind::Exp, {-4242724.559999245, 46257991.971329756, 0.07256108023506411}},
			{42, 64, 42, Kind::Linear, {122689635.30285254, 7893928.817137486}},
			{65, 76, 65, Kind::Exp, {754343433.565255, -450566265.02938336, -0.01632177079851463}},
			{77, 83, 77, Kind::Poly, {383869783.27168435, 6108850.358999205, -70082.41912140552, 2244.8124068173306}},
			{84, 101, 84, Kind::Exp, {756110943.6090055, -332191939.1690128, -0.016225468320366043}},
			{102, 127, 102, Kind::Exp, {754232143.8644904, -246202136.82973847, -0.01633082982816438}},
		};

		// Approximate, the low-pass's -pole: cubic stretches (1e-5 of full scale). SD BASIC reads entries 126 and 127
		// alone
		const Segment LowPass[] = {
			{0, 17, 0, Kind::Poly, {-2091970420.5784323, 42663207.61655937, -418525.2913947403, 3147.8861655979736}},
			{18, 39, 18, Kind::Poly, {-1441321190.5181725, 30569356.433178082, -253337.23400501022,
				1594.8830687174063}},
			{40, 67, 40, Kind::Poly, {-874460535.1521995, 21670067.244387884, -150602.95124851944,
				781.5710441316819}},
			{68, 102, 68, Kind::Poly, {-368643257.23615694, 15020487.401046297, -86685.46791235797,
				369.65731913011416}},
			{103, 127, 103, Kind::Poly, {66692977.96318385, 10283192.633724125, -50134.03434800043,
				200.57339229371073}},
		};

		// ---- CY ALLOY ----

		// Units 1 and 3's frequency factors (Ratio) and unit 2's (Spread), in millionths of the note's factor
		// (0x04000000): round knots joined by exponentials (Ratio 1.01, 1.28, 1.5, 1.74, 2.01 at 0, 36, 72, 96, 127;
		// Spread 1.002 at 0, 2.52 at 116, 2.84 at 127) and lines. Ratio's 62-71 is approximate, one exponential where
		// the firmware's turns at 70 (306 millionths)
		const Segment Ratio[] = {
			{0, 36, 0, Kind::Exp, {1336127.0496637938, -326126.9487278214, -0.048879617778127916}},
			{37, 49, 37, Kind::Exp, {1260592.967504099, 22015.296168065724, 0.12611091122006515}},
			{50, 56, 50, Kind::Exp, {1537452.4241489484, -163311.5521777052, -0.0877498583726489}},
			{57, 61, 57, Kind::Exp, {1463838.0759055342, -14904.225071798306, -0.4556158647320812}},
			{62, 71, 62, Kind::Exp, {1461425.5437443, 910.2555155056419, 0.3709511259992971}},
			{72, 96, 72, Kind::Exp, {1873668.2655520998, -373668.1796835267, -0.04283367057814161}},
			{97, 127, 97, Kind::Exp, {1589623.91772229, 155446.3269054666, 0.0331616406987084}},
		};

		const Segment Spread[] = {
			{0, 16, 0, Kind::Exp, {927883.0106512791, 74116.73710053888, 0.06215820182629141}},
			{17, 34, 17, Kind::Exp, {747620.3091895094, 393643.2953448051, 0.0343394896380079}},
			{35, 58, 35, Kind::Exp, {1834018.3477036958, -358957.53748103627, -0.05220188997621075}},
			{59, 82, 59, Kind::Exp, {1391190.370870563, 340385.2022860991, 0.01660501468079772}},
			{83, 107, 83, Kind::Exp, {1746067.0612524853, 151936.48995884313, 0.05068404536237697}},
			{108, 115, 108, Kind::Linear, {2286966, 29107}},
			{116, 116, 116, Kind::Linear, {2520000, 0}},
			{117, 127, 117, Kind::Linear, {2548928.9999999995, 29107.142857143004}},
		};

		// Units 0 and 2's levels at rest, in millionths: one exponential, 1 at 0 and 0.1 at 127
		const Segment Target[] = {
			{0, 127, 0, Kind::Exp, {-31017.29352962434, 1031017.2511614558, -0.016243872765782194}},
		};

		// The modulation depths, in half-millionths: one exponential, 0.25 at 0 and 1 at 127
		const Segment Depth[] = {
			{0, 127, 0, Kind::Exp, {188181.39302621534, 311818.6185595071, 0.013855627651058508}},
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

		std::array<int32_t, 128> build(const Curve& _c)
		{
			std::array<int32_t, 128> t{};
			for(const Segment* s = _c.begin; s != _c.end; ++s)
				for(int i = s->from; i <= s->to; ++i)
					t[i] = toWord(_c.unit, _c.scale, s->at(i));
			return t;
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
		return build(curves[static_cast<int>(_p)]);
	}

	std::array<int32_t, 128> sdBasic(const SdBasic _t)
	{
		constexpr double Micro = Q31 / 1e6;
		const Curve curves[] = {
			curve(DecayLaw, Unit::T60, 1), curve(DecayU1, Unit::Tau, 1), curve(DecayU2, Unit::Tau, 1),
			curve(DecayU3, Unit::Tau, 1), curve(DecayLaw, Unit::T60, 2), curve(DecayLaw, Unit::T60, 0.5),
			curve(AmpAttack, Unit::Rate, 1), curve(SdSweepDepth, Unit::Word), curve(SdSweepDecay, Unit::Tau, 1),
			curve(SdFeedback, Unit::Micro, Micro / 4), curve(TargetU0, Unit::Micro, Micro / 1.1),
			curve(TargetU1, Unit::Micro, Micro), curve(ScaleU0, Unit::Word), curve(ScaleU1, Unit::Word),
			curve(ScaleU3, Unit::Word), curve(DepthU3, Unit::Word), curve(LowPass, Unit::Word),
		};
		static_assert(std::size(curves) == static_cast<size_t>(SdBasic::Count));
		return build(curves[static_cast<int>(_t)]);
	}

	std::array<int32_t, 128> cyAlloy(const CyAlloy _t)
	{
		// The decays: BD MODERN's amp decay law (AmpDecay), its time constant halved for units 1 and 3 and quartered
		// for units 0 and 2 (entry 3 off the law, 2.6e-5 of the time constant); the amp's, SD BASIC's DECAY law at 15 s
		constexpr double Micro = Q31 / 1e6;
		const Curve curves[] = {
			curve(AmpDecay, Unit::Tau, 0.5), curve(Target, Unit::Micro, Micro), curve(AmpDecay, Unit::Tau, 0.25),
			curve(Ratio, Unit::Micro, 67108864 / 1e6), curve(Spread, Unit::Micro, 67108864 / 1e6),
			curve(Depth, Unit::Micro, Micro / 2), curve(DecayLaw, Unit::T60, 10),
		};
		static_assert(std::size(curves) == static_cast<size_t>(CyAlloy::Count));
		return build(curves[static_cast<int>(_t)]);
	}

	std::array<int32_t, 257> cyAlloyWave()
	{
		// Harmonics 1, 4, 7, 10 and 13: sines of 50, 67, 41, 14 and 2 / 174 of full scale. The firmware's words
		// differ from them by up to 3.1e-6 of full scale, smoothly: not a rounding of these
		constexpr int Harmonic[5] = {1, 4, 7, 10, 13};
		constexpr double Amplitude[5] = {50, 67, 41, 14, 2};
		std::array<int32_t, 257> t{};
		for(int i = 0; i < 257; ++i)
		{
			const double x = 2 * 3.14159265358979323846 * i / 256;
			double v = 0;
			for(int k = 0; k < 5; ++k)
				v += Amplitude[k] / 174 * std::sin(Harmonic[k] * x);
			t[i] = clamp(v * Q31);
		}
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
