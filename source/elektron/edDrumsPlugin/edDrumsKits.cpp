#include "edDrumsKits.h"

namespace elektron::drums
{
	KitBank::KitBank(juce::File _folder) : m_folder(std::move(_folder))
	{
		follow();
	}

	juce::File KitBank::file(const int _slot) const
	{
		return m_folder.getChildFile(juce::String(_slot + 1).paddedLeft('0', 2) + ".edkit");
	}

	uint64_t KitBank::follow()
	{
		uint64_t changed = 0;
		for(int i = 0; i < Slots; ++i)
		{
			auto& s = m_slots[static_cast<size_t>(i)];
			const auto f = file(i);
			const int64_t size = f.existsAsFile() ? f.getSize() : -1;
			const auto written = size < 0 ? juce::Time() : f.getLastModificationTime();
			if(size == s.size && written == s.written)
				continue;
			changed |= uint64_t{1} << i;
			s.size = size;
			s.written = written;
			s.name.clear();
			s.valid = false;
			if(size < 0)
				continue;
			// Its size stays the file's, so that a file that is not a Kit is read once, not at every look
			if(const auto kit = read(i))
			{
				s.name = kit->getStringAttribute("name");
				s.valid = true;
			}
		}
		return changed;
	}

	std::unique_ptr<juce::XmlElement> KitBank::read(const int _slot) const
	{
		if(!valid(_slot))
			return {};
		return readFile(file(_slot), Tag);
	}

	std::unique_ptr<juce::XmlElement> KitBank::readFile(const juce::File& _file, const char* _tag)
	{
		juce::FileInputStream in(_file);
		if(!in.openedOk())
			return {};
		juce::GZIPDecompressorInputStream gzip(in);
		// A Kit with 16 samples of the longest is a few MB: past 64 MB the file is not one, whatever it inflates to
		constexpr size_t MostBytes = size_t{64} << 20;
		juce::MemoryBlock text;
		if(gzip.readIntoMemoryBlock(text, static_cast<int>(MostBytes + 1)) > MostBytes)
			return {};
		auto xml = juce::parseXML(text.toString());
		if(!xml || !xml->hasTagName(_tag))
			return {};
		return xml;
	}

	bool KitBank::write(const int _slot, const juce::XmlElement& _kit)
	{
		return valid(_slot) && m_folder.createDirectory() && writeFile(file(_slot), _kit);
	}

	bool KitBank::writeFile(const juce::File& _file, const juce::XmlElement& _kit)
	{
		const juce::TemporaryFile temp(_file);
		{
			juce::FileOutputStream out(temp.getFile());
			if(!out.openedOk())
				return false;
			{
				juce::GZIPCompressorOutputStream gzip(out);
				_kit.writeTo(gzip);
			}
			out.flush();
			if(out.getStatus().failed())
				return false;
		}
		return temp.overwriteTargetFileWithTemporary();
	}

	bool KitBank::clear(const int _slot)
	{
		return valid(_slot) && file(_slot).deleteFile();
	}
}
