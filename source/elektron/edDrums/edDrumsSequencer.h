#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

// The internal sequencer's core (spec 7, ticket 17 of .scratch/elektron-drums-v1), without JUCE and with nothing allocated
// or locked while it processes. It follows the host's transport alone: process() takes a block's position in quarter notes
// and fills a fixed list with the Hits and the Locks that fall in the block, each at its sample.
//
// Time. A step's event time is a function of the position alone (step n of a Track: pattern start + n x step length, plus
// the swing of an odd step, plus its micro-timing), and a step belongs to the block that holds its sample, so the same
// events come out whatever the block sizes, and a jump of the host's position lands on the step it points at. The
// pattern restarts every master length and every Track restarts with it; a Track shorter than the master loops inside it.
// A pattern asked for starts at the end of the pattern playing (or at once when the transport starts), and so does a
// chain, whose place follows the position alone. A step's gate ends its Hit with an `off` event; a stop or a jump ends
// every Hit held at the block's first sample.
//
// Threads. Nothing here is thread-safe but requestPattern: the edits (Pattern's and the Sequencer's setters) run when
// process() does not, the plug-in serialises them. An Event's lock pointers point into a Pattern's pool and hold until
// that Pattern is edited.
namespace elektron::drums
{
	constexpr int SeqPatterns = 128;
	constexpr int SeqTracks = 16;
	constexpr int SeqSteps = 64;
	constexpr int SeqMaxLength = 512;	// a pattern's length in steps of 1/16 (t-ui-spec, common length): Tracks loop in it
	constexpr int TimingUnits = 24;		// micro-timing in 24ths of a step, -23 to 23
	constexpr uint8_t NoNote = 0xff;	// a step's note: the Track's own
	constexpr int GateUnits = 8;		// a step's length in 8ths of a step, 0 for none (the Hit is never ended)
	constexpr int MaxChain = 64;		// entries of a chain

	// A Track's speed against a 16th-note step: 1/8x to 2x
	enum class StepScale : uint8_t { Eighth, Quarter, Half, ThreeQuarters, One, OneAndHalf, Two, Count };

	// A step's length in quarter notes
	double stepQuarters(StepScale _scale);

	// A parameter of the Track, by an index the caller defines, and the value to give it before the Hit
	struct Lock
	{
		uint16_t parameter = 0;
		float value = 0;
	};

	enum class ConditionKind : uint8_t
	{
		Always,
		Probability,	// a = percent, 1 to 99
		Ratio,			// a:b, 1 <= a <= b: the a-th cycle of every b
		Fill, NotFill,
		Pre, NotPre,	// the last conditional step of the Track passed / failed
		Nei, NotNei,	// the same for the Track above (Track 1 has none: failed)
		First, NotFirst	// the first cycle of the Track since the pattern started
	};

	struct Condition
	{
		ConditionKind kind = ConditionKind::Always;
		uint8_t a = 0;
		uint8_t b = 0;
	};

	struct Step
	{
		bool trig = false;
		bool accent = false;
		bool slide = false;			// its locks glide to the next trig's
		uint8_t velocity = 100;		// 1 to 127
		uint8_t note = NoNote;		// the Track's Note for this Hit, 0 to 127, as a Lock of Note would set it
		uint16_t gate = 0;			// the Hit ends this many GateUnits after its last Hit, 0: it is not ended
		int8_t timing = 0;			// micro-timing, in 24ths of a step
		uint8_t ratchets = 1;		// 1 to 8 Hits
		uint8_t ratchetRate = 0;	// Hits a step: 0 spreads the Hits over the step, else at least `ratchets`
		int8_t ratchetVelocity = 0;	// added to each Hit's velocity after the first
		Condition condition;
		uint16_t lockCount = 0;		// a step with locks and no trig is a lock without note
		uint32_t lockFirst = 0;		// into the pattern's pool
	};

	// One pattern: 16 Tracks of up to 64 steps, locks pooled with no cap
	class Pattern
	{
	public:
		Pattern();

		int masterLength = 16;		// steps of 1/16, 1 to SeqMaxLength: the pattern's length
		int swing = 50;				// percent, 50 to 80
		int kit = -1;				// the Kits' Slot loaded where it starts (ticket 19), -1 keeps the Kit playing
		std::array<int, SeqTracks> trackLength;		// 1 to 64
		std::array<StepScale, SeqTracks> trackScale;

		Step& step(int _track, int _index) { return m_steps[_track][_index]; }
		const Step& step(int _track, int _index) const { return m_steps[_track][_index]; }
		const Lock* locks(int _track, int _index) const;

		// Sets a lock, adding it to the step (a step with no trig becomes a lock without note)
		void setLock(int _track, int _index, uint16_t _parameter, float _value);
		bool removeLock(int _track, int _index, uint16_t _parameter);
		// Removing a trig removes its locks
		void setTrig(int _track, int _index, bool _on, int _velocity = 100);

		void clearTrack(int _track);
		void clear();
		void copyTrackFrom(const Pattern& _from, int _fromTrack, int _toTrack);

	private:
		void compact();

		std::array<std::array<Step, SeqSteps>, SeqTracks> m_steps;
		std::vector<Lock> m_locks;
	};

	struct Event
	{
		uint8_t track = 0;
		uint8_t velocity = 0;			// 0 for a lock without note
		bool accent = false;
		bool trig = false;				// false: apply the locks, no Hit
		bool off = false;				// the end of a Hit with a gate: nothing else
		bool slide = false;
		bool retrig = false;			// a ratchet's Hit after the first: its step's locks again
		uint8_t note = NoNote;			// the step's note, NoNote for the Track's
		uint32_t offset = 0;			// sample in the block
		const Lock* locks = nullptr;	// to apply before the Hit
		uint16_t lockCount = 0;
		uint32_t slideSamples = 0;		// with slide: glide the locked parameters to nextLocks over this long
		const Lock* nextLocks = nullptr;
		uint16_t nextLockCount = 0;
	};

	// The caller's fixed list, in order of offset then Track. Past its capacity events are counted in `dropped`
	struct Events
	{
		static constexpr int Capacity = 1024;
		std::array<Event, Capacity> events;
		int count = 0;
		int dropped = 0;
		int patternStart = -1;		// the sample where another pattern began in the block, -1 for none (ticket 19)
	};

	struct Transport
	{
		bool playing = false;
		double quarters = 0;		// position at the block's first sample
		double bpm = 120;
		double sampleRate = 44100;
		int length = 0;				// samples
	};

	// A chain's entry (spec 7): a pattern played `repeats` times, in fill if `fill`
	struct ChainEntry
	{
		uint8_t pattern = 0;
		uint8_t repeats = 1;	// 1 to 64
		bool fill = false;
	};

	class Sequencer
	{
	public:
		Sequencer();

		Pattern& pattern(const int _index) { return m_patterns[_index]; }
		const Pattern& pattern(const int _index) const { return m_patterns[_index]; }
		void copyPattern(int _from, int _to);
		void clearPattern(int _index);

		void setSeed(const uint32_t _seed) { m_seed = _seed; }
		uint32_t seed() const { return m_seed; }
		void setFill(const bool _on) { m_fill = _on; }
		bool fill() const { return m_fill; }
		// A Track muted plays nothing; while a Track is soloed, the Tracks not soloed play nothing
		void setMute(int _track, bool _on);
		void setSolo(int _track, bool _on);
		bool mute(const int _track) const { return m_mute[_track]; }
		bool solo(const int _track) const { return m_solo[_track]; }
		bool silenced(int _track) const;

		// The chain: its entries in order, MaxChain at most. Asked for (requestChain), it takes over at the end of the
		// pattern playing, and where it stands follows the host's position alone: its passes laid end to end from
		// position 0 and looping, so that a stop and a start play the same place again. A pattern asked for ends it at the
		// end of the pattern playing
		void setChain(const std::vector<ChainEntry>& _entries);
		int chainLength() const { return m_chainLength; }
		const ChainEntry& chainEntry(const int _index) const { return m_chain[_index]; }
		void requestChain() { m_requested.store(ChainRequest); }
		bool chainPlaying() const { return m_chainOn && m_chainLength > 0; }
		int chainSlot() const { return chainPlaying() ? m_chainSlot : -1; }	// the entry playing
		int64_t pass() const { return m_pass; }		// the pattern playing's pass, from 0, in its chain entry if any
		bool fillPlaying() const { return m_fill || (chainPlaying() && m_chain[m_chainSlot].fill); }

		// Takes effect at the end of the pattern playing. Safe against process()
		void requestPattern(int _index);
		int currentPattern() const { return m_current; }

		void process(const Transport& _transport, Events& _out);

		// The step of _track's that holds _quarters in the pattern playing, -1 while the transport stands still
		int stepAt(int _track, double _quarters) const;
		// The same counted from the pattern's start, past the Track's length (Overview's rows), -1 stopped
		int stepSinceStart(int _track, double _quarters) const;

		// LIVE recording: the step of _track nearest _quarters in the pattern playing, and the micro-timing that puts its
		// Hit at _quarters or just before (never after: the step does not play a second time in the lap it was played).
		// False while the transport stands still
		bool nearestStep(int _track, double _quarters, int& _step, int& _timing) const;

		// A Standard MIDI File of one pass of _pattern, as the sequencer plays it from position 0 with _seed and fill off
		// (spec 7: notes 36-51 on channel 1, no Locks): 960 ticks a quarter at 120 BPM, a Hit's note ending at its gate,
		// or for a step without one at the Track's next Hit or after a 16th, whichever comes first
		static constexpr int MidiTicks = 960;
		static std::vector<uint8_t> patternMidi(const Pattern& _pattern, uint32_t _seed);

		// For a plug-in that edits one copy and plays another: the patterns, mutes, seed and fill of _from (allocates),
		// and where _from's playback stands (allocates nothing), which a copy that takes over from it carries on from
		void copyEdits(const Sequencer& _from);
		void takePlayback(const Sequencer& _from);

	private:
		struct Candidate
		{
			int sample;
			int track;
			int n;			// step number since the pattern started
			double time;	// quarters
		};
		struct Retrig
		{
			double time;
			uint8_t track, velocity;
			bool accent;
			uint8_t pattern, step;		// whose locks it plays, read when it sounds
		};
		struct Segment		// the part of a block a pattern plays
		{
			double blockStart, blockEnd, samplesPerQuarter, patternStart, patternEnd;
			int lo, hi;						// samples of the block
			int patternStartSample, patternEndSample;
		};

		struct Off
		{
			double time;
			uint8_t track;
		};
		static constexpr int ChainRequest = -2;

		void restart(double _quarters);
		double patternLength(int _pattern) const;
		void locateChain(double _quarters);
		void collect(const Pattern& _p, const Segment& _s);
		bool evaluate(const Pattern& _p, const Candidate& _c);
		void emit(const Pattern& _p, const Candidate& _c, const Segment& _s, Events& _out);
		void flushRetrigs(double _blockStart, double _samplesPerQuarter, int _length, Events& _out);
		void flushOffs(double _blockStart, double _samplesPerQuarter, int _length, bool _all, Events& _out);

		std::vector<Pattern> m_patterns;
		std::array<bool, SeqTracks> m_mute;
		std::array<bool, SeqTracks> m_solo;
		std::array<bool, SeqTracks> m_last;		// the last conditional step passed, for Pre and Nei
		uint32_t m_seed = 1;
		bool m_fill = false;
		std::array<ChainEntry, MaxChain> m_chain{};
		int m_chainLength = 0;
		bool m_playing = false;
		int m_current = 0;
		std::atomic<int> m_requested{-1};	// a pattern, ChainRequest, -1 for none
		bool m_chainOn = false;
		int m_chainSlot = 0;
		double m_expected = 0;			// where a contiguous block starts
		double m_patternStart = 0;		// quarters
		int64_t m_pass = 0;				// the pattern's passes since it began (or the position's, after a jump)

		static constexpr int MaxCandidates = 1024;
		static constexpr int MaxRetrigs = 256;
		static constexpr int MaxOffs = 256;
		std::array<Candidate, MaxCandidates> m_candidates;
		int m_candidateCount = 0;
		std::array<Retrig, MaxRetrigs> m_retrigs;
		int m_retrigCount = 0;
		std::array<Off, MaxOffs> m_offs;
		int m_offCount = 0;
	};
}
