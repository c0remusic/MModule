#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <vector>

#include <juce_core/juce_core.h>

// The LOCKER's sources and their samples (ticket 20 of .scratch/elektron-drums-v1, spec section 8,
// stockage-proposition.md): a list of sources in the data folder's "Locker/sources.txt", a line each, which every
// instance reads, so that a source added in one is seen by the others. A LINKED source is a folder of the user's, read
// where it lies, nothing copied; an IMPORTED one a folder under "Locker/Imported", which the Locker holds (LockerImport
// copies into it), erased with the source. A scan on a thread of its own lists each source's WAV, AIFF and FLAC files,
// sub-folders included, with what their header says, a fingerprint of their content and a type guessed from their
// name, else their sound, which the user may correct. A name the user gives a source stays in the list, one given a
// sample in "Locker/names.txt" by fingerprint: no file is renamed on the disk. Message thread, but for the scan, whose
// result the message thread takes in update(). Before the Locker the list was "Samples/folders.txt" and the types
// "Samples/types.txt": a data folder without the Locker's files takes them once.
namespace elektron::drums
{
	class SampleLibrary
	{
	public:
		enum class Type : uint8_t { Kick, Snare, Hat, Clap, Tom, Cymbal, Perc, Other };
		static constexpr int Types = 8;
		static const char* typeName(Type _type);
		// From a file's name: its words, "bd" or "kick" a Kick and so on, Other where none says
		static Type guessType(const juce::String& _fileName);
		// What gave an entry its type: its name, its sound where the name says none (by how often its first quarter
		// second crosses zero, and its length), or the user, whose choice the Locker's "types.txt" keeps by fingerprint
		// for every instance
		enum class TypeFrom : uint8_t { Name, Sound, User };
		static bool isAudioName(const juce::String& _fileName);

		// A hash of the file's size and first 64 KB, 16 hex digits: the same file wherever it lies. Empty for a file
		// that cannot be read
		static juce::String fingerprintOf(const juce::File& _file);

		enum class Kind : uint8_t { Linked, Imported };
		struct Source
		{
			Kind kind = Kind::Linked;
			juce::File folder;
			juce::String name;			// shown: the folder's name until the user gives another
		};

		// An entry's folder for a sample that is no file of a source: the factory bank's, one a set brings
		static constexpr int FactoryFolder = -1, SetFolder = -2;
		struct Entry
		{
			juce::String name;			// shown: the file's name until the user gives another
			juce::File file;
			int folder = 0;				// its source's index in sources(), else FactoryFolder or SetFolder
			int64_t length = 0;			// sample frames
			double rate = 0;
			int channels = 0;
			int64_t bytes = 0;			// its file's size
			juce::String fingerprint;
			Type type = Type::Other;
			TypeFrom typeFrom = TypeFrom::Name;
		};

		// The scan reads no more than this many files, in case a folder named is a whole disk
		static constexpr int MostFiles = 20000;

		static juce::File lockerFolder(const juce::File& _dataFolder) { return _dataFolder.getChildFile("Locker"); }
		// Where IMPORTED sources lie, a folder each
		static juce::File importedFolder(const juce::File& _dataFolder)
		{
			return lockerFolder(_dataFolder).getChildFile("Imported");
		}

		explicit SampleLibrary(juce::File _dataFolder);
		~SampleLibrary();

		SampleLibrary(const SampleLibrary&) = delete;
		SampleLibrary& operator=(const SampleLibrary&) = delete;

		const std::vector<Source>& sources() const { return m_sources; }
		int indexOf(const juce::File& _folder) const;
		// Adds a source to the list file, read again first so that another instance's stay, named _name or its folder's
		// name; then scans. A folder listed already is scanned again. False for no folder, an IMPORTED one outside
		// importedFolder, or a list file that cannot be written
		bool addSource(Kind _kind, const juce::File& _folder, const juce::String& _name = {});
		// The folders under importedFolder the list lacks that hold a file, added as IMPORTED sources named after their
		// folder: an import whose instance closed before it ended (LockerImport) leaves nothing the Locker cannot reach
		void adoptImported();
		// Takes a source out of the list file, read again first, and its samples out of the entries, then scans. A LINKED
		// source leaves its files as they are; an IMPORTED one's folder is erased. False for no such source or a list
		// file that cannot be written
		bool removeSource(int _index);
		bool renameSource(int _index, const juce::String& _name);
		// The user's name and type for the sample of _fingerprint, kept by fingerprint, the file read again first, and
		// given to its entries. False for a file that cannot be written
		bool renameSample(const juce::String& _fingerprint, const juce::String& _name);
		bool setType(const juce::String& _fingerprint, Type _type);
		// The user's name and type given to an entry that is no file of a source (a factory sample)
		void dress(Entry& _entry) const;

		// Scans the sources if no scan ran yet; follow() reads the files again where they changed and scans then
		void scanOnce();
		void follow();

		// Takes a finished scan's entries: true when they changed. Scanning: a scan runs or waits to be taken
		bool update();
		bool scanning() const { return m_busy.load(std::memory_order_acquire) || m_ready.load(std::memory_order_acquire); }
		// A scan's entries are taken and none runs: what the sources hold is known
		bool known() const { return m_scanned && m_generation > 0 && !scanning(); }

		// By source, then by path; a count that changes with each scan taken
		const std::vector<Entry>& entries() const { return m_entries; }
		uint32_t generation() const { return m_generation; }
		const Entry* find(const juce::String& _fingerprint) const;
		// What a source's files take on the disk, as the last scan read them
		int64_t sourceBytes(int _index) const;

	private:
		juce::File listFile() const;
		std::vector<Source> readList() const;
		bool writeList();
		juce::File typesFile() const;
		juce::File namesFile() const;
		void readUser();
		void applyUser(Entry& _entry) const;
		bool writeUser(const juce::File& _file, const juce::StringArray& _lines, juce::Time& _written);
		void scan();
		void run();

		const juce::File m_dataFolder;
		std::vector<Source> m_sources;
		juce::Time m_listWritten, m_typesWritten, m_namesWritten;
		std::map<juce::String, Type> m_userTypes;		// by fingerprint
		std::map<juce::String, juce::String> m_userNames;
		bool m_scanned = false;
		std::vector<Entry> m_entries;
		uint32_t m_generation = 0;

		// The scan's thread: the folders it was given, what it found, under the lock; whether it runs, whether a
		// result waits, whether another scan was asked for meanwhile
		juce::CriticalSection m_lock;
		juce::Array<juce::File> m_scanFolders;
		std::vector<Entry> m_found;
		std::atomic<bool> m_busy{false}, m_ready{false}, m_again{false}, m_quit{false};
		std::unique_ptr<juce::ThreadPool> m_pool;
	};
}
