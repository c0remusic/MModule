#include "stStages.h"

#include "stEmac.h"

namespace syntakt
{
	int32_t ff1(const int32_t _v)
	{
		uint32_t v = static_cast<uint32_t>(_v);
		if(!v)
			return 32;
		int32_t n = 0;
		while(!(v & 0x80000000u))
		{
			v <<= 1;
			++n;
		}
		return n;
	}

	int32_t sqrtLog(const Rom& _rom, const int32_t _v)
	{
		const int32_t z = ff1(_v) - 1;
		const int32_t mn = aslReg(_v, z);
		const int32_t k = frac(0x800, mn);
		int32_t l = _rom.entry(0x4000f48c - 0x1000, k);
		l = neg(l) >> 7;
		l = addWrap(l, aslReg(z, 24));
		return l >> 1;
	}

	int32_t exp2Entry(const Rom& _rom, const int32_t _l)
	{
		return _rom.entry(0x4000e488, subWrap(0x1000000, _l & 0xffffff) >> 14);
	}

	namespace
	{
		// The envelopes' one-pole decay: level <- a level - (1 - a) target, with the coefficient c = -a. Two products,
		// each stored on its own, then a wrapping add
		int32_t decayStep(const int32_t _level, const int32_t _c, const int32_t _target)
		{
			Emac acc;
			acc.msac(_level, _c);
			const int32_t a = acc.storeClear();
			acc.msac(_target, addWrap(_c, INT32_MAX));
			return addWrap(a, acc.storeClear());
		}

		// An envelope level applied to a block, ramped from the previous block's level, faded to silence in the block a
		// trig arrives in. The level is in negative representation: out = -g x
		void rampedVca(int32_t* _buf, Voice& _v, const uint32_t _level, const uint32_t _prev, const uint32_t _attack)
		{
			if(_v.at(Voice::TrigPending))
				_v.at(_level) = 0;
			else if(_v.at(Voice::TrigNow) && _v.at(_attack) == INT32_MIN)
				_v.at(_prev) = _v.at(_level);

			const int32_t level = _v.at(_level);
			int32_t g = _v.at(_prev);
			const int32_t ramp = rampStepSat(level, g);
			for(int n = 0; n < BlockSize; ++n)
			{
				Emac acc;
				acc.msac(g, _buf[n]);
				g = addSat(g, ramp);
				_buf[n] = acc.storeClear();
			}
			_v.at(_prev) = level;
		}
	}

	int32_t interpolate(const int32_t* _table, const int32_t _word)
	{
		int32_t i = _word >> 8;
		if(i < 0)
			i = 0;
		const int32_t f = static_cast<int32_t>(static_cast<uint32_t>(_word - (i << 8)) << 23);
		const int32_t next = i + 1 > 127 ? 127 : i + 1;

		// The two weights sum to 1 - 2^-31, as in the original
		Emac acc;
		acc.mac(subWrap(INT32_MAX, f), _table[i]);
		acc.mac(f, _table[next]);
		return acc.storeClear();
	}

	void unitEnvelopes(Voice& _v, const int32_t _units)
	{
		for(int32_t k = 0; k < _units; ++k)
		{
			int32_t& level = _v.unit(k, Voice::EnvLevel);
			int32_t& stage = _v.unit(k, Voice::EnvStage);
			const int32_t attack = _v.unit(k, Voice::EnvAttack);

			switch(stage)
			{
			case 3:
				{
					int32_t& count = _v.unit(k, Voice::EnvDelayCount);
					++count;
					if(count >= _v.unit(k, Voice::EnvDelay))
					{
						level = attack;
						stage = 0;
					}
				}
				break;
			case 0:
				if(level == INT32_MIN)
					stage = 1;
				else
					level = addSat(level, attack);
				break;
			case 4:
				break;
			default:
				level = decayStep(level, _v.unit(k, Voice::EnvDecay), _v.unit(k, Voice::EnvTarget));
				break;
			}

			if(!_v.at(Voice::TrigNow))
				continue;
			if(_v.unit(k, Voice::EnvDelay))
			{
				stage = 3;
				_v.unit(k, Voice::EnvDelayCount) = 0;
				level = 0;
				continue;
			}
			level = attack;
			_v.unit(k, Voice::EnvLevelPrev) = attack;
			stage = attack == INT32_MIN ? 1 : 0;
		}
	}

	void ampEnvelope(Voice& _v)
	{
		int32_t& level = _v.at(Voice::AmpLevel);
		int32_t& stage = _v.at(Voice::AmpStage);
		int32_t& release = _v.at(Voice::AmpRelease);

		if(_v.at(Voice::TrigNow))
		{
			release = 0;
			const int32_t attack = _v.at(Voice::AmpAttack);
			if(attack == INT32_MIN)
			{
				level = attack;
				_v.at(Voice::AmpHoldCount) = 0;
			}
			else if(_v.at(Voice::AmpHalfStart))
			{
				level = attack >> 1;
			}
			stage = 0;
		}

		if(stage == 0)
		{
			if(level == INT32_MIN)
			{
				if(!release && _v.at(Voice::AmpHold))
				{
					stage = 2;
					_v.at(Voice::AmpHoldCount) = 0;
				}
				else
				{
					stage = 1;
				}
			}
			else
			{
				level = addSat(level, _v.at(Voice::AmpAttack));
			}
		}
		else if(stage == 2)
		{
			if(release)
			{
				stage = 1;
				release = 0;
			}
			++_v.at(Voice::AmpHoldCount);
		}
		else
		{
			Emac acc;
			acc.msac(level, _v.at(Voice::AmpDecay));
			level = acc.storeClear();
		}

		if(_v.at(Voice::GateOff) && !_v.at(Voice::TrigPending))
			release = 1;
	}

	void vca(int32_t* _out, Voice& _v)
	{
		rampedVca(_out, _v, Voice::AmpLevel, Voice::AmpLevelPrev, Voice::AmpAttack);
	}

	void driveClip(int32_t* _out, Voice& _v)
	{
		if(_v.at(Voice::TrigNow))
			_v.at(Voice::DriveStage) = 1;

		// The gain envelope: a first-order filter on a step, falling to 0 or recovering to 1.0
		uint32_t set;
		int32_t x;
		if(_v.at(Voice::DriveStage) == 1)
		{
			set = Voice::DriveSetA;
			x = 0;
		}
		else
		{
			set = Voice::DriveSetB;
			x = INT32_MAX;
		}
		_v.at(Voice::DriveSet) = static_cast<int32_t>(_v.address + set);	// the firmware's pointer to the set

		Emac acc;
		acc.mac(_v.at(set + 4), _v.at(Voice::DriveX1));
		const int32_t a = acc.storeClear();
		acc.mac(_v.at(set), x);
		int32_t y = addWrap(acc.storeClear(), a);
		acc.msac(_v.at(set + 8), _v.at(Voice::DriveY1));
		y = addWrap(y, acc.storeClear());

		const int32_t floor = _v.at(Voice::DriveFloor);
		if(floor >= y)
		{
			_v.at(Voice::DriveStage) = 5;
			y = floor;
		}
		if(!_v.at(Voice::DriveOn))
		{
			_v.at(Voice::DriveY1) = INT32_MAX;
			y = INT32_MAX;
		}

		int32_t g = _v.at(Voice::DriveY1);
		const int32_t step = subWrap(y, g) >> 5;
		const int32_t pre = _v.at(Voice::DrivePreGain);
		const int32_t post = _v.at(Voice::DriveGain);
		for(int n = 0; n < BlockSize; ++n)
		{
			Emac gain, drive;
			gain.mac(g, post);
			for(int i = 0; i < 4; ++i)
				drive.mac(pre, _out[n]);
			const int32_t gs = gain.storeClear();
			const int32_t ds = drive.storeClear();
			drive.mac(gs, ds);
			g = addWrap(g, step);
			_out[n] = drive.storeClear();
		}
		_v.at(Voice::DriveX1) = x;
		_v.at(Voice::DriveY1) = y;
	}

	void onePole(int32_t* _buf, int32_t* _c)
	{
		const int32_t c0 = _c[0], c1 = _c[1], c2 = _c[2];
		int32_t x = _c[6];
		int32_t y = _c[7];

		// Two accumulators in turn, each summing a sample's three terms before one truncation
		Emac acc0, acc1;
		acc0.mac(c1, x);
		x = _buf[0];
		acc0.mac(c0, x);
		for(int n = 0; n < BlockSize; n += 2)
		{
			acc0.msac(c2, y);
			acc1.mac(c1, x);
			x = _buf[n + 1];
			acc1.mac(c0, x);
			_c[6] = x;
			y = acc0.storeClear();
			_buf[n] = y;
			acc1.msac(c2, y);
			acc0.mac(c1, x);
			x = _buf[n + 2];
			acc0.mac(c0, x);
			y = acc1.storeClear();
			_buf[n + 1] = y;
		}
		_c[7] = y;
	}

	void mix(const int32_t* _x, const int32_t* _y, int32_t* _out, const int32_t _gainX, const int32_t _gainY,
		Voice& _v)
	{
		const uint32_t tx = 0x08 + 4 * static_cast<uint32_t>(_gainX), ty = 0x08 + 4 * static_cast<uint32_t>(_gainY);
		const int32_t targetX = _v.at(tx);
		int32_t gx = _v.at(tx + 0x10);
		int32_t gy = _v.at(ty + 0x10);
		const int32_t stepX = subWrap(targetX, gx) >> 5;
		const int32_t stepY = subWrap(_v.at(ty), gy) >> 5;
		for(int n = 0; n < BlockSize; ++n)
		{
			Emac acc;
			acc.mac(gx, _x[n]);
			acc.mac(gy, _y[n]);
			gx = addWrap(gx, stepX);
			gy = addWrap(gy, stepY);
			_out[n] = acc.storeClear();
		}
		_v.at(tx + 0x10) = targetX;
		_v.at(ty + 0x10) = _v.at(ty);
	}

	void modulatorVca(int32_t* _buf, Voice& _v)
	{
		rampedVca(_buf, _v, 0x448, 0x44c, 0x458);
	}

	void modulatorEnvelope(Voice& _v)
	{
		int32_t& level = _v.at(0x448);
		int32_t& stage = _v.at(0x450);
		const int32_t attack = _v.at(0x458);

		switch(stage)
		{
		case 3:
			{
				int32_t& count = _v.at(0x454);
				++count;
				if(count >= _v.at(0x460))
				{
					level = attack == INT32_MIN ? attack : attack >> 1;
					stage = 0;
				}
			}
			break;
		case 0:
			if(level == INT32_MIN)
				stage = 1;
			else
				level = addSat(level, attack);
			break;
		case 4:
			break;
		default:
			level = decayStep(level, _v.at(0x45c), _v.at(0x464));
			break;
		}

		if(!_v.at(Voice::TrigNow))
			return;
		if(_v.at(0x460))
		{
			stage = 3;
			_v.at(0x454) = 0;
			level = 0;
			return;
		}
		if(attack == INT32_MIN)
		{
			level = attack;
			_v.at(0x44c) = attack;
		}
		else
		{
			level = attack >> 1;
		}
		stage = 0;
	}

	void sweepEnvelope(Voice& _v)
	{
		int32_t& level = _v.at(0x400);
		int32_t& stage = _v.at(0x408);
		const int32_t attack = _v.at(0x40c);

		if(_v.at(Voice::TrigNow))
		{
			if(attack == INT32_MIN)
				level = INT32_MAX;
			else if(_v.at(0x3fc))
				level = subWrap(0, attack) >> 1;
			stage = 0;
		}

		if(stage == 0)
		{
			if(level == INT32_MAX)
			{
				stage = 1;
				return;
			}
			level = addSat(level, subWrap(0, attack));
			return;
		}
		level = frac(level, subWrap(0, _v.at(0x410)));
	}

	namespace
	{
		// The phase increments 0x40002596, 0x400060b2 and 0x40002686 compute, the three routines alike but for the
		// rate's constant and the last shift
		void phaseIncrements(const int32_t _pitch, Voice& _v, const uint32_t _params, const int32_t _units,
			const int32_t _rate, const int _shift)
		{
			constexpr uint32_t Exp2 = 0x8000a888;
			const Sram& sram = _v.sram;

			// The note plus the tune word, in semitones Q16, clamped to notes 0-127
			const int32_t tune = static_cast<int32_t>(static_cast<uint32_t>(sram.half(_params + 0x24)) << 16);
			int32_t p = addSat(_v.at(0x40), frac(_v.at(0x44), tune));
			p = addSat(p, _pitch);
			if(p < 0)
				p = 0;
			else if(p > 0x7f0000)
				p = 0x7f0000;

			// Octaves from A4 in Q16, and 2^fraction from the RAM table
			const int32_t q = frac(addWrap(p, static_cast<int32_t>(0xffbb0000)), 0x0aaaaaab);
			const int32_t octave = static_cast<int16_t>(static_cast<uint32_t>(q) >> 16);
			const int32_t fraction = static_cast<int32_t>(static_cast<uint32_t>(q) & 0xffff) >> 5;
			int32_t f = frac(0x6e000000, sram.at(Exp2 + 4 * static_cast<uint32_t>(fraction)));
			f = asrReg(f, 5 - octave);

			for(int32_t k = 0; k < _units; ++k)
			{
				const int32_t m = addWrap(frac(addWrap(_v.at(0x3f8), static_cast<int32_t>(0xfc000000)), _v.at(0x400)),
					0x04000000);
				int32_t t = frac(_v.unit(k, 0x50) >> 6, m);
				t = frac(t, _v.unit(k, 0x64));
				t = static_cast<int32_t>(static_cast<uint32_t>(t) << 11);
				const int32_t offset = addSat(_v.unit(k, 0x68), f);
				t = addSat(t, t);
				t = frac(t, offset);
				t = frac(t, _rate);
				_v.unit(k, 0x78) = t >> _shift;
			}
		}
	}

	void phaseIncrements48k(const int32_t _pitch, Voice& _v, const uint32_t _params, const int32_t _units)
	{
		phaseIncrements(_pitch, _v, _params, _units, 0x57619f10, 2);
	}

	void phaseIncrements24k(const int32_t _pitch, Voice& _v, const uint32_t _params, const int32_t _units)
	{
		phaseIncrements(_pitch, _v, _params, _units, 0x57619f10, 1);
	}

	void phaseIncrements96k(const int32_t _pitch, Voice& _v, const uint32_t _params)
	{
		phaseIncrements(_pitch, _v, _params, 4, 0x2bb0cf88, 2);
	}

	namespace
	{
		// The band-limited wave for a note, into unit _k's field _field (0x40006588 and 0x40006646)
		void selectWave(const int32_t _k, const int32_t _note, Voice& _v, const uint32_t _field)
		{
			// At or below note 35 (signed), then ranges of 12 notes from 36 up. The range from 82 overlaps the one before
			// it, so it takes notes 84 to 95 only
			uint32_t wave;
			if(_note <= 35)
				wave = 0x8000d08c;
			else if(static_cast<uint32_t>(_note - 36) <= 11)
				wave = 0x8000c888;
			else if(static_cast<uint32_t>(_note - 48) <= 11)
				wave = 0x80007788;
			else if(static_cast<uint32_t>(_note - 60) <= 11)
				wave = 0x80006f84;
			else if(static_cast<uint32_t>(_note - 72) <= 11)
				wave = 0x80006780;
			else if(static_cast<uint32_t>(_note - 82) <= 13)
				wave = 0x80005f7c;
			else if(static_cast<uint32_t>(_note - 96) <= 11)
				wave = 0x80005778;
			else
				wave = 0x80004f74;
			_v.unit(static_cast<uint32_t>(_k), _field) = static_cast<int32_t>(wave);
		}
	}

	void selectBandlimitedWave(const int32_t _k, const int32_t _note, Voice& _v)
	{
		selectWave(_k, _note, _v, 0xb0);
	}

	void selectSecondBandlimitedWave(const int32_t _k, const int32_t _note, Voice& _v)
	{
		selectWave(_k, _note, _v, 0xb4);
	}

	namespace
	{
		// The table lookup both oscillators share: index and fraction from the phase sum, linear interpolation
		int32_t lookup(const int32_t* _table, Emac& _phase)
		{
			const int32_t p = _phase.storeClear();
			const int32_t i = (p & 0xffffff) >> 16;
			const int32_t f = static_cast<int32_t>((static_cast<uint32_t>(p) & 0xffff) << 15);
			// load, mac, msac at once (EmacT::setSum): the loaded word is at most 2^39 in magnitude as a product
			// is, so the three terms stay under 2^47
			Emac acc;
			acc.setSum(static_cast<int64_t>(_table[i]) * 256 + Emac::product(f, _table[i + 1]) -
				Emac::product(f, _table[i]));
			return acc.storeClear();
		}
	}

	void feedbackOscillator(const int32_t _k, Voice& _v, const Rom& _rom)
	{
		const auto k = static_cast<uint32_t>(_k);
		Sram& sram = _v.sram;
		int32_t* a = sram.ptr(static_cast<uint32_t>(_v.unit(k, 0xc8)));
		int32_t* b = sram.ptr(static_cast<uint32_t>(_v.unit(k, 0xcc)));
		const int32_t* wave = table(sram, _rom, static_cast<uint32_t>(_v.unit(k, 0xb0)));

		const int32_t target = frac(0x04000000, _v.at(0x2c));
		int32_t fb = _v.at(0x30);
		const int32_t fbStep = subWrap(target, fb) >> 5;
		const int32_t inc = _v.unit(k, 0x78);
		const int32_t level = _v.unit(k, Voice::EnvLevel);
		int32_t amount = _v.unit(k, Voice::EnvLevelPrev);
		const int32_t amountStep = subSat(level, amount) >> 5;
		int32_t phase = _v.unit(k, Voice::Phase);
		int32_t s = _v.at(0x28);

		Emac acc1;
		acc1.load(phase);
		acc1.mac(fb, s);
		for(int n = 0; n < BlockSize; ++n)
		{
			fb = addWrap(fb, fbStep);
			s = lookup(wave, acc1);
			phase = addWrap(phase, inc) & 0xffffff;
			Emac acc0;
			acc0.msac(amount, s);
			amount = addWrap(amount, amountStep);
			acc1.load(phase);
			b[n] = s;
			a[n] = acc0.storeClear();
			acc1.mac(fb, s);	// the raw sample feeds back
		}

		_v.at(0x28) = s;
		_v.unit(k, Voice::Phase) = phase;
		_v.unit(k, Voice::EnvLevelPrev) = level;
		_v.at(0x30) = target;
	}

	void pmOscillator(const int32_t _k, Voice& _v, const Rom& _rom)
	{
		const auto k = static_cast<uint32_t>(_k);
		Sram& sram = _v.sram;
		int32_t* a = sram.ptr(static_cast<uint32_t>(_v.unit(k, 0xc8)));
		int32_t* b = sram.ptr(static_cast<uint32_t>(_v.unit(k, 0xcc)));
		const int32_t* src1 = sram.ptr(static_cast<uint32_t>(_v.unit(k, 0x9c)));
		const int32_t* src2 = sram.ptr(static_cast<uint32_t>(_v.unit(k, 0xa0)));
		const int32_t* wave = table(sram, _rom, static_cast<uint32_t>(_v.unit(k, 0xb0)));

		int32_t amount = _v.unit(k, Voice::EnvLevelPrev);
		const int32_t amountStep = subWrap(_v.unit(k, Voice::EnvLevel), amount) >> 5;
		int32_t depth1 = _v.unit(k, 0x94);
		const int32_t step1 = subWrap(_v.unit(k, 0x8c), depth1) >> 5;
		int32_t depth2 = _v.unit(k, 0x98);
		const int32_t step2 = subWrap(_v.unit(k, 0x90), depth2) >> 5;
		const int32_t inc = _v.unit(k, 0x78);
		int32_t phase = _v.unit(k, Voice::Phase);

		// The phase sum, phase + depth1 src1 + depth2 src2, truncated once
		Emac acc1;
		acc1.load(phase);
		acc1.mac(depth1, src1[0]);
		for(int n = 0; n < BlockSize; ++n)
		{
			acc1.mac(depth2, src2[n]);
			depth1 = addWrap(depth1, step1);
			depth2 = addWrap(depth2, step2);
			const int32_t s = lookup(wave, acc1);
			phase = addWrap(phase, inc) & 0xffffff;
			acc1.load(phase);
			Emac acc0;
			acc0.msac(amount, s);
			amount = addWrap(amount, amountStep);
			b[n] = s;
			acc1.mac(depth1, src1[n + 1]);
			a[n] = acc0.storeClear();
		}

		_v.unit(k, Voice::Phase) = phase;
		_v.unit(k, Voice::EnvLevelPrev) = _v.unit(k, Voice::EnvLevel);
		_v.unit(k, 0x94) = _v.unit(k, 0x8c);
		_v.unit(k, 0x98) = _v.unit(k, 0x90);
	}

void scaledFeedbackOscillator(const int32_t _k, Voice& _v, const Rom& _rom)
	{
		const auto k = static_cast<uint32_t>(_k);
		Sram& sram = _v.sram;
		int32_t* a = sram.ptr(static_cast<uint32_t>(_v.unit(k, 0xc8)));
		int32_t* b = sram.ptr(static_cast<uint32_t>(_v.unit(k, 0xcc)));
		const int32_t* wave = table(sram, _rom, static_cast<uint32_t>(_v.unit(k, 0xb0)));

		const int32_t target = frac(0x04000000, _v.at(0x2c));
		int32_t fb = _v.at(0x30);
		const int32_t fbStep = subWrap(target, fb) >> 5;
		const int32_t inc = _v.unit(k, 0x78);
		const int32_t level = _v.unit(k, Voice::EnvLevel);
		int32_t amount = _v.unit(k, Voice::EnvLevelPrev);
		const int32_t amountStep = subSat(level, amount) >> 5;
		int32_t phase = _v.unit(k, Voice::Phase);
		int32_t y = _v.at(0x28);

		Emac acc1;
		acc1.load(phase);
		acc1.mac(fb, y);
		for(int n = 0; n < BlockSize; ++n)
		{
			fb = addWrap(fb, fbStep);
			const int32_t p = acc1.storeClear();
			const int32_t i = (p & 0xffffff) >> 16;
			const int32_t f = static_cast<int32_t>((static_cast<uint32_t>(p) & 0xffff) << 15);
			Emac acc0;
			acc0.load(wave[i]);
			acc0.mac(f, wave[i + 1]);
			acc0.msac(f, wave[i]);
			phase = addWrap(phase, inc) & 0xffffff;
			const int32_t s = acc0.storeClear();
			acc0.msac(amount, s);
			amount = addWrap(amount, amountStep);
			acc1.load(phase);
			b[n] = s;
			y = acc0.storeClear();
			acc1.mac(fb, y);
			a[n] = y;
		}

		_v.at(0x28) = y;
		_v.unit(k, Voice::Phase) = phase;
		_v.unit(k, Voice::EnvLevelPrev) = level;
		_v.at(0x30) = target;
	}

void gatedNoise(int32_t* _out, int32_t* _s)
	{
		for(int n = 0; n < BlockSize; ++n)
		{
			const int32_t d1 = addWrap(_s[1], _s[0]);
			_s[1] = addWrap(_s[1], d1);
			const int32_t a1 = addWrap(_s[3], _s[2]);
			_s[0] = d1;
			_s[3] = addWrap(_s[3], a1);
			_s[2] = a1;
			_out[n] = a1 > _s[4] ? d1 : 0;
		}
	}

	// 0x40004cd6: four buffers mixed into _out with gains ramped over the block: targets at 0x08 + 4 i, current
	// values at 0x18 + 4 i. _x[3] may be _out itself
	void mix4(const int32_t* const* _x, int32_t* _out, Voice& _v)
	{
		int32_t g[4], step[4];
		for(uint32_t i = 0; i < 4; ++i)
		{
			step[i] = subWrap(_v.at(0x08 + 4 * i), _v.at(0x18 + 4 * i)) >> 5;
			g[i] = _v.at(0x18 + 4 * i);
		}
		for(int n = 0; n < BlockSize; ++n)
		{
			Emac acc;
			for(int i = 0; i < 4; ++i)
				acc.mac(g[i], _x[i][n]);
			for(int i = 0; i < 4; ++i)
				g[i] = addWrap(g[i], step[i]);
			_out[n] = acc.storeClear();
		}
		for(uint32_t i = 0; i < 4; ++i)
			_v.at(0x18 + 4 * i) = _v.at(0x08 + 4 * i);
	}

	// 0x4000569a: two sample-and-holds at periods p + 1 and p + 2 samples, crossfaded by p's fraction (_c[4], Q16)
	void reduceRate(int32_t* _buf, int32_t* _c)
	{
		const int32_t rate = _c[4];
		const int32_t p = rate >> 16;
		const int32_t w1 = asl(rate & 0xffff, 15);
		const int32_t w0 = subSat(INT32_MAX, w1);
		const int32_t p1 = addWrap(p, 1), p2 = addWrap(p, 2);
		int32_t h1 = _c[0], h2 = _c[1], n1 = _c[2], n2 = _c[3];
		for(int n = 0; n < BlockSize; ++n)
		{
			if(p1 <= n1)
			{
				h1 = _buf[n];
				n1 = 0;
			}
			if(p2 <= n2)
			{
				h2 = _buf[n];
				n2 = 0;
			}
			Emac acc;
			acc.mac(w0, h1);
			acc.mac(w1, h2);
			n1 = addWrap(n1, 1);
			n2 = addWrap(n2, 1);
			_buf[n] = acc.storeClear();
		}
		_c[0] = h1;
		_c[1] = h2;
		_c[2] = n1;
		_c[3] = n2;
	}
}
