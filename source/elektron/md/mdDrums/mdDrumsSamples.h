#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace md::engine
{
	class VoiceEngine;
}

namespace md::fw
{
	struct RomBank;
}

namespace mdDrums
{
	// A user's Sample for a ROM Machine (the Samples map, .scratch/md-drums-samples/): mono 12-bit codes, two a word, the
	// first in the high half, as the voice DSP reads the factory bank's (fw::loadRomBankFromFlash)
	struct Sample
	{
		static constexpr uint32_t NoLoop = 0xffffff;
		static constexpr uint32_t Unity = 1u << 18;	// the rate of a Sample at 44.1 kHz

		std::vector<uint32_t> words;
		uint32_t length = 0;		// samples played, at most twice the words
		uint32_t loop = NoLoop;		// the loop's first sample, or NoLoop
		uint32_t rate = Unity;		// as a fraction of 44.1 kHz
	};

	// The Samples of the 48 ROM Machines, ROM-01 to ROM-48, as the voice DSP holds them (ticket 08 of the Samples map).
	// The user's lie one after another in ROM order above the factory bank, below what the E12 Machines read; a Machine
	// without one plays the factory's (ROM-01 to ROM-32) or nothing (ROM-33 to ROM-48). A change is written from the
	// first ROM it moves on, a share of words at a time between two blocks of the engine (write), as measured by ticket
	// 05: first the directory entries of the Samples that move point at nothing, then their words, then the entries at
	// them; a Hit of such a Machine meanwhile plays nothing. set runs where the engine does not render (the Device's
	// lock); write on the audio thread, without allocating.
	class Samples
	{
	public:
		static constexpr int RomCount = 48;
		static constexpr uint32_t Base = 0x18fc12;	// the first word after the factory bank
		static constexpr uint32_t End = 0x1f85b5;	// the first word the E12 Machines read
		static constexpr size_t WordsPerBlock = 4096;	// 13 us of a block's 726

		explicit Samples(const md::fw::RomBank& _factory);

		// _sample on ROM _rom (0-47), or none to take the factory's back; false, changing nothing, when the user's Samples
		// would not fit together
		bool set(int _rom, std::shared_ptr<const Sample> _sample);
		const std::shared_ptr<const Sample>& get(int _rom) const { return m_samples[static_cast<size_t>(_rom)]; }
		// Words left above the user's Samples
		size_t room() const;
		// Nothing is left to write
		bool written() const { return m_phase == Phase::Idle; }

		// Up to _words of what is left into _voices; the directory entries cost nothing against it
		void write(md::engine::VoiceEngine& _voices, size_t _words = WordsPerBlock);

	private:
		enum class Phase : uint8_t { Idle, Clear, Words, Entries };

		static uint32_t entryAddress(int _rom);
		std::array<uint32_t, 4> entry(int _rom) const;	// the entry ROM _rom ends with: the user's, the factory's or none

		std::array<std::array<uint32_t, 4>, RomCount> m_factory{};
		std::array<std::shared_ptr<const Sample>, RomCount> m_samples{};
		std::array<uint32_t, RomCount> m_address{};	// where each user's Sample lies

		Phase m_phase = Phase::Idle;
		int m_from = 0;			// the first ROM the write rewrites
		int m_rom = 0;			// the ROM whose words are being written
		size_t m_offset = 0;	// its words written so far
	};
}
