#pragma once

#include "mdDrumsSamples.h"

#include <array>
#include <filesystem>
#include <memory>
#include <string>

namespace mdDrums
{
	// The user's Samples kept on this computer (ticket 10 of the Samples map), a file per ROM Machine, ROM-01.smp to
	// ROM-48.smp, each the Sample as MoDule's own Sample message holds it (messages::sample), in a folder of MoDule's
	// data shared by its instances as the Bank and the Library are. What an instance plays is its own copy (Device),
	// taken when it loads a file, a Kit of the Bank or a set; the library changes no sound by itself.
	class SampleLibrary
	{
	public:
		explicit SampleLibrary(std::filesystem::path _folder);

		// Every ROM read again from its file; true when what it holds changed
		bool read();
		// read() when a file's write time moved since this library last read or wrote: true when what it holds changed
		bool follow();

		std::shared_ptr<const Sample> get(int _rom) const;
		// _sample written for ROM _rom (0-47); false when the file could not be written
		bool put(int _rom, std::shared_ptr<const Sample> _sample);
		bool remove(int _rom);

		std::filesystem::path file(int _rom) const;

	private:
		std::filesystem::file_time_type latestWrite() const;

		std::filesystem::path m_folder;
		std::array<std::shared_ptr<const Sample>, Samples::RomCount> m_samples{};
		std::filesystem::file_time_type m_seen{};
	};
}
