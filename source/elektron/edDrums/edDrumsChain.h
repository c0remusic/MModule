#pragma once

#include <array>
#include <cstdint>

#include "edDrumsOversampler.h"
#include "elektronBlock.h"
#include "mdFastTrackFx.h"
#include "stFastTrack.h"

// A Track's chain (spec 4, ticket 12 of .scratch/elektron-drums-v1), between its voice and its ring:
//
//     machine > drives pre > BAND (before) > FILTER > BAND (after) > drives post > AMP
//
// One Chain holds the 16 Tracks at one rate: the Machinedrum's blocks at 44.1 kHz, the Syntakt's at its 48. A stage of the
// other family on a Track computes at that Track's rate, its frequencies where its knobs say (md::fast::tables, the
// tables of syntakt::fast::Multimode). Each place of the skeleton has its own stages, so that a lane is one stage's at one
// place alone: a stage computes the pairs or 4-lane groups of its Tracks and a Track it does not hold keeps its samples,
// whatever the stage wrote in its state; a lane no one was computing starts clean at its next Hit.
//
// Every stage is unity gain here. The firmware's keep their gains, which cancel only in its order: each is told the level
// it expects at its input (StageLevel) and gives its output back at 1. In the firmware's order every factor is a power
// of 2 and the words pass exactly.
namespace elektron::drums
{
	enum class DriveType : uint8_t
	{
		Off,
		Syntakt,
		MdDist,
		MdSrr,
		Count
	};

	enum class BandType : uint8_t
	{
		Off,
		BaseWidth,
		MdEq,
		ThreeBand,
		Count
	};

	enum class FilterType : uint8_t
	{
		Off,
		Syntakt,
		Machinedrum,
		Count
	};

	// What velocity reaches, besides the level
	enum class VelocityTarget : uint8_t
	{
		None,
		DriveA,
		DriveB,
		BandA,			// BASE, EQF, the low band's gain
		BandB,			// WIDTH, EQG, the middle band's gain
		Frequency,
		Resonance,
		Width,			// FLTW
		EnvelopeDepth,
		Count
	};

	// Values 0-127 as a Kit holds them. Value-initialised, a chain with every slot off: the voice itself
	struct ChainSettings
	{
		struct Drive
		{
			DriveType type = DriveType::Off;
			bool post = false;
			int amount = 0;
			int mix = 127;			// "+": 0 dry, 127 all through the drive
		};

		struct Band
		{
			BandType type = BandType::Off;
			bool after = false;		// after the filter
			// BASE, WIDTH; EQF, EQG; or frequency and gain of the three bands, each frequency 20 Hz to 20 kHz, each gain +-18 dB
			std::array<int, 6> values{};
		};

		struct Filter
		{
			FilterType type = FilterType::Off;
			int mode = 7;			// the Syntakt's TYPE
			int frequency = 127, resonance = 0;		// on the Machinedrum's: FLTF, FLTQ
			int width = 127;		// FLTW
			syntakt::fast::FilterEnvelope::Setting envelope;
			int depth = 64;			// 64 none
			int keyTrack = 0;		// -128 to 127, the Syntakt's filter's
		};

		struct Amp
		{
			bool on = false;		// off, the voice's own: the Machinedrum's stays in its voice
			int attack = 0, hold = 127, decay = 0, sustain = 0, release = 0;
			bool adsr = false;
		};

		std::array<Drive, 2> drive;
		Band band;
		Filter filter;
		Amp amp;
		VelocityTarget velocityTarget = VelocityTarget::None;
		int velocityDepth = 0;		// -127 to 127: added to the target's value at velocity 127
	};

	// A Kit's value as a word (value << 7) moved by _offset, in 1/128 of a value, inside what a Kit's value reaches: a stage's
	// tables end there
	constexpr int wordOf(const int _value, const int _offset)
	{
		const int word = (_value << 7) + _offset;
		return word < 0 ? 0 : word > 127 << 7 ? 127 << 7 : word;
	}

	// The family's chain, as a machine of the family is first chosen, where the firmware has it. The Syntakt's stands as a
	// new Sound does: the filter lowpass and open, the amp AHD at unity; _sustained, a machine that starts in ADSR, the amp
	// in ADSR and its voice held to the gate-off. The Machinedrum's, each stage at the value that leaves the voice as it is, or
	// as near as the stage goes
	ChainSettings syntaktChain(bool _sustained = false);
	ChainSettings machinedrumChain();

	// The three bands of the "+": a low shelf, a bell of fixed Q and a high shelf, each a trapezoidal state-variable filter
	// (Simper), their words ramping over a block
	class ThreeBand
	{
	public:
		static constexpr int Bands = 3;

		// A band's filter: v1 and v2 of the state, mixed m0 x + m1 v1 + m2 v2
		struct Words
		{
			float a1 = 0, a2 = 0, a3 = 0, m0 = 1, m1 = 0, m2 = 0;
		};

		static Words lowShelf(double _hz, double _db, double _rate);
		static Words bell(double _hz, double _db, double _rate);
		static Words highShelf(double _hz, double _db, double _rate);

		ThreeBand();
		void set(int _lane, int _band, const Words& _words);
		void process(Block& _buf, uint16_t _lanes = 0xffff);
		void clear(int _lane);

	private:
		enum { A1, A2, A3, M0, M1, M2, WordCount };

		alignas(16) float m_now[Bands][WordCount][Lanes]{}, m_to[Bands][WordCount][Lanes]{};
		alignas(16) float m_ic1[Bands][Lanes]{}, m_ic2[Bands][Lanes]{};
	};

	class Chain
	{
	public:
		struct StageLevel;

		// A stage changed under a sounding Track: it runs unheard over WarmSamples, a cold filter's first samples ringing at
		// -44 dB of a tone, then takes over in FadeSamples
		static constexpr int FadeSamples = 64, WarmSamples = BlockSize;
		static constexpr int MachinedrumFilterDelay = 4;

		// The longest a chain delays a voice: the Machinedrum's filter and two drives raised 4x
		static constexpr int MaxDelay = MachinedrumFilterDelay + 2 * Oversampler::delay(4);

		// _syntakt: the Tracks here are the Syntakt's, whose firmware's chain this one's drives stay
		Chain(double _rate, bool _syntakt);

		// 1 (off), 2 or 4, from the next block: every drive but a firmware's stage where its firmware has it, and but SRR,
		// whose folding is its sound. A Track in its family's chain stays the firmware's
		void setOversampling(int _factor);

		// Track _lane's chain from the next block, the Chain's own copy, inside the Kit's values; a stage that changes
		// crossfades over FadeSamples while the Track sounds, one that changes during that once it ends
		void configure(int _lane, const ChainSettings& _settings);

		// An LFO's move of _target on _lane from the next block, in 1/128 of a value, inside the value's range there
		void modulate(int _lane, VelocityTarget _target, int _offset);

		// The most an LFO may move _target on _lane in a block, in 1/128 of a value, which is what a square's edge becomes: the
		// Machinedrum's filter takes a whole range in 24 blocks, 17 ms at 44.1 kHz (16 give -48.6 dB of HF), the Syntakt's in 8,
		// drives and bands 24
		int moveLimit(int _lane, VelocityTarget _target) const;

		// The note its filter's key tracking follows
		void setNote(int _lane, int _note);

		// _lane's state back to silence, what it was fading finished
		void clear(int _lane);

		// A Hit at _velocity. A Syntakt chain: the block that sees it, its voice sounding from the next; a Machinedrum chain: the
		// block it sounds from
		void hit(int _lane, int _velocity);

		// A note's end, for a lane that sounds
		void gateOff(int _lane);

		// The Tracks of _active, in place
		void process(Block& _buf, uint16_t _active);

		// Whether _lane's amp closes its voice
		bool ampOn(const int _lane) const { return m_ampLanes >> _lane & 1; }

		// Whether _lane's amp holds its voice to the gate-off, as ADSR or a HOLD at its top do on the machine
		bool holdsVoice(int _lane) const;

		// What _lane's amp lets through at the moment, 1 without one
		float ampLevel(const int _lane) const { return ampOn(_lane) ? m_amp.level(_lane) : 1.0f; }

		// The samples a voice comes late through _lane's chain: the Machinedrum's filter, the drives raised
		int delay(int _lane) const;

		// Of those, the filter's: a pure delay, so that nothing of a Hit leaves the chain in its first samples
		int pureDelay(int _lane) const;

	private:
		enum Place { Pre0, Pre1, BandBefore, FilterPlace, BandAfter, Post0, Post1, Places };

		// Off, then the three stages of a place
		static constexpr int Types = 4;
		static constexpr int8_t NoPending = -1;

		struct DriveStages
		{
			explicit DriveStages(double _rate);
			syntakt::fast::TrackDrive syntakt;
			md::fast::Dist dist;
			md::fast::Srr srr;
			std::array<Oversampler, Types> rates;		// by DriveType: a raised drive's history is its own
		};

		struct BandStages
		{
			explicit BandStages(double _rate);
			syntakt::fast::BaseWidth baseWidth;
			md::fast::Eq eq;
			ThreeBand threeBand;
		};

		struct FilterStages
		{
			explicit FilterStages(double _rate);
			syntakt::fast::Multimode syntakt, syntaktOther;
			md::fast::Filter machinedrum;
		};

		template<typename Function> void withStage(int _place, int _type, Function&& _function);
		template<typename Stage> void runDrive(Stage& _stage, StageLevel _level, int _place, int _type, Block& _buf, uint16_t _lanes);

		static bool isDrive(const int _place) { return _place <= Pre1 || _place >= Post0; }
		static std::array<int, 2> drivePlaces(const ChainSettings& _settings);	// each drive's, the first of a side first
		DriveStages& driveAt(int _place);
		BandStages& bandAt(int _place);
		syntakt::fast::Multimode& syntaktFilter(int _lane);

		void place(int _lane, int _place, int _type);
		void endFade(int _place, int _lane);
		int offset(int _lane, VelocityTarget _target) const;		// in 1/128 of a value: velocity's and the LFOs'
		int envelopeDepth(int _lane) const;							// a value as a word, 0 to $3F80
		void setDrive(int _lane, int _place, const ChainSettings::Drive& _drive, int _offset);
		void setBand(int _lane, int _place, bool _a = true, bool _b = true);		// the three bands: only those of A and B
		void setFilter(int _lane, double _envelope);
		void setValues(int _lane);
		void takeMoves(int _lane, bool _filterSet);
		void stepEnvelopes(uint16_t _active);
		void run(int _place, int _type, Block& _buf, uint16_t _lanes);
		void crossfade(int _place, uint16_t _fading, uint16_t _active, Block& _buf);
		void clearStage(int _place, int _type, int _lane);
		void clearStages(int _lane);
		bool raised(bool _post, int _type) const;

		double m_rate;
		bool m_syntakt;
		int m_factor = 1;

		std::array<DriveStages, 4> m_drives;
		std::array<BandStages, 2> m_bands;
		FilterStages m_filter;
		syntakt::fast::TrackAmp m_amp;
		syntakt::fast::FilterEnvelope m_envelope;

		// At each place, the stage type of each lane, as a mask per type (0 the Off one holds no lane)
		std::array<std::array<uint8_t, Lanes>, Places> m_type{}, m_old{};
		std::array<std::array<int8_t, Lanes>, Places> m_pending{};	// the change that waits for the crossfade's end
		std::array<std::array<uint16_t, Types>, Places> m_lanes{};
		std::array<std::array<int, Lanes>, Places> m_fade{};		// the samples of the crossfade done
		std::array<uint16_t, Places> m_fading{};
		alignas(16) float m_mix[Places][Lanes];						// a drive's wet share, where its block ends
		alignas(16) float m_mixNow[Places][Lanes];					// ... and where it begins

		uint16_t m_ampLanes = 0;
		uint16_t m_reset = 0, m_resetNext = 0;		// the lanes a Hit sounds from, in this block and the next
		uint16_t m_live = 0;						// the Tracks the last block computed
		std::array<ChainSettings, Lanes> m_settings;
		std::array<int, Lanes> m_note{}, m_velocity{};
		std::array<bool, Lanes> m_other{};			// its Syntakt filter is syntaktOther
		std::array<bool, Lanes> m_trig{}, m_gateOff{};
		std::array<std::array<int, static_cast<size_t>(VelocityTarget::Count)>, Lanes> m_modulation{};	// in 1/128 of a value
		std::array<uint16_t, Lanes> m_moved{};						// the VelocityTargets whose words wait for the lane's next block
	};
}
