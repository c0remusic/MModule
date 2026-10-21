#include "mdDrumsKitFiles.h"

#include "HostModel.h"
#include "MdsPackage.h"

#include "mdProtocol/mdmachines.h"

#include "baseLib/filesystem.h"

#include <algorithm>
#include <cstdio>
#include <mutex>

namespace mdDrums
{
	namespace
	{
		// Which KitFiles of this process heard each device's last hello, by the device's file: only that one writes the
		// file and deletes it, so that a device moved to another MoDule keeps the new one's Kit (MoDule Seq's report of
		// 2026-10-21) whatever the old one does until it forgets the device
		std::mutex g_ownersMutex;
		std::map<std::filesystem::path, const KitFiles*> g_owners;

		bool owns(const std::filesystem::path& _file, const KitFiles* _files)
		{
			std::lock_guard lock(g_ownersMutex);
			const auto it = g_owners.find(_file);
			return it == g_owners.end() || it->second == _files;
		}

		// Deletes the file and forgets its owner when _files owns it
		void release(const std::filesystem::path& _file, const KitFiles* _files)
		{
			std::lock_guard lock(g_ownersMutex);
			const auto it = g_owners.find(_file);
			if(it != g_owners.end() && it->second != _files)
				return;
			if(it != g_owners.end())
				g_owners.erase(it);
			std::error_code error;
			std::filesystem::remove(_file, error);
		}
	}

	KitFiles::KitFiles(std::filesystem::path _folder) : m_folder(std::move(_folder))
	{
	}

	KitFiles::~KitFiles()
	{
		for(const auto& [id, device] : m_devices)
			release(file(id), this);
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
		{
			// Back from another instance, whose Kit its file now holds: written again at the next step
			std::lock_guard lock(g_ownersMutex);
			auto& owner = g_owners[file(_id)];
			if(owner && owner != this)
			{
				device->second.written.clear();
				m_nextStep = std::min(m_nextStep, _nowMs);
			}
			owner = this;
		}
		if(isNew)
			m_nextStep = std::min(m_nextStep, _nowMs);
	}

	void KitFiles::step(const std::string& _text, const double _nowMs)
	{
		m_nextStep = _nowMs + StepMs;
		for(auto it = m_devices.begin(); it != m_devices.end();)
		{
			auto& device = it->second;
			const auto path = file(it->first);
			if(_nowMs - device.lastHello > ForgetMs)
			{
				release(path, this);
				it = m_devices.erase(it);
				continue;
			}
			// A device another instance heard since writes nothing here; a file the device holds open stays as it was
			// until a later step; one deleted from elsewhere (another process, the user) is written again
			std::error_code error;
			if(owns(path, this) && (device.written != _text || !std::filesystem::exists(path, error))
				&& write(it->first, _text))
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
