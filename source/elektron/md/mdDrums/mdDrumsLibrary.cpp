#include "mdDrumsLibrary.h"

#include "MdsPackage.h"
#include "VoiceEngine.h"

#include "baseLib/filesystem.h"

#include <cstdio>
#include <fstream>
#include <iterator>

namespace mdDrums
{
	namespace
	{
		std::shared_ptr<const md::mds::Package> readPackage(const std::filesystem::path& _file)
		{
			std::ifstream in(_file, std::ios::binary);
			if(!in)
				return {};
			const std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
			try
			{
				return std::make_shared<const md::mds::Package>(md::mds::parsePackage(bytes));
			}
			catch(const std::exception&)
			{
				return {};
			}
		}

		bool samePackage(const std::shared_ptr<const md::mds::Package>& _a, const std::shared_ptr<const md::mds::Package>& _b)
		{
			if(!_a || !_b)
				return !_a && !_b;
			return _a->bytes == _b->bytes;
		}
	}

	Library::Library(std::filesystem::path _folder) : m_folder(std::move(_folder))
	{
	}

	std::filesystem::path Library::file(const int _index) const
	{
		char name[16];
		std::snprintf(name, sizeof(name), "USR-%02d.mds", _index + 1);
		return m_folder / name;
	}

	std::filesystem::file_time_type Library::latestWrite() const
	{
		std::filesystem::file_time_type latest{};
		std::error_code error;
		for(int i = 0; i < Size; ++i)
		{
			const auto time = std::filesystem::last_write_time(file(i), error);
			if(!error && time > latest)
				latest = time;
		}
		// A removed file moves the folder's time
		const auto folder = std::filesystem::last_write_time(m_folder, error);
		return !error && folder > latest ? folder : latest;
	}

	bool Library::read()
	{
		bool changed = false;
		for(int i = 0; i < Size; ++i)
		{
			auto package = readPackage(file(i));
			if(!samePackage(package, m_machines[static_cast<size_t>(i)]))
			{
				m_machines[static_cast<size_t>(i)] = std::move(package);
				changed = true;
			}
		}
		m_seen = latestWrite();
		return changed;
	}

	bool Library::follow()
	{
		return latestWrite() != m_seen && read();
	}

	std::shared_ptr<const md::mds::Package> Library::get(const int _index) const
	{
		return _index >= 0 && _index < Size ? m_machines[static_cast<size_t>(_index)] : nullptr;
	}

	std::string Library::install(const int _index, const std::vector<uint8_t>& _file)
	{
		if(_index < 0 || _index >= Size)
			return "There is no USR-" + std::to_string(_index + 1);
		std::shared_ptr<const md::mds::Package> package;
		try
		{
			const auto bytes = !_file.empty() && _file.front() == 0xf0 ? md::mds::packageFromSysex(_file) : _file;
			package = std::make_shared<const md::mds::Package>(md::mds::parsePackage(bytes));
			md::engine::VoiceEngine::checkUserMachine(*package);
		}
		catch(const std::exception& _e)
		{
			return _e.what();
		}
		std::error_code error;
		std::filesystem::create_directories(m_folder, error);
		const auto target = file(_index);
		if(!baseLib::filesystem::writeFileAtomic(target.u8string(), package->bytes.data(), package->bytes.size()))
			return "MoDule could not write " + target.u8string();
		m_machines[static_cast<size_t>(_index)] = std::move(package);
		m_seen = latestWrite();
		return {};
	}

	bool Library::remove(const int _index)
	{
		if(_index < 0 || _index >= Size || !m_machines[static_cast<size_t>(_index)])
			return false;
		std::error_code error;
		std::filesystem::remove(file(_index), error);
		m_machines[static_cast<size_t>(_index)].reset();
		m_seen = latestWrite();
		return !error;
	}
}
