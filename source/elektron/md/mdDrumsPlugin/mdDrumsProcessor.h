#pragma once

#include "mdDrumsBank.h"
#include "mdDrumsLibrary.h"
#include "mdDrumsSampleLibrary.h"
#include "mdDrumsTelemetry.h"

#include "MachineRunner.h"
#include "TrackFx.h"

#include "jucePluginEditorLib/pluginProcessor.h"

#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace mdDrums
{
	class Controller;

	// MD Drums on the TUS plug-in stack (ADR 0001): a jucePluginEditorLib::Processor over mdDrums::Device, which speaks
	// the Machinedrum's MIDI (ADR 0002), with a Controller from parameterDescriptions_mddrums.json and an RmlUi skin.
	// Host notes 36-51 trigger Tracks 1-16; the outputs are Main, then "Out 01" to "Out 15", each optional, a Track's
	// Out parameter deciding whether it leaves Main whatever the host does with the bus.
	class Processor final : public jucePluginEditorLib::Processor
	{
	public:
		// _forTests: a configuration that is neither read nor saved, and no MCP server (which Windows would ask the
		// user's firewall about for every new test executable)
		explicit Processor(bool _forTests = false);
		~Processor() override;

		jucePluginEditorLib::PluginEditorState* createEditorState() override;
		synthLib::Device* createDevice() override;
		pluginLib::Controller* createController() override;
		// MD Drums' own Controller, the only one createController makes
		Controller& controller();

		// The host's tempo, sent to the Device as MD Drums' own message when it changes
		void processBpm(float _bpm) override;
		void saveChunkData(baseLib::BinaryStream& _s) override;
		void loadChunkData(baseLib::ChunkReader& _cr) override;

		// Where the flash image is looked for, by the Device and by the Hit screen's preview: the data folder's roms,
		// then the plug-in's own folder, as the stack gives them (UTF-8)
		std::vector<std::filesystem::path> romFolders() const;

		// The Bank and the flash image an earlier MD Drums kept in its data folder (Documents/c0remusic/MD Drums), copied
		// into _to where _to lacks them: MoDule's data folder took the new name. Nothing else moves; false when a copy
		// failed
		static bool migrateData(const std::filesystem::path& _from, const std::filesystem::path& _to);
		// Whether a Device plays, rather than the stack's silent stand-in when no flash image was found
		bool hasDevice();
		// A flash image the user chose in the editor (ticket 35): checked, the Device made again from it (with the Kit the
		// stand-in kept, if any), the parameters following the Device's Kit, then written into the data folder's roms
		// under the name MD Drums looks for. Empty when it went, else why not, for the editor.
		std::string loadFlashImage(const std::filesystem::path& _file);

		// What the editor shows of the sound, which every Device this Processor creates writes (ticket 16)
		Telemetry& getTelemetry() const { return *m_telemetry; }
		// The OS's tables the Device's Tracks' effects read, for the Filter and EQ screen; none without a Device
		std::shared_ptr<const md::engine::TrackFx::Tables> getFxTables() const { return std::atomic_load(&m_fxTables); }
		// The OS's machines (their SYN1-8 defaults, for sysex::applyKitChange), for the Hit screen's preview and the
		// Kit played; none without a Device
		using Machines = std::vector<md::engine::MachineInfo>;
		std::shared_ptr<const Machines> getMachines() const { return std::atomic_load(&m_machines); }
		// The host's tempo as last sent to the Device; 0 before the host gives one (the engine then runs at 125)
		float getBpm() const { return m_bpm.load(std::memory_order_relaxed); }
		// The Kits' Bank (ticket 10 of the editor map), "Bank.syx" in the plug-in's data folder, made with the Device; none
		// without one. A test Processor's is a file of its own in the temporary folder, removed with it.
		Bank* getBank() const { return m_bank.get(); }
		// The Library of User Machines (CONTEXT.md), "UserMachines" in the plug-in's data folder, made with the Bank
		Library* getLibrary() const { return m_library.get(); }

		// A User Machine's file (.syx or .mds) into the Library at _index (0-15, USR-01 to USR-16), then this instance's
		// Device takes it, compiled ahead (rendering pauses for that). Empty when it went, else why not.
		std::string installUserMachine(int _index, const std::vector<uint8_t>& _file);
		// The Library's copies of the User Machines _kit plays into this instance's Device, where it holds another or
		// none: what loading a Kit of the Bank does (ticket 07 of the User Machines map). A number the Library lacks
		// keeps what the Device holds.
		void takeUserMachines(const md::automation::sysex::MdKit& _kit);
		// The Library's copy of one, where the Device holds none (a Machine chosen on a Track)
		void takeUserMachineIfAbsent(int _index);
		// The Library's copy of one, replacing the Device's: the browser's explicit choice (ticket 18 of the User Machines
		// map); nothing when the Library holds none
		void takeUserMachine(int _index) { installFromLibrary(_index, true); }
		// The sample library's Sample for ROM _rom (0-47), replacing the Device's: the browser's explicit choice; nothing
		// when the library holds none or the Device plays the same
		void takeSample(int _rom);

		// An audio file (WAV, AIFF or FLAC) made a Sample for ROM-01 to ROM-48 (_rom 0-47) as a Machinedrum UW takes one
		// (readSampleFile, makeSample), kept in the sample library, then this instance's Device plays it from the next
		// blocks on. Empty when it went, else why not.
		std::string loadSampleFile(int _rom, const std::filesystem::path& _file);
		// ROM _rom back to the factory's Sample (silence on ROM-33 to ROM-48), in the library and this instance's Device
		void removeSample(int _rom);
		// The library's Samples on the ROM Machines _kit plays into this instance's Device, where it holds another or the
		// factory's: what loading a Kit of the Bank does (ticket 10 of the Samples map). A ROM the library lacks keeps
		// what the Device plays.
		void takeSamples(const md::automation::sysex::MdKit& _kit);
		// The user's Samples (ticket 10 of the Samples map), "Samples" in the plug-in's data folder, made with the Bank
		SampleLibrary* getSampleLibrary() const { return m_samples.get(); }
		// What choosing a file in the Menu or dropping it on the editor does (ticket 16 of the User Machines map, 06 of the
		// Samples map), _track (0-15) then playing it:
		// - a User Machine (.syx, .mds) goes into the Library at the number _track plays, or else the first free one
		//   (none: refused), and into this instance's Device;
		// - an audio file (WAV, AIFF, FLAC) becomes the Sample of the ROM Machine _track plays, or else of the first of
		//   ROM-33 to ROM-48 without a user's Sample (loadSampleFile).
		// The line the editor shows: what went where, or why not.
		struct Import
		{
			bool ok = false;
			int machine = -1;	// the machine id _track now plays
			std::string text;
		};
		Import importFile(const std::filesystem::path& _file, int _track);

		// The User Machine this instance's Device holds at _index (0-15), for the editor's names; none without a Device
		std::shared_ptr<const md::mds::Package> userMachine(int _index);

		// The Samples this instance's Device plays, for the Hit preview; none without a Device. On the message thread.
		std::array<std::shared_ptr<const Sample>, Samples::RomCount> samples();

	private:
		// The Library's copy into the Device; false when there is none, or the Device has the same
		bool installFromLibrary(int _index, bool _replace);
		// A Sample, or none, handed to the Device under its lock; false when it does not fit
		bool setDeviceSample(int _rom, std::shared_ptr<const Sample> _sample);
		// Main, then the 15 Outs (messages::OutCount), off until the host enables them
		static BusesProperties buses();
		bool isBusesLayoutSupported(const BusesLayout& _layouts) const override;
		// No flash image: the editor's status line says it, not the stack's message box
		void onDeviceCreationFailed(const synthLib::DeviceException& _e) override;

		std::shared_ptr<Telemetry> m_telemetry = std::make_shared<Telemetry>();
		std::shared_ptr<const md::engine::TrackFx::Tables> m_fxTables;
		std::shared_ptr<const Machines> m_machines;
		std::atomic<float> m_bpm{0.0f};
		bool m_forTests = false;
		std::unique_ptr<Bank> m_bank;
		std::unique_ptr<Library> m_library;
		std::unique_ptr<SampleLibrary> m_samples;
		std::vector<uint8_t> m_chosenFlash;		// loadFlashImage's, for createDevice, while it makes the Device
	};
}
