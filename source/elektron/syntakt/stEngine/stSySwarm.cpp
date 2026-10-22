#include "stEngines.h"

#include "stEmac.h"
#include "stStages.h"

// SY SWARM (engine 10): seven band-limited oscillators at 24 kHz, each phase-modulated by one of three slow sines
// (recursive oscillators) and by white noise, their phases randomised at the trig, summed and put under the amp
// envelope. Unit 0 may sound one or two octaves down.

namespace syntakt::sySwarm
{
	namespace
	{
		// Scratch buffers in SRAM, as the original lays them out for this engine
		constexpr uint32_t SineA = 0x80009c58;		// the three slow sines: units 1 and 4, 2 and 5, 3 and 6
		constexpr uint32_t SineB = 0x80009cec;
		constexpr uint32_t SineC = 0x80009d80;
		constexpr uint32_t Noise = 0x80009e14;		// white noise, every unit's second modulator

		// Tables of section 7 (128 entries)
		constexpr uint32_t AmpDecay = 0x4003ec38;
		constexpr uint32_t RatioU1 = 0x4003fc38;
		constexpr uint32_t RatioU2 = 0x4003fa38;
		constexpr uint32_t RatioU3 = 0x4003f838;
		constexpr uint32_t RatioU4 = 0x4003f638;
		constexpr uint32_t RatioU5 = 0x4003f438;
		constexpr uint32_t RatioU6 = 0x4003f238;
		constexpr uint32_t Depth = 0x4003fe38;
		constexpr uint32_t LevelU0 = 0x4003f038;
		constexpr uint32_t Level = 0x4003ee38;

		// 0x40006014, 0x40006034, 0x40006054: a recursive sine's state (_s[0] output, _s[1] y1, _s[2] y2, _s[3] the
		// coefficient cos w) started at zero phase with amplitude _y1
		void startSine(int32_t* _s, const int32_t _y1, const int32_t _cos)
		{
			_s[0] = 0;
			_s[1] = _y1;
			_s[2] = 0;
			_s[3] = _cos;
		}

		// 0x40006074: _count samples of the recursive sine y = 2 cos w y1 - y2, saturated
		void recursiveSine(int32_t* _dst, const int32_t _count, int32_t* _s)
		{
			for(int32_t n = 0; n < _count; ++n)
			{
				const int32_t y1 = _s[1];
				Emac acc;
				acc.mac(_s[3], y1);
				acc.mac(_s[3], y1);
				const int32_t y = subSat(acc.storeClear(), _s[2]);
				_s[2] = y1;
				_s[0] = y;
				_s[1] = y;
				_dst[n] = y;
			}
		}

		// 0x40006434: unit _k's band-limited wave (512 entries at 0xb0, 25-bit phase), phase-modulated by two buffers
		// (0x9c, 0xa0) at depths ramped from 0x94 to 0x8c and from 0x98 to 0x90, 32 samples into its buffers 0xcc
		// (raw) and 0xc8 (scaled by the level at 0x7c, ramped from 0x80). Each modulator is read one sample past the
		// block, as the original does
		void oscillator(const int32_t _k, Voice& _v, const Rom& _rom)
		{
			Sram& sram = _v.sram;
			const uint32_t k = static_cast<uint32_t>(_k);
			int32_t level = _v.unit(k, 0x80);
			const int32_t levelStep = subWrap(_v.unit(k, 0x7c), level) >> 5;
			int32_t depth1 = _v.unit(k, 0x94);
			const int32_t depth1Step = subWrap(_v.unit(k, 0x8c), depth1) >> 5;
			int32_t depth2 = _v.unit(k, 0x98);
			const int32_t depth2Step = subWrap(_v.unit(k, 0x90), depth2) >> 5;
			const int32_t* mod1 = sram.ptr(static_cast<uint32_t>(_v.unit(k, 0x9c)));
			const int32_t* mod2 = sram.ptr(static_cast<uint32_t>(_v.unit(k, 0xa0)));
			int32_t* scaled = sram.ptr(static_cast<uint32_t>(_v.unit(k, 0xc8)));
			int32_t* raw = sram.ptr(static_cast<uint32_t>(_v.unit(k, 0xcc)));
			const int32_t* wave = table(sram, _rom, static_cast<uint32_t>(_v.unit(k, 0xb0)));
			const int32_t increment = _v.unit(k, 0x78);
			int32_t phase = _v.unit(k, Voice::Phase);

			Emac acc1, acc0;
			acc1.load(phase);
			acc1.mac(depth1, mod1[0]);
			for(int n = 0; n < BlockSize; ++n)
			{
				acc1.mac(depth2, mod2[n]);
				depth1 = addWrap(depth1, depth1Step);
				depth2 = addWrap(depth2, depth2Step);
				const int32_t p = acc1.storeClear();
				const int32_t i = (p & 0x1ffffff) >> 16;
				const int32_t f = asl(p & 0xffff, 15);
				const int32_t w0 = wave[i];
				acc0.load(w0);
				acc0.mac(f, wave[i + 1]);
				acc0.msac(f, w0);
				phase = addWrap(phase, increment) & 0x1ffffff;
				acc1.load(phase);
				const int32_t x = acc0.storeClear();
				acc0.msac(level, x);
				level = addWrap(level, levelStep);
				raw[n] = x;
				acc1.mac(depth1, mod1[n + 1]);
				scaled[n] = acc0.storeClear();
			}
			_v.unit(k, Voice::Phase) = phase;
			_v.unit(k, 0x80) = _v.unit(k, 0x7c);
			_v.unit(k, 0x94) = _v.unit(k, 0x8c);
			_v.unit(k, 0x98) = _v.unit(k, 0x90);
		}
	}

	void update(const int32_t _pitch, Voice& _v, const uint32_t _params, const Rom& _rom)
	{
		Sram& sram = _v.sram;
		_v.at(0x44) = 0x00800000;
		_v.at(0x40) = static_cast<int32_t>(0xffc00000);	// -64 semitones
		const int32_t b16 = machineByte(sram, _params, 16);
		_v.at(Voice::AmpDecay) = b16 == 0x7f ? INT32_MIN : _rom.entry(AmpDecay, b16);

		// Each unit's frequency factor
		const int32_t b8 = machineByte(sram, _params, 8);
		_v.at(0x50) = 0x04000000;
		_v.at(0xd0) = _rom.entry(RatioU1, b8);
		_v.at(0x150) = _rom.entry(RatioU2, b8);
		_v.at(0x1d0) = _rom.entry(RatioU3, b8);
		_v.at(0x250) = _rom.entry(RatioU4, b8);
		_v.at(0x2d0) = _rom.entry(RatioU5, b8);
		_v.at(0x350) = _rom.entry(RatioU6, b8);

		// Units 1 to 6's modulators: a slow sine each, shared by two units, then the noise
		_v.at(0x11c) = static_cast<int32_t>(SineA);
		_v.at(0x19c) = static_cast<int32_t>(SineB);
		_v.at(0x21c) = static_cast<int32_t>(SineC);
		_v.at(0x29c) = static_cast<int32_t>(SineA);
		_v.at(0x31c) = static_cast<int32_t>(SineB);
		_v.at(0x39c) = static_cast<int32_t>(SineC);
		for(const uint32_t o : {0x120u, 0x1a0u, 0x220u, 0x2a0u, 0x320u, 0x3a0u})
			_v.at(o) = static_cast<int32_t>(Noise);

		// The sines' depths, negated for units 4 to 6, and the noise's
		const int32_t sineDepth = interpolate(_rom.ptr(Depth), machineWord(sram, _params, 6));
		for(const uint32_t o : {0x10cu, 0x18cu, 0x20cu})
			_v.at(o) = frac(0x02000000, sineDepth);
		for(const uint32_t o : {0x28cu, 0x30cu, 0x38cu})
			_v.at(o) = frac(0x02000000, neg(sineDepth));
		const int32_t noiseDepth = frac(0x00200000, interpolate(_rom.ptr(Depth), machineWord(sram, _params, 4)));
		for(const uint32_t o : {0x110u, 0x190u, 0x210u, 0x290u, 0x310u, 0x390u})
			_v.at(o) = noiseDepth;

		// The note with the tune word, as 0x40002596 computes it
		int32_t p = addSat(_v.at(0x40), frac(_v.at(0x44), asl(machineWord(sram, _params, 2), 16)));
		p = addSat(p, _pitch);
		if(p < 0)
			p = 0;
		else if(p > 0x7f0000)
			p = 0x7f0000;
		int32_t note = p >> 16;
		for(int32_t k = 0; k < 7; ++k)
			selectBandlimitedWave(k, note, _v);

		// The units' levels: unit 0 on its own table, the others on one
		const int32_t w10 = subWrap(0x7f00, machineWord(sram, _params, 10));
		_v.at(0x7c) = interpolate(_rom.ptr(LevelU0), w10);
		const int32_t level = interpolate(_rom.ptr(Level), w10);
		for(uint32_t k = 1; k < 7; ++k)
			_v.unit(k, Voice::EnvLevel) = level;

		if(_v.at(Voice::TrigNow))
		{
			// No ramp of the levels at the trig, and every phase random
			int32_t* rng = sram.ptr(_v.address + 0x570);
			for(uint32_t k = 0; k < 7; ++k)
			{
				_v.unit(k, Voice::EnvLevelPrev) = _v.unit(k, Voice::EnvLevel);
				drawRandom(&_v.unit(k, Voice::Phase), 1, rng);
				_v.unit(k, Voice::Phase) &= 0x1ffffff;
			}
			startSine(sram.ptr(_v.address + 0x640), 0x346dc, 0x7fffffee);
			startSine(sram.ptr(_v.address + 0x650), 0x68db9, 0x7fffffb6);
			startSine(sram.ptr(_v.address + 0x660), 0xaec34, 0x7ffffe34);
		}

		phaseIncrements24k(_pitch, _v, _params, 7);

		// Unit 0 two octaves down, one octave down, or not
		const int32_t sub = machineByte(sram, _params, 12);
		if(sub == 0)
		{
			_v.at(0x78) >>= 2;
			note -= 24;
		}
		else if(sub == 1)
		{
			_v.at(0x78) >>= 1;
			note -= 12;
		}
		else
		{
			return;
		}
		selectBandlimitedWave(0, note, _v);
	}

	void render(int32_t* _out, Voice& _v, const Rom& _rom)
	{
		Sram& sram = _v.sram;
		recursiveSine(sram.ptr(SineA), BlockSize, sram.ptr(_v.address + 0x640));
		recursiveSine(sram.ptr(SineB), BlockSize, sram.ptr(_v.address + 0x650));
		recursiveSine(sram.ptr(SineC), BlockSize, sram.ptr(_v.address + 0x660));
		drawRandom(sram.ptr(Noise), BlockSize, sram.ptr(_v.address + 0x570));
		for(int32_t k = 0; k < 7; ++k)
			oscillator(k, _v, _rom);

		// The seven units' scaled outputs, summed wrapping
		for(uint32_t n = 0; n < BlockSize; ++n)
		{
			int32_t x = 0;
			for(uint32_t k = 0; k < 7; ++k)
				x = addWrap(x, sram.at(bufferA(k) + 4 * n));
			_out[n] = x;
		}
		ampEnvelope(_v);
		vca(_out, _v);
	}
}
