#include "stEngines.h"

#include "stEmac.h"
#include "stStages.h"

// SY TONE (engine 4): two-operator FM at 96 kHz. Unit 0, a sine with feedback from its own scaled output under its
// envelope, phase-modulates unit 1, a sine at the note; unit 1's raw output is decimated 2:1 by a polyphase IIR
// halfband to 48 kHz, then goes through the amp envelope and the drive stage.

namespace syntakt::syTone
{
	namespace
	{
		constexpr int Oversampled = 2 * BlockSize;	// the operators' block at 96 kHz

		// Scratch buffers in SRAM, as the original lays them out for this engine. Each holds an 8-word header, then
		// 64 samples: the decimator writes its history into unit 1's header
		constexpr uint32_t Modulator = bufferA(0);	// unit 0 scaled: unit 1's phase modulation
		constexpr uint32_t Carrier = bufferB(1);	// unit 1 raw, header and samples, into the decimator
		constexpr uint32_t Recursion = 0x80009c58;	// the decimator's allpass outputs: 4 of history, then 32

		// Tables of section 7 (128 entries)
		constexpr uint32_t Feedback = 0x40021404;
		constexpr uint32_t Ratio = 0x40021604;
		constexpr uint32_t Index = 0x40021804;
		constexpr uint32_t ModTarget = 0x40021a04;
		constexpr uint32_t ModDecay = 0x40021c04;
		constexpr uint32_t AmpDecay = 0x40021e04;

		// The table lookup both operators share: index and fraction from the phase sum, linear interpolation, then
		// halved
		int32_t lookup(const int32_t* _table, Emac& _phase)
		{
			const int32_t p = _phase.storeClear();
			const int32_t i = (p & 0xffffff) >> 16;
			const int32_t f = asl(p & 0xffff, 15);
			Emac acc;
			acc.load(_table[i]);
			acc.mac(f, _table[i + 1]);
			acc.msac(f, _table[i]);
			return acc.storeClear() >> 1;
		}

		// 0x40003aac: unit _k's sine with feedback from its scaled output, 64 samples at 96 kHz, into its buffers A
		// (scaled by the unit's envelope) and B (raw, halved). The feedback depth ramps to 0x2c / 16
		void feedbackOperator(const uint32_t _k, Voice& _v, const Rom& _rom)
		{
			Sram& sram = _v.sram;
			int32_t* a = sram.ptr(static_cast<uint32_t>(_v.unit(_k, 0xc8)) + 0x20);
			int32_t* b = sram.ptr(static_cast<uint32_t>(_v.unit(_k, 0xcc)) + 0x20);
			const int32_t* wave = table(sram, _rom, static_cast<uint32_t>(_v.unit(_k, 0xb0)));

			const int32_t target = frac(0x08000000, _v.at(0x2c));
			int32_t fb = _v.at(0x30);
			const int32_t fbStep = subWrap(target, fb) >> 6;
			const int32_t inc = _v.unit(_k, 0x78);
			const int32_t level = _v.unit(_k, Voice::EnvLevel);
			int32_t amount = _v.unit(_k, Voice::EnvLevelPrev);
			int32_t d = subSat(level, amount);
			const int32_t amountStep = (d < 0 ? d + 0x3f : d) >> 6;
			int32_t phase = _v.unit(_k, Voice::Phase);
			int32_t y = _v.at(0x28);

			Emac acc1;
			acc1.load(phase);
			acc1.mac(fb, y);
			for(int n = 0; n < Oversampled; ++n)
			{
				fb = addWrap(fb, fbStep);
				const int32_t s = lookup(wave, acc1);
				phase = addWrap(phase, inc) & 0xffffff;
				Emac acc0;
				acc0.msac(amount, s);
				amount = addWrap(amount, amountStep);
				acc1.load(phase);
				b[n] = s;
				y = acc0.storeClear();
				acc1.mac(fb, y);	// the scaled sample feeds back
				a[n] = y;
			}

			_v.at(0x28) = y;
			_v.unit(_k, Voice::Phase) = phase;
			_v.unit(_k, Voice::EnvLevelPrev) = level;
			_v.at(0x30) = target;
		}

		// 0x40002c16: unit _k's sine phase-modulated by two buffers, 64 samples at 96 kHz, into its buffers A (scaled
		// by the unit's envelope) and B (raw, halved). The depths ramp to twice their targets
		void pmOperator(const uint32_t _k, Voice& _v, const Rom& _rom)
		{
			Sram& sram = _v.sram;
			int32_t* a = sram.ptr(static_cast<uint32_t>(_v.unit(_k, 0xc8)) + 0x20);
			int32_t* b = sram.ptr(static_cast<uint32_t>(_v.unit(_k, 0xcc)) + 0x20);
			const int32_t* src1 = sram.ptr(static_cast<uint32_t>(_v.unit(_k, 0x9c)) + 0x20);
			const int32_t* src2 = sram.ptr(static_cast<uint32_t>(_v.unit(_k, 0xa0)) + 0x20);
			const int32_t* wave = table(sram, _rom, static_cast<uint32_t>(_v.unit(_k, 0xb0)));

			const int32_t target1 = addWrap(_v.unit(_k, 0x8c), _v.unit(_k, 0x8c));
			const int32_t target2 = addWrap(_v.unit(_k, 0x90), _v.unit(_k, 0x90));
			int32_t amount = _v.unit(_k, Voice::EnvLevelPrev);
			const int32_t amountStep = subWrap(_v.unit(_k, Voice::EnvLevel), amount) >> 6;
			int32_t depth1 = _v.unit(_k, 0x94);
			const int32_t step1 = subWrap(target1, depth1) >> 6;
			int32_t depth2 = _v.unit(_k, 0x98);
			const int32_t step2 = subWrap(target2, depth2) >> 6;
			const int32_t inc = _v.unit(_k, 0x78);
			int32_t phase = _v.unit(_k, Voice::Phase);

			// The phase sum, phase + depth1 src1 + depth2 src2, truncated once
			Emac acc1;
			acc1.load(phase);
			acc1.mac(depth1, src1[0]);
			for(int n = 0; n < Oversampled; ++n)
			{
				acc1.mac(depth2, src2[n]);
				depth1 = addWrap(depth1, step1);
				depth2 = addWrap(depth2, step2);
				const int32_t s = lookup(wave, acc1);
				phase = addWrap(phase, inc) & 0xffffff;
				acc1.load(phase);
				Emac acc0;
				acc0.msac(amount, s);
				amount = addWrap(amount, amountStep);
				b[n] = s;
				acc1.mac(depth1, src1[n + 1]);
				a[n] = acc0.storeClear();
			}

			_v.unit(_k, Voice::Phase) = phase;
			_v.unit(_k, Voice::EnvLevelPrev) = _v.unit(_k, Voice::EnvLevel);
			_v.unit(_k, 0x94) = target1;
			_v.unit(_k, 0x98) = target2;
		}

		// 0x400051c6: a 2:1 polyphase IIR halfband, 96 kHz to 48 kHz. _buf is 8 words of history then 64 samples;
		// one branch is a pure delay, the other a 4th-order recursion on every other sample, and each output is their
		// sum. The voice's history (12 words at 0x56c) keeps the last 8 inputs and the last 4 recursion outputs,
		// those rounded up to even
		void decimate(Voice& _v, const uint32_t _buf, int32_t* _out)
		{
			constexpr int32_t C0 = 0x3d91d187;
			constexpr int32_t C1 = static_cast<int32_t>(0xf3656e2d);
			constexpr int32_t C2 = 0x043f7701;
			constexpr int32_t C3 = static_cast<int32_t>(0xfe7b06ed);

			Sram& sram = _v.sram;
			int32_t* h = sram.ptr(static_cast<uint32_t>(_v.at(Voice::DecimatorHistory)));
			int32_t* x = sram.ptr(_buf);
			int32_t* y = sram.ptr(Recursion) + 4;
			for(int i = 0; i < 8; ++i)
				x[i] = h[i];
			for(int i = 0; i < 4; ++i)
				y[i - 4] = h[8 + i];

			for(int m = 0; m < BlockSize / 2; ++m)
			{
				const int32_t* xi = x + 1 + 4 * m;

				// The first term of y0 is a product by 0x7fffffff, the first of y1 a load
				Emac acc0;
				acc0.mac(INT32_MAX, xi[0]);
				Emac acc2;
				acc2.load(xi[2]);
				acc0.mac(C0, xi[2]);
				acc2.mac(C0, xi[4]);
				acc0.mac(C1, xi[4]);
				acc2.mac(C1, xi[6]);
				acc0.mac(C2, xi[6]);
				acc2.mac(C2, xi[8]);
				acc0.mac(C3, xi[8]);
				acc2.mac(C3, xi[10]);
				acc0.msac(C3, y[-4]);
				acc0.msac(C2, y[-3]);
				acc0.msac(C1, y[-2]);
				acc0.msac(C0, y[-1]);
				acc2.msac(C3, y[-3]);
				acc2.msac(C2, y[-2]);
				acc2.msac(C1, y[-1]);
				const int32_t y0 = acc0.storeClear();
				y[0] = y0;
				acc2.msac(C0, y0);
				_out[2 * m] = addSat(xi[1], y0);
				const int32_t y1 = acc2.storeClear();
				y[1] = y1;
				_out[2 * m + 1] = addSat(xi[3], y1);
				y += 2;
			}

			for(int i = 0; i < 8; ++i)
				h[i] = x[64 + i];
			const int32_t* last = sram.ptr(Recursion) + 4 + 28;
			for(int i = 0; i < 4; ++i)
				h[8 + i] = static_cast<int32_t>(static_cast<uint32_t>(addWrap(last[i], 1)) & ~1u);
		}
	}

	void update(const int32_t _pitch, Voice& _v, const uint32_t _params, const Rom& _rom)
	{
		Sram& sram = _v.sram;
		_v.at(Voice::AmpDecay) = _rom.entry(AmpDecay, machineByte(sram, _params, 16));

		if(_v.at(Voice::TrigNow))
		{
			_v.at(0x28) = 0;
			_v.at(0x414) = 0x012c0f26;
			_v.at(0x418) = 0x012c0f26;
			_v.at(Voice::DriveX1) = INT32_MAX;
			_v.at(Voice::DriveY1) = INT32_MAX;
			_v.at(0x41c) = static_cast<int32_t>(0x82581e4b);
			_v.at(0x420) = 0x001f7c5c;
			_v.at(0x424) = 0x001f7c5c;
			_v.at(0x428) = static_cast<int32_t>(0x803ef8b8);
			_v.at(0x58) = _rom.entry(ModDecay, machineByte(sram, _params, 10));

			if(machineWord(sram, _params, 12))
			{
				_v.at(Voice::DriveFloor) = 0x20000000;
				_v.at(Voice::DriveGain) = 0x20000000;
				_v.at(Voice::DrivePreGain) = 0x60000000;
				_v.at(Voice::DriveOn) = 1;
			}
			else
			{
				_v.at(Voice::DriveFloor) = INT32_MAX;
				_v.at(Voice::DrivePreGain) = 0x20000000;
				_v.at(Voice::DriveGain) = 0x20000000;
				_v.at(Voice::DriveOn) = 0;
			}

			int32_t* h = sram.ptr(static_cast<uint32_t>(_v.at(Voice::DecimatorHistory)));
			for(int i = 0; i < 12; ++i)
				h[i] = 0;
		}

		_v.at(0x2c) = interpolate(_rom.ptr(Feedback), machineWord(sram, _params, 8));
		_v.at(0x40) = static_cast<int32_t>(0xffc00000);	// -64 semitones
		_v.at(0x44) = 0x00800000;
		_v.at(0x50) = interpolate(_rom.ptr(Ratio), machineWord(sram, _params, 4));
		_v.at(0x5c) = interpolate(_rom.ptr(ModTarget), machineWord(sram, _params, 10));
		_v.at(0xd0) = 0x04000000;						// unit 1 at the note
		_v.at(0x10c) = frac(0x02000000, interpolate(_rom.ptr(Index), machineWord(sram, _params, 6)));
		_v.at(0x11c) = static_cast<int32_t>(Modulator);

		phaseIncrements96k(_pitch, _v, _params);
	}

	void render(int32_t* _out, Voice& _v, const Rom& _rom)
	{
		unitEnvelopes(_v, 2);
		feedbackOperator(0, _v, _rom);
		pmOperator(1, _v, _rom);
		decimate(_v, Carrier, _out);
		ampEnvelope(_v);
		vca(_out, _v);
		driveClip(_out, _v);
	}
}
