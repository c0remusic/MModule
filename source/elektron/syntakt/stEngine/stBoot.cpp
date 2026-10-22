#include "stBoot.h"

#include "stEngine.h"
#include "stTrack.h"

namespace syntakt
{
	namespace
	{
		// The boot's two copies of initialized data, from section 7 into SRAM
		struct Copy
		{
			uint32_t from, to, dst;
		};
		constexpr Copy Copies[] = {{0x4004f6e0, 0x40057670, 0x80000000}, {0x40057670, 0x4005df10, 0x80008000}};

		// _n bytes of CPU #1's block, from _src, to the big-endian SRAM at _dst
		void copyBytes(Sram& _sram, const uint32_t _dst, const uint8_t* _src, const uint32_t _n)
		{
			for(uint32_t i = 0; i < _n; ++i)
				_sram.setByte(_dst + i, _src[i]);
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
			voice.at(Voice::DecimatorHistory) = static_cast<int32_t>(Histories + HistoryStride * static_cast<uint32_t>(v));
			initVoice(voice, _g, _rom);
		}
	}

	void receiveBlock(Sram& _sram, const uint8_t* _block, int32_t& _trig, int32_t& _gateOff)
	{
		// The block, a big-endian word at a time
		for(uint32_t i = 0; i < BlockBytes; i += 4)
		{
			_sram.at(Block + i) = static_cast<int32_t>(uint32_t(_block[i]) << 24 | uint32_t(_block[i + 1]) << 16 |
				uint32_t(_block[i + 2]) << 8 | _block[i + 3]);
		}
		_sram.at(ClockStep) = _sram.at(Block + 0x184);

		for(uint32_t t = 0; t < static_cast<uint32_t>(Voices); ++t)
		{
			const uint32_t p = TrackParams + ParamStride * t;
			const uint32_t s = 0xe0 + 14 * t;
			copyBytes(_sram, p + 0x3e, _block + 20 * t, 20);	// the machine block
			copyBytes(_sram, p + 0x52, _block + 0xa0 + 8 * t, 2);	// filter type
			copyBytes(_sram, p + 0x56, _block + 0xa2 + 8 * t, 2);	// resonance
			copyBytes(_sram, p + 0x66, _block + 0xa4 + 8 * t, 4);	// BASE, WIDTH
			copyBytes(_sram, p + 0x6c, _block + s, 10);			// amp ATK, HOLD, DEC, SUS, REL
			copyBytes(_sram, p + 0x80, _block + s + 10, 4);		// amp MODE, RST
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
			const uint32_t p = TrackParams + ParamStride * t;
			track::filter(_sram, _rom, p + 0x52, VoiceOut + TrackOutStride * t, _sram.at(Block + 0x188 + 4 * t),
				static_cast<int32_t>(t), trig, _sram.half(p + 0x82));
		}
		track::ampEnvelopes(_sram, _rom, VoiceOut, TrackParams, trig, gateOff, _sram.byte(Block + 0x1a8));
	}
}
