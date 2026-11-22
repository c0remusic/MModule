#pragma once

#include <array>
#include <cstdint>

#include "cyFastFm.h"

namespace cycles::fast
{
	// FMD PERC, the Model:Cycles OS 1.13's PERC (engine 3; elektron-work's cycles-perc-tone-chord/PERC.md, outside the
	// repository): two pairs of operators, each a modulator into a sine. Operator 0, a three-harmonic wave, modulates
	// operator 1; operator 2, a sine, modulates operator 3. COLR sets the three operators' ratios (operator 3 plays at
	// the note), SWEP the pitch sweep at the trig, SHPE both indices and operator 1's decay, CONT operators 0 and 2's
	// envelope targets, DEC the four decays; operators 3 and 1 mix into the drive (PUNCH), then the amp, which rises
	// over the trig's block and does not decay: the sound's decay is the operators'. GATE on makes operators 1 and 3
	// sustain and the amp hold until the note-off, then decay as operator 3. A first trig on the machine clears the
	// voice as the OS's machine change does, and ignores a note-off in its block; a later one keeps the indices' last
	// values. A trig is seen in its block (the voice ramps out) and sounds from the next; a parameter or note takes
	// effect at the next block
	class Perc
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
		static constexpr std::array<uint8_t, Parameters> Defaults{64, 26, 15, 38, 100, 64, 0, 0};

		Perc();

		// _track 0-15. The note in semitones, 60 the machine's own pitch
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

		// No operator feeds back on itself: nothing to take out for a test
		void testWithoutChaos() {}

		// One 48 kHz block of the 16 lanes
		const Block& renderBlock();

	private:
		struct Voice
		{
			bool started = false;
			uint32_t phase[Units]{};		// the operators' 24-bit accumulators
			double env[Units]{};			// the operators' envelopes
			double coef0 = 0, coef2 = 0;	// operators 0 and 2's coefficients (latched)
			int64_t sweep = 0, sweepCoef = 0;	// Q31: the sweep's envelope and its coefficient (latched)
			double index1 = 0, index3 = 0;	// operators 1 and 3's indices at the last block, in cycles
			bool gate = false, punch = false;
			Attack amp;
			double ampLevel = 0;			// its level at the last block
			double ampDecay = 0, lastOp3 = 0;	// with GATE: the amp's coefficient, operator 3's of the block before
			bool noteOff = false;			// a note-off seen, which the amp's hold takes at the next block
			PunchGain drive;
		};

		void start(Voice& _v, int _l);
		void render(Voice& _v, int _l, bool _trigBlock, bool _cut, bool _release);

		Controls m_controls;
		int m_lanes = Lanes;
		std::array<Voice, Lanes> m_voice{};
		Block m_out;
	};
}
