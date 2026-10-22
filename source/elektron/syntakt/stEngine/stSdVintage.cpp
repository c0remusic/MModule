#include "stEngines.h"

#include "stEmac.h"
#include "stStages.h"

namespace syntakt::sdVintage
{
	namespace
	{
		// Scratch buffers in SRAM, as the original lays them out for this engine
		constexpr uint32_t BufferA0 = 0x8000945c;	// units 0-2 scaled: the three body sines
		constexpr uint32_t BufferA1 = 0x80009580;
		constexpr uint32_t BufferA2 = 0x800096a4;
		constexpr uint32_t BufferA3 = 0x800097c8;	// unit 3 scaled: the modulator of units 0-2
		constexpr uint32_t Sine = 0x80004b70;
		constexpr uint32_t WaveU2 = 0x8000a484;		// RAM waves of units 2 and 3
		constexpr uint32_t WaveU3 = 0x8000a080;

		// Tables of section 7 (128 entries, Body a 128 x 128 grid)
		constexpr uint32_t Ratio0 = 0x40038438;		// unit 3's frequency factor
		constexpr uint32_t Ratio1 = 0x40038638;		// unit 1's
		constexpr uint32_t SweepDecay = 0x40038838;
		constexpr uint32_t SweepDepth = 0x40038a38;
		constexpr uint32_t Depth2 = 0x40038c38;
		constexpr uint32_t Depth1 = 0x40038e38;
		constexpr uint32_t Depth0 = 0x40039038;
		constexpr uint32_t NoiseLp = 0x40039238;
		constexpr uint32_t Level3 = 0x40039438;
		constexpr uint32_t Level2 = 0x40039638;
		constexpr uint32_t Level1 = 0x40039838;
		constexpr uint32_t Feedback = 0x40039a38;
		constexpr uint32_t ModDecay = 0x40039c38;
		constexpr uint32_t Decay2 = 0x40039e38;
		constexpr uint32_t Decay01 = 0x4003a038;
		constexpr uint32_t Decay3 = 0x40028438;	// indexed by byte 16 << 7 | byte 10

		int32_t word(const Sram& _sram, const uint32_t _params, const uint32_t _k)
		{
			return _sram.half(_params + 0x22 + _k);
		}

		int32_t byte(const Sram& _sram, const uint32_t _params, const uint32_t _k)
		{
			return static_cast<int8_t>(_sram.byte(_params + 0x22 + _k));
		}

		// 0x40005016: a biquad in place, y = 2 (c0 x + c1 x1 + c2 x2 - c3 y1 - c4 y2) saturated, kept as the state, and
		// written doubled again. Coefficients at _c[0..4], state x1, x2, y1, y2 at _c[5..8]
		void biquad(int32_t* _buf, int32_t* _c)
		{
			const int32_t c0 = _c[0], c1 = _c[1], c2 = _c[2], c3 = _c[3], c4 = _c[4];
			int32_t x1 = _c[5], x2 = _c[6], y1 = _c[7], y2 = _c[8];
			for(int n = 0; n < BlockSize; ++n)
			{
				Emac acc;
				acc.mac(c2, x2);
				acc.mac(c1, x1);
				const int32_t x0 = _buf[n];
				acc.mac(c0, x0);
				acc.msac(c4, y2);
				acc.msac(c3, y1);
				x2 = x1;
				x1 = x0;
				y2 = y1;
				const int32_t y = acc.storeClear();
				y1 = addSat(y, y);
				_buf[n] = addSat(y1, y1);
			}
			_c[5] = x1;
			_c[6] = x2;
			_c[7] = y1;
			_c[8] = y2;
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
	}

	void update(const int32_t _pitch, Voice& _v, const uint32_t _params, const Rom& _rom)
	{
		const Sram& sram = _v.sram;
		const int32_t w6 = word(sram, _params, 6);

		// The noise's low-pass, its two levels in the mix, the three depths of unit 3's modulation
		const int32_t lp = interpolate(_rom.ptr(NoiseLp), w6);
		_v.at(0x4c4) = lp;
		const int32_t lp0 = addSat(0x40000000, lp >> 1);
		_v.at(0x4bc) = lp0;
		_v.at(0x4c0) = lp0;
		_v.at(0x0c) = interpolate(_rom.ptr(Level1), w6);
		_v.at(0x10) = interpolate(_rom.ptr(Level2), w6);
		_v.at(0x14) = interpolate(_rom.ptr(Level3), w6);
		_v.at(0x8c) = frac(0x01000000, interpolate(_rom.ptr(Depth0), w6));
		_v.at(0x10c) = frac(0x01000000, interpolate(_rom.ptr(Depth1), w6));
		_v.at(0x18c) = frac(0x01000000, interpolate(_rom.ptr(Depth2), w6));

		if(_v.at(Voice::TrigNow))
		{
			const int32_t b16 = byte(sram, _params, 16);
			const int32_t b10 = byte(sram, _params, 10);
			const int32_t b8 = byte(sram, _params, 8);
			constexpr auto attack = static_cast<int32_t>(0xcccccccd);

			_v.at(0x28) = 0;
			const int32_t d01 = _rom.entry(Decay01, b16);
			_v.at(0x58) = d01;
			_v.at(0xd8) = d01;
			_v.at(0x158) = _rom.entry(Decay2, b16);
			_v.at(0x54) = attack;
			_v.at(0xd4) = attack;
			_v.at(0x154) = attack;
			_v.at(0x45c) = _rom.entry(ModDecay, b16);
			_v.at(0x1d4) = b10 <= 0x7e ? attack : 0;
			_v.at(0x1d8) = _rom.entry(Decay3, (b16 << 7) + b10);
			_v.at(0x40c) = attack;
			_v.at(0x3f8) = _rom.entry(SweepDepth, b8);
			_v.at(Voice::DriveX1) = INT32_MAX;
			_v.at(Voice::DriveY1) = INT32_MAX;
			_v.at(0x458) = static_cast<int32_t>(0xe12818ad);
			_v.at(0x414) = 0x01f10e1d;
			_v.at(0x418) = 0x01f10e1d;
			_v.at(0x41c) = static_cast<int32_t>(0x83e21c3a);
			_v.at(0x420) = 0x00117fe6;
			_v.at(0x424) = 0x00117fe6;
			_v.at(0x428) = static_cast<int32_t>(0x8022ffcb);
			_v.at(0x410) = _rom.entry(SweepDecay, b8);

			if(word(sram, _params, 12))
			{
				_v.at(Voice::DriveFloor) = 0x20000000;
				_v.at(Voice::DrivePreGain) = INT32_MAX;
				_v.at(Voice::DriveGain) = 0x60000000;
				_v.at(Voice::DriveOn) = 1;
			}
			else
			{
				_v.at(Voice::DriveFloor) = INT32_MAX;
				_v.at(Voice::DrivePreGain) = 0x20000000;
				_v.at(Voice::DriveGain) = INT32_MAX;
				_v.at(Voice::DriveOn) = 0;
			}

			// Held until gate-off: the units settle at 0.3 and the amp decays as units 0 and 1 do
			if(_v.at(Voice::AmpHold))
			{
				for(const uint32_t o : {0x5cu, 0xdcu, 0x1dcu})
					_v.at(o) = 0x26666666;
				_v.at(Voice::AmpDecay) = _v.at(0x58);
			}
			else
			{
				for(const uint32_t o : {0x5cu, 0xdcu, 0x1dcu})
					_v.at(o) = 0;
				_v.at(Voice::AmpDecay) = INT32_MIN;
			}

			for(const uint32_t o : {0x4b4u, 0x4b8u, 0x494u, 0x498u, 0x4d4u, 0x4d8u, 0x510u, 0x514u, 0x518u, 0x51cu,
				0x4f4u, 0x4f8u})
				_v.at(o) = 0;
			_v.at(0x4c8) = _v.at(0x4bc);
			_v.at(0x4cc) = _v.at(0x4c0);
			_v.at(0x4d0) = _v.at(0x4c4);
			_v.at(0x1c) = _v.at(0x0c);		// no ramp of the mix's levels at the hit
			_v.at(0x20) = _v.at(0x10);
			_v.at(0x24) = _v.at(0x14);
		}

		const int32_t w4 = word(sram, _params, 4);
		_v.at(0x40) = static_cast<int32_t>(0xffc00000);	// -64 semitones
		_v.at(0x44) = 0x00800000;
		_v.at(0x9c) = static_cast<int32_t>(BufferA3);
		_v.at(0x50) = 0x04000000;
		_v.at(0xd0) = interpolate(_rom.ptr(Ratio1), w4);
		_v.at(0x11c) = static_cast<int32_t>(BufferA3);
		_v.at(0x150) = 0x10000000;
		_v.at(0x19c) = static_cast<int32_t>(BufferA3);
		_v.at(0x1d0) = interpolate(_rom.ptr(Ratio0), w4);
		for(const uint32_t o : {0xb0u, 0xb4u, 0x130u, 0x134u})
			_v.at(o) = static_cast<int32_t>(Sine);
		_v.at(0x1b0) = static_cast<int32_t>(WaveU2);
		_v.at(0x1b4) = static_cast<int32_t>(WaveU2);
		_v.at(0x230) = static_cast<int32_t>(WaveU3);
		_v.at(0x234) = static_cast<int32_t>(WaveU3);
		_v.at(0x2c) = interpolate(_rom.ptr(Feedback), w6) >> 2;

		// The noise's band-pass (biquad), its fixed low-pass and the output's high-pass
		_v.at(0x500) = static_cast<int32_t>(0xcad2e288);
		_v.at(0x50c) = 0x2c322027;
		_v.at(0x4a4) = static_cast<int32_t>(0xb51d7968);
		_v.at(0x4fc) = 0x1a968ebc;
		_v.at(0x504) = 0x1a968ebc;
		_v.at(0x508) = static_cast<int32_t>(0x977daa47);
		_v.at(0x49c) = 0x1a8ebcb4;
		_v.at(0x4a0) = 0x1a8ebcb4;
		_v.at(0x47c) = 0x7ec16db1;
		_v.at(0x480) = static_cast<int32_t>(0x813e924f);
		_v.at(0x484) = static_cast<int32_t>(0x827d249e);
		_v.at(0x4dc) = 0x7f2aedb2;
		_v.at(0x4e0) = static_cast<int32_t>(0x80d5124e);
		_v.at(0x4e4) = static_cast<int32_t>(0x81aa249d);
		_v.at(0x588) = 0x2a3d70a4;	// the noise gate's threshold
		_v.at(0x08) = 0x11444815;	// unit 0's level, never ramped
		_v.at(0x18) = 0x11444815;

		sweepEnvelope(_v);
		phaseIncrements48k(_pitch, _v, _params, 4);
	}

	void render(int32_t* _out, Voice& _v, const Rom& _rom)
	{
		unitEnvelopes(_v, 4);
		scaledFeedbackOscillator(3, _v, _rom);
		pmOscillator(0, _v, _rom);
		pmOscillator(1, _v, _rom);
		pmOscillator(2, _v, _rom);
		modulatorEnvelope(_v);

		// The noise, band-passed, low-passed twice, under the modulator envelope
		gatedNoise(_out, &_v.at(0x578));
		biquad(_out, &_v.at(0x4fc));
		onePole(_out, &_v.at(0x4bc));
		onePole(_out, &_v.at(0x49c));
		modulatorVca(_out, _v);

		// Mixed with the three sines, high-passed, doubled
		Sram& sram = _v.sram;
		const int32_t* const sources[4] = {sram.ptr(BufferA0), sram.ptr(BufferA1), sram.ptr(BufferA2), _out};
		mix4(sources, _out, _v);
		onePole(_out, &_v.at(0x47c));
		for(int n = 0; n < BlockSize; ++n)
			_out[n] = addSat(_out[n], _out[n]);

		driveClip(_out, _v);
		ampEnvelope(_v);
		vca(_out, _v);
	}
}
