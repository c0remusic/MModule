// syntakt::fast::SyToy, SY TOY in float on 16 Tracks, measured against the exact integer port (stEngine) and against
// itself at 44.1 kHz, by the criteria BD MODERN's test set (stFastBdModernTest, stFastBench.h):
//	Q3	unit 4's sine feeding back on its own sample (0x400035de), which the fast machine runs in the machine's
//		integers, unit 2's sine phase-modulated (0x4000290e) and the drive's clip against the integer stages fed the
//		same input, over 1 s: SNR >= 70 dB and >= 60 dB in every window of 480 samples above -60 dB, no residual third
//		octave above -60 dB. SY TOY has no filter: no Q2
//	Q4	the whole voice, 16 Hits over SY TOY's parameters (2 s each), against syntakt::syToy at 48 kHz, by bench::q4's
//		measures where not chaotic: Hilbert envelope within -39 dB of the peak, short-time third octaves within 1 dB,
//		pitch within 1 cent, fatal; the null and the 1 ms envelope printed. The same Hits with unit 4's feedback table
//		at 0 in both engines: the same measures, fatal; the null printed
//	Q15	the same Hits at 44.1 kHz, rendered at 48 and resampled (Host), against 48 kHz (bench::q15): envelope below
//		18 kHz within 0.2 dB, every third octave below 10 kHz within 0.5 dB, nothing excused
//	cost: microseconds per 32-sample block of 16 voices playing, fast against exact, and at 44.1 kHz
// The voice is checked word for word block by block (bench::blockByBlock) and as its parameters change while it
// sounds (changesWhileSounding). A Hit is chaotic where the exact port, its phases 1 LSB on, fails Q4's measures
// against itself, and is then judged by its distributions within the calibrated factor times the exact port's own
// dispersion + 0.5 dB (bench::calibrate). The firmware's words a stage runs with are read from the exact port's voice
// after its update, never written here.
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
#include "stFastSyToy.h"
#include "stMemoryMap.h"
#include "stSection7.h"

namespace
{
	using namespace syntakt;
	using fast::Lanes;
	using namespace fast::bench;

	constexpr int Machine = 36;	// SY TOY: machine 36, the exact port's engine 8

	// The 16 Hits: TUNE, the ratios, the modulation and feedback, the levels in the mix, the units' decays and level at
	// rest, the drive, HOLD, DECAY. The ratios on each of their stretches (v = value / 127 below 1/2: 50 and 63, unit 4
	// on its parabola at 50 and on unit 2's line at 51; from 1/2 to 3/4: 64, 70 and 77, unit 4 on unit 2's line at 70
	// and on its parabola at 77; from 3/4: 96 and up), every parameter at 0 and at 127, at 127 with fines past the
	// tables' last entry (Hit 2), between entries (Hit 10). The retrigger (106.7 ms) sets the ratios, the modulation,
	// the levels and the drive to 127 and the note 2 up, in the block where the voice still sounds, fading (unit 4's
	// feedback, its last sample and unit 2's modulation then go on from where they were); the gate-off is at 213.3 ms.
	// The drive by its fine alone (Hit 6). The extremes: the pitch clamped at 127 (Hit 12) and at 0 (Hit 13)
	const HitSet Hits = {{
		{{64, 64, 64, 64, 64, 0, 0, 64}, {}, 48, false, false},
		{{64, 0, 0, 0, 0, 0, 0, 0}, {}, 48, false, false},
		{{64, 127, 127, 127, 127, 1, 0, 127}, {0, 200, 77, 128, 255, 0, 0, 0}, 48, false, false},
		{{76, 32, 96, 20, 100, 0, 0, 110}, {}, 40, false, false},
		{{52, 96, 32, 110, 10, 1, 0, 70}, {}, 52, false, false},
		{{64, 51, 127, 64, 64, 1, 0, 127}, {}, 36, false, false},
		{{64, 50, 0, 127, 0, 0, 0, 100}, {0, 0, 0, 0, 0, 1, 0, 0}, 45, false, false},
		{{100, 77, 110, 30, 120, 0, 0, 80}, {}, 30, false, false},
		{{64, 70, 64, 64, 64, 0, 1, 90}, {}, 48, false, true},
		{{30, 110, 30, 88, 40, 1, 0, 60}, {}, 55, false, false},
		{{64, 70, 50, 50, 70, 0, 0, 96}, {128, 77, 200, 150, 33, 0, 0, 0}, 48, false, false},
		{{64, 63, 64, 65, 64, 1, 0, 100}, {}, 48, true, false},
		{{127, 40, 16, 40, 40, 0, 0, 80}, {}, 72, false, false},
		{{0, 90, 95, 127, 88, 1, 0, 85}, {}, 10, false, false},
		{{64, 120, 15, 80, 60, 1, 1, 120}, {}, 42, true, true},
		{{64, 64, 100, 0, 64, 0, 0, 30}, {}, 48, true, false},
	}, {1, 2, 3, 5}, Machine};

	constexpr double Seconds = 2;
	constexpr uint32_t FeedbackTable = 0x4003d238, DepthTable = 0x4003d038;

	// The voice's words the fast machine computes in integers (SyToy::testWords): the units' increments and phases,
	// units 1 to 4's envelope levels, unit 4's last sample, feedback and feedback word, unit 2's modulation, the units'
	// levels in the mix, the amp's stage and its release latched
	constexpr uint32_t Fields[] = {0x78, 0xf8, 0x178, 0x1f8, 0x278, 0x6c, 0xec, 0x16c, 0x1ec, 0x26c, 0xfc, 0x17c, 0x1fc,
		0x27c, 0x28, 0x30, 0x2c, 0x18c, 0x08, 0x0c, 0x10, 0x14, 0x3dc, 0x3e4};

	std::vector<std::vector<double>> renderFast(const double _fs, const double _seconds, const bool _noFeedback = false,
		const bool _fromTrig = false)
	{
		return fastHits<fast::SyToy>(_fs, _seconds, Hits, [&](fast::SyToy& _e)
		{
			if(_noFeedback)
				_e.testWithoutChaos();
		}, _fromTrig);
	}

	std::vector<std::vector<double>> renderExact(const std::vector<int32_t>& _s7, const double _seconds,
		const Nudge& _nudge = {})
	{
		return exactHits(_s7, _seconds, _nudge, Hits);
	}

	Voice updated(Stage& _st, const int _l, const int32_t _pitch, const std::vector<std::pair<int, uint8_t>>& _bytes)
	{
		return updatedVoice(_st, _l, _pitch, _bytes, syntakt::syToy::update);
	}

	// ---- Q3: the non-linear stages against their integer originals ----

	void q3(const std::vector<int32_t>& _s7)
	{
		Stage st(_s7);
		const Rom& rom = st.rom;
		constexpr int Blocks = 1500;	// 1 s
		bool ok = true, chaoticOk = true;

		// Unit 4 (0x400035de), the feedback from SY TOY's table across its range (bench::q3FeedbackSine)
		q3FeedbackSine(st, FeedbackTable, "FEEDBACK", ok, chaoticOk);
		check(chaoticOk, "Q3: the chaotic feedback sine's distributions within the calibrated factor x the exact "
			"port's dispersion + 0.5 dB");

		// Unit 2 (0x4000290e): the RAM sine modulated by a decaying sine at full scale, as unit 4 under its envelope
		// feeds it, at a depth ramped from block to block, from the table across its range (lane l at entry 8 l + 7,
		// then at 127 - 8 l from 0.5 s)
		{
			Runs r;
			alignas(16) uint32_t phase[Lanes] = {};
			alignas(16) float from[Lanes] = {}, to[Lanes];
			std::vector<float> sine(258);
			fast::sineTable(sine.data());
			fast::Increments inc;
			for(int b = 0; b < Blocks; ++b)
			{
				fast::Block in, out;
				for(int l = 0; l < Lanes; ++l)
				{
					Voice v = st.voice(l);
					if(b == 0)
					{
						v.unit(2, Voice::Phase) = 0;
						v.unit(2, 0x9c) = static_cast<int32_t>(bufferA(4));
						v.unit(2, 0xa0) = static_cast<int32_t>(bufferA(0));
						v.unit(2, 0x90) = v.unit(2, 0x94) = v.unit(2, 0x98) = 0;
						v.unit(2, 0xb0) = static_cast<int32_t>(Sine);
					}
					const int32_t d = frac(0x01000000, rom.entry(DepthTable, b < 750 ? 8 * l + 7 : 127 - 8 * l));
					v.unit(2, 0x8c) = d;
					to[l] = static_cast<float>(d / 16777216.0);
					v.unit(2, 0x78) = sweptIncrement(l, b, 1);
					for(auto& s : inc.s)
						s[l] = static_cast<uint32_t>(v.unit(2, 0x78)) << 8;
					for(uint32_t n = 0; n <= BlockSize; ++n)
					{
						const double t = (b * BlockSize + n) / 48000.0;
						const double m = 0.95 * std::exp(-t / (0.03 + 0.03 * l)) * std::sin(2 * Pi * (210 + 87 * l) * t);
						v.sram.at(bufferA(4) + 4 * n) = toQ31(m);
						if(n < BlockSize)
							in.s[n][l] = static_cast<float>(q31(v.sram.at(bufferA(4) + 4 * n)));
					}
					syntakt::pmOscillator(2, v, rom);
					for(uint32_t n = 0; n < BlockSize; ++n)
						r.ref[l].push_back(q31(v.sram.at(bufferB(2) + 4 * n)));
				}
				fast::pmWave(out, &in, from, to, phase, inc, sine.data());
				std::copy_n(to, Lanes, from);
				for(int l = 0; l < Lanes; ++l)
					for(int n = 0; n < BlockSize; ++n)
						r.test[l].push_back(out.s[n][l]);
			}
			ok &= r.print("PM sine (unit 2)");
		}

		// The drive (0x400047ea) with SY TOY's words, as its update sets them at a trig with the drive on and off, on
		// lanes 0-7 and 8-15 (bench::q3DriveClip)
		const auto side = [&](const bool _on)
		{
			const Voice v = updated(st, 0, 48 << 16, {{12, static_cast<uint8_t>(_on)}});
			return DriveWords::Side{v.at(0x414), v.at(0x41c), v.at(0x420), v.at(0x428), v.at(Voice::DriveFloor),
				v.at(Voice::DrivePreGain), v.at(Voice::DriveGain)};
		};
		ok &= q3DriveClip(st, {side(true), side(false)}, "drive clip (on, off)");
		checkValidated(ok, "Q3: the non-linear stages, SNR >= 70 dB, >= 60 dB in every window above -60 dB, no "
			"residual third octave above -60 dB");
	}

	// ---- the voice changed while it sounds ----

	// Every Track sounding 1 s from a trig while one of its parameters changes every 8 blocks from block 8, cycling
	// through its values: Track t and t + 8 parameter t, the second with a fine of 37 t on all but the switches. TUNE to
	// 0 and 127 at notes that clamp the pitch, the ratios on every stretch and past unit 4's corners, the modulation and
	// feedback, the levels, the units' decays, DECAY; the drive (Tracks 5 and 13, 13 on by its fine alone at some
	// retriggers) and HOLD (6 and 14) with a retrigger every third change, HOLD's Tracks also with a retrigger and a
	// gate-off in one block, the gate-off lost in the trig's pending block, and a gate-off alone after a retrigger
	// held, which releases the hold (counted, fatal if never). The words (Fields) the exact port's every block, fatal;
	// every Track by Q4's fixed measures, fatal
	void changesWhileSounding(const std::vector<int32_t>& _s7)
	{
		const std::vector<int> values[2][8] = {
			{{127, 0, 76, 40, 100, 64}, {0, 50, 51, 63, 64, 76, 77, 95, 96, 127}, {127, 0, 64, 110, 30},
				{0, 127, 40, 90}, {0, 127, 64, 20, 100}, {1, 0, 1, 0}, {1, 0, 1, 0, 0, 1}, {20, 127, 35, 90, 64}},
			{{0, 127, 64, 30, 90}, {127, 96, 70, 20, 64, 5}, {64, 127, 0, 90}, {127, 0, 64}, {127, 0, 40, 90},
				{0, 1, 0, 0}, {0, 1, 1, 0, 1, 1}, {127, 0, 50, 100}},
		};
		const int notes[2][8] = {{70, 48, 40, 52, 60, 44, 48, 36}, {10, 72, 55, 30, 48, 64, 50, 42}};
		Exact16 exact(_s7);
		auto machine = std::make_unique<fast::SyToy>();
		std::vector<std::vector<double>> ref(Lanes), test(Lanes);
		int64_t words = 0, differ = 0;
		int released = 0;	// HOLD's Tracks, the exact port's amp from its hold (stage 2) to its decay (1) by a gate-off
		for(int t = 0; t < Lanes; ++t)
			exact.setMachine(t, Machine);
		for(int b = 0; b <= 48000 / BlockSize; ++b)
		{
			const auto both = [&](auto&& _f) { _f(exact); _f(*machine); };
			for(int t = 0; t < Lanes; ++t)
			{
				const int p = t & 7, half = t >> 3;
				const auto fine = static_cast<uint8_t>(half && p != 5 && p != 6 ? 37 * t : 0);
				if(b == 0)
				{
					const uint8_t v[8] = {64, static_cast<uint8_t>(8 * t), static_cast<uint8_t>(127 - 8 * t), 64,
						static_cast<uint8_t>(16 + 6 * t), static_cast<uint8_t>(t % 2), 0, 100};
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
				const auto f = static_cast<uint8_t>(p == 5 && half ? s % 4 == 2 : fine);	// Track 13: on by its fine
				both([&](auto& _e)
				{
					_e.setParameter(t, p, value, f);
					if((p == 5 || p == 6) && s % 3 == 2)
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
					machine->testWords(t, w);
					for(size_t k = 0; k < std::size(Fields); ++k, ++words)
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
			static_cast<long long>(words), static_cast<long long>(differ), released);
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

	q3(s7);
	// Block by block (bench::blockByBlock), the words the machine computes in integers (Fields) the exact port's: unit 4
	// feeds back in the machine's integers, so its chaos is the exact port's own
	blockByBlock<fast::SyToy>(s7, Hits, "SY TOY", [](fast::SyToy&) {}, {std::begin(Fields), std::end(Fields)},
		[](const fast::SyToy& _m, const int _t, int32_t* _w) { _m.testWords(_t, _w); });
	changesWhileSounding(s7);
	const Exacts exact([&](const Nudge& _p) { return renderExact(s7, Seconds, _p); }, fast::SyToy::Units);
	q4(exact, renderFast(48000, Seconds));
	deterministic(renderExact(withoutTable(s7, FeedbackTable), Seconds), renderFast(48000, Seconds, true));
	q15(renderFast(48000, Seconds + 0.05, false, true), renderFast(44100, Seconds + 0.05, false, true), Seconds);
	printCost<fast::SyToy>("SY TOY", s7, Hits);

	std::printf(g_failures ? "%d FAILED\n" : "all passed\n", g_failures);
	return g_failures ? 1 : 0;
}
