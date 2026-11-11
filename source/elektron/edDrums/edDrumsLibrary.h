#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

#include "mdVoice.h"
#include "mdVoiceCoefficients.h"
#include "mdVoiceTrack.h"
#include "stFastSpTwinshot.h"

namespace elektron::drums
{
	// What a machine has of the data it reads: all of it, none so that it plays silence, however it is played, or part
	// so that it plays without its sampled words. House: all of it, part of it made here, so that it sounds its own way
	enum class MachineData : uint8_t { Whole, House, None };

	// What the machines read, built before the audio thread sees it and read-only there. Without a flash image the
	// Machinedrum's sampled words are those made here (edDrumsHouseWaves) or zeros (spec 11.4): mdData says what that
	// leaves of each machine. Nothing of the image is in the repository. P-I-RC reads the UW bank from PTCH 95 up, but
	// what it reads there reaches no output: it stays Whole. Data made here goes in the same fields, which the voices
	// read whoever filled them, and marks mdData, machine by machine for trx's waves, E12 and the ROM bank. Not for pi:
	// Voice::render silences all five P-I drums on one pointer, and OsTables takes its one-pole table from the same
	// words, so that they come as one.
	// What a Library takes of the flash image, the sampled words alone (spec 11.4), which the owner's import (ticket 20)
	// writes in the data folder so that the image is read once: TRX's waves (bdWaveA, bdWaveB, sdWave, xtWave, xcWave,
	// rsWave, chWaves, one after the other), the P-I words, the E12 words, the UW bank's directory and sample words
	struct ImageWords
	{
		std::vector<uint32_t> trx, pi, e12, directory, samples;
	};

	struct Library
	{
		Library();
		// From the 8 MB Machinedrum UW flash image, OS 1.63: throws md::fw::FirmwareError when it holds no usable OS
		explicit Library(const std::vector<uint8_t>& _flashImage);
		// From the words imported, the same Library as from their image: throws std::runtime_error for words of the
		// wrong sizes
		explicit Library(const ImageWords& _words);
		// The words of the image, which Library(_flashImage) is made from: throws md::fw::FirmwareError as it does
		static ImageWords importImage(const std::vector<uint8_t>& _flashImage);

		Library(const Library&) = delete;
		Library& operator=(const Library&) = delete;

		// The Machinedrum's flash image found (md::fw::findFlashImage), _folder also looked in. Empty if none
		static std::vector<uint8_t> findFlashImage(const std::filesystem::path& _folder);

		md::voice::RomTables rom;
		md::voice::TrxTables trx;
		md::voice::RomBank bank;
		std::unique_ptr<md::voice::OsTables> os;
		std::vector<uint32_t> directory, samples, e12, pi;	// what bank, Shared::e12 and Shared::pi point at

		// By machine id; dataOf reads it
		std::array<MachineData, 256> mdData{};

	private:
		// ROM-01 to -32 and -33 to -48, ids 128 + their directory entry: None where the directory holds no sample
		void markRoms();

	public:

		// What SP TWINSHOT plays, nullptr: silence. A later ticket imports them; a test makes some
		const syntakt::fast::TwinshotSamples* twinshot = nullptr;
	};
}
