#include "mdDrumsProcessor.h"

#include "mdDrumsController.h"
#include "mdDrumsEditor.h"
#include "mdDrumsSampleFile.h"
#include "mdDrumsStatusView.h"

#include "mdDrumsDevice.h"
#include "mdDrumsMessages.h"

// ReSharper disable once CppUnusedIncludeDirective
#include "BinaryData.h"
#include "jucePluginLib/processorPropertiesInit.h"
#include "jucePluginLib/tools.h"

#include "baseLib/binarystream.h"
#include "baseLib/filesystem.h"
#include "synthLib/deviceException.h"
#include "synthLib/os.h"

#include "MdsPackage.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>

namespace mdDrums
{
	namespace
	{
		juce::PropertiesFile::Options configOptions(const bool _forTests)
		{
			juce::PropertiesFile::Options options;
			options.applicationName = _forTests ? juce::String("MDDrumsTest_") + juce::Uuid().toString()
				: juce::String("MDDrums");
			options.filenameSuffix = ".settings";
			options.folderName = options.applicationName;
			options.osxLibrarySubFolder = "Application Support/" + options.applicationName;
			options.doNotSave = _forTests;
			if(_forTests)
				options.millisecondsBeforeSaving = -1;
			return options;
		}

		pluginLib::Processor::Properties properties()
		{
			const auto compiled = pluginLib::initProcessorProperties();
			// Bus 0 is Main (device channels 0 and 1), bus n is Out n (channel n + 1): the host may enable any of them
			std::vector<size_t> outputs{0};
			for(size_t out = 1; out <= messages::OutCount; ++out)
				outputs.push_back(out + 1);
			return {compiled.name, compiled.vendor, compiled.isSynth, compiled.wantsMidiInput, compiled.producesMidiOut,
				compiled.isMidiEffect, compiled.plugin4CC, compiled.lv2Uri, compiled.binaryData, {}, {}, outputs};
		}
	}

	juce::AudioProcessor::BusesProperties Processor::buses()
	{
		auto properties = BusesProperties().withOutput("Main", juce::AudioChannelSet::stereo(), true);
		for(int out = 1; out <= messages::OutCount; ++out)
			properties = properties.withOutput(juce::String::formatted("Out %02d", out), juce::AudioChannelSet::mono(),
				false);
		return properties;
	}

	Processor::Processor(const bool _forTests)
		// No MCP server: it would listen on every network interface, without authentication, for tools MoDule has no
		// use for
		: jucePluginEditorLib::Processor(buses(), configOptions(_forTests), properties(), false,
			_forTests ? ConfigMode::Ephemeral : ConfigMode::Persistent), m_forTests(_forTests)
	{
		// A test's editor asks GitHub nothing
		if(_forTests)
			getConfig().setValue(StatusView::CheckKey, false);
		else
			migrateData(std::filesystem::u8path(pluginLib::Tools::getPublicDataFolder(getProperties().vendor,
				"MD Drums")), std::filesystem::u8path(getDataFolder()));
		getController();
	}

	bool Processor::migrateData(const std::filesystem::path& _from, const std::filesystem::path& _to)
	{
		bool copied = true;
		const auto flash = std::filesystem::path("roms") / Engine::FlashImageName;
		for(const auto& name : {std::filesystem::path("Bank.syx"), flash})
		{
			std::error_code error;
			if(!std::filesystem::exists(_from / name, error) || std::filesystem::exists(_to / name, error))
				continue;
			std::filesystem::create_directories((_to / name).parent_path(), error);
			copied &= std::filesystem::copy_file(_from / name, _to / name, error) && !error;
		}
		return copied;
	}

	Processor::~Processor()
	{
		destroyEditorState();
		if(m_forTests && m_bank)
		{
			std::error_code error;
			std::filesystem::remove_all(m_bank->file().parent_path(), error);
		}
	}

	jucePluginEditorLib::PluginEditorState* Processor::createEditorState()
	{
		return new EditorState(*this);
	}

	std::vector<std::filesystem::path> Processor::romFolders() const
	{
		// The stack's folders are UTF-8, which a path made from a std::string would read in the ANSI code page
		return {std::filesystem::u8path(getPublicRomFolder()), std::filesystem::u8path(synthLib::getModulePath())};
	}

	bool Processor::hasDevice()
	{
		return dynamic_cast<const Device*>(getPlugin().getDevice()) != nullptr;
	}

	std::string Processor::loadFlashImage(const std::filesystem::path& _file)
	{
		std::string error;
		m_chosenFlash = Engine::readFlashImage(_file, error);
		if(m_chosenFlash.empty())
			return error;

		// The Device is made from the bytes just checked, and the file kept only once it plays: one that cannot start
		// never replaces a good copy
		const auto* before = getPlugin().getDevice();
		try
		{
			setDeviceType(pluginLib::DeviceType::Local, true);
		}
		catch(const std::exception& _e)
		{
			error = _e.what();
		}
		const auto flash = std::move(m_chosenFlash);
		m_chosenFlash.clear();
		if(getPlugin().getDevice() == before || !hasDevice())
			return "MoDule could not start from that file" + (error.empty() ? std::string() : ": " + error);
		controller().followDevice();

		const auto target = getPublicRomFolder() + Engine::FlashImageName;
		if(!baseLib::filesystem::writeFileAtomic(target, flash.data(), flash.size()))
			return "MoDule plays it, but could not keep it as " + target;
		return {};
	}

	std::string Processor::installUserMachine(const int _index, const std::vector<uint8_t>& _file)
	{
		if(!m_library)
			return "MoDule needs its flash image before a User Machine";
		if(auto error = m_library->install(_index, _file); !error.empty())
			return error;
		if(installFromLibrary(_index, true))
			markStateChanged();
		return {};
	}

	void Processor::takeUserMachines(const md::automation::sysex::MdKit& _kit)
	{
		uint16_t taken = 0;
		for(uint8_t track = 0; track < Engine::TrackCount; ++track)
		{
			const auto id = static_cast<uint8_t>(_kit.machine(track));
			if(!md::engine::isUserMachine(id))
				continue;
			const auto index = id - Engine::FirstUserMachine;
			if(taken & (1u << index))
				continue;
			taken = static_cast<uint16_t>(taken | 1u << index);
			installFromLibrary(index, true);
		}
	}

	void Processor::takeUserMachineIfAbsent(const int _index)
	{
		// Without the Device's lock first: the Controller's timer asks this of every Track, and both copies change on the
		// message thread only
		if(!m_library || !m_library->get(_index))
			return;
		const auto* device = dynamic_cast<const Device*>(getPlugin().getDevice());
		if(!device || device->userMachine(_index))
			return;
		installFromLibrary(_index, false);
	}

	std::string Processor::loadSampleFile(const int _rom, const std::filesystem::path& _file)
	{
		const auto* device = dynamic_cast<const Device*>(getPlugin().getDevice());
		if(!device)
			return "MoDule needs its flash image before a Sample";
		if(_rom < 0 || _rom >= Samples::RomCount)
			return "No ROM Machine " + std::to_string(_rom + 1);
		// The room it may take: what is left, and what the Sample it replaces frees; a file longer is refused unread
		const auto held = device->sample(_rom);
		const auto room = device->sampleRoom() + (held ? held->words.size() : 0);
		auto audio = readSampleFile(juce::File(juce::String::fromUTF8(_file.u8string().c_str())), 2 * room);
		if(const auto* error = std::get_if<std::string>(&audio))
			return *error;
		auto made = makeSample(std::get<SampleAudio>(audio), device->sampleCodes(), room);
		if(const auto* error = std::get_if<std::string>(&made))
			return *error;
		auto& s = std::get<Sample>(made);
		s.name = _file.filename().u8string();
		const auto sample = std::make_shared<const Sample>(std::move(s));
		if(!setDeviceSample(_rom, sample))
			return "The Samples have no room left for it";
		markStateChanged();
		if(m_samples && !m_samples->put(_rom, sample))
			return "MoDule plays it, but could not keep it in " + m_samples->file(_rom).u8string();
		return {};
	}

	void Processor::removeSample(const int _rom)
	{
		const auto* device = dynamic_cast<const Device*>(getPlugin().getDevice());
		const bool held = device && device->sample(_rom);
		if(m_samples)
			m_samples->remove(_rom);
		setDeviceSample(_rom, nullptr);
		if(held)
			markStateChanged();
	}

	void Processor::markStateChanged()
	{
		updateHostDisplay(juce::AudioProcessorListener::ChangeDetails().withNonParameterStateChanged(true));
	}

	void Processor::takeSample(const int _rom)
	{
		const auto sample = m_samples ? m_samples->get(_rom) : nullptr;
		const auto* device = dynamic_cast<const Device*>(getPlugin().getDevice());
		if(!sample || !device)
			return;
		if(const auto held = device->sample(_rom); !(held && *held == *sample))
			setDeviceSample(_rom, sample);
	}

	void Processor::takeSamples(const md::automation::sysex::MdKit& _kit)
	{
		if(!m_samples)
			return;
		const auto* device = dynamic_cast<const Device*>(getPlugin().getDevice());
		for(uint8_t track = 0; track < Engine::TrackCount && device; ++track)
		{
			const auto rom = Samples::romOf(static_cast<uint8_t>(_kit.machine(track)));
			const auto sample = rom >= 0 ? m_samples->get(rom) : nullptr;
			const auto held = sample ? device->sample(rom) : nullptr;
			if(sample && !(held && *held == *sample))
				setDeviceSample(rom, sample);
		}
	}

	Processor::ImportKind Processor::importKind(const std::filesystem::path& _file)
	{
		auto extension = _file.extension().u8string();
		std::transform(extension.begin(), extension.end(), extension.begin(),
			[](const char _c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(_c))); });
		if(extension == ".syx" || extension == ".mds")
			return ImportKind::UserMachine;
		// What JUCE reads as built (source/juce.cmake): WAV (.wav, .bwf), AIFF, FLAC
		if(extension == ".wav" || extension == ".bwf" || extension == ".aif" || extension == ".aiff" || extension == ".flac")
			return ImportKind::Audio;
		return ImportKind::None;
	}

	Processor::Import Processor::importFile(const std::filesystem::path& _file, const int _track)
	{
		const auto kind = importKind(_file);
		const auto name = _file.filename().u8string();
		const auto* device = dynamic_cast<const Device*>(getPlugin().getDevice());
		if(!device)
			return {false, -1, "MoDule needs its flash image first"};
		auto* machine = _track >= 0 && _track < Engine::TrackCount ? getController().getParameter("Machine",
			static_cast<uint8_t>(_track)) : nullptr;
		const auto playing = machine ? static_cast<uint8_t>(machine->getUnnormalizedValue()) : uint8_t{0};
		const auto play = [&](const int _id)
		{
			if(machine)
				machine->setUnnormalizedValueNotifyingHost(_id, pluginLib::Parameter::Origin::Ui);
		};
		const auto onTrack = machine ? ", Track " + std::to_string(_track + 1) : std::string();

		if(kind == ImportKind::UserMachine)
		{
			// The number that holds the same package already, else the first free one: an install never replaces another
			// Machine in the Library, which other instances and the Bank's Kits take from
			std::ifstream in(_file, std::ios::binary);
			const std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
			std::vector<uint8_t> package;
			try
			{
				package = !bytes.empty() && bytes.front() == 0xf0 ? md::mds::packageFromSysex(bytes) : bytes;
			}
			catch(const std::exception&)
			{
			}
			int index = -1;
			for(int i = 0; i < Library::Size && index < 0 && m_library; ++i)
				if(const auto held = m_library->get(i); held && !package.empty() && held->bytes == package)
					index = i;
			for(int i = 0; i < Library::Size && index < 0 && m_library; ++i)
				if(!m_library->get(i))
					index = i;
			if(index < 0)
				return {false, -1, "The 16 User Machines are taken: " + name + " was not installed"};
			if(auto error = installUserMachine(index, bytes); !error.empty())
				return {false, -1, name + ": " + error};
			const auto id = Engine::FirstUserMachine + index;
			play(id);
			const auto installed = m_library->get(index);
			char number[8];
			std::snprintf(number, sizeof(number), "USR-%02d", index + 1);
			return {true, id, (installed ? installed->displayName() : name) + " installed as " + number + onTrack};
		}
		if(kind == ImportKind::Audio)
		{
			// The ROM the Track plays, else the first of ROM-33 to ROM-48 without a user's Sample
			int rom = Samples::romOf(playing);
			for(int r = 32; r < Samples::RomCount && rom < 0; ++r)
				if(!device->sample(r))
					rom = r;
			if(rom < 0)
				return {false, -1, "ROM-33 to ROM-48 all hold a Sample: put " + name + " on a Track playing a ROM Machine"};
			if(auto error = loadSampleFile(rom, _file); !error.empty())
				return {false, -1, error};
			const auto id = rom < 32 ? 128 + rom : 176 + rom - 32;
			play(id);
			char number[8];
			std::snprintf(number, sizeof(number), "ROM-%02d", rom + 1);
			return {true, id, name + " on " + number + onTrack};
		}
		return {false, -1, name + ": MoDule takes WAV, AIFF and FLAC files, and User Machines (.syx, .mds)"};
	}

	std::shared_ptr<const md::mds::Package> Processor::userMachine(const int _index)
	{
		// Read without the lock: a User Machine changes on the message thread only
		const auto* device = dynamic_cast<const Device*>(getPlugin().getDevice());
		return device ? device->userMachine(_index) : nullptr;
	}

	std::pair<int, const Device::Kept*> Processor::keptUserMachine()
	{
		// Read without the lock: a state is restored on the message thread
		const auto* device = dynamic_cast<const Device*>(getPlugin().getDevice());
		for(int index = 0; device && index < Library::Size; ++index)
			if(const auto& kept = device->keptUserMachine(index))
				return {index, &*kept};
		return {-1, nullptr};
	}

	std::array<std::shared_ptr<const Sample>, Samples::RomCount> Processor::samples()
	{
		// Read without the lock: a Sample changes on the message thread only
		std::array<std::shared_ptr<const Sample>, Samples::RomCount> result{};
		if(const auto* device = dynamic_cast<const Device*>(getPlugin().getDevice()))
			for(int rom = 0; rom < Samples::RomCount; ++rom)
				result[static_cast<size_t>(rom)] = device->sample(rom);
		return result;
	}

	bool Processor::setDeviceSample(const int _rom, std::shared_ptr<const Sample> _sample)
	{
		// Under the Device's lock only to hand it over: the audio thread writes it over the next blocks
		return getPlugin().withDeviceLocked([&](synthLib::Device* _device)
		{
			auto* d = dynamic_cast<Device*>(_device);
			return d && d->setSample(_rom, std::move(_sample));
		});
	}

	bool Processor::installFromLibrary(const int _index, const bool _replace)
	{
		const auto package = m_library ? m_library->get(_index) : nullptr;
		if(!package)
			return false;
		// Nothing to do, without the lock: a click on the browser's cell for a User Machine already held (a User Machine
		// changes on the message thread only)
		if(const auto* d = dynamic_cast<const Device*>(getPlugin().getDevice()))
			if(const auto held = d->userMachine(_index); held && (!_replace || held->bytes == package->bytes))
				return false;
		// Under the Device's lock, which pauses rendering for the time the voice DSP takes it (a few ms)
		return getPlugin().withDeviceLocked([&](synthLib::Device* _device)
		{
			auto* device = dynamic_cast<Device*>(_device);
			if(!device)
				return false;
			const auto held = device->userMachine(_index);
			if(held && (!_replace || held->bytes == package->bytes))
				return false;
			try
			{
				device->installUserMachine(_index, package);
				return true;
			}
			catch(const std::exception&)
			{
				return false;
			}
		});
	}

	void Processor::onDeviceCreationFailed(const synthLib::DeviceException& _e)
	{
		// The editor's status line says it and chooses the file (StatusView); the stack's box would ask for a reload
		if(_e.errorCode() == synthLib::DeviceError::FirmwareMissing)
			return;
		jucePluginEditorLib::Processor::onDeviceCreationFailed(_e);
	}

	synthLib::Device* Processor::createDevice()
	{
		auto flash = m_chosenFlash;
		if(flash.empty())
			flash = Engine::findFlashImage(romFolders());
		if(flash.empty())
			throw synthLib::DeviceException(synthLib::DeviceError::FirmwareMissing,
				"MoDule needs the Machinedrum UW's 8 MB flash image with OS 1.63.");

		if(!m_bank)
		{
			const auto folder = m_forTests
				? std::filesystem::temp_directory_path() / ("MDDrumsTest_" + juce::Uuid().toString().toStdString())
				: std::filesystem::u8path(getDataFolder());
			m_bank = std::make_unique<Bank>(folder / "Bank.syx", messages::factoryKits(flash));
			m_bank->read();
			m_library = std::make_unique<Library>(folder / "UserMachines");
			m_library->read();
			m_samples = std::make_unique<SampleLibrary>(folder / "Samples");
			m_samples->read();
		}

		synthLib::DeviceCreateParams params;
		params.romName = Engine::FlashImageName;
		params.romData = std::move(flash);
		params.homePath = getDataFolder();
		auto* device = new Device(params, m_telemetry);
		std::atomic_store(&m_fxTables, device->fxTables());
		std::atomic_store(&m_machines, std::make_shared<const Machines>(device->machines()));
		return device;
	}

	pluginLib::Controller* Processor::createController()
	{
		return new Controller(*this);
	}

	Controller& Processor::controller()
	{
		return static_cast<Controller&>(getController());
	}

	void Processor::processBpm(const float _bpm)
	{
		if(std::abs(_bpm - m_bpm.load(std::memory_order_relaxed)) < 0.005f)
			return;
		if(tryAddRealtimeMidiEvent(messages::toEvent(messages::tempo(_bpm))))
			m_bpm.store(_bpm, std::memory_order_relaxed);
	}

	void Processor::saveChunkData(baseLib::BinaryStream& _s)
	{
		jucePluginEditorLib::Processor::saveChunkData(_s);
		// The Bank's Slot the Kit played came from (its Kit is the Device's, in "MIDI")
		{
			baseLib::ChunkWriter cw(_s, "KSLT", 1);
			_s.write<uint32_t>(static_cast<uint32_t>(controller().playedSlot()));
		}
		// The Track shown, which the editor and the relays for Push 2 aim at
		baseLib::ChunkWriter cw(_s, "FOCS", 1);
		_s.write<uint32_t>(getController().getCurrentPart());
	}

	void Processor::loadChunkData(baseLib::ChunkReader& _cr)
	{
		_cr.add("KSLT", 1, [this](baseLib::BinaryStream& _stream, uint32_t)
		{
			controller().setPlayedSlot(static_cast<int>(_stream.read<uint32_t>()));
		});
		_cr.add("FOCS", 1, [this](baseLib::BinaryStream& _stream, uint32_t)
		{
			const auto part = _stream.read<uint32_t>();
			if(part < Controller::TrackCount)
				getController().setCurrentPart(static_cast<uint8_t>(part));
		});
		// Before the stack's own "MIDI" reader (the first one registered wins): the Device's state, also handed to the
		// Controller, which then follows it without touching the Device
		_cr.add("MIDI", 1, [this](baseLib::BinaryStream& _stream, uint32_t)
		{
			std::vector<uint8_t> state;
			_stream.read(state);
			getPlugin().setState(state);
			controller().setLoadedState(std::move(state));
		});
		jucePluginEditorLib::Processor::loadChunkData(_cr);
	}

	bool Processor::isBusesLayoutSupported(const BusesLayout& _layouts) const
	{
		if(_layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo() || !_layouts.inputBuses.isEmpty())
			return false;
		for(int bus = 1; bus < _layouts.outputBuses.size(); ++bus)
		{
			const auto& set = _layouts.outputBuses.getReference(bus);
			if(!set.isDisabled() && set != juce::AudioChannelSet::mono())
				return false;
		}
		return true;
	}
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
	return new mdDrums::Processor();
}
