#include "stEngines.h"

#include "stEmac.h"
#include "stStages.h"

namespace syntakt::cyAlloy
{
	namespace
	{
		// Scratch buffers in SRAM, as the original lays them out for this engine
		constexpr uint32_t BufferA0 = 0x8000945c;	// unit 0 scaled: unit 1's modulator
		constexpr uint32_t BufferA1 = 0x80009580;	// unit 1 scaled
		constexpr uint32_t BufferA2 = 0x800096a4;	// unit 2 scaled: unit 3's modulator
		constexpr uint32_t BufferA3 = 0x800097c8;	// unit 3 scaled

		// Tables of section 7: 128 entries, interpolated at a word or read at a byte
		constexpr uint32_t DecayOdd = 0x40020200;	// units 1 and 3's envelope decays, by byte 16
		constexpr uint32_t Target = 0x40020400;		// units 0 and 2's envelope targets, by word 10
		constexpr uint32_t DecayEven = 0x40020600;	// units 0 and 2's envelope decays, by byte 16
		constexpr uint32_t Ratio = 0x40020800;		// units 0 and 1's frequency factors, by word 8
		constexpr uint32_t Spread = 0x40020a00;		// unit 2's frequency factor and unit 0's over unit 1's, by word 4
		constexpr uint32_t Depth = 0x40020c00;		// feedback and modulation depths, by word 6
		constexpr uint32_t AmpDecay = 0x40020e00;
		constexpr uint32_t Wave = 0x40021000;		// units 2 and 3's ROM wave

		// The machine block's byte k as the update reads it: its word (value << 8 | fine) or its high byte, signed
		int32_t word(const Sram& _sram, const uint32_t _params, const uint32_t _k)
		{
			return _sram.half(_params + 0x22 + _k);
		}

		int32_t byte(const Sram& _sram, const uint32_t _params, const uint32_t _k)
		{
			return static_cast<int8_t>(_sram.byte(_params + 0x22 + _k));
		}

		// 0x4000512c: a biquad in place, y = b0 x + b1 x1 + b2 x2 - 2 a1 y1 - a2 y2, every sample's terms summed before one
		// truncation. _b holds b0-b2 and at +0x18 x1, x2; _a holds a1, a2 and at +0x18 y1, y2
		void biquad(int32_t* _buf, int32_t* _b, int32_t* _a)
		{
			const int32_t b0 = _b[0], b1 = _b[1], b2 = _b[2];
			const int32_t a1 = _a[0], a2 = _a[1];
			int32_t x1 = _b[6], x2 = _b[7];
			int32_t y1 = _a[6], y2 = _a[7];

			Emac acc;
			for(int n = 0; n < BlockSize; ++n)
			{
				const int32_t x = _buf[n];
				acc.mac(b2, x2);
				acc.mac(b1, x1);
				acc.mac(b0, x);
				acc.msac(a1, y1);
				acc.msac(a1, y1);
				acc.msac(a2, y2);
				x2 = x1;
				x1 = x;
				y2 = y1;
				y1 = acc.storeClear();
				_buf[n] = y1;
			}
			_b[6] = x1;
			_b[7] = x2;
			_a[6] = y1;
			_a[7] = y2;
		}
	}

	void update(const int32_t _pitch, Voice& _v, const uint32_t _params, const Rom& _rom)
	{
		const Sram& sram = _v.sram;
		const auto interp = [&](const uint32_t _table, const uint32_t _k)
		{
			return interpolate(_rom.ptr(_table), word(sram, _params, _k));
		};

		_v.at(0x08) = 0x40000000;	// the mix's two levels, without a ramp
		_v.at(0x0c) = 0x40000000;
		_v.at(0x18) = 0x40000000;
		_v.at(0x1c) = 0x40000000;

		_v.at(0x2c) = interp(Depth, 6) >> 3;					// unit 2's self-feedback
		_v.at(0x40) = static_cast<int32_t>(0xffd80000);			// -40 semitones
		_v.at(0x44) = 0x00800000;
		const int32_t ratio = interp(Ratio, 8);
		_v.at(0x50) = asl(frac(ratio, interp(Spread, 4)), 5);	// unit 0's frequency factor
		_v.at(0x5c) = interp(Target, 10);
		_v.at(0x10c) = frac(0x02000000, interp(Depth, 6));		// unit 1's depth from unit 0
		_v.at(0x130) = static_cast<int32_t>(Wave);
		_v.at(0x11c) = static_cast<int32_t>(BufferA0);
		_v.at(0xdc) = 0x60000000;
		_v.at(0xd0) = interp(Ratio, 8);							// unit 1's frequency factor
		_v.at(0x150) = interp(Spread, 4);						// unit 2's
		_v.at(0x15c) = interp(Target, 10);
		_v.at(0x20c) = frac(0x02000000, interp(Depth, 6));		// unit 3's depth from unit 2
		_v.at(0x21c) = static_cast<int32_t>(BufferA2);
		_v.at(0x1dc) = 0x60000000;
		_v.at(0x230) = static_cast<int32_t>(Wave);
		_v.at(0x1d0) = 0x04000000;								// unit 3 at the note
		_v.at(Voice::AmpDecay) = _rom.entry(AmpDecay, byte(sram, _params, 16));

		if(_v.at(Voice::TrigNow))
		{
			_v.at(0x28) = 0;
			_v.at(0x58) = _rom.entry(DecayEven, byte(sram, _params, 16));
			_v.at(0xd8) = _rom.entry(DecayOdd, byte(sram, _params, 16));
			_v.at(0x158) = _rom.entry(DecayEven, byte(sram, _params, 16));
			_v.at(0x41c) = static_cast<int32_t>(0x81415554);
			_v.at(Voice::DriveX1) = INT32_MAX;
			_v.at(Voice::DriveY1) = INT32_MAX;
			_v.at(0x420) = 0x0013b13d;
			_v.at(0x424) = 0x0013b13d;
			_v.at(0x1d8) = _rom.entry(DecayOdd, byte(sram, _params, 16));
			_v.at(0x414) = 0x00a0aaaa;
			_v.at(0x418) = 0x00a0aaaa;
			_v.at(0x428) = static_cast<int32_t>(0x8027627b);

			if(word(sram, _params, 12))
			{
				_v.at(Voice::DriveGain) = INT32_MAX;
				_v.at(Voice::DriveFloor) = 0x2a3d70a4;
				_v.at(Voice::DrivePreGain) = 0x2a3d70a4;
				_v.at(Voice::DriveOn) = 1;
			}
			else
			{
				_v.at(Voice::DriveFloor) = INT32_MAX;
				_v.at(Voice::DrivePreGain) = 0x20000000;
				_v.at(Voice::DriveGain) = 0x60000000;
				_v.at(Voice::DriveOn) = 0;
			}
		}

		// The output's biquad
		_v.at(0x47c) = 0x3a2102de;
		_v.at(0x484) = 0x3a2102de;
		_v.at(0x480) = static_cast<int32_t>(0x8bbdfa44);
		_v.at(0x49c) = static_cast<int32_t>(0x92100c93);
		_v.at(0x4a0) = 0x75283018;

		phaseIncrements48k(_pitch, _v, _params, 4);
	}

	void render(int32_t* _out, Voice& _v, const Rom& _rom)
	{
		Sram& sram = _v.sram;
		unitEnvelopes(_v, 4);
		pmOscillator(0, _v, _rom);
		pmOscillator(1, _v, _rom);
		feedbackOscillator(2, _v, _rom);
		pmOscillator(3, _v, _rom);
		mix(sram.ptr(BufferA3), sram.ptr(BufferA1), _out, 0, 1, _v);
		biquad(_out, &_v.at(0x47c), &_v.at(0x49c));
		driveClip(_out, _v);
		ampEnvelope(_v);
		vca(_out, _v);
	}
}
