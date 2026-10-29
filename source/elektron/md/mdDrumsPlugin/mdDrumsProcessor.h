#pragma once

#include "mdDrumsBank.h"
#include "mdDrumsDevice.h"
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
#include <utility>
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
		// The user's MDX X.20 OS (the map of X.20's built-in Machines): checked (md::fw::parseX20Os), written into the data
		// folder's roms as X20OsName, then the Device made again, playing X.20's six built-in Machines. Empty when it
		// went, else why not, for the editor. Each Device this Processor makes finds it there.
		static constexpr const char* X20OsName = "mdx_x20_os.syx";
		std::string loadX20Os(const std::filesystem::path& _file);
		// Whether the Device plays X.20's built-in Machines
		bool playsX20();
		// X.20's voice DSP program the Device plays them with, for the Hit preview; none when it does not play them
		std::shared_ptr<const md::fw::DspImage> playedX20Voice() { return playsX20() ? m_x20Voice : nullptr; }

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

		// A User Machine's file (.syx or .mds) into the Library, then this instance's Device takes it at _index (0-15,
		// USR-01 to USR-16), compiled ahead (rendering pauses for that), no Track changed. Empty when it went, else why
		// not.
		std::string installUserMachine(int _index, const std::vector<uint8_t>& _file);
		// _track (0-15) put on a User Machine of the Library (ticket 25 of the User Machines map), at a number of the Kit
		// played that MoDule gives: the one that holds it, else the one _track plays if no other Track plays it, else the
		// first that no Track plays, an empty one first (a Kit has 16 Tracks: there is always one). The Device takes its
		// copy there, and _track goes to it by ASSIGN MACHINE, its 24 parameters the package's defaults. The machine id,
		// or -1 and why not.
		std::pair<int, std::string> chooseUserMachine(int _track, const std::shared_ptr<const md::mds::Package>& _package);
		// The User Machines the Kit played plays at its numbers, as the Bank keeps them with it (ticket 27 of the User
		// Machines map); on the message thread
		Bank::Machines kitMachines();
		// A Kit of the Bank's User Machines into this instance's Device at their numbers, from the Library: the names of
		// those the Library lacks, whose numbers keep what the Device holds
		std::vector<std::string> takeUserMachines(const Bank::Machines& _machines);
		// The sample library's Sample for ROM _rom (0-47), replacing the Device's: the browser's explicit choice; nothing
		// when the library holds none or the Device plays the same
		void takeSample(int _rom);

		// An audio file (WAV, AIFF or FLAC) made a Sample for ROM-01 to ROM-48 (_rom 0-47) as a Machinedrum UW takes one
		// (readSampleFile, makeSample), kept in the sample library, then this instance's Device plays it from the next
		// blocks on. Empty when it went, else why not.
		std::string loadSampleFile(int _rom, const std::filesystem::path& _file);
		// ROM _rom back to the factory's Sample (silence on ROM-33 to ROM-48), in the library and this instance's Device
		void removeSample(int _rom);
		// What the user changed of the set's state outside the host parameters (a Sample, a User Machine, a Kit's Links,
		// Chokes or name): the host is asked to mark the set modified (VST3 IComponentHandler2::setDirty), and told
		// nothing else; ticket 11 of the Samples map. Never at load, as nothing is told at load (ticket 34 of the editor
		// map). On the message thread.
		void markStateChanged();
		// The library's Samples on the ROM Machines _kit plays into this instance's Device, where it holds another or the
		// factory's: what loading a Kit of the Bank does (ticket 10 of the Samples map). A ROM the library lacks keeps
		// what the Device plays.
		void takeSamples(const md::automation::sysex::MdKit& _kit);
		// The user's Samples (ticket 10 of the Samples map), "Samples" in the plug-in's data folder, made with the Bank
		SampleLibrary* getSampleLibrary() const { return m_samples.get(); }
		// What choosing a file in the Menu or dropping it on the editor does (ticket 16 of the User Machines map, 06 of the
		// Samples map), _track (0-15) then playing it:
		// - a User Machine (.syx, .mds) goes into the Library, unless it holds it, then _track plays it
		//   (chooseUserMachine);
		// - an audio file (WAV, AIFF, FLAC) becomes the Sample of the ROM Machine _track plays, or else of the first of
		//   ROM-33 to ROM-48 without a user's Sample (loadSampleFile).
		// The line the editor shows: what went where, or why not.
		enum class ImportKind
		{
			None,
			Audio,			// WAV, AIFF, FLAC
			UserMachine,	// .syx, .mds
			X20Os			// a .syx of an OS's size: MDX X.20's OS (loadX20Os)
		};
		// What a file is to importFile, by its extension: what the editor's drop targets take
		static ImportKind importKind(const std::filesystem::path& _file);

		struct Import
		{
			bool ok = false;
			int machine = -1;	// the machine id _track now plays
			std::string text;
			std::string label;	// what went in: "ROM-33", "TX8-BD"
		};
		Import importFile(const std::filesystem::path& _file, int _track);

		// The User Machine this instance's Device holds at _index (0-15), for the editor's names; none without a Device
		std::shared_ptr<const md::mds::Package> userMachine(int _index);
		// The first User Machine the set holds that this build does not play (Device::keptUserMachine), for the status
		// line: its number (0-15) and what it is, or -1 and none; valid until the next state restored
		std::pair<int, const Device::Kept*> keptUserMachine();

		// X.20's ANTI-CLICK (ticket 33 of the User Machines map): 0 (off), 64 or 128 samples, one setting for every
		// MoDule as X.20's is machine-wide (the plug-in's configuration, read when a Device is made); the latency
		// reported grows as much. On the message thread.
		static constexpr const char* AntiClickKey = "antiClick";
		int getAntiClick();
		void setAntiClick(int _samples);
		// What this instance's Device plays: the configuration's when it was made or set here, which another instance
		// may have changed since; what the menu ticks
		int antiClickPlayed() const { return m_antiClickPlayed; }

		// The Samples this instance's Device plays, for the Hit preview; none without a Device. On the message thread.
		std::array<std::shared_ptr<const Sample>, Samples::RomCount> samples();

	private:
		// _package into the Device at _index (0-15), under its lock, unless it holds the same: whether it took it, or why
		// not
		struct Taken
		{
			bool changed = false;
			std::string error;
		};
		Taken installPackage(int _index, const std::shared_ptr<const md::mds::Package>& _package);
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
		int m_antiClickPlayed = 0;
		std::unique_ptr<Bank> m_bank;
		std::unique_ptr<Library> m_library;
		// kitMachines' ids, by the package they were taken from: a frame of the head band reads them ten times a second
		std::array<std::pair<const md::mds::Package*, uint64_t>, Bank::MachineCount> m_machineIds{};
		std::unique_ptr<SampleLibrary> m_samples;
		std::vector<uint8_t> m_chosenFlash;		// loadFlashImage's, for createDevice, while it makes the Device
		// X.20's voice DSP program from the OS kept in the roms, read once; createDevice's
		std::shared_ptr<const md::fw::DspImage> m_x20Voice;
		bool m_x20Read = false;
		std::shared_ptr<const md::fw::DspImage> x20Voice();
	};
}
