#include "stTrack.h"

#include "stEmac.h"
#include "stStages.h"

namespace syntakt::track
{
	namespace
	{

		// The drive's state and scratch in SRAM
		constexpr uint32_t DriveGain = 0x800083f0;		// this block's gains, then kept as the next block's start
		constexpr uint32_t DriveGainStep = 0x80008410;
		constexpr uint32_t DriveGainPrev = 0x80008430;
		constexpr uint32_t DriveDryPrev = 0x800083d0;
		constexpr uint32_t DriveWetPrev = 0x800083b0;
		constexpr uint32_t DriveWetEven = 0x80008450;	// the clipped signal of the pair's even track
		constexpr uint32_t DriveWetOdd = 0x800084d0;
		constexpr uint32_t DriveWetLevel = 0x800081b0;	// 128 entries, RAM, copied from section 7 at boot

		constexpr uint32_t BandState = 0x800088a0;		// lp, hp of each track
		constexpr uint32_t BandPoles = 0x40040060;		// one-pole coefficients, section 7

		constexpr uint32_t AttackTable = 0x40014070;
		constexpr uint32_t HoldTable = 0x40013c70;
		constexpr uint32_t DecayTable = 0x40013e70;
		constexpr int32_t KillCoef = static_cast<int32_t>(0x8148c842);
	}

	void drive(Sram& _sram, const uint32_t _out, const uint32_t _params)
	{
		// The gain from the drive word, squared and mapped into 0.1535 .. 1.0, ramped from the previous block's
		for(uint32_t t = 0; t < Voices; ++t)
		{
			const int32_t w = _sram.half(_params + ParamStride * t + 0x50);
			_sram.at(DriveGain + 4 * t) = addWrap(w * w, w * w);
		}
		constexpr int32_t c = 0x6c5a83ad, k = 0x13a57c52;
		for(uint32_t t = 0; t < Voices; ++t)
		{
			const int32_t g = addWrap(fracWrap(c, _sram.at(DriveGain + 4 * t)), k);
			_sram.at(DriveGain + 4 * t) = g;
			_sram.at(DriveGainStep + 4 * t) = subWrap(g, _sram.at(DriveGainPrev + 4 * t)) >> 5;
		}

		for(uint32_t i = 0; i < Voices / 2; ++i)
		{
			const uint32_t even = 2 * i, odd = 2 * i + 1;
			int32_t* xe = _sram.ptr(_out + TrackOutStride * even);
			int32_t* xo = _sram.ptr(_out + TrackOutStride * odd);
			int32_t* wetEven = _sram.ptr(DriveWetEven);
			int32_t* wetOdd = _sram.ptr(DriveWetOdd);
			int32_t ge = _sram.at(DriveGainPrev + 4 * even), go = _sram.at(DriveGainPrev + 4 * odd);
			const int32_t stepE = _sram.at(DriveGainStep + 4 * even), stepO = _sram.at(DriveGainStep + 4 * odd);

			// The soft clip 2u - u|u| three times over, on the accumulator, its guard bits carried from pass to pass
			EmacWrap accO, accE;
			accO.mac(go, xo[0]);
			for(int n = 0; n < BlockSize; ++n)
			{
				accE.mac(ge, xe[n]);
				go = addWrap(go, stepO);
				ge = addWrap(ge, stepE);
				const int32_t so = accO.store() >> 31, se = accE.store() >> 31;
				for(int pass = 0; pass < 3; ++pass)
				{
					int32_t d = accO.store();
					accO.msac(INT32_MIN, d);
					accO.msac(d, d ^ so);
					d = accE.store();
					accE.msac(INT32_MIN, d);
					accE.msac(d, d ^ se);
				}
				wetOdd[n] = accO.storeClear();
				accO.mac(go, xo[n + 1]);
				wetEven[n] = accE.storeClear();
			}

			// Dry and wet levels: the drive fades in over the word's first steps, the clean signal at half level
			int32_t dry[2], wet[2];
			for(uint32_t j = 0; j < 2; ++j)
			{
				const int32_t w = _sram.half(_params + ParamStride * (even + j) + 0x50);
				int32_t d, wetRaw;
				if(w <= 0)
				{
					d = INT32_MAX;
					wetRaw = 0;
				}
				else if(w <= 0x7ff)
				{
					wetRaw = static_cast<int32_t>(static_cast<uint32_t>(w) << 20);
					d = subWrap(INT32_MAX, wetRaw);
				}
				else
				{
					d = 0;
					wetRaw = INT32_MAX;
				}
				dry[j] = d >> 1;
				EmacWrap acc;
				acc.msac(wetRaw, _sram.at(DriveWetLevel + 4 * static_cast<uint32_t>(w >> 8)));
				wet[j] = acc.storeClear();
			}

			int32_t de = _sram.at(DriveDryPrev + 4 * even), doo = _sram.at(DriveDryPrev + 4 * odd);
			int32_t we = _sram.at(DriveWetPrev + 4 * even), wo = _sram.at(DriveWetPrev + 4 * odd);
			const int32_t sde = subWrap(dry[0], de) >> 5, sdo = subWrap(dry[1], doo) >> 5;
			const int32_t swe = subWrap(wet[0], we) >> 5, swo = subWrap(wet[1], wo) >> 5;
			for(int n = 0; n < BlockSize; ++n)
			{
				EmacWrap a0, a1;
				a0.mac(de, xe[n]);
				a0.mac(we, wetEven[n]);
				a1.mac(doo, xo[n]);
				a1.mac(wo, wetOdd[n]);
				de = addWrap(de, sde);
				we = addWrap(we, swe);
				doo = addWrap(doo, sdo);
				wo = addWrap(wo, swo);
				xe[n] = a0.storeClear();
				xo[n] = a1.storeClear();
			}
			_sram.at(DriveDryPrev + 4 * even) = dry[0];
			_sram.at(DriveDryPrev + 4 * odd) = dry[1];
			_sram.at(DriveWetPrev + 4 * even) = wet[0];
			_sram.at(DriveWetPrev + 4 * odd) = wet[1];
		}

		for(uint32_t t = 0; t < Voices; ++t)
			_sram.at(DriveGainPrev + 4 * t) = _sram.at(DriveGain + 4 * t);
	}

	void baseWidth(Sram& _sram, const Rom& _rom, const uint32_t _out, const uint32_t _params)
	{
		const int32_t* poles = _rom.ptr(BandPoles);
		for(uint32_t t = 0; t < Voices; t += 2)
		{
			const uint32_t p = _params + ParamStride * t, q = p + ParamStride;
			int32_t* state = _sram.ptr(BandState + 8 * t);

			// The high-pass pole from BASE, the low-pass pole from BASE + WIDTH
			const int32_t b0 = _sram.half(p + 0x66), b1 = _sram.half(q + 0x66);
			const int32_t ph0 = poles[b0 >> 6], pl0 = poles[addWrap(b0, _sram.half(p + 0x68)) >> 6];

			// A trig seen last block resets the state; the odd track's test reads the even track's RST word
			if(_sram.byte(TrigSeen + t) && _sram.half(p + 0x82))
			{
				state[0] = 0;
				state[1] = 0;
			}
			if(_sram.byte(TrigSeen + t + 1) && _sram.half(p + 0x82))
			{
				state[2] = 0;
				state[3] = 0;
			}
			const int32_t ph1 = poles[b1 >> 6], pl1 = poles[addWrap(b1, _sram.half(q + 0x68)) >> 6];

			const int32_t kl0 = addWrap(INT32_MIN, pl0), kh0 = addWrap(ph0 >> 1, 0x40000000);
			const int32_t kl1 = addWrap(INT32_MIN, pl1), kh1 = addWrap(ph1 >> 1, 0x40000000);
			int32_t lp0 = state[0], hp0 = state[1], lp1 = state[2], hp1 = state[3];
			int32_t* x0 = _sram.ptr(_out + TrackOutStride * t);
			int32_t* x1 = _sram.ptr(_out + TrackOutStride * (t + 1));

			Emac acc0, acc1, acc2, acc3;
			acc0.mac(pl0, lp0);
			for(int n = 0; n < BlockSize; ++n)
			{
				acc0.msac(kl0, x0[n]);
				acc2.msac(kh0, lp0);
				acc2.mac(ph0, hp0);
				acc1.mac(pl1, lp1);
				acc1.msac(kl1, x1[n]);
				lp0 = acc0.storeClear();
				acc2.mac(kh0, lp0);
				acc3.msac(kh1, lp1);
				acc3.mac(ph1, hp1);
				lp1 = acc1.storeClear();
				acc3.mac(kh1, lp1);
				acc0.mac(pl0, lp0);
				hp0 = acc2.storeClear();
				x0[n] = hp0;
				hp1 = acc3.storeClear();
				x1[n] = hp1;
			}
			state[0] = lp0;
			state[1] = hp0;
			state[2] = lp1;
			state[3] = hp1;
		}
	}

	uint32_t ampSegment(Sram& _sram, uint32_t _buf, const uint32_t _level, const int32_t _coef, const int32_t _target,
		const int32_t _n)
	{
		const int32_t k = addWrap(INT32_MAX, _coef);
		int32_t level = _sram.at(_level);
		for(int32_t n = 0; n < _n; ++n)
		{
			EmacWrap out, next;
			out.msac(level, _sram.at(_buf));
			next.msac(level, _coef);
			next.msac(_target, k);
			_sram.at(_buf) = out.storeClear();
			_buf += 4;
			level = next.storeClear();
		}
		_sram.at(_level) = level;
		return _buf;
	}

	void ampEnvelope(Sram& _sram, const Rom& _rom, uint32_t _out, const uint32_t _env, const uint32_t _params,
		const int32_t _trig, const int32_t _gateOff, const int32_t _kill)
	{
		const int32_t hold = static_cast<int8_t>(_sram.byte(_params + 4));
		const int32_t mode = static_cast<int8_t>(_sram.byte(_params + 22));
		const int32_t attack = static_cast<int8_t>(_sram.byte(_params + 2));
		const int32_t decay = static_cast<int8_t>(_sram.byte(_params + 6));
		const int32_t sustain = mode == 1 ? static_cast<int32_t>(static_cast<uint32_t>(_sram.half(_params + 8)) << 16)
			: 0;
		const int32_t release = static_cast<int8_t>(_sram.byte(_params + 10));
		const int32_t reset = static_cast<int8_t>(_sram.byte(_params + 24));

		int32_t& stage = _sram.at(_env);
		int32_t& holdCount = _sram.at(_env + 4);
		int32_t& level = _sram.at(_env + 8);
		const uint32_t levelAddress = _env + 8;

		if(_kill)
			stage = 6;
		else if(_trig && reset)
			stage = 5;

		// Segments consume the block's 32 samples
		int32_t left = BlockSize;
		while(true)
		{
			uint32_t end = _out;
			if(stage == 2)
			{
				if(level == INT32_MIN)
				{
					holdCount = 0;
					stage = mode != 0 ? 1 : 0;
				}
				else
				{
					// Linear attack, stopped at the sample where the level saturates
					const int32_t inc = _rom.entry(AttackTable, attack);
					int32_t l = level;
					for(int32_t n = left; n > 0; --n)
					{
						EmacWrap acc;
						acc.msac(_sram.at(end), l);
						const int64_t sum = static_cast<int64_t>(l) + inc;
						const bool overflow = sum > INT32_MAX || sum < INT32_MIN;
						l = addSat(l, inc);
						_sram.at(end) = acc.storeClear();
						end += 4;
						if(overflow)
							break;
					}
					level = l;
				}
			}
			else if(stage == 0)
			{
				const int32_t length = _rom.entry(HoldTable, hold);
				if(length > holdCount)
				{
					int32_t k = length - holdCount;
					if(k < 0)
						k = 0;
					const int32_t m = k < left ? k : left;
					holdCount += m;
					end += 4 * static_cast<uint32_t>(m);	// full gain: the samples stay as they are
				}
				else if(hold == 127)
				{
					end += 4 * static_cast<uint32_t>(left);
				}
				else
				{
					stage = 1;
					level = INT32_MIN;
				}
			}
			else if(stage == 1)
			{
				end = ampSegment(_sram, _out, levelAddress, _rom.entry(DecayTable, decay), sustain, left);
			}
			else if(stage == 5)
			{
				// A linear fade of the old hit; the original reads its first sample once and never again
				const int32_t step = level >> 5;
				int32_t l = level;
				const int32_t x = _sram.at(_out);
				for(int32_t n = 0; n < left; ++n)
				{
					EmacWrap acc;
					acc.msac(l, x);
					l = subWrap(l, step);
					_sram.at(end) = acc.storeClear();
					end += 4;
				}
				level = l;
			}
			else if(stage == 6)
			{
				end = ampSegment(_sram, _out, levelAddress, KillCoef, 0, left);
			}
			else
			{
				end = ampSegment(_sram, _out, levelAddress, _rom.entry(DecayTable, release), 0, left);
			}

			left -= static_cast<int32_t>((end - _out) >> 2);
			if(left == 0)
				break;
			_out = end;
		}

		bool gateOff = false;
		if(_trig)
		{
			if(reset)
				level = attack == 0 ? INT32_MIN : _rom.entry(AttackTable, attack) >> 1;
			stage = 2;
			gateOff = _gateOff != 0;
		}
		else if(_gateOff)
		{
			if(mode == 1)
			{
				stage = 4;
				return;
			}
			gateOff = true;
		}
		if(gateOff && mode == 0 && hold == 127)
		{
			if(stage == 0)
				level = INT32_MIN;
			stage = 1;
		}
	}

	void ampEnvelopes(Sram& _sram, const Rom& _rom, const uint32_t _out, const uint32_t _params, const int32_t _trig,
		const int32_t _gateOff, const int32_t _kill)
	{
		const int32_t kill = _kill & 0xff;
		for(uint32_t t = 0; t < Voices; ++t)
		{
			ampEnvelope(_sram, _rom, _out + TrackOutStride * t, 0x800049e0 + 12 * t, _params + ParamStride * t + 0x6a,
				(_trig >> t) & 1, (_gateOff >> t) & 1, (kill >> t) & 1);
		}
	}
}
