#include "mdDrumsProcessor.h"

#include "mdDrumsController.h"
#include "mdDrumsEditor.h"
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

#include <cmath>

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
		installFromLibrary(_index, true);
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

	bool Processor::installFromLibrary(const int _index, const bool _replace)
	{
		const auto package = m_library ? m_library->get(_index) : nullptr;
		if(!package)
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
