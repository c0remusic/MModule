// The Machinedrum's voice DSP (DSP2, OS 1.63) ported to C++, one Track's voice at a time, without an emulator: the
// 64-word state block Y:S..S+$3F and the dispatcher of .scratch/uc-less-engines/machinedrum-kit/08-dsp2-voice-abi.md
// (init on a type change, update on every trigger, render every block). Each ported machine is checked sample for
// sample against mdEngine's VoiceEngine by mdVoicePortTest. Ticket 02 of .scratch/elektron-drums-v1.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace md::voice
{
	// The voice DSP's tables the ROM machines read, as its program holds them; the caller fills them from the OS (none
	// is in this code) and keeps them while voices use them
	struct RomTables
	{
		std::array<uint32_t, 0x1000> decode{};			// Y:$146000: a 12-bit code's sample
		std::array<uint32_t, 0x620> interpolator{};		// Y:$163-$782: the 7 rows of $E0 phases of the 4x interpolator
		std::array<uint32_t, 7> decimator{};			// X:$244-$24A: half of the last 2:1 decimator's taps
	};

	// The ROM machines' sample bank, as the voice DSP holds it (md::fw::loadRomBankFromFlash builds it): its directory
	// at Y:$147E00, four words per entry (the first sample word's address, the length, the loop start or $FFFFFF, the
	// rate), ROM-01 to -32 at entries 0-31 and ROM-33 to -48 at 48-63; and the sample words from P:$150000, two 12-bit
	// codes each, the first in the high half. The caller owns both, and changes them between blocks only. A word read
	// outside them is 0.
	// A loop shorter than a block's advance (the rate word times PTCH's step over 2^34 samples: 128 for a 44.1 kHz
	// sample at the top step, $7FFFFF) puts the position past the length for good; the DSP's fill then counts 2^24
	// passes and stalls, the port renders the block silent instead (see mdVoiceRom.cpp).
	struct RomBank
	{
		static constexpr uint32_t DirectoryAddr = 0x147e00, DirectoryWords = 264, DataAddr = 0x150000;
		const uint32_t* directory = nullptr;	// DirectoryWords words
		const uint32_t* data = nullptr;
		size_t dataWords = 0;
	};

	// The voice DSP's internal memory that the ROM machines use as scratch, X:$000-$1FF and Y:$000-$0FF, one for all
	// the voices as the DSP has one. Every ROM voice must render every block, Track 1 to 16 in turn: a render may read
	// a word the last one left (a reversed voice retriggered with the part before the retrigger of odd length keeps
	// that part's last pointer from the last render that wrote it, whatever its voice). A caller that skips voices or
	// renders one alone (a preview) loses exactness on that edge only. mdVoicePortTest starts it from what the DSP's
	// own init left in it; a product starts from zeros.
	struct Scratch
	{
		std::array<uint32_t, 0x200> x{};
		std::array<uint32_t, 0x100> y{};
	};

	// What the 16 voices of one engine share, owned by the caller: the ROM machines' tables (nullptr: a ROM machine
	// throws std::logic_error when it renders) and bank (nullptr: none, which plays silence), changed between blocks
	// only, and the scratch
	struct Shared
	{
		const RomTables* tables = nullptr;
		const RomBank* bank = nullptr;
		Scratch scratch;
	};

	class Voice
	{
	public:
		static constexpr int kBlockFrames = 32;
		static constexpr int kStateWords = 64;
		using State = std::array<uint32_t, kStateWords>;	// 24-bit words
		using Block = std::array<int32_t, kBlockFrames>;	// 24-bit signed samples

		explicit Voice(Shared& _shared) : m_shared(&_shared) {}

		// The types (machine id + 1) whose code is ported: 0 (never assigned), GND--, GND-IM, ROM-01 to -48
		static bool ported(uint32_t _type);

		// As VoiceEngine::setSlot: word 0 the trigger (the type, non-zero only on the block the voice is triggered),
		// then the machine's coefficient words
		void setSlot(const uint32_t* _words, int _count);

		// One block, as the dispatcher runs it: init when the trigger brings another type, update on every trigger,
		// render always. Throws std::out_of_range on a type not ported.
		void render(Block& _out);

		const State& state() const { return m_state; }
		// X:S..S+$3F, which the ROM machines keep their filters' history in
		const State& xState() const { return m_x; }

	private:
		State m_state{};
		State m_x{};
		uint32_t m_active = 0;	// Y:$153 + t
		Shared* m_shared;
	};
}
