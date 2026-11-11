#pragma once

#include <atomic>
#include <memory>
#include <set>

#include <juce_core/juce_core.h>

#include "edDrumsLibrary.h"

// "Import my Elektron content" (ticket 20 of .scratch/elektron-drums-v1, spec sections 8 and 11.4): a folder the user
// points at, or drops on Browse, walked on a thread of its own, sub-folders included. A Machinedrum UW flash image in it
// (8 MB, OS 1.63) gives the words a Library takes of it (ImageWords), written to the data folder's
// "Elektron/Machinedrum.words", which the instrument reads as it loads in place of an image: the image is read once and
// never copied, none of its code goes anywhere. Its WAV, AIFF and FLAC files (factory packs, Transfer's exports) are
// counted, those the samples' Library already holds by fingerprint apart, for the Library to take the folder. Elektron's
// own files (SysEx dumps, projects and sounds) are counted, not read. Message thread, but for the walk.
namespace elektron::drums
{
	class ElektronImport
	{
	public:
		struct Report
		{
			juce::File folder;
			juce::String image;			// the flash image imported, its file name; empty for none
			int refused = 0;			// files of a flash image's size that hold no Machinedrum OS 1.63
			int audio = 0, known = 0;	// WAV, AIFF and FLAC files; of them, those the samples' Library holds
			int unread = 0;				// Elektron's own files, which the import does not read
			juce::String error;			// why the image's words could not be written; empty if they were, or none
		};

		static juce::File wordsFile(const juce::File& _dataFolder);
		static bool writeWords(const juce::File& _file, const ImageWords& _words);
		// False, _words untouched, for no file or one that is not the words written
		static bool readWords(const juce::File& _file, ImageWords& _words);
		// The walk, on the caller's thread, until _quit
		static Report run(const juce::File& _folder, const std::set<juce::String>& _known, const juce::File& _dataFolder,
			const std::atomic<bool>& _quit);

		explicit ElektronImport(juce::File _dataFolder);
		~ElektronImport();

		ElektronImport(const ElektronImport&) = delete;
		ElektronImport& operator=(const ElektronImport&) = delete;

		// Walks _folder, _known the fingerprints the samples' Library holds. False while an import runs
		bool start(const juce::File& _folder, std::set<juce::String> _known);
		bool running() const { return m_busy.load(std::memory_order_acquire); }
		// A finished import's report, once
		bool take(Report& _report);

	private:
		const juce::File m_dataFolder;
		juce::CriticalSection m_lock;
		Report m_report;
		std::atomic<bool> m_busy{false}, m_ready{false}, m_quit{false};
		std::unique_ptr<juce::ThreadPool> m_pool;
	};
}
