#pragma once

#include "mdDrumsEngine.h"
#include "mdDrumsMessages.h"
#include "mdDrumsTelemetry.h"

#include "synthLib/device.h"

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace md::automation::sysex
{
	struct KitChange;
}

namespace mdDrums
{
	// MD Drums' synthLib::Device: a Machinedrum seen through its MIDI (ADR 0002), the engines without the OS behind it.
	// It takes notes 36-51 on any channel from anyone, Locks as notes 64-87 on their Track's channel or, on channel 1,
	// for the Hit just before them at the same sample (ADR 0003, amended), an accent and a slide the same way (notes 88
	// to 114), and from the editor (source Editor) the Machinedrum's CCs on channels 1 to 4 (24 parameters, level,
	// mute), ASSIGN MACHINE ($5B), the master effects
	// ($5D-$60), SET LFO PARAM ($62), SET TRIG GROUP and SET MUTE GROUP ($65, $66) and Kit dumps ($52), plus MD Drums' own
	// messages (Solo, Out, tempo, mixer). What the host sends besides notes is ignored. It renders synchronously, cutting
	// its block at each event's offset, and holds everything the sound depends on: its state is its Kit and its mixer.
	class Device final : public synthLib::Device
	{
	public:
		static constexpr int FirstNote = 36;
		static constexpr int FirstLockNote = 64;	// SYN1 to LFOM, the engine's parameter order; velocity: lockValue()
		// After the Locks: accents the Hit it goes with, as a Lock goes with it; velocity: the accent amount, lockValue()
		static constexpr int AccentNote = 88;
		// Then slides the Hit it goes with (ADR 0003, amended 2026-10-19): velocity the 24-ppqn clocks to its Track's next
		// Hit (Engine::slide); from FirstSlideTargetNote, in the Locks' order, what that next Hit locks, lockValue(); and
		// past 127 clocks, SlideExtensionNote's velocity times 127 more. The host transport's clocks step the slides.
		static constexpr int SlideNote = 89;
		static constexpr int FirstSlideTargetNote = 90;
		static constexpr int SlideExtensionNote = 114;
		// md-drums-seq's hello (KitFiles): note-ons 124 to 127 in that order, on any channel, each velocity 1 + six
		// bits of the device's id, the most significant first (helloVelocity); another note-on between them cancels
		// it. They play nothing.
		static constexpr int FirstHelloNote = 124;
		static constexpr int HelloNotes = 4;
		static constexpr uint8_t helloVelocity(const uint32_t _id, const int _note)
		{
			return static_cast<uint8_t>(1 + (_id >> (6 * (HelloNotes - 1 - _note)) & 0x3f));
		}
		// Main left and right, then Out 01 to 15, each the sum of the Tracks the mixer puts on it (messages::OutCount)
		static constexpr int OutCount = messages::OutCount;
		static constexpr int OutputCount = 2 + OutCount;
		// Every Hit sounds Engine::hitDelay() samples after its note, on Main and on its Out, whatever its machine
		// (ticket 36): the 31 samples of a block, the latest voice's start (TRX-BD's, which sounds a block after its
		// trigger), ANTI-CLICK's wait (0 by default), Dynamix's look-ahead. That is the latency reported
		// (getInternalLatencyMidiToOutput).
		// X.20's ANTI-CLICK, 0, 64 or 128 samples (Engine::setFade): the latency grows as much. Between blocks only: the
		// caller holds the Device (Plugin::withDeviceLocked), then has the Plugin read its latency again.
		void setAntiClick(int _samples) { m_engine->setFade(_samples); }
		int antiClick() const { return m_engine->fade(); }

		// _params.romData: the 8 MB Machinedrum UW flash image with OS 1.63. Throws synthLib::DeviceException without
		// a usable one. _telemetry takes its outputs' peaks and its Hits (none without it); the Device shares it, so that it
		// stays valid while the Device lives, whoever goes first.
		// _x20Voice: MDX X.20's voice DSP program from the user's X.20 OS (md::fw::parseX20Os), whose six built-in Machines
		// the Device then plays (Engine::installX20); none without
		explicit Device(const synthLib::DeviceCreateParams& _params, std::shared_ptr<Telemetry> _telemetry = {},
			std::shared_ptr<const md::fw::DspImage> _x20Voice = {});
		bool playsX20() const { return m_engine->playsX20(4); }
		~Device() override;

		float getSamplerate() const override { return static_cast<float>(Engine::SampleRate); }
		bool isValid() const override { return m_valid; }
#if SYNTHLIB_DEMO_MODE == 0
		bool getState(std::vector<uint8_t>& _state, synthLib::StateType _type) override;
		bool setState(const std::vector<uint8_t>& _state, synthLib::StateType _type) override;
#endif
		uint32_t getChannelCountIn() override { return 0; }
		uint32_t getChannelCountOut() override { return OutputCount; }
		bool setDspClockPercent(uint32_t) override { return false; }
		uint32_t getDspClockPercent() const override { return 100; }
		uint64_t getDspClockHz() const override { return 0; }

		uint32_t getDefaultLatencyBlocks() const override { return 0; }
		uint32_t getInternalLatencyMidiToOutput() const override
		{
			return static_cast<uint32_t>(m_engine->hitDelay());
		}

		void process(const synthLib::TAudioInputs& _inputs, const synthLib::TAudioOutputs& _outputs, size_t _size,
			const std::vector<synthLib::SMidiEvent>& _midiIn, std::vector<synthLib::SMidiEvent>& _midiOut) override;

		const md::automation::sysex::MdKit& kit() const { return m_kit; }
		const messages::Mixer& mixer() const { return m_mixer; }
		// The OS's tables the Tracks' effects read (Engine::fxTables), for the editor's Filter and EQ screen
		std::shared_ptr<const md::engine::TrackFx::Tables> fxTables() const { return m_engine->fxTables(); }
		// The OS's machines: their names, SYN1-8 names and defaults
		const std::vector<md::engine::MachineInfo>& machines() const { return m_engine->machines(); }

		// This Device's own copy of a User Machine (0-15, USR-01 to USR-16), installed and compiled now (Engine::
		// installUserMachine). Never while process runs: the caller holds the Device (Plugin::withDeviceLocked). Throws,
		// installing nothing, on a package the voice DSP cannot play.
		void installUserMachine(int _index, std::shared_ptr<const md::mds::Package> _package)
		{
			m_engine->installUserMachine(_index, std::move(_package));
			m_kept[static_cast<size_t>(_index)].reset();
		}
		std::shared_ptr<const md::mds::Package> userMachine(int _index) const { return m_engine->userMachine(_index); }
		// A User Machine a set holds that this voice DSP does not play (ticket 23 of the User Machines map): its package
		// as the set saved it, which the state keeps for a build that plays it, while its number plays the empty machine;
		// what it is and why it does not play. A state restored or a User Machine installed at its number lets it go.
		struct Kept
		{
			std::vector<uint8_t> package;
			std::string name;
			std::string reason;
		};
		const std::optional<Kept>& keptUserMachine(const int _index) const { return m_kept[static_cast<size_t>(_index)]; }

		// This Device's own copy of a user's Sample on ROM-01 to ROM-48 (_rom 0-47), or none for the factory's (Engine::
		// setSample): written into the voice DSP over the next blocks. Never while process runs: the caller holds the
		// Device. False, changing nothing, when the user's Samples would not fit together.
		bool setSample(int _rom, std::shared_ptr<const Sample> _sample) { return m_engine->setSample(_rom, std::move(_sample)); }
		std::shared_ptr<const Sample> sample(int _rom) const { return m_engine->sample(_rom); }
		size_t sampleRoom() const { return m_engine->sampleRoom(); }
		const std::vector<uint16_t>& sampleCodes() const { return m_engine->sampleCodes(); }

	protected:
		// Nothing: process itself puts what the Device says back into its _midiOut (reportMachines)
		void readMidiOut(std::vector<synthLib::SMidiEvent>&) override {}
		// Nothing: process replaces the base's loop, the only caller of these two
		void processAudio(const synthLib::TAudioInputs&, const synthLib::TAudioOutputs&, size_t) override {}
		bool sendMidi(const synthLib::SMidiEvent&, std::vector<synthLib::SMidiEvent>&) override { return false; }

	private:
		void apply(const synthLib::SMidiEvent& _event);
		void applyControlChange(const synthLib::SMidiEvent& _event);
		void hello(int _note, uint8_t _velocity);
		void applySysex(const synthLib::SMidiEvent& _event);
		void applyKit();
		void applyChange(const md::automation::sysex::KitChange& _change);
		void reportMachines(std::vector<synthLib::SMidiEvent>& _midiOut);
		void applyMixer();
		void render(const synthLib::TAudioOutputs& _outputs, size_t _offset, size_t _count);
		void publishPlayed();
		void capture(size_t _count);

		std::unique_ptr<Engine> m_engine;
		std::shared_ptr<Telemetry> m_telemetry;
		bool m_valid = false;

		// The Hit captures being written (Telemetry::beginCapture): from the sample a Hit sounds, a column of 8 samples at
		// a time, until the capture is full or the Track's next Hit starts another
		struct Capture
		{
			bool active = false;
			int64_t start = 0;	// the sample the Hit sounds
			Telemetry::ColumnMaker column;
		};
		std::array<Capture, Engine::TrackCount> m_captures{};
		// Each Track's own output, a chunk at a time: the Telemetry and the captures read it, the Outs sum it
		static constexpr size_t Chunk = 256;
		std::array<std::array<float, Chunk>, Engine::TrackCount> m_tracks{};
		int64_t m_position = 0;	// samples rendered so far
		// The last channel-1 Hit note's Track and the sample it came at: a Lock on channel 1 at that sample goes with it
		int m_lastHitTrack = 0;
		int64_t m_lastHitPosition = -1;
		std::array<std::optional<Kept>, 16> m_kept{};
		// md-drums-seq's hello under way: the notes taken so far and their bits
		int m_helloNotes = 0;
		uint32_t m_helloId = 0;
		md::automation::sysex::MdKit m_kit;
		messages::Mixer m_mixer;

		// The Tracks whose new machine's 24 parameters (sysex::applyKitChange) the Device says back as CCs, for the
		// editor, once the block is done
		// (the values the block left, should the editor have set some after the machine)
		uint16_t m_machinesToReport = 0;
	};
}
