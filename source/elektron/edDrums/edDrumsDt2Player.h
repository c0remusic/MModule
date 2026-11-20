#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

#include "stFastStages.h"

// The Digitakt II's sample voice in ONESHOT (ticket 04 of .scratch/elektron-drums-v2), the Sampler's third player, a
// machine of syntakt::fast: 16 lanes, blocks of 32 at 48 kHz, rendered 64 at 96 kHz through a 6-tap kernel of 256
// phases, then decimated 2:1 by a half-band. Its laws are those measured at the oracle (research 03 of lockstep-post-v1,
// the ticket's "Décisions et relevés"); nothing of the firmware is read: the kernel is our own even cubic B-spline whose
// 14 coefficients were fitted to the oracle's output, the half-band our own all-pass whose 4 coefficients were fitted
// to its response. Mono, the sample as its owner holds it (int16 at its own rate). Level and velocity are not here:
// Lockstep's come after the chain. Unity gain: a full-scale sample plays at full scale (the oracle's kernel has a DC
// gain of 0.4045, which the test puts back to compare).
namespace elektron::drums
{
	// The samples the player reads, one per lane: nullptr or a length of 0 plays silence. Kept by the caller while it
	// renders, changed between blocks; a lane reads its slot again at each block: a sample replaced under a fading voice
	// is read from the next block, at the voice's position, the old one being the caller's to free
	struct Dt2Samples
	{
		struct Slot
		{
			const int16_t* pcm = nullptr;
			uint32_t length = 0;
			double rate = 48000.0;
		};
		std::array<Slot, Lanes> slots{};
	};

	class Dt2Player
	{
	public:
		static constexpr int Parameters = syntakt::fast::Controls::Parameters;
		static constexpr int Units = 1;		// none: a bench's nudges move fields it does not read
		static constexpr int Phases = 256, Taps = 6;
		static constexpr int Work = 2 * BlockSize;	// the 96 kHz block
		static constexpr double DcGain = 0.4045;	// the oracle's kernel's, which ours is scaled up from

		// Machine 1-8 as the Sampler's page lays them out on this grain (edDrumsNames.cpp), on the slots whose ROM player
		// defaults give this player its own (the Sampler's values stay across a grain change): TUNE (value - 64 semitones,
		// the fine part 1/256 of one; PTCH's 64), MODE (ONESHOT alone here; HOLD's 0), LOOP, STRT, LEN (fractions of the
		// sample, word / (127 << 8); BRR 0, STRT 0, END 127), PLAY (its four modes in the manual's order; REPEATS' 0,
		// forward). Machine 2 and 8, DECAY 127 and REPEAT GAP 64, are left out
		enum Slot : int
		{
			Tune = 0, Mode = 2, Loop = 3, Start = 4, Length = 5, Play = 6
		};
		static constexpr bool usesSlot(const int _slot) { return _slot == Tune || (_slot >= Mode && _slot <= Play); }
		// MODE, a sixth of the range each, in the Digitakt II's order: ONESHOT; GRID (the sample in 4, 8, 16, 32 or 64
		// equal slices, Machine 4; Machine 5 the slice played, 6 how many more follow it; as its OS plays it, probed at
		// the oracle: ticket 05); REPITCH (STRT and LEN over 2^BARS bars at the tempo, Machine 4 BARS, the pitch
		// following, TUNE and the note not read; ticket 05); WERP (from the trig, a window of the sample a segment long,
		// 1/32, 1/16 or 1/8 of it, Machine 5 SEGMENT, steps on by a segment over 2^BARS every as much of a note at the
		// tempo, Machine 4 BARS, the sample crossed in 1 to 8 bars; the voice plays at TUNE's pitch and stops where its
		// read passes the window's end; PLAY's direction and loop; the Digitakt II's ColdFire and SHARC run at the
		// oracle, ticket 11); STRETCH (STRT and LEN over 2^BARS bars at the tempo, Machine 4 BARS, at TUNE's pitch: a
		// grain of the sample read every 2048 outputs from where the time has reached, faded in over 128 as the last fades
		// out, rendered at the output rate without the kernel; PLAY's direction and loop; ticket 13); SLICE plays as
		// ONESHOT until its ticket (12)
		enum PlayerMode : int
		{
			OneShot, Grid, Repitch, Werp, SliceMode, Stretch
		};
		static PlayerMode modeOf(const int _value) { return static_cast<PlayerMode>(_value * 6 / 128); }
		// BARS: 2^bars bars, bars 0 to 4 (1 to 16 bars), a fifth of the range each, as the OS's own BARS, a whole number
		// (2^BARS by a shift); 0, the ROM player's BRR default, is 1 bar
		static int barsOf(const int _value) { return _value * 5 / 128; }
		// WERP: BARS 0 to 3 (1 to 8 bars), a quarter of the range each; SEGMENT 0 to 2 (1/32, 1/16, 1/8), a third each:
		// the Digitakt II's own ranges
		static int werpBarsOf(const int _value) { return _value * 4 / 128; }
		static int werpSegmentOf(const int _value) { return _value * 3 / 128; }
		// GRID: 4 << g slices, g 0 to 4 (4 to 64), a fifth of the range each
		static int gridOf(const int _value) { return 4 << (_value * 5 / 128); }
		// SLICE: which of _slices plays, 1 to _slices; LEN: how many more follow it, 0 to _slices - 1
		static int sliceOf(const int _value, const int _slices) { return 1 + std::min(_value * _slices / 128, _slices - 1); }
		static int moreSlicesOf(const int _value, const int _slices) { return std::min(_value * _slices / 128, _slices - 1); }
		enum PlayMode : int
		{
			Forward, Reverse, ForwardLoop, ReverseLoop
		};
		static PlayMode playModeOf(const int _value) { return static_cast<PlayMode>(_value * 4 / 128); }

		Dt2Player();

		// The samples it plays from the next block (nullptr: none, every lane silent)
		void setSamples(const Dt2Samples* _samples) { m_samples = _samples; }
		// The tempo REPITCH and STRETCH play at, from the next block (120 at construction)
		void setTempo(const double _bpm) { m_bpm = _bpm; }
		// The rate it renders at (48 kHz at construction, the Syntakt family's), set before its voices start: a voice's
		// step is taken at its start, REPITCH's at each block. The sample layer's player renders at its chain's, 44.1 kHz
		// on the Machinedrum family's path. Its 96 kHz stage is twice it
		void setOutputRate(const double _rate) { m_outRate = _rate; }

		// _track 0-15 (a lane). The note in semitones, 60 the sample's own pitch at TUNE 64
		void setNote(int _track, int _note);
		void setParameter(int _track, int _index, uint8_t _value, uint8_t _fine);

		// For the next block rendered only, which sees the trig (the lane fades out) and sounds it from the one after
		void trig(const int _track) { m_controls.trig(_track); }
		void gateOff(const int _track) { m_controls.gateOff(_track); }

		// Only lanes 0 to _lanes - 1 rendered from the next block (a multiple of 4; 16 at construction)
		void setLanes(int _lanes);

		void testWithoutChaos() {}

		// One 48 kHz block of the 16 lanes
		const syntakt::fast::Block& renderBlock();

		// A lane on its own, the trigs left out (the sample layer's, edDrumsLayer.h): its voice starts at once, from the
		// words as they stand, and sounds from the first sample of the next renderLane
		void start(const int _lane) { startVoice(_lane); }
		bool playing(const int _lane) const { return m_lane[static_cast<size_t>(_lane)].playing; }
		void stop(const int _lane) { m_lane[static_cast<size_t>(_lane)].playing = false; }
		// The lane's next block into _out (BlockSize samples), silence when it does not play; _fadeOut ramps the block to
		// silence and ends the voice
		void renderLane(int _lane, float* _out, bool _fadeOut);

		// The kernel's value at _t source samples from the read position (|_t| < 3), the formula the table is made of
		static double kernelAt(double _t);
		// The half-band's four all-pass coefficients, for the tests
		static const std::array<double, 4>& halfBand();

	private:
		struct Lane
		{
			double position = 0;	// in source samples, of the sample's first
			double rate = 1;		// the sample's rate over the output's: the step follows TUNE and the note block by block
			double start = 0, end = 0, loopAt = 0;
			const int16_t* pcm = nullptr;	// read again from the slot each block: the sample may change under a fading voice
			uint32_t length = 0;
			bool playing = false, reverse = false, loop = false;
			bool repitch = false;	// the step from the portion's length and the bars, not from TUNE
			double bars = 1;		// REPITCH: 2^BARS, the bars the portion takes
			// WERP: the ColdFire's sequencer in its own units (a whole sample is WerpWhole), its step (the window's length)
			// and the step clock's accumulator in them too; the window, start and end, from the position at the last step
			bool werp = false, werpLive = false, werpFresh = false;
			int werpBars = 0;
			int64_t werpStride = 0, werpPos = 0, werpAcc = 0;
			// STRETCH: the grain fading in (grain), the one fading out held as an offset behind it (behind, as the DSP
			// holds it: the two go round a looped portion together), and where the last grain started (timeline), in
			// source samples with a 31-bit fraction; the outputs since that grain started; a forward loop gone past the
			// sample's end, which starts the grains again at the next
			bool stretch = false, stretchRestart = false;
			int64_t grain = 0, behind = 0, timeline = 0;
			int grainAge = 0;
			double x[4]{}, y[4]{};	// the half-band's all-pass state, newest first
			float oddTail[4]{};		// the last block's last four odd samples at 96 kHz, the odd branch's delay
		};

		void convert(int _track);
		void startVoice(int _l);
		// WERP: one frame of the sequencer (at the trig, or 32 samples later), the window from the position it leaves
		void werpFrame(Lane& _v, bool _trig);
		static void werpWindow(Lane& _v);
		void render96(int _l, float* _work);
		// STRETCH: the step, and how far the time moves a grain, as the DSP holds them (a 31-bit fraction); the grains
		// as at the trig; the block straight at the output rate (ticket 13)
		int64_t stretchStep(int _l) const;
		int64_t stretchAdvance(const Lane& _v) const;
		void stretchStart(Lane& _v) const;
		void renderStretch(int _l, bool _fadeOut, float* _out);
		// ... to 32 at the output rate into _out; _fadeOut ramps the block to silence (a trig's block)
		void decimate(int _l, const float* _work, bool _fadeOut, float* _out);

		// A phase's taps, then zeros to 8: two SSE loads a phase
		static constexpr int RowTaps = 8;
		using Kernel = std::array<std::array<float, RowTaps>, Phases>;
		static const Kernel& kernel();
		const Kernel& m_kernel;
		const Dt2Samples* m_samples = nullptr;
		double m_bpm = 120;
		double m_outRate = 48000;
		syntakt::fast::Controls m_controls;
		int m_lanes = Lanes;
		std::array<Lane, Lanes> m_lane{};
		// What TUNE and the note convert to (convert): source samples an output at twice the output rate, at a
		// sample of the output's rate
		double m_stepOf[Lanes]{};
		syntakt::fast::Block m_out;
	};
}
