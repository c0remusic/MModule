#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <vector>

#include <juce_core/juce_core.h>

// The samples' Library (ticket 20 of .scratch/elektron-drums-v1, spec section 8): the user's own folders, listed in the
// data folder's "Samples/folders.txt", a full path a line, which every instance reads, so that a folder added in one
// is seen by the others; nothing of the samples is copied. A scan on a thread of its own lists each folder's WAV, AIFF
// and FLAC files, sub-folders included, with what their header says, a fingerprint of their content and a type guessed
// from their name, else their sound, which the user may correct. Message thread, but for the scan, whose result the
// message thread takes in update().
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
		// second crosses zero, and its length), or the user, whose choice the data folder's "Samples/types.txt" keeps
		// by fingerprint for every instance
		enum class TypeFrom : uint8_t { Name, Sound, User };
		static bool isAudioName(const juce::String& _fileName);

		// A hash of the file's size and first 64 KB, 16 hex digits: the same file wherever it lies. Empty for a file
		// that cannot be read
		static juce::String fingerprintOf(const juce::File& _file);

		struct Entry
		{
			juce::String name;			// the file's name
			juce::File file;
			int folder = 0;				// its folder's index in folders()
			int64_t length = 0;			// sample frames
			double rate = 0;
			int channels = 0;
			juce::String fingerprint;
			Type type = Type::Other;
			TypeFrom typeFrom = TypeFrom::Name;
		};

		// The scan reads no more than this many files, in case a folder named is a whole disk
		static constexpr int MostFiles = 20000;

		explicit SampleLibrary(juce::File _dataFolder);
		~SampleLibrary();

		SampleLibrary(const SampleLibrary&) = delete;
		SampleLibrary& operator=(const SampleLibrary&) = delete;

		const juce::Array<juce::File>& folders() const { return m_folders; }
		// Adds _folder to the list file, read again first so that another instance's stay; then scans. False for no
		// folder or a list file that cannot be written
		bool addFolder(const juce::File& _folder);
		// Takes _folder out of the list file, read again first, and its samples out of the entries, then scans: nothing
		// on the disk but the list file changes. False for a folder not listed or a list file that cannot be written
		bool removeFolder(const juce::File& _folder);
		// The user's type for the sample of _fingerprint, kept in the types file, read again first, and given to its
		// entries. False for a types file that cannot be written
		bool setType(const juce::String& _fingerprint, Type _type);

		// Scans the folders if no scan ran yet; follow() reads the list again where it changed and scans then
		void scanOnce();
		void follow();

		// Takes a finished scan's entries: true when they changed. Scanning: a scan runs or waits to be taken
		bool update();
		bool scanning() const { return m_busy.load(std::memory_order_acquire) || m_ready.load(std::memory_order_acquire); }

		// By folder, then by path; a count that changes with each scan taken
		const std::vector<Entry>& entries() const { return m_entries; }
		uint32_t generation() const { return m_generation; }
		const Entry* find(const juce::String& _fingerprint) const;

	private:
		juce::File listFile() const;
		juce::Array<juce::File> readList() const;
		bool writeList();
		juce::File typesFile() const;
		void readTypes();
		void applyTypes();
		void scan();
		void run();

		const juce::File m_dataFolder;
		juce::Array<juce::File> m_folders;
		juce::Time m_listWritten, m_typesWritten;
		std::map<juce::String, Type> m_userTypes;	// by fingerprint
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
