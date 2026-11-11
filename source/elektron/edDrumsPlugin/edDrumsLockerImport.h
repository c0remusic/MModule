#pragma once

#include <atomic>
#include <memory>
#include <set>

#include <juce_core/juce_core.h>

#include "edDrumsLibrary.h"
#include "edDrumsSampleLibrary.h"

// What the LOCKER reads of a folder the user adds (ADD FOLDER, a folder dropped on Browse) or imports (IMPORT PACK):
// ticket 20 of .scratch/elektron-drums-v1, spec sections 8 and 11.4, stockage-proposition.md. The folder is walked on a
// thread of its own, sub-folders included. A flash image in it (8 MB, OS 1.63) gives the words a Library takes of it
// (ImageWords), written to the Locker's "Factory.words", which the instrument reads as it loads in place of an image:
// the image is read once and never copied, none of its code goes anywhere; that file is the FACTORY source. Its WAV,
// AIFF and FLAC files are counted, those the Locker already holds by fingerprint apart; an import copies the others
// into a folder of the Locker's, at their place under the folder walked. The original machines' own files (SysEx dumps,
// projects and sounds) are counted, not read. Message thread, but for the walk.
namespace elektron::drums
{
	class LockerImport
	{
	public:
		struct Report
		{
			juce::File folder;
			juce::File copy;			// where an import copied the samples; empty for a folder linked
			juce::String image;			// the flash image read, its file name; empty for none
			int refused = 0;			// files of a flash image's size that hold no OS 1.63
			int audio = 0, known = 0;	// WAV, AIFF and FLAC files; of them, those the Locker holds
			int copied = 0;				// files copied into copy
			int left = 0;				// samples a bound (Bounds) kept out of copy
			int unread = 0;				// the original machines' own files, which the walk does not read
			juce::String error;			// what could not be written; empty if all was
		};

		// What an import copies at most: as many files as the Locker's scan lists (SampleLibrary::MostFiles); a file no
		// larger than the most the Sampler's reader takes of one, 10.9 s of 8 channels of 32-bit floats at 384 kHz
		// (134 MB, readSampleFile), with room for its header; 4 GB in all
		struct Bounds
		{
			int files = SampleLibrary::MostFiles;
			int64_t fileBytes = int64_t{160} << 20;
			int64_t bytes = int64_t{4} << 30;
		};

		static juce::File wordsFile(const juce::File& _dataFolder);
		// The words file before the Locker ("Elektron/Machinedrum.words"), moved to wordsFile where that has none
		static void takeOldWords(const juce::File& _dataFolder);
		static bool writeWords(const juce::File& _file, const ImageWords& _words);
		// False, _words untouched, for no file or one that is not the words written
		static bool readWords(const juce::File& _file, ImageWords& _words);
		// The walk, on the caller's thread, until _quit; _copy empty links, else copies the samples there within _bounds.
		// A file is copied aside (".part") a megabyte at a time, then moved to its name: _quit stops it between two and
		// removes it, so that no file cut short is left
		static Report run(const juce::File& _folder, const std::set<juce::String>& _known, const juce::File& _dataFolder,
			const juce::File& _copy, const std::atomic<bool>& _quit, const Bounds& _bounds);
		static Report run(const juce::File& _folder, const std::set<juce::String>& _known, const juce::File& _dataFolder,
			const juce::File& _copy, const std::atomic<bool>& _quit)
		{
			return run(_folder, _known, _dataFolder, _copy, _quit, Bounds{});
		}

		explicit LockerImport(juce::File _dataFolder);
		// Stops a walk and waits for it to end: a copy stops within a megabyte
		~LockerImport();

		LockerImport(const LockerImport&) = delete;
		LockerImport& operator=(const LockerImport&) = delete;

		// Walks _folder, _known the fingerprints the Locker holds, copying its new samples to _copy unless empty. False
		// while a walk runs
		bool start(const juce::File& _folder, std::set<juce::String> _known, const juce::File& _copy);
		bool running() const { return m_busy.load(std::memory_order_acquire); }
		// A finished walk's report, once
		bool take(Report& _report);

	private:
		const juce::File m_dataFolder;
		juce::CriticalSection m_lock;
		Report m_report;
		std::atomic<bool> m_busy{false}, m_ready{false}, m_quit{false};
		std::unique_ptr<juce::ThreadPool> m_pool;
	};
}
