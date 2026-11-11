// The LOCKER's sources and what it reads of a folder (ticket 20 of .scratch/elektron-drums-v1,
// stockage-proposition.md) on files this test writes in a temporary folder, never the user's: types by name and by
// sound; a type and a name the user gives kept across a rescan and a second instance; a LINKED source removed leaving
// no entry and every file on the disk as it was; a folder walked that counts what it finds and copies nothing, and one
// imported whose WAV, AIFF and FLAC are copied into the Locker, counted, and erased with the source alone; the words
// file read back as written; the list, types and words of before the Locker taken. With GEARMULATOR_MD_FIRMWARE_BIN,
// the image is copied into a temporary folder and walked: its words are written and nothing else of it, and an
// instrument loaded on that data folder has FACTORY's 32 slots.

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

#include "edDrumsLockerImport.h"
#include "edDrumsProcessor.h"
#include "edDrumsSampleLibrary.h"

namespace
{
	using namespace elektron::drums;
	using Type = SampleLibrary::Type;
	using Kind = SampleLibrary::Kind;

	void require(const bool _condition, const std::string& _message)
	{
		if(!_condition)
			throw std::runtime_error(_message);
	}

	// A 16-bit mono file of _seconds in _format (WAV by default): a decaying sine of _hz, or white noise for _hz 0
	void writeWav(const juce::File& _file, const double _seconds, const float _hz, juce::AudioFormat* _format = nullptr)
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
		auto& format = _format ? *_format : static_cast<juce::AudioFormat&>(wav);
		std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(new juce::FileOutputStream(_file), rate, 1,
			16, {}, 0));
		require(writer != nullptr && writer->writeFromAudioSampleBuffer(wave, 0, frames), "the sound written");
	}

	void waitForScan(SampleLibrary& _library)
	{
		const auto until = juce::Time::getMillisecondCounter() + 20000;
		while(!_library.update() || _library.scanning())
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
		throw std::runtime_error(_name.toStdString() + " not in the Locker");
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

	LockerImport::Report walk(LockerImport& _import, const juce::File& _folder, const std::set<juce::String>& _known,
		const juce::File& _copy)
	{
		require(_import.start(_folder, _known, _copy), "the walk not started");
		LockerImport::Report r;
		const auto until = juce::Time::getMillisecondCounter() + 20000;
		while(!_import.take(r))
		{
			require(juce::Time::getMillisecondCounter() < until, "the walk never ended");
			juce::Thread::sleep(5);
		}
		return r;
	}

	// Types by name, else by sound; a type and a name set by the user kept across a rescan and a second instance; a
	// LINKED source renamed, then removed
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
		require(lib.addSource(Kind::Linked, mine) && lib.addSource(Kind::Linked, other), "the sources added");
		require(!lib.addSource(Kind::Imported, other), "a folder outside the Locker added as IMPORTED");
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

		// The user's type and name: at once, after a rescan, in a second instance on the same data folder
		const auto fingerprint = entry(lib, "two.wav").fingerprint;
		require(lib.setType(fingerprint, Type::Perc), "the type not kept");
		require(lib.renameSample(fingerprint, "Low tom"), "the name not kept");
		require(lib.renameSource(0, "My kit"), "the source's name not kept");
		require(entry(lib, "Low tom").type == Type::Perc && entry(lib, "Low tom").typeFrom == From::User, "not set");
		lib.addSource(Kind::Linked, mine);	// listed already: scans again
		waitForScan(lib);
		require(entry(lib, "Low tom").type == Type::Perc && entry(lib, "Low tom").file.getFileName() == "two.wav",
			"lost by a rescan");
		require(mine.getChildFile("two.wav").existsAsFile(), "the file renamed on the disk");
		{
			SampleLibrary again(data);
			again.scanOnce();
			waitForScan(again);
			require(entry(again, "Low tom").type == Type::Perc && entry(again, "Low tom").typeFrom == From::User,
				"lost by a second instance");
			require(again.sources().size() == 2 && again.sources()[0].name == "My kit"
				&& again.sources()[0].kind == Kind::Linked, "the source's name lost by a second instance");
			require(entry(again, "one.wav").typeFrom == From::Sound, "another sample's type changed");
		}
		std::printf("PASS: a type, a sample's name and a source's name kept across a rescan and a second instance, no "
			"file renamed\n");

		// A LINKED source removed: none of its entries left, the other's kept with its new index, no file changed
		const auto before = filesOf(mine);
		require(lib.removeSource(lib.indexOf(mine)), "the source not removed");
		const auto check = [&](const SampleLibrary& _lib, const char* _when)
		{
			require(_lib.sources().size() == 1 && _lib.sources()[0].folder == other, std::string("the list ") + _when);
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
			check(again, "in a second instance");
		}
		require(filesOf(mine) == before && mine.isDirectory(), "a file of the source removed changed");
		require(!lib.removeSource(5), "a source not listed removed");
		std::printf("PASS: a LINKED source removed leaves no entry, its %d files as they were\n", before.size());
	}

	// A folder linked with a sample the Locker has, a new one, the original machines' files and a file of an image's
	// size that is none: counted, the folder's files untouched, nothing written to the data folder
	void walkLinked(const juce::File& _root)
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
		require(lib.addSource(Kind::Linked, mine), "the source added");
		waitForScan(lib);
		std::set<juce::String> known;
		for(const auto& e : lib.entries())
			known.insert(e.fingerprint);

		const auto before = filesOf(content);
		LockerImport importer(data);
		const auto r = walk(importer, content, known, {});
		require(r.folder == content && r.audio == 2 && r.known == 1 && r.unread == 2 && r.refused == 1 && r.copied == 0
			&& r.image.isEmpty() && r.error.isEmpty(), "the report: " + std::to_string(r.audio) + " samples, "
			+ std::to_string(r.known) + " known, " + std::to_string(r.unread) + " unread, " + std::to_string(r.refused)
			+ " refused, image " + r.image.toStdString());
		require(filesOf(content) == before, "the walk changed a file of its folder");
		require(!LockerImport::wordsFile(data).exists(), "words written without an image");
		require(!SampleLibrary::importedFolder(data).exists(), "a folder linked copied");
		for(const auto& f : juce::RangedDirectoryIterator(data, true, "*", juce::File::findFiles))
			require(f.getFileSize() < (1 << 20), "a big file in the data folder: " + f.getFile().getFileName().toStdString());
		std::printf("PASS: a folder linked: 2 samples (1 known), 2 files not read, 1 file refused as an image; nothing "
			"written\n");
	}

	// IMPORT PACK: the pack's WAV, AIFF and FLAC copied into Locker/Imported/<name>/ at their place, the one the Locker
	// had left; the source IMPORTED counts their space; removed, its copy goes and the pack stays
	void walkImported(const juce::File& _root)
	{
		const auto data = _root.getChildFile("data5"), pack = _root.getChildFile("Pack A");
		require(pack.getChildFile("hats").createDirectory().wasOk(), "the pack");
		juce::AiffAudioFormat aiff;
		juce::FlacAudioFormat flac;
		writeWav(pack.getChildFile("kick.wav"), 0.3, 60);
		writeWav(pack.getChildFile("snare.aif"), 0.2, 200, &aiff);
		writeWav(pack.getChildFile("hats").getChildFile("hat.flac"), 0.1, 0, &flac);
		require(pack.getChildFile("kick.wav").copyFileTo(pack.getChildFile("kick again.wav")), "the copy");
		pack.getChildFile("readme.txt").replaceWithText("not audio");
		const auto before = filesOf(pack);

		SampleLibrary lib(data);
		const auto copy = SampleLibrary::importedFolder(data).getChildFile("Pack A");
		LockerImport importer(data);
		const auto r = walk(importer, pack, {}, copy);
		require(r.audio == 4 && r.copied == 3 && r.error.isEmpty(), "the import: " + std::to_string(r.audio)
			+ " samples, " + std::to_string(r.copied) + " copied: " + r.error.toStdString());
		for(const char* path : {"snare.aif", "hats/hat.flac"})
			require(copy.getChildFile(path).existsAsFile() && copy.getChildFile(path).getSize()
				== pack.getChildFile(path).getSize(), std::string(path) + " not copied as it is");
		// The kick twice in the pack, copied once, whichever the walk met first
		require(!copy.getChildFile("readme.txt").exists()
			&& copy.getChildFile("kick.wav").exists() != copy.getChildFile("kick again.wav").exists(),
			"a file copied that is no sample, or a sample twice");
		require(lib.addSource(Kind::Imported, copy, "Pack A"), "the source not added");
		waitForScan(lib);
		int64_t bytes = 0;
		for(const auto& f : juce::RangedDirectoryIterator(copy, true, "*", juce::File::findFiles))
			bytes += f.getFileSize();
		require(lib.entries().size() == 3 && lib.sources()[0].kind == Kind::Imported && lib.sourceBytes(0) == bytes,
			"the source: " + std::to_string(lib.entries().size()) + " samples, " + std::to_string(lib.sourceBytes(0))
			+ " bytes, not " + std::to_string(bytes));
		{
			SampleLibrary again(data);
			require(again.sources().size() == 1 && again.sources()[0].kind == Kind::Imported, "IMPORTED read back");
		}
		require(lib.removeSource(0) && !copy.exists() && SampleLibrary::importedFolder(data).isDirectory()
			&& filesOf(pack) == before, "the source removed: its copy left, or the pack touched");
		std::printf("PASS: IMPORT PACK copies 3 samples (WAV, AIFF, FLAC; one twice and a text left), %d bytes counted; "
			"removed, the copy goes, the pack stays\n", static_cast<int>(bytes));
	}

	// IMPORT PACK within its bounds: so many files, a file so large, so many bytes in all, the rest left out and counted.
	// A folder under Imported the list lacks, an import its instance closed on, is taken as a source as the Locker loads;
	// an empty one is not
	void importBounds(const juce::File& _root)
	{
		const auto data = _root.getChildFile("data7"), pack = _root.getChildFile("Pack B");
		require(pack.createDirectory().wasOk(), "the pack");
		// Five samples of one size, each its own sound: five fingerprints
		for(int i = 0; i < 5; ++i)
			writeWav(pack.getChildFile("hit" + juce::String(i) + ".wav"), 0.1, 100.0f + 50.0f * static_cast<float>(i));
		const auto size = pack.getChildFile("hit0.wav").getSize();
		require(LockerImport::Bounds{}.files == SampleLibrary::MostFiles, "the bound on files is not the scan's");
		const std::atomic<bool> quit{false};
		const auto copyInto = [&](const char* _name, const LockerImport::Bounds& _bounds)
		{
			const auto copy = SampleLibrary::importedFolder(data).getChildFile(_name);
			const auto r = LockerImport::run(pack, {}, data, copy, quit, _bounds);
			int files = 0;
			for(const auto& f : juce::RangedDirectoryIterator(copy, true, "*", juce::File::findFiles))
				files += f.getFile().getFileName().endsWith(".part") ? 100 : 1;
			require(files == r.copied && r.error.isEmpty(), std::string(_name) + ": " + std::to_string(files)
				+ " files for " + std::to_string(r.copied) + " copied: " + r.error.toStdString());
			return std::make_pair(r.copied, r.left);
		};
		LockerImport::Bounds three;
		three.files = 3;
		LockerImport::Bounds small;
		small.fileBytes = size - 1;
		LockerImport::Bounds two;
		two.bytes = 2 * size + 1;
		require(copyInto("all", {}) == std::make_pair(5, 0), "no bound: not every file copied");
		require(copyInto("three", three) == std::make_pair(3, 2), "three files at most");
		require(copyInto("small", small) == std::make_pair(0, 5), "a file over its bound copied");
		require(copyInto("two", two) == std::make_pair(2, 3), "the bytes in all");

		// "all" holds five files the list lacks; "empty" nothing
		require(SampleLibrary::importedFolder(data).getChildFile("empty").createDirectory().wasOk(), "the empty folder");
		SampleLibrary lib(data);
		lib.adoptImported();
		juce::StringArray adopted;
		for(const auto& s : lib.sources())
			adopted.add(s.kind == Kind::Imported ? s.name : "LINKED " + s.name);
		adopted.sort(false);
		require(adopted.joinIntoString(",") == "all,three,two", "the folders taken: "
			+ adopted.joinIntoString(", ").toStdString());
		waitForScan(lib);
		require(lib.entries().size() == 10, "the folders taken hold " + std::to_string(lib.entries().size()) + " samples");
		std::printf("PASS: IMPORT PACK copies 3 of 5 files, none over its size, 2 within its bytes; 3 folders an import "
			"left taken as sources, the empty one not\n");
	}

	// An instance closed while a large file copies: the walk stops within a megabyte, the destructor waits for it, and
	// the file cut short is removed
	void importStopped(const juce::File& _root)
	{
		const auto data = _root.getChildFile("data8"), pack = _root.getChildFile("Pack C");
		require(pack.createDirectory().wasOk(), "the pack");
		// Two files of 150 MB, a byte apart, two fingerprints: the second one's copy is caught if the first ends unseen
		for(int i = 0; i < 2; ++i)
		{
			juce::FileOutputStream out(pack.getChildFile("big" + juce::String(i) + ".wav"));
			const std::vector<char> chunk(1 << 20, static_cast<char>(i));
			require(out.openedOk(), "the large file");
			for(int m = 0; m < 150; ++m)
				out.write(chunk.data(), chunk.size());
			out.write(chunk.data(), static_cast<size_t>(i));
			out.flush();
			require(!out.getStatus().failed(), "the large file written");
		}
		const auto copy = SampleLibrary::importedFolder(data).getChildFile("Pack C");
		const auto parts = [&]
		{
			return copy.findChildFiles(juce::File::findFiles, true, "*.part").size();
		};
		auto importer = std::make_unique<LockerImport>(data);
		require(importer->start(pack, {}, copy), "the walk not started");
		const auto until = juce::Time::getMillisecondCounter() + 20000;
		while(parts() == 0)
		{
			require(importer->running(), "both copies ended before one was seen");
			require(juce::Time::getMillisecondCounter() < until, "no copy started");
		}
		const auto t0 = juce::Time::getMillisecondCounterHiRes();
		importer.reset();
		const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;
		int whole = 0;
		for(const auto& f : copy.findChildFiles(juce::File::findFiles, true, "*.wav"))
			whole += f.getSize() >= 150 << 20 ? 1 : 0;
		require(parts() == 0 && whole < 2, "after the instance closed: " + std::to_string(parts()) + " parts, "
			+ std::to_string(whole) + " whole files");
		std::printf("PASS: an instance closed while a 150 MB file copies waits %.0f ms for the walk; no part left, %d of 2 "
			"files whole\n", ms, whole);
		pack.deleteRecursively();
		copy.deleteRecursively();
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
		w.directory[1] = 1000;		// slot 1 holds a sample
		const auto file = LockerImport::wordsFile(_root.getChildFile("data3"));
		require(LockerImport::writeWords(file, w), "the words not written");
		ImageWords back;
		require(LockerImport::readWords(file, back) && back.trx == w.trx && back.pi == w.pi && back.e12 == w.e12
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
		require(!LockerImport::readWords(file, back), "a file cut short read");
		std::printf("PASS: the words file read back as written, %d bytes; wrong sizes and a file cut short refused\n",
			static_cast<int>(bytes.getSize()));
	}

	// A data folder of before the Locker: its folders' list becomes LINKED sources, its types and its words come over
	void before(const juce::File& _root)
	{
		const auto data = _root.getChildFile("data6"), mine = _root.getChildFile("mine6");
		require(mine.createDirectory().wasOk(), "the folder");
		writeWav(mine.getChildFile("one.wav"), 0.4, 60);
		const auto fingerprint = SampleLibrary::fingerprintOf(mine.getChildFile("one.wav"));
		const auto samples = data.getChildFile("Samples");
		require(samples.createDirectory().wasOk()
			&& samples.getChildFile("folders.txt").replaceWithText(mine.getFullPathName() + "\n")
			&& samples.getChildFile("types.txt").replaceWithText(fingerprint + " CLAP\n"), "the old lists");
		const auto oldWords = data.getChildFile("Elektron").getChildFile("Machinedrum.words");
		require(oldWords.getParentDirectory().createDirectory().wasOk() && oldWords.replaceWithText("words"), "old words");
		{
			SampleLibrary lib(data);
			require(lib.sources().size() == 1 && lib.sources()[0].folder == mine && lib.sources()[0].kind == Kind::Linked
				&& lib.sources()[0].name == "mine6", "the old list not taken");
			lib.scanOnce();
			waitForScan(lib);
			require(entry(lib, "one.wav").type == Type::Clap && entry(lib, "one.wav").typeFrom
				== SampleLibrary::TypeFrom::User, "the old type not taken");
		}
		LockerImport::takeOldWords(data);
		require(LockerImport::wordsFile(data).loadFileAsString() == "words" && !oldWords.exists(), "the old words not moved");
		// The old list changed later is no longer read: the Locker's own is
		samples.getChildFile("folders.txt").replaceWithText(_root.getFullPathName() + "\n");
		SampleLibrary again(data);
		require(again.sources().size() == 1 && again.sources()[0].folder == mine, "the old list read twice");
		std::printf("PASS: the folders' list, the types and the words of before the Locker taken once\n");
	}

	// The user's image, copied into a temporary folder: walked, its words alone written, the Library made from them the
	// image's; an instrument on that data folder has FACTORY's 32 slots
	void image(const juce::File& _root)
	{
		const auto* path = std::getenv("GEARMULATOR_MD_FIRMWARE_BIN");
		if(!path || !*path || !juce::File(juce::String::fromUTF8(path)).existsAsFile())
		{
			std::printf("SKIP: no GEARMULATOR_MD_FIRMWARE_BIN, no image walked\n");
			return;
		}
		const juce::File source(juce::String::fromUTF8(path));
		const auto dataRoot = _root.getChildFile("root4"), content = _root.getChildFile("content4");
		_putenv_s("GEARMULATOR_DATA_ROOT", dataRoot.getFullPathName().toRawUTF8());
		const auto data = Processor::dataFolder();
		require(content.getChildFile("MD").createDirectory().wasOk(), "the folder");
		require(source.copyFileTo(content.getChildFile("MD").getChildFile("my image.bin")), "the image copied");
		const std::atomic<bool> quit{false};
		const auto r = LockerImport::run(content, {}, data, {}, quit);
		require(r.image == "my image.bin" && r.refused == 0 && r.error.isEmpty(), "the image not read: "
			+ r.error.toStdString());
		const auto file = LockerImport::wordsFile(data);
		ImageWords w;
		require(LockerImport::readWords(file, w), "the words not read");
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
		int files = 0;
		for(const auto& f : juce::RangedDirectoryIterator(dataRoot, true, "*", juce::File::findFiles))
		{
			require(f.getFile() == file, "a file other than the words written: " + f.getFile().getFullPathName().toStdString());
			++files;
		}
		require(files == 1, "no words file");
		const auto kb = static_cast<int>(file.getSize() / 1024);
		{
			Processor p;
			require(p.libraryFromWords() && p.factorySamples().size() == 32, "FACTORY: "
				+ std::to_string(p.factorySamples().size()) + " slots");
			require(p.factoryBytes() == file.getSize() && p.removeFactory() && !file.exists()
				&& p.factorySamples().empty(), "FACTORY removed: its words left");
		}
		std::printf("PASS: the image's words written, %d KB, nothing else of it; FACTORY's 32 slots; removed, its words "
			"erased\n", kb);
	}
}

int main()
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("edDrumsSampleLibraryTest");
	root.deleteRecursively();
	_putenv_s("GEARMULATOR_DATA_ROOT", root.getChildFile("root").getFullPathName().toRawUTF8());
	const juce::ScopedJuceInitialiser_GUI juce;
	int failed = 0;
	for(const auto& [name, test] : std::initializer_list<std::pair<const char*, void (*)(const juce::File&)>>{
		{"library", library}, {"linked", walkLinked}, {"imported", walkImported}, {"bounds", importBounds},
		{"stopped", importStopped}, {"words", words}, {"before", before}, {"image", image}})
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
