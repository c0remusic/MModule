#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <juce_core/juce_core.h>

// The Track sounds' Library (ticket 19 of .scratch/elektron-drums-v1, spec section 8): one Track's sound, a file each in
// the data folder's "TrackSounds", shared by every instance and every set, loadable on any Track. A file is gzipped XML
// as a Kit's (KitBank::readFile): element ElektronDrumsTrackSound, its format version (a Kit's, which its Machine choice
// is read with), its name and its machine's, then each of the Track's sound parameters' plain value as an attribute
// named by its ID after "tNN_" (Machine, Machine 1-24, Note, the chain, the amp, the LFOs and their targets, the
// sends, the accent: not the Output nor the Chokes, which are the Kit's routing), and the Track's sample, as a Kit
// keeps it. Whoever lists them follows the folder (follow). Message thread: it reads and writes files.
namespace elektron::drums
{
	class TrackSounds
	{
	public:
		static constexpr const char* Tag = "ElektronDrumsTrackSound";
		static constexpr const char* Extension = ".edsound";

		struct Entry
		{
			juce::String name, machine;
			juce::File file;
			juce::Time written;
			int64_t size = 0;
		};

		explicit TrackSounds(juce::File _folder);

		// Reads again the folder where its files changed: true when the entries did. By name
		bool follow();
		const std::vector<Entry>& entries() const { return m_entries; }
		uint32_t generation() const { return m_generation; }

		std::unique_ptr<juce::XmlElement> read(const juce::File& _file) const;
		// A new file for _sound, named after its name, " 2", " 3" added where one has it: the file, or none when it
		// cannot be written
		juce::File write(const juce::XmlElement& _sound);

	private:
		const juce::File m_folder;
		std::vector<Entry> m_entries;
		// Files of the extension that are no sound (a crash's temporary file, another program's): read again only once
		// written again
		std::vector<Entry> m_rejected;
		uint32_t m_generation = 0;
	};
}
