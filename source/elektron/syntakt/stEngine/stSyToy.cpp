#include "stEngines.h"

#include "stEmac.h"
#include "stStages.h"

// SY TOY (engine 8): update 0x40008ae8, render 0x40008d58. Five units on the RAM sine: unit 4 a sine with
// self-feedback, units 0 to 3 phase-modulated, their four outputs mixed with ramped gains, under the amp envelope
// and into the drive's clip.

namespace syntakt::syToy
{
	namespace
	{
		// Tables of section 7 (128 entries)
		constexpr uint32_t AmpDecay = 0x4003dc38;
		constexpr uint32_t DecayU1 = 0x4003c838;
		constexpr uint32_t DecayU2 = 0x4003c638;
		constexpr uint32_t DecayU3 = 0x4003c438;
		constexpr uint32_t DecayU4 = 0x4003c238;
		constexpr uint32_t DepthU3 = 0x4003d038;
		constexpr uint32_t Feedback = 0x4003d238;
		constexpr uint32_t Target = 0x4003c038;
		constexpr uint32_t Gain1 = 0x4003ce38;
		constexpr uint32_t Gain2 = 0x4003cc38;
		constexpr uint32_t Gain3 = 0x4003ca38;
		constexpr uint32_t RatioU1 = 0x4003da38;
		constexpr uint32_t RatioU2 = 0x4003d838;
		constexpr uint32_t RatioU3 = 0x4003d638;
		constexpr uint32_t RatioU4 = 0x4003d438;

	}

	void update(const int32_t _pitch, Voice& _v, const uint32_t _params, const Rom& _rom)
	{
		const Sram& sram = _v.sram;
		_v.at(0x40) = static_cast<int32_t>(0xffc00000);	// -64 semitones
		_v.at(0x44) = 0x00800000;
		_v.at(Voice::AmpDecay) = _rom.entry(AmpDecay, machineByte(sram, _params, 16));
		_v.at(0xd8) = _rom.entry(DecayU1, machineByte(sram, _params, 10));
		_v.at(0x158) = _rom.entry(DecayU2, machineByte(sram, _params, 10));
		_v.at(0x1d8) = _rom.entry(DecayU3, machineByte(sram, _params, 10));
		_v.at(0x19c) = static_cast<int32_t>(bufferA(4));	// unit 3 modulated by unit 4
		_v.at(0x258) = _rom.entry(DecayU4, machineByte(sram, _params, 10));

		_v.at(0x18c) = frac(0x01000000, interpolate(_rom.ptr(DepthU3), machineWord(sram, _params, 6)));
		_v.at(0x5c) = INT32_MAX;
		_v.at(0x2c) = interpolate(_rom.ptr(Feedback), machineWord(sram, _params, 6));

		// One envelope target for units 1 to 4, and the RAM sine for units 1 to 5
		const int32_t target = interpolate(_rom.ptr(Target), machineWord(sram, _params, 10));
		_v.at(0xdc) = target;
		_v.at(0x15c) = target;
		_v.at(0x1dc) = target;
		_v.at(0x25c) = target;
		for(uint32_t k = 1; k <= 5; ++k)
			_v.unit(k, 0x30) = static_cast<int32_t>(Sine);

		// The mix's four gains: the first fixed, the others from byte 8
		_v.at(0x08) = 0x2d6a866f;
		_v.at(0x0c) = interpolate(_rom.ptr(Gain1), machineWord(sram, _params, 8));
		_v.at(0x10) = interpolate(_rom.ptr(Gain2), machineWord(sram, _params, 8));
		const int32_t gain3 = interpolate(_rom.ptr(Gain3), machineWord(sram, _params, 8));
		_v.at(0x14) = gain3;

		if(_v.at(Voice::TrigNow))
		{
			_v.at(0x414) = 0x012c0f26;
			_v.at(0x418) = 0x012c0f26;
			_v.at(0x41c) = static_cast<int32_t>(0x82581e4b);
			_v.at(0x420) = 0x0013af8c;
			_v.at(0x424) = 0x0013af8c;
			_v.at(0x428) = static_cast<int32_t>(0x80275f18);
			_v.at(Voice::DriveX1) = INT32_MAX;
			_v.at(Voice::DriveY1) = INT32_MAX;

			if(machineWord(sram, _params, 12))
			{
				_v.at(0x08) = 0x4026e73d;
				_v.at(Voice::DriveFloor) = 0x20000000;
				_v.at(Voice::DrivePreGain) = 0x23d70a3d;
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

			// On a trig the gains start at their targets
			_v.at(0x24) = gain3;
			_v.at(0x18) = _v.at(0x08);
			_v.at(0x1c) = _v.at(0x0c);
			_v.at(0x20) = _v.at(0x10);
		}

		_v.at(0x50) = 0x04000000;
		_v.at(0xd0) = interpolate(_rom.ptr(RatioU1), machineWord(sram, _params, 4));
		_v.at(0x150) = interpolate(_rom.ptr(RatioU2), machineWord(sram, _params, 4));
		_v.at(0x1d0) = interpolate(_rom.ptr(RatioU3), machineWord(sram, _params, 4));
		_v.at(0x250) = interpolate(_rom.ptr(RatioU4), machineWord(sram, _params, 4));

		phaseIncrements48k(_pitch, _v, _params, 5);
	}

	void render(int32_t* _out, Voice& _v, const Rom& _rom)
	{
		Sram& sram = _v.sram;
		unitEnvelopes(_v, 5);
		feedbackOscillator(4, _v, _rom);
		pmOscillator(0, _v, _rom);
		pmOscillator(1, _v, _rom);
		pmOscillator(2, _v, _rom);
		pmOscillator(3, _v, _rom);
		const int32_t* x[4] = {sram.ptr(bufferB(0)), sram.ptr(bufferA(1)), sram.ptr(bufferA(2)), sram.ptr(bufferA(3))};
		mix4(x, _out, _v);
		ampEnvelope(_v);
		vca(_out, _v);
		driveClip(_out, _v);
	}
}
