// syntakt::fast::SyBits, SY BITS in float on 16 Tracks, measured against the exact integer port (stEngine) and against
// itself at 44.1 kHz, by the criteria BD MODERN's test set (stFastBdModernTest, stFastBench.h):
//	Q2	the gated noise (0x400044e2) at SY BITS's gate, the generator in the machine's integers, against the integer
//		stage fed the same seeds: null to <= -100 dBFS RMS, <= -90 dBFS max
//	Q3	the crossfading oscillator (0x400061a2), the offset oscillator (0x40006346) and the bit reducer (0x40005f9a)
//		against the integer stages fed the same input, over 1 s: SNR >= 70 dB and >= 60 dB in every window of 480
//		samples above -60 dB, no residual third octave above -60 dB
//	Q4	the whole voice, 16 Hits over SY BITS's parameters (2 s each), against syntakt::syBits at 48 kHz, by bench::q4's
//		measures where not chaotic: Hilbert envelope within -39 dB of the peak, short-time third octaves within 1 dB,
//		pitch within 1 cent, fatal; the null and the 1 ms envelope printed. SY BITS has no feedback: no render without
//		it
//	Q15	the same Hits at 44.1 kHz, rendered at 48 and resampled (Host), against 48 kHz (bench::q15)
//	cost: microseconds per 32-sample block of 16 voices playing, fast against exact, and at 44.1 kHz
// The voice is checked word for word block by block (bench::blockByBlock) and as its parameters change while it
// sounds (changesWhileSounding). The noise starts from the exact port's seeds. The firmware's words a stage runs with
// are read from the exact port's voice after its update, never written here.
//
// SYNTAKT_SECTION7 names the user's OS 1.41 section 7. Exit 77 without it.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <memory>
#include <utility>
#include <vector>

#include "stEmac.h"
#include "stEngine.h"
#include "stEngines.h"
#include "stFastBench.h"
#include "stFastSyBits.h"
#include "stFastTables.h"
#include "stMemoryMap.h"
#include "stSection7.h"

namespace
{
	using namespace syntakt;
	using fast::Lanes;
	using namespace fast::bench;

	constexpr int Machine = 37;	// SY BITS: machine 37, the exact port's engine 9

	// The 16 Hits: TUNE, DETUNE, the mix, the rate reducer, WAVE, BITS, HOLD, DECAY. WAVE in each region (sine to
	// triangle below 22: 0, 10, 21; triangle to saw from 22 to 42: 22, 30, 42; the offset saw from 43: 43, 50, 64,
	// 100, 127); the notes in every band of the saws (at or below 35, by 12 from 36, from 108); DETUNE at its three
	// octave words (52, 64, 76, fine 0) and beside them (76 fine 1), its sum with TUNE wrapping (Hit 12: TUNE and
	// DETUNE 127); every parameter at 0 and at 127, at 127 with fines past the tables' last entry (Hit 2), between
	// entries (Hit 10); DECAY 127, which holds (Hit 2); HOLD by its fine alone (Hit 6). The retrigger (106.7 ms) sets
	// DETUNE, the mix and WAVE to 127 and the note 2 up; the gate-off is at 213.3 ms. The extremes: the pitch clamped
	// at 127 (Hit 12) and at 0 (Hit 13)
	const HitSet Hits = {{
		{{64, 64, 64, 64, 64, 0, 0, 64}, {}, 48, false, false},
		{{64, 0, 0, 0, 0, 0, 0, 0}, {}, 48, false, false},
		{{64, 127, 127, 127, 127, 127, 0, 127}, {0, 200, 77, 128, 255, 90, 0, 0}, 48, false, false},
		{{76, 52, 96, 20, 21, 10, 0, 110}, {}, 40, false, false},
		{{52, 76, 32, 0, 22, 64, 0, 70}, {}, 60, false, false},
		{{64, 76, 64, 0, 42, 30, 0, 100}, {0, 1, 0, 0, 0, 0, 0, 0}, 72, false, false},
		{{64, 64, 63, 10, 43, 0, 0, 100}, {0, 0, 0, 0, 0, 0, 1, 0}, 84, false, true},
		{{100, 90, 64, 64, 64, 100, 0, 80}, {}, 30, false, false},
		{{64, 70, 64, 0, 127, 0, 1, 90}, {}, 96, false, true},
		{{30, 40, 110, 64, 100, 40, 0, 60}, {}, 55, false, false},
		{{64, 70, 50, 50, 70, 50, 0, 96}, {128, 77, 200, 150, 33, 99, 0, 0}, 90, false, false},
		{{64, 63, 64, 65, 50, 20, 0, 100}, {}, 100, true, false},
		{{127, 127, 40, 0, 100, 0, 0, 80}, {}, 100, false, false},
		{{0, 0, 95, 127, 30, 127, 0, 85}, {}, 10, false, false},
		{{64, 120, 15, 80, 10, 5, 1, 120}, {}, 36, true, true},
		{{64, 64, 100, 0, 0, 127, 0, 30}, {}, 115, true, false},
	}, {1, 2, 4}, Machine};

	constexpr double Seconds = 2;

	// The waves' addresses in the exact port's RAM, in SyBitsWaves' order, and the RAM sine the machine change leaves
	constexpr uint32_t WaveAddress[fast::SyBitsWaves] = {0x8000d08c, 0x8000c888, 0x80007788, 0x80006f84, 0x80006780,
		0x80005f7c, 0x80005778, 0x80004f74, 0x8000d890, 0x8000e094};

	// The voice's words (SyBits::testWords, the waves last as addresses)
	constexpr uint32_t Fields[] = {0x78, 0xf8, 0x178, 0x1f8, 0x6c, 0xec, 0x16c, 0x1ec, 0xa4, 0x1a4, 0xa8, 0xac,
		0x1a8, 0x1ac, 0xf0, 0x1f0, 0xfc, 0x1fc, 0x08, 0x0c, 0x14, 0x59c, 0x5a0, 0x578, 0x57c, 0x580, 0x584, 0x594, 0x598,
		0x3dc, 0x3e4, 0xb0, 0xb4, 0x130, 0x1b0, 0x1b4, 0x230};
	constexpr size_t WaveFields = 6;

	void words(const fast::SyBits& _m, const int _t, int32_t* _w)
	{
		_m.testWords(_t, _w);
		for(size_t k = std::size(Fields) - WaveFields; k < std::size(Fields); ++k)
			_w[k] = static_cast<int32_t>(_w[k] < 0 ? Sine : WaveAddress[_w[k]]);
	}

	std::vector<std::vector<double>> renderFast(const double _fs, const double _seconds, const bool _fromTrig = false)
	{
		return fastHits<fast::SyBits>(_fs, _seconds, Hits, [](fast::SyBits&) {}, _fromTrig);
	}

	std::vector<std::vector<double>> renderExact(const std::vector<int32_t>& _s7, const double _seconds,
		const Nudge& _nudge = {})
	{
		return exactHits(_s7, _seconds, _nudge, Hits);
	}

	Voice updated(Stage& _st, const int _l, const int32_t _pitch, const std::vector<std::pair<int, uint8_t>>& _bytes)
	{
		return updatedVoice(_st, _l, _pitch, _bytes, syntakt::syBits::update);
	}

	// ---- Q2 ----

	void q2(const std::vector<int32_t>& _s7)
	{
		Stage st(_s7);
		const int32_t gate = updated(st, 0, 48 << 16, {}).at(0x588);
		check(q2GatedNoise(_s7, gate, 500), "Q2: the linear stages null to <= -100 dBFS RMS, <= -90 dBFS max");
	}

	// ---- Q3: the non-linear stages against their integer originals ----

	constexpr uint32_t CrossfadeTable = 0x4003e438, PhaseOffsetTable = 0x4003de38, BitsTable = 0x4003e038;

	void q3(const std::vector<int32_t>& _s7)
	{
		Stage st(_s7);
		const Rom& rom = st.rom;
		constexpr int Blocks = 1500;	// 1 s
		bool ok = true;

		// The waves as the fast machine reads them
		std::vector<std::vector<float>> waves(fast::SyBitsWaves, std::vector<float>(fast::SyBitsWaveSize));
		for(int w = 0; w < fast::SyBitsWaves; ++w)
			fast::readQ31(waves[static_cast<size_t>(w)].data(), fast::tables::syBitsWave(w));
		// Lane l's entry of a table across its range: 8 l + 7, then 127 - 8 l from 0.5 s
		const auto entry = [](const int _l, const int _b) { return _b < Blocks / 2 ? 8 * _l + 7 : 127 - 8 * _l; };

		// Unit 0 (0x400061a2): lane l crossfading waves l % 10 and (l + 3) % 10, the crossfade from the table, at a
		// swept increment (twice the 24-bit one: a 25-bit phase)
		{
			Runs r;
			alignas(16) uint32_t phase[Lanes] = {}, inc[Lanes];
			alignas(16) float from[2][Lanes], to[2][Lanes];
			const float* w1[Lanes];
			const float* w2[Lanes];
			fast::Block out;
			for(int b = 0; b < Blocks; ++b)
			{
				for(int l = 0; l < Lanes; ++l)
				{
					Voice v = st.voice(l);
					if(b == 0)
					{
						v.unit(0, Voice::Phase) = 0;
						v.unit(0, 0xa8) = v.unit(0, 0xac) = 0;
						v.unit(0, 0xb0) = static_cast<int32_t>(WaveAddress[l % 10]);
						v.unit(0, 0xb4) = static_cast<int32_t>(WaveAddress[(l + 3) % 10]);
						w1[l] = waves[static_cast<size_t>(l % 10)].data();
						w2[l] = waves[static_cast<size_t>((l + 3) % 10)].data();
					}
					const int32_t fade = rom.entry(CrossfadeTable, entry(l, b));
					v.unit(0, 0xa4) = fade;
					v.unit(0, 0x78) = 2 * sweptIncrement(l, b, 1);
					inc[l] = static_cast<uint32_t>(v.unit(0, 0x78)) << 7;
					const int32_t target[2] = {subWrap(0x7f000000, fade), fade};
					for(int k = 0; k < 2; ++k)
					{
						const int32_t w = v.unit(0, 0xa8 + 4 * static_cast<uint32_t>(k));
						from[k][l] = static_cast<float>(q31(w));
						to[k][l] = static_cast<float>(q31(addWrap(w, BlockSize * (subWrap(target[k], w) >> 6))));
					}
					syBits::crossfadeOscillator(0, v);
					for(uint32_t n = 0; n < BlockSize; ++n)
						r.ref[l].push_back(q31(v.sram.at(bufferB(0) + 4 * n)));
				}
				fast::crossfadeWaves(out, phase, inc, w1, w2, from[0], to[0]);
				for(int l = 0; l < Lanes; ++l)
				{
					phase[l] += BlockSize * inc[l];
					for(int n = 0; n < BlockSize; ++n)
						r.test[l].push_back(out.s[n][l]);
				}
			}
			ok &= r.print("crossfading oscillator");
		}

		// Unit 1 (0x40006346): lane l's saw (band l % 8), its offset and level from the tables, ramped from block to
		// block
		{
			Runs r;
			alignas(16) uint32_t phase[Lanes] = {}, inc[Lanes], offset[Lanes], step[Lanes];
			alignas(16) float from[Lanes], to[Lanes];
			const float* w[Lanes];
			for(int b = 0; b < Blocks; ++b)
			{
				fast::Block out{};
				for(int l = 0; l < Lanes; ++l)
				{
					Voice v = st.voice(l);
					if(b == 0)
					{
						v.unit(1, Voice::Phase) = 0;
						v.unit(1, 0x74) = v.unit(1, Voice::EnvLevelPrev) = 0;
						v.unit(1, 0xb0) = static_cast<int32_t>(WaveAddress[l % 8]);
						w[l] = waves[static_cast<size_t>(l % 8)].data();
					}
					v.unit(1, 0x70) = frac(0x02000000, rom.entry(PhaseOffsetTable, entry(l, b)));
					v.unit(1, Voice::EnvLevel) = rom.entry(CrossfadeTable, entry(l, b));
					v.unit(1, 0x78) = 2 * sweptIncrement(l, b, 1);
					inc[l] = static_cast<uint32_t>(v.unit(1, 0x78)) << 7;
					offset[l] = static_cast<uint32_t>(v.unit(1, 0x74)) << 7;
					step[l] = static_cast<uint32_t>(rampStepSat(v.unit(1, 0x70), v.unit(1, 0x74))) << 7;
					const int32_t level = v.unit(1, Voice::EnvLevelPrev);
					from[l] = static_cast<float>(q31(level));
					to[l] = static_cast<float>(q31(addWrap(level,
						BlockSize * rampStepSat(v.unit(1, Voice::EnvLevel), level))));
					syBits::offsetOscillator(1, v);
					for(uint32_t n = 0; n < BlockSize; ++n)
						r.ref[l].push_back(q31(v.sram.at(bufferA(1) + 4 * n)));
				}
				fast::addOffsetWave(out, phase, inc, w, offset, step, from, to);
				for(int l = 0; l < Lanes; ++l)
				{
					phase[l] += BlockSize * inc[l];
					for(int n = 0; n < BlockSize; ++n)
						r.test[l].push_back(out.s[n][l]);
				}
			}
			ok &= r.print("offset oscillator");
		}

		// The bit reducer (0x40005f9a): a decaying sine at full scale, BITS from the table across its range
		{
			Runs r;
			alignas(16) int32_t word[Lanes];
			for(int b = 0; b < Blocks; ++b)
			{
				fast::Block x;
				for(int l = 0; l < Lanes; ++l)
				{
					int32_t c[6] = {}, buf[BlockSize];
					c[5] = word[l] = rom.entry(BitsTable, entry(l, b));
					for(int n = 0; n < BlockSize; ++n)
					{
						const double t = (b * BlockSize + n) / 48000.0;
						buf[n] = toQ31(0.99 * std::exp(-t / (0.1 + 0.05 * l)) * std::sin(2 * Pi * (55 + 37 * l) * t));
						x.s[n][l] = static_cast<float>(q31(buf[n]));
					}
					syBits::reduceBits(buf, c);
					for(int n = 0; n < BlockSize; ++n)
						r.ref[l].push_back(q31(buf[n]));
				}
				fast::reduceBits(x, word);
				for(int l = 0; l < Lanes; ++l)
					for(int n = 0; n < BlockSize; ++n)
						r.test[l].push_back(x.s[n][l]);
			}
			ok &= r.print("bit reducer");
		}
		check(ok, "Q3: the non-linear stages, SNR >= 70 dB, >= 60 dB in every window above -60 dB, no residual third "
			"octave above -60 dB");
	}

	// ---- the voice changed while it sounds ----

	// Every Track sounding 1 s from a trig while one of its parameters changes every 8 blocks from block 8, cycling
	// through its values: Track t and t + 8 parameter t, the second with a fine of 37 t on all but HOLD. TUNE to 0 and
	// 127 at notes that clamp the pitch, DETUNE at its three octave words and past its wrap, the mix on both halves,
	// the rate, WAVE through its three regions (the waves of units 1 and 3 kept below 22), BITS, DECAY to 127 and
	// back; HOLD (Tracks 6 and 14, 14 on by its fine alone) with a retrigger every third change, a retrigger and a
	// gate-off in one block, the gate-off lost in the trig's pending block, and a gate-off alone after a retrigger
	// held, which releases the hold (counted, fatal if never). The words (Fields) the exact port's every block, fatal;
	// every Track by Q4's fixed measures, fatal
	void changesWhileSounding(const std::vector<int32_t>& _s7)
	{
		const std::vector<int> values[2][8] = {
			{{127, 0, 76, 40, 100, 64}, {52, 64, 76, 0, 127, 30, 63}, {127, 0, 63, 64, 110, 30}, {0, 127, 40, 90},
				{0, 21, 22, 42, 43, 64, 127, 10, 30}, {0, 127, 64, 20, 100}, {1, 0, 1, 0, 0, 1}, {20, 127, 35, 90, 64}},
			{{0, 127, 64, 30, 90}, {127, 76, 52, 20, 64, 5}, {64, 127, 0, 90}, {127, 0, 64}, {127, 43, 0, 64, 22, 90},
				{127, 0, 40, 90}, {0, 0, 0, 0, 0, 0}, {127, 0, 50, 100}},
		};
		const int notes[2][8] = {{70, 48, 40, 52, 60, 44, 48, 36}, {10, 72, 55, 30, 100, 64, 50, 116}};
		Exact16 exact(_s7);
		auto machine = std::make_unique<fast::SyBits>();
		const auto seeds = exactNoiseSeeds(_s7);
		for(int t = 0; t < Lanes; ++t)
		{
			machine->testSeedNoise(t, seeds[static_cast<size_t>(t)].data());
			exact.setMachine(t, Machine);
		}
		std::vector<std::vector<double>> ref(Lanes), test(Lanes);
		int64_t count = 0, differ = 0;
		int released = 0;	// HOLD's Tracks, the exact port's amp from its hold (stage 2) to its decay (1) by a gate-off
		for(int b = 0; b <= 48000 / BlockSize; ++b)
		{
			const auto both = [&](auto&& _f) { _f(exact); _f(*machine); };
			for(int t = 0; t < Lanes; ++t)
			{
				const int p = t & 7, half = t >> 3;
				const auto fine = static_cast<uint8_t>(half && p != 6 ? 37 * t : 0);
				if(b == 0)
				{
					const uint8_t v[8] = {64, static_cast<uint8_t>(8 * t), static_cast<uint8_t>(127 - 8 * t), 0,
						static_cast<uint8_t>(16 + 7 * t), static_cast<uint8_t>(4 * t), 0, 100};
					both([&](auto& _e)
					{
						for(int i = 0; i < 8; ++i)
							_e.setParameter(t, i, v[i], i == p ? fine : 0);
						_e.setNote(t, notes[half][p]);
						_e.trig(t);
					});
					continue;
				}
				if(b % 8)
					continue;
				const int s = b / 8 - 1;
				const std::vector<int>& list = values[half][p];
				const auto value = static_cast<uint8_t>(list[static_cast<size_t>(s) % list.size()]);
				const auto f = static_cast<uint8_t>(p == 6 && half ? s % 2 == 0 : fine);	// Track 14: on by its fine
				both([&](auto& _e)
				{
					_e.setParameter(t, p, value, f);
					if(p == 6 && s % 3 == 2)
						_e.trig(t);
					if(p == 6 && s % 6 == 1)
					{
						_e.trig(t);
						_e.gateOff(t);
					}
					if(p == 6 && s % 6 == 3)
						_e.gateOff(t);
				});
			}
			bool held[2];	// held, a gate-off seen, and no trig sounding this block
			for(int h = 0; h < 2; ++h)
				held[h] = exact.field(6 + 8 * h, Voice::AmpStage) == 2 && exact.field(6 + 8 * h, Voice::AmpRelease) &&
					!exact.field(6 + 8 * h, Voice::TrigNow);
			exact.render();
			const fast::Block& out = machine->renderBlock();
			for(int h = 0; h < 2; ++h)
				released += held[h] && exact.field(6 + 8 * h, Voice::AmpStage) == 1;
			for(int t = 0; t < Lanes; ++t)
			{
				if(b >= 1)
				{
					int32_t w[std::size(Fields)];
					words(*machine, t, w);
					for(size_t k = 0; k < std::size(Fields); ++k, ++count)
						differ += w[k] != exact.field(t, Fields[k]);
				}
				for(int n = 0; n < BlockSize; ++n)
				{
					ref[t].push_back(q31(exact.out(t)[n]));
					test[t].push_back(out.s[n][t]);
				}
			}
		}
		bool ok = true;
		for(int t = 0; t < Lanes; ++t)
		{
			const std::vector<double> r(ref[t].begin() + BlockSize, ref[t].end());
			const std::vector<double> x(test[t].begin() + BlockSize, test[t].end());
			const auto m = measureHit(r, x);
			ok &= m.envAbs <= EnvelopeAbsDb && m.cells <= CellMaxDb;
			std::printf("  changed while sounding, Track %2d: envelope %.1f dB re peak, cells %.3f dB, SNR %.1f dB\n", t,
				m.envAbs, m.cells, m.snr);
		}
		std::printf("  changed while sounding: %lld words, %lld differ; HOLD released by a gate-off %d times\n",
			static_cast<long long>(count), static_cast<long long>(differ), released);
		check(released > 0, "changed while sounding: a gate-off releases a held amp");
		check(differ == 0, "the machine's words are the exact port's every block as its parameters change while it "
			"sounds");
		check(ok, "changed while sounding: every Track's Hilbert envelope within -39 dB of the peak, third octave cells "
			"within 1 dB");
	}
}

int main()
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	const auto s7 = loadSection7OrSkip();
	if(s7.empty())
		return 77;

	q2(s7);
	q3(s7);
	blockByBlock<fast::SyBits>(s7, Hits, "SY BITS", [](fast::SyBits&) {}, {std::begin(Fields), std::end(Fields)},
		words);
	changesWhileSounding(s7);
	const Exacts exact([&](const Nudge& _p) { return renderExact(s7, Seconds, _p); }, fast::SyBits::Units);
	q4(exact, renderFast(48000, Seconds));
	q15(renderFast(48000, Seconds + 0.05, true), renderFast(44100, Seconds + 0.05, true), Seconds);
	printCost<fast::SyBits>("SY BITS", s7, Hits);

	std::printf(g_failures ? "%d FAILED\n" : "all passed\n", g_failures);
	return g_failures ? 1 : 0;
}
