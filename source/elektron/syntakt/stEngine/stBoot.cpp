#include "stBoot.h"

#include "stEngine.h"
#include "stTrack.h"

namespace syntakt
{
	namespace
	{
		constexpr uint32_t Block = 0x80008000;
		constexpr uint32_t Histories = 0x80008a60;	// 12 longs per voice

		// The boot's two copies of initialized data, from section 7 into SRAM
		struct Copy
		{
			uint32_t from, to, dst;
		};
		constexpr Copy Copies[] = {{0x4004f6e0, 0x40057670, 0x80000000}, {0x40057670, 0x4005df10, 0x80008000}};

		void copyBytes(Sram& _sram, const uint32_t _dst, const uint32_t _src, const uint32_t _n)
		{
			for(uint32_t i = 0; i < _n; ++i)
				_sram.setByte(_dst + i, _sram.byte(_src + i));
		}
	}

	void boot(Sram& _sram, Globals& _g, const Rom& _rom)
	{
		_sram.words.fill(0);
		for(const auto& c : Copies)
			for(uint32_t a = c.from; a < c.to; a += 4)
				_sram.at(c.dst + (a - c.from)) = _rom.at(a);

		for(int v = 0; v < Voices; ++v)
		{
			_g.rng = {0x017664c6, 0x8bf3};
			Voice voice{_sram, Voice::addressOf(v)};
			voice.at(Voice::DecimatorHistory) = static_cast<int32_t>(Histories + 0x30 * static_cast<uint32_t>(v));
			initVoice(voice, _g, _rom);
		}
	}

	void receiveBlock(Sram& _sram, const uint8_t* _block, int32_t& _trig, int32_t& _gateOff)
	{
		for(uint32_t i = 0; i < BlockBytes; ++i)
			_sram.setByte(Block + i, _block[i]);
		_sram.at(0x8000a07c) = _sram.at(Block + 0x184);

		for(uint32_t t = 0; t < static_cast<uint32_t>(Voices); ++t)
		{
			const uint32_t p = TrackParams + 0x8e * t;
			copyBytes(_sram, p + 0x3e, Block + 20 * t, 20);
			auto half = [&](const uint32_t _dst, const uint32_t _src)
			{
				_sram.setByte(p + _dst, _sram.byte(Block + _src));
				_sram.setByte(p + _dst + 1, _sram.byte(Block + _src + 1));
			};
			half(0x52, 0xa0 + 8 * t);
			half(0x56, 0xa2 + 8 * t);
			half(0x66, 0xa4 + 8 * t);
			half(0x68, 0xa6 + 8 * t);
			const uint32_t s = 0xe0 + 14 * t;
			half(0x6c, s);
			half(0x6e, s + 2);
			half(0x70, s + 4);
			half(0x72, s + 6);
			half(0x74, s + 8);
			half(0x80, s + 10);
			half(0x82, s + 12);
		}

		// A mask in 0x182 blocks the trigs and forces the gate-off
		const uint32_t mask = static_cast<uint16_t>(_sram.half(Block + 0x182));
		_trig = static_cast<int32_t>(~mask & _sram.byte(Block + 0x180));
		_gateOff = static_cast<int32_t>(mask | _sram.byte(Block + 0x181));
	}

	void runBlock(Sram& _sram, Globals& _g, const Rom& _rom, const uint8_t* _block)
	{
		int32_t trig = 0, gateOff = 0;
		receiveBlock(_sram, _block, trig, gateOff);
		renderVoices(_sram, _g, _rom, VoiceOut, TrackParams, trig, gateOff);

		// The tracks' stages, in place, before the block goes out to CPU #1
		track::drive(_sram, VoiceOut, TrackParams);
		track::baseWidth(_sram, _rom, VoiceOut, TrackParams);
		for(uint32_t t = 0; t < static_cast<uint32_t>(Voices); ++t)
		{
			const uint32_t p = TrackParams + 0x8e * t;
			track::filter(_sram, _rom, p + 0x52, VoiceOut + 0x80 * t, _sram.at(Block + 0x188 + 4 * t),
				static_cast<int32_t>(t), trig, _sram.half(p + 0x82));
		}
		track::ampEnvelopes(_sram, _rom, VoiceOut, TrackParams, trig, gateOff, _sram.byte(Block + 0x1a8));
	}
}
