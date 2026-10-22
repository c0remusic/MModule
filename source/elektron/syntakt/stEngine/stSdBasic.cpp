#include "stEngines.h"

#include "stEmac.h"
#include "stStages.h"

namespace syntakt::sdBasic
{
	namespace
	{
		// Scratch buffers in SRAM, as the original lays them out for this engine
		constexpr uint32_t BufferA0 = 0x8000945c;	// unit 0 scaled
		constexpr uint32_t BufferA1 = 0x80009580;	// unit 1 scaled: units 2 and 3's modulator
		constexpr uint32_t BufferA2 = 0x800096a4;	// unit 2 scaled, high-passed, enveloped
		constexpr uint32_t BufferA3 = 0x800097c8;	// unit 3 scaled, filtered, under the amp envelope

		// Tables of section 7 (128 entries)
		constexpr uint32_t ModDecay = 0x40028238;
		constexpr uint32_t AmpDecay = 0x40026838;
		constexpr uint32_t DecayU0 = 0x40027a38;
		constexpr uint32_t DecayU1 = 0x40028038;
		constexpr uint32_t DecayU2 = 0x40026038;
		constexpr uint32_t DecayU3 = 0x40026a38;
		constexpr uint32_t SweepDepth = 0x40026638;
		constexpr uint32_t SweepDecay = 0x40026438;
		constexpr uint32_t AmpAttack = 0x40026c38;
		constexpr uint32_t Feedback = 0x40026e38;
		constexpr uint32_t TargetU0 = 0x40027838;
		constexpr uint32_t ScaleU0 = 0x40027238;
		constexpr uint32_t TargetU1 = 0x40027e38;
		constexpr uint32_t ScaleU1 = 0x40027c38;
		constexpr uint32_t ScaleU3 = 0x40027038;
		constexpr uint32_t DepthU3 = 0x40027438;
		constexpr uint32_t LowPass = 0x40026238;

		// 0x40004e14: a first-order IIR in place, y = c0 x + c1 x1 - c2 y1, its coefficients at _c[0..2] and its state
		// at _c[6] (x1) and _c[7] (y1), each product truncated alone and the terms added wrapping
		void firstOrder(int32_t* _buf, int32_t* _c)
		{
			for(int n = 0; n < BlockSize; ++n)
			{
				const int32_t x = _buf[n];
				const int32_t t1 = frac(_c[1], _c[6]);
				int32_t y = addWrap(frac(_c[0], x), t1);
				_c[6] = x;
				Emac e;
				e.msac(_c[2], _c[7]);
				y = addWrap(y, e.storeClear());
				_buf[n] = y;
				_c[7] = y;
			}
		}
	}

	void update(const int32_t _pitch, Voice& _v, const uint32_t _params, const Rom& _rom)
	{
		const Sram& sram = _v.sram;
		_v.at(0x45c) = _rom.entry(ModDecay, machineByte(sram, _params, 16));
		_v.at(Voice::AmpDecay) = _rom.entry(AmpDecay, machineByte(sram, _params, 16));

		if(_v.at(Voice::TrigNow))
		{
			_v.at(0x28) = 0;
			_v.at(0x58) = _rom.entry(DecayU0, machineByte(sram, _params, 16));
			_v.at(0xd8) = _rom.entry(DecayU1, machineByte(sram, _params, 16));
			_v.at(0x158) = _rom.entry(DecayU2, machineByte(sram, _params, 16));
			_v.at(0x1d8) = _rom.entry(DecayU3, machineByte(sram, _params, 10));
			_v.at(0x3f8) = _rom.entry(SweepDepth, machineByte(sram, _params, 8));
			_v.at(0x410) = _rom.entry(SweepDecay, machineByte(sram, _params, 8));
			_v.at(0x414) = 0x01f10e1d;
			_v.at(0x418) = 0x01f10e1d;
			_v.at(Voice::DriveX1) = INT32_MAX;
			_v.at(Voice::DriveY1) = INT32_MAX;
			_v.at(0x41c) = static_cast<int32_t>(0x83e21c3a);
			_v.at(0x420) = 0x001f7c5c;
			_v.at(0x424) = 0x001f7c5c;
			_v.at(0x428) = static_cast<int32_t>(0x803ef8b8);
			_v.at(Voice::AmpAttack) = _rom.entry(AmpAttack, machineByte(sram, _params, 16));

			if(machineWord(sram, _params, 12))
			{
				_v.at(Voice::DriveFloor) = 0x40000000;
				_v.at(Voice::DrivePreGain) = INT32_MAX;
				_v.at(Voice::DriveGain) = 0x40000000;
				_v.at(Voice::DriveOn) = 1;
				_v.at(0x458) = INT32_MIN;
			}
			else
			{
				_v.at(Voice::DriveFloor) = INT32_MAX;
				_v.at(Voice::DriveGain) = INT32_MAX;
				_v.at(Voice::DrivePreGain) = 0x20000000;
				_v.at(Voice::DriveOn) = 0;
				_v.at(0x458) = static_cast<int32_t>(0xc028db9c);
			}
		}

		// The mix's two levels, fixed and never ramped
		_v.at(0x08) = INT32_MAX;
		_v.at(0x18) = INT32_MAX;
		_v.at(0x0c) = 0x40000000;
		_v.at(0x1c) = 0x40000000;

		_v.at(0x2c) = interpolate(_rom.ptr(Feedback), machineWord(sram, _params, 6));
		_v.at(0x40) = static_cast<int32_t>(0xffc00000);	// -64 semitones
		_v.at(0x50) = 0x08000000;
		_v.at(0x44) = 0x00800000;
		_v.at(0x5c) = interpolate(_rom.ptr(TargetU0), machineWord(sram, _params, 10));
		_v.at(0x64) = interpolate(_rom.ptr(ScaleU0), machineWord(sram, _params, 4));
		_v.at(0xd0) = 0x08000000;
		_v.at(0xdc) = interpolate(_rom.ptr(TargetU1), machineWord(sram, _params, 10));
		const int32_t scaleU1 = interpolate(_rom.ptr(ScaleU1), machineWord(sram, _params, 4));
		_v.at(0x150) = 0x04000000;
		_v.at(0x10c) = 0;
		_v.at(0x110) = 0;
		_v.at(0x190) = 0;
		_v.at(0xe4) = scaleU1;
		_v.at(0x15c) = 0x40000000;
		const int32_t depthU2 = frac(0x01000000, 0x1d70a3d7);
		_v.at(0x18c) = depthU2;
		_v.at(0x194) = depthU2;
		_v.at(0x19c) = static_cast<int32_t>(BufferA1);
		_v.at(0x1d0) = 0x0c000000;
		_v.at(0x1dc) = 0x20000000;
		_v.at(0x1e4) = interpolate(_rom.ptr(ScaleU3), machineWord(sram, _params, 4));
		const int32_t depthU3 = frac(0x01000000, 0x20000000);
		_v.at(0x20c) = depthU3;
		_v.at(0x214) = depthU3;
		const int32_t depth2U3 = interpolate(_rom.ptr(DepthU3), machineWord(sram, _params, 6));
		_v.at(0x220) = static_cast<int32_t>(BufferA0);
		_v.at(0x480) = static_cast<int32_t>(0xb46fb855);
		_v.at(0x210) = frac(0x10000000, depth2U3);
		_v.at(0x21c) = static_cast<int32_t>(BufferA1);
		_v.at(0x47c) = 0x4b9047ab;						// unit 2's high-pass
		_v.at(0x484) = static_cast<int32_t>(0x841f111a);

		// Unit 3's low-pass, its pole from byte 6
		const int32_t w6 = machineWord(sram, _params, 6);
		const int32_t i = addWrap(frac(0x7f000000, w6), subWrap(0x7f00, w6)) >> 8;
		const int32_t c2 = _rom.entry(LowPass, i);
		_v.at(0x4a4) = c2;
		const int32_t c0 = frac(0x33333333, addWrap(c2 >> 1, 0x40000000));
		_v.at(0x49c) = c0;
		_v.at(0x4a0) = c0;

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
		pmOscillator(3, _v, _rom);
		onePole(sram.ptr(BufferA2), &_v.at(0x47c));
		modulatorEnvelope(_v);
		modulatorVca(sram.ptr(BufferA2), _v);
		firstOrder(sram.ptr(BufferA3), &_v.at(0x49c));
		ampEnvelope(_v);
		vca(sram.ptr(BufferA3), _v);
		mix(sram.ptr(BufferA2), sram.ptr(BufferA3), _out, 0, 1, _v);
		driveClip(_out, _v);
	}
}
