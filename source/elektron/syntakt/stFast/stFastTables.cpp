#include "stFastTables.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iterator>

#include "stFastStages.h"

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
// The rule: laws, round knots carried by a stretch, fitted stretches; no segment of one entry, no word written for an
// entry of its own. Two exceptions, the user's: a three-constant parabola over three entries where a turn follows no
// stretch, in four places only: SD BASIC's SweepDepth 63-65, PC CARBON's SweepDepth 63-65 and its FactorU2 29-31 and
// 47-49; and an exact round knot of its own in a frequency-ratio table where no stretch can hold it and the exact port
// itself moves past its tolerance with that word a little off, in two places only: CY ALLOY's Spread 116 and SY TONE's
// Ratio 0-7
namespace syntakt::fast::tables
{
	namespace
	{
		constexpr double Q31 = 2147483648.0;
		constexpr double Pi = 3.14159265358979323846;

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
		// Between the knots, the firmware's SweepDecay is the knots' chord up to 30 and from 110; between 30 and 110
		// each interval's inside is a line of its own, joined to the knots by 1 or 2 entries that follow neither. Here
		// the chords up to 29, the line through 40 at 90, and from 30 lines, parabolas and cubics fitted to the words,
		// each over more entries than it has constants. Every entry of the firmware's lines stays within 2 LSB of its
		// word: the exact port itself is ill-conditioned in this decay at some settings (TRANSIENT 87 and the high
		// notes move past Q4 with SWEEP 64's word 100 LSB off), and so does every knot, carried by its stretch. The
		// entries joining the knots, k - 2 to k + 2 but k for k = 30, 40 ... 110, are approximate, within 0.066 % of
		// the time constant (0.5 % off is heard as Q4 measures it: SWEEP 41, 79, 99, 109). The knots disagree with a
		// single KnotTau by about 1e-8
		constexpr double KnotTau = 1.15142326;

		const Segment SweepDecay[] = {
			{0, 9, 0, Kind::Linear, {12, 0}},
			{10, 19, 10, Kind::Linear, {12, 0.6}},
			{20, 29, 20, Kind::Linear, {18, 0.7}},
			{30, 39, 30, Kind::Linear, {25.000000514208757, 0.5036577748755049}},
			{40, 44, 40, Kind::Poly, {30.000001013459503, 1.0377496010319303, 0.2266082980822913,
				-0.024816470386505467}},
			{45, 49, 45, Kind::Poly, {37.894274121867085, 1.6962275808130065, 0.014208712725533701,
				-0.004712361636760476}},
			{50, 53, 50, Kind::Poly, {45.999996765478066, 0.98740958330972, -0.08431163004719539, 0}},
			{54, 59, 54, Kind::Linear, {48.74578466481748, 0.5423691799612993}},
			{60, 65, 60, Kind::Poly, {51.999996910059146, 1.1706300604826978, 0.033258318823198206,
				-0.002772009307977564}},
			{66, 70, 66, Kind::Poly, {59.638978404485115, 1.2023641647229217, 0.14088047337681311,
				-0.04222687233553449}},
			{71, 75, 71, Kind::Poly, {63.38012483612002, -1.7334579489965851, -0.037890890172473435,
				0.00421095410996641}},
			{76, 80, 76, Kind::Poly, {54.26664295655734, -1.7729558401120107, -0.11045494603725217,
				0.04050721096274426}},
			{81, 84, 81, Kind::Linear, {47.17783271986022, -0.8221667194923796}},
			{85, 89, 85, Kind::Poly, {43.88916178251867, -0.817089211438868, -0.0076078889664652006,
				0.002534482520377087}},
			{90, 96, 90, Kind::Linear, {40, -1.552942674901896}},
			{97, 100, 97, Kind::Poly, {29.129398803621047, -1.695468679932368, 0.15077863912743272, 0}},
			{101, 108, 101, Kind::Linear, {24.715674191790104, -0.6843259810898425}},
			{109, 127, 109, Kind::Linear, {19.447059155790853, -0.2470588536080669}},
		};

		// Approximate: knots 0.4, 0.8, 1.0 at 32, 64, 96 and 1.0 at 127, built as the firmware's SweepDecay: each
		// interval's inside a line of its own, 31 and 33 following neither (0.03 % of full scale). The knots at 32 and
		// 96 are carried by the line after and before them, 0.11 and 0.05 % of full scale off
		const Segment ClickLevel[] = {
			{0, 31, 0, Kind::Linear, {150000, 7723}},
			{32, 63, 34, Kind::Linear, {427478.48076923063, 12490.423076923129}},
			{64, 96, 64, Kind::Linear, {800000.02777777787, 6287.0555555555484}},
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
		// Spread 1.002 at 0, 2.52 at 116, 2.84 at 127) and lines. Ratio's turn at 70 is a cubic over 68-72, within 3.2
		// millionths (approximate there, exact elsewhere: a unit 306 millionths off its ratio is heard as Q4 measures
		// it); Spread's knot at 116 is exact, an entry of its own (the rule's exception, header): no stretch holds it
		// without moving its neighbours, and the exact port moves past its own tolerance with a word 1000 LSB off at
		// 116, 805 LSB at 115 (a cubic over 112-116), 201 LSB at 120 under Hit 14 of stFastCyAlloyTest (fifth degree
		// over 116-122: -22.4 dB against its tolerance's -30.5, unit pairs nudged too)
		const Segment Ratio[] = {
			{0, 36, 0, Kind::Exp, {1336127.0496637938, -326126.9487278214, -0.048879617778127916}},
			{37, 49, 37, Kind::Exp, {1260592.967504099, 22015.296168065724, 0.12611091122006515}},
			{50, 56, 50, Kind::Exp, {1537452.4241489484, -163311.5521777052, -0.0877498583726489}},
			{57, 61, 57, Kind::Exp, {1463838.0759055342, -14904.225071798306, -0.4556158647320812}},
			{62, 67, 62, Kind::Exp, {1460433.1162041437, 1596.8094560388752, 0.2988356071151076}},
			{68, 72, 68, Kind::Poly, {1470029.125, 2599.166666667314, 574.2499999997672, 162.33333333335517}},
			{73, 96, 73, Kind::Exp, {1873668.9126667008, -358001.1748848942, -0.042833566456615055}},
			{97, 127, 97, Kind::Exp, {1589623.91772229, 155446.3269054666, 0.0331616406987084}},
		};

		const Segment Spread[] = {
			{0, 16, 0, Kind::Exp, {927883.0106512791, 74116.73710053888, 0.06215820182629141}},
			{17, 34, 17, Kind::Exp, {747620.3091895094, 393643.2953448051, 0.0343394896380079}},
			{35, 58, 35, Kind::Exp, {1834018.3477036958, -358957.53748103627, -0.05220188997621075}},
			{59, 82, 59, Kind::Exp, {1391190.370870563, 340385.2022860991, 0.01660501468079772}},
			{83, 107, 83, Kind::Exp, {1746067.0612524853, 151936.48995884313, 0.05068404536237697}},
			{108, 115, 108, Kind::Linear, {2286966, 29107}},
			{116, 116, 116, Kind::Linear, {2520000, 0}},	// the knot, exact: the rule's exception (header)
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

		// ---- PC CARBON ----

		// Units 0 and 2's decays at a trig, in KnotTau: one exponential law (BD MODERN's AmpDecay's growth, 0.022184
		// an entry) for unit 0 up to 100, and unit 2 at half its time constant up to 10 (entries 0-13), the plateaus
		// round. Approximate: the law within 2.5e-5 of the time constant, unit 2's plateau 1 LSB off
		const Segment PcDecayU0[] = {
			{0, 48, 0, Kind::Exp, {-48.123126043146925, 50.68312951287204, 0.02218395685883282}},
			{49, 127, 49, Kind::Linear, {100, 0}},
		};
		const Segment PcDecayU2[] = {
			{0, 13, 0, Kind::Exp, {-48.123126043146925 / 2, 50.68312951287204 / 2, 0.02218395685883282}},
			{14, 127, 14, Kind::Linear, {10, 0}},
		};

		// The modulation depths of units 1 and 3 in millionths of 2^-5 of full scale: one exponential each from 0 to 24
		// and 30, the targets' rate (0.016244 an entry); the firmware's words waver about it by up to 8 millionths
		// (approximate)
		const Segment PcDepthU1[] = {
			{0, 127, 0, Kind::Exp, {-3493796.6787805893, 3493796.338264171, 0.016243869655114294}},
		};
		const Segment PcDepthU3[] = {
			{0, 127, 0, Kind::Exp, {-4367242.572061408, 4367242.870473522, 0.01624387379776335}},
		};

		// Units 0 and 2's levels at rest, in millionths: one exponential each, 1 and 0.35 at 0, 0 at 127
		const Segment PcTargetU0[] = {
			{0, 127, 0, Kind::Exp, {-145574.77583287912, 1145574.778454971, -0.016243873236079853}},
		};
		const Segment PcTargetU2[] = {
			{0, 127, 0, Kind::Exp, {-50951.21000233317, 400951.178339839, -0.016243868842999432}},
		};

		// The sweep's decay in KnotTau, 150 at 0: an exponential to 91, another to 100, a line to 109, an
		// exponential knee to 114 and a line to 3.21 at 127. Approximate: the stretches fitted within 1200 LSB
		const Segment PcSweepDecay[] = {
			{0, 91, 0, Kind::Exp, {176.82135293459015, -26.821347489012137, 0.016234508539896143}},
			{92, 100, 92, Kind::Exp, {81.51496987862153, -24.095579352776138, 0.0714199651307089}},
			{101, 109, 101, Kind::Linear, {35.88514270714785, -2.833439025477561}},
			{110, 114, 110, Kind::Exp, {3.6024150300814717, 6.777285292420838, -0.3861912355573998}},
			{115, 127, 115, Kind::Linear, {4.607411945303479, -0.116570633502287}},
		};

		// The sweep's depth, the word: from 1 (0x04000000, which the first line passes through: a word below it would
		// leave the pitch 1 LSB of its sweep's factor low forever) up a line to 63, down a line to 93, held at 17/12
		// from 98 to 110, then up a line, the turns three-constant parabolas. Approximate: the lines within 32 LSB of
		// words that waver about them, the parabolas over 94-97 and 111-114 within 1.7e-4 of the word
		const Segment PcSweepDepth[] = {
			{0, 62, 0, Kind::Linear, {67108864, 872635.1694}},
			{63, 65, 63, Kind::Poly, {122011553.0, 527984.5, -247778.5, 0}},
			{66, 93, 66, Kind::Linear, {121353235.86363636, -876540.181818181}},
			{94, 97, 94, Kind::Poly, {96767528.375, -865475.5000000024, 121203.75000000249, 0}},
			{98, 110, 98, Kind::Linear, {95070891.0, 0}},
			{111, 114, 111, Kind::Poly, {95349530.75, 144748.0, 423747.5, 0}},
			{115, 127, 115, Kind::Linear, {101844020.45, 2229846.699999999}},
		};

		// The units' frequency factors in millionths of the note's (0x04000000): lines and exponentials, the turns
		// three-constant parabolas (FactorU2 29-31 and 47-49): unit 1 from 1.05 to 7, unit 2 from 0.1 to 9. The
		// firmware sets round values on some entries the stretches run past, which they do not follow: unit 1's 2.16,
		// 2.7, 4.32, 4.99, 5.66, 6.33 at 43, 53, 87, 97, 107, 117 (48 to 184 millionths off), unit 2's 3.65 and 4.29
		// at 60 and 71 (167 and 32). Approximate there, exact elsewhere
		const Segment PcFactorU1[] = {
			{0, 28, 0, Kind::Linear, {1049999.9999999995, 13819.38461538475}},
			{29, 36, 29, Kind::Exp, {1330490.640919751, 124065.28126797167, 0.1538340135192645}},
			{37, 47, 37, Kind::Linear, {1759844.3333333333, 66700.66666666669}},
			{48, 61, 48, Kind::Exp, {3305051.9738351637, -813472.927041842, -0.059168091621480304}},
			{62, 74, 62, Kind::Exp, {2740785.9611342796, 208915.92853004183, 0.10168290543463089}},
			{75, 127, 75, Kind::Linear, {3515761.109375, 67004.59375000015}},
		};
		const Segment PcFactorU2[] = {
			{0, 28, 0, Kind::Linear, {100000.00000000004, 28177.142857142844}},
			{29, 31, 29, Kind::Poly, {912918.0, 22072.999999999884, 5004.000000000058, 0}},
			{32, 40, 32, Kind::Exp, {427142.50471163285, 601357.2447214461, 0.09840333386218712}},
			{41, 46, 41, Kind::Linear, {1912384.125, 164219.25}},
			{47, 49, 47, Kind::Poly, {2886366.0, 120277.99999999953, -13533.999999999767, 0}},
			{50, 127, 50, Kind::Exp, {-1538661.752462084, 4667769.422744132, 0.010576223993095673}},
		};

		struct Curve
		{
			const Segment* begin;
			const Segment* end;
			Unit unit;
			double scale;
		};

		// The curve's value at entry _i, before its unit: the last segment that holds it
		double value(const Curve& _c, const int _i)
		{
			double y = 0;
			for(const Segment* s = _c.begin; s != _c.end; ++s)
				if(_i >= s->from && _i <= s->to)
					y = s->at(_i);
			return y;
		}

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

	std::array<int32_t, 128> pcCarbon(const PcCarbon _t)
	{
		constexpr double Micro = Q31 / 1e6, Micro26 = 67108864 / 1e6;
		std::array<int32_t, 128> t{};
		switch(_t)
		{
		case PcCarbon::DecayPosition:
			// Where units 1 and 3 read their decay curve: ((i / 127)^2.5) (1 - 2^-10)
			for(int i = 0; i < 128; ++i)
				t[i] = clamp(std::pow(i / 127.0, 2.5) * (1 - 1 / 1024.0) * Q31);
			return t;
		case PcCarbon::FactorU0:
			{
				// Unit 0 at half unit 2's stretches, at least 0.12. Approximate: the firmware's round 2.47 and 2.61 at
				// 81 and 85 are 98 and 82 millionths off, and two words 1 millionth, where half of unit 2's falls on a
				// half millionth
				const Curve u2 = curve(PcFactorU2, Unit::Micro, Micro26);
				for(int i = 0; i < 128; ++i)
					t[i] = toWord(Unit::Micro, Micro26, std::max(120000.0, value(u2, i) / 2));
				return t;
			}
		default:
			break;
		}
		const Curve curves[] = {
			{}, curve(PcSweepDecay, Unit::Tau, KnotTau), curve(PcDepthU1, Unit::Micro, Micro26),
			curve(PcFactorU1, Unit::Micro, Micro26), curve(PcTargetU2, Unit::Micro, Micro),
			curve(PcSweepDepth, Unit::Word), curve(PcTargetU0, Unit::Micro, Micro),
			curve(PcFactorU2, Unit::Micro, Micro26), curve(PcDecayU2, Unit::Tau, KnotTau), {},
			curve(PcDecayU0, Unit::Tau, KnotTau), curve(PcDepthU3, Unit::Micro, Micro26),
		};
		static_assert(std::size(curves) == static_cast<size_t>(PcCarbon::Count));
		return build(curves[static_cast<int>(_t)]);
	}

	std::array<int32_t, 1024> pcCarbonDecayCurve()
	{
		// A time constant growing by a line, 10.4230676 blocks at 0 and 3.17378841 more an entry
		std::array<int32_t, 1024> t{};
		for(int i = 0; i < 1024; ++i)
			t[i] = toWord(Unit::Tau, 1, 10.423067558301803 + 3.1737884136095484 * i);
		return t;
	}

	std::array<int32_t, 257> pcCarbonWave()
	{
		// Harmonics 1, 5 and 9: sines of 0.75, 0.19 and 0.06, 1 at the peak. The firmware's words differ from them by
		// up to 1.6e-6 of full scale, smoothly
		std::array<int32_t, 257> t{};
		for(int i = 0; i < 257; ++i)
		{
			const double x = 2 * 3.14159265358979323846 * i / 256;
			t[i] = clamp((0.75 * std::sin(x) + 0.19 * std::sin(5 * x) + 0.06 * std::sin(9 * x)) * Q31);
		}
		return t;
	}

	// ---- SD VINTAGE ----
	//
	// Its tables are laws of x = i / 127, the parameter's own place, each a round value held or joined to the next by a
	// power of the place between them, u = (x - a) / (b - a), with knots at a multiple of 1/8 (SweepDecay: 0.67).
	// Every word comes out as the firmware's: levels in dB, depths and feedback in cycles of phase per unit of input,
	// the noise's low-pass by its cutoff, the decays by SD BASIC's 60 dB law (T60)

	namespace
	{
		double place(const double _x, const double _a, const double _b)
		{
			return std::clamp((_x - _a) / (_b - _a), 0.0, 1.0);
		}

		double fromDb(const double _db) { return std::pow(10.0, _db / 20); }

		// A curve through knots (x, value), each interval a power _p of its place (1: a line)
		struct Knot
		{
			double x, v, p;	// p: the power of the interval that ends here
		};

		template<size_t N>
		double knots(const Knot (&_k)[N], const double _x)
		{
			for(size_t j = 1; j < N; ++j)
				if(_x <= _k[j].x)
					return _k[j - 1].v + (_k[j].v - _k[j - 1].v) * std::pow(place(_x, _k[j - 1].x, _k[j].x), _k[j].p);
			return _k[N - 1].v;
		}

		// The decays' 60 dB times in seconds: units 0 and 1 (and the amp with HOLD), unit 2, the noise's modulator
		const Knot Decay01T[] = {{0, 0.0375, 1}, {0.25, 0.15, 0.5}, {0.5, 0.2625, 2}, {0.75, 0.2625, 1}, {1, 0.9, 2}};
		const Knot Decay2T[] = {{0, 0.01125, 1}, {0.25, 0.045, 0.5}, {0.5, 0.07875, 2}, {0.75, 0.2625, 2},
			{1, 0.4125, 2}};
		const Knot ModDecayT[] = {{0, 0.0375, 1}, {0.25, 0.1875, 2}, {0.5, 0.1875, 1}, {0.75, 0.4875, 1.5},
			{1, 0.4875, 1}};

		// A wave of harmonics _h with amplitudes in dB _db, sines
		template<size_t N>
		std::array<int32_t, 257> harmonics(const int (&_h)[N], const double (&_db)[N])
		{
			std::array<int32_t, 257> t{};
			for(int i = 0; i < 257; ++i)
			{
				const double x = 2 * 3.14159265358979323846 * i / 256;
				double v = 0;
				for(size_t k = 0; k < N; ++k)
					v += fromDb(_db[k]) * std::sin(_h[k] * x);
				t[i] = clamp(v * Q31);
			}
			return t;
		}
	}

	std::array<int32_t, 128> sdVintage(const SdVintage _t)
	{
		constexpr double Note = 1 << 26;	// a unit's frequency factor at the note's, 2^26
		std::array<int32_t, 128> t{};
		for(int i = 0; i < 128; ++i)
		{
			const double x = i / 127.0;
			switch(_t)
			{
			case SdVintage::Ratio0:		// unit 3's factor: 1.25 to 2.25 at 1/2, then to 8.25
				t[i] = clamp((x <= 0.5 ? 1.25 + 2 * x : 12 * x - 3.75) * Note);
				break;
			case SdVintage::Ratio1:		// unit 1's: a triangle between 1 and 1.1, period 1/2
				t[i] = clamp((1 + 0.4 * std::min(std::fmod(x, 0.5), 0.5 - std::fmod(x, 0.5))) * Note);
				break;
			case SdVintage::SweepDecay:	// 0.33 s to 0.09 at 0.67 (not 2/3: 126 words off), back to 0.33
				t[i] = toWord(Unit::T60, 1, x <= 0.67 ? 0.33 - 0.24 * x / 0.67 : 0.09 + 0.24 * (x - 0.67) / 0.33);
				break;
			case SdVintage::SweepDepth:	// one octave, 2^x
				t[i] = clamp(std::exp2(x) * Note);
				break;
			case SdVintage::Depth2:		// unit 2's modulation: 0.1 to 0.3 cycle from 1/8 to 1/2, u^1.5
				t[i] = clamp((0.1 + 0.2 * std::pow(place(x, 0.125, 0.5), 1.5)) * Q31);
				break;
			case SdVintage::Depth1:		// units 0 and 1's: 0.05 to 0.2
			case SdVintage::Depth0:
				t[i] = clamp((0.05 + 0.15 * std::pow(place(x, 0.125, 0.5), 1.5)) * Q31);
				break;
			case SdVintage::NoiseLp:	// the noise's low-pass, 4 to 16 kHz from 3/4 by u^2: (K - 1) / (K + 1)
				{
					const double u = place(x, 0.75, 1);
					const double k = std::tan(3.14159265358979323846 * (4000 + 12000 * (u * u)) / 48000);
					t[i] = clamp((k - 1) / (k + 1) * Q31);
				}
				break;
			case SdVintage::Level3:		// the noise: -6 dB, then up to 0 dB at 3/4
				t[i] = clamp(fromDb(std::min(-6 + 6 * place(x, 0.5, 0.75), 0.0)) * Q31);
				break;
			case SdVintage::Level2:		// unit 2: -71 dB up to -43 at 1/8, held, then up to -38 from 3/4
				t[i] = clamp(fromDb(x < 0.125 ? -71 + 28 * x * 8 : x < 0.75 ? -43 : -43 + 5 * place(x, 0.75, 1)) * Q31);
				break;
			case SdVintage::Level1:		// unit 1: -56 dB up to -20 at 1/8
				t[i] = clamp(fromDb(x < 0.125 ? -56 + 36 * x * 8 : -20) * Q31);
				break;
			case SdVintage::Feedback:	// unit 3's: 0 to 0.0469 cycle from 1/8 to 3/4, u^1.5
				t[i] = clamp(0.0469 * std::pow(place(x, 0.125, 0.75), 1.5) * Q31);
				break;
			case SdVintage::ModDecay:
				t[i] = toWord(Unit::T60, 1, knots(ModDecayT, x));
				break;
			case SdVintage::Decay2:
				t[i] = toWord(Unit::T60, 1, knots(Decay2T, x));
				break;
			case SdVintage::Decay01:
				t[i] = toWord(Unit::T60, 1, knots(Decay01T, x));
				break;
			case SdVintage::Count:
				break;
			}
		}
		return t;
	}

	std::array<int32_t, 128 * 128> sdVintageDecay3()
	{
		// Units 0 and 1's 60 dB time at DECAY, times 1 - x^2 at the fifth parameter's place: no time at 127, the
		// coefficient 0
		std::array<int32_t, 128 * 128> t{};
		for(int d = 0; d < 128; ++d)
			for(int b = 0; b < 128; ++b)
			{
				// 1 - x^2 from the integers, 0 at 127 under any rounding of the division
				const double time = knots(Decay01T, d / 127.0) * ((127 * 127 - b * b) / (127.0 * 127));
				t[d << 7 | b] = time > 0 ? toWord(Unit::T60, 1, time) : 0;
			}
		return t;
	}

	std::array<int32_t, 257> sdVintageWaveU2()
	{
		return harmonics({1, 2, 4, 8}, {-0.1, -12.8, -19.2, -38.2});
	}

	std::array<int32_t, 257> sdVintageWaveU3()
	{
		return harmonics({1, 2, 3, 4}, {-3.92, -9.9, -13.5, -16.0});
	}

	// ---- CP VINTAGE ----
	//
	// Laws of the entry i (x = i / 127, or i / 128 where the firmware's step is a power of two), every word the
	// firmware's: the decays by SD BASIC's 60 dB law (T60), levels as sin^2, the claps' period in milliseconds, the
	// filters by the bilinear transform without prewarping, K = pi f / fs

	namespace
	{
		// The tone's decay, its 60 dB time 15 ms at 127 and growing as (1 - x)^2: the growth fitted (one constant over
		// 128 entries, every word exact; 4e-11 of it moves a word)
		double toneDecayT60(const double _x) { return 0.015 + 0.132898864741 * (1 - _x) * (1 - _x); }

		// The claps' period in ms, the same both sides of 64: 25 ms at the ends, 9.06 at 64
		double periodMs(const int _i)
		{
			const int d = 64 - std::abs(_i - 64);
			return 13 - d / 16.0 + 12 * std::exp(-d / 12.0);
		}
	}

	std::array<int32_t, 128> cpVintage(const CpVintage _t)
	{
		std::array<int32_t, 128> t{};
		for(int i = 0; i < 128; ++i)
		{
			const double x = i / 127.0;
			const double s2 = std::sin(Pi * x) * std::sin(Pi * x);
			switch(_t)
			{
			case CpVintage::ModDecay:		// the tone's modulator: 1.5 times the tone's time
				t[i] = toWord(Unit::T60, 1, 1.5 * toneDecayT60(x));
				break;
			case CpVintage::ToneDecay:
				t[i] = toWord(Unit::T60, 1, toneDecayT60(x));
				break;
			case CpVintage::SampleRate:		// the rate reducer's period less one, Q16: k log2(k + 1) / 24 at k = i - 64
				t[i] = i <= 64 ? 0 : clamp((i - 64) * std::log2(i - 63.0) / 24 * 65536);
				break;
			case CpVintage::Period:			// in the voice's timer, 2^19 a block (1.5 a ms)
				t[i] = clamp(periodMs(i) * 1.5 * 524288);
				break;
			case CpVintage::RepeatDecay:	// the repeats' decay: 39/16 of the period less 8.4375 ms
				t[i] = toWord(Unit::T60, 1, 2.4375 * periodMs(i) / 1000 - 0.0084375);
				break;
			case CpVintage::ClapLevel:		// the clap in the mix: full, then sin^2 from 1/2
				t[i] = clamp((i < 64 ? 1 : s2) * Q31);
				break;
			case CpVintage::ToneLevel:		// the tone's: -4 dB times sin^2 up to 1/2, then held
				t[i] = clamp(fromDb(-4) * (i < 64 ? s2 : 1) * Q31);
				break;
			case CpVintage::ToneDepth:		// unit 0's modulation by unit 1: 0.05 to 1 cycle at 128
				t[i] = clamp((0.05 + 0.95 * i / 128) * Q31);
				break;
			case CpVintage::ToneFeedback:	// unit 1's: 0.05 + (i / 128 - 0.02)^6 / 4, at most 0.25 (x 4: cycles)
				t[i] = clamp(std::min(0.25, 0.05 + std::pow(i / 128.0 - 0.02, 6) / 4) * Q31);
				break;
			case CpVintage::ToneRatio:		// unit 1's factor: 3 to 8 at 128 times the note's (2^26)
				t[i] = clamp((3 + 5 * i / 128.0) * 67108864);
				break;
			case CpVintage::ClapDecay:		// the noise's last decay: 15 ms, up 15.375 ms an entry, from 64 by 28.125
				t[i] = toWord(Unit::T60, 1, i <= 64 ? 0.015 + 0.015375 * i : 0.999 + 0.028125 * (i - 64));
				break;
			case CpVintage::CrackleDecay:	// the crackle's last decay: 15 ms up to 210 ms at 64, then held
				t[i] = toWord(Unit::T60, 1, 0.015 + 0.195 * std::min(i, 64) / 64);
				break;
			case CpVintage::HighPass:		// the clap's high-pass, -its pole: 4.8 times the note, within 30-2093 Hz
				{
					const double f = std::clamp(440 * std::exp2((i - 69) / 12.0), 30.0, 2093.0);
					t[i] = clamp(-bilinearPole(4.8 * f) * Q31);
				}
				break;
			case CpVintage::Count:
				break;
			}
		}
		return t;
	}

	std::array<int32_t, 1024> cpVintageResonator(const bool _radius)
	{
		// At entry i, the note i / 8 (A4 440 Hz) at 48 kHz, theta its frequency in radians: -cos theta, and the
		// allpass's (1 - t) / (1 + t), t = tan(theta / 4.6), whose band-pass has a Q of 2.3
		std::array<int32_t, 1024> t{};
		for(int i = 0; i < 1024; ++i)
		{
			const double theta = 2 * Pi * 440 * std::exp2((i / 8.0 - 69) / 12) / 48000;
			const double k = std::tan(theta / 4.6);
			t[i] = clamp((_radius ? (1 - k) / (1 + k) : -std::cos(theta)) * Q31);
		}
		return t;
	}

	std::array<int32_t, 1024> cpVintageLog2()
	{
		// log2((k + 1) / 2048) at k = 1024 to 2047, the mantissa rounded up
		std::array<int32_t, 1024> t{};
		for(int k = 0; k < 1024; ++k)
			t[k] = clamp(std::log2((k + 1025) / 2048.0) * Q31);
		return t;
	}

	std::array<int32_t, 1025> cpVintageExp2()
	{
		// 2^(j / 1024) / 2, the last saturated
		std::array<int32_t, 1025> t{};
		for(int j = 0; j < 1025; ++j)
			t[j] = clamp(std::exp2(j / 1024.0) * 1073741824.0);
		return t;
	}

	// ---- SY TONE ----
	//
	// Laws of the entry i in the units of BD MODERN's tables: the feedback, the index and the modulator's level at rest
	// in millionths as the tool that made them printed them (the feedback times 4, the index times 2), each one or two
	// exponentials with their growth fitted to the firmware's words; the ratios by a pattern of round values about
	// each whole ratio; the modulator's decay in time constants of SyKnotTau blocks; the amp's SD BASIC's DECAY law at
	// 15 s (CY ALLOY's)

	namespace
	{
		// _top times (e^(k i) - 1) / (e^(127 k) - 1): 0 at 0, _top at 127
		double growth(const int _i, const double _top, const double _k)
		{
			return _top * std::expm1(_k * _i) / std::expm1(_k * 127);
		}

		// The 16 ratios of a whole ratio n from 1 to 7: n times 1, 1.001 and 1.01, n plus 0.1 to 0.9, n + 1 times 0.99
		// and 0.999
		constexpr double RatioTimes[3] = {1, 1.001, 1.01};
		constexpr double RatioPlus[11] = {0.1, 0.15, 0.2, 0.25, 0.3, 0.4, 0.5, 0.6, 0.75, 0.8, 0.9};
		constexpr double RatioBelow[2] = {0.99, 0.999};

		// Entries 0-7, below 1, are the designer's round ratios, exact, entries of their own: the rule's exception
		// (header), the user's, as CY ALLOY's Spread 116. No law with fewer constants than entries holds them (searched:
		// powers, octave and logistic laws, laws on 1 - ratio, every split into stretches of 3 or more; the best, two
		// stretches, 53.5 cents off), and the exact port moves past Q4 with them a little off
		constexpr double RatioLow[8] = {0.0125, 0.075, 0.125, 0.25, 0.5, 0.75, 0.9, 0.99};

		// The modulator's decay, its time constant y SyKnotTau blocks (the firmware's 60 dB time 5.3025 ms a unit),
		// SyKnotTau fitted to its plateaus 800 and 5, which it holds from 0 to 29 and from 98: between them a knee and
		// four exponentials (approximate: within 2 LSB but at 71 and 81, round knots 70.48 and 40 the stretches run
		// past, 43 and 221 LSB off, 1.7e-6 and 4.9e-6 of the time constant)
		constexpr double SyKnotTau = 1.1514232886;
		const Segment SyModDecay[] = {
			{0, 29, 0, Kind::Linear, {800, 0}},
			{30, 33, 30, Kind::Exp, {800.07906363514792, -0.097239499798643145, 1.7298685657943786}},
			{34, 66, 34, Kind::Exp, {-417.5700702236756, 1171.0324555383127, -0.02436764389405895}},
			{67, 78, 67, Kind::Exp, {34.797578518578661, 71.648663363613394, -0.17427828543635357}},
			{79, 88, 79, Kind::Exp, {71.726475293299799, -28.111681939126672, 0.060480104694639367}},
			{89, 98, 89, Kind::Exp, {0.35944237315066552, 19.856615940616347, -0.16152252396753167}},
			{99, 127, 99, Kind::Linear, {5, 0}},
		};
	}

	std::array<int32_t, 128> syTone(const SyTone _t)
	{
		constexpr double Micro = Q31 / 1e6;
		switch(_t)
		{
		case SyTone::ModDecay:
			return build(curve(SyModDecay, Unit::Tau, SyKnotTau));
		case SyTone::AmpDecay:
			return build(curve(DecayLaw, Unit::T60, 10));
		default:
			break;
		}
		std::array<int32_t, 128> t{};
		for(int i = 0; i < 128; ++i)
		{
			switch(_t)
			{
			case SyTone::Feedback:		// unit 0's: 0 to 0.25 (2 cycles per unit), growing 1.18 % an entry
				t[i] = toWord(Unit::Micro, Micro / 4, growth(i, 1e6, 0.011741605533583225));
				break;
			case SyTone::Ratio:			// unit 0's factor times the note's (2^26): 8 to 15 from 120
				{
					double r = i - 112;
					if(i < 8)
						r = RatioLow[i];	// the exception's knots
					else if(i < 120)
					{
						const int n = 1 + (i - 8) / 16, k = (i - 8) % 16;
						r = k < 3 ? n * RatioTimes[k] : k < 14 ? n + RatioPlus[k - 3] : (n + 1) * RatioBelow[k - 14];
					}
					t[i] = clamp(r * 67108864);
				}
				break;
			case SyTone::Index:			// unit 1's modulation: 0 to 1 (4 cycles per unit), growing 3.11 % an entry
				t[i] = toWord(Unit::Micro, Micro / 2, growth(i, 2e6, 0.030597867189663376));
				break;
			case SyTone::ModTarget:		// unit 0's level at rest: from 1 down an exponential, then to 0.1 at 96, held
				{
					const double y = i < 90 ? 1e6 + 1140036.5004055849 * (std::pow(0.98355947250584386, i) - 1)
						: std::max(1e5, 1e5 + 6667.7216446267939 * (std::pow(0.81329458255596676, i - 96) - 1));
					t[i] = toWord(Unit::Micro, Micro, y);
				}
				break;
			default:
				break;
			}
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
