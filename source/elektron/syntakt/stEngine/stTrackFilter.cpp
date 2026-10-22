#include "stTrack.h"

#include "stEmac.h"

namespace syntakt::track
{
	namespace
	{
		// The filter's state and scratch in SRAM
		constexpr uint32_t SnapPending = 0x80008558;	// byte per track
		constexpr uint32_t TypeInUse = 0x80008560;
		constexpr uint32_t StageZ = 0x80008580;
		constexpr uint32_t Steps = 0x80008720;		// 8 longs, also temporaries
		constexpr uint32_t Current = 0x80008740;	// 8 longs per track
		constexpr uint32_t MixGains = 0x80008840;	// 3 longs per track
		constexpr uint32_t StageGain = 0x800088e0;
		constexpr uint32_t Target = 0x80008900;		// 8 longs
		constexpr uint32_t State = 0x80008920;		// s1, s2 per track
		constexpr uint32_t BufA = 0x80008960;
		constexpr uint32_t BufB = 0x800089e0;

		// ff1.l: leading zeros, 32 for 0
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

		// The log step of the inlined square root: L, -log2(sqrt v) in Q8.24
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

		int32_t expIndex(const int32_t _l)
		{
			return subWrap(0x1000000, _l & 0xffffff) >> 14;
		}

		// sqrt v from L
		int32_t sqrtOf(const Rom& _rom, const int32_t _l)
		{
			return asrReg(_rom.entry(0x4000e488, expIndex(_l)), asrReg(_l, 24));
		}

		// 1/sqrt v from L, as a mantissa and the shift that goes with it
		int32_t invMantissa(const Rom& _rom, const int32_t _l)
		{
			return _rom.entry(0x4000e488, expIndex(neg(_l)));
		}

		int32_t invShift(const int32_t _l)
		{
			return neg(asrReg(neg(_l), 24));
		}


		void pathA(Sram& _sram, const Rom& _rom, const int32_t _f, const int32_t _r, Emac& _acc0, Emac& _acc1)
		{
			const int32_t d5 = _f >> 22;
			const int32_t i1 = addWrap(d5, _rom.entry(0x40013870, _r >> 24) >> 22);

			_acc0.msac(_rom.entry(0x40013070, i1), _r);
			int32_t d0 = _acc0.storeClear();
			const int32_t j = d0 >> 24;
			d0 = asl(d0 & 0xffffff, 7);
			const int32_t t0 = _rom.entry(0x40012bcc, j);
			const int32_t t1 = _rom.entry(0x40012bcc, j + 1);
			_acc0.msac(t0, d0);
			_acc0.msac(INT32_MIN, t0);
			_acc0.msac(INT32_MIN, t0);
			_acc0.mac(t1, d0);
			const int32_t i2 = addWrap(_acc0.storeClear() >> 1, d5);

			const int32_t a = _rom.entry(0x40011e70, i1) >> 1;
			const int32_t b = _rom.entry(0x400113cc, i2);
			_acc0.mac(a, b);
			_acc0.mac(INT32_MIN, 0x20000000);
			_acc1.mac(b, b);
			const int32_t u = _acc0.storeClear();
			const int32_t q = _acc1.storeClear() >> 2;
			const int32_t c0 = neg(subWrap(u, q)) >> 1;
			const int32_t q4 = asl(q, 2);
			const int32_t a2 = asl(a, 1);
			_sram.at(Steps) = q4;
			const int32_t t5 = _rom.entry(0x4001082c, i2);
			_sram.at(Target) = c0;
			_sram.at(Steps + 0x14) = c0;
			_acc0.mac(a2, t5);
			const int32_t c1 = _acc0.storeClear();
			_sram.at(Target + 4) = c1;
			_acc1.msac(c1, c1);
			_acc1.mac(INT32_MIN, INT32_MIN);
			const int32_t v = _acc1.storeClear();

			const int32_t l = sqrtLog(_rom, v);
			const int32_t c4 = sqrtOf(_rom, l);
			const int32_t m = invMantissa(_rom, l);
			_sram.at(Target + 0x10) = c4;

			_acc0.mac(c1, m);
			const int32_t p = _acc0.storeClear();
			_acc0.mac(p, _sram.at(Target));
			const int32_t c3 = aslReg(_acc0.storeClear(), invShift(l));

			const int32_t q4r = _sram.at(Steps);
			_acc0.mac(q4r, _sram.at(Target + 0x10));
			_acc1.mac(c1, q4r);
			_sram.at(Target + 0xc) = c3;
			_sram.at(Target + 8) = _acc0.storeClear();
			_sram.at(Target + 0x14) = _acc1.storeClear();
		}

		void pathB(Sram& _sram, const Rom& _rom, const int32_t _f, const int32_t _r, const int32_t _m, Emac& _acc0,
			Emac& _acc1)
		{
			const int32_t fi = _f >> 21;
			_sram.at(Target + 4) = neg(_rom.entry(0x4000d488, fi));

			_acc0.mac(_rom.entry(0x4000c488, fi), _rom.entry(0x4000b488, _m >> 21));
			int32_t e = _acc0.storeClear();
			int32_t k1 = 0x20000000;
			if(_r < 0x40000000)
				k1 = _rom.entry(0x4000a188, _r >> 24);
			_acc0.mac(_rom.entry(0x40009f88, _r >> 24), e);
			e = _acc0.storeClear();
			_acc0.mac(k1, e);
			e = _acc0.storeClear();
			if(e >= 0x16a09e6)
				e = 0x16a09e6;

			const int32_t fr = asl(e & 0x7fff, 16);
			const int32_t idx = (e & 0x1ffffff) >> 15;
			_acc0.mac(subWrap(INT32_MAX, fr), _rom.entry(0x4000a488, idx));
			_acc0.mac(fr, _rom.entry(0x4000a488, idx + 1));
			const int32_t g = _acc0.storeClear();
			_sram.at(Steps) = g;
			_acc0.mac(g, g);
			const int32_t h = sqrtOf(_rom, sqrtLog(_rom, subWrap(INT32_MAX, _acc0.storeClear())));
			_sram.at(Steps + 4) = h;

			const int32_t c1 = _sram.at(Target + 4);
			_acc0.mac(c1, c1);
			const int32_t l = sqrtLog(_rom, subWrap(INT32_MAX, _acc0.storeClear()));
			const int32_t c4 = sqrtOf(_rom, l);
			const int32_t m = invMantissa(_rom, l);
			_sram.at(Target + 0x10) = c4;

			const int32_t hr = _sram.at(Steps + 4);
			_acc0.mac(hr, c4);
			_sram.at(Target + 0x1c) = hr >> 1;
			_acc1.mac(c1, m);
			const int32_t c0 = (_acc0.storeClear() >> 2) >> 7;
			_sram.at(Target) = c0;
			_acc0.mac(_acc1.storeClear(), c0);
			const int32_t c3 = aslReg(_acc0.storeClear(), invShift(l));

			const int32_t gr = _sram.at(Steps);
			_acc0.mac(c4, gr);
			_acc1.mac(c1, gr);
			_sram.at(Target + 0x18) = (gr >> 1) >> 7;
			_sram.at(Target + 0xc) = c3;
			_sram.at(Target + 8) = _acc0.storeClear();
			_sram.at(Target + 0x14) = _acc1.storeClear();
		}
	}

	void filter(Sram& _sram, const Rom& _rom, const uint32_t _pf, const uint32_t _out, const int32_t _cutoff,
		const int32_t _t, const int32_t _trig, const int32_t _rst)
	{
		const uint32_t t = static_cast<uint32_t>(_t);

		int32_t r = static_cast<int32_t>(static_cast<uint32_t>(static_cast<uint16_t>(_sram.half(_pf + 4))) << 16);
		if(r < 0)
			r = 0;
		if(r > 0x7f000000)
			r = 0x7f000000;

		const int32_t typeCur = _sram.at(TypeInUse + 4 * t);

		Emac acc0;
		Emac acc1;

		int32_t m = 0;
		switch(typeCur)
		{
		case 3: m = 0x17000000; break;
		case 4: m = 0x2b000000; break;
		case 5: m = 0x53000000; break;
		case 6: m = 0x7f000000; break;
		default: break;
		}

		if(typeCur == 0 || typeCur == 1 || typeCur == 7)
			pathA(_sram, _rom, _cutoff, r, acc0, acc1);
		else if(static_cast<uint32_t>(typeCur - 2) <= 4)
			pathB(_sram, _rom, _cutoff, r, m, acc0, acc1);

		// Ramp the coefficients toward the target, or snap to it
		const uint8_t trigSeen = _sram.byte(TrigSeen + t);
		const bool snap = (trigSeen && _rst != 0) || _sram.byte(SnapPending + t);
		const uint32_t cur = Current + 32 * t;
		if(snap)
		{
			for(uint32_t i = 0; i < 8; ++i)
			{
				_sram.at(cur + 4 * i) = _sram.at(Target + 4 * i);
				_sram.at(Steps + 4 * i) = 0;
			}
			_sram.at(State + 8 * t) = 0;
			_sram.at(State + 8 * t + 4) = 0;
			_sram.setByte(SnapPending + t, 0);
		}
		else
		{
			for(uint32_t i = 0; i < 8; ++i)
				_sram.at(Steps + 4 * i) = rampStepToZero(_sram.at(Target + 4 * i), _sram.at(cur + 4 * i));
		}

		// The state-space resonator
		{
			int32_t c0 = _sram.at(cur);
			int32_t c1 = _sram.at(cur + 4);
			int32_t c2 = _sram.at(cur + 8);
			int32_t c3 = _sram.at(cur + 12);
			int32_t c4 = _sram.at(cur + 16);
			int32_t c5 = _sram.at(cur + 20);
			int32_t s1 = _sram.at(State + 8 * t);
			int32_t s2 = _sram.at(State + 8 * t + 4);

			for(uint32_t n = 0; n < 32; ++n)
			{
				const int32_t x = _sram.at(_out + 4 * n);
				acc0.mac(c0, x);
				acc0.mac(c0, x);
				acc0.mac(c0, x);
				acc0.mac(c0, x);
				c0 = addWrap(c0, _sram.at(Steps));
				acc0.mac(c1, s1);
				c1 = addWrap(c1, _sram.at(Steps + 4));
				acc0.msac(c2, s2);
				c2 = addWrap(c2, _sram.at(Steps + 8));
				acc1.msac(c3, x);
				acc1.msac(c3, x);
				acc1.msac(c3, x);
				acc1.msac(c3, x);
				c3 = addWrap(c3, _sram.at(Steps + 12));
				acc1.mac(c4, s1);
				c4 = addWrap(c4, _sram.at(Steps + 16));
				acc1.mac(c5, s2);
				s1 = acc0.storeClear();
				c5 = addWrap(c5, _sram.at(Steps + 20));
				_sram.at(BufA + 4 * n) = s1 >> 1;
				s2 = acc1.storeClear();
				_sram.at(BufB + 4 * n) = s2;
			}

			_sram.at(State + 8 * t) = s1;
			_sram.at(State + 8 * t + 4) = s2;
			for(uint32_t i = 0; i < 6; ++i)
				_sram.at(cur + 4 * i) = _sram.at(Target + 4 * i);
		}

		// The second stage on B
		const int8_t resoByte = static_cast<int8_t>(_sram.byte(_pf + 4));
		{
			int32_t gt;
			if(static_cast<uint32_t>(typeCur) <= 1 || typeCur == 7)
				gt = _rom.at(0x4000a388);
			else
				gt = _rom.entry(0x4000a288, resoByte);

			int32_t gp = _sram.at(StageGain + 4 * t);
			int32_t c6 = _sram.at(cur + 0x18);
			int32_t c7 = _sram.at(cur + 0x1c);
			int32_t z = _sram.at(StageZ + 4 * t);
			const int32_t gs = subWrap(gt, gp) >> 5;

			for(uint32_t n = 0; n < 32; ++n)
			{
				const int32_t x = _sram.at(_out + 4 * n);
				acc0.mac(c7, z);
				acc0.mac(c6, x);
				z = _sram.at(BufB + 4 * n);
				c6 = addWrap(c6, _sram.at(Steps + 0x18));
				c7 = addWrap(c7, _sram.at(Steps + 0x1c));
				const int32_t xs = (x >> 1) >> 7;
				int32_t y = acc0.storeClear();
				acc0.mac(gp, subWrap(xs, y));
				gp = addSat(gp, gs);
				y = addWrap(y, xs) >> 2;
				int32_t v = addWrap(acc0.storeClear(), y);
				v = asl(v, 6);
				_sram.at(BufB + 4 * n) = addSat(v, v);
			}

			_sram.at(StageZ + 4 * t) = z;
			_sram.at(cur + 0x18) = _sram.at(Target + 0x18);
			_sram.at(cur + 0x1c) = _sram.at(Target + 0x1c);
			_sram.at(StageGain + 4 * t) = _rom.entry(0x4000a288, resoByte);
		}

		// Type change and trig flags
		int32_t typeNew = static_cast<int8_t>(_sram.byte(_pf));
		if(typeNew < 0)
			typeNew = 0;
		else if(typeNew > 7)
			typeNew = 7;

		bool keep = true;
		if(typeNew != typeCur && (static_cast<uint32_t>(typeCur - 2) > 4 || static_cast<uint32_t>(typeNew - 2) > 4))
		{
			_sram.setByte(SnapPending + t, 1);
			keep = false;
		}
		if(trigSeen)
		{
			_sram.setByte(SnapPending + t, 0);
			_sram.setByte(TrigSeen + t, 0);
			keep = true;
		}
		if((static_cast<uint32_t>(_trig) >> t) & 1)
		{
			_sram.setByte(SnapPending + t, 0);
			_sram.setByte(TrigSeen + t, 1);
			keep = true;
		}
		_sram.at(TypeInUse + 4 * t) = typeNew;

		// Output mix: x, A and B, the gains ramped
		int32_t gx = 0;
		int32_t ga = 0;
		int32_t gb = 0;
		if(keep)
		{
			switch(typeCur)
			{
			case 7: gx = 0x20000000; break;
			case 0: ga = INT32_MAX; break;
			case 1: gx = 0x20000000; ga = INT32_MIN; break;
			default: gb = INT32_MAX; break;
			}
		}

		const uint32_t mix = MixGains + 12 * t;
		const int32_t sx = rampStepToZero(gx, _sram.at(mix));
		const int32_t sa = rampStepToZero(ga, _sram.at(mix + 4));
		const int32_t sb = rampStepToZero(gb, _sram.at(mix + 8));
		int32_t px = _sram.at(mix);
		int32_t pa = _sram.at(mix + 4);
		int32_t pb = _sram.at(mix + 8);

		for(uint32_t n = 0; n < 32; ++n)
		{
			acc0.mac(px, _sram.at(_out + 4 * n));
			acc0.mac(pa, _sram.at(BufA + 4 * n));
			acc0.mac(pb, _sram.at(BufB + 4 * n));
			px = addWrap(px, sx);
			pa = addWrap(pa, sa);
			pb = addWrap(pb, sb);
			_sram.at(_out + 4 * n) = acc0.storeClear();
		}

		_sram.at(mix) = gx;
		_sram.at(mix + 4) = ga;
		_sram.at(mix + 8) = gb;
	}
}
