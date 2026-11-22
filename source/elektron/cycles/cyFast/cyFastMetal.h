#pragma once

#include <array>
#include <cstdint>

#include "cyFastFm.h"

namespace cycles::fast
{
	// FMD METAL, the Model:Cycles OS 1.13's METAL (engine 2; elektron-work's cycles-snare-metal/METAL.md, outside the
	// repository): two pairs of operators, each a modulator into a five-harmonic wave (harmonics 1, 4, 7, 10 and 13).
	// Operator 0, a sine at the product of SWEP's and COLR's ratios, modulates operator 1 at SWEP's; operator 2, at
	// COLR's, modulates itself by its last sample (run in the OS's integers: chaotic at the top of SHPE) and operator
	// 3, at the note. SHPE sets both indices and the feedback, CONT operators 0 and 2's envelopes; operators 1 and 3
	// sum into a resonant high-pass, the drive (PUNCH), then the amp envelope (DEC), held while the note is held with
	// GATE on. A first trig on the machine clears the voice as the OS's machine change does, and ignores a note-off in
	// its block; a later one keeps the last feedback and index. A trig is seen in its block (the voice ramps out) and
	// sounds from the next; a parameter or note takes effect at the next block
	class Metal
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
		static constexpr std::array<uint8_t, Parameters> Defaults{64, 20, 16, 46, 48, 0, 0, 0};

		Metal();

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

		// Operator 2 is the OS's integers, the same at every rate: nothing to take out for a test
		void testWithoutChaos() {}

		// One 48 kHz block of the 16 lanes
		const Block& renderBlock();

	private:
		struct Voice
		{
			bool started = false;
			uint32_t phase[Units]{};		// operators 0, 1 and 3's 24-bit accumulators (operator 2's is its own)
			FeedbackOperator op2;
			double env[Units]{}, coef[Units]{};	// the operators' envelopes and their coefficients (latched)
			double index = 0;				// operators 1 and 3's index at the last block, in cycles
			float x[2]{}, y[2]{};			// the high-pass's last inputs and outputs
			Attack amp;
			double ampLevel = 0;			// its level at the last block
			bool noteOff = false;			// a note-off seen, which the amp's hold takes at the next block
			bool punch = false;
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
