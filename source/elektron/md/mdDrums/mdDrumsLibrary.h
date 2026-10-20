#pragma once

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
	// The Library (CONTEXT.md): the User Machines installed on this computer, any number of them, a file each in a
	// folder of MoDule's data shared by its instances as the Bank is: the MDS package as installed, named by its code
	// (TX8-BD.mds); a .syx dump of X.20's put there by hand reads too. What an instance plays is its own copy (Device),
	// at a number of its Kit that MoDule gives (Processor::chooseUserMachine); the Library changes no sound by itself
	// (tickets 07 and 24 of the User Machines map).
	class Library
	{
	public:
		using PackagePtr = std::shared_ptr<const md::mds::Package>;

		explicit Library(std::filesystem::path _folder);

		// Every file read again; true when what the Library holds changed
		bool read();
		// read() when the folder or a file's write time moved since this Library last read or wrote (another instance
		// installed or removed one): true when what it holds changed
		bool follow();

		// Every User Machine installed, by name (category and code), then by file
		const std::vector<PackagePtr>& machines() const { return m_machines; }
		// The one installed with these package bytes, else none
		PackagePtr find(const std::vector<uint8_t>& _package) const;

		// A User Machine's file as X.20 takes it, its .syx dump ($74) or its .mds package: checked as the voice DSP will
		// play it (md::engine::VoiceEngine::checkUserMachine), then written unless the Library holds it already. The
		// package installed or held, or why not.
		struct Install
		{
			PackagePtr package;
			std::string error;
		};
		Install install(const std::vector<uint8_t>& _file);
		bool remove(const PackagePtr& _package);

		const std::filesystem::path& folder() const { return m_folder; }
		// The file a User Machine installed lives in, empty for one the Library does not hold
		std::filesystem::path file(const PackagePtr& _package) const;

	private:
		std::filesystem::file_time_type latestWrite() const;
		void sort();

		std::filesystem::path m_folder;
		std::vector<PackagePtr> m_machines;
		std::vector<std::filesystem::path> m_files;	// m_machines' files, in their order
		std::filesystem::file_time_type m_seen{};
	};
}
