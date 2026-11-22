#pragma once

#include <array>
#include <cstdint>

#include "cyFastFm.h"

namespace cycles::fast
{
	// FMD TONE, the Model:Cycles OS 1.13's TONE (engine 4; elektron-work's cycles-perc-tone-chord/TONE.md, outside the
	// repository): two operators at 96 kHz, 64 samples a block, then the OS's polyphase half-band decimator back to
	// 48 kHz. Operator 0, at COLR's musical ratio, modulates itself by its enveloped output (SWEP's feedback: chaotic
	// from SWEP about 72) and operator 1, at the note (SHPE's index), whose raw sine is the sound. CONT sets operator
	// 0's envelope (its target and decay), DEC the amp's decay; then the drive (PUNCH). GATE on holds the amp until the
	// note-off. A first trig on the machine clears the voice as the OS's machine change does, and ignores a note-off in
	// its block; a later one keeps the feedback's and the index's last values. A trig is seen in its block (the voice
	// ramps out) and sounds from the next; a parameter or note takes effect at the next block
	class Tone
	{
	public:
		static constexpr int Parameters = Controls::Parameters;
		static constexpr int Units = 2;
		static constexpr int Oversampled = 2 * BlockSize;	// the operators' samples a block, at 96 kHz

		// Machine 1-8: PTCH (semitones about 64), DEC, COLR, SHPE, SWEP, CONT, PUNCH and GATE (on from 64)
		enum Slot : int
		{
			Pitch, Decay, Color, Shape, Sweep, Contour, Punch, Gate
		};
		// The OS's sound for the machine (its records' words): Machine 1-8's values
		static constexpr std::array<uint8_t, Parameters> Defaults{64, 42, 24, 40, 38, 52, 0, 0};

		Tone();

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

		// For tests: operator 0 without its feedback, nothing chaotic left
		void testWithoutChaos() { m_withoutChaos = true; }

		// One 48 kHz block of the 16 lanes
		const Block& renderBlock();

	private:
		struct Voice
		{
			bool started = false;
			uint32_t phase[Units]{};		// the operators' 24-bit accumulators, at 96 kHz
			double env = 0, coef = 0;		// operator 0's envelope and its coefficient (latched)
			double feedback = 0, index = 0;	// operator 0's feedback and operator 1's index at the last block
			float last = 0;				// operator 0's last enveloped output
			float history[8]{}, recursion[4]{};	// the decimator's last inputs and outputs
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
		bool m_withoutChaos = false;
		std::array<Voice, Lanes> m_voice{};
		Block m_out;
	};
}
