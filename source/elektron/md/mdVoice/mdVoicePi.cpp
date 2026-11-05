// P-I-BD, -SD, -MT, -ML, -RS and -MA's voice code and the dispatch by type, as mdVoicePi.h says.
//
// The five drums strike with an excitation of the owner's (PiBank), which a pitch that a fast envelope bends plays at a
// step of under 4 words, linearly interpolated. BD, SD, MT and RS then run it through three delay lines that feed one
// another, each a one-pole in its loop, and all through eight resonators tuned by the machine's own words. SD and RS add
// a noise in the third line and at the input, which their excitation's own level opens. ML has resonators and no lines.
// MA is a shaken noise through a two-pole. A voice's three delay lines are $200 words each, with a modulus of $200.
#include "mdVoicePi.h"

#include <algorithm>
#include <cstdlib>
#include <iterator>

namespace md::voice::pi
{
	namespace
	{
		using Block = Voice::Block;

		// One machine's words of PiBank; past them, what lies there, the zeros that follow on the DSP
		struct Data
		{
			const uint32_t* words;
			uint32_t addr, size;
		};

		Data data(const Context& _c, const uint32_t _range)
		{
			uint32_t at = 0;
			for(uint32_t i = 0; i < _range; ++i)
				at += PiBank::Ranges[i].words;
			const auto& range = PiBank::Ranges[_range];
			return {_c.shared.pi + at, range.addr, range.words};
		}

		uint32_t read(const Context& _c, const Data& _d, const uint32_t _addr)
		{
			const uint32_t i = _addr - _d.addr;
			return i < _d.size ? _d.words[i] & 0xffffff : external(_c, _addr, testHooks::AreaX);
		}

		// Y:$FF, where the DSP saves R7 while a delay line takes it. An artefact of the port's scratch, not a fact of the
		// machine: the word is the output buffer of mdEngine's harness, $1000 + 32 t, where OS 1.63 flips two others.
		// Nothing reads it; it is written so that the scratch compares whole
		uint32_t outputBuffer(const Context& _c)
		{
			return 0x1000 + 0x20 * _c.index;
		}

		// The resonators, a record of three words each, every four words from Y:$64
		constexpr uint32_t kResonatorRecords = 0x64, kModes = 8;

		// $142B49 by _scale of Y:$02, a line's damping, and that times 1/sqrt 2, at X:$FB and X:$FC
		void lineDamping(const Context& _c, const uint32_t _word, const uint32_t _scale)
		{
			auto& ix = _c.shared.scratch.x;
			const uint32_t x1 = tableWord(_c, tables(_c).piRing, TrxTables::PiRingAddr, lim(mul(_scale, _word)));
			ix[0xfb] = x1;
			ix[0xfc] = lim(mul(x1, 0x5a827a));
		}

		// Y:$B4-$BB into Y:$A0-$A7: 1 - w^2 + w^4 / 6, each resonator's cos 2w from its pitch
		void cosines(std::array<uint32_t, 0x100>& _iy)
		{
			uint32_t y0 = _iy[0xb4];
			int64_t b = -mul(y0, y0);
			for(uint32_t m = 0; m < kModes; ++m)
			{
				const uint32_t square = lim(b);
				int64_t a = acc(0x7fffff) + b;
				a += mul(lim(mul(square, 0x155555)), square);
				y0 = _iy[0xb5 + m];
				_iy[0xa0 + m] = lim(a);
				b = -mul(y0, y0);
			}
		}

		// The records Y:$64 + 4 m from the cosines: -r^2, 2 r cos - 1 and the resonator's gain, r from $142949 at _factors'
		// word by _time, the gain the word of _gains by the distance to the unit circle's
		template<typename Factors, typename Gains>
		void tuneModes(const Context& _c, const uint32_t _time, Factors&& _factors, Gains&& _gains)
		{
			auto& iy = _c.shared.scratch.y;
			const auto& damp = tables(_c).piDamp;
			uint32_t n = lim(mul(_factors(0), _time));
			for(uint32_t m = 0; m < kModes; ++m)
			{
				const uint32_t r = tableWord(_c, damp, TrxTables::PiDampAddr, n);
				int64_t a = shl(mul(r, iy[0xa0 + m]), 1) + acc(0x800000);
				const uint32_t c1 = lim(a);
				const int64_t square = -mul(r, r);
				a += square;
				auto* const record = &iy[kResonatorRecords + 4 * m];
				record[0] = lim(square);
				record[1] = c1;
				record[2] = lim(shl(mul(_gains(m), lim(a)), 1));
				n = lim(mul(_factors(m + 1), _time));
			}
		}

		// One resonator's accumulator and last output
		struct Mode
		{
			int64_t a;
			uint32_t x1;
		};

		// Four resonators over X:$60-$7F, each added to X:$40-$5F and limited there in turn. A sample at a time rather than
		// a resonator at a time, as the DSP has it: each sample's words go through the same limits in the same order. The
		// upper word of an accumulator is both what X:$40 adds and the next sample's limited word, so that one shift serves
		// the two, and one test on all of them finds the rare sample that limits. Returns the last word after the third
		uint32_t fourModes(Context& _c, Mode* const _m, const uint32_t* const _r)
		{
			auto& ix = _c.shared.scratch.x;
			const int64_t c00 = 2 * sx64(_r[0]), c01 = 2 * sx64(_r[1]), c02 = 2 * sx64(_r[2]);
			const int64_t c10 = 2 * sx64(_r[4]), c11 = 2 * sx64(_r[5]), c12 = 2 * sx64(_r[6]);
			const int64_t c20 = 2 * sx64(_r[8]), c21 = 2 * sx64(_r[9]), c22 = 2 * sx64(_r[10]);
			const int64_t c30 = 2 * sx64(_r[12]), c31 = 2 * sx64(_r[13]), c32 = 2 * sx64(_r[14]);
			int64_t a0 = _m[0].a, a1 = _m[1].a, a2 = _m[2].a, a3 = _m[3].a;
			int64_t x0 = sx64(_m[0].x1), x1 = sx64(_m[1].x1), x2 = sx64(_m[2].x1), x3 = sx64(_m[3].x1);
			int64_t l0 = sx64(lim(a0)), l1 = sx64(lim(a1)), l2 = sx64(lim(a2)), l3 = sx64(lim(a3));
			int64_t third = 0;
			constexpr int64_t half = 0x800000;
			for(uint32_t k = 0; k < 32; ++k)
			{
				const int64_t in = sx64(ix[0x60 + k]);
				a0 += x0 * c00 + l0 * c01 + in * c02;
				a1 += x1 * c10 + l1 * c11 + in * c12;
				a2 += x2 * c20 + l2 * c21 + in * c22;
				a3 += x3 * c30 + l3 * c31 + in * c32;
				x0 = l0;
				x1 = l1;
				x2 = l2;
				x3 = l3;
				l0 = a0 >> 24;
				l1 = a1 >> 24;
				l2 = a2 >> 24;
				l3 = a3 >> 24;
				const int64_t w0 = sx64(ix[0x40 + k]) + l0, w1 = w0 + l1;
				third = w1 + l2;
				int64_t w3 = third + l3;
				if(((l0 + half) | (l1 + half) | (l2 + half) | (l3 + half) | (w0 + half) | (w1 + half) | (third + half)
					| (w3 + half)) >> 24)
				{
					const uint32_t limited = lim(acc(lim(acc(ix[0x40 + k]) + a0)) + a1);
					third = sx64(lim(acc(limited) + a2));
					w3 = sx64(lim(acc(static_cast<uint32_t>(third)) + a3));
					l0 = sx64(lim(a0));
					l1 = sx64(lim(a1));
					l2 = sx64(lim(a2));
					l3 = sx64(lim(a3));
				}
				ix[0x40 + k] = static_cast<uint32_t>(w3) & 0xffffff;
			}
			_m[0] = {a0, static_cast<uint32_t>(x0) & 0xffffff};
			_m[1] = {a1, static_cast<uint32_t>(x1) & 0xffffff};
			_m[2] = {a2, static_cast<uint32_t>(x2) & 0xffffff};
			_m[3] = {a3, static_cast<uint32_t>(x3) & 0xffffff};
			return static_cast<uint32_t>(third) & 0xffffff;
		}

		// The eight resonators, then the block, X:$40-$5F. Returns the last word before the eighth
		uint32_t resonators(Context& _c, Mode* const _m, Block& _out)
		{
			auto& ix = _c.shared.scratch.x;
			const auto* const records = &_c.shared.scratch.y[kResonatorRecords];
			fourModes(_c, _m, records);
			const uint32_t seventh = fourModes(_c, _m + 4, records + 16);
			for(size_t k = 0; k < _out.size(); ++k)
				_out[k] = sx(ix[0x40 + k]);
			return seventh;
		}

		// BD's, MT's and ML's, their state three words each from Y:_state: the accumulator's two and the last output
		void resonate(Context& _c, const uint32_t _state, Block& _out)
		{
			Mode m[kModes];
			for(uint32_t k = 0; k < kModes; ++k)
				m[k] = {acc(_c.y[_state + 3 * k], _c.y[_state + 3 * k + 1]), _c.y[_state + 3 * k + 2]};
			resonators(_c, m, _out);
			for(uint32_t k = 0; k < kModes; ++k)
			{
				uint32_t* const s = &_c.y[_state + 3 * k];
				s[0] = lim(m[k].a);
				s[1] = lo(m[k].a);
				s[2] = m[k].x1;
			}
		}

		// SD's and RS's, their state two words each from S+_state, the accumulator in L, then in X the last output. They
		// start a word lower, from X:$3F, which ends holding what the eighth started from
		void resonateL(Context& _c, const uint32_t _state, Block& _out)
		{
			Mode m[kModes];
			for(uint32_t k = 0; k < kModes; ++k)
				m[k] = {acc(_c.x[_state + 2 * k], _c.y[_state + 2 * k]), _c.x[_state + 2 * k + 1]};
			_c.shared.scratch.x[0x3f] = resonators(_c, m, _out);
			for(uint32_t k = 0; k < kModes; ++k)
			{
				limL(m[k].a, _c.x[_state + 2 * k], _c.y[_state + 2 * k]);
				_c.x[_state + 2 * k + 1] = m[k].x1;
			}
		}

		// The words of PiBank, and where each machine's excitation starts and stops
		constexpr uint32_t kBd = 0, kSd = 1, kMt = 2, kMl = 3, kRs = 4;
		constexpr uint32_t kBdStart = 0x142291, kSdStart = 0x142f4b, kMtStart = 0x143691, kMlStart = 0x143f50,
			kRsStart = 0x144cb2;

		constexpr uint32_t kLineWords = 0x200, kLineMask = kLineWords - 1;

		// $142100: each voice's three lines, one after the other, at Y:_word on
		void linesAt(Context& _c, const uint32_t _word)
		{
			auto& y = _c.y;
			y[_word] = firstLine(_c);
			y[_word + 1] = firstLine(_c) + kLineWords;
			y[_word + 2] = firstLine(_c) + 2 * kLineWords;
		}

		// Whether the block's positions, X:$00-$1F, all lie in the zeros after the excitation, as after most of a voice's
		// Hit: a word read there would go the whole way through external(), and the positions run from the first to the last
		bool pastEnd(const Context& _c, const Data& _d)
		{
			const auto& ix = _c.shared.scratch.x;
			const uint32_t end = _d.addr + _d.size;
			return ix[0] - end < PiBank::ZerosAfter && ix[31] + 1 - end < PiBank::ZerosAfter;
		}

		// BD's, MT's and ML's excitation over a block. _a the step, Y:$FE, its upper bits the words a sample: positions
		// from _hi:_lo, split at X:$00-$1F and Y:$00-$1F, their complement at Y:$20-$3F, stopping at _d's end; the words
		// between two addresses at X:$40-$5F
		void strike(const Context& _c, const Data& _d, const int64_t _a, uint32_t& _hi, uint32_t& _lo)
		{
			auto& ix = _c.shared.scratch.x;
			auto& iy = _c.shared.scratch.y;

			iy[0xfe] = lim(_a);
			const int64_t step = _a >> 22, words = acc(hi(step), lo(step));
			iy[0x40] = 0xffffff;
			int64_t b = acc(_hi, _lo);
			const int64_t one = acc(0x7fffff, 0xffffff);
			for(uint32_t k = 0; k < 32; ++k)
			{
				b += words;
				limL(b, ix[k], iy[k]);
				iy[0x20 + k] = lo(one - b);
			}
			if(const uint32_t stop = _d.addr + _d.size; b >= acc(stop))
			{
				testHooks::probe(testHooks::PiExcitationEnded);
				b = acc(stop);
			}
			_lo = lo(b);
			_hi = hi(b);
			if(pastEnd(_c, _d))
			{
				std::fill_n(&ix[0x40], 32, 0u);
				return;
			}
			for(uint32_t k = 0; k < 32; ++k)
			{
				const uint32_t p = ix[k];
				const int64_t s = mulSU(read(_c, _d, p), iy[0x20 + k]) + mulSU(read(_c, _d, p + 1), iy[k]);
				ix[0x40 + k] = lim(s);
			}
		}

		// SD's and RS's positions: two running sums at twice the step _a, one a step ahead, into X:$00-$1F and Y:$00-$1F;
		// _hi:_lo left where the first ends, at _d's end at the most. Returns the second's end, B, which P-I-SD then reads
		int64_t positionsTwice(const Context& _c, const Data& _d, const int64_t _a, uint32_t& _hi, uint32_t& _lo)
		{
			auto& ix = _c.shared.scratch.x;
			auto& iy = _c.shared.scratch.y;

			const int64_t x = acc(hi(_a), lo(_a));
			const int64_t twice = shl(_a, 1);
			const int64_t y = acc(hi(twice), lo(twice));
			int64_t a = acc(_hi, _lo);
			int64_t b = a + x;
			for(uint32_t k = 0; k < 32; k += 2)
			{
				limL(a, ix[k], iy[k]);
				a += y;
				limL(b, ix[k + 1], iy[k + 1]);
				b += y;
			}
			if(const uint32_t stop = _d.addr + _d.size; a >= acc(stop))
			{
				testHooks::probe(testHooks::PiExcitationEnded);
				a = acc(stop);
			}
			_hi = hi(a);
			_lo = lo(a);
			return b;
		}

		// The input's one-pole by _x0 over X:$40-$5F, or Y:$40-$5F, its output at X:$60-$7F, and over X:$40-$5F too with
		// _Rewrite, kept in Y:_state
		template<bool FromY, bool Rewrite>
		void inputPole(const Context& _c, const uint32_t _state, const uint32_t _x0)
		{
			auto& ix = _c.shared.scratch.x;
			const auto& iy = _c.shared.scratch.y;
			int64_t a = acc(_c.y[_state]);
			for(uint32_t k = 0; k < 32; ++k)
			{
				const uint32_t held = onePole(a, FromY ? iy[0x40 + k] : ix[0x40 + k], _x0);
				if constexpr(Rewrite)
					ix[0x40 + k] = held;
				ix[0x60 + k] = held;
			}
			_c.y[_state] = lim(a);
		}

		// A line's one-pole by _x1 from _r2, at X:_out-, kept in Y:_state
		void line(const Context& _c, const uint32_t _state, const uint32_t _x1, const uint32_t _r2, const uint32_t _out)
		{
			auto& ix = _c.shared.scratch.x;
			int64_t a = acc(_c.y[_state]);
			uint32_t r2 = _r2;
			for(uint32_t k = 0; k < 32; ++k)
			{
				ix[_out + k] = onePole(a, lineWord(_c, r2), _x1);
				r2 = next(r2, kLineMask);
			}
			_c.y[_state] = lim(a);
		}

		// What the lines hear at sample k. A the input at X:$60-$7F, B to D the lines at X:$A0-$BF, $C0-$DF, $80-$9F; each
		// line takes the input at _coupling, _y0 its neighbours', from X:$FC, and the third _x1 its own
		struct Scatter
		{
			uint32_t x1, y0, coupling;

			void at(const std::array<uint32_t, 0x200>& _ix, const uint32_t _k, uint32_t& _first, uint32_t& _second,
				uint32_t& _third) const
			{
				const int64_t a = mul(_ix[0x60 + _k], coupling);
				_third = lim(a + mul(_ix[0x80 + _k], x1));
				_second = lim(a + mulSum(y0, _ix[0xc0 + _k], _ix[0xa0 + _k]));
				_first = lim(a + mulDiff(y0, _ix[0xc0 + _k], _ix[0xa0 + _k]));
			}
		};

		// BD's and MT's: into the lines at once, the pointers kept in the state
		void scatter(const Context& _c, const uint32_t _coupling)
		{
			auto& y = _c.y;
			auto& ix = _c.shared.scratch.x;
			_c.shared.scratch.y[0xff] = outputBuffer(_c);
			const Scatter s{ix[0xfb], ix[0xfc], _coupling};
			uint32_t r4 = y[0x14], r5 = y[0x15], r7 = y[0x16];
			for(uint32_t k = 0; k < 32; ++k)
			{
				s.at(ix, k, lineWord(_c, r4), lineWord(_c, r5), lineWord(_c, r7));
				r4 = next(r4, kLineMask);
				r5 = next(r5, kLineMask);
				r7 = next(r7, kLineMask);
			}
			y[0x14] = r4;
			y[0x15] = r5;
			y[0x16] = r7;
		}

		// SD's and RS's: the third line at once, the others through Y:$00-$3F
		void scatterThrough(const Context& _c, const uint32_t _coupling)
		{
			auto& y = _c.y;
			auto& ix = _c.shared.scratch.x;
			auto& iy = _c.shared.scratch.y;
			iy[0xff] = outputBuffer(_c);
			const Scatter s{ix[0xfb], ix[0xfc], _coupling};
			uint32_t r0 = y[0x14], r1 = y[0x15], r7 = y[0x16];
			for(uint32_t k = 0; k < 32; ++k)
			{
				s.at(ix, k, iy[0x20 + k], iy[k], lineWord(_c, r7));
				r7 = next(r7, kLineMask);
				lineWord(_c, r0) = iy[0x20 + k];
				lineWord(_c, r1) = iy[k];
				r0 = next(r0, kLineMask);
				r1 = next(r1, kLineMask);
			}
			y[0x14] = r0;
			y[0x15] = r1;
			y[0x16] = r7;
		}

		// BD's and MT's ($1421FF): X:$40-$5F times four over a two-pole of the lines' sum, kept at Y:$20 and Y:$21
		void feedback(const Context& _c)
		{
			auto& y = _c.y;
			auto& ix = _c.shared.scratch.x;
			int64_t b = acc(y[0x20]);
			uint32_t y0 = y[0x21];
			const uint32_t x1 = y[0x1c];
			for(uint32_t k = 0; k < 32; ++k)
			{
				const int64_t sum = acc(ix[0xc0 + k]) - acc(ix[0xa0 + k]) + acc(ix[0x80 + k]);
				const uint32_t last = lim(b);
				const uint32_t held = lim(sum);
				b = mul(x1, last) - mul(0x400000, y0) + mul(0x400000, held);
				y0 = held;
				ix[0x40 + k] = lim(shl(acc(ix[0x40 + k]) + acc(last), 2));
			}
			y[0x21] = y0;
			y[0x20] = lim(b);
		}

		// SD's and RS's first steps: Y:$0F and Y:$13 decay, and the pitch's bend. Y:$FE the step
		int64_t envelopes(Context& _c)
		{
			auto& y = _c.y;
			const uint32_t b = lim(mul(0x7f7cee, y[0xf]));
			const uint32_t a = lim(mul(0x7c28f6, y[0x13]));
			y[0xf] = b;
			y[0x13] = a;
			return mul(y[5], b) + mul(y[3], a) + acc(0x400000);
		}

		// $142DE0: Y:$26, Y:$24 by Y:$07 and Y:$25, the noise's gates
		void gates(Context& _c)
		{
			auto& y = _c.y;
			y[0x26] = lim(mul(0x7f5c29, y[0x26]));
			y[0x24] = lim(mul(y[7], y[0x24]));
			y[0x25] = lim(mul(0x47ae14, y[0x25]));
		}

		// A noise's own one-pole over Y:$00-$1F (Y:$27), its words a sample late. Returns what the DSP holds in X0
		uint32_t colour(const Context& _c)
		{
			auto& iy = _c.shared.scratch.y;
			int64_t b = acc(_c.y[0x27]);
			uint32_t x0 = 0;
			for(uint32_t k = 0; k < 32; ++k)
			{
				x0 = lim(b);
				const uint32_t in = iy[k];
				iy[k] = x0;
				b += acc(in) + mul(0xd9999a, x0);
			}
			_c.y[0x27] = lim(b);
			return x0;
		}

		// The noise's level: the snare's envelope, by Y:$06. A word of the noise past the gate Y:$24 opens Y:$25
		uint32_t noiseLevel(Context& _c, const uint32_t _last, const uint32_t _x0)
		{
			auto& y = _c.y;
			uint32_t x0 = _x0;
			if(std::llabs(acc(_last)) >= acc(y[0x24]))
			{
				testHooks::probe(testHooks::PiNoiseOpens);
				x0 = 0x7fffff;
				y[0x25] = x0;
			}
			return lim(mul(y[6], lim(mul(y[0x24], x0))));
		}

		// SD's and RS's modes: the first four pitches from Y:$FE, the others from Y:$01
		void pitches(const Context& _c, const Data& _d, const uint32_t _addr)
		{
			auto& iy = _c.shared.scratch.y;
			for(uint32_t w = 0; w < kModes; ++w)
				iy[0xb4 + w] = lim(mul(read(_c, _d, _addr + w), w < 4 ? iy[0xfe] : _c.y[1]));
		}

		// BD's, MT's and ML's modes: each from Y:$FE, by words of Y from _addr
		void pitchesAll(const Context& _c, const Data* const _d, const uint32_t _addr)
		{
			auto& iy = _c.shared.scratch.y;
			for(uint32_t w = 0; w < kModes; ++w)
				iy[0xb4 + w] = lim(mul(_d ? read(_c, *_d, _addr + w) : iy[_addr + w], iy[0xfe]));
		}

		// The eight gains at X:$00-$07 ($142EEA): four words as they are, four by Y:$08
		void gains(const Context& _c, const Data& _d, const uint32_t _addr)
		{
			auto& ix = _c.shared.scratch.x;
			for(uint32_t w = 0; w < kModes; ++w)
			{
				const uint32_t word = read(_c, _d, _addr + w);
				ix[w] = w < 4 ? word : lim(mul(_c.y[8], word));
			}
		}

		// Y:_first on, _count words
		void clearY(Context& _c, const uint32_t _first, const uint32_t _count)
		{
			for(uint32_t k = 0; k < _count; ++k)
				_c.y[_first + k] = 0;
		}
	}

	namespace bd
	{
		// $142100, $14213C
		void init(Context& _c)
		{
			auto& y = _c.y;
			y[3] = y[0x12] = 0;
			clearY(_c, 0x22, 0x18);
			clearY(_c, 0x1d, 5);
			y[2] = 0x7fdf3b;
			linesAt(_c, 0x14);
			y[0x17] = 0x10b;
			y[0x18] = 0xff;
			y[0x19] = 0xb5;
			y[0x1a] = 0x7ac083;
			y[6] = 0x733333;
			y[0x1c] = 0x59999a;
		}

		void update(Context& _c)
		{
			auto& y = _c.y;
			y[0x10] = 0;
			y[0x11] = kBdStart;
			y[0x13] = 0x7fffac;
		}

		// $142145
		void render(Context& _c, Block& _out)
		{
			auto& y = _c.y;
			const uint32_t level = y[0x13];
			y[0x13] = lim(mul(0x7c28f6, level));
			const uint32_t bend = lim(mul(y[5], level) + acc(0x400000));

			const auto d = data(_c, kBd);
			strike(_c, d, shl(mul(bend, y[1]), 1), y[0x11], y[0x10]);
			lineDamping(_c, y[2], 0x42);

			inputPole<false, true>(_c, 0x12, y[4]);
			line(_c, 0x1d, y[6], advance(y[0x14], y[0x17], kLineMask), 0x80);
			line(_c, 0x1e, y[6], advance(y[0x15], y[0x18], kLineMask), 0xa0);
			line(_c, 0x1f, y[6], advance(y[0x16], y[0x19], kLineMask), 0xc0);
			scatter(_c, 0x51eb8);
			feedback(_c);

			const uint32_t table = 0x142279;
			pitchesAll(_c, &d, table);
			cosines(_c.shared.scratch.y);
			tuneModes(_c, y[2], [&](const uint32_t _m) { return read(_c, d, table + 8 + _m); },
				[&](const uint32_t _m) { return read(_c, d, table + 16 + _m); });
			resonate(_c, 0x22, _out);
		}
	}

	namespace sd
	{
		// $142D49, $142D88
		void init(Context& _c)
		{
			auto& y = _c.y;
			y[0x12] = 0;
			for(uint32_t k = 0; k < 0x10; ++k)
				_c.x[0x29 + k] = y[0x29 + k] = 0;
			clearY(_c, 0x1d, 5);
			y[5] = y[6] = 0x7ffcb9;
			linesAt(_c, 0x14);
			y[0x17] = 0x10b;
			y[0x18] = 0xff;
			y[0x19] = 0xb5;
			y[0x1b] = 0x19999a;
			y[0x1c] = 0x59999a;
			y[0x22] = 0xa;
			y[0x23] = 1;
		}

		void update(Context& _c)
		{
			auto& y = _c.y;
			y[0x11] = y[0x25] = 0;
			_c.x[0x11] = kSdStart;
			y[0xf] = y[0x13] = y[0x26] = y[0x24] = 0x7fffff;
		}

		// $142DAE-$142DE0: the excitation at Y:$40-$5F, its fractions halved, the first two with the position's last bit
		// for their top bit, their complements negative
		void excite(const Context& _c, const Data& _d, const int64_t _a)
		{
			auto& ix = _c.shared.scratch.x;
			auto& iy = _c.shared.scratch.y;
			uint32_t& hiWord = _c.x[0x11];
			const int64_t step = _a >> 22;
			iy[0xfe] = lim(_a);
			const int64_t b = positionsTwice(_c, _d, step, hiWord, _c.y[0x11]);

			int64_t a = acc(hiWord, _c.y[0x11]), c = b;
			for(uint32_t k = 0; k < 32; k += 2)
			{
				a = (a & ~0xffffffll) | iy[k];
				c = (c & ~0xffffffll) | iy[k + 1];
				a >>= 1;
				c >>= 1;
				iy[k] = lo(a);
				iy[k + 1] = lo(c);
				iy[0x20 + k] = lo(a - 0x7fffff);
				iy[0x20 + k + 1] = lo(c - 0x7fffff);
				a = 0;
				c = 0;
			}
			if(pastEnd(_c, _d))
			{
				std::fill_n(&iy[0x40], 32, 0u);
				return;
			}
			for(uint32_t k = 0; k < 32; ++k)
			{
				const uint32_t p = ix[k];
				const int64_t s = mul(iy[0x20 + k], read(_c, _d, p)) - mul(iy[k], read(_c, _d, p + 1));
				iy[0x40 + k] = lim(s);
			}
		}

		// $142D95
		void render(Context& _c, Block& _out)
		{
			auto& y = _c.y;
			auto& ix = _c.shared.scratch.x;
			auto& iy = _c.shared.scratch.y;

			const uint32_t bend = lim(envelopes(_c));
			const auto d = data(_c, kSd);
			excite(_c, d, shl(mul(bend, y[1]), 1));

			gates(_c);
			lineDamping(_c, y[2], 0x1e);
			const uint32_t lineGain = lim(acc(0x333333) + mul(0xd9999a, y[0x26]));
			ix[0xf9] = lineGain;

			inputPole<true, false>(_c, 0x12, y[4]);
			line(_c, 0x1d, lineGain, advance(y[0x14], y[0x17], kLineMask), 0x80);
			line(_c, 0x1e, lineGain, advance(y[0x15], y[0x18], kLineMask), 0xa0);
			const uint32_t last = noise(_c, 0x22, 16);
			iy[0xfa] = last;
			line(_c, 0x1f, lineGain, advance(y[0x16], y[0x19], kLineMask), 0xc0);
			const uint32_t held = colour(_c);
			const uint32_t level = noiseLevel(_c, last, held);

			// $142E60: the excitation and the noise, into X:$40-$5F and X:$60-$7F
			const uint32_t toInput = lim(mul(0x266666, level)), toOut = lim(mul(0x59999a, level));
			for(uint32_t k = 0; k < 32; ++k)
			{
				const uint32_t n = iy[k];
				ix[0x40 + k] = lim(shl(acc(iy[0x40 + k]), 1) + mul(toOut, n));
				ix[0x60 + k] = lim(acc(ix[0x60 + k]) + mul(toInput, n));
			}

			scatterThrough(_c, 0x7eb852);

			// $142EB4: the lines' sum, a two-pole at Y:$20-$21, kept with a word of X:$40-$5F
			int64_t b = acc(y[0x20]);
			uint32_t y0 = y[0x21];
			int64_t a = acc(ix[0x40]) + b;
			for(uint32_t k = 0; k < 32; ++k)
			{
				const uint32_t held2 = lim(a);
				const int64_t sum = acc(ix[0xa0 + k]) + acc(ix[0xc0 + k]) + acc(ix[0x80 + k]);
				ix[0x40 + k] = held2;
				const uint32_t last2 = lim(b);
				b -= acc(y0);
				y0 = lim(sum);
				b += sum;
				a = acc(ix[0x41 + k]) + acc(last2);
				b -= mul(last2, y[0x1c]);
			}
			y[0x21] = y0;
			y[0x20] = lim(b);

			pitches(_c, d, 0x142f33);
			cosines(iy);
			gains(_c, d, 0x142f43);
			tuneModes(_c, y[2], [&](const uint32_t _m) { return read(_c, d, 0x142f3b + _m); },
				[&](const uint32_t _m) { return ix[_m]; });
			resonateL(_c, 0x29, _out);
		}
	}

	namespace mt
	{
		// $1434A6, $1434D6
		void init(Context& _c)
		{
			auto& y = _c.y;
			y[0x12] = 0;
			clearY(_c, 0x23, 0x18);
			clearY(_c, 0x1d, 5);
			y[0x1c] = 0x59999a;
			linesAt(_c, 0x14);
			y[0x17] = 0x1d2;
			y[0x18] = 0x136;
			y[0x19] = 0xe8;
			y[0x1a] = 0x7ac083;
		}

		void update(Context& _c)
		{
			auto& y = _c.y;
			y[0x10] = 0;
			y[0x11] = kMtStart;
			y[0x13] = 0x7fffac;
		}

		// How far back a line reads: its length, by a part of Y:$07
		uint32_t tap(const Context& _c, const uint32_t _length)
		{
			const uint32_t tune = lim(mul(0x666666, _c.y[7]) + acc(0x19999a));
			return lim(mul(tune, _length));
		}

		uint32_t back(const Context& _c, const uint32_t _pointer, const uint32_t _length)
		{
			return advance(_pointer, (0 - tap(_c, _length)) & 0xffffff, kLineMask);
		}

		// Two tables of words mixed by a word: (1 - _mix) of the first plus _mix of the second
		template<typename Store>
		void mixTables(const Context& _c, const Data& _d, const uint32_t _mix, const uint32_t _first,
			const uint32_t _second, Store&& _store)
		{
			const uint32_t rest = lim(acc(0x7fffff) - acc(_mix));
			for(uint32_t w = 0; w < kModes; ++w)
				_store(w, lim(mul(rest, read(_c, _d, _first + w)) + mul(_mix, read(_c, _d, _second + w))));
		}

		// $1434DF
		void render(Context& _c, Block& _out)
		{
			auto& y = _c.y;
			auto& ix = _c.shared.scratch.x;
			auto& iy = _c.shared.scratch.y;

			const uint32_t level = y[0x13];
			y[0x13] = lim(mul(0x7ef9db, level));
			const int64_t shape = mul(0x333333, y[3]) - acc(0x66666);
			const uint32_t bend = lim(mul(lim(shape), level) + acc(0x400000));

			const auto d = data(_c, kMt);
			strike(_c, d, shl(mul(bend, y[1]), 1), y[0x11], y[0x10]);
			lineDamping(_c, y[2], 0x1ff);

			inputPole<false, true>(_c, 0x12, y[4]);
			line(_c, 0x1d, y[8], back(_c, y[0x14], y[0x17]), 0x80);
			line(_c, 0x1e, y[8], back(_c, y[0x15], y[0x18]), 0xa0);
			line(_c, 0x1f, y[8], back(_c, y[0x16], y[0x19]), 0xc0);
			scatter(_c, 0x28f5c3);
			feedback(_c);

			// $1435D5: the modes' factors, gains and pitches, each two rows of words mixed
			mixTables(_c, d, y[6], 0x143671, 0x143679, [&](const uint32_t _w, const uint32_t _v) { ix[0xdc + _w] = _v; });
			mixTables(_c, d, y[6], 0x143681, 0x143689, [&](const uint32_t _w, const uint32_t _v) { ix[0xe4 + _w] = _v; });
			mixTables(_c, d, y[5], 0x143661, 0x143669, [&](const uint32_t _w, const uint32_t _v) { iy[0xec + _w] = _v; });

			pitchesAll(_c, nullptr, 0xec);
			cosines(iy);
			tuneModes(_c, y[2], [&](const uint32_t _m) { return ix[0xdc + _m]; },
				[&](const uint32_t _m) { return ix[0xe4 + _m]; });
			resonate(_c, 0x23, _out);
		}
	}

	namespace ml
	{
		// $143E0D, $143E3D
		void init(Context& _c)
		{
			auto& y = _c.y;
			y[3] = y[0xd] = y[0x15] = y[0x16] = y[0x17] = y[0x18] = y[0x19] = 0;
			clearY(_c, 0x21, 0x18);
			y[8] = 0x7fffac;
			y[9] = 0x7fdf3b;
			linesAt(_c, 0xf);
			y[0x12] = 0x7f;
			y[0x13] = 0x5f;
			y[0x14] = 0x3f;
			y[0x1a] = 0xa;
			y[0x1b] = 1;
		}

		void update(Context& _c)
		{
			auto& y = _c.y;
			y[0xb] = y[0x1d] = 0;
			y[0xc] = kMlStart;
			y[0xe] = y[0x1c] = y[0xa] = 0x7fffac;
		}

		// $143E49
		void render(Context& _c, Block& _out)
		{
			auto& y = _c.y;
			auto& ix = _c.shared.scratch.x;
			auto& iy = _c.shared.scratch.y;

			const uint32_t level = y[0xe];
			y[0xe] = lim(mul(y[6], level));
			const uint32_t bend = lim(mul(y[5], level) + acc(0x400000));
			const auto d = data(_c, kMl);
			strike(_c, d, shl(mul(bend, y[1]), 1), y[0xc], y[0xb]);

			for(uint32_t k = 0; k < 32; ++k)
				ix[0x40 + k] = lim(shl(mul(ix[0x40 + k], y[3]), 1));

			y[0x1e] = lim(mul(0x7f5c29, y[0x1e]));
			y[0x1c] = lim(mul(0x7ef9db, y[0x1c]));
			y[0x1d] = lim(mul(0x47ae14, y[0x1d]));

			inputPole<false, false>(_c, 0xd, y[8]);

			// $143EAC: a two-pole whose input the DSP clears before adding it: Y:$18 and Y:$19 stay 0, and X:$41-$5F are 0
			for(uint32_t k = 1; k < 32; ++k)
				ix[0x40 + k] = 0;

			pitchesAll(_c, &d, 0x143f38);
			cosines(iy);
			for(uint32_t w = 0; w < kModes; ++w)
			{
				ix[0xe8 + w] = lim(mul(read(_c, d, 0x143f48 + w), y[7]));
				ix[0xe0 + w] = lim(mul(read(_c, d, 0x143f40 + w), y[4]));
			}
			tuneModes(_c, y[9], [&](const uint32_t _m) { return ix[0xe0 + _m]; },
				[&](const uint32_t _m) { return ix[0xe8 + _m]; });
			resonate(_c, 0x21, _out);

			// $143F28: the level, a word Y:$02 decays, halved
			const uint32_t gain = lim(mul(y[0xa], 0x400000));
			y[0xa] = lim(mul(y[2], y[0xa]));
			for(size_t k = 0; k < _out.size(); ++k)
				_out[k] = sx(lim(mul(gain, ix[0x40 + k])));
		}
	}

	namespace rs
	{
		// $144AB9, $144AF4
		void init(Context& _c)
		{
			auto& y = _c.y;
			y[0x12] = 0;
			for(uint32_t k = 0; k < 0x10; ++k)
				_c.x[0x2a + k] = y[0x2a + k] = 0;
			clearY(_c, 0x1d, 5);
			y[5] = y[6] = 0x7ffcb9;
			linesAt(_c, 0x14);
			y[0x17] = 0x10b;
			y[0x18] = 0xff;
			y[0x19] = 0xb5;
			y[0x1c] = 0xccccd;
			y[0x22] = 0xa;
			y[0x23] = 1;
		}

		void update(Context& _c)
		{
			auto& y = _c.y;
			y[0x10] = y[0x25] = 0;
			y[0x11] = kRsStart;
			y[0xf] = y[0x13] = y[0x26] = y[0x24] = 0x7fffff;
		}

		// $144B01
		void render(Context& _c, Block& _out)
		{
			auto& y = _c.y;
			auto& ix = _c.shared.scratch.x;
			auto& iy = _c.shared.scratch.y;

			envelopes(_c);
			const int64_t rate = shl(mul(0x400000, y[1]), 1);
			iy[0xfe] = lim(rate);
			const auto d = data(_c, kRs);

			positionsTwice(_c, d, rate >> 22, y[0x11], y[0x10]);
			if(pastEnd(_c, d))
			{
				std::fill_n(&iy[0x40], 32, 0u);
			}
			else
			{
				for(uint32_t k = 0; k < 32; ++k)
				{
					const uint32_t p = ix[k], f = iy[k];
					const uint32_t w = read(_c, d, p);
					const int64_t s = -mulSU(w, f) + 2 * acc(w) + mulSU(read(_c, d, p + 1), f);
					iy[0x40 + k] = lim(s);
				}
			}

			for(uint32_t k = 0; k < 32; ++k)
				ix[0x40 + k] = lim(shl(mul(y[3], iy[0x40 + k]), 4));

			gates(_c);
			lineDamping(_c, y[2], 0x64);
			const uint32_t lineGain = lim(acc(0x400000) + mul(0xd9999a, y[0x26]));
			ix[0xf9] = lineGain;

			inputPole<false, false>(_c, 0x12, 0x7ffcb9);
			line(_c, 0x1d, lineGain, advance(y[0x14], y[0x17], kLineMask), 0x80);
			line(_c, 0x1e, lineGain, advance(y[0x15], y[0x18], kLineMask), 0xa0);
			const uint32_t noiseWord = noise(_c, 0x22, 16);
			iy[0xfa] = noiseWord;
			line(_c, 0x1f, lineGain, advance(y[0x16], y[0x19], kLineMask), 0xc0);
			const uint32_t held = colour(_c);
			const uint32_t level = noiseLevel(_c, noiseWord, held);

			// $144BCF: the noise into X:$40-$5F and X:$60-$7F
			const uint32_t toBoth = lim(mul(0x266666, level));
			for(uint32_t k = 0; k < 32; ++k)
			{
				const uint32_t n = iy[k];
				ix[0x40 + k] = lim(acc(ix[0x40 + k]) + mul(toBoth, n));
				ix[0x60 + k] = lim(acc(ix[0x60 + k]) + mul(toBoth, n));
			}

			scatterThrough(_c, 0x7eb852);

			// $144C1C: the lines' sum, a two-pole at Y:$20-$21 into X:$60-$7F
			int64_t b = acc(y[0x20]);
			uint32_t y1 = y[0x20], y0 = y[0x21];
			for(uint32_t k = 0; k < 32; ++k)
			{
				const int64_t sum = acc(ix[0xa0 + k]) + acc(ix[0xc0 + k]) + acc(ix[0x80 + k]);
				ix[0x60 + k] = y1;
				y1 = lim(b);
				b += sum - acc(y0);
				y0 = lim(sum);
				b -= mul(y1, y[0x1c]);
			}
			y[0x21] = y0;
			y[0x20] = lim(b);

			pitches(_c, d, 0x144c9a);
			cosines(iy);
			gains(_c, d, 0x144caa);
			tuneModes(_c, y[2], [&](const uint32_t _m) { return read(_c, d, 0x144ca2 + _m); },
				[&](const uint32_t _m) { return ix[_m]; });
			resonateL(_c, 0x2a, _out);
		}
	}

	namespace ma
	{
		// $145AA7, $145AB7
		void init(Context& _c)
		{
			auto& y = _c.y;
			y[0xd] = 0x2228;
			y[0xe] = 0x442;
			y[7] = y[8] = y[9] = y[0xa] = y[0xb] = y[0xc] = y[0xf] = 0;
		}

		void update(Context& _c)
		{
			_c.y[7] = 0x66666;
		}

		// $145ABB: a noise whose words below Y:$01 add a shake to a level, Y:$07 of it decaying by Y:$03. The level,
		// which Y:$04 decays, plays the noise through a two-pole (Y:$05, Y:$06) and a difference
		void render(Context& _c, Block& _out)
		{
			auto& y = _c.y;
			auto& ix = _c.shared.scratch.x;
			fibonacci(&ix[0], y[0xd], y[0xe]);

			const uint32_t chance = y[1], gain = y[2], fall = y[3], decay = y[4], c0 = y[5], c1 = y[6];
			uint32_t shake = y[7], level = y[8], p0 = y[0xa], p1 = y[0xb], last = y[0xf];
			for(uint32_t k = 0; k < 32; ++k)
			{
				const uint32_t in = ix[k];
				int64_t a = mul(level, decay);
				if(std::llabs(acc(in)) <= acc(chance))
				{
					testHooks::probe(testHooks::PiMaShake);
					a += acc(shake);
				}
				level = lim(a);
				a = shl(round(mul(gain, lim(round(mul(level, in))))), 3) >> 1;
				a = shl(round(a - mul(p0, c0)), 1);
				a = round(a - mul(p1, c1));
				p1 = p0;
				p0 = lim(a);
				_out[k] = sx(lim(acc(p1) - acc(last)));
				last = p1;
				shake = lim(mul(shake, fall));
			}
			y[7] = shake;
			y[8] = level;
			y[0xa] = p0;
			y[0xb] = p1;
			y[0xf] = last;
		}
	}

	const Routines* routines(const uint32_t _type)
	{
		static constexpr Routines kRoutines[9] = {
			{bd::init, bd::update, bd::render, true},
			{sd::init, sd::update, sd::render, true},
			{mt::init, mt::update, mt::render, true},
			{ml::init, ml::update, ml::render, true},
			{ma::init, ma::update, ma::render},
			{rs::init, rs::update, rs::render, true},
			{rc::init, rc::update, rc::render},
			{cc::init, cc::update, cc::render},
			{hh::init, hh::update, hh::render},
		};
		return _type - 65 < std::size(kRoutines) ? &kRoutines[_type - 65] : nullptr;
	}
}
