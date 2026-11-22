#pragma once

#include <array>
#include <cstdint>

#include "cyFastFm.h"

namespace cycles::fast
{
	// FMD SNARE, the Model:Cycles OS 1.13's SNARE (engine 1; elektron-work's cycles-snare-metal/SNARE.md, outside the
	// repository): a body and a noise. The noise is operator 0 phase-modulating itself by its last sample at an index
	// where that map is chaotic, run in the OS's integers (FeedbackOperator: the OS's noise, sample for sample),
	// phase-modulating operator 3 (SHPE's index), low-passed, under the amp envelope, which has an attack (DEC) and
	// holds while the note is held with GATE on. The body is operator 2 phase-modulated by operator 1, high-passed,
	// under its own envelope, which ignores GATE. COLR sets operators 0, 1 and 3's frequencies, SWEP the pitch sweep at
	// the trig, CONT operators 0, 1 and 3's envelopes; then the drive (PUNCH). A first trig on the machine clears the
	// voice as the OS's machine change does, and ignores a note-off in its block; a later one keeps the noise's last
	// feedback and SHPE's last index. A trig is seen in its block (the voice ramps out) and sounds from the next; a
	// parameter or note takes effect at the next block
	class Snare
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
		static constexpr std::array<uint8_t, Parameters> Defaults{64, 40, 0, 127, 8, 0, 0, 0};

		Snare();

		// _track 0-15. The note in semitones, 60 the machine's own pitch
		void setNote(int _track, int _note) { m_controls.notes[_track] = _note; }
		void setParameter(const int _track, const int _index, const uint8_t _value, const uint8_t _fine)
		{
			m_controls.setParameter(_track, _index, _value, _fine);
		}

		// For the next block rendered only, which sees the trig (the voice ramps out) and sounds it from the one after
		void trig(const int _track) { m_controls.trig(_track); }
		// A note-off: with GATE on, the noise holds until it, and decays from the second block after; one before the
		// attack's top cancels the hold
		void gateOff(const int _track) { m_controls.gateOff(_track); }

		// Only lanes 0 to _lanes - 1 rendered from the next block (a multiple of 4; 16 at construction)
		void setLanes(int _lanes);

		// The noise is the OS's integers, the same at every rate: nothing to take out for a test
		void testWithoutChaos() {}

		// One 48 kHz block of the 16 lanes
		const Block& renderBlock();

	private:
		struct Voice
		{
			bool started = false;
			uint32_t phase[Units]{};		// operators 1-3's 24-bit accumulators (operator 0's is the noise's)
			FeedbackOperator noise;		// operator 0
			double env[Units]{}, coef[Units]{};	// the operators' envelopes and their coefficients (latched)
			int64_t depth = 0, sweep = 0, sweepCoef = 0;	// Q31: the sweep's depth, envelope, coefficient (latched)
			double index = 0;				// operator 3's index on operator 0 at the last block, in cycles
			float hp[2]{}, lp[2]{};		// the body's high-pass, the noise's low-pass: last input, last output
			bool punch = false;
			Attack body, amp;
			double bodyLevel = 0, ampLevel = 0;	// their levels at the last block
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
