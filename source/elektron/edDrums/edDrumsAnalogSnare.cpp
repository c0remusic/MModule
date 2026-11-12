// The snares' and rims' control (edDrumsAnalogPort.h): snareHard, snareClassic, snareFm, snareNatural, snareAcoustic,
// rimHard, rimClassic, each one block of tracks 9-11's voice as the main CPU computes it.

#include "edDrumsAnalogPort.h"

#include <algorithm>
#include <array>
#include <iterator>
#include <cmath>

namespace elektron::drums::analog::port
{
	// The tables these routines read by a knob's byte. A byte above 127 reads, as the CPU does, the end of the table
	// before in memory: each function below takes the signed byte and gives both. Where no formula is exact the table is
	// a fit, registered with its bound (snareTables): those tables look tuned by hand around a curve. Two machines play
	// a recording where the original holds one; ours are made here. Everything is built at static initialisation
	namespace tables
	{
		constexpr double Pi = 3.14159265358979323846;
		// The fits' largest |fit - original| over each table, in its words' units (measured against the original)
		constexpr int64_t ClassicDecayBound = 2, ClassicBelowBound = 3, NaturalRateBound = 2, NaturalDecayBound = 1741,
			AcousticAttackBound = 5, AcousticRateBound = 1, AcousticDecayBound = 185;

		// The house samples' shapes, chosen so that their peak, its place, spectral centroid, decay time and RMS come
		// near those measured on the original's: the attack's peak 0.52 at sample 5, centroid 1.5 kHz, a decay of 11
		// samples; the body's peak 30666, centroid 3.7 kHz, an RMS decay of 1666 samples
		constexpr double AttackPeak = 0.7958158519775586, AttackDecay = 14.625006870130738,
			AttackCycle = 0.03464380829678763, AttackPhase = 3.2388839134533294;
		constexpr double BodyPeak = 30666, BodyDecay = 1794.4951718278955, BodyAttack = 14.447455179029754,
			BodyTone = 145.50770229676152, BodyToneDecay = 209.96773546411978, BodyToneLevel = 0.7429476067199915,
			BodyNoiseColour = 0.36278402329604814;

		// Clenshaw's sum of a Chebyshev series on x = k / 127 * 2 - 1
		template<size_t N> double series(const std::array<double, N>& _c, const int _k)
		{
			const double x = _k / 127.0 * 2 - 1;
			double b1 = 0, b2 = 0;
			for(size_t i = N - 1; i > 0; --i)
			{
				const double b = 2 * x * b1 - b2 + _c[i];
				b2 = b1;
				b1 = b;
			}
			return x * b1 - b2 + _c[0];
		}

		// An envelope rate word from its per-tick fall _d: the fall's octave in bits 16-19, its mantissa below, as
		// decayTicks reads one
		int32_t rateWord(const double _d)
		{
			const int s = static_cast<int>(std::floor(-std::log2(_d)));
			const auto m = std::min<long long>(std::llround((1 - std::ldexp(_d, s)) * 131072), 0xffff);
			return s << 16 | static_cast<int32_t>(m);
		}

		// A Q31 per-block decay coefficient for a time constant of _t blocks
		int32_t coefficient(const double _t)
		{
			return static_cast<int32_t>(std::min<long long>(std::llround(std::exp(-1 / _t) * 2147483648.0), INT32_MAX));
		}

		template<typename F> std::array<int32_t, 128> make(F _f)
		{
			std::array<int32_t, 128> r{};
			for(int k = 0; k < 128; ++k)
				r[static_cast<size_t>(k)] = _f(k);
			return r;
		}

		// snareClassic's decay: a time constant of 34.2 (r^k - 1) blocks
		std::array<int32_t, 128> g_classicDecay = make([](const int _k)
		{
			return _k == 0 ? 0 : coefficient(34.19854820397764 * (std::pow(1.011316042920896, _k) - 1));
		});

		// The rates below it: a fall of about 1/(262 k) per tick, a Chebyshev series of log2 of k times the fall; 0 at 0
		std::array<int32_t, 128> g_classicBelow = make([](const int _k)
		{
			static constexpr std::array<double, 24> c = {-8.710070466820946, -0.4332458968874791, 0.27362900252345446,
				-0.0008881653805073461, -0.01752139330865932, 0.001977188268332674, 0.000280183119064327,
				0.0006442285967078137, -0.0007547278980265148, 0.0005838981956113661, -0.0004815080507827297,
				0.0004134895645042001, -0.00034745013956887633, 0.0002855807207049074, -0.000238160052030782,
				0.0001905841866027809, -0.00015132726024613222, 0.0001171615798709007, -8.822483093860769e-05,
				7.108777822345456e-05, -4.814289375834193e-05, 3.288130608258811e-05, -1.765933074212265e-05,
				1.0762462721874034e-05};
			return _k == 0 ? 0 : rateWord(std::exp2(series(c, _k)) / _k);
		});

		// snareNatural's envelope rate, a series of log2 of the fall
		std::array<int32_t, 128> g_naturalRate = make([](const int _k)
		{
			static constexpr std::array<double, 10> c = {-9.11960887052854, -3.3337730657833737, -0.24872346389389302,
				0.0907301646048635, -0.005526779737817933, -0.001657752398609152, 0.0015843610017046103,
				-0.0001991668697876848, -5.967804343851318e-05, 4.041349249136163e-05};
			return rateWord(std::exp2(series(c, _k)));
		});

		// snareNatural's decay: a time constant of 59.19 r^k - 53.76 blocks
		std::array<int32_t, 128> g_naturalDecay = make([](const int _k)
		{
			return coefficient(-53.75937821479987 + 59.18807862964724 * std::pow(1.016750605773003, _k));
		});

		// snareAcoustic's attack rate
		std::array<int32_t, 128> g_acousticAttack = make([](const int _k)
		{
			static constexpr std::array<double, 11> c = {-7.5136740557842225, -2.8673000723449626, -0.4286604081575666,
				0.08066449174596123, 0.009914283824919368, -0.00722331512522754, 0.0006989991597320139,
				0.000587492217479406, -0.0001728284534118801, -2.6288151584757018e-05, 2.4010794314543277e-05};
			return rateWord(std::exp2(series(c, _k)));
		});

		// snareAcoustic's decay rate
		std::array<int32_t, 128> g_acousticRate = make([](const int _k)
		{
			static constexpr std::array<double, 13> c = {-9.974642311315208, -2.042174718820428, -0.5133228023709964,
				0.02054886388588957, 0.03326295306891717, -0.0006173660651385512, -0.0026382260185108717,
				0.00021155714169680164, 0.0002924533685694456, -2.917282036726588e-05, -3.354222280132076e-05,
				4.907288168706216e-06, 3.703921074900152e-06};
			return rateWord(std::exp2(series(c, _k)));
		});

		// snareAcoustic's body decay: a time constant of 10.49 r^k + 22.08 blocks, the curve of snareNatural's decay
		std::array<int32_t, 128> g_acousticDecay = make([](const int _k)
		{
			return coefficient(22.08297746592674 + 10.48910800745666 * std::pow(1.0167505398586925, _k));
		});

		// snareNatural's attack, 32 samples in Q31: a damped sine of our own, matched to the length, peak, centroid and
		// decay measured on the original (no word of it)
		std::array<int32_t, 32> g_naturalAttack = []
		{
			std::array<int32_t, 32> r{};
			for(int n = 0; n < 32; ++n)
			{
				const double wave = std::sin(AttackPhase + 2 * Pi * AttackCycle * n);
				const double v = AttackPeak * std::exp(-n / AttackDecay) * wave;
				r[static_cast<size_t>(n)] = static_cast<int32_t>(std::llround(v * 2147483648.0));
			}
			return r;
		}();

		// snareAcoustic's body, 4800 samples at 48 kHz: a drum tone and a snare's lowpassed noise under one decay, of our
		// own, matched to the length, peak, centroid and decay measured on the original (no word of it)
		constexpr int BodyLength = 4800;
		std::array<int16_t, BodyLength> g_acousticBody = []
		{
			std::array<double, BodyLength> x{};
			uint32_t seed = 0x2545f491u;
			double low = 0, peak = 0;
			for(int n = 0; n < BodyLength; ++n)
			{
				seed = seed * 1664525u + 1013904223u;
				const double white = static_cast<int32_t>(seed) / 2147483648.0;
				low += BodyNoiseColour * (white - low);
				const double tone = std::sin(2 * Pi * BodyTone * n / 48000.0) * std::exp(-n / BodyToneDecay);
				const double attack = 1 - std::exp(-n / BodyAttack);
				x[static_cast<size_t>(n)] = attack * (BodyToneLevel * tone + low * std::exp(-n / BodyDecay));
				peak = std::max(peak, std::fabs(x[static_cast<size_t>(n)]));
			}
			std::array<int16_t, BodyLength> r{};
			for(int n = 0; n < BodyLength; ++n)
			{
				const double v = x[static_cast<size_t>(n)] / peak * BodyPeak;
				r[static_cast<size_t>(n)] = static_cast<int16_t>(std::lround(v));
			}
			return r;
		}();

		int32_t classicDecay(const int _k)
		{
			return _k >= 0 ? g_classicDecay[size_t(_k)] : g_classicBelow[size_t(128 + _k)];
		}
		// A byte from 0xe0 reads snareNatural's attack, from 0x80 the decay table before it
		int32_t naturalRate(const int _k)
		{
			if(_k >= 0)
				return g_naturalRate[size_t(_k)];
			return _k < -32 ? g_classicDecay[size_t(160 + _k)] : g_naturalAttack[size_t(32 + _k)];
		}
		// The attack's 32 samples; a position past them reads the rate table after it
		int32_t naturalAttack(const int _i)
		{
			return _i < 32 ? g_naturalAttack[size_t(_i)] : g_naturalRate[size_t(_i - 32)];
		}
		int32_t naturalDecay(const int _k)
		{
			return _k >= 0 ? g_naturalDecay[size_t(_k)] : g_naturalRate[size_t(128 + _k)];
		}
		int32_t acousticAttack(const int _k)
		{
			return _k >= 0 ? g_acousticAttack[size_t(_k)] : g_naturalDecay[size_t(128 + _k)];
		}
		int32_t acousticRate(const int _k)
		{
			return _k >= 0 ? g_acousticRate[size_t(_k)] : g_acousticAttack[size_t(128 + _k)];
		}
		int32_t acousticDecay(const int _k)
		{
			return _k >= 0 ? g_acousticDecay[size_t(_k)] : g_acousticRate[size_t(128 + _k)];
		}
		// A position past the body plays silence (the original reads memory there)
		int32_t acousticBody(const uint32_t _i) { return _i < BodyLength ? g_acousticBody[_i] : 0; }

		// The gain the first knob's byte puts on the voice's two upper CVs, negative in Q31: 10^(0.9 k/127) of 1/8
		// (k = 0..127, exact on every entry). Below it in memory, a straight segment that doubles its slope every 16
		// steps up to 2048 of its unit and ends on 0
		int32_t tune(const int _k)
		{
			static const std::array<int32_t, 256> t = []
			{
				++tablesBuilt();
				std::array<int32_t, 256> r{};
				for(int k = 0; k < 128; ++k)
				{
					const double gain = std::ldexp(std::pow(10.0, 0.9 * k / 127), 28);
					r[static_cast<size_t>(k)] = static_cast<int32_t>(-std::llround(gain));
				}
				constexpr int32_t unit = 0x2932e;
				for(int j = 0; j < 127; ++j)
				{
					int32_t v;
					if(j < 30)
						v = (j + 2) * unit;
					else
					{
						const int s = (j - 30) / 16;
						v = (16 + (j - 30 - 16 * s)) * unit << (s + 1);
					}
					r[static_cast<size_t>(128 + j)] = v;
				}
				r[255] = 0;
				return r;
			}();
			return t[static_cast<size_t>(_k & 0xff)];
		}
	}

	namespace
	{
		uint16_t uword(const VoiceIo& _io, const int _index) { return be16(_io.params.data() + 2 * _index); }
		int16_t word(const VoiceIo& _io, const int _index) { return static_cast<int16_t>(uword(_io, _index)); }
		int8_t sbyte(const VoiceIo& _io, const int _offset)
		{
			return static_cast<int8_t>(_io.params[static_cast<size_t>(_offset)]);
		}

		void setSwitch(VoiceIo& _io, const int _k, const bool _on)
		{
			if(_on)
				_io.switches |= 1u << _k;
			else
				_io.switches &= ~(1u << _k);
		}

		int32_t clampCv(const int32_t _v) { return _v < 0 ? 0 : _v > 0x7fff ? 0x7fff : _v; }

		// An oscillator's pitch CV: the exponential of the pitch word at _scale, bounded to the CV's 15 bits
		int32_t pitchCv(Emac& _e, const int32_t _tune, const int32_t _note, const int32_t _scale,
			const bool _modulator = false)
		{
			const int32_t w = _modulator ? modPitchWord(_e, _tune, _note) : pitchWord(_e, _tune, _note);
			const auto x = static_cast<int32_t>(expPitch(_e, w));
			_e.mac(0, _scale, add(x, 1));
			return clampCv(_e.storeClear(0));
		}

		// A CV trimmed by the first knob's byte (the voice's tune table): its 16 bits << 3 at the table's negative gain
		int32_t tuned(Emac& _e, const VoiceIo& _io, const int32_t _cv)
		{
			_e.msac(0, lsl(_cv & 0xffff, 3), tables::tune(sbyte(_io, 0)));
			return _e.storeClear(0);
		}

		// muls.l: the low 32 bits of the product
		int32_t mul(const int32_t _a, const int32_t _b)
		{
			return static_cast<int32_t>(static_cast<uint32_t>(static_cast<int64_t>(_a) * _b));
		}

		int32_t product(Emac& _e, const int32_t _y, const int32_t _x)
		{
			_e.mac(0, _y, _x);
			return _e.storeClear(0);
		}

		// add.l then sats.l
		int32_t addSat(const int32_t _a, const int32_t _b)
		{
			const int32_t r = add(_a, _b);
			return sats(r, ((_a ^ r) & (_b ^ r)) < 0);
		}

		// The calibration's two level trims, zero on an untrimmed unit
		void levels(VoiceIo& _io)
		{
			std::fill_n(_io.frame.begin() + frame::Level, 8, uint8_t(0));
		}

		// The voice's click: outA ramps from _level by its decay in a block, which returns the level the next block
		// starts from
		int32_t click(Emac& _e, VoiceIo& _io, const int32_t _level)
		{
			const int32_t step = asr(sub(product(_e, 0x15032885, _level), _level), 5);
			int32_t v = _level;
			for(int i = 0; i < 32; ++i)
			{
				setBe32(_io.outA.data() + 4 * i, static_cast<uint32_t>(v));
				v = add(v, step);
			}
			return add(_level, lsl(step, 5));
		}

		void fillOutA(VoiceIo& _io, const int32_t _v)
		{
			for(int i = 0; i < 32; ++i)
				setBe32(_io.outA.data() + 4 * i, static_cast<uint32_t>(_v));
		}

		void setNoise(VoiceIo& _io, const uint8_t _a, const uint8_t _b, const uint8_t _c, const uint8_t _d)
		{
			_io.noise = {_a, _b, _c, _d};
		}

		uint8_t* env(VoiceIo& _io) { return _io.frame.data() + frame::Envelope; }

		// A 20-bit field in the low bits of an envelope long
		void setField(uint8_t* _p, const int32_t _v)
		{
			setBe32(_p, (be32(_p) & 0xfff00000u) | (static_cast<uint32_t>(_v) & 0xfffff));
		}
		void setNibble(uint8_t* _p, const uint8_t _high) { *_p = static_cast<uint8_t>((*_p & 0x0f) | _high); }
		void copy8(uint8_t* _env, const int _from, const int _to) { std::copy_n(_env + _from, 8, _env + _to); }

		// The four envelope words every block clears
		void clearEnvelopeWords(VoiceIo& _io)
		{
			uint8_t* e = env(_io);
			setBe16(e + 0x00, 0);
			setBe16(e + 0x10, 0);
			setBe16(e + 0x18, 0);
			setBe16(e + 0x08, 0);
		}

		int32_t quotient(const int32_t _num, const int32_t _den, const int32_t _old)
		{
			// divs.l by zero traps and leaves its destination; a knob word of 0xfff0 or more gives it
			return _den == 0 ? _old : _num / _den;
		}

		// The state's longs
		int32_t sget(const VoiceIo& _io, const int _i) { return static_cast<int32_t>(be32(_io.state.data() + 4 * _i)); }
		void sset(VoiceIo& _io, const int _i, const int32_t _v)
		{
			setBe32(_io.state.data() + 4 * _i, static_cast<uint32_t>(_v));
		}

		int32_t hget(const VoiceIo& _io, const int _i) { return static_cast<int32_t>(be32(_io.heads.data() + 4 * _i)); }
		void hset(VoiceIo& _io, const int _i, const int32_t _v)
		{
			setBe32(_io.heads.data() + 4 * _i, static_cast<uint32_t>(_v));
		}

		// The attack segment's opening of snareHard and rimHard: both segments rise at 0x4000 from 0x7f, the decay's
		// rate from the knob _decay, the release's from _release
		void hardEnvelope(Emac& _e, VoiceIo& _io, const int16_t _decay, const int16_t _release)
		{
			uint8_t* e = env(_io);
			e[0x1c] = 0xf0;
			e[0x1b] = 0x80;
			setBe16(e + 0x18, 0x4000);
			e[0x1a] = 0x7f;
			setNibble(e + 0x1d, 0x10);
			const int32_t d = lsl(_decay, 5) & 0xfffff;
			const uint32_t high = be32(e + 0x1c) & 0xfff00000u;
			e[0x13] = 0x80;
			setBe16(e + 0x10, 0x4000);
			setBe16(e + 0x00, 0x4000);
			setBe32(e + 0x1c, high | static_cast<uint32_t>(d));
			e[0x12] = 0x7f;
			e[0x14] = 0xf0;
			setNibble(e + 0x15, 0x10);
			setField(e + 0x14, product(_e, 0x770a3d70, d));
			e[0x03] = 0;
			e[0x04] = 0xf0;
			setNibble(e + 0x05, 0x10);
			setField(e + 0x04, lsl(_release, 5));
			copy8(e, 0, 8);
			e[0x02] = _io.params[6];
			e[0x0a] = _io.params[6];
		}

		// The attack of snareClassic and snareFm: the first segment at 0x4000, the two rates _first and _second
		void classicEnvelope(VoiceIo& _io, const int32_t _first, const int32_t _second, const uint8_t _level,
			const bool _fm)
		{
			uint8_t* e = env(_io);
			e[0x13] = 0x80;
			if(_fm)
			{
				e[0x12] = 0x7f;
				setBe16(e + 0x10, 0x4000);
			}
			else
			{
				setBe16(e + 0x10, 0x4000);
				e[0x12] = 0x7f;
			}
			e[0x14] = 0xf0;
			setNibble(e + 0x15, 0x10);
			if(_fm)
				e[0x04] = 0xf0;
			setField(e + 0x14, lsl(add(_first, 0x3f80), 5));
			copy8(e, 0x10, 0x18);
			setBe16(e + 0x00, 0x4000);
			setField(e + 0x1c, lsl(add(_second, 0x3f80), 5));
			e[0x03] = 0;
			e[0x02] = _level;
			e[0x04] = 0xf0;
			setNibble(e + 0x05, 0x10);
			setBe32(e + 0x04, (be32(e + 0x04) & 0xfff00000u) | 0x40000);
			copy8(e, 0, 8);
		}
	}

	void snareHard(VoiceIo& _io)
	{
		Emac e(_io.saturate);
		const int32_t s0 = sget(_io, 0);
		int32_t s1 = sget(_io, 1);
		int32_t fall = sget(_io, 2);
		int32_t level = sget(_io, 3);
		uint8_t* cv = _io.frame.data() + frame::Cv;

		if(s0 >= 0)
		{
			const int32_t a = pitchCv(e, word(_io, 1), _io.pitch, 0xff000);
			const int32_t b = pitchCv(e, word(_io, 1), _io.pitch, 0xaa000);
			setBe16(cv + 0, static_cast<uint32_t>(a));
			setBe16(cv + 2, static_cast<uint32_t>(b));
			levels(_io);
		}
		else
		{
			setBe16(cv + 0, 0);
			setBe16(cv + 2, 0);
		}
		clearEnvelopeWords(_io);
		setSwitch(_io, 6, false);
		setSwitch(_io, 1, true);
		setSwitch(_io, 5, false);
		setSwitch(_io, 8, false);
		setSwitch(_io, 2, true);
		setSwitch(_io, 7, false);
		setSwitch(_io, 4, true);
		if(s0 >= 0)
			setSwitch(_io, 3, false);
		setSwitch(_io, 9, true);

		const int32_t c2 = product(e, 0x4103, 0x14ffde44);
		const int32_t c3 = product(e, 0xc309, 0x14ffde44);
		setBe16(cv + 4, static_cast<uint32_t>(tuned(e, _io, c2)));
		setBe16(cv + 6, static_cast<uint32_t>(tuned(e, _io, c3)));

		if(s1 <= 0)
		{
			switch(s0)
			{
			case 0:
				setSwitch(_io, 0, true);
				s1 = 16;
				sset(_io, 0, 1);
				break;
			case 1:
				setSwitch(_io, 0, false);
				level = velocitySquare(e, word(_io, 4), 0x4952);
				hset(_io, 1, 0);
				hset(_io, 0, 0);
				hardEnvelope(e, _io, word(_io, 2), word(_io, 7));
				s1 = 16;
				sset(_io, 0, 2);
				break;
			case 2:
			{
				const int32_t d0 = asr(word(_io, 6), 1);
				fall = quotient(lsl(d0, 16), add(asr(word(_io, 5), 4), 1), fall);
				hset(_io, 1, product(e, lsl(d0, 15), 0x49520000));
				s1 = add(s1, decayTicksDefault(e, static_cast<int32_t>(be32(env(_io) + 0x1c) & 0xfffff)));
				sset(_io, 0, 3);
				break;
			}
			case 3:
				setSwitch(_io, 3, true);
				sset(_io, 0, -1);
				break;
			default:
				s1 = INT32_MAX;
				break;
			}
			sset(_io, 1, s1);
		}

		const int32_t next = click(e, _io, level);
		const int32_t h = sub(hget(_io, 1), fall);
		hset(_io, 0, h < 0 ? 0 : h);
		sset(_io, 2, fall);
		sset(_io, 3, next);
		setNoise(_io, 2, 31, 80, 115);
	}

	void snareClassic(VoiceIo& _io)
	{
		Emac e(_io.saturate);
		const int32_t s0 = sget(_io, 0);
		int32_t s1 = sget(_io, 1);
		int32_t decay = sget(_io, 2);
		int32_t level = sget(_io, 3);
		uint8_t* cv = _io.frame.data() + frame::Cv;

		if(s0 == 2)
		{
			hset(_io, 1, 0);
			hset(_io, 0, 0);
		}
		if(s0 >= 0)
		{
			const int32_t t1 = word(_io, 1);
			const int32_t detuned = addSat(t1, asr(word(_io, 3), 1));
			const int32_t a = pitchCv(e, t1, _io.pitch, 0xee000);
			const int32_t b = pitchCv(e, static_cast<int16_t>(detuned), _io.pitch, 0xee000);
			setBe16(cv + 0, static_cast<uint32_t>(a));
			setBe16(cv + 2, static_cast<uint32_t>(b));
			levels(_io);
		}
		else
		{
			setBe16(cv + 0, 0);
			setBe16(cv + 2, 0);
		}
		clearEnvelopeWords(_io);

		int32_t other;
		const int32_t k = knob(e, word(_io, 7), other);
		const int32_t scale = product(e, 0x1040c, 0xfc04000);
		e.msac(0, scale, other);
		const int32_t c3 = e.storeClear(0);
		e.msac(0, scale, k);
		const int32_t c2 = e.storeClear(0);
		setBe16(cv + 4, static_cast<uint32_t>(tuned(e, _io, c2)));
		setBe16(cv + 6, static_cast<uint32_t>(tuned(e, _io, c3)));

		setSwitch(_io, 6, false);
		setSwitch(_io, 1, true);
		setSwitch(_io, 5, true);
		setSwitch(_io, 8, false);
		setSwitch(_io, 2, true);
		setSwitch(_io, 7, true);
		setSwitch(_io, 4, true);
		if(s0 >= 0)
			setSwitch(_io, 3, false);
		setSwitch(_io, 9, true);

		const int32_t tone = asr(word(_io, 2), 1);
		const int32_t first = product(e, 0x40000000, tone);
		const int32_t second = product(e, 0x13333333, tone);

		if(s1 <= 0)
		{
			switch(s0)
			{
			case 0:
				setSwitch(_io, 0, true);
				s1 = 16;
				sset(_io, 0, 1);
				break;
			case 1:
				setSwitch(_io, 0, false);
				level = velocitySquare(e, 0x3f80, 0x3f80);
				hset(_io, 1, 0);
				hset(_io, 0, 0);
				classicEnvelope(_io, first, second, static_cast<uint8_t>(uword(_io, 4) >> 8), false);
				s1 = 16;
				sset(_io, 0, 2);
				break;
			case 2:
			{
				decay = product(e, 0x40810204, tables::classicDecay(sbyte(_io, 10)));
				decay = addSat(decay, decay);
				int32_t v = word(_io, 6) * word(_io, 6);
				v = add(v, v);
				hset(_io, 1, product(e, v, 0x3f800000));
				s1 = add(s1, decayTicksDefault(e, static_cast<int32_t>(be32(env(_io) + 0x14) & 0xfffff)));
				sset(_io, 0, 3);
				break;
			}
			case 3:
				setSwitch(_io, 3, true);
				sset(_io, 0, -1);
				break;
			default:
				s1 = INT32_MAX;
				break;
			}
			sset(_io, 1, s1);
		}

		hset(_io, 0, product(e, hget(_io, 1), decay));
		const int32_t next = click(e, _io, level);
		sset(_io, 2, decay);
		sset(_io, 3, next);
		setNoise(_io, 26, 34, 62, 50);
	}

	void snareFm(VoiceIo& _io)
	{
		Emac e(_io.saturate);
		int32_t fall = sget(_io, 2);
		const int32_t amount = product(e, 0x27904a79, product(e, 0x52a54a95, add(word(_io, 7), word(_io, 7))));
		int32_t level = sget(_io, 3);
		const int32_t s0 = sget(_io, 0);
		int32_t s1 = sget(_io, 1);
		uint8_t* cv = _io.frame.data() + frame::Cv;

		if(s0 == 2)
		{
			hset(_io, 1, 0);
			hset(_io, 0, 0);
		}
		if(s0 >= 0)
		{
			const int32_t a = pitchCv(e, word(_io, 1), _io.pitch, 0xee000);
			const int32_t b = pitchCv(e, word(_io, 3), _io.pitch, 0xee000, true);
			setBe16(cv + 0, static_cast<uint32_t>(a));
			setBe16(cv + 2, static_cast<uint32_t>(b));
			levels(_io);
		}
		else
		{
			setBe16(cv + 0, 0);
			setBe16(cv + 2, 0);
		}
		clearEnvelopeWords(_io);
		setBe16(cv + 6, static_cast<uint32_t>(amount));
		setBe16(cv + 4, static_cast<uint32_t>(tuned(e, _io, product(e, 0x1040c, 0xfc04000))));

		setSwitch(_io, 6, false);
		setSwitch(_io, 1, true);
		setSwitch(_io, 5, true);
		setSwitch(_io, 8, false);
		setSwitch(_io, 2, true);
		setSwitch(_io, 7, true);
		setSwitch(_io, 4, true);
		setSwitch(_io, 3, true);
		setSwitch(_io, 9, true);

		const int32_t first = product(e, 0x40000000, asr(word(_io, 2), 1));
		const int32_t second = product(e, 0x40000000, asr(word(_io, 4), 1));

		if(s1 <= 0)
		{
			switch(s0)
			{
			case 0:
				setSwitch(_io, 0, true);
				s1 = 16;
				sset(_io, 0, 1);
				break;
			case 1:
				setSwitch(_io, 0, false);
				level = velocitySquare(e, 0x3f80, 0x3f80);
				hset(_io, 1, 0);
				hset(_io, 0, 0);
				classicEnvelope(_io, first, second, 0x7f, true);
				s1 = 16;
				sset(_io, 0, 2);
				break;
			case 2:
			{
				const int32_t d0 = asr(word(_io, 6), 1);
				fall = quotient(lsl(d0, 16), add(asr(word(_io, 5), 4), 1), fall);
				hset(_io, 1, product(e, lsl(d0, 15), 0x3f800000));
				s1 = add(s1, decayTicksDefault(e, static_cast<int32_t>(be32(env(_io) + 0x14) & 0xfffff)));
				sset(_io, 0, 3);
				break;
			}
			case 3:
				sset(_io, 0, -1);
				break;
			default:
				s1 = INT32_MAX;
				break;
			}
			sset(_io, 1, s1);
		}

		const int32_t h = sub(hget(_io, 1), fall);
		hset(_io, 0, h < 0 ? 0 : h);
		const int32_t next = click(e, _io, level);
		sset(_io, 2, fall);
		sset(_io, 3, next);
		setNoise(_io, 2, 31, 80, 115);
	}

	void snareNatural(VoiceIo& _io)
	{
		Emac e(_io.saturate);
		uint8_t* cv = _io.frame.data() + frame::Cv;
		uint8_t* en = env(_io);
		int32_t s0 = sget(_io, 0);

		setSwitch(_io, 6, false);
		setSwitch(_io, 1, true);
		setSwitch(_io, 5, true);
		setSwitch(_io, 8, false);
		setSwitch(_io, 2, true);
		setSwitch(_io, 7, true);
		setSwitch(_io, 4, true);
		if(s0 >= 0)
			setSwitch(_io, 3, false);
		setSwitch(_io, 9, true);

		int32_t rising, falling;
		knobPair(e, word(_io, 4), rising, falling);
		int32_t rateStep = sget(_io, 4);
		int32_t rate = sget(_io, 3);
		int32_t decay = sget(_io, 2);
		uint32_t attack = static_cast<uint32_t>(sget(_io, 5));
		const int32_t velocity = velocitySquare(e, 0x7d00, 0x48c0);

		int32_t c1 = 0;
		if(s0 >= 0)
			c1 = pitchCv(e, word(_io, 1), _io.pitch, 0x82d1e);
		setBe16(cv + 2, static_cast<uint32_t>(c1));
		setBe16(cv + 0, 0);
		levels(_io);

		int32_t full = product(e, 0x40810204, 0x48c00000);
		full = static_cast<int32_t>(static_cast<uint32_t>(addSat(full, full)) & 0xffff0000u);
		e.msac(0, asr(rising, 16), full);
		const int32_t snap = e.storeClear(0);
		e.msac(0, falling, full);
		int32_t body = asr(e.storeClear(0), 16);
		body = body * body;
		const int32_t c3 = product(e, 0xc309, body);
		setBe16(cv + 4, 0);
		setBe16(cv + 6, static_cast<uint32_t>(tuned(e, _io, c3)));

		const int32_t s1 = sget(_io, 1);
		switch(static_cast<uint32_t>(s0))
		{
		case 0:
			setSwitch(_io, 0, true);
			hset(_io, 1, 0);
			hset(_io, 0, 0);
			setBe16(en + 8, 0);
			setNibble(en + 0xc, 0x90);
			attack = 0xffffffffu;
			sset(_io, 1, 16);
			sset(_io, 0, 1);
			break;
		case 1:
			if(s1 > 15)
				break;
			sset(_io, 1, 16);
			sset(_io, 0, 2);
			attack = 0;
			break;
		case 2:
			if(s1 > 15)
				break;
			decay = tables::naturalDecay(sbyte(_io, 6));
			hset(_io, 1, sub(0, add(mul(snap, snap), mul(snap, snap))));
			setSwitch(_io, 0, false);
			setBe32(en + 0x18, 0x40007f7f);
			rate = tables::naturalRate(sbyte(_io, 4));
			setBe32(en + 0x1c, (static_cast<uint32_t>(rate) & 0xfffff) | 0xf0100000u);
			rateStep = 0x40012599;
			sset(_io, 1, 32);
			sset(_io, 0, 3);
			break;
		case 3:
		{
			if(s1 > 15)
				break;
			int32_t r = product(e, rateStep, rate);
			r = addSat(r, r);
			if(r == rate)
			{
				sset(_io, 1, add(s1, decayTicksDefault(e, rate)));
				sset(_io, 0, 4);
			}
			setBe32(en + 0x18, 0x40000080);
			rate = r;
			setBe32(en + 0x1c, (static_cast<uint32_t>(r) & 0xfffff) | 0xb0100000u);
			break;
		}
		case 4:
			if(s1 > 15)
				break;
			setSwitch(_io, 3, true);
			sset(_io, 0, -1);
			break;
		default:
			sset(_io, 0, -1);
			break;
		}

		std::fill(_io.outA.begin(), _io.outA.begin() + 128, uint8_t(0));
		if(attack <= 31)
		{
			for(int i = 0; i < 32; ++i)
			{
				const int32_t a = tables::naturalAttack(static_cast<int>(attack) + i);
				setBe32(_io.outA.data() + 4 * i, static_cast<uint32_t>(product(e, velocity, a)));
			}
			attack += 32;
		}
		hset(_io, 0, product(e, decay, hget(_io, 1)));
		int32_t nextDecay = product(e, rateStep, 0x7ffe5c91);
		nextDecay = product(e, nextDecay, decay);
		const int32_t nextStep = sub(rateStep, product(e, 0x126e978, add(rateStep, static_cast<int32_t>(0xc0000000))));
		const uint16_t tone = uword(_io, 6);
		setNoise(_io, static_cast<uint8_t>(tone >> 8), static_cast<uint8_t>(tone >> 8),
			static_cast<uint8_t>(static_cast<uint32_t>(asr(word(_io, 5), 1)) >> 8),
			static_cast<uint8_t>(uword(_io, 7) >> 8));
		sset(_io, 2, addSat(nextDecay, nextDecay));
		sset(_io, 3, rate);
		sset(_io, 4, nextStep);
		sset(_io, 5, static_cast<int32_t>(attack));
	}

	void snareAcoustic(VoiceIo& _io)
	{
		Emac e(_io.saturate);
		uint8_t* cv = _io.frame.data() + frame::Cv;
		uint8_t* en = env(_io);
		const int32_t s0 = sget(_io, 0);

		setSwitch(_io, 9, true);
		setSwitch(_io, 6, false);
		setSwitch(_io, 1, true);
		setSwitch(_io, 5, true);
		setSwitch(_io, 8, false);
		setSwitch(_io, 2, true);
		setSwitch(_io, 7, false);
		setSwitch(_io, 4, true);
		if(s0 >= 0)
			setSwitch(_io, 3, false);

		int32_t decay = sget(_io, 2);
		int32_t release = sget(_io, 3);
		int32_t level = sget(_io, 6);
		int32_t rate = sget(_io, 4);
		uint32_t pos = static_cast<uint32_t>(sget(_io, 5));
		const int32_t velocity = velocitySquare(e, static_cast<int16_t>(product(e, 0x73333333, word(_io, 7))), 0x48c0);

		if(s0 >= 0)
		{
			const int32_t a = pitchCv(e, word(_io, 1), _io.pitch, 0x82d1e);
			const int32_t b = pitchCv(e, add(word(_io, 1), 0x700), _io.pitch, 0x82d1e);
			setBe16(cv + 0, static_cast<uint32_t>(a));
			setBe16(cv + 2, static_cast<uint32_t>(b));
			levels(_io);
		}

		int32_t full = product(e, 0x40810204, 0x48c00000);
		full = static_cast<int32_t>(static_cast<uint32_t>(addSat(full, full)) & 0xffff0000u);
		e.msac(0, add(asr(word(_io, 4), 1), 0x4000), full);
		const int32_t snap = e.storeClear(0);
		int32_t body = asr(full, 16);
		body = body * body;
		const int32_t c2 = product(e, 0xc309, body);
		const int32_t c3 = product(e, 0x2082, body);
		setBe16(cv + 4, static_cast<uint32_t>(tuned(e, _io, c2)));
		setBe16(cv + 6, static_cast<uint32_t>(tuned(e, _io, c3)));

		int32_t gain = INT32_MAX;
		bool play = false;
		bool check = true;
		const int32_t s1 = sget(_io, 1);
		switch(static_cast<uint32_t>(s0))
		{
		case 0:
			setSwitch(_io, 0, true);
			hset(_io, 1, 0);
			hset(_io, 0, 0);
			setBe16(en + 0, 0);
			level = INT32_MAX;
			setBe16(en + 8, 0);
			setNibble(en + 4, 0x90);
			setNibble(en + 0xc, 0x90);
			sset(_io, 1, 16);
			pos = 0xffffffffu;
			sset(_io, 0, 1);
			check = false;
			break;
		case 1:
			if(s1 > 15)
				break;
			pos = 0;
			sset(_io, 1, 16);
			sset(_io, 0, 2);
			play = true;
			check = false;
			break;
		case 2:
			if(s1 > 15)
				break;
			gain = 0x7eb851eb;
			decay = tables::acousticDecay(sbyte(_io, 6));
			hset(_io, 1, sub(0, add(mul(snap, snap), mul(snap, snap))));
			setSwitch(_io, 0, false);
			rate = tables::acousticRate(sbyte(_io, 4));
			release = product(e, 0x7d70a3d7, rate);
			setBe32(en + 0x10, 0x40007f00);
			setBe32(en + 0x14, (static_cast<uint32_t>(tables::acousticAttack(sbyte(_io, 10))) & 0xfffff) | 0xf0100000u);
			copy8(en, 0x10, 0x18);
			setBe32(en + 0x00, (static_cast<uint32_t>(word(_io, 6)) >> 2 & 0xff00) | 0x40000000u);
			setBe32(en + 0x04, 0xf0162b4e);
			copy8(en, 0, 8);
			sset(_io, 0, 3);
			sset(_io, 1, add(s1, asr(word(_io, 5), 4)));
			break;
		case 3:
			if(s1 > 15)
				break;
			setBe32(en + 0x10, 0x40000080);
			setBe32(en + 0x14, (static_cast<uint32_t>(release) & 0xfffff) | 0xb0100000u);
			copy8(en, 0x10, 0x18);
			setField(en + 0x1c, rate);
			sset(_io, 0, 4);
			sset(_io, 1, add(s1, decayTicks(e, 0x20c49b, rate)));
			break;
		case 4:
			break;
		default:
			sset(_io, 0, -1);
			break;
		}

		if(check)
			play = pos <= 0x12bf;
		if(play)
		{
			const int32_t amp = product(e, velocity, level);
			level = product(e, gain, level);
			// the CPU's word multiply takes each sample as the upper half of its operand
			for(int i = 0; i < 32; ++i)
			{
				e.macWord(i & 1, amp, true, lsl(tables::acousticBody(pos + static_cast<uint32_t>(i)), 16), true, true);
				setBe32(_io.outA.data() + 4 * i, static_cast<uint32_t>(e.storeClear(i & 1)));
			}
			pos += 32;
		}
		else
			std::fill(_io.outA.begin(), _io.outA.begin() + 128, uint8_t(0));

		hset(_io, 0, product(e, product(e, decay, hget(_io, 1)), gain));
		setNoise(_io, 0x0e, 0x0e, 0x37, 0x0f);
		sset(_io, 5, static_cast<int32_t>(pos));
		sset(_io, 2, decay);
		sset(_io, 3, release);
		sset(_io, 4, rate);
		sset(_io, 6, level);
	}

	void rimHard(VoiceIo& _io)
	{
		Emac e(_io.saturate);
		setSwitch(_io, 9, false);
		int32_t decay = sget(_io, 2);
		const int32_t s0 = sget(_io, 0);
		uint8_t* cv = _io.frame.data() + frame::Cv;

		if(s0 >= 0)
		{
			const int32_t a = pitchCv(e, word(_io, 1), _io.pitch, 0x683000);
			const int32_t b = pitchCv(e, word(_io, 1), _io.pitch, 0x1c7000);
			setBe16(cv + 0, static_cast<uint32_t>(a));
			setBe16(cv + 2, static_cast<uint32_t>(b));
			levels(_io);
		}
		clearEnvelopeWords(_io);
		setSwitch(_io, 6, false);
		setSwitch(_io, 1, true);
		setSwitch(_io, 5, true);
		setSwitch(_io, 8, false);
		setSwitch(_io, 2, true);
		setSwitch(_io, 7, false);
		setSwitch(_io, 4, true);
		if(s0 >= 0)
			setSwitch(_io, 3, false);

		const int32_t c2 = product(e, 0xad63, 0x7e03871);
		const int32_t c3 = product(e, 0x20808, 0x7e03871);
		setBe16(cv + 4, static_cast<uint32_t>(tuned(e, _io, c2)));
		setBe16(cv + 6, static_cast<uint32_t>(tuned(e, _io, c3)));

		int32_t fill = 0;
		int32_t s1 = sget(_io, 1);
		if(s1 <= 0)
		{
			switch(s0)
			{
			case 0:
				setSwitch(_io, 0, true);
				s1 = 16;
				sset(_io, 0, 1);
				break;
			case 1:
				setSwitch(_io, 0, false);
				fill = velocitySquare(e, word(_io, 4), 0x2ce7);
				decay = product(e, 0x40810204, 0x77a6521b);
				decay = addSat(decay, decay);
				{
					int32_t v = word(_io, 5) * word(_io, 5);
					v = add(v, v);
					hset(_io, 1, product(e, v, 0x2ce70000));
				}
				hardEnvelope(e, _io, word(_io, 2), word(_io, 7));
				s1 = 16;
				sset(_io, 0, 2);
				break;
			case 2:
				s1 = add(s1, decayTicksDefault(e, static_cast<int32_t>(be32(env(_io) + 0x1c) & 0xfffff)));
				sset(_io, 0, 3);
				break;
			case 3:
				setSwitch(_io, 3, true);
				sset(_io, 0, -1);
				break;
			default:
				s1 = INT32_MAX;
				break;
			}
			sset(_io, 1, s1);
		}

		fillOutA(_io, fill);
		sset(_io, 2, decay);
		hset(_io, 0, product(e, hget(_io, 1), decay));
		setNoise(_io, 2, 31, 64, 115);
	}

	void rimClassic(VoiceIo& _io)
	{
		Emac e(_io.saturate);
		setSwitch(_io, 9, false);
		int32_t decay = sget(_io, 2);
		const int32_t s0 = sget(_io, 0);
		uint8_t* cv = _io.frame.data() + frame::Cv;
		uint8_t* en = env(_io);

		if(s0 >= 0)
		{
			const int32_t a = pitchCv(e, word(_io, 4), _io.pitch, 0x683000);
			const int32_t b = pitchCv(e, word(_io, 1), _io.pitch, 0x1c7000);
			setBe16(cv + 0, static_cast<uint32_t>(a));
			setBe16(cv + 2, static_cast<uint32_t>(b));
			levels(_io);
		}
		setSwitch(_io, 6, false);
		setSwitch(_io, 1, true);
		setSwitch(_io, 5, true);
		setSwitch(_io, 8, false);
		setSwitch(_io, 2, true);
		setSwitch(_io, 7, true);
		setSwitch(_io, 4, true);
		if(s0 >= 0)
			setSwitch(_io, 3, false);

		int32_t other;
		const int32_t k = knob(e, word(_io, 3), other);
		const int32_t scale = product(e, 0x1040c, 0xfc04000);
		e.msac(0, scale, k);
		const int32_t c2 = e.storeClear(0);
		e.msac(0, scale, other);
		const int32_t c3 = e.storeClear(0);
		setBe16(cv + 4, static_cast<uint32_t>(tuned(e, _io, c2)));
		setBe16(cv + 6, static_cast<uint32_t>(tuned(e, _io, c3)));

		int32_t fill = 0;
		const int32_t s1 = sget(_io, 1);
		switch(s0)
		{
		case 0:
			if(s1 > 15)
				break;
			setSwitch(_io, 0, true);
			sset(_io, 1, add(s1, 16));
			sset(_io, 0, 1);
			break;
		case 1:
		{
			if(s1 > 15)
				break;
			fill = velocitySquare(e, word(_io, 7), 0x3f80);
			decay = product(e, 0x40810204, 0x77a6521b);
			decay = addSat(decay, decay);
			int32_t v = word(_io, 6) * word(_io, 6);
			v = add(v, v);
			hset(_io, 1, product(e, v, 0x3f800000));
			setSwitch(_io, 0, false);
			en[0x12] = 0x7f;
			const uint8_t b14 = en[0x14];
			en[0x13] = 0x80;
			en[0x14] = static_cast<uint8_t>(b14 | 0xf0);
			en[0x14] = static_cast<uint8_t>(0xf0 | s1);
			setNibble(en + 0x15, 0x10);
			const int32_t d = lsl(word(_io, 2), 5) & 0xfffff;
			setField(en + 0x14, d);
			copy8(en, 0x10, 0x18);
			sset(_io, 1, add(s1, decayTicksDefault(e, d)));
			sset(_io, 0, 2);
			break;
		}
		case 2:
			if(s1 > 15)
				break;
			sset(_io, 0, -1);
			setSwitch(_io, 3, true);
			break;
		default:
		{
			const uint32_t low = static_cast<uint32_t>(lsl(word(_io, 2), 5)) & 0xfffff;
			const uint32_t v = (be32(en + 0x14) & 0xfff00000u) | low;
			setBe32(en + 0x14, v);
			en[0x14] = static_cast<uint8_t>((v >> 24 & 0x0f) | 0x90);
			copy8(en, 0x10, 0x18);
			sset(_io, 0, -1);
			break;
		}
		}

		hset(_io, 0, product(e, hget(_io, 1), decay));
		sset(_io, 2, decay);
		fillOutA(_io, fill);
		setNoise(_io, 0, 0, 55, 99);
	}

	const Table* snareTables(int& _count)
	{
		using namespace tables;
		static const Table t[] = {
			{"snareClassic.decay", g_classicDecay.data(), 128, 4, ClassicDecayBound},
			{"snareClassic.decayBelow", g_classicBelow.data(), 128, 4, ClassicBelowBound},
			{"snareNatural.rate", g_naturalRate.data(), 128, 4, NaturalRateBound},
			{"snareNatural.decay", g_naturalDecay.data(), 128, 4, NaturalDecayBound},
			{"snareAcoustic.attack", g_acousticAttack.data(), 128, 4, AcousticAttackBound},
			{"snareAcoustic.rate", g_acousticRate.data(), 128, 4, AcousticRateBound},
			{"snareAcoustic.decay", g_acousticDecay.data(), 128, 4, AcousticDecayBound},
			{"snareNatural.attackSample", g_naturalAttack.data(), 32, 4, -1},
			{"snareAcoustic.bodySample", g_acousticBody.data(), BodyLength, 2, -1},
		};
		_count = static_cast<int>(std::size(t));
		return t;
	}
}

namespace elektron::drums::analog::port
{
	void warmUpSnare()
	{
		tables::tune(64);
	}
}
