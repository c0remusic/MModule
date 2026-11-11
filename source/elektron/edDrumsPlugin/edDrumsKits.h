#pragma once

#include <array>
#include <cstdint>
#include <memory>

#include <juce_core/juce_core.h>

// The Kits' Bank (ticket 19 of .scratch/elektron-drums-v1, spec section 8): 64 Slots in the data folder's Kits, a file
// per Slot ("01.edkit" to "64.edkit"), shared by every instance and every set. A file is a Kit, gzipped XML: element
// ElektronDrumsKit, its format version and its name, then each sound parameter's plain value as an attribute named by
// its frozen ID, and each Track's sample in an element of its own, as the plug-in's state keeps them; no file is an
// empty Slot. A write replaces one Slot's file whole, through a temporary file, so that two instances writing two Slots
// keep each other's and a reader never sees half a Kit; whoever shows the Slots follows the files' write times. Message
// thread: it reads and writes files.
namespace elektron::drums
{
	class KitBank
	{
	public:
		static constexpr int Slots = 64;
		static constexpr int Version = 1;
		static constexpr const char* Tag = "ElektronDrumsKit";

		struct Slot
		{
			juce::String name;
			juce::Time written;
			int64_t size = -1;		// the file's, -1 for none: an empty Slot
			bool valid = false;		// the file holds a Kit: one that does not is shown empty, and written over
			bool empty() const { return size < 0 || !valid; }
		};

		explicit KitBank(juce::File _folder);

		// Reads again the Slots whose file changed since this Bank last looked: a bit per Slot that did
		uint64_t follow();

		const Slot& slot(const int _slot) const { return m_slots[static_cast<size_t>(_slot)]; }
		static bool valid(const int _slot) { return _slot >= 0 && _slot < Slots; }

		// The Kit a Slot's file holds, nullptr for an empty Slot or a file that is not a Kit
		std::unique_ptr<juce::XmlElement> read(int _slot) const;
		// Replaces the Slot's file with _kit; empties it. False when the file cannot be written or removed
		bool write(int _slot, const juce::XmlElement& _kit);
		bool clear(int _slot);

	private:
		juce::File file(int _slot) const;

		juce::File m_folder;
		std::array<Slot, Slots> m_slots;
	};
}
