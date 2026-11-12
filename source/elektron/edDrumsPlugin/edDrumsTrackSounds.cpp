#include "edDrumsTrackSounds.h"

#include <algorithm>

#include "edDrumsKits.h"

namespace elektron::drums
{
	TrackSounds::TrackSounds(juce::File _folder) : m_folder(std::move(_folder))
	{
		follow();
	}

	bool TrackSounds::follow()
	{
		const auto files = m_folder.findChildFiles(juce::File::findFiles, false, juce::String("*") + Extension);
		std::vector<Entry> found, rejected;
		found.reserve(static_cast<size_t>(files.size()));
		bool changed = false;
		for(const auto& f : files)
		{
			Entry e;
			e.file = f;
			e.size = f.getSize();
			e.written = f.getLastModificationTime();
			const auto same = [&](const Entry& _e)
			{
				return _e.file == f && _e.size == e.size && _e.written == e.written;
			};
			// A file seen before and not written since keeps what was read of it, one that was not a sound too
			if(const auto seen = std::find_if(m_entries.begin(), m_entries.end(), same); seen != m_entries.end())
			{
				found.push_back(*seen);
				continue;
			}
			if(std::any_of(m_rejected.begin(), m_rejected.end(), same))
			{
				rejected.push_back(std::move(e));
				continue;
			}
			const auto sound = read(f);
			if(!sound)
			{
				rejected.push_back(std::move(e));
				continue;
			}
			changed = true;
			e.name = sound->getStringAttribute("name", f.getFileNameWithoutExtension());
			e.machine = sound->getStringAttribute("machineName");
			e.machineChoice = sound->getIntAttribute("machine", -1);
			e.version = sound->getIntAttribute("version");
			found.push_back(std::move(e));
		}
		m_rejected = std::move(rejected);
		// A sound gone or a new one read: the entries changed
		if(!changed && found.size() == m_entries.size())
			return false;
		std::sort(found.begin(), found.end(), [](const Entry& _a, const Entry& _b)
		{
			const int c = _a.name.compareNatural(_b.name);
			return c != 0 ? c < 0 : _a.file.getFileName() < _b.file.getFileName();
		});
		m_entries = std::move(found);
		++m_generation;
		return true;
	}

	std::unique_ptr<juce::XmlElement> TrackSounds::read(const juce::File& _file) const
	{
		return KitBank::readFile(_file, Tag);
	}

	juce::File TrackSounds::write(const juce::XmlElement& _sound)
	{
		if(!m_folder.createDirectory())
			return {};
		auto base = juce::File::createLegalFileName(_sound.getStringAttribute("name").trim());
		if(base.isEmpty())
			base = "Sound";
		auto file = m_folder.getChildFile(base + Extension);
		for(int n = 2; file.exists(); ++n)
			file = m_folder.getChildFile(base + " " + juce::String(n) + Extension);
		if(!KitBank::writeFile(file, _sound))
			return {};
		follow();
		return file;
	}
}
