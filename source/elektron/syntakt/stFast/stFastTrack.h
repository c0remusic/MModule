#pragma once

#include <cstdint>

#include "stFastStages.h"

// The Syntakt's track stages, which its voice CPU runs after a voice (syntakt::track, .scratch/elektron-engines/
// trackstages.md), in doubles or floats across the 16 Tracks, a class per stage for the slot chain (ticket 12 of
// .scratch/elektron-drums-v1). Their tables come by formula from a sample rate: 48 kHz is the machine's, and at 44.1 kHz
// a stage on a Machinedrum Track keeps its frequencies. stFastTrackTest holds each to the exact port and prints what the
// formulas leave of OS 1.41's tables.
//
// A stage keeps the DSP code's gain: the drive halves a clean signal, the filter's outputs are x / 4. process computes
// the pairs (TrackDrive: the 4-lane groups, TrackAmp: the lanes) that hold a lane of _lanes, whatever it writes on the rest;
// clear puts a lane back to silence, its parameters taken at once. Parameters are the block's words, value << 8 | fine,
// and set does not limit them.
namespace syntakt::fast
{
	// 0x4000112c: the soft clip 2u - |u|u three times over a ramped gain, crossfaded with half the dry signal over the
	// first 8 steps of the word
	class TrackDrive
	{
	public:
		TrackDrive();
		void set(int _lane, int _word);
		void process(Block& _buf, uint16_t _lanes = 0xffff);
		void clear(int _lane) { m_gain[_lane] = m_to[_lane]; m_dry[_lane] = m_dryTo[_lane]; m_wet[_lane] = m_wetTo[_lane]; }

	private:
		alignas(16) float m_gain[Lanes], m_dry[Lanes], m_wet[Lanes];		// at the start of the next block
		alignas(16) float m_to[Lanes], m_dryTo[Lanes], m_wetTo[Lanes];
	};

	// 0x40002320: a one-pole low-pass at BASE + WIDTH, then a one-pole high-pass at BASE. The poles jump between blocks
	class BaseWidth
	{
	public:
		explicit BaseWidth(double _rate = MachineRate);
		void set(int _lane, int _base, int _width);
		void process(Block& _buf, uint16_t _lanes = 0xffff);
		void clear(int _lane) { m_low[_lane] = m_high[_lane] = 0; }

	private:
		double m_table[1024];
		alignas(16) double m_pl[Lanes], m_ph[Lanes], m_low[Lanes]{}, m_high[Lanes]{};
	};

	// TYPE 2 to 6: the types that share a resonator and move between them by their coefficients
	constexpr bool isMorph(const int _type) { return _type >= 2 && _type <= 6; }

	// 0x400019d6: the multimode filter, a state-space resonator and a stage on its second output. TYPE 0 and 1 take the
	// low-pass and the high-pass of one resonator, 2 to 6 morph its other output, 7 is x / 4. Its coefficients ramp over
	// a block; a type that leaves the resonator's group fades out over a block and snaps.
	//
	// set: the block's type, cutoff word (0 to 0x7f000000) and RESO word << 16. trig: the block that sees a Hit, which
	// sounds from the next (a Syntakt Hit). hit: the block a Hit sounds from (a Machinedrum Hit). With _reset, a Hit
	// that sounds snaps the coefficients to where they are and clears the resonator
	class Multimode
	{
	public:
		explicit Multimode(double _rate = MachineRate);
		void set(int _lane, int _type, int32_t _cutoff, int32_t _reso, bool _reset);
		void trig(const int _lane) { m_trig[_lane] = true; }
		void hit(const int _lane) { m_trigSeen[_lane] = true; }
		void process(Block& _buf, uint16_t _lanes = 0xffff);
		void clear(int _lane);

	private:
		enum { C0, C1, C2, C3, C4, C5, C6, C7, Coefficients };

		struct Tables;

		void prepare(int _l);
		void pathA(int _f, int _r, double* _c) const;
		void pathB(int _f, int _r, int _m, double* _c) const;
		void render(Block& _buf, int _l);
		void mixOf(int _type, bool _keep, int _lane);

		const Tables& m_t;
		alignas(16) double m_cur[Coefficients][Lanes]{}, m_step[Coefficients][Lanes]{}, m_to[Coefficients][Lanes]{};
		alignas(16) double m_s1[Lanes]{}, m_s2[Lanes]{}, m_z[Lanes]{};
		alignas(16) double m_stage[Lanes]{}, m_stageStep[Lanes]{}, m_stageNext[Lanes]{};
		alignas(16) double m_mix[3][Lanes]{}, m_mixStep[3][Lanes]{}, m_mixTo[3][Lanes]{};		// x, A, B
		int m_type[Lanes]{}, m_next[Lanes]{};
		int32_t m_cutoff[Lanes]{}, m_reso[Lanes]{};
		bool m_reset[Lanes]{}, m_trig[Lanes]{}, m_trigSeen[Lanes]{}, m_snap[Lanes]{}, m_idle[Lanes]{};
	};

	// 0x40001476: the amp's AHD or ADSR with the Track's RST set: the block that sees a Hit (trig) fades the last one
	// out, its next block starts the attack
	class TrackAmp
	{
	public:
		explicit TrackAmp(double _rate = MachineRate);
		void set(int _lane, int _attack, int _hold, int _decay, int _sustain, int _release, bool _adsr);
		void trig(const int _lane) { m_trig[_lane] = true; }
		void gateOff(const int _lane) { m_gateOff[_lane] = true; }
		void process(Block& _buf, uint16_t _lanes = 0xffff);
		void clear(int _lane);

	private:
		enum Stage { Hold = 0, Decay = 1, Attack = 2, Fade = 5, Release = 4 };

		void lane(Block& _buf, int _l);

		float m_attack[128], m_decay[128];
		int m_hold[128];

		int m_a[Lanes]{}, m_h[Lanes]{}, m_d[Lanes]{}, m_r[Lanes]{};
		float m_sustain[Lanes]{};
		bool m_adsr[Lanes]{};
		int m_stage[Lanes]{}, m_holdCount[Lanes]{};
		float m_level[Lanes]{};
		bool m_trig[Lanes]{}, m_gateOff[Lanes]{};
	};

	// CPU #1's filter envelope (0x4009652c) and the cutoff it moves (0x400956bc), a block at a time for one Track
	class FilterEnvelope
	{
	public:
		// The blocks' lengths and coefficients at _rate
		explicit FilterEnvelope(double _rate = MachineRate);

		struct Setting
		{
			int delay = 0, attack = 0, decay = 0, sustain = 0, release = 0;		// 0-127
		};

		// Lane _l's next block, _gateOff or _trig in it; its value, 0 to 1
		double step(int _l, const Setting& _s, bool _trig, bool _gateOff);
		double value(const int _l) const { return m_value[_l]; }
		void clear(int _l) { m_stage[_l] = Off; m_value[_l] = 0; m_count[_l] = 0; }

	private:
		enum Stage { Off, Decay, Attack, Delay };

		double m_decay[128], m_attack[128];
		double m_delay[128];
		int m_stage[Lanes]{};
		double m_value[Lanes]{}, m_count[Lanes]{};
	};

	// The cutoff word from FREQ, ENV and KEY's words and the note, as 0x400956bc computes them, _envelope 0 to 1
	int32_t filterCutoff(int _freq, int _depth, int _keyTrack, int _note, double _envelope);
}
