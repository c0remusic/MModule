#include "mdDrumsSampleLibrary.h"

#include "mdDrumsMessages.h"

#include "baseLib/filesystem.h"

#include <cstdio>
#include <fstream>
#include <iterator>

namespace mdDrums
{
	namespace
	{
		std::shared_ptr<const Sample> readSample(const std::filesystem::path& _file, const int _rom)
		{
			std::ifstream in(_file, std::ios::binary);
			if(!in)
				return {};
			const std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
			auto parsed = messages::parseSample(bytes);
			if(!parsed || parsed->rom != _rom)
				return {};
			return parsed->sample;
		}

		bool same(const std::shared_ptr<const Sample>& _a, const std::shared_ptr<const Sample>& _b)
		{
			if(!_a || !_b)
				return !_a && !_b;
			return *_a == *_b;
		}
	}

	SampleLibrary::SampleLibrary(std::filesystem::path _folder) : m_folder(std::move(_folder))
	{
	}

	std::filesystem::path SampleLibrary::file(const int _rom) const
	{
		char name[16];
		std::snprintf(name, sizeof(name), "ROM-%02d.smp", _rom + 1);
		return m_folder / name;
	}

	std::filesystem::file_time_type SampleLibrary::latestWrite() const
	{
		std::filesystem::file_time_type latest{};
		std::error_code error;
		for(int rom = 0; rom < Samples::RomCount; ++rom)
		{
			const auto time = std::filesystem::last_write_time(file(rom), error);
			if(!error && time > latest)
				latest = time;
		}
		// A removed file moves the folder's time
		const auto folder = std::filesystem::last_write_time(m_folder, error);
		return !error && folder > latest ? folder : latest;
	}

	bool SampleLibrary::read()
	{
		bool changed = false;
		for(int rom = 0; rom < Samples::RomCount; ++rom)
		{
			auto sample = readSample(file(rom), rom);
			if(!same(sample, m_samples[static_cast<size_t>(rom)]))
			{
				m_samples[static_cast<size_t>(rom)] = std::move(sample);
				changed = true;
			}
		}
		m_seen = latestWrite();
		return changed;
	}

	bool SampleLibrary::follow()
	{
		return latestWrite() != m_seen && read();
	}

	std::shared_ptr<const Sample> SampleLibrary::get(const int _rom) const
	{
		return _rom >= 0 && _rom < Samples::RomCount ? m_samples[static_cast<size_t>(_rom)] : nullptr;
	}

	bool SampleLibrary::put(const int _rom, std::shared_ptr<const Sample> _sample)
	{
		if(_rom < 0 || _rom >= Samples::RomCount || !_sample)
			return false;
		std::error_code error;
		std::filesystem::create_directories(m_folder, error);
		const auto bytes = messages::sample({static_cast<uint8_t>(_rom), _sample});
		if(!baseLib::filesystem::writeFileAtomic(file(_rom).u8string(), bytes.data(), bytes.size()))
			return false;
		m_samples[static_cast<size_t>(_rom)] = std::move(_sample);
		m_seen = latestWrite();
		return true;
	}

	bool SampleLibrary::remove(const int _rom)
	{
		if(_rom < 0 || _rom >= Samples::RomCount || !m_samples[static_cast<size_t>(_rom)])
			return false;
		std::error_code error;
		std::filesystem::remove(file(_rom), error);
		m_samples[static_cast<size_t>(_rom)].reset();
		m_seen = latestWrite();
		return !error;
	}
}
