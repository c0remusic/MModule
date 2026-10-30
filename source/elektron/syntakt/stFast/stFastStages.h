#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <type_traits>

#include <emmintrin.h>

// What the Syntakt's machines rewritten for speed share: 32-bit float, the 16 Tracks side by side (each field an array
// of 16 lanes, a vector op on 4 of them), the machine's own blocks of 32 samples at its own 48 kHz (a 44.1 kHz host
// gets them through Host, stFastResample.h). The exact integer port (stEngine) is the reference.
namespace syntakt::fast
{
	constexpr int Lanes = 16;		// one per Track
	constexpr int BlockSize = 32;	// samples per block
	constexpr float MachineRate = 48000.0f;

	// The 16 Tracks' samples of a block, sample-major: s[n] holds sample n of every Track. A stage computes lanes 0 to
	// lanes - 1 of the block it writes (a multiple of 4: a vector's) and leaves the others as they are
	struct alignas(16) Block
	{
		float s[BlockSize][Lanes];
		int lanes = Lanes;
	};

	// A stage that reads a block computes the lanes of the block it writes: both must have the same, else a block its
	// machine's setLanes left out goes on computing all 16, the narrowing's saving silently lost (stFastLanesTest
	// narrows every machine). A programming error: it stops the program
	inline void requireLanes(const Block& _out, const Block& _in)
	{
		if(_in.lanes != _out.lanes)
			std::abort();
	}

	// A machine's setLanes: lanes 0 to _lanes - 1 of each of its blocks computed from now on, the others zeroed;
	// _lanes 4, 8, 12 or 16, else the program stops
	inline void setLanes(const int _lanes, const std::initializer_list<Block*> _blocks)
	{
		if(_lanes < 4 || _lanes > Lanes || _lanes % 4)
			std::abort();
		for(Block* b : _blocks)
		{
			b->lanes = _lanes;
			for(auto& s : b->s)
				std::fill(s + _lanes, s + Lanes, 0.0f);
		}
	}

	// The stages, each over the lanes of the block it writes. A ramp goes linearly over the block from _from to _to
	// (per lane), as the firmware's per-sample steps of (to - from) / 32 do

	// _out = gx x + gy y, both gains ramped
	void mix(const Block& _x, const Block& _y, Block& _out, const float* _gxFrom, const float* _gxTo,
		const float* _gyFrom, const float* _gyTo);

	// _buf *= g, g ramped (the modulator's and the amp's VCA)
	void rampedGain(Block& _buf, const float* _from, const float* _to);

	// _buf *= _g, sample by sample (an envelope read on the machine's block clock)
	void gain(Block& _buf, const Block& _g);

	// First-order IIR in place, y = b0 x + b1 x1 + a1 y1, the same coefficients on every lane; _x1, _y1 per lane
	struct OnePole
	{
		float b0, b1, a1;
	};
	void onePole(Block& _buf, const OnePole& _c, float* _x1, float* _y1);

	// A one-pole's pole by the bilinear transform without prewarping at _hz, 48 kHz: (1 - K) / (1 + K), K = pi f / fs
	inline double bilinearPole(const double _hz)
	{
		const double k = 3.14159265358979323846 * _hz / 48000;
		return (1 - k) / (1 + k);
	}

	// The low-pass and the high-pass on that pole: y = (1 - p) / 2 (x + x1) + p y1, y = (1 + p) / 2 (x - x1) + p y1
	inline OnePole bilinearLowPass(const double _hz)
	{
		const double p = bilinearPole(_hz);
		return {static_cast<float>((1 - p) / 2), static_cast<float>((1 - p) / 2), static_cast<float>(p)};
	}

	inline OnePole bilinearHighPass(const double _hz)
	{
		const double p = bilinearPole(_hz);
		return {static_cast<float>((1 + p) / 2), static_cast<float>(-(1 + p) / 2), static_cast<float>(p)};
	}

	// First-order IIR in place, y = b x + b x1 + a y1, its coefficients per lane
	void onePoleLanes(Block& _buf, const float* _b, const float* _a, float* _x1, float* _y1);

	// A biquad in place, y = b0 x + b1 x1 + b2 x2 - a1 y1 - a2 y2 (_c), the same coefficients on every lane, each
	// output clipped to [-1, 1] as the machine's store saturates; _s: x1, x2, y1, y2 per lane
	void biquad(Block& _buf, const float* _c, float (*_s)[Lanes]);

	// Four buffers mixed into _out, gain i ramped from _from[i] to _to[i] (0x40004cd6); _x[3] may be _out
	void mix4(const Block* const (&_x)[4], Block& _out, const float (*_from)[Lanes], const float (*_to)[Lanes]);

	// 0x400044e2: noise from a lagged additive generator per lane (_s: its four words, s[k][lane]), in the machine's
	// integers; a sample is the generator's first sum where its second is above _gate (signed), else 0
	void gatedNoise(Block& _out, uint32_t (*_s)[Lanes], int32_t _gate);

	// 0x40003eb4: white noise from a lagged additive generator per lane, two of its words (_s0, _s1), in the machine's
	// integers: a sample is s0 + s1, which becomes s0 and is added to s1
	void whiteNoise(Block& _out, uint32_t* _s0, uint32_t* _s1);

	// 0x4000569a: two sample-and-holds per lane, at periods p + 1 and p + 2 samples, crossfaded by p's fraction: _rate
	// p in Q16 per lane; _held the two held samples, _count the samples since each took one
	void reduceRate(Block& _buf, const int32_t* _rate, float (*_held)[Lanes], int32_t (*_count)[Lanes]);

	// The gated noise of the 16 Tracks (gatedNoise), each from its seed at the Track's first trig. The seeds are a
	// law: word w of Track t is splitmix32's output 4 t + w + 1 from 0. The firmware draws them from its boot generator
	// instead, so the noise is statistically the firmware's but not its sample sequence; a test gives the exact port's
	// (seed, before the Track's first trig)
	struct NoiseGenerator
	{
		alignas(16) uint32_t words[4][Lanes]{}, seeds[4][Lanes]{};

		NoiseGenerator()
		{
			for(uint32_t l = 0; l < Lanes; ++l)
				for(uint32_t w = 0; w < 4; ++w)
				{
					uint32_t z = (4 * l + w) * 0x9e3779b9u + 0x9e3779b9u;
					z = (z ^ (z >> 16)) * 0x85ebca6bu;
					z = (z ^ (z >> 13)) * 0xc2b2ae35u;
					seeds[w][l] = z ^ (z >> 16);
				}
		}

		// Track _l's four words from its next start (start)
		void seed(const int _l, const int32_t* _words)
		{
			for(int w = 0; w < 4; ++w)
				seeds[w][_l] = static_cast<uint32_t>(_words[w]);
		}

		// Track _l's first trig: its generator from its seed
		void start(const int _l)
		{
			for(int w = 0; w < 4; ++w)
				words[w][_l] = seeds[w][_l];
		}

		void render(Block& _out, const int32_t _gate) { gatedNoise(_out, words, _gate); }
	};

	// The oscillators. Phases are 32-bit fractions of a cycle (an integer accumulator, so that a 2 s Hit does not drift
	// from float rounding), increments the same, one per sample. Phase-modulation depths are in cycles per unit of
	// input
	struct alignas(16) Increments
	{
		uint32_t s[BlockSize][Lanes];
	};

	// ---- the machine's integers, where a stage computes as its accumulator does ----

	// A product of two words as the accumulator holds it, Q39, and a sum of such stored to a word, saturated
	inline int64_t product(const int32_t _y, const int32_t _x) { return (static_cast<int64_t>(_y) * _x) >> 23; }

	inline int32_t store(const int64_t _acc)
	{
		return static_cast<int32_t>(std::clamp<int64_t>(_acc >> 8, INT32_MIN, INT32_MAX));
	}

	inline int32_t wrap(const int64_t _v) { return static_cast<int32_t>(static_cast<uint32_t>(_v)); }

	// The machine's fractional product of two words, truncated as its accumulator stores it
	inline int32_t frac(const int32_t _y, const int32_t _x) { return store(product(_y, _x)); }

	// 0x40003c0e's decay step of a unit's envelope in its words (negative, INT32_MIN full): toward _target, the
	// coefficient _c negative, the two products truncated alone and added wrapping, as the machine's
	inline int32_t envelopeDecay(const int32_t _level, const int32_t _c, const int32_t _target)
	{
		return wrap(static_cast<int64_t>(store(-product(_level, _c))) +
			store(-product(_target, wrap(static_cast<int64_t>(_c) + INT32_MAX))));
	}

	// The RAM sine the machine's oscillators read: sin(2 pi i / 256) in Q31, 257 words
	inline const std::array<int64_t, 257> QSine = []
	{
		std::array<int64_t, 257> t{};
		for(int i = 0; i < 257; ++i)
			t[i] = std::clamp<int64_t>(std::llround(std::sin(2 * 3.14159265358979323846 * i / 256) * 2147483648.0),
				INT32_MIN, INT32_MAX);
		return t;
	}();

	// The sample the feedback oscillators read (0x400035de and 0x40003958),
	// the RAM sine at the 24-bit _phase moved by _fb (2^-24 cycle per unit) times their feedback _x, in the machine's
	// integers: the phase sum (phase << 8) + (fb x >> 23), then >> 8; the lookup (T[i] << 8) + (f T[i + 1] >> 23) -
	// (f T[i] >> 23), then >> 8. Neither saturates nor wraps where the machine's accumulator would: the phase sum stays
	// within 2^25 and the lookup between two words of the table (Q3 of their machines, sample for sample)
	inline int64_t feedbackSample(const int64_t _phase, const int64_t _fb, const int64_t _x)
	{
		const int64_t p = (_phase * 256 + ((_fb * _x) >> 23)) >> 8;
		const int64_t i = (p & 0xffffff) >> 16, f = (p & 0xffff) << 15;
		return (QSine[i] * 256 + ((f * QSine[i + 1]) >> 23) - ((f * QSine[i]) >> 23)) >> 8;
	}

	// A sine phase-modulated by its previous sample, the amount ramped from _fb to frac(0x04000000, _fbWord) (left
	// there), in the machine's own integers (0x400035de): its 256-entry sine as Q31 words, the phase sum and the lookup
	// truncated as its accumulator truncates them. A float one drifts from it where the feedback is strong (SD BASIC's
	// feedback 1: 1.6 dB of third octaves). _fbWord: the machine's feedback words (its 0x2c) as its update computes
	// them; _fb: the amount ramped, 2^-24 cycle per unit of input (its 0x30); _last: the previous sample, a Q31 word
	void feedbackSine(Block& _out, uint32_t* _phase, const Increments& _inc, int32_t* _fb, const int32_t* _fbWord,
		int32_t* _last);

	// 0x40003958 with the RAM sine, as feedbackSine runs 0x400035de: the sine phase-modulated by its own previous
	// output under its envelope, in the machine's own integers per lane (a float one drifts from it where the feedback
	// is strong: CP VINTAGE's unit 1, a cycle per unit, 2 dB at 16 kHz on its chaotic Hits). _scaled the output under
	// the envelope, _last its previous sample (Q31); the feedback ramped from _fb to the words' _fbWord >> 5 (left
	// there); the envelope ramped from _amountFrom to _amountTo (left there), Q31 words in the machine's negative
	// representation
	void scaledFeedbackSine(Block& _scaled, uint32_t* _phase, const Increments& _inc, int32_t* _fb,
		const int32_t* _fbWord, int32_t* _last, int32_t* _amountFrom, const int32_t* _amountTo);

	// A sine phase-modulated by _src (nullptr: none)
	void pmSine(Block& _out, const Block* _src, uint32_t* _phase, const Increments& _inc, float _depth);

	// A wave of 256 entries (258 values) phase-modulated by _src (nullptr: none) at a depth ramped from _from to _to,
	// read as the machine reads its waves
	void pmWave(Block& _out, const Block* _src, const float* _from, const float* _to, uint32_t* _phase,
		const Increments& _inc, const float* _wave);

	// A wave as pmWave reads it, phase-modulated by its own previous sample under its envelope _g (0x40003958): _out
	// the wave, _scaled it times _g, _last the previous scaled sample; the feedback ramped from _fb to _fbTo (left
	// there), in cycles per unit of input
	void feedbackWave(Block& _out, Block& _scaled, const Block& _g, float* _last, float* _fb, const float* _fbTo,
		uint32_t* _phase, const Increments& _inc, const float* _wave);

	// Drive into a hard clip under a ramped gain: _buf = g post clip(drive x)
	void driveClip(Block& _buf, const float* _from, const float* _to, const float* _drive, const float* _post);

	// _buf = clip(_k x), as the machine's saturated stores (x 2: a doubling, x 8: three, each saturated)
	void saturate(Block& _buf, float _k);

	// ---- the control path ----

	inline float q31(const int32_t _v) { return static_cast<float>(static_cast<double>(_v) / 2147483648.0); }

	// A table's Q31 words (stFastTables) into _dst as floats
	template<typename Words>
	void readQ31(float* _dst, const Words& _words)
	{
		for(size_t i = 0; i < _words.size(); ++i)
			_dst[i] = q31(_words[i]);
	}

	// A table's words into _dst as they are, for what the machine computes in integers
	template<typename Words>
	void copyWords(int32_t* _dst, const Words& _words)
	{
		static_assert(std::is_same<typename Words::value_type, int32_t>::value, "a table of Q31 words");
		std::copy(_words.begin(), _words.end(), _dst);
	}

	// The 256-entry sine a wave oscillator reads (pmWave): sin(2 pi i / 256), 258 values
	inline void sineTable(float* _dst)
	{
		for(int i = 0; i < 258; ++i)
			_dst[i] = static_cast<float>(std::sin(2 * 3.14159265358979323846 * i / 256));
	}

	// 0x40004dc6: a 128-entry table read at a parameter word, linearly interpolated
	inline float interpolate(const float* _table, const int32_t _word)
	{
		const int32_t i = std::max(_word >> 8, 0);
		const float f = static_cast<float>(_word - (i << 8)) / 256.0f;
		return (1 - f) * _table[i] + f * _table[std::min(i + 1, 127)];
	}

	// 0x40004dc6 as the machine computes it, for the words of the pitch: both products truncated to Q39, their sum to
	// Q31
	inline int32_t interpolateWord(const int32_t* _table, const int32_t _word)
	{
		const int32_t i = std::max(_word >> 8, 0);
		const int64_t f = static_cast<int64_t>(static_cast<uint32_t>(_word - (i << 8)) << 23);
		const int64_t sum = ((INT32_MAX - f) * _table[i] >> 23) + (f * _table[std::min(i + 1, 127)] >> 23);
		return static_cast<int32_t>(std::clamp<int64_t>(sum >> 8, INT32_MIN, INT32_MAX));
	}

	// ---- the pitch, as the machine computes it (0x40002596): in integers, so that a long Hit keeps its phase ----

	// The pitch for the note, the TUNE word (value << 8 | fine) and the machine's offset to the note in semitones (its
	// 0x40): note - offset + TUNE / 256 semitones, Q16, clamped to notes 0-127
	inline int32_t pitchQ16(const int _note, const int32_t _tune, const int _offset)
	{
		return static_cast<int32_t>(std::clamp<int64_t>((_note - _offset) * int64_t(65536) + 256 * _tune, 0,
			127 * 65536));
	}

	// The pitch map's word at pitchQ16: octaves from A4 by the machine's 1/12 (0x0aaaaaab), their fraction's top 11
	// bits indexing its exp2 table (_exp2: 2048 words)
	inline int32_t pitchWord(const int32_t* _exp2, const int _note, const int32_t _tune, const int _offset)
	{
		const int64_t p = pitchQ16(_note, _tune, _offset);
		const int64_t q = ((p - 69 * 65536) * 0x0aaaaaab) >> 31;
		const int octave = static_cast<int16_t>(q >> 16);
		const int64_t f = (int64_t(0x6e000000) * _exp2[(q & 0xffff) >> 5]) >> 31;
		const int shift = 5 - octave;
		return shift >= 32 ? 0 : static_cast<int32_t>(f >> shift);
	}

	// The sweep's factor word: 0x04000000 plus the depth word's excess under the sweep's level _sweep (a fraction, from
	// INT32_MAX / 2^31 at the trig), rounded down
	inline int64_t sweepWord(const double _depth, const double _sweep)
	{
		return static_cast<int64_t>(std::floor((_depth - 67108864.0) * _sweep)) + 67108864;
	}

	// A unit's increment in 2^-25 cycles per 48 kHz sample, for a 25-bit phase (24: the 24 kHz oscillator's,
	// phaseIncr24k at 0x400060b2): its factor (0x50) under
	// the sweep's word _m, its ratio (0x64, 0x40000000 the note's), at the pitch word, each product truncated as the
	// machine's, at most 32 times the note's
	inline int64_t increment24(const int64_t _m, const int64_t _factor, const int64_t _scale, const int64_t _pitch)
	{
		const int64_t t2 = (((_factor >> 6) * _m >> 31) * _scale) >> 31;
		const auto t3 = static_cast<int32_t>(static_cast<uint32_t>(t2) << 11);
		const int64_t t4 = std::clamp<int64_t>(2 * int64_t(t3), INT32_MIN, INT32_MAX);
		return ((t4 * _pitch >> 31) * 0x57619f10 >> 31) >> 1;
	}

	// The same in 2^-24 cycles, for a 24-bit phase (0x40002596): one more shift, as the machine's
	inline double increment48(const int64_t _m, const int64_t _factor, const int64_t _scale, const int64_t _pitch)
	{
		return static_cast<double>(increment24(_m, _factor, _scale, _pitch) >> 1);
	}

	// That increment as a 32-bit phase step (Increments')
	inline uint32_t unitIncrement(const int64_t _m, const int64_t _factor, const int64_t _scale, const int64_t _pitch)
	{
		return static_cast<uint32_t>(std::llround(increment48(_m, _factor, _scale, _pitch) * 256));
	}

	// The same on lane _l of every sample of _inc
	inline void setIncrement(Increments& _inc, const int _l, const int64_t _m, const int64_t _factor,
		const int64_t _scale, const int64_t _pitch)
	{
		const uint32_t inc = unitIncrement(_m, _factor, _scale, _pitch);
		for(auto& s : _inc.s)
			s[_l] = inc;
	}

	// A block-rate envelope as the machine steps it, once per 48 kHz block: its levels at the ends of 48 kHz blocks
	// k - 1 and k, between which its ramp runs over block k
	struct Ramp48
	{
		float prev[Lanes]{}, level[Lanes]{};

		void step(const int _l, const float _v)
		{
			prev[_l] = level[_l];
			level[_l] = _v;
		}

		// A first-order step toward _target, _a the pole: a unit's envelope
		void stepToward(const int _l, const float _a, const float _target)
		{
			step(_l, _a * level[_l] + (1 - _a) * _target);
		}

		// The ramp _t of the way through block k
		float at(const int _l, const double _t) const
		{
			return prev[_l] + static_cast<float>(_t) * (level[_l] - prev[_l]);
		}
	};

	// The envelope of a unit with an attack (0x40003c0e) and of a modulator (0x40003df2): from a trig, up by its step a
	// block to full, saturated, held full a block, then a first-order step toward a target. A step of 0 holds it where
	// it starts
	struct RiseEnvelope : Ramp48
	{
		bool rising[Lanes]{};

		// A trig: the ramp from _from to _start over the trig's block, rising from there
		void trig(const int _l, const float _from, const float _start)
		{
			rising[_l] = true;
			prev[_l] = _from;
			level[_l] = _start;
		}

		// One 48 kHz block
		void stepRise(const int _l, const float _attack, const float _a, const float _target)
		{
			const float v = level[_l];
			if(!rising[_l])
			{
				stepToward(_l, _a, _target);
			}
			else if(v == 1)
			{
				rising[_l] = false;
				step(_l, v);
			}
			else
			{
				step(_l, std::min(v + _attack, 1.0f));
			}
		}
	};

	// The amp's envelope as the machines step it: rising from the trig (stage 0) where it has an attack, held while
	// HOLD is on (2) until a gate-off, decaying (1, the state before the first trig)
	struct AmpEnvelope : Ramp48
	{
		int stage[Lanes];
		bool released[Lanes]{};	// a gate-off seen: the hold ends at the next step

		AmpEnvelope() { std::fill_n(stage, Lanes, 1); }

		// A trig that starts the amp full without a ramp, held with _hold, else decaying from the next block
		void trigFull(const int _l, const bool _hold)
		{
			stage[_l] = _hold ? 2 : 1;
			prev[_l] = level[_l] = 1;
			released[_l] = false;
		}

		// A trig that ramps the amp from the trig's block's silence to one and a half of its step _attack, saturated
		void trigRise(const int _l, const float _attack)
		{
			stage[_l] = 0;
			prev[_l] = 0;
			level[_l] = std::min(_attack / 2 + _attack, 1.0f);
			released[_l] = false;
		}

		void release(const int _l) { released[_l] = true; }

		// One 48 kHz block: rising by _attack to full, then held with _hold until a release, or decaying by _decay
		void stepAmp(const int _l, const float _decay, const float _attack, const bool _hold)
		{
			const float amp = level[_l];
			if(stage[_l] == 0)
			{
				if(amp == 1)
					stage[_l] = !released[_l] && _hold ? 2 : 1;
				step(_l, amp == 1 ? amp : std::min(amp + _attack, 1.0f));
			}
			else if(stage[_l] == 2)
			{
				if(released[_l])
				{
					stage[_l] = 1;
					released[_l] = false;
				}
				step(_l, amp);
			}
			else
			{
				step(_l, amp * _decay);
			}
		}
	};

	// A trig's pending block fades lane _l of _g (an envelope read sample by sample) out from where the last block
	// left it (_end), and leaves _env silent until the trig; _end then takes _env's level at this block's end
	inline void fadePending(Block& _g, Ramp48& _env, float* _end, const int _l, const bool _pending)
	{
		if(_pending)
		{
			for(int n = 0; n < BlockSize; ++n)
				_g.s[n][_l] = _end[_l] * (1 - static_cast<float>(n) / BlockSize);
			_env.prev[_l] = _env.level[_l] = 0;
		}
		_end[_l] = _env.at(_l, 1);
	}

	// The 16 Tracks' trigs and gate-offs: queued for the next block, which sees them (pending: the Track fades out),
	// a trig sounding from the block after (now)
	struct Trigs
	{
		bool queued[Lanes]{}, gateQueued[Lanes]{};
		bool pending[Lanes]{}, now[Lanes]{}, gateOff[Lanes]{};

		// A block's start: what was queued is the block's
		void start()
		{
			for(int l = 0; l < Lanes; ++l)
			{
				pending[l] = queued[l];
				gateOff[l] = gateQueued[l];
				queued[l] = gateQueued[l] = false;
			}
		}

		// A block's end: its trigs sound from the next
		void end() { std::copy_n(pending, Lanes, now); }

		// Whether the block's gate-off releases the Track: not in a trig's block
		bool releases(const int _l) const { return gateOff[_l] && !pending[_l]; }

		// The machine change at a Track's first trig (0x40003ee0) clears its voice's words 0x00-0x3c, its trig flags
		// and gate-off among them: a trig or a gate-off in that block is lost
		void machineChange(const int _l) { pending[_l] = gateOff[_l] = false; }
	};

	// The 48 kHz block clock of the block-rate control: the blocks since the trig and the envelopes' block. The
	// envelopes step once per 48 kHz block, as on the machine, but in the trig's block
	struct Clock48
	{
		int64_t at[Lanes]{}, envBlock[Lanes]{};

		void restart(const int _l) { at[_l] = envBlock[_l] = 0; }

		// One block of lane _l: the 48 kHz blocks its envelopes step, one for each block since the trig, to the
		// block's start, they have not stepped
		int advance(const int _l)
		{
			const int64_t start = at[_l]++;
			const int64_t steps = std::max<int64_t>(start - envBlock[_l], 0);
			envBlock[_l] += steps;
			return static_cast<int>(steps);
		}
	};

	// What a machine of syntakt::fast is to the code that plays it (Host, bench::fastHits, bench::costPerBlock,
	// bench::printCost, stFastSweep):
	//	- a default constructor, its tables made by formula; static constexpr int Parameters, 8, and Units
	//	- setNote(track, note) and setParameter(track, index 0-7, value 0-127, fine): from the next block
	//	- trig(track), gateOff(track): for the next block rendered only, which sees the trig (the Track fades out)
	//	  and sounds it from the block after
	//	- testWithoutChaos(): for tests, the voice with its feedback (or depth) table at 0, nothing chaotic left
	//	- const Block& renderBlock(): one 48 kHz block of the 16 Tracks, in this order: FlushDenormals,
	//	  controls.trigs.start(), the block-rate control, the stages, controls.trigs.end()
	//	- setLanes(n), n 4, 8, 12 or 16: from the next block it renders lanes 0 to n - 1 only, the others zero, their
	//	  voices frozen (a trig or an edit there is lost) until a wider setLanes resumes them where they stopped.
	//	  Its control loops over m_lanes, its stages over their blocks' lanes, which its setLanes sets on each block.
	//	  A track here is a lane: the code that plays the machine puts its Tracks on its first lanes (stFastMixCost)
	//	- at 48 kHz only, a machine may step its envelopes once per block but in the trig's block rather than on
	//	  Clock48, whose steps that is (CP VINTAGE, SY TOY, SY BITS and SP TWINSHOT do)
	// Controls holds what the front sets, which the machine converts (its convert(track)) and its control reads
	struct Controls
	{
		static constexpr int Parameters = 8;

		int32_t words[Parameters][Lanes]{};	// value << 8 | fine
		int notes[Lanes]{};
		Trigs trigs;

		// Whether _index is a parameter, its word then set
		bool setParameter(const int _track, const int _index, const uint8_t _value, const uint8_t _fine)
		{
			if(_index < 0 || _index >= Parameters)
				return false;
			words[_index][_track] = std::min<int32_t>(_value, 127) << 8 | _fine;
			return true;
		}

		void trig(const int _track) { trigs.queued[_track] = true; }
		void gateOff(const int _track) { trigs.gateQueued[_track] = true; }
	};

	// 0x400047ea's gain envelope, one 48 kHz block: a first-order filter on a step, falling from 1 toward 0 until
	// _floor (_stage 1, pole _fall), then recovering toward 1 (_stage 5, pole _rise); 1 where the drive is off. _x: its
	// input at the previous block, _level its output
	inline float driveGain(int& _stage, float& _x, const float _level, const bool _on, const float _fall,
		const float _rise, const float _floor = 0.5f)
	{
		const bool falling = _stage == 1;
		const float pole = falling ? _fall : _rise;
		const float x = falling ? 0.0f : 1.0f;
		float y = (1 - pole) / 2 * (x + _x) + pole * _level;
		if(y <= _floor)
		{
			_stage = 5;
			y = _floor;
		}
		if(!_on)
			y = 1;
		_x = x;
		return y;
	}

	// The drive stage of a machine: its gain envelope (driveGain at the machine's poles and floor) stepped on the
	// block clock and ramped over the block (driveClip's _from, _to), its pre-gain and its gain after the clip, the
	// machine's with the drive on or off
	struct Drive
	{
		struct Gains
		{
			float amount, post;	// driveClip's _drive and _post
		};

		Ramp48 gain;
		int stage[Lanes];
		float x[Lanes];
		bool on[Lanes]{};
		alignas(16) float from[Lanes], to[Lanes], amount[Lanes], post[Lanes];
		float fall, rise, bottom;
		Gains gainsOn, gainsOff;

		// Before the first trig: off, the gain at 1, recovering
		Drive(const float _fall, const float _rise, const float _floor, const Gains _on, const Gains _off)
			: fall(_fall), rise(_rise), bottom(_floor), gainsOn(_on), gainsOff(_off)
		{
			for(int l = 0; l < Lanes; ++l)
			{
				stage[l] = 5;
				x[l] = 1;
				gain.prev[l] = gain.level[l] = 1;
				from[l] = to[l] = 1;
				amount[l] = _off.amount;
				post[l] = _off.post;
			}
		}

		// A trig: _on latched with its gains, the gain falling from 1, its first 48 kHz block stepped
		void trig(const int _l, const bool _on)
		{
			on[_l] = _on;
			amount[_l] = _on ? gainsOn.amount : gainsOff.amount;
			post[_l] = _on ? gainsOn.post : gainsOff.post;
			stage[_l] = 1;
			x[_l] = 1;
			gain.prev[_l] = gain.level[_l] = 1;
			gain.level[_l] = next(_l);
			from[_l] = 1;
		}

		// One 48 kHz block of the envelope
		void step(const int _l) { gain.step(_l, next(_l)); }

		// The ramp's end at the block's end; at the next block's start, its start (advance)
		void end(const int _l) { to[_l] = gain.at(_l, 1); }
		void advance() { std::copy_n(to, Lanes, from); }

	private:
		float next(const int _l) { return driveGain(stage[_l], x[_l], gain.level[_l], on[_l], fall, rise, bottom); }
	};

	// RAII: denormals flushed to zero, in and out, while a block renders
	struct FlushDenormals
	{
		unsigned int csr = _mm_getcsr();
		FlushDenormals() { _mm_setcsr(csr | 0x8040); }
		~FlushDenormals() { _mm_setcsr(csr); }
	};

	// ---- the vector helpers the stages are made of ----
	namespace simd
	{
		using V = __m128;

		constexpr float Pi = 3.14159265358979f;

		inline V ramp(const float* _v, const int _g) { return _mm_load_ps(_v + _g); }

		inline V stepOf(const float* _from, const float* _to, const int _g)
		{
			const V d = _mm_sub_ps(_mm_load_ps(_to + _g), _mm_load_ps(_from + _g));
			return _mm_mul_ps(d, _mm_set1_ps(1.0f / BlockSize));
		}

		// A 32-bit phase as a fraction of a cycle in [0, 1), to 24 bits
		inline V phaseOf(const __m128i _p)
		{
			return _mm_mul_ps(_mm_cvtepi32_ps(_mm_srli_epi32(_p, 8)), _mm_set1_ps(1.0f / 16777216.0f));
		}

		inline V floorOf(const V _x)
		{
			const V t = _mm_cvtepi32_ps(_mm_cvttps_epi32(_x));
			return _mm_sub_ps(t, _mm_and_ps(_mm_cmplt_ps(_x, t), _mm_set1_ps(1.0f)));
		}

		// sin(2 pi _u): _u reduced to [-1/2, 1/2], folded to [-1/4, 1/4], then the odd Taylor series to x^11 (error
		// below 1e-7 on [-pi/2, pi/2]). The firmware reads a 256-entry sine linearly interpolated (error ~8e-5)
		inline V sin2pi(const V _u)
		{
			const V sign = _mm_set1_ps(-0.0f);
			V r = _mm_sub_ps(_u, _mm_cvtepi32_ps(_mm_cvtps_epi32(_u)));
			const V folded = _mm_sub_ps(_mm_or_ps(_mm_set1_ps(0.5f), _mm_and_ps(r, sign)), r);
			const V big = _mm_cmpgt_ps(_mm_andnot_ps(sign, r), _mm_set1_ps(0.25f));
			r = _mm_or_ps(_mm_and_ps(big, folded), _mm_andnot_ps(big, r));
			const V x = _mm_mul_ps(r, _mm_set1_ps(2 * Pi));
			const V x2 = _mm_mul_ps(x, x);
			V p = _mm_set1_ps(-1.0f / 39916800.0f);
			p = _mm_add_ps(_mm_mul_ps(p, x2), _mm_set1_ps(1.0f / 362880.0f));
			p = _mm_add_ps(_mm_mul_ps(p, x2), _mm_set1_ps(-1.0f / 5040.0f));
			p = _mm_add_ps(_mm_mul_ps(p, x2), _mm_set1_ps(1.0f / 120.0f));
			p = _mm_add_ps(_mm_mul_ps(p, x2), _mm_set1_ps(-1.0f / 6.0f));
			p = _mm_add_ps(_mm_mul_ps(p, x2), _mm_set1_ps(1.0f));
			return _mm_mul_ps(p, x);
		}

		// A 256-entry wave at _u cycles (258 values: 257 and a copy of the last), linearly interpolated as the machine
		// reads its waves
		inline V lookup(const V _u, const float* _wave)
		{
			const V u = _mm_mul_ps(_mm_sub_ps(_u, floorOf(_u)), _mm_set1_ps(256.0f));
			const __m128i k = _mm_cvttps_epi32(u);
			const V f = _mm_sub_ps(u, _mm_cvtepi32_ps(k));
			alignas(16) int32_t i[4];
			alignas(16) float a[4], b[4];
			_mm_store_si128(reinterpret_cast<__m128i*>(i), k);
			for(int l = 0; l < 4; ++l)	// a gather: SSE2 has none
			{
				a[l] = _wave[i[l]];
				b[l] = _wave[i[l] + 1];
			}
			const V va = _mm_load_ps(a);
			return _mm_add_ps(va, _mm_mul_ps(f, _mm_sub_ps(_mm_load_ps(b), va)));
		}

		inline V clip(const V _x)
		{
			return _mm_max_ps(_mm_min_ps(_x, _mm_set1_ps(1.0f)), _mm_set1_ps(-1.0f));
		}
	}
}
