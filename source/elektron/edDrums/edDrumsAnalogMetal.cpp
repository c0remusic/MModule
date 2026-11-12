#include "edDrumsAnalogPort.h"

#include <algorithm>
#include <array>
#include <cmath>

// Track 12's metal machines (edDrumsAnalog.h): eleven routines and the topology a change of machine sets. Each block
// writes the voice's envelope commands, the six oscillators' rate words, a few control words and the voice's
// digital signals, byte for byte as the original.
namespace elektron::drums::analog::port
{
	namespace
	{
		// The voice's frame (VoiceIo::frame) as these routines use it
		constexpr int Cv = metalFrame::Cv;
		constexpr int Topology = metalFrame::Topology;
		constexpr int Commands = metalFrame::Envelope;	// four stages of two longs: a level word, a command word
		constexpr int Rates = metalFrame::Oscillators;	// six rate words, and six more for a second bank
		constexpr int Setup = metalFrame::Setup;

		// An idle stage's level and command words, which every machine's first block sends
		constexpr uint32_t IdleLevel = 0x40007f80;
		constexpr uint32_t IdleCommand = 0xb0188000;
		constexpr uint32_t Unbounded = 0x7fffffff;		// a countdown that never ends

		// The tables these routines read, by use
		enum class Lookup : uint8_t
		{
			Level, ClosedClassicDecay, OpenClassicDecay, CymbalClassicDecay, BellClassicDecay, ClosedMetallicDecay,
			OpenMetallicDecay, BellMetallicDecay, CymbalMetallicDecay, CymbalMetallicRelease, RideBell, RideDecay,
			RideRelease, HatBasicFeedback, HatBasicDecay, HatBasicRelease, HatLabDecay
		};

		// The envelope's rate curves, knob 0-127 to a rate word: an exponent (bits 16-19) and a mantissa m, the stage
		// stepping by (1 - m / 2^17) / 2^exponent of full scale a tick. Each is a Chebyshev series in sqrt(k) of g,
		// log2 of 1 / step, less log2(k) where the curve starts at an instant stage (k = 0). A fit, not the original's
		// generator: within 2 of the original words (metalTables() gives each one's bound)
		struct RateCurve
		{
			bool fromZero;			// k = 0 is an instant stage, g grows as log2(k) from it
			int32_t first;			// ... and its word
			double c[13];
		};
		constexpr RateCurve ClosedClassicCurve{true, 0, {6.4954403241952532, 1.6072461700227241, 0.36680891301214175,
			-0.10963736819621205, -0.032670647234448076, -0.025695796319625302, 0.0067040077627085921,
			-0.0031928618597880844, 0.0031241472673939354, -0.00092576274180828559, 0.00048007242302726123,
			-0.00016071292815069365, 5.1003112258354011e-06}};
		constexpr RateCurve OpenClassicCurve{true, 0, {8.5558542249572742, 0.55864964588367982, -0.084067173012483037,
			-0.15000119769289644, -0.0094743335362233623, 0.008764168629131857, 0.0075233488656166444,
			-0.0011059322289054224, -0.00014283266355598557, -0.00054672225076262944, 0.00014091962143998379,
			2.0008977927716008e-05, 1.6994543871127605e-05}};
		constexpr RateCurve CymbalClassicCurve{true, 0, {6.4955876588250518, 1.6069969503334505, 0.36702932962451856,
			-0.10984100724599334, -0.032494955896317242, -0.02584096133826742, 0.0068182473594806696,
			-0.0032762722892281035, 0.0031823892223576925, -0.00096134107991020833, 0.0005002714392312519,
			-0.0001689218712402913, 9.4072354721675671e-06}};
		constexpr RateCurve BellClassicCurve{true, 0, {8.4374672346600708, 0.26931876709565439, -0.02513142420516834,
			-0.061247586851987064, -0.005606292199721738, 0.0004595828125966415, 0.0018988727204150224,
			-0.00034229909292008391, 0.00020795847201481564, -0.00013257691502896994, 3.3904639134213896e-05,
			-5.1693622840311598e-06, -3.8619765150079995e-06}};
		constexpr RateCurve MetallicCurve{true, 0, {7.6843568822696842, 0.60590581987882153, 0.089489107676804569,
			-0.060595512812402198, -0.008956334707953699, -0.0051863140034313143, 0.0031102693055820581,
			-0.0011878951621952171, 0.00089594165368122702, -0.00042229510413572033, 0.00017518515542720616,
			-6.6669751317960179e-05, 1.6666978613451081e-05}};
		constexpr RateCurve MetallicDecayCurve{false, 0, {7.7933977542611128, 2.0118161251977313, 0.24930149190688167,
			-0.071963943992874577, 0.024071748769349713, -0.0025339314148155913, -0.0011510163453463222,
			0.00067117662848899425, -0.00016466346584003327, -5.8519513054787027e-07, 1.8938202418042743e-05,
			-5.7574541960707838e-06, 2.2559766431485198e-06}};
		constexpr RateCurve MetallicReleaseCurve{true, 0x80, {8.1731306361481515, 0.062419557945693815,
			-0.012779980309038633, -0.021963147407809341, -9.8276262696663582e-05, -0.0011772174479454849,
			0.0010363277777252172, -0.00051482740085637645, 0.00027692040206647972, -0.0001307058829707264,
			4.6301425733819593e-05, -1.3297799073831558e-05, 1.2831890061958456e-06}};
		constexpr RateCurve RideBellCurve{false, 0, {13.285973811248637, 1.7945403346339348, 0.046994228086336062,
			-0.11195120476447704, 0.032890941839075989, -0.0070733525352969723, -0.00052828859518750889,
			0.0011198544607719473, -0.00044970799215371762, 9.0313418832405147e-05, 7.6593836217685753e-06,
			-1.3042103471882043e-05, 5.0544254104924801e-06}};
		constexpr RateCurve RideDecayCurve{false, 0, {12.090979754223293, 2.498183759752719, -0.051308501524336809,
			-0.10938181835896268, 0.063467701991552078, -0.026918382244340015, 0.0060695150292872637,
			0.00044183028598435451, -0.0013153665152463635, 0.00080915613491012832, -0.00032025985859186507,
			8.4806973031100193e-05, -9.1738259865966146e-06}};
		constexpr RateCurve RideReleaseCurve{false, 0, {10.136977147204011, 2.5405981978628733, 1.0059575073752902,
			0.14483180096522158, -0.049586737920314268, -0.031738531765917039, -0.0033588581497843357,
			0.0018681045740236116, 0.00023144520875300662, -0.00046223952767538649, -0.00013256620252055093,
			2.9072241903690288e-05, 5.545247645420472e-05}};
		// The basic hat's noise feedback, knob 1-126 to a gain below one in Q31: 2^-e, log2(e k) a series in sqrt(k)
		constexpr double FeedbackCurve[15] = {-2.5069536764161251, -1.5600820557251627, -0.46054312471154896,
			-0.037780151259164764, -0.002583237563674342, 0.00083567997110767602, 0.00011306786493608529,
			-5.403117709184597e-06, -6.72947056667745e-06, 1.5281463371381698e-07, -3.4091220878890162e-07,
			3.5862652441552595e-07, -1.769991178645641e-07, 8.0682742087418114e-08, -5.5128133172246574e-08};

		template<size_t N> double chebyshev(const double (&_c)[N], const int _k)
		{
			const double u = 2 * std::sqrt(_k / 127.0) - 1;
			double b1 = 0, b2 = 0;
			for(size_t i = N - 1; i > 0; --i)
			{
				const double b = 2 * u * b1 - b2 + _c[i];
				b2 = b1;
				b1 = b;
			}
			return u * b1 - b2 + _c[0];
		}

		int32_t rateWord(const RateCurve& _curve, const int _k)
		{
			if(_curve.fromZero && _k == 0)
				return _curve.first;
			const double g = chebyshev(_curve.c, _k) + (_curve.fromZero ? std::log2(static_cast<double>(_k)) : 0.0);
			auto exponent = static_cast<int32_t>(std::floor(g));
			const double fraction = std::exp2(exponent - g);		// in (1/2, 1]
			auto m = std::llround((1 - fraction) * 131072);
			if(m > 0xffff)
			{
				++exponent;
				m = std::llround((1 - 2 * fraction) * 131072);
			}
			return exponent << 16 | static_cast<int32_t>(std::clamp<long long>(m, 0, 0xffff));
		}

		using Curve = std::array<int32_t, 128>;
		template<typename F> Curve makeCurve(F _f)
		{
			Curve t{};
			for(int k = 0; k < 128; ++k)
				t[static_cast<size_t>(k)] = _f(k);
			return t;
		}
		Curve rates(const RateCurve& _curve)
		{
			return makeCurve([&](const int _k) { return rateWord(_curve, _k); });
		}

		// The tables, made at static initialisation. The fits are not const: edDrumsAnalogControlTest swaps the
		// original's words in through metalTables() to check the logic apart from the fits
		// 0 to 18 dB in 127 steps, unity in Q13: exact
		const Curve LevelTable = makeCurve([](const int _k)
		{
			return static_cast<int32_t>(std::llround(8192.0 * std::pow(10.0, 0.9 * _k / 127)));
		});
		Curve ClosedClassicTable = rates(ClosedClassicCurve);
		Curve OpenClassicTable = rates(OpenClassicCurve);
		Curve CymbalClassicTable = rates(CymbalClassicCurve);
		Curve BellClassicTable = rates(BellClassicCurve);
		Curve MetallicTable = rates(MetallicCurve);
		Curve MetallicDecayTable = rates(MetallicDecayCurve);
		Curve MetallicReleaseTable = rates(MetallicReleaseCurve);
		Curve RideBellTable = rates(RideBellCurve);
		Curve RideDecayTable = rates(RideDecayCurve);
		Curve RideReleaseTable = rates(RideReleaseCurve);
		Curve FeedbackTable = makeCurve([](const int _k)
		{
			if(_k == 0)
				return 0;
			const double e = std::exp2(chebyshev(FeedbackCurve, _k)) / _k;
			return static_cast<int32_t>(std::llround(std::ldexp(std::exp2(-e), 31)));
		});

		// An entry of a table, _index the signed byte the routine indexes with. The machines' knobs give 0-127; the
		// original reads its neighbours' memory past either end, which the port does not: it clamps the index to
		// 0-127 here, the only place a table is read
		int32_t entry(const Lookup _t, const int _index)
		{
			const auto k = static_cast<size_t>(std::clamp(_index, 0, 127));
			switch(_t)
			{
			case Lookup::Level:					return LevelTable[k];
			case Lookup::HatBasicFeedback:		return FeedbackTable[k];
			case Lookup::ClosedClassicDecay:
			case Lookup::HatLabDecay:			return ClosedClassicTable[k];
			case Lookup::OpenClassicDecay:		return OpenClassicTable[k];
			case Lookup::CymbalClassicDecay:	return CymbalClassicTable[k];
			case Lookup::BellClassicDecay:		return BellClassicTable[k];
			case Lookup::ClosedMetallicDecay:
			case Lookup::OpenMetallicDecay:
			case Lookup::BellMetallicDecay:		return MetallicTable[k];
			case Lookup::CymbalMetallicDecay:
			case Lookup::HatBasicDecay:			return MetallicDecayTable[k];
			case Lookup::CymbalMetallicRelease:
			case Lookup::HatBasicRelease:		return MetallicReleaseTable[k];
			case Lookup::RideBell:				return RideBellTable[k];
			case Lookup::RideDecay:				return RideDecayTable[k];
			case Lookup::RideRelease:			return RideReleaseTable[k];
			}
			return 0;
		}

		// The six oscillators' frequencies in Hz: their rate words are hz / 1500 in Q24, truncated
		using Six = std::array<int32_t, 6>;
		constexpr int32_t rateOfHz(const int64_t _hz) { return static_cast<int32_t>((_hz << 24) / 1500); }
		constexpr Six six(const int64_t _a, const int64_t _b, const int64_t _c, const int64_t _d, const int64_t _e,
			const int64_t _f)
		{
			return {rateOfHz(_a), rateOfHz(_b), rateOfHz(_c), rateOfHz(_d), rateOfHz(_e), rateOfHz(_f)};
		}
		// The classic set is a TR-808's six square oscillators, rounded to the hertz
		constexpr Six Classic = six(540, 800, 522, 304, 370, 205);
		// Three oscillators and three words far above them, which saturate
		constexpr Six Metallic = six(272, 8529, 633, 9073, 1458, 13500);
		constexpr Six CymbalMetallic = six(272, 8601, 641, 9161, 1485, 13703);
		// Two pairs nearly in unison, which beat
		constexpr Six HatBasic = six(246, 418, 308, 440, 241, 627);

		uint8_t* cmd(VoiceIo& _io, const int _off) { return _io.frame.data() + Commands + _off; }
		uint32_t cmdLong(VoiceIo& _io, const int _off) { return be32(cmd(_io, _off)); }
		void setCmd(VoiceIo& _io, const int _off, const uint32_t _v) { setBe32(cmd(_io, _off), _v); }
		// A command's top nibble, its low nibble kept
		void setNibble(VoiceIo& _io, const int _off, const uint8_t _high)
		{
			uint8_t& b = *cmd(_io, _off);
			b = static_cast<uint8_t>((b & 0xf) | _high);
		}
		// A command's low nibble, its top nibble kept
		void setLowNibble(VoiceIo& _io, const int _off, const int32_t _low)
		{
			uint8_t& b = *cmd(_io, _off);
			b = static_cast<uint8_t>((b & 0xf0) | (_low & 0xf));
		}
		// A command's rate: its low 20 bits
		void setRate(VoiceIo& _io, const int _off, const int32_t _rate)
		{
			setCmd(_io, _off, (cmdLong(_io, _off) & 0xfff00000) | (static_cast<uint32_t>(_rate) & 0xfffff));
		}
		void copyStage(VoiceIo& _io, const int _to, const int _from)
		{
			setCmd(_io, _to, cmdLong(_io, _from));
			setCmd(_io, _to + 4, cmdLong(_io, _from + 4));
		}
		// Both stages of a voice idle: what a block before the trig's first sends
		void idle(VoiceIo& _io, const int _a, const int _b)
		{
			setCmd(_io, _a, IdleLevel);
			setCmd(_io, _a + 4, IdleCommand);
			setCmd(_io, _b, IdleLevel);
			setCmd(_io, _b + 4, IdleCommand);
		}

		// The state machine's step, its countdown and a level it keeps
		int32_t step(const VoiceIo& _io) { return static_cast<int32_t>(be32(_io.state.data())); }
		int32_t countdown(const VoiceIo& _io) { return static_cast<int32_t>(be32(_io.state.data() + 4)); }
		void setStep(VoiceIo& _io, const int32_t _v) { setBe32(_io.state.data(), static_cast<uint32_t>(_v)); }
		void setCountdown(VoiceIo& _io, const int32_t _v) { setBe32(_io.state.data() + 4, static_cast<uint32_t>(_v)); }
		// The countdown's low nibble, as the stage that follows it takes it
		int32_t countdownNibble(const VoiceIo& _io) { return _io.state[7] & 0xf; }
		// The countdown has run out: the block a stage ends in
		bool due(const VoiceIo& _io) { return countdown(_io) <= 15; }

		int8_t byteParam(const VoiceIo& _io, const int _i)
		{
			return static_cast<int8_t>(_io.params[static_cast<size_t>(_i)]);
		}
		int16_t wordParam(const VoiceIo& _io, const int _i)
		{
			return static_cast<int16_t>(be16(_io.params.data() + 2 * _i));
		}

		void setFrameWord(VoiceIo& _io, const int _off, const int32_t _v)
		{
			setBe16(_io.frame.data() + _off, static_cast<uint32_t>(_v));
		}

		// How the oscillators and filters are wired: three switches
		void setTopology(VoiceIo& _io, const int _index)
		{
			uint8_t& b = _io.frame[Topology];
			b = static_cast<uint8_t>((b & ~0x38) | (_index << 3));
		}

		// The voice's level, a knob of 18 dB scaled by _scale
		void setLevel(Emac& _e, VoiceIo& _io, const int32_t _scale)
		{
			_e.mac(0, _scale, entry(Lookup::Level, byteParam(_io, 0)));
			setFrameWord(_io, Cv + 8, _e.storeClear(0));
		}
		// The tune knob centred: a control word about zero
		void setTuneCv(VoiceIo& _io)
		{
			setFrameWord(_io, Cv, (wordParam(_io, 3) - 0x4000) * 2);
		}

		// The six oscillators' rate words at _at: the note and tune as a modulator's pitch, times each frequency,
		// bounded, over a header of _top's top byte, _bank at bit 22 and _restart at bit 31
		void setOscillators(Emac& _e, VoiceIo& _io, const int _at, const int32_t _bank, const int32_t _tune,
			const int32_t _note, const Six& _hz, const int32_t _top, const int32_t _restart)
		{
			const auto pitch = static_cast<int32_t>(expPitch(_e, modPitchWord(_e, _tune, _note)));
			const int32_t header = add(lsl(_bank, 22), add(_top & 0x7f000000, lsl(_restart, 31)));
			int32_t r[6];
			_e.mac(0, pitch, _hz[0]);
			_e.mac(1, pitch, _hz[1]);
			_e.mac(2, pitch, _hz[2]);
			_e.mac(3, pitch, _hz[3]);
			r[0] = _e.storeClear(0);
			r[1] = _e.storeClear(1);
			_e.mac(0, pitch, _hz[4]);
			_e.mac(1, pitch, _hz[5]);
			r[2] = _e.storeClear(2);
			r[3] = _e.storeClear(3);
			r[4] = _e.storeClear(0);
			r[5] = _e.storeClear(1);
			for(int i = 0; i < 6; ++i)
			{
				const uint32_t bounded = std::min(static_cast<uint32_t>(r[i]), 0x3fffffu);
				const int32_t word = add(static_cast<int32_t>(bounded), header);
				setBe32(_io.frame.data() + _at + 4 * i, static_cast<uint32_t>(word));
			}
		}

		void clearHead(VoiceIo& _io) { std::fill_n(_io.heads.begin(), 4, uint8_t(0)); }
		void silence(VoiceIo& _io) { std::fill_n(_io.outA.begin(), 128, uint8_t(0)); }
		void setNoise(VoiceIo& _io, const uint8_t _a, const uint8_t _b, const uint8_t _c, const uint8_t _d)
		{
			_io.noise = {_a, _b, _c, _d};
		}
		void setOut(VoiceIo& _io, const int _i, const int32_t _v)
		{
			setBe32(_io.outA.data() + 4 * _i, static_cast<uint32_t>(_v));
		}
		// _n longs of outA from _from, a straight line towards _to
		void line(VoiceIo& _io, const int _from, const int32_t _start, const int32_t _end, const int _n)
		{
			const int32_t d = sub(_end, _start) / _n;
			int32_t v = _start;
			for(int i = 0; i < _n; ++i)
			{
				setOut(_io, _from + i, v);
				v = add(v, d);
			}
		}
		// An envelope that falls from _level to zero over _n longs of outA, kept for the next block
		int32_t fall(VoiceIo& _io, const int32_t _level, const int32_t _step, const int _n)
		{
			int32_t v = _level;
			for(int i = 0; i < _n; ++i)
			{
				setOut(_io, i, v);
				v = add(v, _step);
			}
			return add(_level, _step * _n);
		}

		// A release's rate word from its time: an exponent and a mantissa of _level << 23 / _time
		int32_t releaseRate(const int32_t _level, const int32_t _time)
		{
			if(_time == 0)
				return 0xfffff;
			int32_t q = lsl(_level, 23);
			if(!(q == INT32_MIN && _time == -1))
				q = q / _time;
			q = add(q, 0x8000);
			const int n = ff1(static_cast<uint32_t>(q)) - 1;
			q = lsl(q, n);
			const auto exponent = static_cast<int32_t>(static_cast<uint32_t>(n) << 16);
			return static_cast<int32_t>(static_cast<uint32_t>(asr(sub(INT32_MAX, q), 14)) & 0xffff) | exponent;
		}

		// The classic hats' attack and decay: two stages, then the release at the decay knob's rate
		void classicHat(VoiceIo& _io, Emac& _e, const Lookup _release)
		{
			const int32_t release = entry(_release, byteParam(_io, 4));
			switch(step(_io))
			{
			case 0:
				idle(_io, 0x10, 0x08);
				setStep(_io, 1);
				return;
			case 1:
			{
				const auto n = static_cast<uint32_t>(countdownNibble(_io)) << 24;
				setCmd(_io, 0x18, 0x40007f7f);
				setCmd(_io, 0x1c, n | 0xf0100000);
				setCmd(_io, 0x10, IdleLevel);
				setCmd(_io, 0x14, n | 0xf0173806);
				setStep(_io, 2);
				setCountdown(_io, decayTicks(_e, 0x20000000, 0x73806));
				return;
			}
			case 2:
				if(!due(_io))
					return;
				*cmd(_io, 0x14) = static_cast<uint8_t>(0x90 | countdownNibble(_io));
				setNibble(_io, 0x15, 0x10);
				setRate(_io, 0x14, release);
				*cmd(_io, 0x1b) = 0x80;
				*cmd(_io, 0x1c) = static_cast<uint8_t>(0xb0 | countdownNibble(_io));
				setNibble(_io, 0x1d, 0x10);
				setRate(_io, 0x1c, release);
				setStep(_io, -1);
				return;
			default:
				setNibble(_io, 0x14, 0x90);
				setRate(_io, 0x14, release);
				setNibble(_io, 0x1c, 0x90);
				setRate(_io, 0x1c, release);
				return;
			}
		}

		// The bells' envelope: an attack, then the decay knob's rate on both stages
		void bell(VoiceIo& _io, Emac& _e, const Lookup _decay, const int32_t _level)
		{
			switch(step(_io))
			{
			case 0:
				setStep(_io, 1);
				return;
			case 1:
				setCmd(_io, 0x1c, 0xf0100000);
				setCmd(_io, 0x18, 0x40007f7f);
				setCmd(_io, 0x10, 0x40001488);
				setCmd(_io, 0x14, 0xf0100000 | 0x7f32f);
				setStep(_io, 2);
				setCountdown(_io, decayTicks(_e, _level, 0x7f32f));
				return;
			case 2:
			{
				if(!due(_io))
					return;
				const int32_t decay = entry(_decay, byteParam(_io, 4));
				*cmd(_io, 0x14) = static_cast<uint8_t>(0x90 | countdownNibble(_io));
				setNibble(_io, 0x15, 0x10);
				setRate(_io, 0x14, decay);
				*cmd(_io, 0x1b) = 0x80;
				*cmd(_io, 0x1c) = static_cast<uint8_t>(0xb0 | countdownNibble(_io));
				setNibble(_io, 0x1d, 0x10);
				setRate(_io, 0x1c, decay);
				setCountdown(_io, Unbounded);
				setStep(_io, -1);
				return;
			}
			default:
			{
				const int32_t decay = entry(_decay, byteParam(_io, 4));
				setNibble(_io, 0x14, 0x90);
				setRate(_io, 0x14, decay);
				setNibble(_io, 0x1c, 0x90);
				setRate(_io, 0x1c, decay);
				setCountdown(_io, Unbounded);
				setStep(_io, -1);
				return;
			}
			}
		}

		// The bells' two oscillators: two pitches as a modulator's, scaled into the second bank's first two words
		void bellOscillators(Emac& _e, VoiceIo& _io, const int32_t _tuneA, const int32_t _tuneB)
		{
			constexpr int32_t Scale = 0x594d24;
			const auto a = static_cast<int32_t>(expPitch(_e, modPitchWord(_e, _tuneA, _io.pitch)));
			const auto b = static_cast<int32_t>(expPitch(_e, modPitchWord(_e, _tuneB, _io.pitch)));
			_e.mac(0, Scale, a);
			_e.mac(1, Scale, b);
			const int32_t ra = _e.storeClear(0);
			const int32_t rb = _e.storeClear(1);
			setBe32(_io.frame.data() + Rates + 0x18, (static_cast<uint32_t>(ra) & 0x3fffff) | 0x40c00000);
			setBe32(_io.frame.data() + Rates + 0x1c, (static_cast<uint32_t>(rb) & 0x3fffff) | 0x40c00000);
		}

		// A metallic machine's release stage: the decay command, two of its mode bits kept
		void releaseCommand(VoiceIo& _io, const int _off)
		{
			uint8_t& b = *cmd(_io, _off);
			b = static_cast<uint8_t>((b & 0x60) | 0x90 | (b & 0xf));
		}
		// ... at the decay knob's rate
		void metallicRelease(VoiceIo& _io, const int _off, const int32_t _rate)
		{
			releaseCommand(_io, _off);
			setRate(_io, _off, _rate);
		}
	}

	const Table* metalTables(int& _count)
	{
		// the fits, each within bound of the original's words (measured over the 128)
		static const Table tables[] = {
			{"metal.closedClassicDecay", ClosedClassicTable.data(), 128, 4, 2},
			{"metal.openClassicDecay", OpenClassicTable.data(), 128, 4, 2},
			{"metal.cymbalClassicDecay", CymbalClassicTable.data(), 128, 4, 2},
			{"metal.bellClassicDecay", BellClassicTable.data(), 128, 4, 2},
			{"metal.metallicDecay", MetallicTable.data(), 128, 4, 1},
			{"metal.cymbalMetallicDecay", MetallicDecayTable.data(), 128, 4, 2},
			{"metal.cymbalMetallicRelease", MetallicReleaseTable.data(), 128, 4, 1},
			{"metal.rideBell", RideBellTable.data(), 128, 4, 1},
			{"metal.rideDecay", RideDecayTable.data(), 128, 4, 1},
			{"metal.rideRelease", RideReleaseTable.data(), 128, 4, 2},
			{"metal.hatBasicFeedback", FeedbackTable.data(), 128, 4, 1},
		};
		_count = static_cast<int>(sizeof(tables) / sizeof(tables[0]));
		return tables;
	}

	void closedClassic(VoiceIo& _io)
	{
		Emac e(_io.saturate);
		setTopology(_io, 1);
		setTuneCv(_io);
		setLevel(e, _io, 0x49581062);
		classicHat(_io, e, Lookup::ClosedClassicDecay);
		setOscillators(e, _io, Rates, 1, wordParam(_io, 1), _io.pitch, Classic, 0x40000000, 0);
		clearHead(_io);
		silence(_io);
		setNoise(_io, 0, 0, 0, 0);
	}

	void openClassic(VoiceIo& _io)
	{
		Emac e(_io.saturate);
		setTopology(_io, 1);
		setTuneCv(_io);
		setLevel(e, _io, 0x2978d4fd);
		const int32_t s = step(_io);
		if(s == 0)
		{
			idle(_io, 0x10, 0x08);
			setStep(_io, 1);
		}
		else if(s == 1)
		{
			const auto n = static_cast<uint32_t>(countdownNibble(_io)) << 24;
			const auto decay = static_cast<uint32_t>(entry(Lookup::OpenClassicDecay, byteParam(_io, 4))) & 0xfffff;
			setCmd(_io, 0x18, IdleLevel);
			setCmd(_io, 0x1c, n | 0xf0000000 | decay);
			setCmd(_io, 0x08, IdleLevel);
			setCmd(_io, 0x0c, n | 0xf0000000 | decay);
			setCountdown(_io, Unbounded);
			setStep(_io, -1);
		}
		else
		{
			const int32_t decay = entry(Lookup::OpenClassicDecay, byteParam(_io, 4));
			setNibble(_io, 0x1c, 0x90);
			setRate(_io, 0x1c, decay);
			setNibble(_io, 0x0c, 0x90);
			setRate(_io, 0x0c, decay);
			if(due(_io))
			{
				setCountdown(_io, Unbounded);
				setStep(_io, -1);
			}
		}
		setOscillators(e, _io, Rates, 1, wordParam(_io, 1), _io.pitch, Classic, 0x40000000, 0);
		clearHead(_io);
		silence(_io);
		setNoise(_io, 0, 0, 0, 0);
	}

	void cymbalClassic(VoiceIo& _io)
	{
		Emac e(_io.saturate);
		setTopology(_io, 3);
		setLevel(e, _io, 0x1126e978);
		const auto release = [&]
		{
			const int32_t decay = entry(Lookup::CymbalClassicDecay, byteParam(_io, 4));
			setNibble(_io, 0x04, 0x90);
			setRate(_io, 0x04, decay);
			setNibble(_io, 0x1c, 0x90);
			setRate(_io, 0x1c, decay);
			if(due(_io))
			{
				setCountdown(_io, Unbounded);
				setStep(_io, -1);
			}
		};
		const int32_t s = step(_io);
		if(s == 1)
		{
			// the colour knob sets the three stages' levels about their centre
			int32_t rising, falling;
			knobPair(e, wordParam(_io, 4), rising, falling);
			e.msac(0, 0xff, falling);
			e.msac(1, 0x45, rising);
			e.msac(2, 0xff, rising);
			const int32_t a = e.storeClear(0);
			const int32_t b = e.storeClear(1);
			const int32_t c = e.storeClear(2);
			const auto centred = [](const int32_t _v) { return static_cast<uint32_t>(_v - 0x80) & 0xff; };
			setCmd(_io, 0x18, 0x40007f7f);
			setCmd(_io, 0x1c, 0xf0100000);
			setCmd(_io, 0x14, 0xf0186036);
			setCmd(_io, 0x0c, 0xf01709e9);
			setCmd(_io, 0x08, centred(b) | 0x40008000);
			setCmd(_io, 0x10, centred(a) << 8 | 0x40000080);
			setCmd(_io, 0x00, centred(c) | 0x40008000);
			setCmd(_io, 0x04, 0xf01969e5);
			setStep(_io, 2);
			setCountdown(_io, 600);
		}
		else if(s == 2)
		{
			if(due(_io))
			{
				*cmd(_io, 0x14) = 0x90;
				*cmd(_io, 0x0c) = 0xb0;
				setCmd(_io, 0x14, (cmdLong(_io, 0x14) & 0xfff00000) | 0xba47f);
				*cmd(_io, 0x0b) = 0x80;
				setCmd(_io, 0x0c, (cmdLong(_io, 0x0c) & 0xfff00000) | 0xba47f);
				setStep(_io, 3);
				setCountdown(_io, add(countdown(_io), 2400));
			}
		}
		else if(s == 3)
		{
			if(due(_io))
			{
				const int32_t decay = entry(Lookup::CymbalClassicDecay, byteParam(_io, 4));
				*cmd(_io, 0x0c) = 0x90;
				*cmd(_io, 0x03) = 0x80;
				setCmd(_io, 0x0c, (cmdLong(_io, 0x0c) & 0xfff00000) | 0xc3b37);
				*cmd(_io, 0x04) = 0xb0;
				setRate(_io, 0x04, decay);
				*cmd(_io, 0x1c) = 0xb0;
				*cmd(_io, 0x1b) = 0x80;
				setRate(_io, 0x1c, decay);
				setCountdown(_io, Unbounded);
				setStep(_io, -1);
			}
		}
		else if(s == 0)
			setStep(_io, 1);
		else
			release();
		setTuneCv(_io);
		setOscillators(e, _io, Rates, 1, wordParam(_io, 1), _io.pitch, Classic, 0x40000000, 0);
		clearHead(_io);
		setNoise(_io, 0, 0, 0, 0);
		silence(_io);
	}

	void bellClassic(VoiceIo& _io)
	{
		Emac e(_io.saturate);
		setTopology(_io, 5);
		bell(_io, e, Lookup::BellClassicDecay, 0x33333333);
		copyStage(_io, 0x08, 0x10);
		const int16_t tune = wordParam(_io, 1);
		bellOscillators(e, _io, tune, tune + (wordParam(_io, 3) >> 2));
		setLevel(e, _io, 0x26872b02);
		clearHead(_io);
		silence(_io);
		setNoise(_io, 0, 0, 0, 0);
	}

	void closedMetallic(VoiceIo& _io)
	{
		Emac e(_io.saturate);
		setTopology(_io, 2);
		setFrameWord(_io, Cv, 0);
		setLevel(e, _io, 0x43f7ced9);
		const int32_t s = step(_io);
		bool release = true;
		if(s == 1)
		{
			setCmd(_io, 0x18, 0x40007f7f);
			setCmd(_io, 0x1c, 0xf0100000);
			setCmd(_io, 0x10, 0x4000807f);
			setCmd(_io, 0x14, 0xf0173806);
			setStep(_io, 2);
			setCountdown(_io, decayTicks(e, 0x20000000, 0x73806));
			release = false;
		}
		else if(s == 2)
		{
			if(due(_io))
			{
				const auto hold = static_cast<uint8_t>((*cmd(_io, 0x1c) & 0xd0) | 0x20);
				*cmd(_io, 0x1b) = 0x80;
				setNibble(_io, 0x1c, hold);
				setLowNibble(_io, 0x1c, countdownNibble(_io));
				setLowNibble(_io, 0x14, countdownNibble(_io));
				setStep(_io, 3);
			}
			else
				release = false;
		}
		else if(s == 0)
		{
			idle(_io, 0x10, 0x08);
			setStep(_io, 1);
			release = false;
		}
		if(release)
		{
			const int32_t decay = entry(Lookup::ClosedMetallicDecay, byteParam(_io, 4));
			metallicRelease(_io, 0x1c, decay);
			metallicRelease(_io, 0x14, decay);
		}
		setOscillators(e, _io, Rates + 0x18, 2, wordParam(_io, 1), _io.pitch, Metallic, 0x40000000, 0);
		clearHead(_io);
		silence(_io);
		setNoise(_io, 0, 0, 0, 0);
	}

	void openMetallic(VoiceIo& _io)
	{
		Emac e(_io.saturate);
		setTopology(_io, 2);
		setFrameWord(_io, Cv, 0);
		setLevel(e, _io, 0x32f1a9fb);
		const int32_t s = step(_io);
		bool release = true;
		if(s == 1)
		{
			const int32_t rate = releaseRate(0x40, 0x780);
			setCmd(_io, 0x18, IdleLevel);
			setCmd(_io, 0x1c, 0xf00fffff);
			setCmd(_io, 0x10, 0x4000807f);
			setCmd(_io, 0x14, (static_cast<uint32_t>(rate) & 0xfffff) | 0xf0000000);
			setCountdown(_io, 0x780);
			setStep(_io, 2);
			release = false;
		}
		else if(s == 2)
		{
			if(due(_io))
			{
				setLowNibble(_io, 0x1c, countdownNibble(_io));
				setNibble(_io, 0x1d, 0x10);
				setLowNibble(_io, 0x14, countdownNibble(_io));
				setNibble(_io, 0x15, 0x10);
				setStep(_io, -1);
			}
			else
				release = false;
		}
		else if(s == 0)
		{
			idle(_io, 0x10, 0x08);
			setStep(_io, 1);
			release = false;
		}
		if(release)
		{
			const int32_t decay = entry(Lookup::OpenMetallicDecay, byteParam(_io, 4));
			setNibble(_io, 0x1c, 0x90);
			setRate(_io, 0x1c, decay);
			setNibble(_io, 0x14, 0x90);
			setRate(_io, 0x14, decay);
		}
		setOscillators(e, _io, Rates + 0x18, 2, wordParam(_io, 1), _io.pitch, Metallic, 0x40000000, 0);
		clearHead(_io);
		silence(_io);
		setNoise(_io, 0, 0, 0, 0);
	}

	void cymbalMetallic(VoiceIo& _io)
	{
		Emac e(_io.saturate);
		setTopology(_io, 4);
		setFrameWord(_io, Cv, 0);
		setLevel(e, _io, 0x5a7ef9db);
		// the colour knob: one level falling as its square, one rising
		int32_t rising, falling;
		knobPair(e, wordParam(_io, 3), rising, falling);
		const int32_t d = sub(INT32_MIN, rising);
		e.msac(0, d, d);
		const int32_t square = sub(INT32_MIN, e.storeClear(0));
		e.msac(0, 0x2000, square);
		e.msac(1, 0x7fff, falling);
		const int32_t levelA = e.storeClear(0);
		const int32_t levelB = e.storeClear(1);
		int32_t level = static_cast<int32_t>(be32(_io.state.data() + 8));
		const int32_t s = step(_io);
		if(s == 1)
		{
			e.macWord(0, 0x105e, false, 0x6400, false);
			level = e.storeClear(0);
			e.mac(0, level, level);
			level = e.storeClear(0);
			setCmd(_io, 0x18, 0x40007f7f);
			setCmd(_io, 0x1c, 0xf0100000);
			const int32_t decay = entry(Lookup::CymbalMetallicDecay, byteParam(_io, 8));
			const int32_t ticks = decayTicks(e, 0x36000000, decay);
			setCmd(_io, 0x10, 0x7f00);
			setCmd(_io, 0x14, (static_cast<uint32_t>(decay) & 0xfffff) | 0xf0100000);
			const auto n = static_cast<uint32_t>(countdownNibble(_io)) << 24;
			const int32_t releaseWord = entry(Lookup::CymbalMetallicRelease, byteParam(_io, 4));
			const auto release = static_cast<uint32_t>(releaseWord) & 0xfffff;
			setCmd(_io, 0x08, 0x7f7f);
			setCmd(_io, 0x0c, n | 0xf0100000 | release);
			setCountdown(_io, ticks);
			setStep(_io, 2);
		}
		else if(s == 2)
		{
			if(due(_io))
			{
				const auto held = static_cast<int32_t>(cmdLong(_io, 0x14) & 0xfffff);
				const int32_t release = std::max(entry(Lookup::CymbalMetallicRelease, byteParam(_io, 4)), held);
				setCmd(_io, 0x18, IdleLevel);
				setCmd(_io, 0x1c, 0xf0100000 | (static_cast<uint32_t>(release) & 0xfffff));
				setCmd(_io, 0x10, 0);
				const auto n = static_cast<uint32_t>(countdownNibble(_io)) << 24;
				setCmd(_io, 0x14, n | 0x901e4b06);
				setCmd(_io, 0x08, 0x7f00);
				setCmd(_io, 0x0c, n | 0xf01feeee);
				setStep(_io, 3);
			}
		}
		else if(s == 0)
			setStep(_io, 1);
		setFrameWord(_io, Commands + 0x10, levelB);
		setFrameWord(_io, Commands + 0x08, levelA);
		releaseCommand(_io, 0x14);
		releaseCommand(_io, 0x0c);
		setOscillators(e, _io, Rates + 0x18, 2, wordParam(_io, 1), _io.pitch, CymbalMetallic, 0x46666666, 0);
		// the attack's level falls to 5/8 a block, a line across the block
		e.mac(0, 0x50000000, level);
		const int32_t stepDown = asr(sub(e.storeClear(0), level), 5);
		setBe32(_io.state.data() + 8, static_cast<uint32_t>(fall(_io, level, stepDown, 32)));
		clearHead(_io);
		setNoise(_io, 0, 0, 0, 0);
	}

	void bellMetallic(VoiceIo& _io)
	{
		Emac e(_io.saturate);
		setTopology(_io, 6);
		bell(_io, e, Lookup::BellMetallicDecay, 0x4ccccccc);
		const int32_t tune = (wordParam(_io, 1) - 0x4000) / 2 + 0x4000;
		bellOscillators(e, _io, tune, wordParam(_io, 3) + tune);
		setLevel(e, _io, 0x1ef9db22);
		clearHead(_io);
		silence(_io);
		setNoise(_io, 0, 0, 0, 0);
	}

	void ride(VoiceIo& _io)
	{
		Emac e(_io.saturate);
		setTopology(_io, 3);
		silence(_io);
		setLevel(e, _io, 0x1126e978);
		// three knobs about a quarter: the stages' levels
		constexpr int32_t KnobScale = 0x78f1e3c7;
		constexpr int32_t Quarter = 0x10000000;
		const auto offset = [](const int32_t _a, const int32_t _b)
		{
			const int64_t sum = static_cast<int64_t>(_a) + _b;
			return sats(add(_a, _b), sum > INT32_MAX || sum < INT32_MIN);
		};
		e.mac(0, KnobScale, lsl(wordParam(_io, 6), 16));
		const int32_t levelA = offset(Quarter, e.storeClear(0));
		e.mac(0, KnobScale, lsl(wordParam(_io, 5), 16));
		const int32_t knobB = e.storeClear(0);
		e.mac(0, KnobScale, lsl(wordParam(_io, 7), 16));
		const int32_t levelC = offset(Quarter, e.storeClear(0));
		const int32_t levelB = offset(Quarter, knobB);
		const int32_t release = entry(Lookup::RideRelease, byteParam(_io, 8));
		const int32_t decay = entry(Lookup::RideDecay, byteParam(_io, 4));
		const int32_t bell = entry(Lookup::RideBell, byteParam(_io, 4));
		int32_t restart = 0;
		const int32_t s = step(_io);
		if(s == 0)
		{
			if(due(_io))
			{
				setFrameWord(_io, Cv, 0);
				setCountdown(_io, add(countdown(_io), 0x10));
				setStep(_io, 1);
				restart = 1;
			}
		}
		else if(s == 1)
		{
			if(due(_io))
			{
				// the stick's click: a short envelope in outA, up, held, down
				e.mac(0, 0x0d916872, 0x6400);
				int32_t peak = e.storeClear(0);
				peak = static_cast<int32_t>(static_cast<uint32_t>(peak) * static_cast<uint32_t>(peak));
				peak = add(peak, peak);
				line(_io, 0, 0, peak, 8);
				line(_io, 8, peak, peak, 8);
				line(_io, 16, peak, 0, 16);
				*cmd(_io, 0x1a) = 0x7f;
				*cmd(_io, 0x1b) = 0x7f;
				const uint8_t b1d = *cmd(_io, 0x1d);
				*cmd(_io, 0x1c) = 0xf0;
				setFrameWord(_io, Commands + 0x18, 0x7fff);
				*cmd(_io, 0x1d) = static_cast<uint8_t>((b1d & 0xf) | 0x10);
				setRate(_io, 0x1c, bell);
				setFrameWord(_io, Commands + 0x10, 0x4000);
				setFrameWord(_io, Commands + 0x08, 0x4000);
				*cmd(_io, 0x12) = static_cast<uint8_t>((levelB >> 23) - 0x80);
				setFrameWord(_io, Commands + 0x00, 0x4000);
				*cmd(_io, 0x14) = 0xf0;
				*cmd(_io, 0x13) = 0x80;
				setNibble(_io, 0x15, 0x10);
				setRate(_io, 0x14, release);
				*cmd(_io, 0x0a) = static_cast<uint8_t>((levelA >> 24) - 0x80);
				*cmd(_io, 0x0c) = 0xf0;
				*cmd(_io, 0x0b) = 0x80;
				setNibble(_io, 0x0d, 0x10);
				setRate(_io, 0x0c, decay);
				*cmd(_io, 0x02) = static_cast<uint8_t>((levelC >> 23) - 0x80);
				*cmd(_io, 0x04) = 0xf0;
				*cmd(_io, 0x03) = 0x80;
				setNibble(_io, 0x05, 0x10);
				setRate(_io, 0x04, bell);
				setStep(_io, 2);
				setCountdown(_io, decayTicks(e, 0x20000000, release));
			}
		}
		else if(s == 2)
		{
			if(due(_io))
			{
				const uint8_t b1d = *cmd(_io, 0x1d);
				*cmd(_io, 0x1b) = 0x80;
				*cmd(_io, 0x1c) = 0xb0;
				setFrameWord(_io, Commands + 0x10, 0x4000);
				setFrameWord(_io, Commands + 0x08, 0x4000);
				*cmd(_io, 0x1d) = static_cast<uint8_t>((b1d & 0xf) | 0x10);
				setRate(_io, 0x1c, bell);
				*cmd(_io, 0x13) = 0x80;
				setNibble(_io, 0x14, 0xb0);
				*cmd(_io, 0x0b) = 0x80;
				setNibble(_io, 0x15, 0x10);
				setRate(_io, 0x14, release);
				setNibble(_io, 0x0c, 0xb0);
				setNibble(_io, 0x0d, 0x10);
				setRate(_io, 0x0c, decay);
			}
		}
		else
			setStep(_io, -1);
		// the ride's four oscillator banks: the classic, the basic hat's and the metallic one twice
		const int bank = std::clamp(static_cast<int>(byteParam(_io, 6)), 0, 3);
		static constexpr const Six* banks[4] = {&Classic, &HatBasic, &Metallic, &Metallic};
		setOscillators(e, _io, Rates, bank == 3 ? 2 : 1, wordParam(_io, 1), _io.pitch, *banks[bank], 0x40000000,
			restart);
		clearHead(_io);
		setNoise(_io, 0, 0, 0, 0);
	}

	void hatBasic(VoiceIo& _io)
	{
		Emac e(_io.saturate);
		setTopology(_io, 0);
		setTuneCv(_io);
		setLevel(e, _io, 0x445a1cac);
		const int8_t k = byteParam(_io, 14);
		const int32_t feedback = k == 0x7f ? INT32_MIN : sub(0, entry(Lookup::HatBasicFeedback, k));
		int32_t level = static_cast<int32_t>(be32(_io.state.data() + 8));
		int32_t restart = 0;
		const int32_t s = step(_io);
		if(s == 0)
		{
			restart = byteParam(_io, 10) != 0 ? 1 : 0;
			idle(_io, 0x10, 0x08);
			setStep(_io, 1);
		}
		else if(s == 1)
		{
			setBe32(_io.heads.data() + 4, 0);
			level = velocitySquare(e, 0xb4d, 0x6400);
			setCmd(_io, 0x18, IdleLevel);
			setCmd(_io, 0x1c, 0xf01f3ed8);
			const auto rate = static_cast<uint32_t>(entry(Lookup::HatBasicDecay, byteParam(_io, 8)));
			const uint32_t decay = (rate & 0xfffff) | 0xf0100000;
			setCmd(_io, 0x10, IdleLevel);
			setCmd(_io, 0x14, decay);
			setCountdown(_io, decayTicks(e, 0x25000000, static_cast<int32_t>(decay & 0xfffff)));
			setStep(_io, 2);
		}
		else
		{
			bool release = true;
			if(s == 2)
			{
				if(due(_io))
				{
					setLowNibble(_io, 0x14, countdownNibble(_io));
					setStep(_io, 3);
				}
				else
					release = false;
			}
			if(release)
			{
				setNibble(_io, 0x14, 0x90);
				setRate(_io, 0x14, std::max(entry(Lookup::HatBasicRelease, byteParam(_io, 4)), 0x7fa74));
				copyStage(_io, 0x18, 0x10);
			}
		}
		copyStage(_io, 0x08, 0x10);
		setOscillators(e, _io, Rates, 1, wordParam(_io, 1), _io.pitch, HatBasic, 0x40000000, restart);
		// the noise's feedback: what the generator left times the tone knob
		e.msac(0, feedback, static_cast<int32_t>(be32(_io.heads.data() + 4)));
		setBe32(_io.heads.data(), static_cast<uint32_t>(e.storeClear(0)));
		setBe32(_io.state.data() + 8, static_cast<uint32_t>(fall(_io, level, asr(sub(0, level), 4), 16)));
		setNoise(_io, 0x1e, 0x2c, 0x56, 0x30);
	}

	void hatLab(VoiceIo& _io)
	{
		Emac e(_io.saturate);
		setTopology(_io, 1);
		e.mac(0, 0xc309, 0x1f802e29);
		setFrameWord(_io, Cv + 6, e.storeClear(0));
		setLevel(e, _io, 0x445a1cac);
		const int32_t s = step(_io);
		const int32_t restart = s == 0 ? 1 : 0;
		if(s == 0)
		{
			idle(_io, 0x10, 0x08);
			setStep(_io, 1);
		}
		else if(s == 1)
		{
			const int8_t k = byteParam(_io, 4);
			const int32_t decay = k == 0x7f ? 0xfffff : entry(Lookup::HatLabDecay, k) & 0xfffff;
			setCmd(_io, 0x18, IdleLevel);
			setCmd(_io, 0x1c, 0xf0100000 | static_cast<uint32_t>(decay));
			setCmd(_io, 0x10, IdleLevel);
			setCmd(_io, 0x14, 0xf0100000 | static_cast<uint32_t>(decay));
			setStep(_io, 2);
			setCountdown(_io, decayTicksDefault(e, decay));
		}
		else if(s == 2 && due(_io))
		{
			setLowNibble(_io, 0x14, countdownNibble(_io));
			setStep(_io, 3);
		}
		copyStage(_io, 0x08, 0x10);
		// six oscillators tuned by their own knobs, a hertz per two steps of the word
		Six hz;
		const int words[6] = {1, 3, 4, 5, 6, 7};
		for(int i = 0; i < 6; ++i)
		{
			const int32_t x = asr(lsl(wordParam(_io, words[i]) >> 1, 8), 2);
			hz[static_cast<size_t>(i)] = static_cast<int32_t>(
				(static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32) / (48000u * 512u));
		}
		setOscillators(e, _io, Rates, 1, 0x4000, sub(_io.pitch, 12 << 16), hz, 0x40000000, restart);
		silence(_io);
		std::fill_n(_io.heads.begin(), 8, uint8_t(0));
		setNoise(_io, 0, 0, 0, 0);
	}

	void setupMetal(const Machine _m, VoiceIo& _io)
	{
		const auto set = [&](const int _off, const int32_t _v) { setFrameWord(_io, _off, _v); };
		if(!isMetal(_m))
			return;		// a utility on this voice leaves its topology
		set(Cv + 8, 0);
		switch(_m)
		{
		case Machine::CymbalClassic:
		case Machine::Ride:
			set(Setup + 0x0, 0xfc00);
			set(Setup + 0x2, 0);
			set(Setup + 0xe, 0);
			set(Setup + 0xa, 0);
			set(Setup + 0x6, 0);
			set(Cv + 6, 0);
			set(Cv + 4, 0);
			set(Cv + 2, 0);
			set(Setup + 0xc, 0x4000);
			set(Setup + 0x8, 0x4000);
			set(Setup + 0x4, 0x4000);
			break;
		case Machine::BellClassic:
			set(Setup + 0x2, 0);
			set(Setup + 0x0, 0xfc00);
			set(Setup + 0xe, 0);
			set(Setup + 0xa, 0);
			set(Setup + 0x6, 0);
			set(Cv + 6, 0);
			set(Cv + 4, 0);
			set(Setup + 0xc, 0x4000);
			set(Setup + 0x8, 0x4000);
			set(Setup + 0x4, 0x4000);
			set(Cv + 2, 0x8000);
			break;
		case Machine::CymbalMetallic:
			set(Setup + 0x0, 0x4000);
			set(Setup + 0xc, 0x4000);
			set(Setup + 0x8, 0x4000);
			set(Setup + 0x2, 0);
			set(Setup + 0xe, 0);
			set(Setup + 0xa, 0);
			set(Cv + 6, 0x8000);
			set(Cv + 4, 0x8000);
			break;
		case Machine::BellMetallic:
			set(Setup + 0xe, 0);
			set(Setup + 0xc, 0x4000);
			set(Cv + 6, 0);
			break;
		case Machine::ClosedClassic:
		case Machine::OpenClassic:
		case Machine::HatLab:
			set(Setup + 0x0, 0xfc00);
			set(Setup + 0x2, 0);
			set(Setup + 0xc, 0x4000);
			set(Setup + 0xe, 0);
			set(Setup + 0x8, 0x2800);
			set(Setup + 0xa, 0xd000);
			set(Cv + 6, 0);
			set(Cv + 4, 0);
			break;
		case Machine::ClosedMetallic:
		case Machine::OpenMetallic:
			set(Setup + 0x0, 0x4000);
			set(Setup + 0x2, 0);
			set(Setup + 0xc, 0xce80);
			set(Setup + 0xe, 0xff00);
			set(Cv + 6, 0);
			break;
		case Machine::HatBasic:
			set(Setup + 0x0, 0xfc00);
			set(Setup + 0x2, 0);
			set(Setup + 0xc, 0x2800);
			set(Setup + 0xe, 0xd000);
			set(Cv + 6, 0);
			set(Setup + 0x8, 0x2800);
			set(Setup + 0xa, 0xd000);
			set(Cv + 4, 0);
			break;
		default:
			break;
		}
	}
}
