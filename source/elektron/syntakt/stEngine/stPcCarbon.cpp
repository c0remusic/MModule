#include "stEngines.h"

#include "stEmac.h"
#include "stStages.h"

namespace syntakt::pcCarbon
{
	namespace
	{
		// Scratch buffers in SRAM, as the original lays them out for this engine
		constexpr uint32_t BufferA0 = 0x8000945c;	// unit 0 scaled: unit 1's modulator
		constexpr uint32_t BufferA1 = 0x80009580;	// unit 1 scaled
		constexpr uint32_t BufferA2 = 0x800096a4;	// unit 2 scaled: unit 3's modulator
		constexpr uint32_t BufferA3 = 0x800097c8;	// unit 3 scaled
		constexpr uint32_t Wave = 0x40024804;		// unit 0's ROM wave, 257 entries

		// Tables of section 7 (128 entries unless noted)
		constexpr uint32_t DecayCurve = 0x40022004;	// 1024 entries, read at the top 11 bits of a Q31 value
		constexpr uint32_t DecayCurveLast = 0x40023000;
		constexpr uint32_t DecayPosition = 0x40023004;
		constexpr uint32_t SweepDecay = 0x40023204;
		constexpr uint32_t DepthU1 = 0x40023404;
		constexpr uint32_t FactorU1 = 0x40023604;
		constexpr uint32_t TargetU2 = 0x40023804;
		constexpr uint32_t SweepDepth = 0x40023a04;
		constexpr uint32_t TargetU0 = 0x40023c04;
		constexpr uint32_t FactorU2 = 0x40023e04;
		constexpr uint32_t DecayU2 = 0x40024004;
		constexpr uint32_t FactorU0 = 0x40024204;
		constexpr uint32_t DecayU0 = 0x40024404;
		constexpr uint32_t DepthU3 = 0x40024604;

		// The machine block's byte k as the update reads it: its word (value << 8 | fine) or its high byte, signed
		int32_t word(const Sram& _sram, const uint32_t _params, const uint32_t _k)
		{
			return _sram.half(_params + 0x22 + _k);
		}

		int32_t byte(const Sram& _sram, const uint32_t _params, const uint32_t _k)
		{
			return static_cast<int8_t>(_sram.byte(_params + 0x22 + _k));
		}

		// The 1024-entry curve read at _x's top 11 bits, interpolated on its next 21. An index above 1022 reads the last
		// entry twice; a negative one reads before the table, as the original does
		int32_t curve(const Rom& _rom, const int32_t _x)
		{
			const int32_t i = _x >> 21;
			const int32_t f = asl(_x & 0x1fffff, 10);
			const int32_t g = subWrap(INT32_MAX, f);
			Emac e;
			if(i > 0x3fe)
			{
				const int32_t last = _rom.at(DecayCurveLast);
				e.mac(g, last);
				e.mac(f, last);
			}
			else
			{
				e.mac(g, _rom.entry(DecayCurve, i));
				e.mac(f, _rom.entry(DecayCurve, i + 1));
			}
			return e.storeClear();
		}
	}

	void update(const int32_t _pitch, Voice& _v, const uint32_t _params, const Rom& _rom)
	{
		const Sram& sram = _v.sram;
		_v.at(0x44) = 0x00800000;
		_v.at(0x40) = static_cast<int32_t>(0xffc00000);	// -64 semitones
		_v.at(0x50) = interpolate(_rom.ptr(FactorU0), word(sram, _params, 4));
		_v.at(0xb0) = static_cast<int32_t>(Wave);
		_v.at(0x5c) = interpolate(_rom.ptr(TargetU0), word(sram, _params, 10));
		_v.at(0xd0) = interpolate(_rom.ptr(FactorU1), word(sram, _params, 4));
		_v.at(0x11c) = static_cast<int32_t>(BufferA0);
		_v.at(0x10c) = frac(0x02000000, interpolate(_rom.ptr(DepthU1), word(sram, _params, 6)));
		_v.at(0x150) = interpolate(_rom.ptr(FactorU2), word(sram, _params, 4));
		_v.at(0x1d0) = 0x04000000;
		_v.at(0x15c) = interpolate(_rom.ptr(TargetU2), word(sram, _params, 10));
		_v.at(0x20c) = frac(0x02000000, interpolate(_rom.ptr(DepthU3), word(sram, _params, 6)));
		_v.at(0x21c) = static_cast<int32_t>(BufferA2);
		_v.at(0x414) = 0x012c0f26;
		_v.at(0x418) = 0x012c0f26;
		_v.at(0x41c) = static_cast<int32_t>(0x82581e4b);
		_v.at(0x420) = 0x0013af8c;
		_v.at(0x424) = 0x0013af8c;
		_v.at(0x428) = static_cast<int32_t>(0x80275f18);
		_v.at(0x3f8) = _rom.entry(SweepDepth, byte(sram, _params, 8));

		// With hold on, the amp decays as unit 3's envelope did last block
		const int32_t hold = _v.at(Voice::AmpHold);
		if(hold)
			_v.at(Voice::AmpDecay) = _v.at(0x1d8);

		// Unit 3's decay from the decay byte, unit 1's from it and byte 6
		const int32_t position = _rom.entry(DecayPosition, byte(sram, _params, 16));
		_v.at(0x1d8) = curve(_rom, position);
		const int32_t shift = frac(0x46666666, asl(word(sram, _params, 6), 16));
		const int32_t scale = subSat(0x60000000, shift);
		Emac e;
		e.mac(scale, position);
		e.mac(scale, position);
		_v.at(0xd8) = curve(_rom, e.storeClear());

		if(_v.at(Voice::TrigNow))
		{
			_v.at(Voice::AmpAttack) = static_cast<int32_t>(0xa2e8ba2f);
			if(hold)
			{
				_v.at(0xdc) = 0x5a7ef9db;
				_v.at(0x1dc) = 0x5a7ef9db;
			}
			else
			{
				_v.at(0xdc) = 0;
				_v.at(0x1dc) = 0;
				_v.at(Voice::AmpDecay) = INT32_MIN;
			}
			_v.at(0x28) = 0;
			_v.at(0x58) = _rom.entry(DecayU0, byte(sram, _params, 16));
			_v.at(0x158) = _rom.entry(DecayU2, byte(sram, _params, 16));
			_v.at(Voice::DriveX1) = INT32_MAX;
			_v.at(Voice::DriveY1) = INT32_MAX;
			_v.at(0x410) = _rom.entry(SweepDecay, byte(sram, _params, 8));

			if(word(sram, _params, 12))
			{
				_v.at(Voice::DriveGain) = INT32_MAX;
				_v.at(0x08) = 0x40000000;
				_v.at(0x0c) = 0x20000000;
				_v.at(Voice::DriveFloor) = 0x20000000;
				_v.at(Voice::DrivePreGain) = 0x2a3d70a4;
				_v.at(Voice::DriveOn) = 1;
			}
			else
			{
				_v.at(0x08) = 0x20000000;
				_v.at(0x0c) = 0x10000000;
				_v.at(Voice::DriveFloor) = INT32_MAX;
				_v.at(Voice::DrivePreGain) = 0x20000000;
				_v.at(Voice::DriveGain) = INT32_MAX;
				_v.at(Voice::DriveOn) = 0;
			}
		}

		// The mix's gains never ramp
		_v.at(0x18) = _v.at(0x08);
		_v.at(0x1c) = _v.at(0x0c);

		sweepEnvelope(_v);
		phaseIncrements48k(_pitch, _v, _params, 4);
	}

	void render(int32_t* _out, Voice& _v, const Rom& _rom)
	{
		Sram& sram = _v.sram;
		unitEnvelopes(_v, 4);
		for(int32_t k = 0; k < 4; ++k)
			pmOscillator(k, _v, _rom);
		mix(sram.ptr(BufferA3), sram.ptr(BufferA1), _out, 0, 1, _v);
		driveClip(_out, _v);
		ampEnvelope(_v);
		vca(_out, _v);
	}
}
