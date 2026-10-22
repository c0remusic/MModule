#include "stEngines.h"

#include "stEmac.h"
#include "stStages.h"

// CP VINTAGE (engine 7): a hand clap. Two noise sources (sparse crackle, and white noise through a one-pole), each
// under its own repeating burst envelope, mixed, through a resonator, a biquad section, a sample-rate reducer and a
// one-pole, then x8 into the clip. A phase-modulated sine pair, under a third envelope, is mixed in last.

namespace syntakt::cpVintage
{
	namespace
	{
		// Scratch buffers in SRAM, as the original lays them out for this engine
		constexpr uint32_t Noise = bufferA(2);		// white noise, unit 2's buffer A
		constexpr uint32_t Tone = bufferB(0);		// unit 0's buffer B: the tone
		constexpr uint32_t ToneModulator = bufferA(1);	// unit 1's buffer A
		constexpr uint32_t ResonatorSteps = 0x80004a50;	// 8 coefficient steps, then two 32-sample buffers
		constexpr uint32_t ResonatorOutA = 0x80004a70;
		constexpr uint32_t ResonatorOutB = 0x80004af0;

		// Tables of section 7 (128 entries unless said)
		constexpr uint32_t DecayB = 0x4003a238;
		constexpr uint32_t DecayA = 0x4003a438;
		constexpr uint32_t PeriodHold = 0x4003a638;
		constexpr uint32_t Period = 0x4003aa38;
		constexpr uint32_t BurstDecay = 0x4003ac38;
		constexpr uint32_t SampleRate = 0x4003a838;
		constexpr uint32_t GainNoise = 0x4003ae38;
		constexpr uint32_t GainCrackle = 0x4003b038;
		constexpr uint32_t ToneDepth = 0x4003b238;
		constexpr uint32_t ToneFeedback = 0x4003b438;
		constexpr uint32_t ToneRatio = 0x4003b638;
		constexpr uint32_t AmpDecay = 0x4003b838;
		constexpr uint32_t BurstDecayLast = 0x4003ba38;
		constexpr uint32_t BiquadPole = 0x4003bc38;
		constexpr uint32_t ResonatorSin = 0x40014b80;	// 1025 entries
		constexpr uint32_t ResonatorCos = 0x40015b80;

		// 1 / sqrt(1 - _s^2) as a mantissa and an exponent: the shared part of 0x400052f8
		int32_t invNorm(const int32_t _s, const Rom& _rom)
		{
			return sqrtLog(_rom, subWrap(INT32_MAX, frac(_s, _s)));
		}

		// 0x400052f8: the resonator's coefficients (8 words at _c) from a pitch word
		void resonatorCoefficients(int32_t _f, int32_t* _c, Sram& _sram, const Rom& _rom)
		{
			if(_f < 0)
				_f = 0;
			if(_f > 0x7fdfff)
				_f = 0x7fdfff;
			const int32_t f = asl(_f & 0xffff, 15);
			const int32_t w = subWrap(INT32_MAX, f);
			const int32_t i = _f >> 13;

			Emac a;
			a.mac(w, _rom.entry(ResonatorSin, i));
			a.mac(f, _rom.entry(ResonatorSin, i + 1));
			_c[1] = neg(a.storeClear());

			a.mac(w, _rom.entry(ResonatorCos, i));
			a.mac(f, _rom.entry(ResonatorCos, i + 1));
			const int32_t cosine = a.storeClear();
			int32_t* g = _sram.ptr(ResonatorSteps);
			g[0] = cosine;

			int32_t d4 = invNorm(cosine, _rom);
			g[1] = asrReg(exp2Entry(_rom, d4), d4 >> 24);

			d4 = invNorm(_c[1], _rom);
			const int32_t d3 = d4 >> 24;
			const int32_t d5n = neg(d4);
			const int32_t d6 = d5n >> 24;
			const int32_t e4 = asrReg(exp2Entry(_rom, d4), d3);
			const int32_t e5 = exp2Entry(_rom, d5n);

			_c[4] = e4;
			const int32_t s = _c[1];
			const int32_t g1 = g[1];
			Emac acc0, acc1;
			acc0.mac(g1, e4);
			_c[7] = g1;
			acc1.mac(s, e5);
			const int32_t c0 = acc0.storeClear() >> 8;
			_c[0] = c0;
			acc0.mac(acc1.storeClear(), c0);
			const int32_t c3 = aslReg(acc0.storeClear(), neg(d6));
			const int32_t g0 = g[0];
			acc0.mac(e4, g0);
			acc1.mac(s, g0);
			_c[6] = g0 >> 8;
			_c[3] = c3;
			_c[2] = acc0.storeClear();
			_c[5] = acc1.storeClear();
		}

		// 0x40005728: burst envelope _k (fields at 0x5a4 + 0x34 k): attack, decay, retriggered every period for a
		// number of repeats, or held while the gate is
		void burstEnvelope(const int32_t _k, Voice& _v)
		{
			const uint32_t e = 0x34 * static_cast<uint32_t>(_k);
			const uint32_t u = Voice::UnitStride * static_cast<uint32_t>(_k);
			const int32_t period = _v.at(e + 0x5d4);
			if(static_cast<uint32_t>(period) <= static_cast<uint32_t>(_v.at(e + 0x5b4)))
			{
				const int32_t count = _v.at(e + 0x5d0);
				if(count < _v.at(e + 0x5cc))
				{
					_v.at(e + 0x5d0) = count + 1;
					_v.at(e + 0x5b0) = 0;
					_v.at(u + 0x84) = 0;
					_v.at(u + 0x7c) = 0;
					_v.at(e + 0x5a4) = 0;
					_v.at(e + 0x5b4) = subWrap(_v.at(e + 0x5b4), period);
				}
				if(!_v.at(e + 0x5b8) && _v.at(e + 0x5c0))
				{
					_v.at(e + 0x5d0) = 0;
					_v.at(e + 0x5b0) = 0;
					_v.at(u + 0x84) = 0;
					_v.at(u + 0x7c) = 0;
					_v.at(e + 0x5a4) = 0;
					_v.at(e + 0x5b4) = subWrap(_v.at(e + 0x5b4), period);
				}
			}

			if(_v.at(Voice::TrigNow))
			{
				_v.at(e + 0x5b8) = 0;
				_v.at(e + 0x5b4) = 0;
				_v.at(e + 0x5d0) = 0;
				const int32_t attack = _v.at(e + 0x5bc);
				if(attack == INT32_MIN)
					_v.at(e + 0x5a4) = attack;
				else if(_v.at(e + 0x5ac))
					_v.at(e + 0x5a4) = attack >> 1;
				_v.at(e + 0x5b0) = 0;
			}

			if(!_v.at(e + 0x5b0))
			{
				const int32_t level = _v.at(e + 0x5a4);
				if(level == INT32_MIN)
					_v.at(e + 0x5b0) = 1;
				else
					_v.at(e + 0x5a4) = addSat(level, _v.at(e + 0x5bc));
			}
			else
			{
				const int32_t c = _v.at(e + 0x5cc) > _v.at(e + 0x5d0) ? _v.at(e + 0x5c8) : _v.at(e + 0x5c4);
				Emac acc;
				acc.msac(_v.at(e + 0x5a4), c);
				_v.at(e + 0x5a4) = acc.storeClear();
			}

			if(_v.at(Voice::GateOff) && !_v.at(Voice::TrigPending))
				_v.at(e + 0x5b8) = 1;

			if(_v.at(e + 0x5c0))
				_v.at(e + 0x5b4) = addWrap(_v.at(e + 0x5b4), _v.sram.at(ClockStep));
			else
				_v.at(e + 0x5b4) = addWrap(_v.at(e + 0x5b4), 0x80000);
		}

		// 0x400058e8: burst envelope _k applied to _buf, ramped from the previous block's level, and faded to silence
		// in the block a trig arrives in
		void burstVca(const int32_t _k, int32_t* _buf, Voice& _v)
		{
			const uint32_t e = 0x34 * static_cast<uint32_t>(_k);
			if(_v.at(Voice::TrigPending))
				_v.at(e + 0x5a4) = 0;
			else if(_v.at(Voice::TrigNow) && _v.at(e + 0x5bc) == INT32_MIN)
				_v.at(e + 0x5a8) = _v.at(e + 0x5a4);

			const int32_t target = _v.at(e + 0x5a4);
			int32_t g = _v.at(e + 0x5a8);
			const int32_t step = subSat(target, g) >> 5;
			for(int n = 0; n < BlockSize; ++n)
			{
				Emac acc;
				acc.msac(g, _buf[n]);
				g = addSat(g, step);
				_buf[n] = acc.storeClear();
			}
			_v.at(e + 0x5a8) = target;
		}

		// 0x400054e6: a two-pole resonator (states at _c[16], _c[17]) then a feed-forward stage, coefficients ramped
		// from _c[8..15] to _c[0..7] over the block
		void resonator(int32_t* _buf, int32_t* _c, Sram& _sram)
		{
			int32_t* g = _sram.ptr(ResonatorSteps);
			for(int i = 0; i < 8; ++i)
				g[i] = subWrap(_c[i], _c[i + 8]) >> 5;
			int32_t* outA = _sram.ptr(ResonatorOutA);
			int32_t* outB = _sram.ptr(ResonatorOutB);

			int32_t c0 = _c[8], c1 = _c[9], c2 = _c[10], c3 = _c[11], c4 = _c[12], c5 = _c[13];
			int32_t y1 = _c[16], y2 = _c[17];
			Emac acc0, acc1;
			for(int n = 0; n < BlockSize; ++n)
			{
				const int32_t x = _buf[n];
				acc0.mac(c0, x);
				c0 = addWrap(c0, g[0]);
				acc0.mac(c1, y1);
				c1 = addWrap(c1, g[1]);
				acc0.msac(c2, y2);
				c2 = addWrap(c2, g[2]);
				acc1.msac(c3, x);
				c3 = addWrap(c3, g[3]);
				acc1.mac(c4, y1);
				c4 = addWrap(c4, g[4]);
				acc1.mac(c5, y2);
				y1 = acc0.storeClear();
				c5 = addWrap(c5, g[5]);
				outA[n] = y1 >> 1;
				y2 = acc1.storeClear();
				outB[n] = y2;
			}
			_c[16] = y1;
			_c[17] = y2;

			int32_t c6 = _c[14], c7 = _c[15];
			int32_t z = _c[18];
			for(int n = 0; n < BlockSize; ++n)
			{
				const int32_t x = _buf[n];
				acc0.mac(c6, x);
				acc0.mac(c7, z);
				z = outB[n];
				c6 = addWrap(c6, g[6]);
				c7 = addWrap(c7, g[7]);
				acc1.mac(0x400000, x);
				const int32_t e = acc0.storeClear();
				acc1.msac(0x40000000, e);
				const int32_t r = acc1.storeClear();
				const int32_t r7 = asl(r, 7);
				_buf[n] = addSat(r7, r7);
			}
			_c[18] = z;

			for(int i = 0; i < 8; ++i)
				_c[i + 8] = _c[i];
		}

		// 0x40004f74: a first-order section y = c0 x + c1 x1 - c2 y1, its coefficients ramped from _c[3..5] to
		// _c[0..2] over the block, its state at _c[6] (x1) and _c[7] (y1)
		void rampedPole(int32_t* _buf, int32_t* _c)
		{
			const int32_t s0 = subWrap(_c[0], _c[3]) >> 5;
			const int32_t s1 = subWrap(_c[1], _c[4]) >> 5;
			const int32_t s2 = subWrap(_c[2], _c[5]) >> 5;
			int32_t c0 = _c[3], c1 = _c[4], c2 = _c[5];
			int32_t x = _c[6], y = _c[7];
			for(int n = 0; n < BlockSize; ++n)
			{
				Emac acc;
				acc.mac(c1, x);
				x = _buf[n];
				acc.mac(c0, x);
				acc.msac(c2, y);
				c0 = addWrap(c0, s0);
				c1 = addWrap(c1, s1);
				c2 = addWrap(c2, s2);
				y = acc.storeClear();
				_buf[n] = y;
			}
			_c[6] = x;
			_c[7] = y;
			_c[3] = _c[0];
			_c[4] = _c[1];
			_c[5] = _c[2];
		}

	}

	void update(const int32_t _pitch, Voice& _v, const uint32_t _params, const Rom& _rom)
	{
		Sram& sram = _v.sram;
		_v.at(0x40) = static_cast<int32_t>(0xffc00000);	// -64 semitones
		for(const uint32_t o : {0xb0u, 0xb4u, 0x130u, 0x134u})
			_v.at(o) = static_cast<int32_t>(Sine);
		_v.at(0x9c) = static_cast<int32_t>(ToneModulator);
		_v.at(0x44) = 0x00800000;
		_v.at(0x50) = 0x08000000;
		_v.at(0x8c) = frac(0x01000000, interpolate(_rom.ptr(ToneDepth), machineWord(sram, _params, 4)));
		_v.at(0xd0) = interpolate(_rom.ptr(ToneRatio), machineWord(sram, _params, 4));
		_v.at(0x2c) = interpolate(_rom.ptr(ToneFeedback), machineWord(sram, _params, 4));
		_v.at(0x588) = 0x2a3d70a4;						// the crackle's threshold
		_v.at(0x59c) = interpolate(_rom.ptr(SampleRate), machineWord(sram, _params, 8));

		// The note with the tune word, as 0x40002596 computes it
		int32_t p = addSat(_v.at(0x40), frac(_v.at(0x44), asl(machineWord(sram, _params, 2), 16)));
		p = addSat(p, _pitch);
		if(p < 0)
			p = 0;
		else if(p > 0x7f0000)
			p = 0x7f0000;
		resonatorCoefficients(addSat(p, 0x180000), _v.sram.ptr(_v.address + 0x520), sram, _rom);

		// The biquad's pole from the note, read from a table with a 16-bit fraction as weight
		{
			const int32_t i = p >> 16;
			const int32_t f = p & 0xffff;
			const int32_t w = subSat(INT32_MAX, f);
			const int32_t t0 = _rom.entry(BiquadPole, i);
			const int32_t i1 = i + 1 > 0x7f ? 0x7f : i + 1;
			Emac acc;
			acc.mac(t0, w);
			acc.mac(_rom.entry(BiquadPole, i1), f);
			const int32_t pole = acc.storeClear();
			const int32_t c0 = subSat(0x40000000, pole >> 1);
			_v.at(0x47c) = c0;
			_v.at(0x480) = neg(c0);
			const int32_t h = subSat(0x40000000, static_cast<int32_t>(0xc2770223));
			_v.at(0x4bc) = h;
			_v.at(0x4c0) = neg(h);
			_v.at(0x4c4) = static_cast<int32_t>(0x84ee0447);
			_v.at(0x4a4) = static_cast<int32_t>(0x827d249e);
			_v.at(0x484) = pole;
			const int32_t l = addSat(0x40000000, static_cast<int32_t>(0xc13e924f));
			_v.at(0x49c) = l;
			_v.at(0x4a0) = l;
		}

		_v.at(0x08) = interpolate(_rom.ptr(GainCrackle), machineWord(sram, _params, 6));
		const int32_t gainNoise = interpolate(_rom.ptr(GainNoise), machineWord(sram, _params, 6));
		_v.at(0x14) = 0x64f40316;
		_v.at(0x0c) = gainNoise;
		_v.at(0x10) = 0x3298b05c;

		if(_v.at(Voice::TrigNow))
		{
			_v.at(0x414) = 0x0096b8c4;
			_v.at(0x418) = 0x0096b8c4;
			_v.at(0x41c) = static_cast<int32_t>(0x812d7188);
			_v.at(0x420) = 0x0034714f;
			_v.at(0x424) = 0x0034714f;
			_v.at(0x428) = static_cast<int32_t>(0x8068e29d);
			if(machineWord(sram, _params, 12))
			{
				_v.at(Voice::DriveX1) = INT32_MAX;
				_v.at(Voice::DriveY1) = INT32_MAX;
				_v.at(Voice::DriveFloor) = 0x40000000;
				_v.at(Voice::DrivePreGain) = 0x60000000;
				_v.at(Voice::DriveGain) = 0x66666666;
				_v.at(Voice::DriveOn) = 1;
			}
			else
			{
				_v.at(Voice::DriveFloor) = INT32_MAX;
				_v.at(Voice::DrivePreGain) = 0x20000000;
				_v.at(Voice::DriveGain) = INT32_MAX;
				_v.at(Voice::DriveOn) = 0;
			}
			_v.at(0x1c) = gainNoise;			// no ramp of the mix's levels at the hit
			_v.at(0x20) = 0x3298b05c;
			_v.at(0x600) = 3;
			_v.at(0x5cc) = 3;
			for(const uint32_t o : {0x4b4u, 0x4b8u, 0x494u, 0x498u, 0x4d4u, 0x4d8u, 0x4f4u, 0x4f8u, 0x560u, 0x564u})
				_v.at(o) = 0;
			_v.at(0x24) = 0x64f40316;
			_v.at(0x634) = 0;
			const int32_t hold = _v.at(Voice::AmpHold);
			_v.at(0x488) = _v.at(0x47c);
			_v.at(0x48c) = _v.at(0x480);
			_v.at(0x490) = _v.at(0x484);
			_v.at(0x18) = _v.at(0x08);
			_v.at(0x5c0) = hold;
			_v.at(0x5f4) = hold;
			_v.at(0x628) = hold;
			_v.at(0x94) = _v.at(0x8c);
		}

		const int32_t b8 = machineByte(sram, _params, 8);
		const int32_t b10 = machineByte(sram, _params, 10);
		const int32_t b16 = machineByte(sram, _params, 16);
		_v.at(0x5bc) = INT32_MIN;
		_v.at(0x5c8) = _rom.entry(BurstDecay, b8);
		_v.at(0x5c4) = _rom.entry(BurstDecayLast, b16);
		_v.at(0x5f0) = INT32_MIN;
		_v.at(0x5d4) = _rom.entry(Period, b8);
		_v.at(0x5fc) = _rom.entry(BurstDecay, b8);
		_v.at(0x5f8) = _rom.entry(AmpDecay, b16);
		_v.at(Voice::AmpAttack) = INT32_MIN;
		_v.at(0x608) = _rom.entry(Period, b8);
		_v.at(0x624) = static_cast<int32_t>(0xd89d89d9);
		_v.at(Voice::AmpDecay) = _rom.entry(AmpDecay, b16);
		_v.at(0x63c) = 0;
		_v.at(0x630) = 0;
		_v.at(0xd4) = static_cast<int32_t>(0xd89d89d9);
		_v.at(0x62c) = _rom.entry(DecayA, b10);
		_v.at(0xd8) = _rom.entry(DecayB, b10);

		if(_v.at(Voice::AmpHold))
		{
			_v.at(0x5cc) = 0;
			_v.at(0x600) = 0;
			_v.at(0x634) = 0;
			const int32_t period = _rom.entry(PeriodHold, b8);
			_v.at(0x5d4) = period;
			_v.at(0x608) = period;
			_v.at(0x63c) = period;
		}
		else
		{
			_v.at(0x5c0) = 0;
			_v.at(0x5f4) = 0;
			_v.at(0x628) = 0;
		}

		if(_v.at(Voice::TrigPending))
		{
			_v.at(0x5cc) = 0;
			_v.at(0x5b0) = 1;
			_v.at(0x600) = 0;
			_v.at(0x5e4) = 1;
			_v.at(0x634) = 0;
			_v.at(0x618) = 1;
		}

		phaseIncrements48k(_pitch, _v, _params, 2);
	}

	void render(int32_t* _out, Voice& _v, const Rom& _rom)
	{
		Sram& sram = _v.sram;
		const auto field = [&](const uint32_t _o) { return sram.ptr(_v.address + _o); };

		burstEnvelope(0, _v);
		burstEnvelope(1, _v);
		burstEnvelope(2, _v);
		unitEnvelopes(_v, 2);
		scaledFeedbackOscillator(1, _v, _rom);
		pmOscillator(0, _v, _rom);
		gatedNoise(_out, field(0x578));
		drawRandom(sram.ptr(Noise), BlockSize, field(0x580));
		onePole(sram.ptr(Noise), field(0x49c));
		burstVca(0, _out, _v);
		burstVca(1, sram.ptr(Noise), _v);
		burstVca(2, sram.ptr(Tone), _v);
		mix(sram.ptr(Noise), _out, _out, 2, 3, _v);
		resonator(_out, field(0x520), sram);
		rampedPole(_out, field(0x47c));
		reduceRate(_out, field(0x58c));
		onePole(_out, field(0x4bc));
		for(int n = 0; n < BlockSize; ++n)
		{
			// x8, saturated at each doubling
			int32_t x = _out[n];
			x = addSat(x, x);
			x = addSat(x, x);
			_out[n] = addSat(x, x);
		}
		mix(sram.ptr(Tone), _out, _out, 0, 1, _v);
		driveClip(_out, _v);
	}
}
