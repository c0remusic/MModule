#include "stMainOutput.h"

#include "stEmac.h"
#include "stMainBlock.h"
#include "stMainBus.h"
#include "stMainDelay.h"
#include "stMainFx.h"
#include "stMainMix.h"
#include "stMainReverb.h"

namespace syntakt::main
{
	namespace
	{
		constexpr uint32_t KitPointer = 0x800030b8;		// a long: the kit, 0x123 bytes per track
		constexpr uint32_t Velocity = 0x80004670;		// a word per track
		constexpr uint32_t AnalogGains = 0x401dc5e0;	// by the machine's byte +0x3e, for analog tracks
		constexpr uint32_t LineIn8 = 0x80004150;		// channels 8 and 9 of the track buffers: the returns
		constexpr uint32_t LineIn9 = 0x800041d0;
		constexpr uint32_t OverbridgeGains = 0x43bd768c;	// 12 longs: each track's last gain
		constexpr int32_t Unit = 0x40810204;			// 64/127

		int32_t ramp(const int32_t _target, const Memory& _mem, const uint32_t _current)
		{
			return subWrap(_target, _mem.word(_current)) >> 5;
		}

		// -2 v^2 of a parameter word's low 16 bits (muls.w, neg, add)
		int32_t square(const int32_t _v)
		{
			const int16_t v = static_cast<int16_t>(_v);
			const int32_t s = neg(static_cast<int32_t>(static_cast<uint32_t>(v * v)));
			return addWrap(s, s);
		}

		// A long of _table by the signed byte at _index
		int32_t bySignedByte(const Memory& _mem, const uint32_t _table, const uint32_t _index)
		{
			return _mem.word(_table + 4 * static_cast<uint32_t>(int32_t(int8_t(_mem.byte(_index)))));
		}

		int8_t kitByte(const Memory& _mem, const uint32_t _t)
		{
			return static_cast<int8_t>(_mem.byte(static_cast<uint32_t>(_mem.word(KitPointer)) + 0x123 * _t + 205));
		}

		// 0x4008f7e6, 0x4008f91e: SendBus += AnalogBus, saturated
		void addAnalogSends(Memory& _mem)
		{
			for(uint32_t i = 0; i < 2 * BlockFrames; ++i)
				_mem.setWord(SendBus + 4 * i, addSat(_mem.word(SendBus + 4 * i), _mem.word(AnalogBus + 4 * i)));
		}
	}

	int32_t trackSends(Memory& _mem, Emac4& _acc, const uint32_t _p, MixFrame& _f)
	{
		int32_t fxMutes = _mem.half(0x80003148);
		if(fxMutes >= 0)
		{
			_f.at(-0x1fc) = _mem.half(0x8000314a);
		}
		else
		{
			_f.at(-0x1fc) = _mem.half(0x8000314e);
			fxMutes = _mem.half(0x8000314c);
		}

		for(uint32_t t = 0; t < 12; ++t)
		{
			const uint32_t tp = _p + TrackBytes * t;
			const int32_t o = static_cast<int32_t>(4 * t);
			const Pair g = trackGain<false>(_mem, static_cast<uint32_t>(_mem.half(_p + 2 * t)),
				static_cast<uint32_t>(_mem.half(Velocity + 2 * t)), static_cast<uint32_t>(_mem.half(tp + 0x7c)),
				static_cast<uint32_t>(kitByte(_mem, t)));
			int32_t gain = g.d0;
			if(t > 7 && ((_mem.byte(Machines + t) - 32u) & 0xff) > 1)
			{
				_acc.msac(0, gain, bySignedByte(_mem, AnalogGains, tp + 0x3e));
				gain = _acc.storeClear(0);
			}
			const Pair q = pan(_mem.half(tp + 0x7a));
			_acc.msac(0, q.d0, gain);
			_acc.msac(1, q.d1, gain);
			const int32_t l = _acc.storeClear(0), r = _acc.storeClear(1);
			const bool fxOff = (fxMutes >> t) & 1;
			const bool routed = _mem.half(tp + 0x7e) != 0;
			const int32_t fxL = routed && !fxOff ? l : 0, fxR = routed && !fxOff ? r : 0;
			if(t <= 7)
			{
				_f.at(-0xa0 + o) = ramp(routed ? 0 : l, _mem, 0x8000e188 + 4 * t);
				_f.at(-0x78 + o) = ramp(routed ? 0 : r, _mem, 0x8000e160 + 4 * t);
				_f.at(-0x50 + o) = ramp(fxL, _mem, 0x8000e138 + 4 * t);
				_f.at(-0x28 + o) = ramp(fxR, _mem, 0x8000e110 + 4 * t);
			}
			else
			{
				_f.at(-0x148 + o) = ramp(fxL, _mem, 0x8000e0d0 + 4 * t);
				_f.at(-0x128 + o) = ramp(fxR, _mem, 0x8000e0b0 + 4 * t);
			}
			int32_t del = 0, rev = 0;
			if(!((_f.at(-0x1fc) >> t) & 1))
			{
				del = square(_mem.half(tp + 0x76));
				rev = square(_mem.half(tp + 0x78));
			}
			_acc.msac(0, l, del);
			_acc.msac(1, r, del);
			const int32_t delL = _acc.storeClear(0), delR = _acc.storeClear(1);
			_acc.msac(0, l, rev);
			_acc.msac(1, r, rev);
			const int32_t revL = _acc.storeClear(0), revR = _acc.storeClear(1);
			if(t <= 7)
			{
				_f.at(-0x1e8 + o) = ramp(delL, _mem, 0x80006328 + 4 * t);
				_f.at(-0x1c8 + o) = ramp(delR, _mem, 0x80006308 + 4 * t);
				_f.at(-0xe8 + o) = ramp(revL, _mem, 0x800062e4 + 4 * t);
				_f.at(-0xc4 + o) = ramp(revR, _mem, 0x800062c0 + 4 * t);
			}
			else
			{
				_f.at(-0x1c8 + o) = ramp(delL, _mem, 0x80006280 + 4 * t);
				_f.at(-0x1a8 + o) = ramp(delR, _mem, 0x80006260 + 4 * t);
				_f.at(-0x188 + o) = ramp(revL, _mem, 0x80006240 + 4 * t);
				_f.at(-0x168 + o) = ramp(revR, _mem, 0x80006220 + 4 * t);
			}
		}

		// The external input (ADC 4-5): its level (+0x70a), halved when mono (byte 0x80003146 = 0), pan (+0x70e), two
		// sends (+0x710, +0x714) and a direct gain while +0x71a is set
		const bool mono = _mem.byte(0x80003146) == 0;
		_acc.msac(0, Unit, asl(_mem.half(_p + 0x70a), 16));
		int32_t level = _acc.storeClear(0);
		level = addSat(level, level);
		_acc.msac(0, level, level);
		level = _acc.storeClear(0);
		if(mono)
			level >>= 1;
		const Pair q = pan(_mem.half(_p + 0x70e));
		_acc.msac(0, q.d0, level);
		_acc.msac(1, q.d1, level);
		const int32_t gl = _acc.storeClear(0), gr = _acc.storeClear(1);
		const int32_t send1 = square(_mem.half(_p + 0x710)), send2 = square(_mem.half(_p + 0x714));
		const bool direct = _mem.half(_p + 0x71a) != 0;
		_acc.msac(0, gl, send1);
		_acc.msac(1, gr, send1);
		const int32_t d1L = _acc.storeClear(0), d1R = _acc.storeClear(1);
		_acc.msac(0, gl, send2);
		_acc.msac(1, gr, send2);
		const int32_t d2L = _acc.storeClear(0), d2R = _acc.storeClear(1);
		_f.at(-0x198) = ramp(d1L, _mem, 0x800062b0);
		_f.at(-0x158) = ramp(d2L, _mem, 0x80006270);
		_f.at(-0x178) = ramp(mono ? d1R : 0, _mem, 0x80006290);
		_f.at(-0x138) = ramp(mono ? d2R : 0, _mem, 0x80006250);
		_f.at(-0x118) = ramp(direct ? gl : 0, _mem, 0x8000e100);
		_f.at(-0x114) = ramp(direct && mono ? gl : 0, _mem, 0x8000e104);
		_f.at(-0x194) = ramp(mono ? d1L : 0, _mem, 0x800062b4);
		_f.at(-0x154) = ramp(mono ? d2L : 0, _mem, 0x80006274);
		_f.at(-0x174) = ramp(d1R, _mem, 0x80006294);
		_f.at(-0x134) = ramp(d2R, _mem, 0x80006254);
		_f.at(-0xf8) = ramp(direct && mono ? gr : 0, _mem, 0x8000e0e0);
		_f.at(-0xf4) = ramp(direct ? gr : 0, _mem, 0x8000e0e4);

		// The FX track's input (ADC 6-7): its level (+0x18, the FX track's LEV) x +0x748, squared, panned (+0x746),
		// times the analog voices' word 0x454e3c18 of the source +0x71c (0 to 7); sent to the delay and the reverb
		// (+0x742, +0x744) only while each is routed to the FX track, and direct unless bit 14 of the FX bus mask
		const int32_t panWord = _mem.half(_p + 0x746);
		_acc.msac(0, Unit, asl(_mem.half(_p + 0x18), 16));
		int32_t a = _acc.storeClear(0);
		a = addSat(a, a);
		_acc.msac(0, Unit, asl(_mem.half(_p + 0x748), 16));
		int32_t b = _acc.storeClear(0);
		b = addSat(b, b);
		_acc.msac(0, b, a);
		int32_t fx = _acc.storeClear(0);
		const Pair fq = pan(panWord);
		_acc.msac(0, fx, fx);
		fx = _acc.storeClear(0);
		_acc.msac(0, fq.d0, fx);
		_acc.msac(1, fq.d1, fx);
		const int32_t xl = _acc.storeClear(0), xr = _acc.storeClear(1);
		int32_t source = static_cast<int8_t>(_mem.byte(_p + 0x71c));
		source = source < 0 ? 0 : source > 7 ? 7 : source;
		_acc.mac(0, xl, _mem.word(0x454e3c18 + 4 * static_cast<uint32_t>(source)));
		const int32_t al = _acc.storeClear(0);
		_acc.mac(0, xr, _mem.word(0x454e3c38 + 4 * static_cast<uint32_t>(source)));
		const int32_t ar = _acc.storeClear(0);
		const int32_t toDelay = _mem.half(_p + 0x6f6) ? square(_mem.half(_p + 0x742)) : 0;
		const int32_t toReverb = _mem.half(_p + 0x708) ? square(_mem.half(_p + 0x744)) : 0;
		_acc.msac(0, al, toDelay);
		_acc.msac(1, ar, toDelay);
		const int32_t dl = _acc.storeClear(0), dr = _acc.storeClear(1);
		_f.at(-0x16c) = ramp(dr, _mem, 0x8000629c);
		_f.at(-0x190) = ramp(dl, _mem, 0x800062b8);
		_f.at(-0x170) = neg(_mem.word(0x80006298)) >> 5;
		_f.at(-0x18c) = neg(_mem.word(0x800062bc)) >> 5;
		_acc.msac(0, al, toReverb);
		_acc.msac(1, ar, toReverb);
		const int32_t rl = _acc.storeClear(0), rr = _acc.storeClear(1);
		_f.at(-0x150) = ramp(rl, _mem, 0x80006278);
		_f.at(-0x12c) = ramp(rr, _mem, 0x8000625c);
		_f.at(-0x14c) = neg(_mem.word(0x8000627c)) >> 5;
		_f.at(-0x130) = neg(_mem.word(0x80006258)) >> 5;
		const bool fxDirectOff = (fxMutes >> 14) & 1;
		_f.at(-0x110) = ramp(fxDirectOff ? 0 : al, _mem, 0x8000e108);
		_f.at(-0x10c) = neg(_mem.word(0x8000e10c)) >> 5;
		_f.at(-0xec) = ramp(fxDirectOff ? 0 : ar, _mem, 0x8000e0ec);
		_f.at(-0xf0) = neg(_mem.word(0x8000e0e8)) >> 5;

		// The delay's input: the tracks' and the ADC's sends
		_acc.setMacsr(0xa0);
		sumChannels(_mem, _acc, 8, SendBus, VoiceOutputs, 0x80006328, &_f.at(-0x1e8));
		sumChannels(_mem, _acc, 8, SendBus + 4, VoiceOutputs, 0x80006308, &_f.at(-0x1c8));
		sumChannels(_mem, _acc, 8, AnalogBus, Adc, 0x800062a0, &_f.at(-0x1a8));
		sumChannels(_mem, _acc, 8, AnalogBus + 4, Adc, 0x80006280, &_f.at(-0x188));
		addAnalogSends(_mem);
		return fxMutes;
	}

	int32_t mixOutput(Memory& _mem, Emac4& _acc, const uint32_t _p)
	{
		prepareEffects(_mem, _acc, _p);
		MixFrame f;
		const int32_t fxMutes = trackSends(_mem, _acc, _p, f);

		// 0x4008f80a polls channel 30's ESG until its reads are in: here they are
		delay(_mem, _acc, DelayReturn, SendBus, _p + 0x6e6);
		_acc.setMacsr(0xa0);

		// The reverb's input: the tracks', the delay's return (its REV +0x6f2, unless the delay goes to the FX track
		// with a positive +0x742, or bit 12 of the send mutes), the ADC's
		const bool blocked = _mem.half(_p + 0x708) == 0 && _mem.half(_p + 0x742) > 0;
		const int32_t delayToReverb = ((f.at(-0x1fc) >> 12) & 1) || blocked ? 0 : square(_mem.half(_p + 0x6f2));
		f.at(-0xc8) = ramp(delayToReverb, _mem, 0x80006304);
		f.at(-0xa4) = ramp(delayToReverb, _mem, 0x800062e0);
		copySide(_mem, LineIn8, DelayReturn);
		sumChannels(_mem, _acc, 9, SendBus, VoiceOutputs, 0x800062e4, &f.at(-0xe8));
		copySide(_mem, LineIn8, DelayReturn + 4);
		sumChannels(_mem, _acc, 9, SendBus + 4, VoiceOutputs, 0x800062c0, &f.at(-0xc4));
		sumChannels(_mem, _acc, 8, AnalogBus, Adc, 0x80006260, &f.at(-0x168));
		sumChannels(_mem, _acc, 8, AnalogBus + 4, Adc, 0x80006240, &f.at(-0x148));
		addAnalogSends(_mem);
		reverb(_mem, _acc, ReverbReturn, SendBus, _p + 0x6fa);
		advanceReverbLines(_mem);
		writeLines(_mem);
		_acc.setMacsr(0xa0);

		// The returns as channels 8 and 9 of the direct bus, or of the FX bus where they are routed
		const int32_t delayLevel = (fxMutes >> 12) & 1 ? 0 : square(_mem.half(_p + 0x6f4));
		const int32_t reverbLevel = (fxMutes >> 13) & 1 ? 0 : square(_mem.half(_p + 0x706));
		const bool delayFx = _mem.half(_p + 0x6f6) != 0, reverbFx = _mem.half(_p + 0x708) != 0;
		f.at(-0x80) = ramp(delayFx ? 0 : delayLevel, _mem, 0x8000e1a8);
		f.at(-0x58) = ramp(delayFx ? 0 : delayLevel, _mem, 0x8000e180);
		f.at(-0x30) = ramp(delayFx ? delayLevel : 0, _mem, 0x8000e158);
		f.at(-0x8) = ramp(delayFx ? delayLevel : 0, _mem, 0x8000e130);
		f.at(-0x7c) = ramp(reverbFx ? 0 : reverbLevel, _mem, 0x8000e1ac);
		f.at(-0x54) = ramp(reverbFx ? 0 : reverbLevel, _mem, 0x8000e184);
		f.at(-0x2c) = ramp(reverbFx ? reverbLevel : 0, _mem, 0x8000e15c);
		f.at(-0x4) = ramp(reverbFx ? reverbLevel : 0, _mem, 0x8000e134);
		copySide(_mem, LineIn8, DelayReturn);
		copySide(_mem, LineIn9, ReverbReturn);
		sumChannels(_mem, _acc, 10, DirectBus, VoiceOutputs, 0x8000e188, &f.at(-0xa0));
		sumChannels(_mem, _acc, 10, FxBus, VoiceOutputs, 0x8000e138, &f.at(-0x50));
		copySide(_mem, LineIn8, DelayReturn + 4);
		copySide(_mem, LineIn9, ReverbReturn + 4);
		sumChannels(_mem, _acc, 10, DirectBus + 4, VoiceOutputs, 0x8000e160, &f.at(-0x78));
		sumChannels(_mem, _acc, 10, FxBus + 4, VoiceOutputs, 0x8000e110, &f.at(-0x28));

		// The ADC side's direct mix plus the FX bus, x 8: Overbridge's channels 0-1
		sumChannels(_mem, _acc, 8, AnalogBus, Adc, 0x8000e0f0, &f.at(-0x128));
		sumChannels(_mem, _acc, 8, AnalogBus + 4, Adc, 0x8000e0d0, &f.at(-0x108));
		int32_t unity[2] = {INT32_MIN, INT32_MIN};
		const int32_t still[2] = {0, 0};
		mixBuses(_mem, _acc, AnalogBus, AnalogBus, unity, still);
		scaleBus(_mem, _acc, AnalogBus, 8);
		for(uint32_t i = 0; i < BlockFrames; ++i)
		{
			_mem.setWord(Overbridge + 0x50 * i, _mem.word(AnalogBus + 8 * i));
			_mem.setWord(Overbridge + 0x50 * i + 4, _mem.word(AnalogBus + 8 * i + 4));
		}
		for(uint32_t i = 0; i < 2 * BlockFrames; ++i)
		{
			const int32_t v = addSat(_mem.word(DelayReturn + 4 * i), _mem.word(ReverbReturn + 4 * i));
			_mem.setWord(Overbridge + 0x40 + 0x50 * (i >> 1) + 4 * (i & 1), v);
		}
		scaleBus(_mem, _acc, DirectBus, 10);
		scaleBus(_mem, _acc, FxBus, 8);

		// The DAC: the direct bus into slots 7 and 2 against the other input's slots 2 and 3 (0x8000e1b0, the half
		// 0x43bdc4d8 names), with gains easing by 1/100 per frame to the table 0x401dafdc[+0x740] and its share
		// (byte 0x80003144); the FX bus into slots 5 and 3 with 0x43bdbf68's 32 samples and slots 0 and 1
		const int32_t target = bySignedByte(_mem, 0x401dafdc, _p + 0x740);
		const int32_t share = 3 - int32_t(int8_t(_mem.byte(0x80003144)));
		const int32_t target2 = asrReg(target, share);
		const uint32_t half = static_cast<uint32_t>(_mem.word(0x4553f6dc)) << 10;
		const uint32_t other = static_cast<uint32_t>(_mem.word(0x43bdc4d8)) << 10;
		{
			int32_t g1 = _mem.word(0x43bd7688), g0 = _mem.word(0x43bd7684);
			uint32_t dac = DacRing + half, x = 0x8000e1b8 + other, bus = DirectBus;
			int32_t v = _mem.word(bus);
			bus += 4;
			for(uint32_t i = 0; i < BlockFrames; ++i)
			{
				for(int k = 0; k < 3; ++k)
					_acc.mac(0, g1, v);
				v = _mem.word(x);
				for(int k = 0; k < 3; ++k)
					_acc.msac(0, g0, v);
				v = _mem.word(bus);
				bus += 4;
				for(int k = 0; k < 3; ++k)
					_acc.mac(1, g1, v);
				v = _mem.word(x + 4);
				for(int k = 0; k < 3; ++k)
					_acc.msac(1, g0, v);
				v = _mem.word(bus);
				bus += 4;
				_acc.mac(2, 0x7eb851eb, g1);
				_acc.mac(2, 0x0147ae14, target);
				_acc.mac(3, 0x7eb851eb, g0);
				_acc.mac(3, 0x0147ae14, target2);
				_mem.setWord(dac + 0x1c, _acc.storeClear(0) >> 8);
				_mem.setWord(dac + 8, _acc.storeClear(1) >> 8);
				g1 = _acc.storeClear(2);
				g0 = _acc.storeClear(3);
				dac += 0x20;
				x += 0x20;
			}
			_mem.setWord(0x43bd7688, g1);
			_mem.setWord(0x43bd7684, g0);
		}
		{
			int32_t click = _mem.word(0x43bdbf64), fxGain = INT32_MIN;
			const int8_t mode = static_cast<int8_t>(_mem.byte(0x8000313d));
			if(!mode || (mode == 1 && _mem.byte(0x8000313c)))
				click = fxGain = 0;
			const int32_t otherGain = asrReg(INT32_MIN, share);
			uint32_t dac = DacRing + half, x = 0x8000e1b0 + other, bus = FxBus, c = 0x43bdbf68;
			int32_t v = _mem.word(c);
			for(uint32_t i = 0; i < BlockFrames; ++i)
			{
				_acc.msac(0, click, v);
				v = _mem.word(x);
				_acc.mac(0, otherGain, v);
				v = _mem.word(bus);
				bus += 4;
				_acc.mac(0, fxGain, v);
				v = _mem.word(c);
				c += 4;
				_acc.msac(1, click, v);
				v = _mem.word(x + 4);
				_acc.mac(1, otherGain, v);
				v = _mem.word(bus);
				bus += 4;
				_acc.mac(1, fxGain, v);
				v = _mem.word(c);
				_mem.setWord(dac + 0x14, _acc.storeClear(0) >> 8);
				_mem.setWord(dac + 0xc, _acc.storeClear(1) >> 8);
				dac += 0x20;
				x += 0x20;
			}
		}

		// Each track's buffer in place at its gain for Overbridge: velocity and VOL, and LEV where 0x80003145 is set
		// (an analog track's machine level 0x401e08e0 where it is not)
		for(uint32_t t = 0; t < 12; ++t)
		{
			const int8_t machine = static_cast<int8_t>(_mem.byte(Machines + t));
			int32_t level;
			if(_mem.byte(0x80003145))
				level = _mem.half(_p + 2 * t);
			else
				level = t > 7 ? analogLevel(_mem, _acc, machine, 0x7f00) : 0x7f00;
			level = static_cast<int16_t>(level);
			const Pair g = trackGain<true>(_mem, static_cast<uint32_t>(level),
				static_cast<uint32_t>(_mem.half(Velocity + 2 * t)),
				static_cast<uint32_t>(_mem.half(_p + TrackBytes * t + 0x7c)), static_cast<uint32_t>(kitByte(_mem, t)));
			const uint32_t buffer = t <= 7 ? VoiceOutputs + 128 * t : 0x80003550 + 128 * t;
			rampBuffer(_mem, _acc, buffer, _mem.word(OverbridgeGains + 4 * t), g.d0);
			_mem.setWord(OverbridgeGains + 4 * t, g.d0);
		}

		// The Overbridge frame: 2-9 the digital tracks, 10-13 ADC 0-3, 14-15 ADC 6-7. All three in SRAM, whose
		// bytes the memory gives whole
		uint8_t* frames = _mem.writableBytesAt(Overbridge, 0x50 * BlockFrames);
		const uint8_t* voices = _mem.bytesAt(VoiceOutputs, 128 * 8);
		const uint8_t* adc = _mem.bytesAt(Adc, 128 * 8);
		for(uint32_t i = 0; i < BlockFrames; ++i)
		{
			uint8_t* frame = frames + 0x50 * i;
			for(uint32_t t = 0; t < 8; ++t)
				Memory::setWordAt(frame + 8 + 4 * t, Memory::wordAt(voices + 128 * t + 4 * i));
			for(uint32_t c = 0; c < 4; ++c)
				Memory::setWordAt(frame + 0x28 + 4 * c, Memory::wordAt(adc + 128 * c + 4 * i));
			Memory::setWordAt(frame + 0x38, Memory::wordAt(adc + 0x300 + 4 * i));
			Memory::setWordAt(frame + 0x3c, Memory::wordAt(adc + 0x380 + 4 * i));
		}
		_acc.setMacsr(0x20);
		return static_cast<int32_t>(SendBus);
	}
}
