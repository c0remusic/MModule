// The samples' Library and "Import my Elektron content" (ticket 20 of .scratch/elektron-drums-v1) on files this test
// writes in a temporary folder, never the user's: types by name and by sound, a type the user sets kept across a rescan
// and a new Library, a folder removed leaving no entry and every file on the disk as it was, an import that counts what
// it finds and copies nothing, and the words file read back as written. With GEARMULATOR_MD_FIRMWARE_BIN, the image is
// copied into a temporary folder, imported, and the Library made from the words written is the image's.

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <random>
#include <set>
#include <stdexcept>
#include <string>

#include "edDrumsElektronImport.h"
#include "edDrumsSampleLibrary.h"

namespace
{
	using namespace elektron::drums;
	using Type = SampleLibrary::Type;

	void require(const bool _condition, const std::string& _message)
	{
		if(!_condition)
			throw std::runtime_error(_message);
	}

	// A 16-bit mono WAV of _seconds: a decaying sine of _hz, or white noise for _hz 0
	void writeWav(const juce::File& _file, const double _seconds, const float _hz)
	{
		constexpr double rate = 44100;
		const int frames = static_cast<int>(_seconds * rate);
		juce::AudioBuffer<float> wave(1, frames);
		std::mt19937 random(7);
		std::uniform_real_distribution<float> noise(-1, 1);
		for(int n = 0; n < frames; ++n)
		{
			const float env = 0.7f * std::exp(-static_cast<float>(n) / static_cast<float>(frames / 3));
			wave.setSample(0, n, env * (_hz > 0 ? std::sin(2 * 3.14159265f * _hz * static_cast<float>(n / rate))
				: noise(random)));
		}
		_file.deleteFile();
		juce::WavAudioFormat wav;
		std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(new juce::FileOutputStream(_file), rate, 1, 16,
			{}, 0));
		require(writer != nullptr && writer->writeFromAudioSampleBuffer(wave, 0, frames), "the sound written");
	}

	void waitForScan(SampleLibrary& _library)
	{
		const auto until = juce::Time::getMillisecondCounter() + 20000;
		while(!_library.update())
		{
			require(juce::Time::getMillisecondCounter() < until, "the scan never ended");
			juce::Thread::sleep(5);
		}
	}

	const SampleLibrary::Entry& entry(const SampleLibrary& _library, const juce::String& _name)
	{
		for(const auto& e : _library.entries())
			if(e.name == _name)
				return e;
		throw std::runtime_error(_name.toStdString() + " not in the Library");
	}

	// Every file under _folder: its path, size and time written
	juce::StringArray filesOf(const juce::File& _folder)
	{
		juce::StringArray files;
		for(const auto& f : juce::RangedDirectoryIterator(_folder, true, "*", juce::File::findFiles))
			files.add(f.getFile().getFullPathName() + " " + juce::String(f.getFileSize()) + " "
				+ juce::String(f.getModificationTime().toMilliseconds()));
		files.sort(false);
		return files;
	}

	// Types by name, else by sound; one set by the user kept across a rescan and a new Library; a folder removed
	void library(const juce::File& _root)
	{
		const auto data = _root.getChildFile("data"), mine = _root.getChildFile("mine"), other = _root.getChildFile("other");
		require(mine.createDirectory().wasOk() && other.createDirectory().wasOk(), "the folders");
		writeWav(mine.getChildFile("kick_01.wav"), 0.3, 1000);	// its name says Kick, whatever it sounds
		writeWav(mine.getChildFile("one.wav"), 0.4, 60);
		writeWav(mine.getChildFile("two.wav"), 0.4, 300);
		writeWav(mine.getChildFile("three.wav"), 0.15, 0);
		writeWav(mine.getChildFile("four.wav"), 1.5, 0);
		writeWav(other.getChildFile("snare.wav"), 0.2, 200);

		SampleLibrary lib(data);
		require(lib.addFolder(mine) && lib.addFolder(other), "the folders added");
		waitForScan(lib);
		while(lib.scanning())
			waitForScan(lib);
		require(lib.entries().size() == 6, "the scan found " + std::to_string(lib.entries().size()) + ", not 6");
		struct Expected
		{
			const char* name;
			Type type;
			SampleLibrary::TypeFrom from;
		};
		using From = SampleLibrary::TypeFrom;
		const Expected expected[]{{"kick_01.wav", Type::Kick, From::Name}, {"one.wav", Type::Kick, From::Sound},
			{"two.wav", Type::Tom, From::Sound}, {"three.wav", Type::Hat, From::Sound},
			{"four.wav", Type::Cymbal, From::Sound}, {"snare.wav", Type::Snare, From::Name}};
		for(const auto& x : expected)
		{
			const auto& e = entry(lib, x.name);
			require(e.type == x.type && e.typeFrom == x.from, std::string(x.name) + " is " + SampleLibrary::typeName(e.type)
				+ " from " + std::to_string(static_cast<int>(e.typeFrom)));
		}
		std::printf("PASS: types by name, and by sound where the name says none (60 Hz kick, 300 Hz tom, short noise "
			"hat, long noise cymbal)\n");

		// The user's type: at once, after a rescan, in a new Library on the same data folder
		const auto fingerprint = entry(lib, "two.wav").fingerprint;
		require(lib.setType(fingerprint, Type::Perc), "the type not kept");
		require(entry(lib, "two.wav").type == Type::Perc && entry(lib, "two.wav").typeFrom == From::User, "not set");
		lib.addFolder(mine);	// listed already: scans again
		waitForScan(lib);
		require(entry(lib, "two.wav").type == Type::Perc && entry(lib, "two.wav").typeFrom == From::User,
			"lost by a rescan");
		{
			SampleLibrary again(data);
			again.scanOnce();
			waitForScan(again);
			require(entry(again, "two.wav").type == Type::Perc && entry(again, "two.wav").typeFrom == From::User,
				"lost by a new Library");
			require(entry(again, "one.wav").typeFrom == From::Sound, "another sample's type changed");
		}
		std::printf("PASS: a type set by the user kept across a rescan and a new Library\n");

		// A folder removed: none of its entries left, the other's kept with its new index, no file changed on the disk
		const auto before = filesOf(mine);
		require(lib.removeFolder(mine), "the folder not removed");
		const auto check = [&](const SampleLibrary& _lib, const char* _when)
		{
			require(_lib.folders().size() == 1 && _lib.folders()[0] == other, std::string("the list ") + _when);
			require(_lib.entries().size() == 1 && _lib.entries()[0].name == "snare.wav" && _lib.entries()[0].folder == 0,
				std::string("the entries ") + _when + ": " + std::to_string(_lib.entries().size()));
		};
		check(lib, "at once");
		waitForScan(lib);
		check(lib, "after the scan");
		{
			SampleLibrary again(data);
			again.scanOnce();
			waitForScan(again);
			check(again, "in a new Library");
		}
		require(filesOf(mine) == before && mine.isDirectory(), "a file of the folder removed changed");
		require(!lib.removeFolder(mine), "a folder not listed removed");
		std::printf("PASS: a folder removed leaves no entry, its %d files as they were\n", before.size());
	}

	// The import of a folder with a sample the Library has, a new one, Elektron files and a file of an image's size
	// that is none: counted, the folder's files untouched, nothing written to the data folder
	void import(const juce::File& _root)
	{
		const auto data = _root.getChildFile("data2"), mine = _root.getChildFile("mine2"),
			content = _root.getChildFile("content");
		require(mine.createDirectory().wasOk() && content.getChildFile("pack").createDirectory().wasOk(), "the folders");
		writeWav(mine.getChildFile("kick.wav"), 0.3, 60);
		require(mine.getChildFile("kick.wav").copyFileTo(content.getChildFile("kick copy.wav")), "the copy");
		writeWav(content.getChildFile("pack").getChildFile("snare 2.wav"), 0.2, 200);
		content.getChildFile("dump.syx").replaceWithData("\xf0\x00\x20\x3c\xf7", 5);
		content.getChildFile("pack").getChildFile("song.dtprj").replaceWithText("not read");
		{
			juce::FileOutputStream out(content.getChildFile("flash.bin"));
			std::vector<char> zeros(1 << 20, 0);
			for(int i = 0; i < 8; ++i)
				out.write(zeros.data(), zeros.size());
		}
		SampleLibrary lib(data);
		require(lib.addFolder(mine), "the folder added");
		waitForScan(lib);
		std::set<juce::String> known;
		for(const auto& e : lib.entries())
			known.insert(e.fingerprint);

		const auto before = filesOf(content);
		ElektronImport importer(data);
		require(importer.start(content, known), "the import not started");
		ElektronImport::Report r;
		const auto until = juce::Time::getMillisecondCounter() + 20000;
		while(!importer.take(r))
		{
			require(juce::Time::getMillisecondCounter() < until, "the import never ended");
			juce::Thread::sleep(5);
		}
		require(r.folder == content && r.audio == 2 && r.known == 1 && r.unread == 2 && r.refused == 1
			&& r.image.isEmpty() && r.error.isEmpty(), "the report: " + std::to_string(r.audio) + " samples, "
			+ std::to_string(r.known) + " known, " + std::to_string(r.unread) + " unread, " + std::to_string(r.refused)
			+ " refused, image " + r.image.toStdString());
		require(filesOf(content) == before, "the import changed a file of its folder");
		require(!ElektronImport::wordsFile(data).exists(), "words written without an image");
		for(const auto& f : juce::RangedDirectoryIterator(data, true, "*", juce::File::findFiles))
			require(f.getFileSize() < (1 << 20), "a big file in the data folder: " + f.getFile().getFileName().toStdString());
		std::printf("PASS: the import counts 2 samples (1 known), 2 Elektron files not read, 1 file refused as an "
			"image; nothing written\n");
	}

	// The words file as written, its sizes checked, a file cut short refused
	void words(const juce::File& _root)
	{
		ImageWords w;
		{
			const Library house;
			size_t trx = 0;
			for(const size_t n : {house.trx.bdWaveA.size(), house.trx.bdWaveB.size(), house.trx.sdWave.size(),
				house.trx.xtWave.size(), house.trx.xcWave.size(), house.trx.rsWave.size(), house.trx.chWaves.size()})
				trx += n;
			w.trx.resize(trx);
		}
		w.pi.resize(md::voice::PiBank::Words);
		w.e12.resize(md::voice::E12Bank::Words);
		w.directory.resize(md::voice::RomBank::DirectoryWords);
		w.samples.resize(1000);
		uint32_t k = 1;
		for(auto* v : {&w.trx, &w.pi, &w.e12, &w.samples})
			for(auto& x : *v)
				x = (k++ * 2654435761u) & 0xffffff;
		w.directory[1] = 1000;		// ROM-01 holds a sample
		const auto file = ElektronImport::wordsFile(_root.getChildFile("data3"));
		require(ElektronImport::writeWords(file, w), "the words not written");
		ImageWords back;
		require(ElektronImport::readWords(file, back) && back.trx == w.trx && back.pi == w.pi && back.e12 == w.e12
			&& back.directory == w.directory && back.samples == w.samples, "the words not read back as written");
		const Library lib(back);
		require(lib.samples == w.samples && lib.trx.bdWaveA[0] == w.trx[0] && lib.trx.chWaves.back() == w.trx.back()
			&& lib.mdData[128] == MachineData::Whole && lib.mdData[129] == MachineData::None, "the Library of the words");
		auto wrong = w;
		wrong.pi.pop_back();
		bool thrown = false;
		try
		{
			const Library bad(wrong);
		}
		catch(const std::runtime_error&)
		{
			thrown = true;
		}
		require(thrown, "words of the wrong size taken");
		juce::MemoryBlock bytes;
		require(file.loadFileAsData(bytes), "the words read");
		file.replaceWithData(bytes.getData(), bytes.getSize() - 4);
		require(!ElektronImport::readWords(file, back), "a file cut short read");
		std::printf("PASS: the words file read back as written, %d bytes; wrong sizes and a file cut short refused\n",
			static_cast<int>(bytes.getSize()));
	}

	// The user's image, copied into a temporary folder: its words imported, the Library made from them the image's
	void image(const juce::File& _root)
	{
		const auto* path = std::getenv("GEARMULATOR_MD_FIRMWARE_BIN");
		if(!path || !*path || !juce::File(juce::String::fromUTF8(path)).existsAsFile())
		{
			std::printf("SKIP: no GEARMULATOR_MD_FIRMWARE_BIN, no image imported\n");
			return;
		}
		const juce::File source(juce::String::fromUTF8(path));
		const auto data = _root.getChildFile("data4"), content = _root.getChildFile("content4");
		require(content.getChildFile("MD").createDirectory().wasOk(), "the folder");
		require(source.copyFileTo(content.getChildFile("MD").getChildFile("my machinedrum.bin")), "the image copied");
		const std::atomic<bool> quit{false};
		const auto r = ElektronImport::run(content, {}, data, quit);
		require(r.image == "my machinedrum.bin" && r.refused == 0 && r.error.isEmpty(), "the image not imported: "
			+ r.error.toStdString());
		const auto file = ElektronImport::wordsFile(data);
		ImageWords w;
		require(ElektronImport::readWords(file, w), "the words not read");
		juce::MemoryBlock bytes;
		source.loadFileAsData(bytes);
		const auto* b = static_cast<const uint8_t*>(bytes.getData());
		const Library fromImage(std::vector<uint8_t>(b, b + bytes.getSize()));
		const Library fromWords(w);
		require(fromWords.samples == fromImage.samples && fromWords.directory == fromImage.directory
			&& fromWords.pi == fromImage.pi && fromWords.e12 == fromImage.e12 && fromWords.trx.sdWave == fromImage.trx.sdWave
			&& fromWords.trx.chWaves == fromImage.trx.chWaves && fromWords.mdData == fromImage.mdData,
			"the words' Library is not the image's");
		require(file.getSize() < source.getSize(), "the words are as big as the image");
		for(const auto& f : juce::RangedDirectoryIterator(data, true, "*", juce::File::findFiles))
			require(f.getFileSize() != source.getSize(), "a file of the image's size in the data folder");
		std::printf("PASS: the image's words imported, %d KB, the Library made from them the image's; no copy of the "
			"image in the data folder\n", static_cast<int>(file.getSize() / 1024));
	}
}

int main()
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("edDrumsSampleLibraryTest");
	root.deleteRecursively();
	int failed = 0;
	for(const auto& [name, test] : std::initializer_list<std::pair<const char*, void (*)(const juce::File&)>>{
		{"library", library}, {"import", import}, {"words", words}, {"image", image}})
	{
		try
		{
			test(root);
		}
		catch(const std::exception& _e)
		{
			std::printf("FAIL %s: %s\n", name, _e.what());
			++failed;
		}
	}
	root.deleteRecursively();
	std::printf(failed ? "edDrumsSampleLibraryTest: %d FAILED\n" : "edDrumsSampleLibraryTest: PASS\n", failed);
	return failed ? 1 : 0;
}
