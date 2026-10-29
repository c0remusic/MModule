#pragma once

#include <algorithm>
#include <cstdint>

#include "stFastStages.h"

namespace syntakt::fast
{
	// SY TONE's own stages at 96 kHz, which its test checks against the exact port's
	namespace syTone
	{
		// 0x40003aac, as scaledFeedbackSine runs 0x40003958 at 48 kHz: unit 0, the RAM sine phase-modulated by its own
		// previous output under its envelope, 64 samples at 96 kHz in the machine's own integers per lane, the sample
		// halved (a float one drifts from it where the feedback is strong, as CP VINTAGE's unit 1 does). _scaled the
		// output under the envelope, two blocks, _last its previous sample (Q31); _inc its increment (its first
		// sample's, the same over the block); the feedback ramped from _fb to the words' _fbWord >> 4 by (target -
		// start) >> 6 a sample, the envelope from _from to _to by their difference / 64 truncated, Q31 words in the
		// machine's negative representation, both left at their targets
		void feedbackUnit(Block (&_scaled)[2], uint32_t* _phase, const Increments& _inc, int32_t* _fb,
			const int32_t* _fbWord, int32_t* _last, int32_t* _from, const int32_t* _to);

		// 0x400051c6: the 64 samples at 96 kHz (_in, two blocks) halved, decimated 2:1 into _out: each output the sum
		// of an even input and a 4th-order allpass's output on the odd inputs, the even one 7 inputs before the
		// allpass's newest, clipped as the machine's sum saturates; _x the last 8 inputs, _y the allpass's last 4
		// outputs
		void decimate(const Block (&_in)[2], Block& _out, float (*_x)[Lanes], float (*_y)[Lanes]);
	}

	// SY TONE on 16 Tracks: two-operator FM at 96 kHz. Unit 0, a sine feeding back on its own output under its
	// envelope, phase-modulates unit 1, a sine at the note; unit 1 halved is decimated 2:1 by a polyphase IIR halfband
	// to 48 kHz, then goes through the amp envelope and the drive stage. The same voice as syntakt::syTone, block for
	// block: a trig is seen in its block (the Track fades out) and sounds from the next; a parameter or note takes
	// effect at the next block. It renders the machine's own 48 kHz blocks, its operators' 64 samples at 96 kHz as two
	// blocks of 32, its block-rate control stepped once per block (Host: on a host's)
	class SyTone
	{
	public:
		static constexpr int Parameters = Controls::Parameters;
		static constexpr int Units = 2;

		// Its tables come from stFastTables' formulas: nothing of the firmware
		SyTone();

		// _track 0-15. The note in semitones; parameter _index 0-7 as VoiceCpu's Machine page: TUNE, unit 0's ratio,
		// unit 1's modulation by it, unit 0's feedback, unit 0's decay and level at rest, the drive switch, HOLD,
		// DECAY; _value 0-127
		void setNote(int _track, int _note);
		void setParameter(int _track, int _index, uint8_t _value, uint8_t _fine);

		// For the next block rendered only
		void trig(const int _track) { m_controls.trig(_track); }
		void gateOff(const int _track) { m_controls.gateOff(_track); }

		// For tests: unit 0's feedback table at 0
		void testWithoutChaos();

		// For tests: unit 0's words on _track as the last block left them, as the voice holds them: its envelope's
		// level and where its next ramp starts (0x40003c0e's, the voice's 0x7c and 0x80), its last output (0x28), its
		// feedback (0x30)
		void testWords(const int _track, int32_t* _words) const
		{
			const int32_t w[4] = {m_env[_track], m_envFrom[_track], m_last[_track], m_fb[_track]};
			std::copy_n(w, 4, _words);
		}

		// One 48 kHz block of the 16 Tracks
		const Block& renderBlock();

	private:
		void convert(int _track);
		void control();

		// The tables SY TONE reads (stFastTables, section 7's words by formula): as words where the machine computes
		// in integers (unit 0, its envelope, the pitch, unit 1's modulation), the amp's decay as floats
		struct Tables
		{
			int32_t feedback[128], ratio[128], index[128], modTarget[128], modDecay[128];
			float ampDecay[128];
			int32_t exp2[2048];
			float sine[258];
		};
		Tables m_tables;

		// Each Track's parameters as words, its note and its trigs
		Controls m_controls;

		// What they convert to (convert): read every block, or latched at a trig (*Next)
		Increments m_inc[Units]{};				// at 96 kHz
		int32_t m_fbWord[Lanes]{};				// unit 0's feedback, the machine's word (its 0x2c)
		int32_t m_target[Lanes]{};				// unit 0's level at rest, the machine's word (its 0x5c)
		alignas(16) float m_depthTarget[Lanes]{};	// unit 1's modulation by unit 0, cycles per unit
		float m_ampDecay[Lanes]{};
		int32_t m_decayNext[Lanes]{};			// unit 0's decay, the machine's word (its 0x58)
		bool m_driveNext[Lanes]{}, m_hold[Lanes]{};

		// The voices' state
		bool m_playing[Lanes]{};	// since its first trig
		alignas(16) uint32_t m_phase[Units][Lanes]{};
		int32_t m_fb[Lanes]{}, m_last[Lanes]{};	// unit 0's feedback ramped from (its 0x30), its last output (0x28)
		alignas(16) float m_depth[Lanes]{}, m_depthMid[Lanes]{};	// unit 1's modulation ramped from, and half-way

		// Unit 0's envelope in the machine's words (0x40003c0e: negative, INT32_MIN full), at the end of the last block
		// and of this one, and its decay latched at the trig
		int32_t m_envFrom[Lanes]{}, m_env[Lanes]{}, m_decay[Lanes]{};

		AmpEnvelope m_amp;
		Drive m_drive;

		// The amp's envelope over the block, sample by sample, and at its end
		Block m_gAmp;
		float m_ampEnd[Lanes]{};

		// The decimator's history: its last 8 inputs and its recursion's last 4 outputs
		alignas(16) float m_decX[8][Lanes]{}, m_decY[4][Lanes]{};

		// The operators' 64 samples at 96 kHz, two blocks each: unit 0 under its envelope, unit 1
		Block m_mod[2], m_car[2], m_out;
	};
}
