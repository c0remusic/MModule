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

	// The voice DSP's tables the TRX and EFM machines read. The sine is the boot's own recurrence, which the constructor runs;
	// the sampled waveforms are words of the voice program in the flash image (spec 11.4: its owner imports it), which
	// the caller copies in (md::fw::dspWords). So are the filters' coefficients for now, as the ROM machines' tables
	// are, until their formula is known
	struct TrxTables
	{
		static constexpr uint32_t SineAddr = 0x148000, BdWaveAAddr = 0x10050c, BdWaveBAddr = 0x10062c,
			SdWaveAddr = 0x100881, FiltersAddr = 0x140000, XtWaveAddr = 0x10190f, RsWaveAddr = 0x101ba0,
			ChWavesAddr = 0x102600, XcWaveAddr = 0x102c0f;
		std::array<uint32_t, 0x8000> sine{};		// X:$148000 and Y:$148000, one period
		std::array<uint32_t, 288> bdWaveA{};		// P:$10050C, TRX-BD's attack
		std::array<uint32_t, 288> bdWaveB{};		// P:$10062C, the same for a second packet word
		std::array<uint32_t, 0x1000> sdWave{};		// P:$100881, TRX-SD's, 32 words a block for 128 blocks
		// X:$140000-$1420FF, the two-pole filters': two tables of $80 offsets, then the pole angles (their cosines)
		// from $140100 and the pole radii from $141100, $1000 each
		std::array<uint32_t, 0x2100> filters{};
		std::array<uint32_t, 64> xtWave{};			// P:$10190F, TRX-XT's second and third blocks after a Hit
		std::array<uint32_t, 0x300> rsWave{};		// P:$101BA0, TRX-RS's, 32 words a block for 24 blocks
		std::array<uint32_t, 32> xcWave{};			// P:$102C0F, TRX-XC's third block after a Hit
		// X:$102600-$1028FF, TRX-CH's six waves, each a modulo buffer at its offset from $102600, the words between
		// them not loaded
		struct ChWave
		{
			uint32_t offset, size;
		};
		static constexpr ChWave ChWaves[6] = {{0, 0x72}, {0x100, 0x66}, {0x180, 0x56}, {0x200, 0x50}, {0x280, 0x3a},
			{0x2c0, 0x34}};
		std::array<uint32_t, 0x300> chWaves{};

		TrxTables();
	};

	// The voice DSP's internal memory that the ported machines use as scratch, X:$000-$1FF and Y:$000-$0FF, one for
	// all the voices as the DSP has one. Every ported voice must render every block, Track 1 to 16 in turn, whatever
	// its family: a ROM render may read a word the last render left (a reversed voice retriggered with the part before
	// the retrigger of odd length keeps that part's last pointer from the last render that wrote it, whatever its
	// voice). A caller that skips voices or renders one alone (a preview) loses exactness on that edge only.
	// mdVoicePortTest starts it from what the DSP's own init left in it; a product starts from zeros.
	struct Scratch
	{
		std::array<uint32_t, 0x200> x{};
		std::array<uint32_t, 0x100> y{};
	};

	// What the 16 voices of one engine share, owned by the caller: the ROM machines' tables (nullptr: a ROM machine
	// throws std::logic_error when it renders) and bank (nullptr: none, which plays silence), changed between blocks
	// only, the scratch, and the TRX and EFM machines' tables (nullptr: one throws when it renders)
	struct Shared
	{
		const RomTables* tables = nullptr;
		const RomBank* bank = nullptr;
		Scratch scratch;
		const TrxTables* trx = nullptr;
	};

	class Voice
	{
	public:
		static constexpr int kBlockFrames = 32;
		static constexpr int kStateWords = 64;
		using State = std::array<uint32_t, kStateWords>;	// 24-bit words
		using Block = std::array<int32_t, kBlockFrames>;	// 24-bit signed samples

		explicit Voice(Shared& _shared) : m_shared(&_shared) {}

		// The types (machine id + 1) whose code is ported: 0 (never assigned), GND--, GND-IM, TRX-BD, TRX-SD, TRX-XT,
		// TRX-CP, TRX-RS, TRX-CB, TRX-CH, TRX-OH, TRX-CY, TRX-MA, TRX-CL, TRX-XC, TRX-B2, TRX-S2, EFM-BD, ROM-01 to -48
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
