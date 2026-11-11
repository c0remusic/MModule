#include "edDrumsLockerImport.h"

#include "edDrumsSampleLibrary.h"

#include <cstring>

namespace elektron::drums
{
	namespace
	{
		constexpr int64_t FlashSize = 8 * 1024 * 1024;
		constexpr int MostVisited = 200000;
		// The words file: these eight bytes, then each of ImageWords' five vectors as a count and its words, 32 bits
		// little-endian. The factory bank holds at most 2.5 MB of samples, which pack two in a word
		const char Magic[9] = "EDMDWRD1";
		constexpr uint32_t MostWords = 1u << 24;

		// The original machines' own files the walk knows and does not read: SysEx dumps, projects and sounds as their
		// transfer tool saves them
		bool isMachineFile(const juce::String& _name)
		{
			for(const auto* ext : {".syx", ".dtprj", ".dtsnd", ".stprj", ".stsnd"})
				if(_name.endsWithIgnoreCase(ext))
					return true;
			return false;
		}

		// _from to _to, through a file beside it that _quit or a failure removes: none cut short keeps the name
		bool copyStopping(const juce::File& _from, const juce::File& _to, const std::atomic<bool>& _quit)
		{
			const auto part = _to.getSiblingFile(_to.getFileName() + ".part");
			bool done = false;
			{
				juce::FileInputStream in(_from);
				juce::FileOutputStream out(part);
				if(in.openedOk() && out.openedOk() && out.setPosition(0) && out.truncate().wasOk())
				{
					std::vector<char> chunk(1 << 20);
					while(!_quit.load(std::memory_order_acquire))
					{
						const int n = in.read(chunk.data(), static_cast<int>(chunk.size()));
						if(n <= 0)
						{
							done = in.isExhausted();
							break;
						}
						if(!out.write(chunk.data(), static_cast<size_t>(n)))
							break;
					}
					out.flush();
					done = done && !out.getStatus().failed();
				}
			}
			if(done && part.moveFileTo(_to))
				return true;
			part.deleteFile();
			return false;
		}
	}

	juce::File LockerImport::wordsFile(const juce::File& _dataFolder)
	{
		return SampleLibrary::lockerFolder(_dataFolder).getChildFile("Factory.words");
	}

	void LockerImport::takeOldWords(const juce::File& _dataFolder)
	{
		const auto old = _dataFolder.getChildFile("Elektron").getChildFile("Machinedrum.words");
		const auto now = wordsFile(_dataFolder);
		if(old.existsAsFile() && !now.exists() && now.getParentDirectory().createDirectory().wasOk())
			old.moveFileTo(now);
	}

	bool LockerImport::writeWords(const juce::File& _file, const ImageWords& _words)
	{
		if(!_file.getParentDirectory().createDirectory())
			return false;
		// Written aside, then moved over the file, so that an instance loading meanwhile reads the old or the new
		juce::TemporaryFile temp(_file);
		{
			juce::FileOutputStream out(temp.getFile());
			if(!out.openedOk() || !out.write(Magic, 8))
				return false;
			for(const auto* v : {&_words.trx, &_words.pi, &_words.e12, &_words.directory, &_words.samples})
			{
				out.writeInt(static_cast<int>(v->size()));
				for(const auto w : *v)
					out.writeInt(static_cast<int>(w));
			}
			out.flush();
			if(out.getStatus().failed())
				return false;
		}
		return temp.overwriteTargetFileWithTemporary();
	}

	bool LockerImport::readWords(const juce::File& _file, ImageWords& _words)
	{
		juce::FileInputStream in(_file);
		char magic[8]{};
		if(!in.openedOk() || in.read(magic, 8) != 8 || std::memcmp(magic, Magic, 8) != 0)
			return false;
		ImageWords w;
		for(auto* v : {&w.trx, &w.pi, &w.e12, &w.directory, &w.samples})
		{
			const auto n = static_cast<uint32_t>(in.readInt());
			if(n > MostWords || static_cast<int64_t>(n) * 4 > in.getNumBytesRemaining())
				return false;
			v->resize(n);
			for(auto& word : *v)
				word = static_cast<uint32_t>(in.readInt());
		}
		if(!in.isExhausted())
			return false;
		_words = std::move(w);
		return true;
	}

	LockerImport::Report LockerImport::run(const juce::File& _folder, const std::set<juce::String>& _known,
		const juce::File& _dataFolder, const juce::File& _copy, const std::atomic<bool>& _quit, const Bounds& _bounds)
	{
		Report r;
		r.folder = _folder;
		r.copy = _copy;
		// What this walk copied, so that a pack holding a file twice is copied once
		std::set<juce::String> copied;
		int visited = 0;
		int64_t bytes = 0;
		for(const auto& item : juce::RangedDirectoryIterator(_folder, true, "*", juce::File::findFiles,
			juce::File::FollowSymlinks::noCycles))
		{
			if(_quit.load(std::memory_order_acquire) || ++visited > MostVisited)
				break;
			const auto& file = item.getFile();
			const auto name = file.getFileName();
			if(SampleLibrary::isAudioName(name))
			{
				++r.audio;
				const auto fingerprint = SampleLibrary::fingerprintOf(file);
				if(_known.count(fingerprint))
				{
					++r.known;
					continue;
				}
				if(_copy == juce::File() || fingerprint.isEmpty() || copied.count(fingerprint))
					continue;
				const auto size = item.getFileSize();
				if(r.copied >= _bounds.files || size > _bounds.fileBytes || bytes + size > _bounds.bytes)
				{
					++r.left;
					continue;
				}
				const auto target = _copy.getChildFile(file.getRelativePathFrom(_folder));
				if(target.getParentDirectory().createDirectory().wasOk() && copyStopping(file, target, _quit))
				{
					copied.insert(fingerprint);
					++r.copied;
					bytes += size;
				}
				else if(!_quit.load(std::memory_order_acquire))
					r.error = name + " could not be copied to " + _copy.getFullPathName();
				continue;
			}
			if(isMachineFile(name))
			{
				++r.unread;
				continue;
			}
			// A flash image: the first one that holds the OS, its words alone kept
			if(item.getFileSize() != FlashSize || r.image.isNotEmpty())
				continue;
			juce::MemoryBlock data;
			if(!file.loadFileAsData(data) || static_cast<int64_t>(data.getSize()) != FlashSize)
				continue;
			const auto* bytes = static_cast<const uint8_t*>(data.getData());
			try
			{
				const auto words = Library::importImage(std::vector<uint8_t>(bytes, bytes + data.getSize()));
				if(writeWords(wordsFile(_dataFolder), words))
					r.image = name;
				else
					r.error = "the factory bank could not be written to " + wordsFile(_dataFolder).getFullPathName();
			}
			catch(const std::exception&)
			{
				++r.refused;
			}
		}
		return r;
	}

	LockerImport::LockerImport(juce::File _dataFolder) : m_dataFolder(std::move(_dataFolder))
	{
	}

	LockerImport::~LockerImport()
	{
		m_quit.store(true, std::memory_order_release);
		// Until the job ends, however long: it uses this object's lock and report. Between two files, or two megabytes
		// of one, it sees m_quit
		if(m_pool)
			m_pool->removeAllJobs(true, -1);
	}

	bool LockerImport::start(const juce::File& _folder, std::set<juce::String> _known, const juce::File& _copy)
	{
		if(!_folder.isDirectory() || m_busy.exchange(true))
			return false;
		if(!m_pool)
			m_pool = std::make_unique<juce::ThreadPool>(juce::ThreadPoolOptions{}.withNumberOfThreads(1)
				.withThreadName("Locker import").withDesiredThreadPriority(juce::Thread::Priority::background));
		m_pool->addJob([this, _folder, _copy, known = std::move(_known)]
		{
			auto report = run(_folder, known, m_dataFolder, _copy, m_quit);
			const juce::ScopedLock lock(m_lock);
			m_report = std::move(report);
			m_ready.store(true, std::memory_order_release);
			m_busy.store(false, std::memory_order_release);
		});
		return true;
	}

	bool LockerImport::take(Report& _report)
	{
		if(!m_ready.load(std::memory_order_acquire))
			return false;
		const juce::ScopedLock lock(m_lock);
		_report = m_report;
		m_ready.store(false, std::memory_order_release);
		return true;
	}
}
