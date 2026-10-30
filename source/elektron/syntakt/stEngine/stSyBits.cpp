#include "stEngines.h"

#include "stEmac.h"
#include "stStages.h"

// SY BITS (engine 9): four oscillators at 24 kHz on band-limited waves. Units 0 and 2 each crossfade two waves, units
// 1 and 3 read one wave at a phase offset; unit 1 is added to unit 0 and unit 3 to unit 2, the two pairs mixed, gated
// noise mixed in, then a bit reducer, a sample-rate reducer and the amp envelope.

namespace syntakt::syBits
{
	namespace
	{
		// Scratch buffers in SRAM, as the original lays them out for this engine
		constexpr uint32_t Noise = bufferB(3);
		constexpr uint32_t Scratch = 0x8000a064;	// the crossfading oscillator's working words

		// The band-limited waves the update starts from
		constexpr uint32_t WaveFirst = 0x8000e094;
		constexpr uint32_t WaveSecond = 0x8000d890;

		// Tables of section 7 (128 entries)
		constexpr uint32_t AmpDecay = 0x4003ec38;
		constexpr uint32_t PhaseOffset = 0x4003de38;
		constexpr uint32_t Crossfade = 0x4003e438;
		constexpr uint32_t GainA = 0x4003ea38;
		constexpr uint32_t GainB = 0x4003e838;
		constexpr uint32_t GainNoise = 0x4003e638;
		constexpr uint32_t SampleRate = 0x4003e238;
		constexpr uint32_t Bits = 0x4003e038;

		// The note with a tune word, as 0x40002596 computes it: clamped to 0..127 semitones in Q16
		int32_t note(const int32_t _pitch, const int32_t _tune, const Voice& _v)
		{
			int32_t p = addSat(_v.at(0x40), frac(_v.at(0x44), _tune));
			p = addSat(p, _pitch);
			if(p < 0)
				return 0;
			return p > 0x7f0000 ? 0x7f0000 : p;
		}

		// Linear interpolation in a 513-entry wave at a 25-bit phase: index from bits 16-24, fraction from 0-15
		int32_t lookup(const int32_t* _wave, const int32_t _phase)
		{
			const int32_t i = _phase >> 16;
			const int32_t f = asl(_phase & 0xffff, 15);
			Emac acc;
			acc.load(_wave[i]);
			acc.mac(f, _wave[i + 1]);
			acc.msac(f, _wave[i]);
			return acc.storeClear();
		}
	}

	// 0x400061a2: unit _k's two waves (0xb0, 0xb4) crossfaded, 32 samples at 24 kHz, into its buffers B (raw) and
	// A (under the unit's level). The weights ramp toward 0x7f000000 - w and w (w: 0xa4) by a 64th of the way
	// per sample, so they cover only half of it in a block
	void crossfadeOscillator(const int32_t _k, Voice& _v)
	{
		Sram& sram = _v.sram;
		const uint32_t u = Voice::UnitStride * static_cast<uint32_t>(_k);
		int32_t* bufA = sram.ptr(static_cast<uint32_t>(_v.at(u + 0xc8)));
		int32_t* bufB = sram.ptr(static_cast<uint32_t>(_v.at(u + 0xcc)));
		const int32_t inc = _v.at(u + 0x78);
		const uint32_t waveFirst = static_cast<uint32_t>(_v.at(u + 0xb0));
		const uint32_t waveSecond = static_cast<uint32_t>(_v.at(u + 0xb4));

		int32_t amp = _v.at(u + 0x80);
		const int32_t ampStep = rampStepSat(_v.at(u + 0x7c), amp);

		const int32_t w = _v.at(u + 0xa4);
		const int32_t target1 = subWrap(0x7f000000, w);
		const int32_t weight1 = _v.at(u + 0xa8);
		const int32_t weight2 = _v.at(u + 0xac);
		const int32_t step1 = subWrap(target1, weight1) >> 6;
		const int32_t step2 = subWrap(w, weight2) >> 6;
		_v.at(u + 0xa8) = target1;
		_v.at(u + 0xac) = w;
		_v.at(u + 0xb8) = _v.at(u + 0xb0);
		_v.at(u + 0xbc) = _v.at(u + 0xb4);

		int32_t* s = sram.ptr(Scratch);
		s[0] = 15;
		s[1] = 16;
		s[2] = static_cast<int32_t>(waveFirst);
		s[3] = static_cast<int32_t>(waveSecond);
		s[4] = weight1;
		s[5] = weight2;

		const int32_t* wave1 = sram.ptr(waveFirst);
		const int32_t* wave2 = sram.ptr(waveSecond);
		int32_t phase = _v.at(u + Voice::Phase);
		for(int n = 0; n < BlockSize; ++n)
		{
			const int32_t s1 = lookup(wave1, phase);
			const int32_t s2 = lookup(wave2, phase);
			Emac acc;
			acc.mac(s[4], s1);
			acc.mac(s[5], s2);
			s[4] = addWrap(s[4], step1);
			s[5] = addWrap(s[5], step2);
			phase = addWrap(phase, inc) & 0x1ffffff;
			const int32_t x = acc.storeClear();
			acc.msac(amp, x);
			amp = addWrap(amp, ampStep);
			bufB[n] = x;
			bufA[n] = acc.storeClear();
		}
		_v.at(u + Voice::Phase) = phase;
		_v.at(u + Voice::EnvLevelPrev) = _v.at(u + Voice::EnvLevel);
	}

	// 0x40006346: unit _k's wave (0xb0) read at its phase plus an offset that ramps from 0x74 to 0x70 over the
	// block, 32 samples at 24 kHz, into its buffers B (raw) and A (under the unit's level)
	void offsetOscillator(const int32_t _k, Voice& _v)
	{
		Sram& sram = _v.sram;
		const uint32_t u = Voice::UnitStride * static_cast<uint32_t>(_k);
		int32_t* bufA = sram.ptr(static_cast<uint32_t>(_v.at(u + 0xc8)));
		int32_t* bufB = sram.ptr(static_cast<uint32_t>(_v.at(u + 0xcc)));
		const int32_t* wave = sram.ptr(static_cast<uint32_t>(_v.at(u + 0xb0)));
		const int32_t inc = _v.at(u + 0x78);

		int32_t offset = _v.at(u + 0x74);
		const int32_t offsetStep = rampStepSat(_v.at(u + 0x70), offset);
		int32_t amp = _v.at(u + 0x80);
		const int32_t ampStep = rampStepSat(_v.at(u + 0x7c), amp);

		int32_t phase = _v.at(u + Voice::Phase);
		for(int n = 0; n < BlockSize; ++n)
		{
			const int32_t x = lookup(wave, addWrap(phase, offset) & 0x1ffffff);
			offset = addWrap(offset, offsetStep);
			phase = addWrap(phase, inc) & 0x1ffffff;
			Emac acc;
			acc.msac(amp, x);
			amp = addWrap(amp, ampStep);
			bufB[n] = x;
			bufA[n] = acc.storeClear();
		}
		_v.at(u + Voice::Phase) = phase;
		_v.at(u + 0x74) = _v.at(u + 0x70);
		_v.at(u + Voice::EnvLevelPrev) = _v.at(u + Voice::EnvLevel);
	}

	// 0x40005f9a: a bit reducer. Each sample is truncated to a multiple of 2^b and of 2^(b + 1), and the two
	// crossfaded by the fraction; b and the fraction from _c[5], Q16, b clamped to 0..30
	void reduceBits(int32_t* _buf, const int32_t* _c)
	{
		const int32_t word = _c[5];
		int32_t b = word >> 16;
		if(b < 0)
			b = 0;
		else if(b > 30)
			b = 30;
		const int32_t w1 = asl(word & 0xffff, 15);
		const int32_t w0 = subSat(INT32_MAX, w1);
		for(int n = 0; n < BlockSize; ++n)
		{
			const int32_t x = _buf[n];
			Emac acc;
			acc.mac(w0, aslReg(asrReg(x, b), b));
			acc.mac(w1, aslReg(asrReg(x, b + 1), b + 1));
			_buf[n] = acc.storeClear();
		}
	}

	void update(const int32_t _pitch, Voice& _v, const uint32_t _params, const Rom& _rom)
	{
		Sram& sram = _v.sram;
		_v.at(0x40) = static_cast<int32_t>(0xffc00000);	// -64 semitones
		_v.at(0x44) = 0x00800000;
		const int32_t b16 = machineByte(sram, _params, 16);
		_v.at(Voice::AmpDecay) = b16 == 0x7f ? INT32_MIN : _rom.entry(AmpDecay, b16);
		_v.at(0x588) = 0x3851eb85;						// the noise's threshold
		_v.at(0xc0) = 1;
		_v.at(0x1c0) = 1;
		for(const uint32_t o : {0x50u, 0xd0u, 0x150u, 0x1d0u})
			_v.at(o) = 0x04000000;
		_v.at(0xb0) = static_cast<int32_t>(WaveFirst);
		_v.at(0x1b0) = static_cast<int32_t>(WaveFirst);
		_v.at(0xb4) = static_cast<int32_t>(WaveSecond);
		_v.at(0x1b4) = static_cast<int32_t>(WaveSecond);

		// Units 1 and 3's phase offset
		const int32_t offset = frac(0x02000000, interpolate(_rom.ptr(PhaseOffset), machineWord(sram, _params, 10)));
		_v.at(0xf0) = offset;
		_v.at(0x1f0) = offset;

		// Units 0 and 1 play the note, units 2 and 3 the note detuned by word 4 (0x4000 centred)
		const int32_t tune = asl(machineWord(sram, _params, 2), 16);
		const int32_t noteA = note(_pitch, tune, _v);
		const int32_t noteB = note(_pitch, addWrap(tune, asl(machineWord(sram, _params, 4) - 0x4000, 16)), _v);

		// Units 0 and 2's crossfade, and above 42 units 1 and 3's level
		const int32_t fade = interpolate(_rom.ptr(Crossfade), machineWord(sram, _params, 10));
		_v.at(0xa4) = fade;
		_v.at(0x1a4) = fade;
		const int32_t b10 = machineByte(sram, _params, 10);
		_v.at(0xfc) = b10 > 0x2a ? fade : 0;
		_v.at(0x1fc) = b10 > 0x2a ? fade : 0;

		const int32_t a = noteA >> 16, b = noteB >> 16;
		if(static_cast<uint32_t>(b10 - 22) <= 20)
		{
			_v.at(0xb4) = static_cast<int32_t>(WaveSecond);
			_v.at(0x1b4) = static_cast<int32_t>(WaveSecond);
			selectBandlimitedWave(0, a, _v);
			selectBandlimitedWave(1, a, _v);
			selectBandlimitedWave(2, b, _v);
			selectBandlimitedWave(3, b, _v);
		}
		else if(b10 > 0x2a)
		{
			selectBandlimitedWave(0, a, _v);
			selectSecondBandlimitedWave(0, a, _v);
			selectBandlimitedWave(1, a, _v);
			selectBandlimitedWave(2, b, _v);
			selectSecondBandlimitedWave(2, b, _v);
			selectBandlimitedWave(3, b, _v);
		}

		_v.at(0x08) = interpolate(_rom.ptr(GainA), machineWord(sram, _params, 6));
		_v.at(0x0c) = interpolate(_rom.ptr(GainB), machineWord(sram, _params, 6));
		_v.at(0x10) = 0x2026f310;
		_v.at(0x14) = interpolate(_rom.ptr(GainNoise), machineWord(sram, _params, 6));

		if(_v.at(Voice::TrigNow))
		{
			// No ramp of the mix's levels, units 1 and 3's levels or their offsets at the hit
			_v.at(0x24) = _v.at(0x14);
			_v.at(0x18) = _v.at(0x08);
			_v.at(0x1c) = _v.at(0x0c);
			_v.at(0x20) = _v.at(0x10);
			_v.at(0x100) = _v.at(0xfc);
			_v.at(0x200) = _v.at(0x1fc);
			_v.at(0xf4) = _v.at(0xf0);
			_v.at(0x1f4) = _v.at(0x1f0);
		}

		_v.at(0x59c) = interpolate(_rom.ptr(SampleRate), machineWord(sram, _params, 8));
		_v.at(0x5a0) = interpolate(_rom.ptr(Bits), machineWord(sram, _params, 12));

		phaseIncrements24k(_pitch, _v, _params, 2);

		// Units 2 and 3's increment: unit 0's at three ratios of word 4, else from the detuned note
		const int32_t w4 = machineWord(sram, _params, 4);
		int32_t inc;
		if(w4 == 0x3400)
			inc = frac(_v.at(0x78), 0x40000000);
		else if(w4 == 0x4000)
			inc = _v.at(0x78);
		else if(w4 == 0x4c00)
			inc = addSat(_v.at(0x78), _v.at(0x78));
		else
		{
			// The detuned note in octaves from 69 (A), its exp2 from the RAM table, under the sweep and the unit's
			// frequency factor
			const int32_t d = asl(addWrap(w4 - 0x4000, machineWord(sram, _params, 2)), 16);
			int32_t p = addSat(static_cast<int32_t>(0xffc00000), frac(0x00800000, d));
			p = addSat(p, _pitch);
			if(p < 0)
				p = 0;
			else if(p > 0x7f0000)
				p = 0x7f0000;
			const int32_t oct = frac(addWrap(p, static_cast<int32_t>(0xffbb0000)), 0x0aaaaaab);
			const int32_t i = (oct & 0xffff) >> 5;
			const int32_t shift = subWrap(5, oct >> 16);
			const int32_t e = asrReg(frac(0x6e000000, sram.at(0x8000a888 + 4 * static_cast<uint32_t>(i))), shift);
			const int32_t base = addSat(_v.at(0x168), e);
			int32_t sweep = frac(addWrap(_v.at(0x3f8), static_cast<int32_t>(0xfc000000)), _v.at(0x400));
			sweep = addWrap(sweep, 0x04000000);
			int32_t f = frac(_v.at(0x150) >> 6, sweep);
			f = frac(f, _v.at(0x164));
			f = asl(f, 11);
			f = addSat(f, f);
			inc = frac(frac(f, base), 0x57619f10) >> 1;
		}
		_v.at(0x178) = inc;
		_v.at(0x1f8) = inc;
	}

	void render(int32_t* _out, Voice& _v, const Rom&)
	{
		Sram& sram = _v.sram;
		crossfadeOscillator(0, _v);
		crossfadeOscillator(2, _v);
		offsetOscillator(1, _v);
		offsetOscillator(3, _v);

		// Unit 1's scaled output added to unit 0's raw one, unit 3's to unit 2's
		int32_t* b0 = sram.ptr(bufferB(0));
		int32_t* b2 = sram.ptr(bufferB(2));
		const int32_t* a1 = sram.ptr(bufferA(1));
		const int32_t* a3 = sram.ptr(bufferA(3));
		for(int n = 0; n < BlockSize; ++n)
		{
			b0[n] = addSat(a1[n], b0[n]);
			b2[n] = addSat(a3[n], b2[n]);
		}

		mix(b0, b2, _out, 0, 1, _v);
		gatedNoise(sram.ptr(Noise), sram.ptr(_v.address + 0x578));
		mix(_out, sram.ptr(Noise), _out, 2, 3, _v);
		reduceBits(_out, sram.ptr(_v.address + 0x58c));
		reduceRate(_out, sram.ptr(_v.address + 0x58c));
		ampEnvelope(_v);
		vca(_out, _v);
	}
}
