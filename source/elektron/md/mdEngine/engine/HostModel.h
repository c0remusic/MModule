// The Machinedrum's main-processor side, for the voice DSP: per-track kit parameters, machine assignment,
// triggers and the control tick. The maths is the OS's own code run in MachineRunner (parameter smoothing
// $10002e0, LFO oscillator $1000088, LFO apply $1000332, machine coefficient functions); this class does what
// the OS's tick routine ($20ad9a) and slot pump ($10004d0) do around them. See docs/PROTOCOL.md.
//
// It also produces each track's inputs to the mixer DSP (DSP1): the 9 effect words (Y:$200+$40k) and the
// 5 mix words (Y:$100+5k), computed as the tick routine does ($20b1f6-$20b302).
//
// md-drums: trig and mute groups (Links and Chokes) are the Kit's and act on every Hit; parameter locks come with a
// Hit from outside (lock()), as the OS's sequencer gives them to its own trigger path.
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>

#include "MachineRunner.h"
#include "MdsPackage.h"
#include "VoiceEngine.h"

namespace md::engine
{
	// MID and CTR machines ($60-$7b) give the voice DSP nothing to play
	constexpr bool isAudioMachine(const uint8_t _id) { return !(_id >= 0x60 && _id <= 0x7b); }

	// md-drums: USR-01 to USR-16, the User Machines' ids as MDX X.20 numbers them (machine number + 128, 66 to 81)
	constexpr uint8_t kFirstUserMachine = 194;
	constexpr bool isUserMachine(const uint8_t _id) { return _id >= kFirstUserMachine && _id < kFirstUserMachine + 16; }

	// TVoices: VoiceEngine, or any type with the same setSlot/renderBlock/Block/kSlotWords surface.
	template<class TVoices = VoiceEngine>
	class HostModel
	{
	public:
		static constexpr int kTracks = 16;
		static constexpr int kParams = 24;	// SYN1-8, AMD AMF EQF EQG FLTF FLTW FLTQ SRR DIST, VOL PAN DEL REV, LFOS LFOD LFOM

		HostModel(MachineRunner& _os, TVoices& _voices);

		// Assign a machine (0-191, MachineInfo::id). SYN1-8 take the machine's defaults. As on the MD, the voice
		// switches machine at its next trigger, where all 24 parameters take effect immediately (no glide).
		// md-drums: or a User Machine, USR-01 to USR-16 (isUserMachine), which takes its package's defaults, or plays the
		// empty machine while its number holds none.
		void setMachine(int _track, uint8_t _machineId);
		// md-drums: the package of the User Machine at USR-01 to USR-16 (_index 0-15), or none: its defaults, and the
		// conversions that turn its eight parameters into the words its code reads at Y:(R6+1..8). The voice DSP holds the
		// code (VoiceEngine::installUserMachine).
		void setUserMachine(int _index, std::shared_ptr<const md::mds::Package> _package);
		// md-drums: OS 1.63's decay table, which a User Machine's Decay parameters read, as the OS image holds it
		const md::mds::DecayTable& decayTable() const { return m_decay; }
		void setParam(int _track, int _param, int _value);	// 0-127, smoothed like the MD
		// md-drums: a knob turned or a CC (0-127): the Kit's value, as setParam, and the target over a lock or a slide too,
		// as the OS's CC path writes it (mdSlideFirmwareTest)
		void knob(int _track, int _param, int _value);
		int param(int _track, int _param) const { return m_raw[_track][_param]; }
		// md-drums: a parameter lock (0-127) for the track's Hit in the block about to render. Where the tick's trigger
		// path applies the OS's own locks, with a pending machine, the parameter takes the value at once (no glide) and
		// holds it until the track's next Hit, or until knob gives it the Kit's new value; a Hit without a lock on a
		// parameter the last Hit locked plays the Kit's value again, at once too. A lock with no Hit of its track in that
		// block is dropped; param() stays the Kit's.
		void lock(int _track, int _param, int _value);
		// md-drums: an accent for the track's Hit in the block about to render, as the sequencer's accented step: that Hit
		// and its Link's play at velocity 128 + 2 x _amount, the accent amount (0-127) from then on. Taken where the
		// block's locks are, so the Hit's trigger may come before it; with no Hit of its track in that block, dropped.
		void accent(int _track, int _amount);
		// md-drums: a slide for the track's Hit in the block about to render, as the sequencer's slide trig: every
		// parameter that Hit locks glides from its lock to the track's next Hit's (slideTarget), or to the Kit's value
		// without one, over _clocks clocks of 24 ppqn (1-127, extendSlide for more) to that next Hit. OS 1.63's own ramp
		// (the clock handler at $239762: setup $23b25a, update $23ac2c): increment = (end - start) x 128 / clocks,
		// truncated, added at each clock(), the smoothing target taking the sum >> 7 until the next sum would pass the
		// end; the track's next Hit ends it, its own locks or the Kit's values at once. Taken where the block's locks
		// are; with no Hit of its track in that block, dropped.
		void slide(int _track, int _clocks);
		// md-drums: 127 x _times127 (0-127) clocks more for the slide of the track's Hit in the block about to render
		void extendSlide(int _track, int _times127);
		// md-drums: the value (0-127) _param slides to in the slide of the track's Hit in the block about to render: the
		// lock the track's next Hit plays on it
		void slideTarget(int _track, int _param, int _value);
		// md-drums: one clock of 24 ppqn of the transport, before the block about to render: every slide steps, but a
		// track's whose Hit that block plays, as the OS's update skips a track whose trig waits for its tick ($2ae8c2)
		// (on the firmware the trig's own clock skips it or steps it as the control tick's phase falls:
		// mdSlideFirmwareTest).
		// Without clocks (the transport stopped) slides stand still, as the OS's do when its sequencer stops.
		void clock();
		// md-drums: the smoothing target the next tick gives a parameter: a lock's or a slide's value, the Kit's elsewhere
		uint8_t target(int _track, int _param) const;
		void setTempo(double _bpm);							// tempo factor for tempo-synced LFOs and E12/ROM retrig
		// velocity 1-127; accent: the sequencer's accented step (velocity becomes 128 + 2 x accent amount). A muted track
		// drops its Hit, as the OS's note handler does ($20ccf6), and so fires neither its Link nor its Choke.
		// md-drums: returns the tracks it struck, one bit each: the track and its Link, a muted one left out.
		uint32_t trigger(int _track, int _velocity = 100, bool _accent = false);
		// md-drums: _track's User Machine given to its voice at the next tick as a Hit gives it (its type, so INIT and TRIG,
		// and its words), at full velocity, without what else a Hit does (the LFO's restart, the accent, the Link, the
		// Choke): a NEIGHBOUR effect works from then on without waiting for a Hit (ticket 32 of the User Machines map)
		void start(int _track);

		// md-drums: X.20's ANTI-CLICK (ticket 33 of the User Machines map, research/13): 0 off, or 2 or 4 blocks (64 or
		// 128 samples). A Hit on a track whose machine X.20 fades (all but its 20 continuous built-ins and the User
		// Machines that set CONTINUOUS RETRIGGER, the machine the Hit brings deciding) waits that many blocks with its
		// locks, accent and slide, while the track's voice keeps its words and its raw output falls in a straight line to
		// zero, as X.20's mixer ramps it (gain 1 - i/64 or 1 - i/128, truncated as the DSP's mpy); a Hit during the wait
		// replaces the waiting one and the count goes on. The track's Link is struck at the note, as ever.
		void setFadeBlocks(int _blocks);
		int fadeBlocks() const { return m_fadeBlocks; }
		// Whether _track has a Hit waiting for the fade
		bool holds(int _track) const { return m_held[_track].waiting; }
		// Whether a waiting Hit of _track fired in the block just rendered
		bool firedHeld(int _track) const { return (m_firedHeld >> _track) & 1; }
		// Whether X.20 fades a Hit of _machine: not its 20 continuous built-ins, nor a User Machine that sets CONTINUOUS
		// RETRIGGER (_package, none for an empty number)
		static bool fades(uint8_t _machine, const md::mds::Package* _package);

		// md-drums: Links and Chokes, the Kit's trig and mute groups, played as OS 1.63 plays them
		// (.scratch/md-drums-editor/research/06-groupes.md). A Hit of _track also hits its Link, at the same velocity
		// and in the same block, one step only (the Link's own Link does not fire), and silences its Choke until that
		// track's own next Hit: a higher track in the Hit's block, a lower one at the next tick, as the OS orders its
		// tick; a track struck while choked stays silent until the next tick. kOff, or any value past the tracks,
		// switches one off; a self-reference does nothing. A muted Link drops the Hit it is given, as a muted track
		// drops its own.
		static constexpr uint8_t kOff = 0xff;
		void setLink(int _track, int _target);
		void setChoke(int _track, int _target);
		uint8_t link(int _track) const { return m_link[_track]; }
		uint8_t choke(int _track) const { return m_choke[_track]; }
		// md-drums: in the block just rendered, the track whose Hit's Choke silenced _track there, or kOff. Only a higher
		// track goes silent in the Hit's block; a lower one waits for the next tick and is not reported.
		uint8_t cutBy(int _track) const { return m_cutBy[_track]; }
		// md-drums: the machine _track plays: from its trigger on, the one its Hit brought (a pending setMachine)
		uint8_t machine(int _track) const { return m_machine[_track]; }
		// md-drums: the machine _track's next Hit brings: the pending one, else the one it plays
		uint8_t pendingOrMachine(int _track) const
		{
			return static_cast<uint8_t>(m_pendingMachine[_track] >= 0 ? m_pendingMachine[_track] : m_machine[_track]);
		}
		void setAccentAmount(int _amount) { m_accentAmount = std::clamp(_amount, 0, 127); }

		// Track level (kit LEV, 0-127; smoothed by the OS's level slew $100029e, and set at once by every Hit, as the
		// OS's trigger path does at $20b022), mute (the track's mix words 0, and its Hits dropped), output routing
		// (DSP1's per-track route word; 6 = the main outputs, the MD's default).
		void setLevel(int _track, int _level);
		void setMute(int _track, bool _mute) { m_mute[_track] = _mute; }
		void setRouting(int _track, int _route) { m_route[_track] = static_cast<uint8_t>(_route); }

		// md-drums: the master effects' 32 Kit bytes (0-127), in the Kit dump's order: Gate Box DVOL PRED DEC DAMP HP LP
		// GATE LEV, Echo TIME MOD MFRQ FB FLTF FLTW MONO LEV, EQ LF LG HF HG PF PG PQ GAIN, Dynamix ATCK REL TRHD RTIO KNEE
		// HP OUTG MIX. Smoothed by the OS's level slew, as the track levels; each tick then computes one effect's DSP
		// words in turn, with the OS's own code (the tick routine's bodies at $20b4ae, $20b604, $20b74e, $20b940).
		static constexpr int kMasterParams = 32;
		void setMaster(int _index, int _value);
		int master(int _index) const;
		struct MasterWords
		{
			uint32_t y = 0;					// the first Y address: $150 Echo, $17a Dynamix, $170 EQ, $185 Gate Box
			int count = 0;
			std::array<uint32_t, 11> words{};
		};
		// The words the last tick computed, once (as the OS sends a block once): false when there are none
		bool takeMasterWords(MasterWords& _out);

		// Per-track inputs to the mixer DSP, as of the last tick.
		struct MixerInput
		{
			std::array<uint32_t, 9> fx{};	// AMD AMF EQF EQG FLTF FLTW FLTQ SRR DIST (smoothed + LFO, value << 7)
			std::array<uint32_t, 5> mix{};	// route, VOL gain, PAN << 16, REV send, DEL send
		};
		const MixerInput& mixerInput(int _track) const { return m_mixer[_track]; }

		// A track's LFO (LFO k belongs to track k; its speed, depth and mix are track parameters 21-23):
		// destination track and parameter (0-23), two shapes, type (bit 0 TRIG: restart on the track's trigger,
		// bit 1 HOLD: output only updated at a trigger; 0 = FREE).
		void setLfo(int _track, int _destTrack, int _destParam, int _shape1, int _shape2, int _type);
		// md-drums: a Kit's whole 36-byte LFO block (settings, then the running LFO's state), copied into the running
		// LFO as the OS's LOAD KIT does ($20a042)
		static constexpr int kLfoBytes = 36;
		void loadLfo(int _track, const uint8_t* _block);

		// Control ticks: the MD runs its tick as fast as the ColdFire gets through it (~120 Hz measured in
		// gearmulator-md-mm, varying with load). Here one tick every 11 blocks of 32 samples (125.3 Hz).
		static constexpr int kBlocksPerTick = 11;

		// Render one 32-sample block of the 16 voices (runs a tick first when due).
		bool renderBlock(typename TVoices::Block& _out);

		// Optional stage timing (microseconds, accumulated): the OS tick + trigger updates, and the voice DSP.
		bool timingOn = false;
		double tickUs = 0, dspUs = 0;	// tickUs: the machine functions, the smoothing, the LFOs and the pokes
		void tick();
		void updateVoice(int _track);	// machine function on the voice's current array -> voice slot
		void updateMixer(int _track);	// the track's DSP1 words from its current array, level and velocity

		const uint16_t* voiceParams(int _track) const;	// the per-voice 24-value array after smoothing and LFO

	private:
		void hit(int _track, int _velocity, bool _accent);	// one track's part of a trigger (the note handler's)
		void applyChoke(int _track);	// the tick's mute-group step for a triggered track ($20b3a2)

		MachineRunner& m_os;
		TVoices& m_voices;
		std::array<std::array<uint8_t, kParams>, kTracks> m_raw{};
		std::array<uint8_t, kTracks> m_machine{};
		std::array<int, kTracks> m_pendingMachine{};	// -1 = none
		std::array<bool, kTracks> m_trigger{};
		std::array<bool, kTracks> m_started{};	// start()'s, for the next tick
		void applyPendingMachine(int _track);

		// ANTI-CLICK: each track's waiting Hit, with what came with it
		struct Held
		{
			bool waiting = false;
			bool fresh = false;		// taken since the last block: its locks, accent and slide still to take
			int count = 0;			// blocks before it fires
			uint8_t velocity = 0;
			bool accent = false;
			bool accented = false;
			uint8_t accentAmount = 0;
			uint32_t locks = 0;
			std::array<uint8_t, kParams> lockValues{};
			uint8_t slide = 0;
			uint8_t slideExtension = 0;
			uint32_t slideTargets = 0;
			std::array<uint8_t, kParams> slideTargetValues{};
		};
		int m_fadeBlocks = 0;
		std::array<Held, kTracks> m_held{};
		std::array<int32_t, kTracks> m_fadeGain{};	// the gain at the next block's first sample, Q23; 0: no fade
		std::array<int32_t, kTracks> m_fadeStep{};	// its fall per sample, from the setting when the fade began
		uint32_t m_firedHeld = 0;
		bool holdsHit(int _track) const;
		void hitNow(int _track, int _velocity, bool _accent);
		void hold(int _track, int _velocity, bool _accent);
		void takeHeld();
		void fade(typename TVoices::Block& _out);
		std::array<uint8_t, kTracks> m_velocity{};	// last trigger's velocity (0 until triggered, as on the MD)
		std::array<bool, kTracks> m_accent{};
		std::array<bool, kTracks> m_mute{};
		std::array<uint8_t, kTracks> m_route{};
		std::array<MixerInput, kTracks> m_mixer{};
		int m_accentAmount = 0;
		std::array<uint16_t, kParams> m_scratch{};
		int m_blockCount = 0;
		uint32_t m_tickCount = 0;
		std::array<uint8_t, kTracks> m_link{};
		std::array<uint8_t, kTracks> m_choke{};
		std::array<bool, kTracks> m_choked{};	// the OS's group-mute flag ($1001510), apart from the user mute
		std::array<uint8_t, kTracks> m_cutBy{};	// md-drums: cutBy, for the block being rendered
		MasterWords m_masterWords;
		bool m_masterPending = false;
		std::array<std::shared_ptr<const md::mds::Package>, 16> m_userMachines{};
		md::mds::DecayTable m_decay{};

		// md-drums: locks (bit n = parameter n) waiting for a Hit in the block about to render, and those the track's
		// last Hit plays; the smoothing target is the lock's value on a locked parameter, the Kit's elsewhere
		void applyLocks(int _track);
		std::array<uint32_t, kTracks> m_lockPending{};
		void applyAccents();
		uint32_t m_accentPending = 0;	// bit n: track n's Hit in the block about to render is accented
		uint8_t m_accentPendingAmount = 0;
		std::array<uint32_t, kTracks> m_locked{};
		std::array<std::array<uint8_t, kParams>, kTracks> m_lockPendingValue{};
		std::array<std::array<uint8_t, kParams>, kTracks> m_lockValue{};

		// md-drums: slides. A sliding parameter is a locked one whose lock value the clocks move (target() reads it), as
		// the OS's update writes the smoothing target only; the next Hit's applyLocks ends it.
		void armSlide(int _track);		// after the Hit's locks, as the trigger path arms the waiting increment
		std::array<uint8_t, kTracks> m_slidePending{};	// the clocks of a slide waiting for a Hit; 0: none
		std::array<uint8_t, kTracks> m_slideExtension{};	// its clocks past them, in 127s
		std::array<uint32_t, kTracks> m_slideTargetPending{};
		std::array<std::array<uint8_t, kParams>, kTracks> m_slideTargetValue{};
		std::array<uint32_t, kTracks> m_sliding{};		// bit n: parameter n slides
		struct Slide
		{
			int32_t current = 0;	// value << 7, as the OS keeps it ($2ae996)
			int32_t increment = 0;
			int32_t end = 0;
		};
		std::array<std::array<Slide, kParams>, kTracks> m_slides{};
	};
}
