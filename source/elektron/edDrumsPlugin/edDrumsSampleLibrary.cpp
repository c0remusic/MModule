#include "edDrumsSampleLibrary.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>

namespace elektron::drums
{
	namespace
	{
		constexpr int MostVisited = 200000;

		// A sound's type where its name says none, from how often its first quarter second crosses zero (twice a
		// period: a sine of f Hz crosses 2f times a second), counted past a twentieth of its peak so that the noise of a
		// quiet tail does not count, and from its length: a kick below 200 Hz, a tom below 600 Hz, a snare below
		// 4.5 kHz, above that noise, a cymbal when it lasts beyond 0.6 s, else a hat. Simple on purpose: the user
		// corrects what it gets wrong
		SampleLibrary::Type typeOfSound(juce::AudioFormatReader& _reader)
		{
			using Type = SampleLibrary::Type;
			const int frames = static_cast<int>(std::min<int64_t>(_reader.lengthInSamples,
				static_cast<int64_t>(_reader.sampleRate / 4)));
			if(frames < 16)
				return Type::Other;
			juce::AudioBuffer<float> buffer(static_cast<int>(_reader.numChannels), frames);
			if(!_reader.read(&buffer, 0, frames, 0, true, true))
				return Type::Other;
			std::vector<float> mono(static_cast<size_t>(frames));
			float peak = 0;
			for(int n = 0; n < frames; ++n)
			{
				float x = 0;
				for(int c = 0; c < buffer.getNumChannels(); ++c)
					x += buffer.getSample(c, n);
				mono[static_cast<size_t>(n)] = x;
				peak = std::max(peak, std::abs(x));
			}
			if(peak <= 0)
				return Type::Other;
			const float floor = peak / 20;
			int sign = 0, crossings = 0;
			for(const float x : mono)
			{
				const int s = x > floor ? 1 : x < -floor ? -1 : 0;
				if(s != 0 && sign != 0 && s != sign)
					++crossings;
				if(s != 0)
					sign = s;
			}
			const double perSecond = crossings * _reader.sampleRate / frames;
			const double seconds = static_cast<double>(_reader.lengthInSamples) / _reader.sampleRate;
			return perSecond < 400 ? Type::Kick : perSecond < 1200 ? Type::Tom : perSecond < 9000 ? Type::Snare
				: seconds > 0.6 ? Type::Cymbal : Type::Hat;
		}

		// A name's words, lower case, the digits after letters cut off ("bd01" is "bd")
		juce::StringArray wordsOf(const juce::String& _name)
		{
			juce::StringArray words;
			juce::String word;
			const auto flush = [&]
			{
				const auto letters = word.trimCharactersAtEnd("0123456789");
				if(letters.isNotEmpty())
					words.add(letters);
				word.clear();
			};
			for(const auto c : _name.toLowerCase())
			{
				if(juce::CharacterFunctions::isLetterOrDigit(c))
					word += juce::String::charToString(c);
				else
					flush();
			}
			flush();
			return words;
		}
	}

	const char* SampleLibrary::typeName(const Type _type)
	{
		constexpr const char* names[Types]{"KICK", "SNARE", "HAT", "CLAP", "TOM", "CYMBAL", "PERC", "OTHER"};
		return names[static_cast<int>(_type)];
	}

	bool SampleLibrary::isAudioName(const juce::String& _name)
	{
		return _name.endsWithIgnoreCase(".wav") || _name.endsWithIgnoreCase(".aif")
			|| _name.endsWithIgnoreCase(".aiff") || _name.endsWithIgnoreCase(".flac");
	}

	SampleLibrary::Type SampleLibrary::guessType(const juce::String& _fileName)
	{
		// The first rule a word meets decides: a word that holds a name ("lowtom", "hihat"), or that is an abbreviation
		struct Rule
		{
			Type type;
			const char* holds;	// words, space-separated
			const char* is;
		};
		constexpr Rule rules[]{
			{Type::Kick, "kick kik", "bd kd"},
			{Type::Snare, "snare snr", "sd sn"},
			{Type::Clap, "clap", "cp cl"},
			{Type::Hat, "hat", "hh ch oh hhc hho"},
			{Type::Tom, "tom", "lt mt ht"},
			{Type::Cymbal, "cymbal ride crash china splash", "cy cym rd cr"},
			{Type::Perc, "perc rim shaker cowbell conga bongo clave tamb", "rs cb sh"},
		};
		const auto words = wordsOf(juce::File::createFileWithoutCheckingPath(_fileName).getFileNameWithoutExtension());
		for(const auto& rule : rules)
		{
			const auto holds = juce::StringArray::fromTokens(rule.holds, false);
			const auto is = juce::StringArray::fromTokens(rule.is, false);
			for(const auto& w : words)
			{
				if(is.contains(w))
					return rule.type;
				for(const auto& h : holds)
					if(w.contains(h))
						return rule.type;
			}
		}
		return Type::Other;
	}

	juce::String SampleLibrary::fingerprintOf(const juce::File& _file)
	{
		juce::FileInputStream in(_file);
		if(!in.openedOk())
			return {};
		// FNV-1a over the size's eight bytes, then the first 64 KB
		uint64_t h = 0xcbf29ce484222325ull;
		const auto add = [&h](const uint8_t _b) { h = (h ^ _b) * 0x100000001b3ull; };
		const auto size = static_cast<uint64_t>(in.getTotalLength());
		for(int i = 0; i < 8; ++i)
			add(static_cast<uint8_t>(size >> (8 * i)));
		std::vector<uint8_t> head(65536);
		const int read = in.read(head.data(), static_cast<int>(head.size()));
		for(int i = 0; i < read; ++i)
			add(head[static_cast<size_t>(i)]);
		return juce::String::toHexString(static_cast<juce::int64>(h)).paddedLeft('0', 16);
	}

	SampleLibrary::SampleLibrary(juce::File _dataFolder) : m_dataFolder(std::move(_dataFolder))
	{
		m_folders = readList();
		m_listWritten = listFile().getLastModificationTime();
		readTypes();
	}

	SampleLibrary::~SampleLibrary()
	{
		m_quit.store(true, std::memory_order_release);
		if(m_pool)
			m_pool->removeAllJobs(true, 10000);
	}

	juce::File SampleLibrary::listFile() const
	{
		return m_dataFolder.getChildFile("Samples").getChildFile("folders.txt");
	}

	juce::Array<juce::File> SampleLibrary::readList() const
	{
		juce::Array<juce::File> folders;
		const auto file = listFile();
		// A list of folders is a few KB: a bigger file is not one
		if(!file.existsAsFile() || file.getSize() > (1 << 20))
			return folders;
		juce::StringArray lines;
		file.readLines(lines);
		for(const auto& line : lines)
		{
			const auto path = line.trim();
			if(juce::File::isAbsolutePath(path))
				folders.addIfNotAlreadyThere(juce::File(path));
		}
		return folders;
	}

	bool SampleLibrary::addFolder(const juce::File& _folder)
	{
		if(!_folder.isDirectory())
			return false;
		m_folders = readList();
		if(!m_folders.contains(_folder))
		{
			m_folders.add(_folder);
			if(!writeList())
				return false;
		}
		m_listWritten = listFile().getLastModificationTime();
		scan();
		return true;
	}

	bool SampleLibrary::writeList()
	{
		juce::StringArray lines;
		for(const auto& f : m_folders)
			lines.add(f.getFullPathName());
		const auto file = listFile();
		return file.getParentDirectory().createDirectory() && file.replaceWithText(lines.joinIntoString("\n") + "\n");
	}

	bool SampleLibrary::removeFolder(const juce::File& _folder)
	{
		m_folders = readList();
		const int index = m_folders.indexOf(_folder);
		if(index < 0)
			return false;
		m_folders.remove(index);
		if(!writeList())
			return false;
		m_listWritten = listFile().getLastModificationTime();
		// Its entries go now, the others keep their folder's new index, until the scan's
		m_entries.erase(std::remove_if(m_entries.begin(), m_entries.end(), [index](const Entry& _e)
		{
			return _e.folder == index;
		}), m_entries.end());
		for(auto& e : m_entries)
			if(e.folder > index)
				--e.folder;
		++m_generation;
		scan();
		return true;
	}

	juce::File SampleLibrary::typesFile() const
	{
		return m_dataFolder.getChildFile("Samples").getChildFile("types.txt");
	}

	// "fingerprint TYPE" a line
	void SampleLibrary::readTypes()
	{
		m_userTypes.clear();
		const auto file = typesFile();
		m_typesWritten = file.getLastModificationTime();
		if(!file.existsAsFile() || file.getSize() > (4 << 20))
			return;
		juce::StringArray lines;
		file.readLines(lines);
		for(const auto& line : lines)
		{
			const auto fingerprint = line.upToFirstOccurrenceOf(" ", false, false).trim();
			const auto name = line.fromFirstOccurrenceOf(" ", false, false).trim();
			for(int t = 0; t < Types; ++t)
				if(fingerprint.length() == 16 && name == typeName(static_cast<Type>(t)))
					m_userTypes[fingerprint] = static_cast<Type>(t);
		}
	}

	void SampleLibrary::applyTypes()
	{
		for(auto& e : m_entries)
		{
			const auto it = m_userTypes.find(e.fingerprint);
			if(it != m_userTypes.end())
			{
				e.type = it->second;
				e.typeFrom = TypeFrom::User;
			}
		}
	}

	bool SampleLibrary::setType(const juce::String& _fingerprint, const Type _type)
	{
		readTypes();
		m_userTypes[_fingerprint] = _type;
		juce::StringArray lines;
		for(const auto& [fingerprint, type] : m_userTypes)
			lines.add(fingerprint + " " + typeName(type));
		const auto file = typesFile();
		if(!file.getParentDirectory().createDirectory() || !file.replaceWithText(lines.joinIntoString("\n") + "\n"))
			return false;
		m_typesWritten = file.getLastModificationTime();
		applyTypes();
		++m_generation;
		return true;
	}

	void SampleLibrary::scanOnce()
	{
		if(!m_scanned)
			scan();
	}

	void SampleLibrary::follow()
	{
		// Another instance's correction of a type
		if(typesFile().getLastModificationTime() != m_typesWritten)
		{
			readTypes();
			applyTypes();
			++m_generation;
		}
		const auto written = listFile().getLastModificationTime();
		if(written == m_listWritten)
			return;
		m_listWritten = written;
		m_folders = readList();
		if(m_scanned)
			scan();
	}

	void SampleLibrary::scan()
	{
		m_scanned = true;
		const juce::ScopedLock lock(m_lock);
		m_scanFolders = m_folders;
		if(m_busy.load(std::memory_order_acquire))
		{
			m_again = true;
			return;
		}
		m_busy.store(true, std::memory_order_release);
		if(!m_pool)
			m_pool = std::make_unique<juce::ThreadPool>(juce::ThreadPoolOptions{}.withNumberOfThreads(1)
				.withThreadName("Sample library scan").withDesiredThreadPriority(juce::Thread::Priority::background));
		m_pool->addJob([this] { run(); });
	}

	// The scan's thread: the folders given, again while another scan was asked for meanwhile
	void SampleLibrary::run()
	{
		juce::AudioFormatManager formats;
		formats.registerBasicFormats();
		for(;;)
		{
			juce::Array<juce::File> folders;
			{
				const juce::ScopedLock lock(m_lock);
				folders = m_scanFolders;
				m_again = false;
			}
			std::vector<Entry> found;
			int files = 0, visited = 0;
			for(int f = 0; f < folders.size(); ++f)
				for(const auto& item : juce::RangedDirectoryIterator(folders[f], true, "*",
					juce::File::findFiles, juce::File::FollowSymlinks::noCycles))
				{
					if(m_quit.load(std::memory_order_acquire) || ++visited > MostVisited || files >= MostFiles)
						break;
					const auto& file = item.getFile();
					if(!isAudioName(file.getFileName()))
						continue;
					++files;
					// The header alone: a file that is not audio, or whose header says what no file holds, is left out
					const std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
					if(!reader || reader->lengthInSamples <= 0 || reader->numChannels < 1 || reader->numChannels > 8
						|| reader->sampleRate <= 0 || reader->sampleRate > 384000)
						continue;
					Entry e;
					e.name = file.getFileName();
					e.file = file;
					e.folder = f;
					e.length = reader->lengthInSamples;
					e.rate = reader->sampleRate;
					e.channels = static_cast<int>(reader->numChannels);
					e.fingerprint = fingerprintOf(file);
					e.type = guessType(e.name);
					if(e.type == Type::Other)
					{
						e.type = typeOfSound(*reader);
						e.typeFrom = TypeFrom::Sound;
					}
					if(e.fingerprint.isNotEmpty())
						found.push_back(std::move(e));
				}
			if(m_quit.load(std::memory_order_acquire))
				break;
			std::sort(found.begin(), found.end(), [](const Entry& _a, const Entry& _b)
			{
				return _a.folder != _b.folder ? _a.folder < _b.folder
					: _a.file.getFullPathName().compareNatural(_b.file.getFullPathName()) < 0;
			});
			// Done unless asked again, which scan() asks under the same lock: a pass whose folders changed meanwhile
			// (one removed shifts the others' index) is not shown
			const juce::ScopedLock lock(m_lock);
			if(!m_again)
			{
				m_found = std::move(found);
				m_ready.store(true, std::memory_order_release);
				m_busy.store(false, std::memory_order_release);
				return;
			}
		}
		m_busy.store(false, std::memory_order_release);
	}

	bool SampleLibrary::update()
	{
		if(!m_ready.load(std::memory_order_acquire))
			return false;
		{
			const juce::ScopedLock lock(m_lock);
			m_entries = std::move(m_found);
			m_found.clear();
			m_ready.store(false, std::memory_order_release);
		}
		applyTypes();
		++m_generation;
		return true;
	}

	const SampleLibrary::Entry* SampleLibrary::find(const juce::String& _fingerprint) const
	{
		if(_fingerprint.isEmpty())
			return nullptr;
		for(const auto& e : m_entries)
			if(e.fingerprint == _fingerprint)
				return &e;
		return nullptr;
	}
}
