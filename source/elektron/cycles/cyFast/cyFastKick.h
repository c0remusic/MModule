#pragma once

#include <array>
#include <cstdint>

#include "cyFastFm.h"

// The FM DRUMS family's machines (ticket 18 of .scratch/elektron-drums-v2): the voices of the Model:Cycles OS 1.13,
// their laws read in its code and checked at its oracle (elektron-work's cycles-kick/KICK.md, outside the
// repository), rewritten in float. Machines of syntakt::fast's kind (stFastStages.h): 16 lanes, blocks of 32 at
// 48 kHz, Host for another rate; nothing of the firmware is read, their parameter curves are fitted constants
// (cyFastKickCurves.inc).
namespace cycles::fast
{
	// FMD KICK: four sine operators. Operator 0 phase-modulates itself by its last sample; operator 2, a five-harmonic
	// wave under its own envelope, is phase-modulated by operator 1; the two mix (COLR and SHPE's gains) under the
	// index envelope (CONT) into the phase of operator 3, at the note, whose pitch the sweep (SWEP) raises at the trig.
	// A click (SHPE) is added after the drive (PUNCH), then the amp envelope (DEC), held while the note is held with
	// GATE on. A trig is seen in its block (the voice ramps out) and sounds from the next; a parameter or note takes
	// effect at the next block
	class Kick
	{
	public:
		static constexpr int Parameters = Controls::Parameters;
		static constexpr int Units = 4;

		// Machine 1-8: PTCH (semitones about 64, 40 to 88), DEC, COLR, SHPE, SWEP, CONT, PUNCH and GATE (on from 64)
		enum Slot : int
		{
			Pitch, Decay, Color, Shape, Sweep, Contour, Punch, Gate
		};
		// The OS's sound for the machine (its parameter sheets): Machine 1-8's values
		static constexpr std::array<uint8_t, Parameters> Defaults{64, 28, 10, 16, 16, 24, 0, 0};

		Kick();

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

		void testWithoutChaos() {}

		// One 48 kHz block of the 16 lanes
		const Block& renderBlock();

	private:
		struct Voice
		{
			bool started = false;
			uint32_t phase[Units]{};	// 24-bit accumulators, as the OS's
			int64_t sweep = 0, sweepCoef = 0;	// Q31: the sweep envelope and its coefficient a block (latched)
			float op2 = 1;						// operator 2's envelope
			float index = 1, indexCoef = 0;	// the index envelope and its coefficient (latched)
			bool indexHeld = true;				// it holds one block more than the others
			float amp = 1;
			int ampStage = 1;					// 2: GATE holds it until a note-off; 1: decaying
			bool released = false;				// a note-off seen, which ends the hold at the next block
			float last = 0;						// operator 0's last sample, its feedback
			float feedback = 0, colorGain = 0, shapeGain = 0, click = 1, clickLevel = 0;
			float hp[2]{}, lp[2]{};				// the click's filters: last input, last output
			bool punch = false;
			int punchStage = 1;
			float punchIn = 1, punchGain = 1;
		};

		void start(Voice& _v, int _l);
		void render(Voice& _v, int _l, bool _trigBlock, bool _cut, bool _release);

		Controls m_controls;
		int m_lanes = Lanes;
		std::array<Voice, Lanes> m_voice{};
		Block m_out;
	};
}
