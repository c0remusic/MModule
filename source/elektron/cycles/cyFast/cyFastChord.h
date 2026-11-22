#pragma once

#include <array>
#include <cstdint>

#include "cyFastChordWaves.h"
#include "cyFastFm.h"

namespace cycles::fast
{
	// FMD CHORD, the Model:Cycles OS 1.13's CHORD (engine 5; elektron-work's cycles-perc-tone-chord/CHORD.md, outside
	// the repository): four plain oscillators, each reading two of 32 waves at one phase and crossfading them (SWEP),
	// tuned as a chord or detuned unisons (SHPE, COLR: the chord's voices fade in, then its inversions), mixed, driven
	// (PUNCH), then two one-pole low-passes whose cutoff, which follows the pitch, an envelope sweeps down (CONT its
	// depth, DEC its decay, the amp's and the operators' attack). GATE on holds the amp until the note-off. The waves
	// are the OS's own, which its owner reads from the user's OS (cyFastChordWaves.h) and gives it: without them it
	// is silent. A first trig on the machine clears the voice as the OS's machine change does, and ignores a note-off
	// in its block; a later one keeps the crossfade's, the gains' and the increments' last values. A trig is seen in
	// its block (the voice ramps out) and sounds from the next; a parameter or note takes effect at the next block
	class Chord
	{
	public:
		static constexpr int Parameters = Controls::Parameters;
		static constexpr int Units = 4;

		// Machine 1-8: PTCH (semitones about 64), DEC, COLR, SHPE, SWEP, CONT, PUNCH and GATE (on from 64)
		enum Slot : int
		{
			Pitch, Decay, Color, Shape, Sweep, Contour, Punch, Gate
		};
		// The OS's sound for the machine (its records' words): Machine 1-8's values
		static constexpr std::array<uint8_t, Parameters> Defaults{64, 64, 32, 3, 43, 24, 0, 0};

		Chord();

		// The waves it plays from the next block (nullptr: none, every Track silent), kept by the caller while it
		// renders; not while rendering
		void setWaves(const ChordWaves* _waves) { m_waves = _waves; }
		bool hasWaves() const { return m_waves != nullptr; }

		// _track 0-15. The note in semitones, 60 the machine's own pitch (96 at most, as the OS clamps it)
		void setNote(int _track, int _note) { m_controls.notes[_track] = _note; }
		void setParameter(const int _track, const int _index, const uint8_t _value, const uint8_t _fine)
		{
			m_controls.setParameter(_track, _index, _value, _fine);
		}

		// For the next block rendered only, which sees the trig (the voice ramps out) and sounds it from the one after
		void trig(const int _track) { m_controls.trig(_track); }
		// A note-off: with GATE on, the voice holds until it, and decays from the second block after
		void gateOff(const int _track) { m_controls.gateOff(_track); }

		// Only lanes 0 to _lanes - 1 rendered from the next block (a multiple of 4; 16 at construction)
		void setLanes(int _lanes);

		// One 48 kHz block of the 16 lanes
		const Block& renderBlock();

		// SHPE's and COLR's words as the OS lays the chord out (0x400aae88): the operators' ratios / 32 (Q31, into the
		// integer pitch chain) and voices 1-3's gains (voice 0's is a fixed 0.25)
		static void layout(int32_t _shape, int32_t _color, int64_t* _ratio, double* _gain);

	private:
		struct Voice
		{
			bool started = false;
			uint32_t phase[Units]{};		// the operators' 24-bit accumulators
			int64_t inc[Units]{};			// their increments at the last block (an operator over 2217 Hz keeps it)
			double env = 0, step = 0;		// the operators' envelope, and its attack a block (latched)
			double gain[Units - 1]{};		// voices 1-3's gains at the last block
			double weight[2]{};				// the crossfade's weights at the last block's end
			int index = 0, waveA = 0, waveB = 0;	// the last block's SWEP index and its two waves (0 the sine)
			double sweep = 1, sweepCoef = 0;	// the filter sweep, and its coefficient a block (latched)
			float filter[2][2]{};			// the low-passes' last input and output
			Attack amp;
			double ampLevel = 0;			// its level at the last block
			bool noteOff = false;			// a note-off seen, which the amp's hold takes at the next block
			bool punch = false;
			PunchGain drive;
		};

		void start(Voice& _v, int _l);
		void render(Voice& _v, int _l, bool _trigBlock, bool _cut, bool _release);
		// SWEP index 0-32's wave, and what the crossfade compares
		const std::array<float, ChordWaves::Entries>& wave(int _index) const;
		uint32_t waveId(int _index) const;

		Controls m_controls;
		int m_lanes = Lanes;
		const ChordWaves* m_waves = nullptr;
		std::array<Voice, Lanes> m_voice{};
		Block m_out;
	};
}
