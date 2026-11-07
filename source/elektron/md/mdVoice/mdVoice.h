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
	// The tables the ROM machines read, which the constructor computes: the decoding as the DSP's, word for word; the
	// two filters ours, near the OS's (no formula gives the OS's, and none of its tables ships). mdVoicePortTest puts the
	// DSP's own filters in them, to compare the render; mdVoiceRomFiltersTest measures what ours change.
	struct RomTables
	{
		std::array<uint32_t, 0x1000> decode{};			// Y:$146000: a 12-bit code's sample
		std::array<uint32_t, 0x620> interpolator{};		// Y:$163-$782: the 7 rows of $E0 phases of the 4x interpolator
		std::array<uint32_t, 7> decimator{};			// X:$244-$24A: half of the last 2:1 decimator's taps

		RomTables();
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

	// The E12 machines' samples, 24-bit words of the voice program in the flash image (spec 11.4: its owner imports them,
	// none is in this code), which Shared::e12 points at, Words of them from Addr: the 63 words of the directory at
	// $103D7B, three per sample (the first sample word's address, the length, 0), then the sample words that follow it,
	// two 12-bit samples each, the first in the high half, up to the last sample's end. A render reads past a sample's
	// end into the next, as the program holds them, and past the last it reads 0, as the zeros after it on the DSP. The
	// caller owns them and changes them between blocks only. Without them (nullptr) the E12 machines play silence.
	struct E12Bank
	{
		static constexpr uint32_t Addr = 0x103d7b, Words = 0x13516e - Addr;
	};

	// The P-I machines' words that no formula gives (spec 11.4: their owner imports them, none is in this code), which
	// mdVoicePiImport.h reads from the flash image, or a caller makes in their place, and Shared::pi points at, Words of them: the ranges below of 24-bit words
	// of the voice program, one after the other, then the OS's table OnePoleAddr, which OsTables takes. Each range holds
	// the machine's 24 or 48 words of pitches, gains and factors, then its excitation; zeros follow on the DSP. BD, SD, MT,
	// ML, RS in that order. The caller owns them and changes them between blocks only.
	// Without them (nullptr) these five play silence, whatever their lines hold. CC, RC, HH and MA read none of them, but
	// P-I-RC's sine leaves its table from the pitch knob at 95 up and reads the UW bank, Shared::bank, there.
	struct PiBank
	{
		struct Range
		{
			uint32_t addr, words;
		};
		static constexpr Range Ranges[5] = {{0x142279, 0x142859 - 0x142279}, {0x142f33, 0x1433b6 - 0x142f33},
			{0x143661, 0x143d1d - 0x143661}, {0x143f38, 0x1449c9 - 0x143f38}, {0x144c9a, 0x144efc - 0x144c9a}};
		static constexpr uint32_t ProgramWords = Ranges[0].words + Ranges[1].words + Ranges[2].words + Ranges[3].words
			+ Ranges[4].words;
		static constexpr uint32_t OnePoleAddr = 0x24da14, OnePoleWords = 256;
		static constexpr uint32_t Words = ProgramWords + OnePoleWords;
		static constexpr uint32_t ZerosAfter = 240;		// after each range, on the DSP
	};

	// The voice DSP's tables the TRX, EFM, E12, P-I and GND-SN machines read. The constructor computes the sine, the boot's
	// own recurrence, and the filters' coefficients, the E12 machines' interpolation and the P-I machines' tables, their
	// formulas. The sampled waveforms are words of the voice program in the flash image (spec 11.4: its owner imports
	// it), which the caller copies in (md::fw::dspWords)
	struct TrxTables
	{
		static constexpr uint32_t SineAddr = 0x148000, BdWaveAAddr = 0x10050c, BdWaveBAddr = 0x10062c,
			SdWaveAddr = 0x100881, FiltersAddr = 0x140000, XtWaveAddr = 0x10190f, RsWaveAddr = 0x101ba0,
			ChWavesAddr = 0x102600, XcWaveAddr = 0x102c0f, E12OffsetsAddr = 0x103c7b, InterpolationAddr = 0x147200,
			PiDampAddr = 0x142949, PiRingAddr = 0x142b49, PiLowCutAddr = 0x145224, PiHighCutAddr = 0x145424;
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
		// X:$103C7B-$103D7A, the E12 machines' two tables of $80 offsets into the poles, as in filters for another Q
		std::array<uint32_t, 0x100> e12Offsets{};
		// Y:$147200-$1475FF, the E12 machines' cubic B-spline: four taps for each of 256 phases
		std::array<uint32_t, 0x400> interpolation{};
		// X:$142949 and $142B49, the P-I machines' pole radii, of a decay of 0.5 ms to 4 s and of 4 us to 31 ms
		std::array<uint32_t, 512> piDamp{};
		std::array<uint32_t, 512> piRing{};
		// X:$145224 and $145424, 128 rows of a two-pole: a high-pass, -30 dB below, from 220 Hz to 1.76 kHz; a low-pass,
		// -15 dB above, from 8.8 kHz to 18.7 kHz
		std::array<uint32_t, 512> piLowCut{};
		std::array<uint32_t, 512> piHighCut{};

		TrxTables();
	};

	// The voice DSP's internal memory that the ported machines use as scratch, X:$000-$1FF and Y:$000-$0FF, one for
	// all the voices as the DSP has one. Every ported voice must render every block, Track 1 to 16 in turn, whatever
	// its family: a ROM render may read a word the last render left (a reversed voice retriggered with the part before
	// the retrigger of odd length keeps that part's last pointer from the last render that wrote it, whatever its
	// voice), an E12 render's tap reads what lies in X where a retrigger left it, and P-I-CC's and P-I-RC's renders start
	// from what Y:$20 holds, an earlier render's whatever its machine, on every block. A caller that skips voices or renders
	// one alone (a preview) loses exactness: on those edges, and on those two machines wherever Y:$20 differs.
	// mdVoicePortTest starts it from what the DSP's own init left in it; a product starts from zeros.
	struct Scratch
	{
		std::array<uint32_t, 0x200> x{};
		std::array<uint32_t, 0x100> y{};
	};

	// What the 16 voices of one engine share, owned by the caller: the ROM machines' tables (nullptr: a ROM machine
	// throws std::logic_error when it renders) and bank (nullptr: none, which plays silence), changed between blocks
	// only, the scratch, the voice DSP's tables (nullptr: a machine that reads them through trx::tables, every TRX, EFM,
	// E12 and P-I one but P-I-MA, and GND-SN, throws std::logic_error when it renders; a word external() would have
	// taken from them reads 0), the E12 machines' samples and the P-I machines' words (nullptr: none, which plays
	// silence), and the P-I machines' delay lines, $600 words a voice from $135600 + $600 t as the DSP has them
	struct Shared
	{
		static constexpr uint32_t LinesAddr = 0x135600, LinesWords = 0x8000;	// past the 16 voices' $6000, for a mask

		const RomTables* tables = nullptr;
		const RomBank* bank = nullptr;
		Scratch scratch;
		const TrxTables* trx = nullptr;
		const uint32_t* e12 = nullptr;
		const uint32_t* pi = nullptr;
		std::array<uint32_t, LinesWords> lines{};
	};

	class Voice
	{
	public:
		static constexpr int kBlockFrames = 32;
		static constexpr int kStateWords = 64;
		using State = std::array<uint32_t, kStateWords>;	// 24-bit words
		using Block = std::array<int32_t, kBlockFrames>;	// 24-bit signed samples

		// _index: the Track, 0 to 15, which the P-I machines' delay lines go by
		Voice(Shared& _shared, const uint32_t _index) : m_shared(&_shared), m_index(_index) {}

		// The types (machine id + 1) whose code is ported: 0 (never assigned), GND-- to GND-IM, TRX-BD, TRX-SD, TRX-XT,
		// TRX-CP, TRX-RS, TRX-CB, TRX-CH, TRX-OH, TRX-CY, TRX-MA, TRX-CL, TRX-XC, TRX-B2, TRX-S2, EFM-BD, EFM-SD,
		// EFM-XT, EFM-CP, EFM-RS, EFM-CB, EFM-HH, EFM-CY, E12-BD to E12-BC, P-I-BD to P-I-HH, ROM-01 to -48
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
		uint32_t m_index;
	};
}
