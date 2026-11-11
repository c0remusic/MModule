#pragma once

#include <array>
#include <atomic>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "edDrumsElektronImport.h"
#include "edDrumsEngine.h"
#include "edDrumsKits.h"
#include "edDrumsParameters.h"
#include "edDrumsSampleLibrary.h"
#include "edDrumsSequencer.h"
#include "edDrumsTrackSounds.h"

// The instrument: the engine (elektron::drums::Engine) as a plain JUCE AudioProcessor, VST3. Notes 36-51 on any channel
// hit Tracks 1-16 with their velocity, their note-off ends a held voice; outputs Main and Out 01-15, stereo. It runs at
// 44.1 kHz, the one rate both families are right at: at any other it plays silence, reporting no latency, until ticket
// 16. As the instrument loads it reads the Machinedrum's words an import kept in the data folder
// (edDrumsElektronImport.h), else its flash image, from the variable GEARMULATOR_MD_FIRMWARE_BIN or the data folder's roms.
//
// The parameters of a Track, the IDs t01_*, the names "T01 ...": Machine and Chokes, which a host does not automate,
// Machine 1-8, Note, Level, Pan, Output; then Tempo; then, appended by ticket 18 (edDrumsParameters.h), each Track's
// chain, LFOs, sends and accent, the shared effects, the master and the oversampling; then Pattern; then, appended by
// ticket 26, each Track's Machine 9-24, which a machine of 8 does not use. Every ID is frozen in
// edDrumsParameterIds.txt. The audio thread gives the engine only what changed since it last did.
//
// The Sampler (ticket 20) is the Machine list's choice where ROM-33 stood: ROM-33 to -48 left the list, its last 16,
// and an older state on one of them loads the Sampler. OPEN DRUM's four machines follow it (ticket 26). Since state
// version 4 and Kit version 2 the list holds none of the Machinedrum's INP, CTR, MID and RAM machines: an older state's
// or Kit's choice is read in the list it was written with, a machine no longer offered loading as Off. A Track's sample
// is loaded on the message thread (loadSample), kept in the state, and reaches the audio thread through a mailbox per
// Track.
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

		// The Sampler's index in machines(), OPEN DRUM's four after it, then SOPHIE
		int samplerChoice() const { return static_cast<int>(m_machines.size()) - 1 - OpenDrumMachines - SophieMachines; }
		// The rate _track's chain runs at: the Syntakt's for a Track that took its chain, the host's 44.1 kHz otherwise
		double chainRate(const int _track) const
		{
			return m_chainFamily[static_cast<size_t>(_track)] == Family::Syntakt ? syntakt::fast::MachineRate : SampleRate;
		}
		int sophieChoice() const { return static_cast<int>(m_machines.size()) - SophieMachines; }

		// Message thread. One of SOPHIE's 16 factory sounds (sophie::soundValues) on _track's Machine 1-20, each an edit
		// the host is told of, as the editor's own. The sound the Track's Machine 1-20 hold, -1 for none
		void setSophieSound(int _track, int _sound);
		int sophieSound(int _track) const;

		// Message thread. A Track's sample from a WAV, AIFF or FLAC file (readSampleFile): false and why in _error when
		// it cannot be read; the Track plays it on the Sampler from the next block. Its name, empty for none
		bool loadSample(int _track, const juce::File& _file, juce::String& _error);
		juce::String sampleName(int _track) const;
		void clearSample(int _track);

		// Message thread. The samples' Library (edDrumsSampleLibrary.h), which the timer follows. A Library sample on a
		// Track by its fingerprint, as loadSample loads its file. A Track's sample's fingerprint, which the state and a
		// Kit keep beside its copy: empty for none
		SampleLibrary& sampleLibrary() { return *m_sampleLibrary; }
		bool loadLibrarySample(int _track, const juce::String& _fingerprint, juce::String& _error);
		juce::String sampleFingerprint(const int _track) const { return m_sampleRefs[static_cast<size_t>(_track)]; }

		// Message thread. "Import my Elektron content" (edDrumsElektronImport.h) from _folder: false while one runs. The
		// timer takes its report, gives the samples' Library the folder when it holds samples the Library lacks, and
		// counts the reports taken. The Library of an image's words imported plays from the instrument's next load.
		// libraryFromWords: the engine plays the words imported
		bool importElektron(const juce::File& _folder);
		bool importing() const { return m_import->running(); }
		const ElektronImport::Report& lastImport() const { return m_lastImport; }
		uint32_t imports() const { return m_imports; }
		bool libraryFromWords() const { return m_libraryFromWords; }

		// Message thread. The audition: _file read as the Sampler reads it, played once on Main as it is, through no
		// Track, from the next block; false and why when it cannot be read. What it played last, nullptr for none
		bool auditionSample(const juce::File& _file, juce::String& _error);
		const Sample* auditioned() const { return m_auditioned.get(); }

		// Message thread. _edit changes the message thread's copy, which the audio thread takes whole at its next block
		void editSequencer(const std::function<void(Sequencer&)>& _edit);
		const Sequencer& sequencer() const { return *m_edited; }

		// LIVE (spec 7), not kept in the state. While it is on and the transport plays, a host's note of a Track becomes a
		// trig of the pattern playing, at the step nearest with the micro-timing that puts it where it was played
		// (Sequencer::nearestStep), and a Track's parameter moved in a gesture becomes a Lock of the step playing. The
		// timer writes them into the patterns (takeLive, which a test calls)
		void setLive(const bool _on) { m_live.store(_on); }
		bool live() const { return m_live.load(); }
		void takeLive();
		// The chain asked for (Sequencer::requestChain), from the next block
		void requestChain() { m_chainAsked.store(true); }
		// Any thread: the chain's entry playing, -1 for none, and its pass; fill playing, held or the chain entry's
		int chainSlot() const { return m_chainSlot.load(std::memory_order_relaxed); }
		// The step playing counted from the pattern's start, past the Track's length, -1 stopped (Sequencer::stepSinceStart)
		int playingRowStep(const int _track) const { return m_playingRowStep[static_cast<size_t>(_track)].load(std::memory_order_relaxed); }
		int chainPass() const { return m_chainPass.load(std::memory_order_relaxed); }
		bool fillPlaying() const { return m_fillPlaying.load(std::memory_order_relaxed); }
		// One pass of a pattern as a Standard MIDI File (Sequencer::patternMidi), with the sequencer's seed
		bool exportPatternMidi(int _pattern, const juce::File& _file) const;

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
		// A Slot's Kit to a file the user chose, as the Slot's file is; a Kit file into a Slot, under its own name
		bool exportKit(int _slot, const juce::File& _file);
		bool importKit(int _slot, const juce::File& _file);

		// Message thread. A pattern's Kit at its start (Pattern::kit), -1 to keep the Kit playing. The Kits patterns
		// load are read from the Bank ahead, here, as a state loads and as the Slots' files change (prepareKits), so that
		// the audio thread loads one where its pattern starts, at that sample, without a file or an allocation; the
		// message thread then takes it as the Kit played
		void setPatternKit(int _pattern, int _slot);
		int patternKit(const int _pattern) const { return m_edited->pattern(_pattern).kit; }
		// A pattern to a file and back, its Locks and its Kit at its start included
		bool exportPattern(int _pattern, const juce::File& _file) const;
		bool importPattern(int _pattern, const juce::File& _file);

		// Message thread. The Track sounds' Library (edDrumsTrackSounds.h), which the timer follows. Saved from any
		// Track, loaded on any, as a Kit loads: re-read by the host, none told as an edit. The audition renders the
		// sound's Hit on an engine of its own and plays it as a sample's audition plays
		TrackSounds& trackSounds() { return *m_trackSounds; }
		juce::File saveTrackSound(int _track, const juce::String& _name);
		bool loadTrackSound(int _track, const juce::File& _file);
		bool auditionTrackSound(const juce::File& _file);

		// Any thread: what the audio thread last played. The step under the transport, -1 stopped; the pattern playing,
		// 0-127; the Tracks whose voice sounds, a bit each; an output bus's peak, both channels, falling 20 dB/s
		int playingStep(const int _track) const { return m_playingStep[static_cast<size_t>(_track)].load(std::memory_order_relaxed); }
		int currentPattern() const { return m_currentPattern.load(std::memory_order_relaxed); }
		uint32_t liveTracks() const { return m_liveTracks.load(std::memory_order_relaxed); }
		float outputPeak(const int _bus) const { return m_outputPeak[static_cast<size_t>(_bus)].load(std::memory_order_relaxed); }
		// A send's return's peak, as outputPeak; the master's gain reduction in dB, 0 or under, 0 while it is off
		float returnPeak(const int _send) const { return m_returnPeak[static_cast<size_t>(_send)].load(std::memory_order_relaxed); }
		float masterReduction() const { return m_reduction.load(std::memory_order_relaxed); }

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
			std::array<juce::AudioParameterInt*, MachineParameters> values{};	// Machine 1-24
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
			uint64_t hash = 0;		// sampleHash's
		};

		// A Kit of the Bank read ahead for a pattern's start (setPatternKit): its values normalised by parameter index (a
		// Kit's parameters alone count), its samples and what the message thread takes of it once the audio thread has
		// loaded it. The audio thread reads one only within a block, the message thread frees it after
		struct PreparedKit
		{
			int slot = -1;
			juce::Time written;
			int64_t size = -1;
			std::vector<float> values;
			std::array<std::unique_ptr<Sample>, Tracks> samples;
			std::array<uint64_t, Tracks> sampleHash{};
			std::array<juce::String, Tracks> refs;
			std::unique_ptr<juce::XmlElement> xml;
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
		void endSlide(int _track);
		void stepSlide(int _track);
		void applyEvent(const Event& _event, int64_t _at);
		void publishSequencer();
		void publishSample(int _track);
		void pruneSamples();
		bool isKitParameter(const int _index) const { return m_kitParameter[static_cast<size_t>(_index)] != 0; }
		void writeSound(juce::XmlElement& _xml, bool _all) const;
		void readSound(const juce::XmlElement& _xml, bool _all);
		void rememberKit(const juce::XmlElement* _kit);
		std::vector<float> kitValuesOf(const juce::XmlElement& _kit) const;
		void prepareKits(uint64_t _changed);
		void takeKitSwitch();
		void switchKit(const PreparedKit& _kit);
		std::unique_ptr<juce::XmlElement> trackSoundOf(int _track) const;
		void sync(bool _hostIsTruth);
		void syncSound(bool _hostIsTruth);
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
		Watched m_delaySync, m_delayNote, m_dp4SampleHold;
		std::array<Watched, 2> m_lfoSync{}, m_lfoNote{};	// the chorus's, the phaser's
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
		std::array<juce::String, Tracks> m_sampleRefs;	// the fingerprints
		// The values the engine's VINTAGE grain reads where they lie (Sample::source): the message thread holds every one it
		// may be given (a mail's, a prepared Kit's, the Tracks') and drops one once nothing else holds it; the audio thread
		// holds those it gave the engine and lets go of one there, which never frees it
		std::vector<std::shared_ptr<const std::vector<int16_t>>> m_heldSources;
		std::array<std::shared_ptr<const std::vector<int16_t>>, Tracks> m_engineSources;
		void holdSource(const Sample* _sample);
		void giveSample(int _track, const Sample* _sample);
		std::unique_ptr<SampleLibrary> m_sampleLibrary;
		std::unique_ptr<ElektronImport> m_import;
		ElektronImport::Report m_lastImport;
		uint32_t m_imports = 0;
		bool m_libraryFromWords = false;

		// The audition, the same way: the message thread's copy, the mail; the serial of the one the audio thread plays,
		// whose mail stays until a later one is taken; where it stands, nullptr when it has ended
		std::unique_ptr<Sample> m_auditioned;
		std::atomic<SampleMail*> m_auditionMail{nullptr};
		std::atomic<uint32_t> m_auditionHeld{0};
		std::vector<std::unique_ptr<SampleMail>> m_auditionFlight;
		const Sample* m_auditionPlaying = nullptr;
		double m_auditionAt = 0;

		// The sequencer: the message thread's copy, and three that change hands without a lock, a triple buffer: the
		// audio thread plays m_playing, the message thread fills m_spare, m_middle holds the third, Fresh when published
		static constexpr int Fresh = 4;
		std::unique_ptr<Sequencer> m_edited;
		std::array<std::unique_ptr<Sequencer>, 3> m_sequencers;
		int m_playingIndex = 0, m_spare = 1;
		std::atomic<int> m_middle{2};
		Events m_events;
		int m_patternAsked = 0;

		// LIVE: the notes the audio thread recorded, for the timer; the Locks the message thread's gestures recorded;
		// the gestures under way by parameter index
		struct LiveNote
		{
			uint8_t pattern, track, step, velocity;
			int8_t timing;
		};
		static constexpr int LiveNotes = 256;
		std::atomic<bool> m_live{false}, m_chainAsked{false};
		juce::AbstractFifo m_liveFifo{LiveNotes};
		std::array<LiveNote, LiveNotes> m_liveNotes{};
		struct LiveLock
		{
			int pattern, track, step, parameter;
			float value;
		};
		std::vector<LiveLock> m_liveLocks;
		std::vector<int> m_gestures;
		struct LiveListener final : juce::AudioProcessorListener
		{
			Processor& processor;
			explicit LiveListener(Processor& _p) : processor(_p) {}
			void audioProcessorParameterChanged(juce::AudioProcessor*, const int _index, const float _value) override
			{
				processor.liveParameter(_index, _value);
			}
			void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails&) override {}
			void audioProcessorParameterChangeGestureBegin(juce::AudioProcessor*, const int _index) override
			{
				processor.liveGesture(_index, true);
			}
			void audioProcessorParameterChangeGestureEnd(juce::AudioProcessor*, const int _index) override
			{
				processor.liveGesture(_index, false);
			}
		};
		LiveListener m_liveListener{*this};
		void liveParameter(int _index, float _value);
		void liveGesture(int _index, bool _begin);
		std::atomic<int> m_chainSlot{-1}, m_chainPass{0};
		std::array<std::atomic<int>, Tracks> m_playingRowStep{};
		std::atomic<bool> m_fillPlaying{false};

		// The Kits: by parameter index, whether a Kit holds it; the Bank; the Slot played and what it held when the Kit
		// played was loaded, saved or rewritten, normalised values by parameter index and samples by Track
		std::vector<uint8_t> m_kitParameter;
		std::unique_ptr<KitBank> m_kits;
		int m_kitSlot = -1;
		bool m_kitHeld = false;		// m_kitValues and m_kitSamples hold the Slot's Kit: false for an empty Slot
		std::vector<float> m_kitValues;
		std::array<std::unique_ptr<Sample>, Tracks> m_kitSamples;
		int m_kitFollow = 0;		// the timer's ticks since it last followed the Bank

		// The Kits read ahead for the patterns' starts: the message thread's, by Slot; the pointers the audio thread
		// reads; the ones replaced, each with the blocks done when it was, freed once a block has ended since; the one
		// the audio thread loaded last, which the message thread takes; the blocks processed
		std::array<std::unique_ptr<PreparedKit>, KitSlots> m_preparedKits;
		std::array<std::atomic<PreparedKit*>, KitSlots> m_prepared{};
		std::vector<std::pair<std::unique_ptr<PreparedKit>, uint64_t>> m_preparedRetired;
		std::atomic<PreparedKit*> m_kitSwitched{nullptr};
		std::atomic<uint64_t> m_blocksDone{0};
		std::array<uint64_t, Tracks> m_engineSampleHash{};	// the audio thread's: the samples it gave the engine

		std::unique_ptr<TrackSounds> m_trackSounds;

		// The audio thread's Locks: by parameter index, held and the value; by Track, the ones it holds
		std::vector<LockTarget> m_lockTargets;
		std::vector<uint8_t> m_locked;
		std::vector<float> m_lockValue;
		std::array<std::vector<int>, Tracks> m_lockedBy;	// reserved: no allocation on the audio thread

		// The audio thread's slides: a step with SLIDE moves what it locks in a line, from its Locks to the next trig's, or
		// to the host's value where that trig locks none, over the time to that trig, a step every SlideStep samples
		// from the slide's Hit; the Track's next Hit or Lock, host note or step, ends it
		static constexpr int SlideStep = 32;
		static constexpr int64_t NoSlide = std::numeric_limits<int64_t>::max();
		struct Glide
		{
			int parameter = 0;
			float from = 0, to = 0;
		};
		struct Slide
		{
			int64_t start = 0, length = 0;
			int64_t next = NoSlide;			// the sample of its next step
			std::vector<Glide> glides;		// reserved as m_lockedBy
		};
		std::array<Slide, Tracks> m_slides;
		int64_t m_now = 0;		// samples processed since prepareToPlay

		// The audio thread's transport at the block, from sync
		double m_blockBpm = 120;
		bool m_blockPlaying = false;
		double m_blockQuarters = 0;

		std::array<std::atomic<int>, Tracks> m_playingStep{};
		std::atomic<int> m_currentPattern{0};
		std::atomic<uint32_t> m_liveTracks{0};
		std::array<std::atomic<float>, Outputs> m_outputPeak{};
		std::array<std::atomic<float>, SendCount> m_returnPeak{};
		std::atomic<float> m_reduction{0};
	};
}
