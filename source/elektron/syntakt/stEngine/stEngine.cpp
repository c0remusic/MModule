#include "stEngine.h"

#include <algorithm>

#include "stEngines.h"

namespace syntakt
{
	namespace
	{
		constexpr uint32_t SortOrder = 0x80004a48;		// the voice indices, bubble-sorted by machine
		constexpr uint32_t SortKey = 0x80004a40;		// each voice's machine byte
		constexpr uint32_t Pitch = Block + 0x150;		// the block's pitch words, one per track
		constexpr uint32_t MachineToEngine = 0x40014950;	// 46 bytes
		constexpr int32_t LastMachine = 45;
	}

	const EngineRoutines* engine(const uint32_t _id)
	{
		for(const auto& e : PortedEngines)
			if(e.id == _id)
				return &e;
		return nullptr;
	}

	int32_t engineOf(const Rom& _rom, int32_t _machine)
	{
		if(_machine < 0)
			_machine = 0;
		else if(_machine > LastMachine)
			_machine = LastMachine;
		return static_cast<int8_t>(_rom.byte(MachineToEngine + static_cast<uint32_t>(_machine)));
	}

	void renderVoices(Sram& _sram, Globals& _g, const Rom& _rom, const uint32_t _out, const uint32_t _params,
		const int32_t _trig, const int32_t _gateOff)
	{
		uint8_t order[Voices], key[Voices];
		for(uint32_t v = 0; v < static_cast<uint32_t>(Voices); ++v)
		{
			order[v] = static_cast<uint8_t>(v);
			key[v] = _sram.byte(_params + ParamStride * v + 0x3e);
		}

		// A stable bubble sort on the signed machine byte, in the original's order of comparisons. The original keeps
		// both arrays in SRAM, where they are left as it leaves them
		for(int32_t pass = 7; pass > 0; --pass)
		{
			for(int32_t j = 0; j < pass; ++j)
			{
				const uint8_t a = order[j], b = order[j + 1];
				if(static_cast<int8_t>(key[static_cast<int8_t>(b)]) < static_cast<int8_t>(key[static_cast<int8_t>(a)]))
				{
					order[j] = b;
					order[j + 1] = a;
				}
			}
		}
		for(uint32_t v = 0; v < static_cast<uint32_t>(Voices); ++v)
		{
			_sram.setByte(SortOrder + v, order[v]);
			_sram.setByte(SortKey + v, key[v]);
		}

		for(uint32_t i = 0; i < static_cast<uint32_t>(Voices); ++i)
		{
			const int32_t v = static_cast<int8_t>(order[i]);
			const uint32_t params = _params + ParamStride * static_cast<uint32_t>(v);
			Voice voice{_sram, Voice::addressOf(v)};

			voice.at(Voice::TrigPending) = (_trig >> v) & 1;
			voice.at(Voice::GateOff) = (_gateOff >> v) & 1;

			if(voice.at(Voice::TrigNow))
			{
				const int32_t id = engineOf(_rom, static_cast<int8_t>(_sram.byte(params + 0x3e)));
				if(voice.at(Voice::EngineCopy) != id)
				{
					initVoice(voice, _g, _rom);
					voice.at(Voice::TrigNow) = 1;
					voice.at(Voice::Engine) = id;
					voice.at(Voice::EngineCopy) = id;
				}
			}

			voice.at(Voice::AmpHold) = _sram.half(params + 0x4c);
			restartPhases(voice);

			// The original runs engines 0 to 11 (an unsigned compare); a voice on one not ported writes silence
			if(const auto* e = engine(static_cast<uint32_t>(voice.at(Voice::Engine))))
			{
				e->update(_sram.at(Pitch + 4 * static_cast<uint32_t>(v)), voice, params + 0x1c, _rom);
				e->render(_sram.ptr(_out + TrackOutStride * static_cast<uint32_t>(v)), voice, _rom);
			}
			else
			{
				std::fill_n(_sram.ptr(_out + TrackOutStride * static_cast<uint32_t>(v)), BlockSize, 0);
			}
			voice.at(Voice::TrigNow) = voice.at(Voice::TrigPending);
		}
	}
}
