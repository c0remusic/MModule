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
	// What the machines read and the instrument does not make, built before the audio thread sees it and read-only
	// there. Without a flash image the Machinedrum's sampled words are zeros: its ROM, E12 and five of its P-I machines
	// play silence, its TRX machines without their sampled attacks (spec 11.4). Nothing of it is in the repository.
	struct Library
	{
		Library();
		// From the 8 MB Machinedrum UW flash image, OS 1.63: throws md::fw::FirmwareError when it holds no usable OS
		explicit Library(const std::vector<uint8_t>& _flashImage);

		Library(const Library&) = delete;
		Library& operator=(const Library&) = delete;

		// The Machinedrum's flash image found (md::fw::findFlashImage), _folder also looked in. Empty if none
		static std::vector<uint8_t> findFlashImage(const std::filesystem::path& _folder);

		md::voice::RomTables rom;
		md::voice::TrxTables trx;
		md::voice::RomBank bank;
		std::unique_ptr<md::voice::OsTables> os;
		std::vector<uint32_t> directory, samples, e12, pi;	// what bank, Shared::e12 and Shared::pi point at

		// SYN1-8 as ASSIGN MACHINE gives them, by machine id: the flash image's, 64 without it
		std::array<md::voice::Track::Defaults, 256> mdDefaults{};

		// What SP TWINSHOT plays, nullptr: silence. A later ticket imports them; a test makes some
		const syntakt::fast::TwinshotSamples* twinshot = nullptr;
	};
}
