// syntakt::fast::CpVintage, CP VINTAGE in float on 16 Tracks, measured against the exact integer port (stEngine) and
// against itself at 44.1 kHz, by the criteria BD MODERN's test set (stFastBdModernTest, stFastBench.h):
//	Q2	the linear stages (the two-way mix, the noise's low-pass, the clap's last high-pass, the rate reducer) and the
//		two noises against the integer stages fed the same input: null residual RMS <= -100 dBFS and max <= -90 dBFS;
//		the resonator's coefficient words, which the machine computes in integers, equal the exact port's at every
//		note, TUNE and fine tried (fatal)
//	Q3	the non-linear stages (unit 0's phase-modulated sine, the drive's clip) against the integer stages fed the
//		same input, over 1 s: SNR >= 70 dB and >= 60 dB in every 10 ms window above -60 dB, no residual third octave
//		above -60 dB; unit 1's sine feeding back under its envelope, which the fast machine runs in the machine's
//		integers, equal to the exact port's sample for sample (fatal)
//	Q4	the whole voice, 16 Hits over CP VINTAGE's parameters (2 s each), against syntakt::cpVintage at 48 kHz, by
//		bench::q4's measures where not chaotic: Hilbert envelope within -39 dB of the peak, short-time third octaves
//		within 1 dB, pitch within 1 cent, fatal; the null and the 1 ms envelope printed. The fast engine's noise starts
//		from the exact port's seeds (bench::exactNoiseSeeds). The same Hits with unit 1's feedback table at 0 in both
//		engines: the same measures, fatal; the null printed
//	Q15	the same Hits at 44.1 kHz, rendered at 48 and resampled (Host), against 48 kHz (bench::q15): envelope below
//		18 kHz within 0.2 dB, every third octave below 10 kHz within 0.5 dB, nothing excused
//	cost: microseconds per 32-sample block of 16 voices playing, fast against exact, and at 44.1 kHz
// A Hit is chaotic where the exact port, its phases 1 LSB on, fails Q4's measures against itself, and is then judged by
// its distributions within the calibrated factor times the exact port's own dispersion + 0.5 dB (bench::calibrate).
// The firmware's words a stage runs with are read from the exact port's voice after its update, never written here.
//
// SYNTAKT_SECTION7 names the user's OS 1.41 section 7. Exit 77 without it.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <utility>
#include <vector>

#include "stEmac.h"
#include "stEngine.h"
#include "stEngines.h"
#include "stFastBench.h"
#include "stFastCpVintage.h"
#include "stFastTables.h"
#include "stMemoryMap.h"
#include "stSection7.h"
#include "stStages.h"

namespace
{
	using namespace syntakt;
	using fast::Lanes;
	using namespace fast::bench;

	constexpr int Machine = 7;	// CP VINTAGE's id

	// The 16 Hits: TUNE, the tone, the balance (clap at 0, tone at 127), the period and the rate reducer, the tone's
	// decay, the drive, HOLD, DECAY. The retrigger (106.7 ms) sets the period and the drive to 127 and the note 2 up;
	// the gate-off is at 213.3 ms. The knots: 64 (the balance's, the period's, the rate reducer's, DECAY's), the tone's
	// feedback held from 126, the high-pass's at notes 22 and 96 (Hits 9 and 13) and its top entry 127 (Hit 12, the
	// pitch clamped there)
	const HitSet Hits = {{
		{{64, 64, 64, 64, 64, 0, 0, 64}, {}, 48, false, false},
		{{64, 0, 0, 0, 0, 0, 0, 0}, {}, 48, false, false},
		{{64, 127, 127, 127, 127, 1, 0, 127}, {}, 48, false, false},
		{{76, 32, 96, 20, 100, 0, 0, 110}, {}, 40, false, false},
		{{52, 96, 32, 110, 10, 1, 0, 70}, {}, 52, false, false},
		{{64, 10, 127, 64, 64, 1, 0, 127}, {}, 36, false, false},
		{{64, 126, 0, 127, 0, 0, 0, 100}, {}, 45, false, false},
		{{100, 50, 110, 30, 120, 0, 0, 80}, {}, 30, false, false},
		{{64, 64, 64, 64, 64, 0, 1, 90}, {}, 48, false, true},
		{{30, 110, 30, 90, 40, 1, 0, 60}, {}, 55, false, false},
		{{64, 70, 50, 50, 70, 0, 0, 96}, {128, 77, 200, 0, 33, 0, 0, 0}, 48, false, false},
		{{64, 64, 64, 65, 64, 1, 0, 100}, {}, 48, true, false},
		{{127, 40, 16, 40, 40, 0, 0, 80}, {}, 72, false, false},
		{{0, 90, 95, 127, 90, 1, 0, 85}, {}, 72, false, false},
		{{64, 120, 15, 80, 60, 1, 1, 120}, {}, 42, true, true},
		{{64, 64, 100, 0, 64, 0, 0, 30}, {}, 48, true, false},
	}, {3, 5}, Machine};

	constexpr double Seconds = 2;
	constexpr uint32_t FeedbackTable = 0x4003b438, DepthTable = 0x4003b238;

	// _seconds of the 16 Hits from the fast engine at _fs (bench::fastHits, which seeds its noise as the exact
	// port's), unit 1's feedback at 0 (_noFeedback)
	std::vector<std::vector<double>> renderFast(const double _fs, const double _seconds, const bool _noFeedback = false,
		const bool _fromTrig = false)
	{
		return fastHits<fast::CpVintage>(_fs, _seconds, Hits, [&](fast::CpVintage& _e)
		{
			if(_noFeedback)
				_e.testWithoutChaos();
		}, _fromTrig);
	}

	// The same from the exact port at 48 kHz, every Track on CP VINTAGE
	std::vector<std::vector<double>> renderExact(const std::vector<int32_t>& _s7, const double _seconds,
		const Nudge& _nudge = {})
	{
		return exactHits(_s7, _seconds, _nudge, Hits);
	}

	// Voice _l of _st after the exact port's CP VINTAGE update at a trig (bench::updatedVoice)
	Voice updated(Stage& _st, const int _l, const int32_t _pitch, const std::vector<std::pair<int, uint8_t>>& _bytes)
	{
		return updatedVoice(_st, _l, _pitch, _bytes, syntakt::cpVintage::update);
	}

	// ---- Q2: the linear stages against their integer originals ----

	void q2(const std::vector<int32_t>& _s7)
	{
		Stage st(_s7);
		std::mt19937 rng(1234);
		std::uniform_real_distribution<double> u(-1, 1);
		constexpr int Blocks = 500;
		const auto noise = [&](const double _a) { return toQ31(_a * u(rng)); };
		const auto q = [](const int32_t _v) { return static_cast<float>(q31(_v)); };
		bool ok = true;

		// The two-way mix (0x400049f4), each gain ramped from the block before's to a new one
		{
			Residual r;
			alignas(16) float from[2][Lanes] = {}, to[2][Lanes];
			fast::Block x[2], out;
			const uint32_t Out = 0x80009c58;
			for(int b = 0; b < Blocks; ++b)
			{
				int32_t ref[Lanes][BlockSize];
				for(int l = 0; l < Lanes; ++l)
				{
					Voice v = st.voice(l);
					for(uint32_t i = 0; i < 2; ++i)
					{
						const int32_t g = noise(0.5);
						v.at(0x18 + 4 * i) = toQ31(from[i][l]);
						v.at(0x08 + 4 * i) = g;
						to[i][l] = q(g);
						for(int n = 0; n < BlockSize; ++n)
						{
							v.sram.at(bufferA(i) + 4 * n) = noise(0.95);
							x[i].s[n][l] = q(v.sram.at(bufferA(i) + 4 * n));
						}
					}
					mix(v.sram.ptr(bufferA(0)), v.sram.ptr(bufferA(1)), v.sram.ptr(Out), 0, 1, v);
					std::copy_n(v.sram.ptr(Out), BlockSize, ref[l]);
				}
				fast::mix(x[0], x[1], out, from[0], to[0], from[1], to[1]);
				for(auto i = 0; i < 2; ++i)
					std::copy_n(to[i], Lanes, from[i]);
				for(int l = 0; l < Lanes; ++l)
					for(int n = 0; n < BlockSize; ++n)
						r.add(q31(ref[l][n]), out.s[n][l]);
			}
			ok &= r.print("mix");
		}

		// The noise's low-pass and the clap's last high-pass (0x40004e6c) at the update's words, noise at 0.45
		const Voice w = updated(st, 0, 48 << 16, {});
		const auto onePoleRun = [&](const char* _name, const uint32_t _field, const bool _lowPass)
		{
			Residual r;
			int32_t state[Lanes][8] = {};
			for(int l = 0; l < Lanes; ++l)
				for(uint32_t k = 0; k < 3; ++k)
					state[l][k] = w.at(_field + 4 * k);
			alignas(16) float b[Lanes], a[Lanes], x1[Lanes] = {}, y1[Lanes] = {};
			for(int l = 0; l < Lanes; ++l)
			{
				b[l] = q(state[l][0]);
				a[l] = -q(state[l][2]);
			}
			fast::Block x;
			for(int blk = 0; blk < Blocks; ++blk)
			{
				int32_t ref[Lanes][BlockSize + 1];
				for(int l = 0; l < Lanes; ++l)
				{
					for(int n = 0; n <= BlockSize; ++n)
						ref[l][n] = noise(0.45);
					for(int n = 0; n < BlockSize; ++n)
						x.s[n][l] = q(ref[l][n]);
					onePole(ref[l], state[l]);
				}
				if(_lowPass)
					fast::onePoleLanes(x, b, a, x1, y1);
				else
					fast::onePole(x, fast::OnePole{b[0], q(state[0][1]), a[0]}, x1, y1);
				for(int l = 0; l < Lanes; ++l)
					for(int n = 0; n < BlockSize; ++n)
						r.add(q31(ref[l][n]), x.s[n][l]);
			}
			ok &= r.print(_name);
		};
		onePoleRun("noise low-pass 150 Hz", 0x49c, true);
		onePoleRun("high-pass 300 Hz", 0x4bc, false);

		// The rate reducer (0x4000569a) at its table's words, lane l at entry 64 + 4 l, then at 127 - 4 l from 1/3 s:
		// noise at 0.9
		{
			Residual r;
			const auto table = fast::tables::cpVintage(fast::tables::CpVintage::SampleRate);
			int32_t c[Lanes][5] = {};
			alignas(16) int32_t rate[Lanes], count[2][Lanes] = {};
			alignas(16) float held[2][Lanes] = {};
			fast::Block x;
			for(int b = 0; b < Blocks; ++b)
			{
				int32_t ref[Lanes][BlockSize];
				for(int l = 0; l < Lanes; ++l)
				{
					c[l][4] = rate[l] = table[static_cast<size_t>(b < 500 / 3 ? 64 + 4 * l : 127 - 4 * l)];
					for(int n = 0; n < BlockSize; ++n)
						x.s[n][l] = q(ref[l][n] = noise(0.9));
					reduceRate(ref[l], c[l]);
				}
				fast::reduceRate(x, rate, held, count);
				for(int l = 0; l < Lanes; ++l)
					for(int n = 0; n < BlockSize; ++n)
						r.add(q31(ref[l][n]), x.s[n][l]);
			}
			ok &= r.print("rate reducer");
		}

		// The two noises (0x400044e2, 0x40003eb4), the generator in integers as the machine's: lane l's seeds from the
		// exact port, the white noise going on with the crackle's last two words, as CP VINTAGE draws it
		{
			Residual gated, white;
			const auto seeds = exactNoiseSeeds(_s7);
			int32_t s[Lanes][5];
			alignas(16) uint32_t words[4][Lanes];
			for(int l = 0; l < Lanes; ++l)
			{
				const int32_t* drawn = seeds[static_cast<size_t>(l)].data();
				for(int k = 0; k < 4; ++k)
					words[k][l] = static_cast<uint32_t>(s[l][k] = drawn[2 + k]);	// the noise's words, the last four
				s[l][4] = 0x2a3d70a4;
			}
			fast::Block outGated, outWhite;
			for(int b = 0; b < Blocks; ++b)
			{
				int32_t refGated[Lanes][BlockSize], refWhite[Lanes][BlockSize];
				for(int l = 0; l < Lanes; ++l)
				{
					gatedNoise(refGated[l], s[l]);
					drawRandom(refWhite[l], BlockSize, &s[l][2]);
				}
				fast::gatedNoise(outGated, words, 0x2a3d70a4);
				fast::whiteNoise(outWhite, words[2], words[3]);
				for(int l = 0; l < Lanes; ++l)
					for(int n = 0; n < BlockSize; ++n)
					{
						gated.add(q31(refGated[l][n]), outGated.s[n][l]);
						white.add(q31(refWhite[l][n]), outWhite.s[n][l]);
					}
			}
			ok &= gated.print("gated noise");
			ok &= white.print("white noise");
		}
		check(ok, "Q2: the linear stages null to <= -100 dBFS RMS, <= -90 dBFS max");

		// The resonator's coefficient words (0x400052f8, the voice's 0x520-0x53c) at every note, at TUNE 64 and three
		// others with fines, as the fast machine computes them and as the exact port's update does
		{
			const fast::CpVintage machine;
			int words = 0, differ = 0;
			const std::pair<uint8_t, uint8_t> tunes[] = {{64, 0}, {64, 128}, {100, 37}, {20, 250}, {127, 255}};
			for(int note = 0; note < 128; ++note)
				for(const auto& [value, fine] : tunes)
				{
					const Voice v = updated(st, 0, note << 16, {{2, value}, {3, fine}});
					int32_t c[8];
					machine.resonatorWords(note, value << 8 | fine, c);
					for(uint32_t k = 0; k < 8; ++k, ++words)
						differ += c[k] != v.at(0x520 + 4 * k);
				}
			std::printf("  Q2 resonator coefficients     %d words, %d differ\n", words, differ);
			check(differ == 0, "Q2: the resonator's coefficient words are the exact port's at every note and TUNE "
				"tried");
		}
	}

	// ---- Q3: the non-linear stages against their integer originals ----

	void q3(const std::vector<int32_t>& _s7)
	{
		Stage st(_s7);
		const Rom& rom = st.rom;
		constexpr int Blocks = 1500;	// 1 s
		bool ok = true;
		std::vector<float> sine(258);
		fast::sineTable(sine.data());

		// Unit 1 (0x40003958), which the fast machine runs in the machine's integers (scaledFeedbackSine):
		// the RAM sine feeding back on its own output under an envelope that decays from full (time constant 0.1 to
		// 1.2 s across the lanes), the feedback from the table across its range (lane l at entry 8 l + 7, then at
		// 127 - 8 l from 0.5 s), up to a cycle per unit, where it is chaotic: every sample equal, fatal
		{
			alignas(16) uint32_t phase[Lanes] = {};
			int32_t fb[Lanes] = {}, fbWord[Lanes], last[Lanes] = {}, from[Lanes], to[Lanes];
			std::fill_n(from, Lanes, INT32_MIN);
			fast::Increments inc;
			int64_t samples = 0, differ = 0;
			for(int b = 0; b < Blocks; ++b)
			{
				fast::Block scaled;
				int32_t ref[Lanes][BlockSize];
				for(int l = 0; l < Lanes; ++l)
				{
					Voice v = st.voice(l);
					if(b == 0)
					{
						v.unit(1, Voice::Phase) = 0;
						v.at(0x28) = v.at(0x30) = 0;
						v.unit(1, 0xb0) = static_cast<int32_t>(Sine);
						v.unit(1, Voice::EnvLevelPrev) = INT32_MIN;
					}
					fbWord[l] = v.at(0x2c) = rom.entry(FeedbackTable, b < 750 ? 8 * l + 7 : 127 - 8 * l);
					const double t = (b + 1) * BlockSize / 48000.0;
					to[l] = v.unit(1, Voice::EnvLevel) = toQ31(-std::exp(-t / (0.1 + 0.07 * l)));
					v.unit(1, 0x78) = sweptIncrement(l, b, 3);
					for(auto& s : inc.s)
						s[l] = static_cast<uint32_t>(v.unit(1, 0x78)) << 8;
					scaledFeedbackOscillator(1, v, rom);
					std::copy_n(v.sram.ptr(bufferA(1)), BlockSize, ref[l]);
				}
				fast::scaledFeedbackSine(scaled, phase, inc, fb, fbWord, last, from, to);
				for(int l = 0; l < Lanes; ++l)
				{
					differ += last[l] != st.voice(l).at(0x28);
					for(int n = 0; n < BlockSize; ++n, ++samples)
						differ += scaled.s[n][l] != static_cast<float>(q31(ref[l][n]));
				}
			}
			std::printf("  Q3 feedback sine (unit 1)     %lld samples, %lld differ (its last word too)\n",
				static_cast<long long>(samples), static_cast<long long>(differ));
			check(differ == 0, "Q3: unit 1's feedback sine in the machine's integers is the exact port's, every "
				"sample");
		}

		// Unit 0 (0x4000290e): the RAM sine modulated by a decaying wave, as unit 1 under its envelope feeds it, at a
		// depth ramped from block to block, from the table across its range (lane l at entry 8 l + 7, then at 127 - 8 l
		// from 0.5 s)
		{
			Runs r;
			alignas(16) uint32_t phase[Lanes] = {};
			alignas(16) float from[Lanes] = {}, to[Lanes];
			fast::Increments inc;
			for(int b = 0; b < Blocks; ++b)
			{
				fast::Block in, out;
				for(int l = 0; l < Lanes; ++l)
				{
					Voice v = st.voice(l);
					if(b == 0)
					{
						v.unit(0, Voice::Phase) = 0;
						v.unit(0, 0x9c) = static_cast<int32_t>(bufferA(3));
						v.unit(0, 0x94) = 0;
						v.unit(0, 0x90) = v.unit(0, 0x98) = 0;
						v.unit(0, 0xb0) = static_cast<int32_t>(Sine);
					}
					const int32_t d = frac(0x01000000, rom.entry(DepthTable, b < 750 ? 8 * l + 7 : 127 - 8 * l));
					v.unit(0, 0x8c) = d;
					to[l] = static_cast<float>(q31(d) * 128);
					v.unit(0, 0x78) = sweptIncrement(l, b, 2);
					for(auto& s : inc.s)
						s[l] = static_cast<uint32_t>(v.unit(0, 0x78)) << 8;
					for(int n = 0; n <= BlockSize; ++n)
					{
						const double t = (b * BlockSize + n) / 48000.0;
						const double m = 0.95 * std::exp(-t / (0.03 + 0.03 * l)) *
							std::sin(2 * Pi * (210 + 87 * l) * t);
						v.sram.at(bufferA(3) + 4 * n) = toQ31(m);
						if(n < BlockSize)
							in.s[n][l] = static_cast<float>(q31(v.sram.at(bufferA(3) + 4 * n)));
					}
					pmOscillator(0, v, rom);
					for(int n = 0; n < BlockSize; ++n)
						r.ref[l].push_back(q31(v.sram.at(bufferB(0) + 4 * n)));
				}
				fast::pmWave(out, &in, from, to, phase, inc, sine.data());
				std::copy_n(to, Lanes, from);
				for(int l = 0; l < Lanes; ++l)
					for(int n = 0; n < BlockSize; ++n)
						r.test[l].push_back(out.s[n][l]);
			}
			ok &= r.print("PM sine (unit 0)");
		}

		// The drive (0x400047ea) with CP VINTAGE's words, as its update sets them at a trig with the drive on and off,
		// on lanes 0-7 and 8-15 (bench::q3DriveClip)
		const auto side = [&](const bool _on)
		{
			const Voice v = updated(st, 0, 48 << 16, {{12, static_cast<uint8_t>(_on)}});
			return DriveWords::Side{v.at(0x414), v.at(0x41c), v.at(0x420), v.at(0x428), v.at(Voice::DriveFloor),
				v.at(Voice::DrivePreGain), v.at(Voice::DriveGain)};
		};
		ok &= q3DriveClip(st, {side(true), side(false)}, "drive clip (on, off)");
		checkValidated(ok, "Q3: the non-linear stages, SNR >= 70 dB, >= 60 dB in every 10 ms window above -60 dB, "
			"no residual third octave above -60 dB");
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
	// Block by block (bench::blockByBlock), unit 1's envelope word (0x40003c0e, the voice's 0xfc) the exact port's
	blockByBlock<fast::CpVintage>(s7, Hits, "CP VINTAGE", [](fast::CpVintage&) {},
		{0xfc}, [](const fast::CpVintage& _m, const int _t, int32_t* _w) { _w[0] = _m.testEnvelope(_t); });
	const Exacts exact([&](const Nudge& _p) { return renderExact(s7, Seconds, _p); }, fast::CpVintage::Units);
	q4(exact, renderFast(48000, Seconds));
	deterministic(renderExact(withoutTable(s7, FeedbackTable), Seconds), renderFast(48000, Seconds, true));
	q15(renderFast(48000, Seconds + 0.05, false, true), renderFast(44100, Seconds + 0.05, false, true),
		Seconds);
	printCost<fast::CpVintage>("CP VINTAGE", s7, Hits);

	std::printf(g_failures ? "%d FAILED\n" : "all passed\n", g_failures);
	return g_failures ? 1 : 0;
}
