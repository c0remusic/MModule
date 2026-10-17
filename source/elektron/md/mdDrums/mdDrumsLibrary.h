#pragma once

#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace md::mds
{
	struct Package;
}

namespace mdDrums
{
	// The Library (CONTEXT.md): the User Machines installed on this computer, a file per number, USR-01.mds to
	// USR-16.mds, each the MDS package as installed, in a folder of MoDule's data shared by its instances as the Bank is.
	// What an instance plays is its own copy (Device), taken when it installs, loads a Kit of the Bank or a set, or
	// chooses that Machine; the Library changes no sound by itself (ticket 07 of the User Machines map).
	class Library
	{
	public:
		static constexpr int Size = 16;

		explicit Library(std::filesystem::path _folder);

		// Every number read again from its file; true when what it holds changed
		bool read();
		// read() when a file's write time moved since this Library last read or wrote (another instance installed or
		// removed one): true when what it holds changed
		bool follow();

		std::shared_ptr<const md::mds::Package> get(int _index) const;

		// A User Machine's file as X.20 takes it, its .syx dump ($74) or its .mds package, at _index (0-15): checked as the
		// voice DSP will play it (md::engine::VoiceEngine::checkUserMachine), then written. Empty, or why not.
		std::string install(int _index, const std::vector<uint8_t>& _file);
		bool remove(int _index);

		const std::filesystem::path& folder() const { return m_folder; }
		std::filesystem::path file(int _index) const;

	private:
		std::filesystem::file_time_type latestWrite() const;

		std::filesystem::path m_folder;
		std::array<std::shared_ptr<const md::mds::Package>, Size> m_machines{};
		std::filesystem::file_time_type m_seen{};
	};
}
