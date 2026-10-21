#include "stStages.h"

#include "stEmac.h"

namespace syntakt
{
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
				{
					// level <- a level - (1 - a) target, with the coefficient c = -a
					const int32_t c = _v.unit(k, Voice::EnvDecay);
					Emac acc;
					acc.msac(level, c);
					const int32_t a = acc.storeClear();
					acc.msac(_v.unit(k, Voice::EnvTarget), addWrap(c, INT32_MAX));
					level = addWrap(a, acc.storeClear());
				}
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
		if(_v.at(Voice::TrigPending))
			_v.at(Voice::AmpLevel) = 0;
		else if(_v.at(Voice::TrigNow) && _v.at(Voice::AmpAttack) == INT32_MIN)
			_v.at(Voice::AmpLevelPrev) = _v.at(Voice::AmpLevel);

		const int32_t level = _v.at(Voice::AmpLevel);
		int32_t g = _v.at(Voice::AmpLevelPrev);
		const int32_t ramp = subSat(level, g) >> 5;

		// The level is in negative representation: out = -g x
		for(int n = 0; n < BlockSize; ++n)
		{
			Emac acc;
			acc.msac(g, _out[n]);
			g = addSat(g, ramp);
			_out[n] = acc.storeClear();
		}
		_v.at(Voice::AmpLevelPrev) = level;
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
		if(_v.at(Voice::TrigPending))
			_v.at(0x448) = 0;
		else if(_v.at(Voice::TrigNow) && _v.at(0x458) == INT32_MIN)
			_v.at(0x44c) = _v.at(0x448);

		const int32_t level = _v.at(0x448);
		int32_t g = _v.at(0x44c);
		const int32_t ramp = subSat(level, g) >> 5;
		for(int n = 0; n < BlockSize; ++n)
		{
			Emac acc;
			acc.msac(g, _buf[n]);
			g = addSat(g, ramp);
			_buf[n] = acc.storeClear();
		}
		_v.at(0x44c) = level;
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
			{
				const int32_t c = _v.at(0x45c);
				Emac acc;
				acc.msac(level, c);
				const int32_t a = acc.storeClear();
				acc.msac(_v.at(0x464), addWrap(c, INT32_MAX));
				level = addWrap(a, acc.storeClear());
			}
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

	void phaseIncrements48k(const int32_t _pitch, Voice& _v, const Sram& _sram, const uint32_t _params,
		const int32_t _units)
	{
		constexpr uint32_t Exp2 = 0x8000a888;

		// The note plus the tune word, in semitones Q16, clamped to notes 0-127
		const int32_t tune = static_cast<int32_t>(static_cast<uint32_t>(_sram.half(_params + 0x24)) << 16);
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
		int32_t f = frac(0x6e000000, _sram.at(Exp2 + 4 * static_cast<uint32_t>(fraction)));
		const int32_t shift = (5 - octave) & 63;	// asr.l takes its count modulo 64
		f = shift >= 32 ? (f < 0 ? -1 : 0) : f >> shift;

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
			t = frac(t, 0x57619f10);
			_v.unit(k, 0x78) = t >> 2;
		}
	}

	namespace
	{
		// The table lookup both oscillators share: index and fraction from the phase sum, linear interpolation
		int32_t lookup(const int32_t* _table, Emac& _phase)
		{
			const int32_t p = _phase.store();
			const int32_t i = (p & 0xffffff) >> 16;
			const int32_t f = static_cast<int32_t>((static_cast<uint32_t>(_phase.storeClear()) & 0xffff) << 15);
			Emac acc;
			acc.load(_table[i]);
			acc.mac(f, _table[i + 1]);
			acc.msac(f, _table[i]);
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
}
