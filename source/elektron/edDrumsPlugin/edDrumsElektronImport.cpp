#include "edDrumsElektronImport.h"

#include "edDrumsSampleLibrary.h"

#include <cstring>

namespace elektron::drums
{
	namespace
	{
		constexpr int64_t FlashSize = 8 * 1024 * 1024;
		constexpr int MostVisited = 200000;
		// The words file: these eight bytes, then each of ImageWords' five vectors as a count and its words, 32 bits
		// little-endian. The UW bank holds at most 2.5 MB of samples, which pack two in a word
		const char Magic[9] = "EDMDWRD1";
		constexpr uint32_t MostWords = 1u << 24;

		// Elektron's own files the import knows and does not read: SysEx dumps, Digitakt's and Syntakt's projects and
		// sounds as Transfer saves them
		bool isElektronFile(const juce::String& _name)
		{
			for(const auto* ext : {".syx", ".dtprj", ".dtsnd", ".stprj", ".stsnd"})
				if(_name.endsWithIgnoreCase(ext))
					return true;
			return false;
		}
	}

	juce::File ElektronImport::wordsFile(const juce::File& _dataFolder)
	{
		return _dataFolder.getChildFile("Elektron").getChildFile("Machinedrum.words");
	}

	bool ElektronImport::writeWords(const juce::File& _file, const ImageWords& _words)
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

	bool ElektronImport::readWords(const juce::File& _file, ImageWords& _words)
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

	ElektronImport::Report ElektronImport::run(const juce::File& _folder, const std::set<juce::String>& _known,
		const juce::File& _dataFolder, const std::atomic<bool>& _quit)
	{
		Report r;
		r.folder = _folder;
		int visited = 0;
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
				if(_known.count(SampleLibrary::fingerprintOf(file)))
					++r.known;
				continue;
			}
			if(isElektronFile(name))
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
					r.error = "the words of " + name + " could not be written to " + wordsFile(_dataFolder).getFullPathName();
			}
			catch(const std::exception&)
			{
				++r.refused;
			}
		}
		return r;
	}

	ElektronImport::ElektronImport(juce::File _dataFolder) : m_dataFolder(std::move(_dataFolder))
	{
	}

	ElektronImport::~ElektronImport()
	{
		m_quit.store(true, std::memory_order_release);
		if(m_pool)
			m_pool->removeAllJobs(true, 10000);
	}

	bool ElektronImport::start(const juce::File& _folder, std::set<juce::String> _known)
	{
		if(!_folder.isDirectory() || m_busy.exchange(true))
			return false;
		if(!m_pool)
			m_pool = std::make_unique<juce::ThreadPool>(juce::ThreadPoolOptions{}.withNumberOfThreads(1)
				.withThreadName("Elektron import").withDesiredThreadPriority(juce::Thread::Priority::background));
		m_pool->addJob([this, _folder, known = std::move(_known)]
		{
			auto report = run(_folder, known, m_dataFolder, m_quit);
			const juce::ScopedLock lock(m_lock);
			m_report = std::move(report);
			m_ready.store(true, std::memory_order_release);
			m_busy.store(false, std::memory_order_release);
		});
		return true;
	}

	bool ElektronImport::take(Report& _report)
	{
		if(!m_ready.load(std::memory_order_acquire))
			return false;
		const juce::ScopedLock lock(m_lock);
		_report = m_report;
		m_ready.store(false, std::memory_order_release);
		return true;
	}
}
