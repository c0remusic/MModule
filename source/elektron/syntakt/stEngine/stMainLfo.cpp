#include "stMainLfo.h"

#include <algorithm>

#include "stEmac.h"

namespace syntakt::main
{
	namespace
	{
		constexpr uint32_t HalfTable = 0x402457e4;		// a long per waveform: the output where HALF stops
		constexpr uint32_t OneUpTable = 0x40245800;		// the output where ONE stops, running forward
		constexpr uint32_t OneDownTable = 0x4024581c;	// and backward
		constexpr uint32_t RandomCurves = 0x40245850;	// RND's interpolation coefficient, by SPH >> 8
		constexpr int32_t FreeRate = 0x3840;			// the rate of the free multipliers, 120 BPM's tempo
		constexpr int32_t HalfOffset = static_cast<int32_t>(0xd6cd2000);	// minus half a cycle
		constexpr int32_t ToQ32 = 0x636ba875;			// 2^30 / LfoPeriod in Q31: the phase in quarter cycles
		constexpr int32_t FullFade = 0x3fffffff;

		enum Mode
		{
			Free,
			Trig,
			Hold,
			One,
			Half
		};

		constexpr int32_t Rnd = 6;

		int32_t mulWrap(const int32_t _a, const int32_t _b)
		{
			return static_cast<int32_t>(static_cast<uint32_t>(_a) * static_cast<uint32_t>(_b));
		}

		// lsr.l Dn: a register's count is taken mod 64, and 32 or more shifts everything out
		int32_t lsrReg(const int32_t _v, const int32_t _n)
		{
			const int32_t n = _n & 63;
			return n >= 32 ? 0 : static_cast<int32_t>(static_cast<uint32_t>(_v) >> n);
		}

		// add.l d0,d0 then subx: -1 when bit 31 is set, else 0
		int32_t signMask(const int32_t _v) { return _v < 0 ? -1 : 0; }

		int32_t triangle(const int32_t _x)
		{
			const int32_t y = addWrap(_x, 0x40000000);
			return asl(y, 1) ^ signMask(y) ^ INT32_MIN;
		}

		// asr.l #1 rounding toward zero: the negative values get 1 first
		int32_t halve(const int32_t _v) { return (_v < 0 ? _v + 1 : _v) >> 1; }

		int32_t sine(const int32_t _x)
		{
			int32_t d0 = halve(_x ^ 0x7fffffff);
			int32_t d1 = fracWrap(d0 < 0 ? neg(d0) : d0, d0);
			d0 = subWrap(halve(d0), d1);
			d1 = addWrap(fracWrap(d0 < 0 ? neg(d0) : d0, 0x73333334), 0x18cccccc);
			return asl(fracWrap(d0, d1), 6);
		}

		int32_t exponential(const int32_t _x)
		{
			const int32_t a = fracWrap(static_cast<int32_t>(0xf3ffe8eb), lsrReg(_x, 1));
			return addWrap(exp2(addWrap(a, 0x1715)), static_cast<int32_t>(0xfff80000));
		}

		// RND's output: the random value and the last output mixed by the coefficient of SPH
		int32_t interpolated(const Memory& _mem, const uint32_t _a, const int32_t _phaseStart)
		{
			const int32_t c = _mem.word(RandomCurves + 4 * static_cast<uint32_t>(_phaseStart >> 8));
			EmacWrap acc;
			acc.mac(_mem.word(_a - 0x1c), subWrap(INT32_MAX, c));
			acc.mac(_mem.word(_a - 0x18), c);
			return acc.storeClear();
		}
	}

	int32_t exp2(const int32_t _x)
	{
		const int32_t m = lsrReg(asl(_x, 7) | INT32_MIN, 1);
		const int32_t square = fracWrap(m, m);
		const int32_t d0 = fracWrap(square, 0x55555556);
		return lsrReg(addWrap(addWrap(d0, d0), 0x55555556), neg(_x >> 24));
	}

	int32_t lfoFadeStep(const int32_t _fade)
	{
		const int32_t fade = static_cast<int16_t>(_fade);
		const int32_t d = fade - 0x4000;
		if(d == 0)
			return 0x40000000;
		const int32_t e = d > 0 ? neg(exp2(addWrap(asl(fade, 14), static_cast<int32_t>(0xdf000000))))
			: exp2(addWrap(asl(d, 14), static_cast<int32_t>(0xff000000)));
		return fracWrap(0x1c200000, e);
	}

	int32_t lfoFadeStart(const int32_t _fade) { return static_cast<int16_t>(_fade) > 0x3fff ? FullFade : 0; }

	int32_t lfoWave(const int32_t _wave, const int32_t _phase)
	{
		switch(_wave)
		{
		case 0:
			return triangle(_phase);
		case 1:
			return sine(_phase);
		case 2:
			return _phase < 0 ? INT32_MIN : INT32_MAX;
		case 3:
			return _phase ^ INT32_MAX;
		case 4:
			return exponential(_phase);
		default:
			return _phase < 0 ? 0 : _phase;
		}
	}

	int32_t lfoRandom(Memory& _mem)
	{
		const int32_t last = _mem.word(RandomState + 4);
		const int32_t next = addWrap(last, _mem.word(RandomState));
		_mem.setWord(RandomState, last);
		_mem.setWord(RandomState + 4, next);
		return next;
	}

	void lfos(Memory& _mem, const uint32_t _p, const int32_t _tempo, const int32_t _trigs)
	{
		for(uint32_t t = 0; t < 13; ++t)
		{
			const uint32_t params = _p + 0x1c + TrackBytes * t;
			const bool trig = (_trigs >> t & 1) != 0;
			for(int32_t j = 1; j >= 0; --j)
			{
				// The LFO's DEST word, its other parameters around it: SPD -6, MULT -4, FADE -2, WAVE +2, SPH +4,
				// MODE +6, DEP +8 (bytes for MULT, DEST, WAVE and MODE: a word's value)
				const uint32_t base = params + 8 + 0x10 * static_cast<uint32_t>(j);
				const uint32_t a = LfoState + LfoTrackStride * t - LfoStride * static_cast<uint32_t>(1 - j);
				const uint32_t out = a - 0x20;

				int32_t mult = static_cast<int8_t>(_mem.byte(base - 4));
				int32_t rate = _tempo;
				if(mult < 0)
				{
					mult = 0;
				}
				else if(mult > 11)
				{
					mult = std::min(mult, 23) - 12;
					rate = FreeRate;
				}
				const int32_t wave = std::clamp<int32_t>(static_cast<int8_t>(_mem.byte(base + 2)), 0, Rnd);
				const int32_t speed = mulWrap(asl(addWrap(_mem.half(base - 6), -0x4000), 1), rate) >> (11 - mult);
				const int32_t phaseStart = _mem.half(base + 4);
				int32_t fade = lfoFadeStep(_mem.half(base - 2));
				const int32_t mode = static_cast<int8_t>(_mem.byte(base + 6));

				const int32_t old = _mem.word(a - 0x24);
				int32_t phase = addWrap(speed, old);
				fade = addWrap(fade, _mem.word(a - 0x14));
				const bool idle = !_mem.byte(a + 1) && !_mem.byte(a);
				if((addWrap(phase, HalfOffset) ^ addWrap(old, HalfOffset)) < 0 && idle && mode == Half)
				{
					_mem.setWord(out, _mem.word(HalfTable + 4 * static_cast<uint32_t>(wave)));
					_mem.setByte(a, 1);
				}
				if(static_cast<uint32_t>(phase) > static_cast<uint32_t>(LfoPeriod - 1))
				{
					if(!_mem.byte(a + 1) && !_mem.byte(a) && mode == One)
					{
						const uint32_t table = speed < 0 ? OneDownTable : OneUpTable;
						_mem.setWord(out, _mem.word(table + 4 * static_cast<uint32_t>(wave)));
						_mem.setByte(a, 1);
					}
					phase = addWrap(phase, speed < 0 ? LfoPeriod : -LfoPeriod);
				}
				if(static_cast<uint32_t>(mode) <= Trig)
					_mem.setByte(a, 0);
				_mem.setByte(a + 1, 0);

				// On a trig, TRIG, ONE and HALF restart the phase at SPH and the fade; FREE restarts the fade; HOLD
				// samples the waveform once and holds it
				bool sample = false;
				if(trig)
				{
					if(mode == Trig || static_cast<uint16_t>(mode - One) <= 1)
					{
						phase = wave != Rnd ? fracWrap(LfoPeriod, asl(phaseStart, 16)) : 0;
						fade = lfoFadeStart(_mem.half(base - 2));
						_mem.setByte(a, 0);
						_mem.setByte(a + 1, 1);
					}
					if(mode == Free)
					{
						_mem.setByte(a, 0);
						fade = lfoFadeStart(_mem.half(base - 2));
					}
					else if(mode == Hold)
					{
						_mem.setByte(a, 1);
						sample = true;
					}
				}
				_mem.setWord(a - 0x24, phase);
				fade = std::clamp(fade, 0, FullFade);
				_mem.setWord(a - 0x14, fade);

				// Stopped (or holding), the output stays as it is, but RND's mix of its values goes on
				const bool held = _mem.byte(a) && !sample;
				int32_t v = _mem.word(out);
				if(!held && wave != Rnd)
				{
					v = lfoWave(wave, asl(fracWrap(ToQ32, phase), 2));
				}
				else if(wave == Rnd)
				{
					if(!held)
					{
						const int32_t last = _mem.word(a - 0x10);
						const int32_t since = speed < 0 ? subWrap(last, phase) : subWrap(phase, last);
						if(static_cast<uint32_t>(since) > 0x5265bff || (mode != Free && trig))
						{
							_mem.setWord(a - 0x10, phase);
							_mem.setWord(a - 0x1c, lfoRandom(_mem));
						}
					}
					v = interpolated(_mem, a, phaseStart);
					_mem.setWord(out, v);
				}
				_mem.setWord(a - 0x18, v);
				if(!held)
				{
					v = fracWrap(fade, v);
					v = addWrap(v, v);
					_mem.setWord(out, v);
				}

				const int32_t dest = static_cast<int8_t>(_mem.byte(base));
				_mem.setWord(a - 0xc, dest);
				if(static_cast<uint32_t>(dest) <= 0x46)
				{
					const int32_t amount = fracWrap(asl(addWrap(_mem.half(base + 8), -0x4000), 1), v);
					_mem.setWord(a - 8, amount);
					const uint32_t at = params + 2 * static_cast<uint32_t>(dest);
					int32_t r = addWrap(amount, _mem.half(at));
					if(r < 0)
						r = 0;
					else if(r > 0x7f00)
						r = 0x7f00;
					_mem.setHalf(at, r);
				}
				_mem.setWord(a - 4, _mem.byte(a) ? 0 : _mem.word(a - 0x24));
			}
		}
	}
}
