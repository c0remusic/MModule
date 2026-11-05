// The E12 machines' voice code (DSP2, OS 1.63), ported as mdVoiceTrx.h says.
//
// They share one way of playing a sample. The pitch word Y:$01, bent by Y:$08 of the level, is a step of up to 4
// samples. A block's positions, relative to Y:$10:$11, go to X:$A0-$BF, and their phases pick the cubic B-spline's
// taps, whose pointers go to Y:$A0-$BF. Past the end Y:$1C (the length times Y:$07) the position stays at the length,
// so that a voice goes on reading what follows its sample, a block at a time. A retrigger, Y:$09 of them Y:$0A samples
// apart, moves it back to the start (the length times Y:$06), and subtracts from the block's last positions a word the
// DSP's code means as the offset but that holds the start too: with a start, these point outside the window and read
// what lies there. The window, 68 sample words at X:$00-$87 in the order that puts the position first, is the block's
// reach at the top step. A sample at each position, level by level, then goes through the rest.
//
// Four renders do that:
// - E12-BD's ($103945) has no filter, and a level that Y:$20, decaying by Y:$05, adds to the one Y:$03 decays.
// - The one nine machines share ($103838) ends in a two-pole high-pass whose gain follows its poles.
// - E12-RS's ($103A10) keeps a second sample's window at Y:$00-$87 while the position is in it, heard whole over the
//   first at Y:$0B.
// - The mixed machines' ($103B46) mix the two by Y:$0B. They and E12-RS's end in the fixed high-pass of the others.
// The second sample's address and length come from the directory by address, in each machine's update.
#include "mdVoiceTrx.h"

#include <algorithm>
#include <iterator>

namespace md::voice::e12
{
	using namespace trx;

	namespace
	{
		constexpr uint32_t kPositions = 0xa0;	// X: the positions, Y: the taps' pointers
		constexpr uint32_t kInput = 0xc8;		// X: the filter's input
		constexpr uint32_t kWindowSamples = 0x88;
		constexpr uint32_t kWindowWords = kWindowSamples / 2;

		// Y:$1C, then the block's positions and pointers, from the level the pitch is bent by. The DSP also clears a step
		// below zero ($10384E), which no word gives: the bend is -1 at the lowest. E12-BD reads the phase unsigned from
		// the table's start, so that a row's t is half a sample off; the others signed from its middle, _flip $200
		void positions(Context& _c, const uint32_t _level, const uint32_t _flip)
		{
			auto& y = _c.y;
			auto& ix = _c.shared.scratch.x;
			auto& iy = _c.shared.scratch.y;

			y[0x1c] = lim(mul(y[0x13], y[7]));
			const uint32_t bend = lim(mul(_level, y[8]));
			int64_t step = shl(y[1] + (mul(bend, y[1]) >> 24), 3);
			if(step > acc(4))
			{
				testHooks::probe(testHooks::E12StepClamped);
				step = acc(4);
			}

			int64_t a = y[0x11];
			for(uint32_t k = 0; k < 32; ++k)
			{
				iy[kPositions + k] = TrxTables::InterpolationAddr + (((lo(a) >> 14) & 0x3fc) ^ _flip);
				ix[kPositions + k] = hi(a);
				a += step;
			}

			ix[0xfe] = hi(a);
			ix[0xff] = y[0x10];
			a += acc(y[0x10]);
			if(a >= acc(y[0x1c]))
			{
				testHooks::probe(testHooks::E12EndOfSample);
				a = acc(y[0x13]);
			}
			y[0x11] = lo(a);
			y[0x10] = hi(a);
		}

		// The retrigger: Y:$1E counts the samples from the last, Y:$1F the retriggers. _endsBlock, not in the render nine
		// machines share, whose code branches over it ($10389F, leaving $1038A1-$1038AB dead): a time under a block puts
		// the positions before the retrigger to 0 and carries its count
		void retrigger(Context& _c, const bool _endsBlock)
		{
			auto& y = _c.y;
			auto& ix = _c.shared.scratch.x;

			int64_t a = acc(y[0x1e]) + acc(0x20);
			if(a < acc(y[0xa]))
			{
				y[0x1e] = lim(a);
				return;
			}
			const int64_t count = acc(y[0x1f]) + acc(1);
			if(count >= acc(y[0xe]))
			{
				testHooks::probe(testHooks::E12RetriggerLast);
				return;
			}
			testHooks::probe(testHooks::E12Retrigger);
			y[0x1f] = lim(count);

			const uint32_t words = hi(a - acc(y[0xa])) & 0x1f;
			if(words == 0)
				testHooks::probe(testHooks::E12RetriggerNoWords);
			const uint32_t first = 0xbf - words;
			ix[0xc0] = 0;
			const uint32_t position = lim(mul(y[0x13], y[6]) + acc(ix[first]));
			y[0x10] = position;
			ix[0xff] = position;
			for(uint32_t k = 0; k < words; ++k)
				ix[first + k] = lim(acc(ix[first + k]) - acc(position));

			if(_endsBlock && acc(y[0xa]) < acc(0x20))
			{
				testHooks::probe(testHooks::E12RetriggerEnds);
				for(uint32_t k = 0; k < 32 - words; ++k)
					ix[kPositions + k] = 0;
				y[0x1e] = words;
				return;
			}
			y[0x1e] = 0;
		}

		// A window into _w, two samples a word from the sample starting at the word _start. The odd position's first
		// sample is the high half of a word it is past: it goes last, at $87
		void loadWindow(const Context& _c, const uint32_t _start, uint32_t* const _w)
		{
			const uint32_t position = _c.shared.scratch.x[0xff];
			const uint32_t addr = hi((acc(position) >> 1) + acc(_start));

			uint32_t words[kWindowWords];
			const uint32_t* const bank = _c.shared.e12;
			if(bank && addr - E12Bank::Addr <= E12Bank::Words - kWindowWords)
			{
				std::copy_n(bank + (addr - E12Bank::Addr), kWindowWords, words);
			}
			else
			{
				for(uint32_t k = 0; k < kWindowWords; ++k)
					words[k] = external(_c, (addr + k) & 0xffffff, testHooks::AreaX);
			}

			uint32_t at = 0;
			if(position & 1)
			{
				testHooks::probe(testHooks::E12OddPosition);
				at = kWindowSamples - 1;
			}
			for(uint32_t k = 0; k < kWindowWords; ++k)
			{
				_w[at] = words[k];
				at = at + 1 == kWindowSamples ? 0 : at + 1;
				_w[at] = (words[k] << 12) & 0xffffff;
				at = at + 1 == kWindowSamples ? 0 : at + 1;
			}
		}

		// The second sample, Y:$24 and Y:$23, at Y:$00-$87 while the position is in it, mixed over the first by _first
		// and by _second, or by 0 past its end
		void mixSecond(Context& _c, const uint32_t _first, const uint32_t _second)
		{
			auto& y = _c.y;
			auto& ix = _c.shared.scratch.x;
			auto& iy = _c.shared.scratch.y;

			uint32_t second = 0;
			if(acc(ix[0xff]) < acc(y[0x23]))
			{
				testHooks::probe(testHooks::E12SecondSample);
				loadWindow(_c, y[0x24], iy.data());
				second = _second;
			}
			else
			{
				testHooks::probe(testHooks::E12SecondEnded);
			}
			for(uint32_t k = 0; k < kWindowSamples; ++k)
				ix[k] = lim(mul(ix[k], _first) + mul(iy[k], second));
		}

		// The level a sample at Y:$00-$1F
		void levels(Context& _c)
		{
			auto& y = _c.y;
			auto& iy = _c.shared.scratch.y;

			uint32_t level = y[0x15];
			for(uint32_t k = 0; k < 32; ++k)
			{
				iy[k] = level;
				level = hi(mul(level, y[3]));
			}
			y[0x15] = level;
		}

		// A tap's window word; a position a retrigger left outside the internal memory reads what lies there
		inline uint32_t tapWord(const Context& _c, const uint32_t _addr)
		{
			if(_addr < 0x200)
				return _c.shared.scratch.x[_addr];
			testHooks::probe(testHooks::E12TapOutside);
			return external(_c, _addr, testHooks::AreaX);
		}

		// The sample at each position, from the window and the taps, for _store(k, the word). The first position is the
		// window's start whatever X:$A0 holds
		template<typename Store>
		void interpolate(const Context& _c, Store&& _store)
		{
			const auto& table = tables(_c).interpolation;
			const auto& ix = _c.shared.scratch.x;
			const auto& iy = _c.shared.scratch.y;

			uint32_t at = 0;
			for(uint32_t k = 0; k < 32; ++k)
			{
				const uint32_t* const tap = &table[iy[kPositions + k] - TrxTables::InterpolationAddr];
				int64_t b = 0;
				if(at < ix.size() - 3)
				{
					const uint32_t* const w = &ix[at];
					b = mul(tap[0], w[0]) + mul(tap[1], w[1]) + mul(tap[2], w[2]) + mul(tap[3], w[3]);
				}
				else
				{
					for(uint32_t j = 0; j < 4; ++j)
						b += mul(tap[j], tapWord(_c, (at + j) & 0xffffff));
				}
				_store(k, lim(b));
				at = ix[kPositions + k + 1];
			}
		}

		// The filters' input: the sample at the level
		void filterInput(Context& _c)
		{
			levels(_c);
			auto& ix = _c.shared.scratch.x;
			const auto& iy = _c.shared.scratch.y;
			interpolate(_c, [&](const uint32_t _k, const uint32_t _w) { ix[kInput + _k] = lim(mul(_w, iy[_k])); });
		}

		// Twice the poles' r c and -r^2, by Y:$04 (Q) and Y:$05 (the cutoff)
		struct Feedback
		{
			int64_t a, b;
		};

		Feedback feedback(const Context& _c)
		{
			const auto& t = tables(_c);
			const auto& y = _c.y;
			const Pole p = pole(_c, y[5] + word(t.e12Offsets, y[4]), y[5] + word(t.e12Offsets, 0x80 + y[4]));
			return {shl(mul(p.r, p.c), 1), shl(-mul(p.r, p.r), 1)};
		}

		// The two-pole filter over X:$C8-$E7, whose five words the DSP keeps at Y:$78-$7C, the recursion's _c1 taken twice.
		// The output adds to the accumulator, which keeps its low word and its extension from sample to sample, each
		// sample its upper word limited; it lands at L:$C6-$E5, over the input already read.
		void filter(Context& _c, Voice::Block& _out, const uint32_t _c0, const uint32_t _c1, const uint32_t _c2,
			const uint32_t _c3, const uint32_t _c4)
		{
			auto& y = _c.y;
			auto& ix = _c.shared.scratch.x;
			auto& iy = _c.shared.scratch.y;

			iy[0x78] = _c0;
			iy[0x79] = _c1;
			iy[0x7a] = _c2;
			iy[0x7b] = _c3;
			iy[0x7c] = _c4;

			int64_t a = acc(y[0x18], y[0x1b]);
			ix[0xc4] = y[0x17];
			ix[0xc5] = y[0x18];
			ix[0xc6] = y[0x1a];
			ix[0xc7] = y[0x19];
			y[0x1a] = ix[0xe6];
			y[0x19] = ix[0xe7];

			uint32_t o2 = ix[0xc4], o1 = ix[0xc5], i2 = ix[0xc6], i1 = ix[0xc7];
			for(uint32_t k = 0; k < 32; ++k)
			{
				const uint32_t i0 = ix[kInput + k];
				a = wrap56(a + mul(_c0, o2) + 2 * mul(_c1, o1) + mul(_c2, i2) + mul(_c3, i1) + mul(_c4, i0));
				uint32_t high, low;
				limL(a, high, low);
				ix[0xc6 + k] = high;
				iy[0xc6 + k] = low;
				_out[k] = sx(high);
				o2 = o1;
				o1 = high;
				i2 = i1;
				i1 = i0;
			}
			y[0x17] = ix[0xe4];
			y[0x18] = ix[0xe5];
			y[0x1b] = iy[0xe5];
		}

		// The render nine machines share ($103838): a gain that holds the filter's level
		void renderOne(Context& _c, Voice::Block& _out)
		{
			auto& y = _c.y;
			positions(_c, y[0x15], 0x200);
			retrigger(_c, false);
			loadWindow(_c, y[0x12], _c.shared.scratch.x.data());
			filterInput(_c);

			const auto [a, b] = feedback(_c);
			const int64_t gain = ((shl(a, 1) - b + acc(0x7fffff)) >> 3) + acc(0x400000);
			const uint32_t g = lim(gain >> 1);
			filter(_c, _out, lim(b), lim(a + acc(0xc00000)), g, lim(shl(mul(0x800000, g), 1)), g);
		}

		// E12-RS's ($103A10) and the mixed machines' ($103B46): the second sample at _second, and a fixed zero at DC,
		// 1/2 - z^-1 + 1/2 z^-2
		void renderTwo(Context& _c, Voice::Block& _out, const uint32_t _second)
		{
			auto& y = _c.y;
			positions(_c, y[0x15], 0x200);
			retrigger(_c, true);
			loadWindow(_c, y[0x12], _c.shared.scratch.x.data());
			mixSecond(_c, y[0xb], _second);
			filterInput(_c);

			const auto [a, b] = feedback(_c);
			filter(_c, _out, lim(b), lim(a + acc(0xc00000)), 0x400000, 0x800000, 0x400000);
		}

		void renderRs(Context& _c, Voice::Block& _out)
		{
			renderTwo(_c, _out, 0x7fffff);
		}

		void renderMix(Context& _c, Voice::Block& _out)
		{
			renderTwo(_c, _out, lim(acc(0x7fffff) - acc(_c.y[0xb])));
		}

		// E12-BD's ($103945), its level kept 4 bits down
		void renderBd(Context& _c, Voice::Block& _out)
		{
			auto& y = _c.y;
			auto& iy = _c.shared.scratch.y;

			y[0x21] = y[5];
			y[0x22] = lim(acc(y[0x22]) + acc(1));
			positions(_c, hi(shl(acc(y[0x15]), 4)), 0);
			retrigger(_c, true);
			loadWindow(_c, y[0x12], _c.shared.scratch.x.data());

			uint32_t level = y[0x15], attack = y[0x20];
			for(uint32_t k = 0; k < 32; ++k)
			{
				attack = hi(mul(attack, y[0x21]));
				const int64_t a = mul(level, y[3]);
				iy[k] = hi(shl(mul(level, attack), 1) + a);
				level = hi(a);
			}
			y[0x15] = level;
			y[0x20] = attack;

			interpolate(_c, [&](const uint32_t _k, const uint32_t _w) { _out[_k] = sx(lim(shl(mul(_w, iy[_k]), 4))); });
		}

		// The inits ($1036B6-$10373E): the filter's state, and a machine of one sample its number. $103718 for the
		// machines of two
		void initTwo(Context& _c)
		{
			auto& y = _c.y;
			y[0x17] = y[0x18] = y[0x19] = y[0x1a] = 0;
		}

		template<uint32_t Sample>
		void initOne(Context& _c)
		{
			_c.y[0xf] = Sample;
			initTwo(_c);
		}

		// What every update does: a machine of one sample takes its start and length from Y:$0F's directory entry
		void startAt(Context& _c, const uint32_t _first)
		{
			auto& y = _c.y;
			y[0x1f] = y[0x1e] = y[0x11] = 0;
			y[0x15] = 0x7fffff;
			y[0x12] = external(_c, _first);
			y[0x13] = external(_c, _first + 1);
			y[0x10] = lim(mul(y[0x13], y[6]));
			y[0xe] = y[9];
		}

		uint32_t oneEntry(const Context& _c)
		{
			return (E12Bank::Addr + 3 * _c.y[0xf]) & 0xffffff;
		}

		// $103768
		void updateOne(Context& _c)
		{
			auto& y = _c.y;
			const uint32_t first = oneEntry(_c);
			startAt(_c, first);
			y[0x14] = external(_c, first + 2);
		}

		// $103819
		void updateBd(Context& _c)
		{
			updateOne(_c);
			auto& y = _c.y;
			y[0x22] = 0;
			y[0x15] = 0x07ffff;
			y[0x20] = y[4];
		}

		// $103783-$103800: two samples named by their directory entries
		template<uint32_t First, uint32_t Second>
		void updateTwo(Context& _c)
		{
			auto& y = _c.y;
			startAt(_c, First);
			y[0x24] = external(_c, Second);
			y[0x23] = external(_c, Second + 1);
		}

		// E12-BD to E12-BC, by type from 49
		constexpr Routines kRoutines[16] = {
			{initOne<7>, updateBd, renderBd},							// E12-BD
			{initTwo, updateTwo<0x103d9f, 0x103da8>, renderMix},		// E12-SD
			{initOne<0x11>, updateOne, renderOne},						// E12-HT
			{initOne<0x12>, updateOne, renderOne},						// E12-LT
			{initOne<1>, updateOne, renderOne},							// E12-CP
			{initTwo, updateTwo<0x103d9f, 0x103d9c>, renderRs},			// E12-RS
			{initOne<2>, updateOne, renderOne},							// E12-CB
			{initOne<4>, updateOne, renderOne},							// E12-CH
			{initOne<8>, updateOne, renderOne},							// E12-OH
			{initTwo, updateTwo<0x103d96, 0x103d99>, renderMix},		// E12-RC
			{initOne<3>, updateOne, renderOne},							// E12-CC
			{initTwo, updateTwo<0x103db7, 0x103d7b>, renderMix},		// E12-BR
			{initOne<0x10>, updateOne, renderOne},						// E12-TA
			{initOne<0x13>, updateOne, renderOne},						// E12-TR
			{initTwo, updateTwo<0x103da5, 0x103da2>, renderMix},		// E12-SH
			{initTwo, updateTwo<0x103d8d, 0x103d8a>, renderMix},		// E12-BC
		};
	}

	const Routines* routines(const uint32_t _type)
	{
		return _type - 49 < std::size(kRoutines) ? &kRoutines[_type - 49] : nullptr;
	}
}
