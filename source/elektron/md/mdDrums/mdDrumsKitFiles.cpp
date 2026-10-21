#include "mdDrumsKitFiles.h"

#include "HostModel.h"
#include "MdsPackage.h"

#include "mdProtocol/mdmachines.h"

#include "baseLib/filesystem.h"

#include <algorithm>
#include <cstdio>

namespace mdDrums
{
	KitFiles::KitFiles(std::filesystem::path _folder) : m_folder(std::move(_folder))
	{
	}

	KitFiles::~KitFiles()
	{
		std::error_code error;
		for(const auto& [id, device] : m_devices)
			std::filesystem::remove(file(id), error);
	}

	std::filesystem::path KitFiles::defaultFolder()
	{
		std::error_code error;
		return std::filesystem::temp_directory_path(error) / "md-drums-seq";
	}

	std::filesystem::path KitFiles::file(const uint32_t _id) const
	{
		char name[16];
		std::snprintf(name, sizeof(name), "%06x.kit", _id & 0xffffffu);
		return m_folder / name;
	}

	std::string KitFiles::text(const md::automation::sysex::MdKit& _kit, const UserMachines& _userMachines,
		const SampleNames& _samples)
	{
		constexpr auto model = md::MachineModel::Machinedrum;
		// A name is one word in the file
		const auto word = [](std::string _s)
		{
			std::replace(_s.begin(), _s.end(), ' ', '_');
			return _s.empty() ? std::string("-") : _s;
		};
		std::string out;
		out.reserve(3000);
		out.append("md-drums-kit 1\nkit ").append(_kit.displayName()).append("\n");
		for(uint8_t t = 0; t < md::automation::machinedrum::TrackCount; ++t)
		{
			const auto id = _kit.machine(t);
			const auto* package = md::engine::isUserMachine(static_cast<uint8_t>(id))
				? _userMachines[id - md::engine::kFirstUserMachine].get() : nullptr;
			if(package)
			{
				out += "track " + std::to_string(t + 1) + " " + std::to_string(id) + " " + word(package->displayName());
				for(size_t p = 0; p < 8; ++p)
					out += " " + word(p < package->parameters.size() ? package->parameters[p].name : std::string());
				out += "\n";
			}
			else if(const auto* machine = md::machines::find(model, id))
			{
				// A ROM Machine playing a user's Sample: the file's name without its extension
				std::string name(machine->name);
				if(const auto rom = Samples::romOf(static_cast<uint8_t>(id)); rom >= 0)
				{
					const auto& file = _samples[static_cast<size_t>(rom)];
					const auto dot = file.rfind('.');
					if(const auto stem = file.substr(0, dot == 0 ? file.size() : dot); !stem.empty())
						name = word(stem);
				}
				out += "track " + std::to_string(t + 1) + " " + std::to_string(id) + " " + name;
				const auto* names = md::machines::parameterNames(model, id);
				for(size_t p = 0; p < 8; ++p)
				{
					const auto name = names ? (*names)[p] : std::string_view();
					out += " " + (name.empty() ? std::string("-") : std::string(name));
				}
				out += "\n";
			}
			out += "values " + std::to_string(t + 1);
			for(const auto value : _kit.parameters[t])
				out += " " + std::to_string(std::min<int>(value, 127));
			out += "\n";
		}
		return out;
	}

	void KitFiles::hello(const uint32_t _id, const double _nowMs)
	{
		const auto [device, isNew] = m_devices.try_emplace(_id);
		device->second.lastHello = _nowMs;
		if(isNew)
			m_nextStep = std::min(m_nextStep, _nowMs);
	}

	void KitFiles::step(const std::string& _text, const double _nowMs)
	{
		m_nextStep = _nowMs + StepMs;
		for(auto it = m_devices.begin(); it != m_devices.end();)
		{
			auto& device = it->second;
			if(_nowMs - device.lastHello > ForgetMs)
			{
				std::error_code error;
				std::filesystem::remove(file(it->first), error);
				it = m_devices.erase(it);
				continue;
			}
			// A file the device holds open stays as it was until a later step; one another instance deleted (the device
			// moved here from it) is written again
			std::error_code error;
			if((device.written != _text || !std::filesystem::exists(file(it->first), error)) && write(it->first, _text))
				device.written = _text;
			++it;
		}
	}

	bool KitFiles::write(const uint32_t _id, const std::string& _text) const
	{
		std::error_code error;
		std::filesystem::create_directories(m_folder, error);
		const auto* bytes = reinterpret_cast<const uint8_t*>(_text.data());
		return baseLib::filesystem::writeFileAtomic(file(_id).u8string(), bytes, _text.size());
	}
}
