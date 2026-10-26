// The Machinedrum's voice DSP (DSP2, OS 1.63) ported to C++, one Track's voice at a time, without an emulator: the
// 64-word state block Y:S..S+$3F and the dispatcher of .scratch/uc-less-engines/machinedrum-kit/08-dsp2-voice-abi.md
// (init on a type change, update on every trigger, render every block). Each ported machine is checked sample for
// sample against mdEngine's VoiceEngine by mdVoicePortTest. Ticket 02 of .scratch/elektron-drums-v1.
#pragma once
#include <array>
#include <cstdint>

namespace md::voice
{
	class Voice
	{
	public:
		static constexpr int kBlockFrames = 32;
		static constexpr int kStateWords = 64;
		using State = std::array<uint32_t, kStateWords>;	// 24-bit words
		using Block = std::array<int32_t, kBlockFrames>;	// 24-bit signed samples

		// The types (machine id + 1) whose code is ported: 0 (never assigned), GND-- and GND-IM
		static bool ported(uint32_t _type);

		// As VoiceEngine::setSlot: word 0 the trigger (the type, non-zero only on the block the voice is triggered),
		// then the machine's coefficient words
		void setSlot(const uint32_t* _words, int _count);

		// One block, as the dispatcher runs it: init when the trigger brings another type, update on every trigger,
		// render always. Throws std::out_of_range on a type not ported.
		void render(Block& _out);

		const State& state() const { return m_state; }

	private:
		State m_state{};
		uint32_t m_active = 0;	// Y:$153 + t
	};
}
