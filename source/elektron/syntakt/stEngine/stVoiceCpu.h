#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "stVoice.h"

namespace syntakt
{
	// The Syntakt's voice CPU (OS 1.41's section 7) as its eight digital tracks play: each track's machine, note and
	// parameters in, its output after the track's stages (drive, BASE/WIDTH, filter, amp envelope) out, one output per
	// track, at the machine's own 48 kHz. No mix and no effects: CPU #1 does those. It boots as it is made, then keeps
	// the block CPU #1 would send: a change takes effect at the next 32-sample block it renders. A trig is seen in its
	// block and sounds from the block after, as on the machine.
	class VoiceCpu
	{
	public:
		static constexpr int Tracks = Voices;
		static constexpr int SampleRate = 48000;

		// The parameter pages the voice CPU reads, a word (value << 8 | fine) per parameter, in the order of CPU #1's
		// block. Machine: the machine's own parameters, index i at byte 2 + 2 i of the track's 20 machine bytes (8 is
		// the one the drive stage reads). Filter: TYPE, RESO, BASE, WIDTH. Amp: ATK, HOLD, DEC, SUS, REL, MODE, RST
		enum class Page
		{
			Machine,
			Filter,
			Amp
		};

		static int parameterCount(Page _page);

		// _section7: the words loadSection7 returned, which must outlive the VoiceCpu
		explicit VoiceCpu(const std::vector<int32_t>& _section7);
		~VoiceCpu();

		// The samples SP TWINSHOT plays, as CPU #1 would load them; nullptr, as it is made: none. _samples must outlive
		// the VoiceCpu or the next call, and not change while a block renders
		void setSamples(const Samples* _samples) { m_rom.samples = _samples; }

		// True when the machine's engine is ported; a track on one that is not plays silence
		bool isPorted(int _machine) const;

		// _track 0-7. The machine byte as CPU #1 sends it (the voice CPU clamps it to 0-45); the note in semitones
		void setMachine(int _track, uint8_t _machine);
		void setNote(int _track, int _note);
		void setParameter(int _track, Page _page, int _index, uint8_t _value, uint8_t _fine);

		// For the next block rendered only, as CPU #1 sends them
		void trig(int _track);
		void gateOff(int _track);

		// _count samples of each track into _outputs[0-7], Q31 as the voice CPU sends them to CPU #1
		void render(int32_t* const* _outputs, size_t _count);

	private:
		friend struct VoiceCpuTest;

		void renderBlock();

		std::unique_ptr<Sram> m_sram;
		Globals m_globals;
		Rom m_rom;
		std::array<uint8_t, BlockBytes> m_block{};
		std::array<int32_t, Voices * BlockSize> m_out{};
		int m_position = BlockSize;		// the next sample of m_out to give; BlockSize: none left
	};
}
