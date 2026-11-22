#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

#include "stFastStages.h"

// What the FM DRUMS family's machines share (cyFastKick, cyFastSnare, cyFastMetal, cyFastPerc, cyFastTone,
// cyFastChord): their parameter curves made into tables, the OS's lookup on them in float and in its integers
// (0x400a9ca8), its Q31 arithmetic, the sine, the pitch chain (0x400a7e6c, and 0x400a7f66 at 96 kHz; its pitch word
// alone for CHORD) and, for the machines after KICK, the routines they add to it: the self-modulated operator in the
// OS's integers (0x400a8adc), the attack envelopes (0x400a9252, 0x400a9302) and the drive's gain (0x400a967a). The
// laws were read at the OS's oracle, outside the repository (elektron-work's cycles-kick/KICK.md,
// cycles-snare-metal/SNARE.md and METAL.md, cycles-perc-tone-chord/PERC.md, TONE.md and CHORD.md).
namespace cycles::fast
{
	using syntakt::fast::Block;
	using syntakt::fast::Controls;
	using syntakt::fast::Lanes;
	constexpr int BlockSize = syntakt::fast::BlockSize;

	// A curve over a word's high byte, 0-127: knots (linear between them) or segments, each a cubic in the index
	// from its start (highest power first) or p + q r^x. Where the curve is a coefficient a block, v = -exp(-1 / tau),
	// it is fitted on tau (tau) or on ln tau (logTau), its instant entries kept as they are (special)
	struct Knot
	{
		int x;
		double y;
	};
	struct Segment
	{
		int from, to;
		enum Kind { Poly, Exp } kind;
		double c[4];
	};
	struct Special
	{
		int x;
		double v;
	};
	struct Curve
	{
		const Knot* knots;
		size_t knotCount;
		const Segment* segments;
		size_t segmentCount;
		bool tau;
		bool logTau = false;
		const Special* special = nullptr;
		size_t specialCount = 0;
	};

	using Table = std::array<double, 128>;

	// The curve's entry _i (a curve may run past 127: PERC's exp2 curve has 1024 entries), and its 128 entries
	double entry(const Curve& _c, int _i);
	Table makeTable(const Curve& _c);
	std::array<float, 128> toFloat(const Table& _t);
	// As the OS holds the table: round(v 2^31), halves to even, saturated
	std::array<int32_t, 128> toQ31(const Table& _t);

	constexpr int64_t Max31 = 0x7fffffff, Min31 = -0x80000000LL;

	// The OS's truncating Q31 product, and its saturating one (the EMAC's)
	inline int64_t q31(const int64_t _a, const int64_t _b) { return (_a * _b) >> 31; }
	inline int64_t fm(const int64_t _a, const int64_t _b) { return std::clamp((_a * _b * 2) >> 32, Min31, Max31); }
	inline int64_t s32(const int64_t _v) { return static_cast<int32_t>(static_cast<uint32_t>(_v & 0xffffffff)); }

	// A Q31 word as a fraction
	constexpr double fraction(const uint32_t _word) { return static_cast<int32_t>(_word) / 2147483648.0; }

	inline int hb(const int32_t _word) { return std::clamp(_word >> 8, 0, 127); }

	// lk: a word on its curve, linear between the high byte's entry and the next (0x400a9ca8), in float (KICK's), in
	// double, and in the OS's integers (the words that reach its integer chains)
	inline float lookup(const std::array<float, 128>& _t, const int32_t _word)
	{
		const int i = hb(_word);
		const float f = static_cast<float>(_word - (i << 8)) / 256;
		const float a = _t[static_cast<size_t>(i)], b = _t[static_cast<size_t>(std::min(i + 1, 127))];
		return a + f * (b - a);
	}
	inline double lookup(const Table& _t, const int32_t _word)
	{
		const int i = hb(_word);
		const double f = (_word - (i << 8)) / 256.0;
		return _t[static_cast<size_t>(i)] * (1 - f) + _t[static_cast<size_t>(std::min(i + 1, 127))] * f;
	}
	inline int64_t lookup(const std::array<int32_t, 128>& _t, const int32_t _word)
	{
		const int i = hb(_word);
		const int64_t f = s32(static_cast<int64_t>(_word - (i << 8)) << 23);
		const int64_t a = _t[static_cast<size_t>(i)], b = _t[static_cast<size_t>(std::min(i + 1, 127))];
		const int64_t acc = (((Max31 - f) * a * 2) >> 24) + ((f * b * 2) >> 24);
		return std::clamp(acc >> 8, Min31, Max31);
	}

	// The sine of 256 entries and the first again, in float, and in the OS's integers (round(sin 2^31), saturated)
	const std::array<float, 257>& sine();
	const std::array<int32_t, 257>& sineQ31();
	// Builds the shared tables (the sines, the pitch's steps), which a machine's constructor calls: not on the audio
	// thread at a first block
	void warmUp();

	// A 256-entry wave at a phase in cycles, linear between entries
	inline float wave(const std::array<float, 257>& _w, const float _phase)
	{
		// The phase's entry by truncation, floored for a negative phase (a modulated one), then wrapped: no library
		// floor in the loop
		const float x = _phase * 256;
		const int whole = static_cast<int>(x) - (x < static_cast<float>(static_cast<int>(x)) ? 1 : 0);
		const float f = x - static_cast<float>(whole);
		const auto i = static_cast<size_t>(whole & 255);
		return _w[i] + f * (_w[i + 1] - _w[i]);
	}

	// The pitch word the chain multiplies (0x400a7e6c's d3, CHORD's P), in the OS's integers: the note's frequency,
	// 0x6E000000 x its step of 2048 an octave, shifted by its octave (as increments below takes its arguments). PTCH is
	// held to its record's range, 40 to 88
	int64_t pitchWord(int32_t _pitch, int _note, int64_t _offset);

	// The four operators' 24-bit increments a sample, in the OS's integers (0x400a7e6c): the PTCH word (its fine in
	// the low byte; the fine pitch word at its centre), the note, the machine's offset in semitones << 16 (KICK
	// -88, SNARE -64, METAL -40, PERC and TONE -64), the sweep's depth and envelope (Q31), per operator its multiplier
	// and factor (Q31), and the chain's last factor, the rate: 0x57619F10 for a 48 kHz sample, half of it for TONE's
	// 96 kHz one (0x400a7f66). The OS's semitones to octaves (x 0x0AAAAAAB / 2^31, floored: note 60 lands on step 511
	// of 2048, not 512) and its chain of truncating products, which the phases are sensitive to
	void increments(int32_t _pitch, int _note, int64_t _offset, int64_t _depth, int64_t _sweep,
		const int64_t* _multiplier, const int64_t* _factor, int64_t* _inc, int64_t _rate = 0x57619F10);

	// The self-modulated operator (0x400a8adc) in the OS's integers: s[n] = sine(phase + 4 F s[n-1]), its feedback
	// word F ramped over the block from the last block's. Chaotic at SNARE's indices (its noise) and at METAL's top
	// SHPE: in integers it is the OS's, sample for sample, given its integer increment and feedback word
	struct FeedbackOperator
	{
		int64_t phase = 0;		// 24 bits
		int64_t last = 0;		// the last sample, Q31
		int64_t feedback = 0;	// the last block's feedback word (0 after the voice's init)

		// One block of its samples, Q31
		void block(int64_t _inc, int64_t _feedback, int32_t* _out);
	};

	// The OS's attack envelopes (0x400a9252, the amp; 0x400a9302, SNARE's body), in magnitudes: at the trig v = step
	// / 2 (1 for a step of 1), then v += step a block until 1 (the block that reaches it stays in the attack), then a
	// block at 1 that decides hold (GATE, without a note-off) or decay, then v <- d v a block. 0x400a9252 sets the
	// trig's values before its state machine, 0x400a9302 after
	struct Attack
	{
		enum State { Rising, Top, Held, Decaying };

		double step = 0, v = 0;
		bool hold = false, trigFirst = true;
		State state = Decaying;
		bool consumed = false;		// a hold ended by the note-off this block

		void start(double _step, bool _hold, bool _trigFirst);
		// One block: _trig the voice's trig block, _cut a retrig's block (the level then 0)
		void advance(bool _trig, double _decay, bool _noteOff, bool _cut);
	};

	// The drive's gain (0x400a967a) a block: off, 1; on, a first-order filter on a step from 1 down to its floor
	// (fall), then back toward 1 (rise)
	struct PunchGain
	{
		bool on = false;
		int stage = 1;
		double x = 1, y = 1;

		// The gain at the block's end
		double advance(const double* _fall, const double* _rise, double _floor);
	};
}
