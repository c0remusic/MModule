#pragma once

#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

#include "edDrumsChain.h"
#include "edDrumsLfo.h"
#include "edDrumsLibrary.h"
#include "edDrumsMaster.h"
#include "edDrumsSamples.h"
#include "edDrumsSends.h"
#include "elektronBlock.h"
#include "mdVoice.h"
#include "mdVoiceTrack.h"

struct OpenDrumVoice;

// The instrument's engine: 16 Tracks, each one mono voice of either family, the Syntakt's rewritten machines at their
// 48 kHz or the ported Machinedrum at its 44.1 kHz, on a 44.1 kHz host (ticket 11 of .scratch/elektron-drums-v1).
//
// A Hit lands at its note's sample. The two families render fixed blocks, so that a note waits for the next block of
// its family; each Track then writes its voice into a ring of its own, at a position chosen by the Hit, and the
// outputs read the rings at a fixed delay. What an older Hit left in a ring stays and adds up (Ring::write), so that a
// voice's tail is not moved by the next Hit's delay. The Machinedrum's ring holds host samples: an integer position is
// enough. The Syntakt's holds 48 kHz samples, read through the 48 to 44.1 kHz FIR each Track at its own phase, which a
// Hit sets, so that every Hit is read at the same sample positions: Tracks of the Syntakt sound the same samples
// wherever their note falls. That per-Track read is the family's one resampler, after the sum of its machines.
//
// A Track's chain (edDrumsChain.h) runs before its ring, on its family's blocks, at its family's rate: its voice's time, in
// which every Hit starts at a block, so that a block-rate stage or envelope meets a Hit the same wherever its note fell
// and a Hit adds no latency. Level and velocity come after the chain, at the ring. A cut is one ramp of CutSamples: after
// the chain, so that what the chain holds goes too, until a Hit of the Track sounds; from there on the old voice ramps
// before the chain, under the new one.
//
// A Track's two LFOs (edDrumsLfo.h, ticket 13) step at the blocks of its family, the Machinedrum's for an Off Track, so that
// a TRIG LFO meets a Hit the same wherever its note fell. A Track takes what is aimed at it at the blocks of its own family,
// each block by its target's limit, which is what keeps a square's edge from clicking: SYN1-8 at the machine, level and pan at
// the Track, the rest at its chain. A Hit of the Track takes it all at once.
namespace elektron::drums
{
	// The chain's value an LFO target at or past DriveA is
	constexpr VelocityTarget chainTarget(const LfoTarget _target)
	{
		return static_cast<VelocityTarget>(static_cast<int>(_target) - static_cast<int>(LfoTarget::DriveA) + static_cast<int>(VelocityTarget::DriveA));
	}
	static_assert(chainTarget(LfoTarget::DriveA) == VelocityTarget::DriveA && chainTarget(LfoTarget::DriveB) == VelocityTarget::DriveB
		&& chainTarget(LfoTarget::BandA) == VelocityTarget::BandA && chainTarget(LfoTarget::BandB) == VelocityTarget::BandB
		&& chainTarget(LfoTarget::Frequency) == VelocityTarget::Frequency && chainTarget(LfoTarget::Resonance) == VelocityTarget::Resonance
		&& chainTarget(LfoTarget::Width) == VelocityTarget::Width && chainTarget(LfoTarget::EnvelopeDepth) == VelocityTarget::EnvelopeDepth,
		"an LFO's chain targets are VelocityTarget's, in its order");
	// Whether _target is a value of the chain: DriveA to EnvelopeDepth, before Machine 9-24
	constexpr bool isChainTarget(const LfoTarget _target)
	{
		return _target >= LfoTarget::DriveA && _target <= LfoTarget::EnvelopeDepth;
	}
	static_assert(static_cast<int>(LfoTarget::Machine9) - static_cast<int>(LfoTarget::EnvelopeDepth) == 1,
		"Machine 9-24 follow the chain's targets");

	constexpr int Tracks = Lanes;
	static_assert(LfoBank::Tracks == Tracks, "an LFO bank is the engine's");
	constexpr int Outputs = 16;		// Main, then Out 01 to 15, each stereo: Live opens 16 output buses per plug-in
	constexpr double SampleRate = 44100.0;

	enum class Family : uint8_t
	{
		Off,
		Syntakt,
		Machinedrum,
		OpenDrum,
		Sophie
	};

	// A Syntakt machine by its index in syntaktMachineNames, a Machinedrum one by its id
	struct Machine
	{
		Family family = Family::Off;
		uint8_t id = 0;
	};

	inline bool operator==(const Machine& _a, const Machine& _b) { return _a.family == _b.family && _a.id == _b.id; }

	constexpr int SyntaktMachines = 9;
	constexpr std::array<const char*, SyntaktMachines> syntaktMachineNames{"BD MODERN", "SD BASIC", "CY ALLOY",
		"PC CARBON", "SD VINTAGE", "CP VINTAGE", "SY TOY", "SY BITS", "SP TWINSHOT"};
	constexpr uint8_t SyntaktTwinshot = 8;

	// The Sampler, of the Machinedrum's family: the Track's own sample (Engine::setSample) through the ROM machines'
	// player, SYN1-8 theirs (PTCH, DEC, HOLD, BRR, STRT, END, RTRG, RTIM). Track t plays it as ROM-33 + t, at that
	// entry of the bank, so that ROM-33 to -48 are not machines of their own here. An id no Machinedrum machine has.
	// Machine 9 is its grain (edDrumsVintage.h): under 64 the ROM player's, from 64 VINTAGE, which plays the whole sample
	// (Sample::MaxSourceLength) by PTCH, DEC, HOLD, STRT and END through Machine 10-12, BITS, COMPANDING and OUTPUT
	// FILTER, the Kit's values: no lock reaches Machine 9-12
	constexpr Machine Sampler{Family::Machinedrum, 192};

	// OPEN DRUM (ticket 26): four voices of a drum engine published under the GPL v3 (source/elektron/opendrum), a voice
	// per Track on the Machinedrum's blocks, rings and chain, at the 44.1 kHz host rate. The firmware steps its envelopes
	// once in 64 samples: a voice renders 64 every other block of 32, its second half played by the next, and a Hit
	// starts it afresh at the next block of 32, so that it plays the original's samples. Velocity is the Track's, after
	// the voice, as the Machinedrum's is. Ids 0-3: drum, snare, cymbal, hihat
	constexpr int OpenDrumMachines = 4;

	// SOPHIE: the metallic FM percussion voice of mestela/schwung-sophie (MIT, source/elektron/sophie), a voice per Track
	// on the Machinedrum's blocks, rings and chain like OPEN DRUM's, at the 44.1 kHz host rate it runs at sample by
	// sample. Every Track plays upstream's pad 1 (its base note 36), Machine 1-19 Sophie's parameters and Machine 20 the
	// kick body (sophieMachine.h); velocity is the Track's, after the voice, the voice itself triggered at 127
	constexpr int SophieMachines = 1;

	// Whether a Track can play _machine: every Syntakt machine here, a Machinedrum one whose render is ported or a MID or
	// CTR one, which stops the voice
	bool playable(const Machine& _machine);

	// A Track's Machine 1-24. The Machinedrum's and the Syntakt's machines, and the Sampler, use the first 8: the others
	// change nothing, and a lock of one holds nothing
	constexpr int MachineParameters = 24;

	// How many of Machine 1-24 _machine uses, from Machine 1 on
	int machineParameters(const Machine& _machine);

	// A Syntakt machine's Machine 1-8 follow its SYN page (state version 6, Kit version 4): TUNE, SWEEP or SHIMMER, PUNCH,
	// DECAY, the machine's two, MOD ENV, OVERDRIVE, as the editor labels them. Its voice reads internal ids 18-25 at its
	// indices 0-7; OVERDRIVE, id 26, is the drive stage after the voice; HOLD, id 24, is on no page (the amp holds the
	// voice), and Machine 9 keeps it for a set from before. The slot (0-8) that plays the voice's index _index (0-7):
	// where Machine _index + 1 of before plays now
	int syntaktSlotOf(int _index);
	// The name _machine gives Machine _index, nullptr where the interface has its own or none
	const char* machineParameterName(const Machine& _machine, int _index);

	// SYN1-8 of _machine as a machine change gives them; and all 24, those it does not use 0
	std::array<uint8_t, 8> defaults(const Machine& _machine);
	std::array<uint8_t, MachineParameters> allDefaults(const Machine& _machine);

	// What _machine has of the data it reads, which an interface can show
	MachineData dataOf(const Library& _library, const Machine& _machine);

	class Engine
	{
	public:
		// A voice's first sample sounds HitDelay samples after its note, on every output, jitter 0: the latency to report to
		// the host. The Machinedrum sets it: a note waits up to a block for the next of its family, the latest voice starts
		// LatestStart in and the longest chain delays it. A voice that starts sooner, or whose chain delays it less, is held
		// back. A Syntakt machine sees a trig in a block of 48 kHz and sounds from the next, the note waits up to a block for
		// the first and the FIR places it Latency on, with room left: a Hit holds its voice back by what is left past its
		// chain's delay (Chain::delay), so that a raised drive arrives on time
		static constexpr int HitDelay = 81;

		// A voice whose peak stays under Silence (-100 dB) that many blocks is off: not rendered until its next Hit. As long
		// as the longest silence inside a Hit, which the engine test measures
		static constexpr float Silence = 1e-5f;
		// 5.9 s: P-I-HH rests up to 6170 blocks whatever the tempo, ROM and E12 retriggers 1380 at 30 BPM
		static constexpr int MachinedrumQuietBlocks = 8192;
		static constexpr int SyntaktQuietBlocks = 512;		// 341 ms: SP TWINSHOT plays a gap in a sample, slowed down
		static constexpr int OpenDrumQuietBlocks = 64;		// 46 ms, once an OPEN DRUM voice's envelope has closed: its chain's tail
															// (a SOPHIE voice's too, once it has stopped)

		// What a SOPHIE voice's float output is multiplied by: upstream's voices peak near 1 before its mix gain of 0.24;
		// this one makes its factory kick peak as the other families' kicks do (edDrumsSophieTest measures them)
		static constexpr float SophieGain = 0.63f;

		// Every sample of a cut ramps: a Choke, a machine changed under a sounding voice
		static constexpr int CutSamples = 64;

		// The drives' rate unless told otherwise
		static constexpr int DefaultOversampling = 2;

		// The most an LFO moves level, pan or SYN1-8 in a block, in 1/128 of a value, which is what a square's edge becomes: a
		// whole range in 4 blocks, at the top of level's law as fast as a cut's ramp. A chain's are Chain::moveLimit
		static constexpr int LfoMove = (127 << 7) / 4;

		// Where a host's position agrees with the engine's own, in quarter notes: a sample at 323 BPM, past any host's rounding,
		// 1/512 of the cycle of a 1/64
		static constexpr double PositionTolerance = 1.0 / 8192;

		// _library outlives the engine
		explicit Engine(const Library& _library);
		~Engine();

		Engine(const Engine&) = delete;
		Engine& operator=(const Engine&) = delete;

		// What follows is the audio thread's, between render calls; a call takes effect at the sample render left at.

		// A sounding voice ramps out, as where a Hit of this moment would sound; SYN1-8 take the new machine's defaults,
		// which parameter() reports, and the locks of SYN1-8 go. The voice takes the machine at its next Hit
		void setMachine(int _track, const Machine& _machine);

		// _index 0-23, Machine 1-24, _value 0-127
		void setParameter(int _track, int _index, uint8_t _value);
		// The sample the Track plays on the Sampler, nullptr none (silence). The ROM player's words copied into the engine's
		// bank, without allocating, or a factory sample's (Sample::factory) read where the bank holds it; the VINTAGE grain
		// reads _sample->source where it lies, which must outlive the engine's use of it: until a later call for the Track
		// has returned. A voice of the Track's sounding ramps out, as where a Hit of this moment would sound; the other
		// Tracks' go on
		void setSample(int _track, const Sample* _sample);

		uint8_t parameter(const int _track, const int _index) const
		{
			return m_tracks[static_cast<size_t>(_track)].params[static_cast<size_t>(_index)];
		}
		// What the Track plays of Machine _index + 1: the last Hit's lock (lock) where it holds one, else parameter(). For
		// the tests, which compare it with what the editor draws
		uint8_t played(int _track, int _index) const;

		// The Syntakt's machines play a note, in semitones; the Machinedrum's take none
		void setNote(int _track, int _note);

		// 0-127, the gain the square of its fraction
		void setLevel(int _track, int _level);
		// 0 left, 64 centre, 127 right
		void setPan(int _track, int _pan);
		// 0 Main, 1-15 Out 01-15
		void setOutput(int _track, int _output);
		// 0-127, the gain the square of its fraction: what of the Track, after its level and pan, goes to the effect, on
		// Main or an Out alike
		void setSend(int _track, Send _send, int _level);
		// The effects the Tracks share: their settings, and which output their return goes to
		Sends& sends() { return *m_sends; }
		// The master on Main, off until set on: Main then as the Tracks and returns sum
		Master& master() { return *m_master; }
		void setMasterOn(const bool _on)
		{
			if(_on && !m_masterOn)
				m_master->settle();
			m_masterOn = _on;
		}
		// Beats per minute, for the Machinedrum's machines that read the tempo and for the LFOs
		void setTempo(double _bpm);

		// The host's transport at this sample, in quarter notes; between two calls it runs on at the tempo. A call that
		// agrees with where it stands, to PositionTolerance, changes nothing: the samples do not depend on the host's blocks
		void setPosition(double _quarters);
		double position() const;

		// LFO _index (0 or 1) of _track, from the next block of its Track's family. A target it leaves ramps back; off, an LFO costs nothing
		void setLfo(int _track, int _index, const LfoSettings& _settings);
		double lfoPhase(int _track, int _index) const;

		// A Hit of _track silences the Tracks of _targets, a bit each, whatever their family, where the Hit sounds
		void setChokes(int _track, uint32_t _targets);

		// A lock of machine parameter _index for the Hit that follows at this sample, until the Track's next Hit
		void lock(int _track, int _index, uint8_t _value);

		// The Track's chain from here on; a machine of another family brings that family's chain. Stages that change crossfade
		void setChain(int _track, const ChainSettings& _settings);

		// The drives' rate, global, 1, 2 or 4 times the Track's
		void setOversampling(int _factor);

		// 0-127: a Hit with an accent plays at velocity 128 + 2 x amount
		void setAccent(int _track, int _amount);

		// A Hit, _velocity 1-127, and its end for a machine that holds
		void noteOn(int _track, int _velocity, bool _accent = false);
		void noteOff(int _track);

		// The next _count samples of the Outputs, _outputs holding left then right of each, nullptr for a bus the host
		// does not take. Whatever the host's blocks, the samples are the same
		void render(float* const* _outputs, int _count);

		// The voices sounding, a bit per Track
		uint32_t liveTracks() const;
		// The lanes Syntakt machine _machine renders, 0 when it does not
		int lanesRendered(int _machine) const;

	private:
		static constexpr int64_t Never = std::numeric_limits<int64_t>::max() / 2;

		// What a Track's voice leaves on its way out, positions counted in its family's samples. Everything before end()
		// holds samples; past it, nothing. A write that starts before end() adds, one that starts after it leaves
		// silence between. A window of Mirror samples can be read in one piece anywhere
		class Ring
		{
		public:
			static constexpr int Size = 512, Mirror = 48;

			// A block of _x from _at, its gain ramping over the block from _from to _to, and from _fadeAt in a line to 0.
			// Returns the block's peak before the gains
			float write(int64_t _at, const float* _x, float _from, float _to, int64_t _fadeAt);
			// The ramp of a cut at _at on what is already written
			void fade(int64_t _at);
			// Silence for a window after the last write, a voice over
			void close();
			int64_t end() const { return m_end; }
			const float* window(int64_t _from) const { return m_x + (_from & (Size - 1)); }

		private:
			float at(const int64_t _position) const { return m_x[_position & (Size - 1)]; }
			void put(int64_t _position, float _value);

			alignas(16) float m_x[Size + Mirror]{};
			int64_t m_end = 0;
		};

		// A voice going into its ring
		struct Run
		{
			int64_t at = 0;			// where its next block goes
			float gain = 0, velocity = 0;
			int quiet = 0;			// its last blocks in a row under Silence, before its chain too
			bool voiced = false;	// quiet out of its chain, not before it: a closed filter is no end. Read once it is quiet

			void start(int64_t _at, float _velocity, float _level);
			void write(Ring& _ring, const float* _x, float _level, int64_t _fadeAt);
		};

		// A lane of a Syntakt machine, for as long as a voice holds it
		struct Lane
		{
			int track = -1;
			bool skip = false;			// the block that sees its trig holds what the lane froze on
			int64_t fadeAt = Never;		// a cut's, in the Track's ring: no more its Track's, whose next Hit takes another
			int heir = -1;				// the Track taking it at the block that sees its trig, a cut voice's last
		};

		struct Source;


		// As far as a Machinedrum voice cut can reach past what is written: the ramp, a Hit's wait and the voice's start
		static constexpr int TailBlocks = 5;

		struct Track
		{
			Machine machine;
			std::array<uint8_t, MachineParameters> params{};
			int note = 60;
			int levelValue = 100;
			float level = 0.62f;			// the gain of level 100, the LFOs' moves in it
			int panValue = 64;
			std::array<float, 2> pan{1, 1}, panNow{1, 1};
			std::array<float, SendCount> send{}, sendNow{};
			int output = 0;
			uint32_t chokes = 0;
			int accent = 0;
			Family chainFamily = Family::Off;	// the Chain that holds its settings
			bool held = false;				// its chain's amp holds its voice (Chain::holdsVoice)
			std::array<int, static_cast<size_t>(LfoTarget::Count)> taken{};	// what the LFOs aimed here add up to, as far as its slope has got

			// Machinedrum
			Ring mdRing;
			Run md;
			int64_t mdFadeAt = Never;		// a cut's ramp, in the ring, until the Track's next Hit
			float mdTail[TailBlocks][BlockSize]{};	// a cut voice, ahead of its blocks: its own voice is free at once
			int mdTailBlocks = 0;
			int mdHitSkip = 0;				// the filter's pure delay: what still comes of the last voice in a Hit's first block

			// OPEN DRUM, on the Machinedrum's blocks
			OpenDrumVoice* od = nullptr;	// its voice, in m_odMemory
			int16_t odHalf[BlockSize]{};	// the second half of the voice's last block of 64
			bool odPending = false;			// odHalf not played yet
			bool odTrig = false;			// a Hit waits for the next block
			bool odSettle = false;			// ... on a quiet voice: its smoothers start at their targets
			bool odOpen = false;			// the hihat plays open

			// SOPHIE, on the Machinedrum's blocks: its voice is m_sophie's
			bool spTrig = false;			// a Hit waits for the next block

			// The Sampler's VINTAGE grain, on the Machinedrum's blocks: its sample is m_vtSource's
			vintage::Player vt;
			bool vtTrig = false;			// a Hit waits for the next block

			// Syntakt
			Ring stRing;
			Run st;
			bool stSounding = false;		// st has a place in the ring
			int stVoices = 0;				// the lanes that hold its samples, a cut one included
			int64_t stFadeAt = Never;		// a cut's ramp, in the ring, until the Track's next Hit
			float stTailGain = 1;			// a cut voice's under the Hit that follows (tailGain)
			int64_t stNext = -1;			// where the Hit the next block sees goes, -1 none
			float stNextGain = 0;
			int stNextVelocity = 0;			// what the chain's velocity target hears: the note's
			bool stGateOff = false;			// that Hit's note has ended
			int lane = -1;					// the lane its voice holds, on the machine it plays
			int phase = 0;					// how much earlier than the family's clock its ring is read, in 1/147 sample
			int nextPhase = 0;				// ... from output sample phaseAt, which a Hit sets
			int64_t phaseAt = Never;
			uint32_t pendingLocks = 0, lockedNow = 0;
			std::array<uint8_t, MachineParameters> lockValue{}, lockPendingValue{};
		};

		void cut(int _track);
		void hitMachinedrum(int _track, int _velocity, float _gain);
		void hitSyntakt(int _track, int _velocity, float _gain);
		void takeMoves(int _track, bool _settle = false);
		void modulate(uint32_t _tracks, double _seconds);
		// Machine _slot + 1 of a Syntakt Track to what plays it: its voice's index, or the OVERDRIVE stage
		void playSyntakt(int _track, int _slot);
		void setOver(int _track, int _word);
		void overdrive(Block& _in, uint32_t _live);
		static int playedValue(const Track& _track, const int _index)
		{
			return (_track.lockedNow >> _index & 1 ? _track.lockValue : _track.params)[static_cast<size_t>(_index)];
		}
		void takeLane(int _track);
		void freeLane(int _source, int _lane);
		void releaseVoice(int _track);
		Source& sourceOf(const Track& _track);
		Chain& chainOf(const Track& _track) { return _track.chainFamily == Family::Syntakt ? *m_stChain : *m_mdChain; }
		void renderMachinedrum();
		void renderVoice(int _track, float* _x);
		void playOpenDrum(int _track, int _index);
		void renderOpenDrum(Track& _track, float* _x);
		void playSophie(int _track, int _index);
		void renderSophie(int _track, float* _x);
		static bool vintageGrain(const Track& _track);
		void renderVintage(int _track, float* _x);
		void renderSyntakt();
		void read(int _track, float* _out, int _count);
		void mix(float* const* _outputs, int _offset, int _count);

		const Library& m_library;

		std::array<Track, Tracks> m_tracks;

		// Machinedrum
		std::array<md::voice::Track, Tracks> m_mdTracks;
		std::unique_ptr<md::voice::Shared> m_shared;
		// The ROM player's bank: the Library's, then from SamplerAddr a slot of Sample::SlotWords per Track
		std::vector<uint32_t> m_directory, m_bankData;
		md::voice::RomBank m_bank;
		std::vector<md::voice::Voice> m_voices;
		uint32_t m_mdMask = 0;			// the Tracks on a Machinedrum machine
		uint32_t m_mdLive = 0;			// ... whose voice sounds
		uint32_t m_mdTails = 0;			// ... whose chain still has a cut voice to take
		std::unique_ptr<Sends> m_sends;
		std::unique_ptr<Master> m_master;
		bool m_masterOn = false;
		std::unique_ptr<Chain> m_mdChain;
		std::unique_ptr<Block> m_mdIn;
		uint32_t m_tempo = 120 * 24;
		int64_t m_mdBlock = 0;			// the next block

		// OPEN DRUM: a voice per Track, made at once
		std::vector<std::max_align_t> m_odMemory;
		uint32_t m_odMask = 0;			// the Tracks on an OPEN DRUM machine

		// SOPHIE: a voice per Track, made at once (6 kB each)
		struct SophieVoices;
		std::unique_ptr<SophieVoices> m_sophie;
		uint32_t m_spMask = 0;			// the Tracks on SOPHIE

		// The VINTAGE grain's samples: each Track's Sample::source, where it lies
		std::array<const int16_t*, Tracks> m_vtSource{};
		std::array<uint32_t, Tracks> m_vtLength{};
		std::array<double, Tracks> m_vtRate{};

		// Syntakt
		std::vector<std::unique_ptr<Source>> m_sources;
		int64_t m_stBlock = 0;
		int64_t m_stNext = 0;			// the host sample whose reading first needs block m_stBlock
		std::unique_ptr<Chain> m_stChain;
		std::unique_ptr<Block> m_stIn;
		// OVERDRIVE, the drive stage after a Syntakt voice: the Tracks whose word is above 0, and those it runs on
		std::unique_ptr<syntakt::fast::TrackDrive> m_stOver;
		uint32_t m_overOn = 0, m_overRun = 0;
		std::array<int, Tracks> m_overWord{};	// the word each Track's stage was last given

		LfoBank m_lfos;
		uint32_t m_stMask = 0;			// the Tracks on a Syntakt machine: the others' LFOs and moves go by the Machinedrum's blocks

		// The transport: position m_quarters at sample m_quartersAt, and on at the tempo
		double m_quarters = 0;
		int64_t m_quartersAt = 0;
		double m_quartersPerSample = 120.0 / 60 / SampleRate;

		int64_t m_now = 0;				// the next host sample
	};
}
