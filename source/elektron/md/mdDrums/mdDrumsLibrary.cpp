#include "mdDrumsLibrary.h"

#include "MdsPackage.h"
#include "VoiceEngine.h"

#include "baseLib/filesystem.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>

namespace mdDrums
{
	namespace
	{
		std::string lower(std::string _text)
		{
			std::transform(_text.begin(), _text.end(), _text.begin(),
				[](const char _c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(_c))); });
			return _text;
		}

		bool isMachineFile(const std::filesystem::path& _file)
		{
			const auto extension = lower(_file.extension().u8string());
			return extension == ".mds" || extension == ".syx";
		}

		// A file as X.20 takes it, as a package the voice DSP plays; throws with what is wrong
		md::mds::Package parse(const std::vector<uint8_t>& _file)
		{
			const auto bytes = !_file.empty() && _file.front() == 0xf0 ? md::mds::packageFromSysex(_file) : _file;
			auto package = md::mds::parsePackage(bytes);
			md::engine::VoiceEngine::checkUserMachine(package);
			return package;
		}

		std::vector<uint8_t> readFile(const std::filesystem::path& _file)
		{
			std::ifstream in(_file, std::ios::binary);
			return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
		}

		// A file name from a package's name: what Windows and macOS take, never empty
		std::string fileName(const md::mds::Package& _package)
		{
			std::string name;
			for(const auto c : _package.displayName())
				name += std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' ? c : '_';
			return name.empty() || name == "-" ? "User Machine" : name;
		}
	}

	Library::Library(std::filesystem::path _folder) : m_folder(std::move(_folder))
	{
	}

	std::filesystem::file_time_type Library::latestWrite() const
	{
		std::error_code error;
		// A file added or removed moves the folder's time, a file rewritten its own
		auto latest = std::filesystem::last_write_time(m_folder, error);
		if(error)
			return {};
		for(std::filesystem::directory_iterator it(m_folder, error), end; !error && it != end; it.increment(error))
		{
			if(!isMachineFile(it->path()))
				continue;
			std::error_code timeError;
			const auto time = it->last_write_time(timeError);
			if(!timeError && time > latest)
				latest = time;
		}
		return latest;
	}

	void Library::sort()
	{
		std::vector<size_t> order(m_machines.size());
		for(size_t i = 0; i < order.size(); ++i)
			order[i] = i;
		std::sort(order.begin(), order.end(), [this](const size_t _a, const size_t _b)
		{
			const auto a = m_machines[_a]->displayName(), b = m_machines[_b]->displayName();
			return a != b ? a < b : m_files[_a].filename() < m_files[_b].filename();
		});
		std::vector<PackagePtr> machines;
		std::vector<std::filesystem::path> files;
		for(const auto i : order)
		{
			machines.push_back(m_machines[i]);
			files.push_back(m_files[i]);
		}
		m_machines = std::move(machines);
		m_files = std::move(files);
	}

	bool Library::read()
	{
		std::vector<PackagePtr> before;
		before.swap(m_machines);
		m_files.clear();
		std::error_code error;
		std::vector<std::filesystem::path> paths;
		for(std::filesystem::directory_iterator it(m_folder, error), end; !error && it != end; it.increment(error))
			if(it->is_regular_file() && isMachineFile(it->path()))
				paths.push_back(it->path());
		// The same package as a .syx and as an .mds is one User Machine; a file the voice DSP cannot play, none
		std::sort(paths.begin(), paths.end());
		for(const auto& path : paths)
		{
			try
			{
				auto package = std::make_shared<const md::mds::Package>(parse(readFile(path)));
				if(find(package->bytes))
					continue;
				m_machines.push_back(std::move(package));
				m_files.push_back(path);
			}
			catch(const std::exception&)
			{
			}
		}
		sort();
		m_seen = latestWrite();
		return !std::equal(before.begin(), before.end(), m_machines.begin(), m_machines.end(),
			[](const PackagePtr& _a, const PackagePtr& _b) { return _a->bytes == _b->bytes; });
	}

	bool Library::follow()
	{
		return latestWrite() != m_seen && read();
	}

	Library::PackagePtr Library::find(const std::vector<uint8_t>& _package) const
	{
		for(const auto& machine : m_machines)
			if(machine->bytes == _package)
				return machine;
		return {};
	}

	std::filesystem::path Library::file(const PackagePtr& _package) const
	{
		for(size_t i = 0; _package && i < m_machines.size(); ++i)
			if(m_machines[i]->bytes == _package->bytes)
				return m_files[i];
		return {};
	}

	Library::Install Library::install(const std::vector<uint8_t>& _file)
	{
		PackagePtr package;
		try
		{
			package = std::make_shared<const md::mds::Package>(parse(_file));
		}
		catch(const std::exception& _e)
		{
			return {{}, _e.what()};
		}
		if(auto held = find(package->bytes))
			return {std::move(held), {}};

		std::error_code error;
		std::filesystem::create_directories(m_folder, error);
		// Its name, or the first of "TX8-BD 2", "TX8-BD 3"... that no file takes: another package of the same name
		const auto name = fileName(*package);
		auto target = m_folder / std::filesystem::u8path(name + ".mds");
		for(int n = 2; std::filesystem::exists(target, error); ++n)
			target = m_folder / std::filesystem::u8path(name + " " + std::to_string(n) + ".mds");
		if(!baseLib::filesystem::writeFileAtomic(target.u8string(), package->bytes.data(), package->bytes.size()))
			return {{}, "MoDule could not write " + target.u8string()};
		m_machines.push_back(package);
		m_files.push_back(target);
		sort();
		m_seen = latestWrite();
		return {std::move(package), {}};
	}

	bool Library::remove(const PackagePtr& _package)
	{
		for(size_t i = 0; _package && i < m_machines.size(); ++i)
		{
			if(m_machines[i]->bytes != _package->bytes)
				continue;
			std::error_code error;
			std::filesystem::remove(m_files[i], error);
			m_machines.erase(m_machines.begin() + static_cast<std::ptrdiff_t>(i));
			m_files.erase(m_files.begin() + static_cast<std::ptrdiff_t>(i));
			m_seen = latestWrite();
			return !error;
		}
		return false;
	}
}
