#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "stFastStages.h"

namespace syntakt::fast
{
	// The samples SP TWINSHOT plays, laid out as the machine reads them: 64 slots, each a length in samples and the
	// address of its first sample, in an address space where data[k] lies at base + 2 k (slot addresses base + 2 k);
	// address 0 is no sample. A read outside the data is 0. The machine holds none: its caller gives them. As the
	// machine reads them, which a loader must keep:
	//	- base is not 0, or the data at it plays silence
	//	- a slot's address is base + 2 k, never odd
	//	- a length is clamped to 2^19 - 1 samples (10.9 s), but a reversed start is the length before the clamp: a
	//	  longer sample played reversed starts out of range and stops
	//	- a voice keeps its slot's address until it loops or is triggered again: change the set between blocks
	struct TwinshotSamples
	{
		struct Slot
		{
			uint32_t length = 0;
			uint32_t address = 0;
		};

		std::array<Slot, 64> slots{};
		uint32_t base = 0;
		std::vector<int16_t> data;
	};

	// A player's record, as the machine keeps it (9 words): its sample's address, the address a loop goes on at, the
	// position (Q12 samples), the length and the next length (Q12), whether it plays, its rate (Q12 samples a sample),
	// whether it plays reversed, whether it loops
	namespace twinshotRecord
	{
		enum : int
		{
			Pointer, NextPointer, Position, Length, NextLength, Playing, Rate, Reverse, Loop, Words
		};
	}

	// SP TWINSHOT's own stages, public for its tests

	// 0x40006764 on lane _l, in the machine's integers: 32 samples of the player _record from _samples (nullptr:
	// none) into lane _l of _out, forward or reversed, two samples interpolated to the 16 bits the machine keeps; at
	// the end it loops on the next address and length, or stops
	void twinshotPlayer(Block& _out, int _l, int32_t* _record, const TwinshotSamples* _samples);

	// 0x400068ea on 16 lanes: two one-poles in series (coefficients _c0, _c1; states _s1, _s2), each step clipped to
	// full scale as the machine's saturates; the first's output minus the second's, in place
	void twinshotBandPass(Block& _buf, const float* _c0, const float* _c1, float* _s1, float* _s2);

	// SP TWINSHOT on 16 Tracks: two sample players at the note, each reading one of 64 samples forward or reversed;
	// the first through a band-pass and the amp envelope, the second, which loops while HOLD is on, as it is; a
	// crossfade sets their levels. The same voice as syntakt::spTwinshot, block for block: a trig is seen in its block
	// (the Track fades out) and sounds from the next; a parameter or note takes effect at the next block. Its players
	// run in the machine's integers (bit-exact), what follows them in float
	class SpTwinshot
	{
	public:
		static constexpr int Parameters = Controls::Parameters;
		static constexpr int Units = 1;	// none: the bench's nudges move fields it does not read

		// Its tables come from stFastTables' formulas: nothing of the firmware
		SpTwinshot();

		// The samples it plays from the next block (nullptr: none, every Track silent), kept by the caller while it
		// renders; not while rendering
		void setSamples(const TwinshotSamples* _samples) { m_samples = _samples; }

		// _track 0-15. The note in semitones; parameter _index 0-7 as VoiceCpu's Machine page: TUNE and sample of the
		// first player, the band-pass, the crossfade, TUNE and sample of the second, HOLD (the second loops), DECAY;
		// _value 0-127
		void setNote(int _track, int _note);
		void setParameter(int _track, int _index, uint8_t _value, uint8_t _fine);

		// For the next block rendered only
		void trig(const int _track) { m_controls.trig(_track); }
		void gateOff(const int _track) { m_controls.gateOff(_track); }

		// SP TWINSHOT has no feedback: nothing to take out
		void testWithoutChaos() {}

		// For tests: _track's words as the last block left them, as the voice holds them (Fields in its test)
		void testWords(int _track, int32_t* _words) const;

		// One 48 kHz block of the 16 Tracks
		const Block& renderBlock();

	private:
		void convert(int _track);
		void control();

		// The tables it reads (stFastTables, section 7's words by formula)
		std::array<int32_t, 2048> m_exp2;
		std::array<int32_t, 128> m_pole;
		float m_ampDecayTable[128];

		const TwinshotSamples* m_samples = nullptr;
		Controls m_controls;

		// What the parameters convert to (convert): the players' rates, the mix's levels (0x08, 0x0c), the band-pass's
		// coefficients, the slots and directions, the amp's decay
		int32_t m_rateWord[2][Lanes]{}, m_levelWord[2][Lanes]{}, m_poleWord[2][Lanes]{}, m_slot[2][Lanes]{};
		bool m_reverse[2][Lanes]{};
		float m_ampDecay[Lanes]{};

		// The voices' state
		bool m_playing[Lanes]{};	// since its first trig
		int32_t m_record[2][Lanes][twinshotRecord::Words]{};
		int32_t m_target[2][Lanes]{}, m_from[2][Lanes]{};	// the mix's levels and where they ramp from (0x18, 0x1c)
		alignas(16) float m_gFrom[2][Lanes]{}, m_gTo[2][Lanes]{};
		alignas(16) float m_c0[Lanes]{}, m_c1[Lanes]{}, m_s1[Lanes]{}, m_s2[Lanes]{};

		AmpEnvelope m_amp;

		// The amp's envelope over the block, sample by sample, and at its end
		Block m_gAmp;
		float m_ampEnd[Lanes]{};

		Block m_player[2], m_out;
	};
}
