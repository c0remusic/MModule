#include "stBdModern.h"

#include "stEmac.h"
#include "stStages.h"

namespace syntakt::bdModern
{
	namespace
	{
		// Scratch buffers in SRAM, as the original lays them out for this engine
		constexpr uint32_t Click = 0x80008be0;
		constexpr uint32_t BufferA2 = 0x800096a4;	// unit 2 scaled
		constexpr uint32_t BufferB0 = 0x80008c60;	// unit 0 raw
		constexpr uint32_t BufferB1 = 0x80008d84;	// unit 1 raw: unit 2's modulator
		constexpr uint32_t BufferB3 = 0x80008fcc;	// unit 3 raw: the body
		constexpr uint32_t Modulator = 0x80009c58;	// the enveloped mix: unit 3's modulator
		constexpr uint32_t Sine = 0x80004b70;
		constexpr uint32_t Wave = 0x4001e7fc;		// unit 2's ROM wave, 257 entries

		// Tables of section 7 (128 entries)
		constexpr uint32_t SweepDepth = 0x4001ec00;
		constexpr uint32_t SweepDecay = 0x4001ee00;
		constexpr uint32_t DriveSetB = 0x4001f000;	// the clean setting's drive coefficients, which the gain never
		constexpr uint32_t DriveSetA = 0x4001f200;	// uses there: DriveOn is 0
		constexpr uint32_t ClickLevel = 0x4001f400;
		constexpr uint32_t ModDecay = 0x4001f600;
		constexpr uint32_t AmpDecay = 0x4001f800;
		constexpr uint32_t ModTarget = 0x4001fa00;
		constexpr uint32_t Feedback = 0x4001fc00;
		constexpr uint32_t LevelU0 = 0x4001fe00;
		constexpr uint32_t LevelU2 = 0x40020000;

		// The machine block's byte k as the update reads it: its word (value << 8 | fine) or its high byte, signed
		int32_t word(const Sram& _sram, const uint32_t _params, const uint32_t _k)
		{
			return _sram.half(_params + 0x22 + _k);
		}

		int32_t byte(const Sram& _sram, const uint32_t _params, const uint32_t _k)
		{
			return static_cast<int8_t>(_sram.byte(_params + 0x22 + _k));
		}

	}

	void click(int32_t* _buf, Voice& _v)
	{
		if(_v.at(Voice::TrigNow))
			_v.at(0x470) = INT32_MAX;

		// A ramp from full to 0 in 479 samples, at a level ramped over the block
		const int32_t step = subWrap(_v.at(0x474), _v.at(0x478)) >> 5;
		for(int n = 0; n < BlockSize; ++n)
		{
			_buf[n] = frac(_v.at(0x478), _v.at(0x470));
			const int32_t r = addSat(_v.at(0x470), static_cast<int32_t>(0xffbb9740));
			_v.at(0x470) = r < 0 ? 0 : r;
			_v.at(0x478) = addWrap(_v.at(0x478), step);
		}
		_v.at(0x478) = _v.at(0x474);

		// A high-pass then a low-pass, first order
		onePole(_buf, _v.sram.ptr(static_cast<uint32_t>(_v.at(0x468))));
		onePole(_buf, _v.sram.ptr(static_cast<uint32_t>(_v.at(0x46c))));
	}

	void sum(const int32_t* _a, const int32_t* _b, int32_t* _out)
	{
		for(int n = 0; n < BlockSize; ++n)
			_out[n] = addSat(_b[n], _a[n]);
	}

	void update(const int32_t _pitch, Voice& _v, const uint32_t _params, const Rom& _rom)
	{
		const Sram& sram = _v.sram;
		_v.at(0x40) = static_cast<int32_t>(0xffa80000);	// -88 semitones
		_v.at(0x44) = 0x00800000;
		_v.at(0x1d0) = 0x04000000;						// unit 3 at the note
		_v.at(0x50) = 0x08000000;						// units 0-2 an octave up
		_v.at(0xd0) = 0x08000000;
		_v.at(0x150) = 0x08000000;
		_v.at(0x3f8) = _rom.entry(SweepDepth, byte(sram, _params, 8));
		_v.at(0x474) = interpolate(_rom.ptr(ClickLevel), word(sram, _params, 6));
		_v.at(0x08) = interpolate(_rom.ptr(LevelU2), word(sram, _params, 4));
		const int32_t levelU0 = interpolate(_rom.ptr(LevelU0), word(sram, _params, 6));
		_v.at(0x0c) = levelU0;
		_v.at(Voice::AmpDecay) = _rom.entry(AmpDecay, byte(sram, _params, 16));

		if(_v.at(Voice::TrigNow))
		{
			for(const uint32_t u : {0x54u, 0xd4u, 0x154u, 0x1d4u})
				_v.at(u) = INT32_MIN;
			_v.at(0x158) = static_cast<int32_t>(0x8aa5ca3b);
			for(const uint32_t o : {0x60u, 0xe0u, 0x160u, 0x1e0u, 0x28u, 0x58u, 0xd8u, 0x1d8u})
				_v.at(o) = 0;
			_v.at(0x458) = INT32_MIN;
			_v.at(0x460) = 0;
			_v.at(Voice::AmpAttack) = INT32_MIN;
			_v.at(Voice::DriveX1) = INT32_MAX;
			_v.at(Voice::DriveY1) = INT32_MAX;
			_v.at(0x40c) = INT32_MIN;
			_v.at(0x45c) = _rom.entry(ModDecay, byte(sram, _params, 10));
			_v.at(0x410) = _rom.entry(SweepDecay, byte(sram, _params, 8));

			if(word(sram, _params, 12))
			{
				// Drive x2 into the clip, and the gain ducking after the hit
				_v.at(0x414) = 0x02e3f426;
				_v.at(0x418) = 0x02e3f426;
				_v.at(0x41c) = static_cast<int32_t>(0x85c7e84d);
				_v.at(0x420) = 0x004e99d0;
				_v.at(0x424) = 0x004e99d0;
				_v.at(0x428) = static_cast<int32_t>(0x809d339f);
				_v.at(Voice::DriveFloor) = 0x40000000;
				_v.at(Voice::DrivePreGain) = 0x40000000;
				_v.at(Voice::DriveGain) = 0x3eb851ec;
				_v.at(Voice::DriveOn) = 1;
			}
			else
			{
				const int32_t a = _rom.entry(DriveSetA, byte(sram, _params, 16));
				_v.at(0x41c) = a;
				_v.at(0x414) = addWrap(a >> 1, 0x40000000);
				_v.at(0x418) = _v.at(0x414);
				_v.at(Voice::DriveOn) = 0;
				const int32_t b = _rom.entry(DriveSetB, byte(sram, _params, 16));
				_v.at(0x428) = b;
				_v.at(0x420) = addWrap(b >> 1, 0x40000000);
				_v.at(0x424) = _v.at(0x420);
				_v.at(Voice::DriveFloor) = 0x66666666;
				_v.at(Voice::DrivePreGain) = 0x20000000;
				_v.at(Voice::DriveGain) = 0x370a3d71;
			}
			_v.at(0x1c) = levelU0;			// no ramp of the mix's levels at the hit
			_v.at(0x4b4) = 0;
			_v.at(0x4b8) = 0;
			_v.at(0x18) = _v.at(0x08);
			_v.at(0x494) = 0;
			_v.at(0x498) = 0;
			_v.at(0x478) = _v.at(0x474);	// nor of the click's level
		}

		_v.at(0x15c) = 0x60000000;			// unit 2's envelope settles at 0.75
		_v.at(0x5c) = INT32_MAX;
		_v.at(0xdc) = INT32_MAX;
		_v.at(0x1dc) = INT32_MAX;
		_v.at(0x2c) = interpolate(_rom.ptr(Feedback), word(sram, _params, 6));
		_v.at(0x10c) = 0;
		_v.at(0x110) = 0;
		_v.at(0x190) = 0;
		_v.at(0x19c) = static_cast<int32_t>(BufferB1);
		const int32_t depthU2 = frac(0x01000000, static_cast<int32_t>(0xf1eb851f));
		_v.at(0x18c) = depthU2;
		_v.at(0x194) = depthU2;
		_v.at(0x464) = interpolate(_rom.ptr(ModTarget), word(sram, _params, 10));
		_v.at(0x210) = 0;
		const int32_t depthU3 = frac(0x01000000, INT32_MAX);
		_v.at(0x21c) = static_cast<int32_t>(Modulator);
		for(const uint32_t o : {0xb0u, 0xb4u, 0x130u, 0x134u, 0x230u, 0x234u})
			_v.at(o) = static_cast<int32_t>(Sine);
		_v.at(0x1b0) = static_cast<int32_t>(Wave);
		_v.at(0x1b4) = static_cast<int32_t>(Wave);
		_v.at(0x47c) = 0x7df07773;			// high-pass, about 250 Hz
		_v.at(0x480) = static_cast<int32_t>(0x820f888d);
		_v.at(0x484) = static_cast<int32_t>(0x841f111a);
		_v.at(0x49c) = 0x17db49cb;			// low-pass, about 3.4 kHz
		_v.at(0x4a0) = 0x17db49cb;
		_v.at(0x4a4) = static_cast<int32_t>(0xafb69397);
		_v.at(0x20c) = depthU3;
		_v.at(0x214) = depthU3;

		sweepEnvelope(_v);
		phaseIncrements48k(_pitch, _v, _params, 4);
	}

	void render(int32_t* _out, Voice& _v, const Rom& _rom)
	{
		Sram& sram = _v.sram;
		unitEnvelopes(_v, 4);
		feedbackOscillator(0, _v, _rom);
		pmOscillator(1, _v, _rom);
		pmOscillator(2, _v, _rom);
		modulatorEnvelope(_v);
		mix(sram.ptr(BufferA2), sram.ptr(BufferB0), sram.ptr(Modulator), 0, 1, _v);
		modulatorVca(sram.ptr(Modulator), _v);
		pmOscillator(3, _v, _rom);
		click(sram.ptr(Click), _v);
		driveClip(sram.ptr(BufferB3), _v);
		sum(sram.ptr(Click), sram.ptr(BufferB3), _out);
		ampEnvelope(_v);
		vca(_out, _v);
	}
}
