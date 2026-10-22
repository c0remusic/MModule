#include "stMainBlock.h"

#include "stEmac.h"
#include "stMainControl.h"
#include "stMainFilter.h"
#include "stMainLfo.h"
#include "stMainModulation.h"
#include "stMainPitch.h"

namespace syntakt::main
{
	namespace
	{
		constexpr int32_t Legato = 3;			// settings +0x60 whose trigs leave an open gate alone [INFER]
		constexpr int32_t Full = 0x7f00;
		constexpr uint32_t FxTrack = 12;
		constexpr int32_t SyBits = 37;			// the machines whose word +0x48 the voice CPU reads uninverted
		constexpr int32_t SpTwinshot = 44;

		bool bit(const int32_t _mask, const uint32_t _t) { return (static_cast<uint32_t>(_mask) >> _t & 1) != 0; }

		int32_t without(const int32_t _mask, const uint32_t _t)
		{
			return static_cast<int32_t>(static_cast<uint32_t>(_mask) & ~(1u << _t));
		}

		uint32_t trackOf(const uint32_t _p, const uint32_t _t) { return _p + TrackBytes * _t; }

		// The amp a cut or a reload leaves: HOLD full, DEC and REL 0 (track 12's amp is at +0x90 instead)
		void holdAmp(Memory& _mem, const uint32_t _p, const uint32_t _t)
		{
			if(_t == FxTrack)
			{
				_mem.setHalf(_p + 0x73a, 0);
				_mem.setHalf(_p + 0x738, Full);
				_mem.setHalf(_p + 0x73e, 0);
				return;
			}
			const uint32_t track = trackOf(_p, _t);
			_mem.setHalf(track + 0x6e, Full);
			_mem.setHalf(track + 0x70, 0);
			_mem.setHalf(track + 0x74, 0);
		}

		// 0x400a4a10: every track whose bit is set in Kills, lowest first
		void cut(Memory& _mem, const uint32_t _p)
		{
			uint32_t kills = static_cast<uint16_t>(_mem.half(Kills));
			while(kills)
			{
				uint32_t t = 0;
				while(!(kills >> t & 1))
					++t;
				kills &= ~(1u << t);
				holdAmp(_mem, _p, t);
			}
		}

		// 0x400a4b40: a kit reload. At 2 it starts (every gate off, no trig, nothing held) and moves to 1; while it is
		// not 0, tracks 0-11's amp holds and the FX track's +0x706 fades by 127/128 a block, +0x6ec, +0x6f4 and
		// +0x6fc at 0
		void reload(Memory& _mem, const uint32_t _p, BlockMasks& _m)
		{
			const int32_t state = _mem.word(Reload);
			if(state == 0)
				return;
			if(state == 2)
			{
				_mem.setHalf(ReloadLevel, _mem.half(_p + 0x706));
				_m.gateOffs = -1;
				_m.filterGateOffs = -1;
				_m.filterTrigs = 0;
				_mem.setWord(Reload, 1);
				_m.trigs = 0;
				_mem.setHalf(Held, 0);
			}
			for(uint32_t t = 0; t < 12; ++t)
			{
				const uint32_t track = trackOf(_p, t);
				_mem.setHalf(track + 0x70, 0);
				_mem.setHalf(track + 0x6e, Full);
				_mem.setHalf(track + 0x74, 0);
			}
			// muls.w #$7f, then a division by 128 toward zero
			int32_t level = _mem.half(ReloadLevel) * 0x7f;
			if(level < 0)
				level += 0x7f;
			level >>= 7;
			_mem.setHalf(ReloadLevel, level);
			_mem.setHalf(_p + 0x706, level);
			_mem.setHalf(_p + 0x6fc, 0);
			_mem.setHalf(_p + 0x6ec, 0);
			_mem.setHalf(_p + 0x6f4, 0);
		}

		// 0x400a4bc6: tracks 0-7's machine id << 8 at +0x3e, and at +0x4c the voice CPU's hold flag: 1 when AMP MODE
		// (+0x80) is set or HOLD (+0x6e) is above 0x7eff
		void machineWords(Memory& _mem, const uint32_t _p)
		{
			for(uint32_t t = 0; t < 8; ++t)
			{
				const uint32_t track = trackOf(_p, t);
				_mem.setHalf(track + 0x3e, asl(static_cast<int8_t>(_mem.byte(Machines + t)), 8));
				const bool hold = _mem.half(track + 0x80) != 0 || _mem.half(track + 0x6e) > 0x7eff;
				_mem.setHalf(track + 0x4c, hold ? 1 : 0);
			}
		}

		// 0x400a4c02: the analog tracks 8-12's voice state: 4 and its long cleared on a filter gate-off, both cleared
		// on a filter trig
		void analogVoices(Memory& _mem, const BlockMasks& _m)
		{
			for(uint32_t t = 8; t < 13; ++t)
			{
				const uint32_t voice = AnalogVoices + 0x20 * (t - 8);
				if(bit(_m.filterGateOffs, t))
				{
					_mem.setWord(voice, 4);
				}
				else if(bit(_m.filterTrigs, t))
				{
					_mem.setWord(voice, 0);
				}
				else
				{
					continue;
				}
				_mem.setWord(voice + 4, 0);
			}
		}

		// 0x400a4c7a-0x400a4d74: track _t's part of the block
		void trackBlock(Memory& _mem, const uint32_t _p, const uint32_t _t)
		{
			const uint32_t track = trackOf(_p, _t);
			const uint32_t machine = Block + 20 * _t;
			for(uint32_t i = 0; i < 20; ++i)
				_mem.setByte(machine + i, _mem.byte(track + 0x3e + i));
			const int32_t id = static_cast<int8_t>(_mem.byte(Machines + _t));
			if(id != SyBits && id != SpTwinshot)
				_mem.setHalf(machine + 10, subWrap(Full, _mem.half(track + 0x48)));

			const uint32_t filter = Block + 0xa0 + 8 * _t;
			_mem.setHalf(filter, _mem.half(track + 0x52));
			_mem.setHalf(filter + 2, _mem.half(track + 0x56));
			_mem.setHalf(filter + 4, _mem.half(track + 0x66));
			_mem.setHalf(filter + 6, _mem.half(track + 0x68));

			const uint32_t amp = Block + 0xe0 + 14 * _t;
			constexpr uint32_t AmpWords[] = {0x6c, 0x6e, 0x70, 0x72, 0x74, 0x80, 0x82};
			for(uint32_t i = 0; i < 7; ++i)
				_mem.setHalf(amp + 2 * i, _mem.half(track + AmpWords[i]));

			const int32_t pitch = _mem.word(Pitches + 4 * _t);
			_mem.setWord(Block + 0x150 + 4 * _t, pitch);
			_mem.setHalf(Block + 0x170 + 2 * _t, _mem.half(Velocity + 2 * _t));
			const int32_t keyTrack = static_cast<int8_t>(_mem.byte(_p + 2 * EngineWords + SettingsStride * _t + 0x79));
			_mem.setWord(Block + 0x188 + 4 * _t,
				cutoff(_mem, track + 0x52, keyTrack, pitch, static_cast<int32_t>(_t)));
		}
	}

	void fillCvFrame(Memory& _mem, const uint32_t _frame)
	{
		// 0x4011a1e4
		EmacWrap acc;
		acc.mac(0x361134, addWrap(_mem.word(CvSource), static_cast<int32_t>(0xe7000000)));
		const int32_t value = asrReg(addWrap(acc.storeClear(), 0x800000), 9);
		_mem.setWord(CvDefault, value);

		// 0x4011a250: each long is the value's low word, then 0
		const int32_t word = asl(value, 16);
		for(uint32_t i = 0; i < 0x10; i += 4)
			for(const uint32_t base : {0x00u, 0x14u, 0x28u, 0x38u})
				_mem.setWord(_frame + base + i, word);
		for(const uint32_t offset : {0x10u, 0x24u, 0x8cu, 0x90u, 0x50u, 0x54u, 0x60u, 0x64u, 0x70u, 0x74u, 0x88u})
			_mem.setWord(_frame + offset, word);
	}

	void buildBlock(Memory& _mem, const BlockMasks& _masks)
	{
		BlockMasks m = _masks;

		// 0x400a49a6: no trig on a muted track; the pitch keeps the trigs a legato track's open gate takes from the
		// voice, the filter envelope and the LFOs
		m.trigs &= ~static_cast<int32_t>(_mem.half(Mutes));
		const int32_t pitchTrigs = m.trigs;
		const int32_t legato = static_cast<uint16_t>(_mem.half(Gates)) & m.trigs;
		for(uint32_t t = 0; t < 12; ++t)
		{
			if(_mem.word(Settings + SettingsStride * t + 0x60) != Legato || !bit(legato, t))
				continue;
			m.filterTrigs = without(m.filterTrigs, t);
			m.trigs = without(m.trigs, t);
			m.lfoTrigs = without(m.lfoTrigs, t);
		}

		fillCvFrame(_mem, CvFrame);
		const uint32_t p = smooth(_mem, Engine);
		cut(_mem, p);

		// 0x400a4a6c: the eDMA of the voice CPU's last output (its registers are the plug-in's), or its buffer cleared
		if(_mem.word(VoiceOutputDma) != 0)
		{
			_mem.setByte(DmaPending, 1);
		}
		else
		{
			for(uint32_t i = 0; i < 0x500; ++i)
				_mem.setByte(VoiceOutputs + i, 0);
		}

		modulationMatrix(_mem, p);
		lfos(_mem, p, _mem.word(Tempo), m.lfoTrigs);
		pitches(_mem, p, pitchTrigs);
		reload(_mem, p, m);
		machineWords(_mem, p);
		analogVoices(_mem, m);
		const int32_t mute = static_cast<int8_t>(_mem.byte(Mutes + 1));
		filterEnvelopes(_mem, p, m.filterTrigs, m.filterGateOffs, mute);
		for(uint32_t t = 0; t < 8; ++t)
			trackBlock(_mem, p, t);

		// 0x400a4d78: what the voice CPU plays: the trigs and gate-offs of the tracks not muted, the cuts (0x182 blocks
		// a trig and forces a gate-off, 0x1a8 with the mutes stops the amp), the tempo
		_mem.setWord(Block + 0x184, _mem.word(Tempo));
		const uint32_t muted = _mem.byte(Mutes + 1);
		_mem.setByte(Block + 0x180, static_cast<uint8_t>(~muted & static_cast<uint32_t>(m.trigs)));
		_mem.setByte(Block + 0x181, static_cast<uint8_t>(~muted & static_cast<uint32_t>(m.gateOffs)));
		const uint32_t kills = static_cast<uint16_t>(_mem.half(Kills));
		_mem.setHalf(Block + 0x182, static_cast<int32_t>(kills & 0xff));
		_mem.setByte(Block + 0x1a8, static_cast<uint8_t>(muted | kills));
	}
}
