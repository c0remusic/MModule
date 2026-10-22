#include "stEngines.h"

#include "stEmac.h"
#include "stStages.h"

// SY CHORD (engine 5): up to four oscillators, each a crossfade of two neighbouring waves of a wavetable, tuned as a
// chord from one of 38 shapes, mixed with ramped gains, through the drive stage, then two one-pole low-passes whose
// cutoff follows the note, a pitch-sweep envelope and a parameter, and the amp envelope.

namespace syntakt::syChord
{
	namespace
	{
		// Scratch in SRAM: the oscillator's two waves and gains, which it leaves there
		constexpr uint32_t OscillatorScratch = 0x8000a064;

		// Tables of section 7 (128 entries unless said)
		constexpr uint32_t AmpDecay = 0x40025e38;
		constexpr uint32_t EnvAttack = 0x40024c08;	// 40 entries
		constexpr uint32_t SweepDecay = 0x40025c38;
		constexpr uint32_t RatioA = 0x40025208;		// the ratios of shapes 0 to 2
		constexpr uint32_t RatioB = 0x40024e08;
		constexpr uint32_t RatioC = 0x40025408;
		constexpr uint32_t RatioD = 0x40025008;
		constexpr uint32_t Shapes = 0x400259d8;		// 38 rows of 4 words: the ratios of units 1 to 3 at + 4
		constexpr uint32_t Cutoff = 0x40025808;
		constexpr uint32_t Pole = 0x40025608;
		constexpr uint32_t Waves = 0x40014870;		// 32 wave pointers
		constexpr uint32_t Exp2 = 0x8000a888;

		// 0x40002770: wave _n of the wavetable, 1 to 32, else the RAM sine
		uint32_t wave(const int32_t _n, const Rom& _rom)
		{
			const auto i = static_cast<uint32_t>(_n - 1);
			return i <= 31 ? static_cast<uint32_t>(_rom.at(Waves + 4 * i)) : Sine;
		}

		// 0x40002790: the wave position of units 0 to 3 from parameter word 8, its top 6 bits the wave and the next
		// 10 the crossfade into the next one. Each unit keeps the waves and the index of the previous block
		void wavePositions(Voice& _v, const uint32_t _params, const Rom& _rom)
		{
			const int32_t w = machineWord(_v.sram, _params, 8);
			const int32_t index = w >> 10;
			const int32_t position = asl(w & 0x3ff, 21);
			const auto low = static_cast<int32_t>(wave(index, _rom));
			const auto high = static_cast<int32_t>(wave(index + 1, _rom));
			for(uint32_t k = 0; k < 4; ++k)
			{
				_v.unit(k, 0xc4) = _v.unit(k, 0xc0);
				_v.unit(k, 0xb0) = low;
				_v.unit(k, 0xb4) = high;
				_v.unit(k, 0xa4) = position;
				_v.unit(k, 0xc0) = index;
			}
		}

		// 0x40007630: a chord note's gain from its step _n: full outside 0 to 9, 1 - n/10 doubled from 0 to 5, from
		// 6 to 9 the rise of n/10 above 1/2 doubled
		int32_t stepGain(const int32_t _n)
		{
			if(static_cast<uint32_t>(_n) > 9)
				return INT32_MAX;
			const int32_t x = asl(_n, 27);
			if(_n <= 5)
			{
				Emac acc;
				acc.msac(0x1999999a, x);
				int32_t d = asl(acc.storeClear(), 3);
				d = addSat(d, d);
				return addSat(INT32_MAX, d);
			}
			int32_t d = asl(frac(0x1999999a, x), 3);
			d = subSat(d, 0x3fffffff);
			return addSat(d, d);
		}

		// 0x4000768c: the gains of units 1 to 3 in the mix (0x0c-0x14) for the chord shapes above 2, from byte 4;
		// unit 3 silent for the shapes byte 6 names
		void chordGains(Voice& _v, const uint32_t _params)
		{
			const Sram& sram = _v.sram;
			const int32_t b4 = machineByte(sram, _params, 4);
			if(b4 > 31)
			{
				const int32_t n = b4 & 0x1f;
				_v.at(0x0c) = stepGain(n) >> 2;
				_v.at(0x10) = stepGain(n - 10) >> 2;
				_v.at(0x14) = stepGain(n - 20) >> 2;
			}
			else
			{
				for(uint32_t i = 0; i < 3; ++i)
				{
					const int32_t n = b4 - 10 * static_cast<int32_t>(i);
					int32_t g;
					if(n <= 0)
						g = 0;
					else if(n > 10)
						g = INT32_MAX;
					else
					{
						g = asl(frac(0x0ccccccd, asl(n, 27)), 3);
						g = addSat(g, g);
					}
					_v.at(0x0c + 4 * i) = g >> 2;
				}
			}

			const int32_t b6 = machineByte(sram, _params, 6);
			if(static_cast<uint16_t>(b6 - 7) > 28 || static_cast<uint16_t>(b6 - 17) <= 1 || b6 == 21)
				_v.at(0x14) = 0;
		}

		// One wave read at the phase, linearly interpolated
		int32_t lookup(const int32_t* _table, const int32_t _i, const int32_t _f)
		{
			Emac acc;
			acc.load(_table[_i]);
			acc.mac(_f, _table[_i + 1]);
			acc.msac(_f, _table[_i]);
			return acc.storeClear();
		}

		// 0x400032b4: unit _k's oscillator, 32 samples at 48 kHz, two waves crossfaded with gains ramped over the
		// block, into its buffers A (scaled by the unit's envelope) and B (raw). The gains of the low and high wave
		// (0xa8, 0xac) ramp to 1 - position and position; when the wave index moved by one the ramps carry on from
		// the previous block's waves, when it moved more the previous waves fade out and the new ones start next block
		void waveOscillator(const uint32_t _k, Voice& _v, const Rom& _rom)
		{
			Sram& sram = _v.sram;
			int32_t* a = sram.ptr(static_cast<uint32_t>(_v.unit(_k, 0xc8)));
			int32_t* b = sram.ptr(static_cast<uint32_t>(_v.unit(_k, 0xcc)));
			int32_t& low = _v.unit(_k, 0xa8);
			int32_t& high = _v.unit(_k, 0xac);
			const int32_t position = _v.unit(_k, 0xa4);
			const int32_t index = _v.unit(_k, 0xc0);
			const int32_t previous = _v.unit(_k, 0xc4);
			const int32_t oldLow = low, oldHigh = high;
			const int32_t lowTarget = subWrap(0x7f000000, position);

			int32_t wave1 = _v.unit(_k, 0xb0), wave2 = _v.unit(_k, 0xb4);
			int32_t g1, g2, step1, step2;
			if(addWrap(previous, 1) == index)
			{
				// Up one: the old high wave is the new low one
				g1 = oldHigh;
				step1 = subWrap(lowTarget, oldHigh) >> 5;
				g2 = oldLow;
				step2 = neg(oldLow) >> 5;
				low = lowTarget;
				high = 0;
				wave2 = _v.unit(_k, 0xb8);
			}
			else if(subWrap(previous, 1) == index)
			{
				// Down one: the old low wave is the new high one
				g1 = oldHigh;
				step1 = neg(oldHigh) >> 5;
				g2 = oldLow;
				step2 = subWrap(position, oldLow) >> 5;
				low = 0;
				high = position;
				wave1 = _v.unit(_k, 0xbc);
			}
			else if(wave1 != _v.unit(_k, 0xb8))
			{
				g1 = oldLow;
				step1 = neg(oldLow) >> 5;
				g2 = oldHigh;
				step2 = neg(oldHigh) >> 5;
				low = 0;
				high = 0;
				wave1 = _v.unit(_k, 0xb8);
				wave2 = _v.unit(_k, 0xbc);
			}
			else
			{
				g1 = oldLow;
				step1 = subWrap(lowTarget, oldLow) >> 5;
				g2 = oldHigh;
				step2 = subWrap(position, oldHigh) >> 5;
				low = lowTarget;
				high = position;
			}
			_v.unit(_k, 0xb8) = _v.unit(_k, 0xb0);
			_v.unit(_k, 0xbc) = _v.unit(_k, 0xb4);

			const int32_t* t1 = table(sram, _rom, static_cast<uint32_t>(wave1));
			const int32_t* t2 = table(sram, _rom, static_cast<uint32_t>(wave2));
			const int32_t inc = _v.unit(_k, 0x78);
			int32_t amount = _v.unit(_k, Voice::EnvLevelPrev);
			const int32_t amountStep = subSat(_v.unit(_k, Voice::EnvLevel), amount) >> 5;
			int32_t phase = _v.unit(_k, Voice::Phase);

			for(int n = 0; n < BlockSize; ++n)
			{
				const int32_t i = phase >> 16;
				const int32_t f = asl(phase & 0xffff, 15);
				const int32_t s1 = lookup(t1, i, f);
				const int32_t s2 = lookup(t2, i, f);
				Emac acc;
				acc.mac(g1, s1);
				acc.mac(g2, s2);
				g1 = addWrap(g1, step1);
				g2 = addWrap(g2, step2);
				phase = addWrap(phase, inc) & 0xffffff;
				const int32_t s = acc.storeClear();
				acc.msac(amount, s);
				amount = addWrap(amount, amountStep);
				b[n] = s;
				a[n] = acc.storeClear();
			}

			_v.unit(_k, Voice::Phase) = phase;
			_v.unit(_k, Voice::EnvLevelPrev) = _v.unit(_k, Voice::EnvLevel);

			int32_t* scratch = sram.ptr(OscillatorScratch);
			scratch[0] = 15;
			scratch[1] = 16;
			scratch[2] = wave1;
			scratch[3] = wave2;
			scratch[4] = g1;
			scratch[5] = g2;
		}
	}

	void update(const int32_t _pitch, Voice& _v, const uint32_t _params, const Rom& _rom)
	{
		const Sram& sram = _v.sram;
		int32_t pitch = _pitch < 0 ? 0 : _pitch;
		if(pitch > 0x600000)
			pitch = 0x600000;

		_v.at(0x40) = static_cast<int32_t>(0xffc00000);	// -64 semitones
		_v.at(0x44) = 0x00800000;
		for(const uint32_t o : {0x5cu, 0xdcu, 0x15cu, 0x1dcu})
			_v.at(o) = INT32_MAX;
		_v.at(0x08) = 0x20000000;		// unit 0's level in the mix
		const int32_t b16 = machineByte(sram, _params, 16);
		_v.at(Voice::AmpDecay) = _rom.entry(AmpDecay, b16);

		if(_v.at(Voice::TrigNow))
		{
			const int32_t attack = b16 <= 0x27 ? _rom.entry(EnvAttack, b16) : static_cast<int32_t>(0xec11a411);
			_v.at(0x54) = attack;
			_v.at(0x18) = 0x20000000;
			_v.at(0xd4) = attack;
			_v.at(0x154) = attack;
			_v.at(0x1d4) = attack;
			_v.at(0x414) = 0x0096b8c4;
			_v.at(0x418) = 0x0096b8c4;
			_v.at(0x41c) = static_cast<int32_t>(0x812d7188);
			_v.at(0x420) = 0x000d205f;
			_v.at(0x424) = 0x000d205f;
			_v.at(0x428) = static_cast<int32_t>(0x801a40bf);
			_v.at(Voice::DriveX1) = INT32_MAX;
			_v.at(Voice::DriveY1) = INT32_MAX;
			_v.at(0x410) = _rom.entry(SweepDecay, b16);

			if(machineWord(sram, _params, 12))
			{
				_v.at(Voice::DriveFloor) = 0x20000000;
				_v.at(Voice::DrivePreGain) = 0x20000000;
				_v.at(Voice::DriveGain) = INT32_MAX;
				_v.at(Voice::DriveOn) = 1;
			}
			else
			{
				_v.at(Voice::DriveFloor) = INT32_MAX;
				_v.at(Voice::DrivePreGain) = 0x20000000;
				_v.at(Voice::DriveGain) = 0x40000000;
				_v.at(Voice::DriveOn) = 0;
			}
		}

		// The chord's shape from byte 6: shapes 0 to 2 take their ratios from tables at word 4, the others from a
		// row of Shapes, their gains from byte 4, an octave down or up at some of its values
		const int32_t shape = machineByte(sram, _params, 6);
		const int32_t w4 = machineWord(sram, _params, 4);
		if(shape <= 2)
		{
			if(shape == 0)
			{
				_v.at(0x0c) = 0x20000000;
				_v.at(0x10) = 0;
				_v.at(0x14) = 0;
				_v.at(0x50) = interpolate(_rom.ptr(RatioA), w4);
				_v.at(0xd0) = interpolate(_rom.ptr(RatioB), w4);
			}
			else if(shape == 1)
			{
				_v.at(0x0c) = 0x20000000;
				_v.at(0x10) = 0x20000000;
				_v.at(0x14) = 0;
				_v.at(0x50) = 0x04000000;
				_v.at(0xd0) = interpolate(_rom.ptr(RatioA), w4);
				_v.at(0x150) = interpolate(_rom.ptr(RatioB), w4);
			}
			else
			{
				_v.at(0x0c) = 0x20000000;
				_v.at(0x10) = 0x20000000;
				_v.at(0x14) = 0x20000000;
				_v.at(0x50) = interpolate(_rom.ptr(RatioC), w4);
				_v.at(0xd0) = interpolate(_rom.ptr(RatioA), w4);
				_v.at(0x150) = interpolate(_rom.ptr(RatioB), w4);
				_v.at(0x1d0) = interpolate(_rom.ptr(RatioD), w4);
			}
		}
		else
		{
			chordGains(_v, _params);
			_v.at(0x50) = 0x04000000;
			const int32_t row = shape > 37 ? 37 : shape;
			const uint32_t r = Shapes + 16 * static_cast<uint32_t>(row);
			int32_t r1 = _rom.at(r + 4), r2 = _rom.at(r + 8), r3 = _rom.at(r + 12);
			_v.at(0xd0) = r1;
			_v.at(0x150) = r2;
			_v.at(0x1d0) = r3;

			const int32_t b4 = machineByte(sram, _params, 4);
			if(static_cast<uint32_t>(b4 - 37) <= 31)
				_v.at(0xd0) = r1 >> 1;
			else if(b4 > 100)
				_v.at(0xd0) = addWrap(r1, r1);
			if(static_cast<uint32_t>(b4 - 47) <= 31)
				_v.at(0x150) = r2 >> 1;
			else if(b4 > 110)
				_v.at(0x150) = addWrap(r2, r2);
			if(static_cast<uint32_t>(b4 - 57) <= 31)
				_v.at(0x1d0) = r3 >> 1;
			else if(b4 > 120)
				_v.at(0x1d0) = addWrap(r3, r3);
		}

		wavePositions(_v, _params, _rom);

		// The note plus the tune word, in semitones Q16, clamped to notes 0-127, then the units' phase increments.
		// A unit above 0x454800 keeps its increment, and units 1 to 3 leave the mix
		const int32_t tune = asl(machineWord(sram, _params, 2), 16);
		int32_t p = addSat(_v.at(0x40), frac(_v.at(0x44), tune));
		p = addSat(p, pitch);
		if(p < 0)
			p = 0;
		else if(p > 0x7f0000)
			p = 0x7f0000;
		const int32_t q = frac(addWrap(p, static_cast<int32_t>(0xffbb0000)), 0x0aaaaaab);
		const int32_t octave = static_cast<int16_t>(static_cast<uint32_t>(q) >> 16);
		const int32_t fraction = static_cast<int32_t>(static_cast<uint32_t>(q) & 0xffff) >> 5;
		int32_t f = frac(0x6e000000, sram.at(Exp2 + 4 * static_cast<uint32_t>(fraction)));
		f = asrReg(f, 5 - octave);

		for(uint32_t k = 0; k < 4; ++k)
		{
			const int32_t t = frac(_v.unit(k, 0x50), f);
			if(t <= 0x454800)
				_v.unit(k, 0x78) = frac(t, 0x57619f10) >> 2;
			else if(k)
				_v.at(0x08 + 4 * k) = 0;
		}

		// The filters' cutoff, lower as the note rises
		const int32_t cutoff = interpolate(_rom.ptr(Cutoff), machineWord(sram, _params, 10));
		_v.at(0x3f4) = frac(subWrap(0x55f00000, f), cutoff);
	}

	void render(int32_t* _out, Voice& _v, const Rom& _rom)
	{
		Sram& sram = _v.sram;
		unitEnvelopes(_v, 4);
		for(uint32_t k = 0; k < 4; ++k)
			waveOscillator(k, _v, _rom);

		const int32_t* const sources[4] = {sram.ptr(bufferA(0)), sram.ptr(bufferA(1)), sram.ptr(bufferA(2)),
			sram.ptr(bufferA(3))};
		mix4(sources, _out, _v);
		driveClip(_out, _v);
		sweepEnvelope(_v);

		// The low-passes' pole: the cutoff under the sweep envelope, up to 0x55f00000, read from Pole
		int32_t c = addWrap(frac(_v.at(0x3f4), addWrap(_v.at(0x400), static_cast<int32_t>(0x80000001))), 0x55f00000);
		if(c > 0x55f00000)
			c = 0x55f00000;
		if(c < 0)
			c = 0;
		Emac acc;
		acc.mac(0x5f533928, c);
		acc.mac(0x5f533928, c);
		const int32_t w = static_cast<int16_t>(static_cast<uint32_t>(acc.storeClear()) >> 16);
		const int32_t c2 = interpolate(_rom.ptr(Pole), w);
		const int32_t c0 = addWrap(c2 >> 1, 0x40000000);
		_v.at(0x4a4) = c2;
		_v.at(0x484) = c2;
		_v.at(0x49c) = c0;
		_v.at(0x4a0) = c0;
		_v.at(0x47c) = c0;
		_v.at(0x480) = c0;
		onePole(_out, &_v.at(0x49c));
		onePole(_out, &_v.at(0x47c));

		ampEnvelope(_v);
		vca(_out, _v);
	}
}
