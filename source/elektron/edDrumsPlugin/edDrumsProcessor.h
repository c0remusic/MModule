#pragma once

#include <array>
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "edDrumsEngine.h"
#include "edDrumsKits.h"
#include "edDrumsParameters.h"
#include "edDrumsSequencer.h"

// The instrument: the engine (elektron::drums::Engine) as a plain JUCE AudioProcessor, VST3. Notes 36-51 on any channel
// hit Tracks 1-16 with their velocity, their note-off ends a held voice; outputs Main and Out 01-15, stereo. It runs at
// 44.1 kHz, the one rate both families are right at: at any other it plays silence, reporting no latency, until ticket
// 16. The Machinedrum's flash image is read once, as the instrument loads, from the variable
// GEARMULATOR_MD_FIRMWARE_BIN or the data folder's roms.
//
// The parameters of a Track, the IDs t01_*, the names "T01 ...": Machine and Chokes, which a host does not automate,
// Machine 1-8, Note, Level, Pan, Output; then Tempo; then, appended by ticket 18 (edDrumsParameters.h), each Track's
// chain, LFOs, sends and accent, the shared effects, the master and the oversampling. Every ID is frozen in
// edDrumsParameterIds.txt. The audio thread gives the engine only what changed since it last did.
//
// The Sampler (ticket 20) is the Machine list's last choice, where ROM-33 stood: ROM-33 to -48 left the list, its last
// 16, so that no other choice moved, and an older state on one of them loads the Sampler. A Track's sample is loaded on
// the message thread (loadSample), kept in the state, and reaches the audio thread through a mailbox per Track.
//
// The internal sequencer (ticket 17, edDrumsSequencer.h) runs every block from the host's transport, its Hits and
// Locks at their samples beside the host's notes. A Lock's parameter is a host parameter's index in getParameters(),
// which is the frozen ID order (the line of edDrumsParameterIds.txt, from 0), and its value the parameter's plain value,
// as the host shows it. A Lock on a parameter of its own Track: Machine 1-8 lock the Hit (Engine::lock); Note, Level,
// Pan, the chain, the LFOs, the sends and the accent take the value from the Lock's step until the Track's next Hit or
// Lock, host note or step, which give the host's value back first; a Lock without a trig does the same with Machine
// 1-8. A Lock on any other parameter does nothing. Parameter "pattern" (1-128) and a program change (0-127) ask for a
// pattern, which starts at the end of the one playing. The patterns are edited on the message thread
// (editSequencer), and the audio thread takes the copy published at its next block.
namespace elektron::drums
{
	class Processor final : public juce::AudioProcessor, juce::Timer
	{
	public:
		Processor();
		~Processor() override;

		void prepareToPlay(double _sampleRate, int _maxBlock) override;
		void releaseResources() override {}
		bool isBusesLayoutSupported(const BusesLayout& _layout) const override;
		void processBlock(juce::AudioBuffer<float>& _buffer, juce::MidiBuffer& _midi) override;
		using AudioProcessor::processBlock;

		juce::AudioProcessorEditor* createEditor() override;
		bool hasEditor() const override { return true; }

		const juce::String getName() const override { return ED_DRUMS_NAME; }
		bool acceptsMidi() const override { return true; }
		bool producesMidi() const override { return false; }
		double getTailLengthSeconds() const override { return 2.0; }

		int getNumPrograms() override { return 1; }
		int getCurrentProgram() override { return 0; }
		void setCurrentProgram(int) override {}
		const juce::String getProgramName(int) override { return {}; }
		void changeProgramName(int, const juce::String&) override {}

		void getStateInformation(juce::MemoryBlock& _data) override;
		void setStateInformation(const void* _data, int _size) override;

		static juce::File dataFolder();

		// The machines a Track's parameter chooses from, and what each has of its data (dataOf)
		const std::vector<Machine>& machines() const { return m_machines; }
		MachineData machineData(const size_t _choice) const { return dataOf(*m_library, m_machines[_choice]); }

		// The audio thread's, after prepareToPlay, for a test between blocks
		Engine* engine() { return m_engine.get(); }

		// Where an editor finds the parameters: by ID, nullptr for none; a Track's by box, or all of them; the shared
		// ones by section. Built by the constructor, read-only after
		enum class Section { Machine, Drive, Band, Filter, Amp, Lfo1, Lfo2, Sends, Effects, Master, Global };
		static constexpr int TrackSections = static_cast<int>(Section::Effects);
		juce::RangedAudioParameter* parameter(const juce::String& _id) const;
		// _track for a Track's section, ignored for a shared one
		const juce::StringArray& ids(Section _section, int _track = 0) const;
		const juce::StringArray& trackIds(const int _track) const { return m_trackIds[static_cast<size_t>(_track)]; }

		// The Sampler's index in machines()
		int samplerChoice() const { return static_cast<int>(m_machines.size()) - 1; }

		// Message thread. A Track's sample from a WAV, AIFF or FLAC file (readSampleFile): false and why in _error when
		// it cannot be read; the Track plays it on the Sampler from the next block. Its name, empty for none
		bool loadSample(int _track, const juce::File& _file, juce::String& _error);
		juce::String sampleName(int _track) const;
		void clearSample(int _track);

		// Message thread. _edit changes the message thread's copy, which the audio thread takes whole at its next block
		void editSequencer(const std::function<void(Sequencer&)>& _edit);
		const Sequencer& sequencer() const { return *m_edited; }

		// Message thread. The Kits' Bank (edDrumsKits.h): a Kit is every parameter but the Global ones (Tempo,
		// Oversampling, Pattern) and the Tracks' samples, nothing of the sequencer. Loaded, its values change as a
		// state's do, re-read by the host, none told as an edit, and a parameter it lacks takes its default. Saved, the
		// sound goes into a Slot under the Slot's name, "Kit NN" for an empty one. The Kit played is the Slot last
		// loaded or saved, -1 for none, which the state keeps; kitDiffers says whether the sound is no longer what that
		// Slot holds, which another instance may have rewritten. followKits reads again the Slots whose file changed,
		// which the timer does twice a second and each call here does first
		static constexpr int KitSlots = KitBank::Slots;
		const KitBank::Slot& kitSlot(const int _slot) const { return m_kits->slot(_slot); }
		void followKits();
		bool loadKit(int _slot);
		bool saveKit(int _slot);
		bool renameKit(int _slot, const juce::String& _name);
		bool clearKit(int _slot);
		int currentKit() const { return m_kitSlot; }
		bool kitDiffers() const;

		// Any thread: what the audio thread last played. The step under the transport, -1 stopped; the pattern playing,
		// 0-127; the Tracks whose voice sounds, a bit each; an output bus's peak, both channels, falling 20 dB/s
		int playingStep(const int _track) const { return m_playingStep[static_cast<size_t>(_track)].load(std::memory_order_relaxed); }
		int currentPattern() const { return m_currentPattern.load(std::memory_order_relaxed); }
		uint32_t liveTracks() const { return m_liveTracks.load(std::memory_order_relaxed); }
		float outputPeak(const int _bus) const { return m_outputPeak[static_cast<size_t>(_bus)].load(std::memory_order_relaxed); }

	private:
		// A parameter and the value the engine last got from it, normalised: due, it gets it at the next block
		struct Watched
		{
			juce::RangedAudioParameter* parameter = nullptr;
			float applied = 0;
			bool due = true;

			bool changed()
			{
				const float v = parameter->getValue();
				if(!due && v == applied)
					return false;
				due = false;
				applied = v;
				return true;
			}
			float value() const { return parameter->convertFrom0to1(applied); }
			int integer() const { return juce::roundToInt(value()); }
		};

		struct TrackParameters
		{
			juce::AudioParameterChoice* machine = nullptr;
			std::array<juce::AudioParameterInt*, 8> values{};
			juce::AudioParameterInt* note = nullptr;
			juce::AudioParameterInt* level = nullptr;
			juce::AudioParameterInt* pan = nullptr;
			juce::AudioParameterInt* output = nullptr;
			juce::AudioParameterInt* chokes = nullptr;

			std::array<Watched, parameters::ChainValues> chain{};
			std::array<std::array<Watched, parameters::LfoValues>, 2> lfo{};
			std::array<Watched, SendCount> sends{};
			Watched accent;
		};

		// What a host parameter is to a Lock of the sequencer's: whose Track, and which of its values
		enum class LockKind : uint8_t { None, Machine, Note, Level, Pan, Chain, Lfo, Send, Accent };
		struct LockTarget
		{
			int track = -1;
			LockKind kind = LockKind::None;
			int a = 0, b = 0;	// Machine 1-8's, the chain's or the send's index; the LFO and its value's
		};

		// A sample on its way to the audio thread, numbered in the order published
		struct SampleMail
		{
			Sample sample;
			uint32_t serial = 0;
		};

		static BusesProperties buses();
		void timerCallback() override;
		void buildLockTargets();
		bool held(const juce::AudioProcessorParameter* _parameter) const
		{
			return m_locked[static_cast<size_t>(_parameter->getParameterIndex())] != 0;
		}
		float lockOr(const Watched& _watched) const;
		parameters::ChainArray chainValues(int _track) const;
		parameters::LfoArray lfoValues(int _track, int _lfo) const;
		void setChainHeld(int _track);
		void applyTarget(int _parameter);
		void releaseLocks(int _track);
		void applyEvent(const Event& _event);
		void publishSequencer();
		void publishSample(int _track);
		void pruneSamples();
		bool isKitParameter(const int _index) const { return m_kitParameter[static_cast<size_t>(_index)] != 0; }
		void writeSound(juce::XmlElement& _xml, bool _all) const;
		void readSound(const juce::XmlElement& _xml, bool _all);
		void rememberKit(const juce::XmlElement* _kit);
		void sync(bool _hostIsTruth);
		void syncShared();
		template<typename P> P* add(P* _parameter, Section _section, int _track = -1);
		void watch(Watched& _watched, const parameters::Spec& _spec, const juce::String& _id, const juce::String& _name,
			int _default, Section _section, int _track = -1);

		std::unique_ptr<Library> m_library;
		std::vector<Machine> m_machines;	// Machine's choices
		std::array<TrackParameters, Tracks> m_tracks{};
		juce::AudioParameterFloat* m_tempo = nullptr;
		std::array<float*, 2 * Outputs> m_outputs{};

		// The shared effects, the master and the oversampling, each byte in its setter's order
		std::array<Watched, 2> m_sources{};
		std::array<Watched, SendCount> m_sendOutputs{};
		std::array<Watched, 8> m_echo{}, m_gateBox{};
		std::array<Watched, StDelayBytes> m_syntaktDelay{};
		std::array<Watched, StReverbBytes> m_syntaktReverb{};
		Watched m_syntaktDelaySend, m_delayFree;
		std::array<Watched, ChorusBytes> m_chorus{};
		std::array<Watched, PhaserBytes> m_phaser{};
		std::array<Watched, MasterBytes> m_master{};
		Watched m_masterOn, m_oversampling;
		std::vector<Watched*> m_watched;	// all of them, made due for a new engine

		std::map<juce::String, juce::RangedAudioParameter*> m_byId;
		std::array<std::array<juce::StringArray, TrackSections>, Tracks> m_trackSections;
		std::array<juce::StringArray, 3> m_sharedSections;
		std::array<juce::StringArray, Tracks> m_trackIds;
		std::array<Family, Tracks> m_chainFamily{};	// the family whose chain the parameters took last
		// The audio thread's: the family whose chain the engine loaded last, as Engine's, and the chain it holds
		std::array<Family, Tracks> m_engineFamily{};
		std::array<parameters::ChainArray, Tracks> m_chainHeld{};

		std::unique_ptr<Engine> m_engine;	// the audio thread's after prepareToPlay, none at a rate it does not run at
		std::array<int, Tracks> m_machineSeen{};	// Machine's choice whose defaults the Track's parameters took
		std::atomic<uint32_t> m_stateSerial{0};
		uint32_t m_stateSeen = 0;
		std::atomic<bool> m_reread{false};	// the audio thread changed parameters: the host reads them again
		juce::AudioParameterInt* m_pattern = nullptr;

		// Each Track's sample, the message thread's; the mail the audio thread takes at its next block, and the serial of
		// the last it gave the engine; what the message thread published that the audio thread may still read
		std::array<std::unique_ptr<Sample>, Tracks> m_samples;
		std::array<std::atomic<SampleMail*>, Tracks> m_sampleMail{};
		std::array<std::atomic<uint32_t>, Tracks> m_sampleDone{};
		std::array<std::vector<std::unique_ptr<SampleMail>>, Tracks> m_sampleFlight;
		uint32_t m_sampleSerial = 0;

		// The sequencer: the message thread's copy, and three that change hands without a lock, a triple buffer: the
		// audio thread plays m_playing, the message thread fills m_spare, m_middle holds the third, Fresh when published
		static constexpr int Fresh = 4;
		std::unique_ptr<Sequencer> m_edited;
		std::array<std::unique_ptr<Sequencer>, 3> m_sequencers;
		int m_playingIndex = 0, m_spare = 1;
		std::atomic<int> m_middle{2};
		Events m_events;
		int m_patternAsked = 0;

		// The Kits: by parameter index, whether a Kit holds it; the Bank; the Slot played and what it held when the Kit
		// played was loaded, saved or rewritten, normalised values by parameter index and samples by Track
		std::vector<uint8_t> m_kitParameter;
		std::unique_ptr<KitBank> m_kits;
		int m_kitSlot = -1;
		bool m_kitHeld = false;		// m_kitValues and m_kitSamples hold the Slot's Kit: false for an empty Slot
		std::vector<float> m_kitValues;
		std::array<std::unique_ptr<Sample>, Tracks> m_kitSamples;
		int m_kitFollow = 0;		// the timer's ticks since it last followed the Bank

		// The audio thread's Locks: by parameter index, held and the value; by Track, the ones it holds
		std::vector<LockTarget> m_lockTargets;
		std::vector<uint8_t> m_locked;
		std::vector<float> m_lockValue;
		std::array<std::vector<int>, Tracks> m_lockedBy;	// reserved: no allocation on the audio thread

		// The audio thread's transport at the block, from sync
		double m_blockBpm = 120;
		bool m_blockPlaying = false;
		double m_blockQuarters = 0;

		std::array<std::atomic<int>, Tracks> m_playingStep{};
		std::atomic<int> m_currentPattern{0};
		std::atomic<uint32_t> m_liveTracks{0};
		std::array<std::atomic<float>, Outputs> m_outputPeak{};
	};
}
