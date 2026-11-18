#pragma once

#include <array>
#include <cstdint>

#include "edDrumsDt2Player.h"

// The sample layer (ticket 06 of .scratch/elektron-drums-v2, spec v2 §7): a sample of its own on any Track, which the
// Digitakt II's voice plays in ONESHOT with its four PLAY modes, at the rate of the chain the Track plays on (44.1 kHz on
// the Machinedrum family's path, 48 kHz on the Syntakt family's), summed with the machine before the pre drives. After
// the player: its own bit reduction (the Analog Rytm's law), an attack-hold-decay envelope and its level. Its Hit waits
// the machine's own start, so that both sound at the Track's Hit, then its offset. A Hit while the layer sounds fades the
// old voice over a block as the new one starts. Off by default, and nothing rendered while off or silent
namespace elektron::drums
{
	struct LayerSettings
	{
		bool on = false;
		// 0-127 each: TUNE semitones from 64, FINE a half semitone either way from 64; PLAY, START, LENGTH and LOOP as the
		// DT2 player reads them on its ONESHOT page (Dt2Player's slots)
		int tune = 64, fine = 64, play = 0, start = 0, length = 127, loop = 0;
		int level = 100;		// (level / 127)^2
		int bitReduction = 0;	// BR: 0 off
		int attack = 0, hold = 0, decay = 127;	// decay 127 holds the level to the sample's end
		int offset = 0;			// 0 to 100 ms after the Hit
		bool noteTrack = true;	// the Track's note moves the pitch, 60 its own
	};

	// What an LFO moves of a layer (SampleLayers::modulate): its values but ON and KEY TRACK, in LayerSettings' order
	enum class LayerMove : uint8_t
	{
		Tune, Fine, Play, Start, Length, Loop, Level, BitReduction, Attack, Hold, Decay, Offset, Count
	};

	class SampleLayers
	{
	public:
		// _rate: the chain's, 44.1 or 48 kHz
		explicit SampleLayers(double _rate);

		// The sample the Track's layer plays (pcm nullptr: none), kept by the caller while a voice may read it; the voices
		// sounding fade out over their next block
		void setSample(int _track, const Dt2Samples::Slot& _slot);
		// Off cuts what sounds; the others reach the next Hit, TUNE, FINE, LEVEL and BR the voices sounding too
		void setSettings(int _track, const LayerSettings& _settings);
		void setNote(int _track, int _note);
		// An LFO's move of _move on _track, in 1/128 of a value, inside the value's range, reaching what setSettings
		// says
		void modulate(int _track, LayerMove _move, int _offset);

		// A Hit: the layer's first sample comes _startIn samples into the next block render gives, plus its offset.
		// PLAY, START, LENGTH, LOOP, the envelope and OFFSET are taken here, as the Hit's Locks and LFOs leave them.
		// _moved: how much later than the last block's end the next block's first sample lies, when the Track's ring
		// starts a new run there (the Machinedrum path's, Engine::hitMachinedrum): what sounds of the layer goes on placed
		// as it was
		void hit(int _track, int _startIn, int _moved = 0);
		// Its voices fade out over their next block
		void cut(int _track);
		// Its voices end at once (the Track leaves this path, or its ring ramps over what was rendered)
		void stop(int _track);

		bool sounding(const int _track) const { return m_sounding >> _track & 1; }
		uint32_t soundingTracks() const { return m_sounding; }

		// The layer's next block of _track added to _x (BlockSize samples); nothing for a Track that does not sound
		void render(int _track, float* _x);

		// Rytm's BR: the step a 16-bit input is truncated to, in LSB; 1 at BR 0, where nothing is reduced
		static double reductionStep(double _br);
		static double attackSeconds(int _attack);

	private:
		static constexpr int Voices = 2;	// a Hit's new voice and the old one fading under it

		// A voice's delay line: its place inside a block (up to 31) plus a ring's move (up to 7 at 44.1 kHz,
		// Engine::hitMachinedrum) plus the block written ahead of the read
		static constexpr int Delay = 128;

		struct Voice
		{
			int wait = -1;		// samples before the voice starts, -1 started or none
			bool live = false;	// the player plays it, or its delay still holds samples
			bool fade = false;	// fades out over its next block
			bool tail = false;	// the player is over, the delay gives its last samples
			// The delay line: samples written and read, silence to give before the next read
			float line[Delay]{};
			int64_t written = 0, read = 0;
			int silence = 0;
			// The envelope as its Hit took it; its gain, the attack's step per sample, the hold's samples left, the
			// decay's factor
			int attack = 0, hold = 0, decay = 127;
			float gain = 0, attackStep = 1, fall = 1;
			int64_t holdLeft = 0;
			bool attacking = false;
		};

		// _base, a value of _track's settings, moved by its LFOs, in values
		double moved(int _track, LayerMove _move, int _base) const;
		int whole(int _track, LayerMove _move, int _base) const;
		void apply(int _track);
		void startVoice(int _track, int _voice);
		void renderVoice(int _track, int _voice, float* _x);
		static void drain(Voice& _voice, float* _x);
		void updateSounding(int _track);

		const double m_rate;
		std::array<Dt2Player, Voices> m_players;
		Dt2Samples m_samples{};		// both players'
		std::array<LayerSettings, Lanes> m_settings{};
		std::array<std::array<int, static_cast<size_t>(LayerMove::Count)>, Lanes> m_moves{};
		std::array<int, Lanes> m_note{};
		std::array<int, Lanes> m_current{};		// the voice the last Hit took
		std::array<std::array<Voice, Lanes>, Voices> m_voices{};
		std::array<float, Lanes> m_level{}, m_step{};
		uint32_t m_sounding = 0;
	};
}
