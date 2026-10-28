// syntakt::fast::SdVintage, SD VINTAGE in float on 16 Tracks, measured against the exact integer port (stEngine) and
// against itself at 44.1 kHz, by the criteria BD MODERN's test set (stFastBdModernTest, stFastBench.h):
//	Q2	the linear stages (the four-way mix, the noise's two low-passes and the output's high-pass) and the gated noise
//		against the integer stages fed the same input: null residual RMS <= -100 dBFS and max <= -90 dBFS
//	Q3	the non-linear stages (unit 3's wave feeding back under its envelope, unit 2's phase-modulated wave, the noise's
//		high-pass driven into its clip, the drive's clip) against the integer stages fed the same input, over 1 s: SNR
//		>= 70 dB and >= 60 dB in every 10 ms window above -60 dB, no residual third octave above -60 dB
//	Q4	the whole voice, 16 Hits over SD VINTAGE's parameters (2 s each), against syntakt::sdVintage at 48 kHz, by
//		bench::q4's measures where not chaotic: Hilbert envelope within -39 dB of the peak, short-time third octaves
//		within 1 dB, pitch within 1 cent, fatal; the null and the 1 ms envelope printed. The fast engine's noise starts
//		from the exact port's seeds (bench::exactNoiseSeeds). The same Hits with unit 3's feedback table at 0 in both
//		engines: the same measures, fatal; the null printed
//	Q15	the same Hits at 44.1 kHz, rendered at 48 and resampled (Host), against 48 kHz (bench::q15): envelope below
//		18 kHz within 0.2 dB, every third octave below 10 kHz within 0.5 dB, nothing excused
//	cost: microseconds per 32-sample block of 16 voices playing, fast against exact, and at 44.1 kHz
// A Hit is chaotic where the exact port, its phases 1 LSB on, fails Q4's measures against itself, and is then judged by
// its distributions within the calibrated factor times the exact port's own dispersion + 0.5 dB (bench::calibrate).
//
// SYNTAKT_SECTION7 names the user's OS 1.41 section 7. Exit 77 without it.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

#include "stEmac.h"
#include "stEngine.h"
#include "stEngines.h"
#include "stFastBench.h"
#include "stFastSdVintage.h"
#include "stFastTables.h"
#include "stMemoryMap.h"
#include "stSection7.h"
#include "stStages.h"

namespace
{
	using namespace syntakt;
	using fast::Lanes;
	using namespace fast::bench;

	constexpr int Machine = 6;	// SD VINTAGE's id

	// The 16 Hits: TUNE, units 1 and 3's ratios, the balance, the sweep, unit 3's decay (127: unit 3 silent), the
	// drive, HOLD, DECAY. The retrigger (106.7 ms) sets the sweep and the drive to 127 and the note 2 up; the gate-off
	// is at 213.3 ms. The balance's knots are at 1/8, 1/2 and 3/4 (16, 64, 96)
	const HitSet Hits = {{
		{{64, 64, 64, 64, 64, 0, 0, 64}, {}, 48, false, false},
		{{64, 0, 0, 0, 0, 0, 0, 0}, {}, 48, false, false},
		{{64, 127, 127, 127, 127, 1, 0, 127}, {}, 48, false, false},
		{{76, 32, 96, 20, 100, 0, 0, 110}, {}, 40, false, false},
		{{52, 96, 32, 110, 10, 1, 0, 70}, {}, 52, false, false},
		{{64, 10, 127, 64, 64, 1, 0, 127}, {}, 36, false, false},
		{{64, 127, 0, 127, 0, 0, 0, 100}, {}, 45, false, false},
		{{100, 50, 110, 30, 120, 0, 0, 80}, {}, 30, false, false},
		{{64, 64, 64, 64, 64, 0, 1, 90}, {}, 48, false, true},
		{{30, 110, 30, 90, 40, 1, 0, 60}, {}, 55, false, false},
		{{64, 70, 50, 50, 70, 0, 0, 96}, {128, 77, 200, 0, 33, 0, 0, 0}, 48, false, false},
		{{64, 64, 64, 64, 64, 1, 0, 100}, {}, 48, true, false},
		{{127, 40, 16, 40, 40, 0, 0, 80}, {}, 50, false, false},
		{{0, 90, 95, 127, 90, 1, 0, 85}, {}, 72, false, false},
		{{64, 120, 15, 80, 60, 1, 1, 120}, {}, 42, true, true},
		{{64, 64, 100, 0, 64, 0, 0, 30}, {}, 48, true, false},
	}, {3, 5}, Machine};

	constexpr double Seconds = 2;
	constexpr uint32_t FeedbackTable = 0x40039a38, Depth2Table = 0x40038c38, NoiseLpTable = 0x40039238;
	constexpr uint32_t WaveU2 = 0x8000a484, WaveU3 = 0x8000a080;	// RAM, as the boot copies them

	// _seconds of the 16 Hits from the fast engine at _fs (bench::fastHits, which seeds its noise as the exact
	// port's), unit 3's feedback at 0 (_noFeedback)
	std::vector<std::vector<double>> renderFast(const double _fs, const double _seconds, const bool _noFeedback = false,
		const bool _fromTrig = false)
	{
		return fastHits<fast::SdVintage>(_fs, _seconds, Hits, [&](fast::SdVintage& _e)
		{
			if(_noFeedback)
				_e.testWithoutChaos();
		}, _fromTrig);
	}

	// The same from the exact port at 48 kHz, every Track on SD VINTAGE
	std::vector<std::vector<double>> renderExact(const std::vector<int32_t>& _s7, const double _seconds,
		const Nudge& _nudge = {})
	{
		return exactHits(_s7, _seconds, _nudge, Hits);
	}

	// ---- Q2: the linear stages against their integer originals ----

	void q2(const std::vector<int32_t>& _s7)
	{
		Stage st(_s7);
		const Rom& rom = st.rom;
		std::mt19937 rng(1234);
		std::uniform_real_distribution<double> u(-1, 1);
		constexpr int Blocks = 500;
		const auto noise = [&](const double _a) { return toQ31(_a * u(rng)); };
		const auto q = [](const int32_t _v) { return static_cast<float>(q31(_v)); };
		bool ok = true;

		// The four-way mix (0x40004cd6), each gain ramped from the block before's to a new one
		{
			Residual r;
			alignas(16) float from[4][Lanes] = {}, to[4][Lanes];
			fast::Block x[4], out;
			const uint32_t Out = 0x80009c58;
			for(int b = 0; b < Blocks; ++b)
			{
				int32_t ref[Lanes][BlockSize];
				for(int l = 0; l < Lanes; ++l)
				{
					Voice v = st.voice(l);
					const int32_t* src[4];
					for(uint32_t i = 0; i < 4; ++i)
					{
						const int32_t g = noise(0.25);
						v.at(0x18 + 4 * i) = toQ31(from[i][l]);
						v.at(0x08 + 4 * i) = g;
						to[i][l] = q(g);
						const uint32_t a = bufferA(i);
						for(int n = 0; n < BlockSize; ++n)
						{
							v.sram.at(a + 4 * n) = noise(0.95);
							x[i].s[n][l] = q(v.sram.at(a + 4 * n));
						}
						src[i] = v.sram.ptr(a);
					}
					mix4(src, v.sram.ptr(Out), v);
					std::copy_n(v.sram.ptr(Out), BlockSize, ref[l]);
				}
				const fast::Block* in[4] = {&x[0], &x[1], &x[2], &x[3]};
				fast::mix4(in, out, from, to);
				for(auto i = 0; i < 4; ++i)
					std::copy_n(to[i], Lanes, from[i]);
				for(int l = 0; l < Lanes; ++l)
					for(int n = 0; n < BlockSize; ++n)
						r.add(q31(ref[l][n]), out.s[n][l]);
			}
			ok &= r.print("mix4");
		}

		// The noise's first low-pass (0x40004e6c) at its table's coefficients, lane l at entry 8 l + 7; its second, at
		// 4 kHz, and the output's high-pass at 150 Hz: noise at 0.45, as the machine's words set them
		const auto onePoleRun = [&](const char* _name, const auto& _coefficients)
		{
			Residual r;
			int32_t state[Lanes][8] = {};
			alignas(16) float b[Lanes], a[Lanes], c1[Lanes], x1[Lanes] = {}, y1[Lanes] = {};
			for(int l = 0; l < Lanes; ++l)
			{
				_coefficients(l, state[l]);
				b[l] = q(state[l][0]);
				c1[l] = q(state[l][1]);
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
				if(c1[0] == b[0])	// a low-pass: the fast stage with its coefficients per lane
					fast::onePoleLanes(x, b, a, x1, y1);
				else
					fast::onePole(x, fast::OnePole{b[0], c1[0], a[0]}, x1, y1);
				for(int l = 0; l < Lanes; ++l)
					for(int n = 0; n < BlockSize; ++n)
						r.add(q31(ref[l][n]), x.s[n][l]);
			}
			ok &= r.print(_name);
		};
		onePoleRun("noise low-pass (table)", [&](const int _l, int32_t* _c)
		{
			const int32_t lp = rom.entry(NoiseLpTable, 8 * _l + 7);
			_c[0] = _c[1] = addSat(0x40000000, lp >> 1);
			_c[2] = lp;
		});
		onePoleRun("noise low-pass 4 kHz", [](int, int32_t* _c)
		{
			_c[0] = _c[1] = 0x1a8ebcb4;
			_c[2] = static_cast<int32_t>(0xb51d7968);
		});
		onePoleRun("output high-pass 150 Hz", [](int, int32_t* _c)
		{
			_c[0] = 0x7ec16db1;
			_c[1] = static_cast<int32_t>(0x813e924f);
			_c[2] = static_cast<int32_t>(0x827d249e);
		});

		// The gated noise (0x400044e2), the generator in integers as the machine's: lane l's seeds from the exact port
		{
			Residual r;
			const auto seeds = exactNoiseSeeds(_s7);
			int32_t s[Lanes][5];
			alignas(16) uint32_t w[4][Lanes];
			for(int l = 0; l < Lanes; ++l)
			{
				const int32_t* drawn = seeds[static_cast<size_t>(l)].data();
				for(int k = 0; k < 4; ++k)
					w[k][l] = static_cast<uint32_t>(s[l][k] = drawn[2 + k]);	// the noise's words, the last four
				s[l][4] = 0x2a3d70a4;
			}
			fast::Block out;
			for(int b = 0; b < Blocks; ++b)
			{
				int32_t ref[Lanes][BlockSize];
				for(int l = 0; l < Lanes; ++l)
					gatedNoise(ref[l], s[l]);
				fast::gatedNoise(out, w, 0x2a3d70a4);
				for(int l = 0; l < Lanes; ++l)
					for(int n = 0; n < BlockSize; ++n)
						r.add(q31(ref[l][n]), out.s[n][l]);
			}
			ok &= r.print("gated noise");
		}
		check(ok, "Q2: the linear stages null to <= -100 dBFS RMS, <= -90 dBFS max");
	}

	// ---- Q3: the non-linear stages against their integer originals ----

	void q3(const std::vector<int32_t>& _s7)
	{
		Stage st(_s7);
		const Rom& rom = st.rom;
		constexpr int Blocks = 1500;	// 1 s
		bool ok = true;
		const auto waveU2 = fastWave(fast::tables::sdVintageWaveU2());
		const auto waveU3 = fastWave(fast::tables::sdVintageWaveU3());

		// Unit 3 (0x40003958): its RAM wave feeding back on its own output under an envelope that decays from 1 (time
		// constant 0.1 to 1.2 s across the lanes), the feedback from the table across its range (lane l at entry
		// 8 l + 7, then at 127 - 8 l from 0.5 s)
		{
			Runs r;
			alignas(16) uint32_t phase[Lanes] = {};
			alignas(16) float fb[Lanes] = {}, fbTo[Lanes], last[Lanes] = {};
			fast::Increments inc;
			int32_t prev[Lanes];
			std::fill_n(prev, Lanes, INT32_MIN);
			for(int b = 0; b < Blocks; ++b)
			{
				fast::Block out, scaled, g;
				for(int l = 0; l < Lanes; ++l)
				{
					Voice v = st.voice(l);
					if(b == 0)
					{
						v.unit(3, Voice::Phase) = 0;
						v.at(0x28) = v.at(0x30) = 0;
						v.unit(3, 0xb0) = static_cast<int32_t>(WaveU3);
						v.unit(3, Voice::EnvLevelPrev) = INT32_MIN;
					}
					v.at(0x2c) = rom.entry(FeedbackTable, b < 750 ? 8 * l + 7 : 127 - 8 * l) >> 2;
					fbTo[l] = static_cast<float>(4 * q31(v.at(0x2c)));
					const double t = (b + 1) * BlockSize / 48000.0;
					const int32_t level = toQ31(-std::exp(-t / (0.1 + 0.07 * l)));
					v.unit(3, Voice::EnvLevel) = level;
					for(int n = 0; n < BlockSize; ++n)
						g.s[n][l] = static_cast<float>(-q31(prev[l]) + n / 32.0 * (q31(prev[l]) - q31(level)));
					prev[l] = level;
					v.unit(3, 0x78) = sweptIncrement(l, b, 1.25);
					for(auto& s : inc.s)
						s[l] = static_cast<uint32_t>(v.unit(3, 0x78)) << 8;
					scaledFeedbackOscillator(3, v, rom);
					for(int n = 0; n < BlockSize; ++n)
						r.ref[l].push_back(q31(v.sram.at(bufferB(3) + 4 * n)));
				}
				fast::feedbackWave(out, scaled, g, last, fb, fbTo, phase, inc, waveU3.data());
				for(int l = 0; l < Lanes; ++l)
					for(int n = 0; n < BlockSize; ++n)
						r.test[l].push_back(out.s[n][l]);
			}
			ok &= r.print("feedback wave (unit 3)");
		}

		// Unit 2 (0x4000290e): its RAM wave modulated by a decaying wave, as unit 3 under its envelope feeds it, at a
		// depth ramped from block to block, from the table across its range (lane l at entry 8 l + 7, then at
		// 127 - 8 l from 0.5 s); units 0 and 1 run the same routine on the RAM sine
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
						v.unit(2, Voice::Phase) = 0;
						v.unit(2, 0x9c) = static_cast<int32_t>(bufferA(3));
						v.unit(2, 0x94) = 0;
						v.unit(2, 0x90) = v.unit(2, 0x98) = 0;
						v.unit(2, 0xb0) = static_cast<int32_t>(WaveU2);
					}
					const int32_t d = frac(0x01000000, rom.entry(Depth2Table, b < 750 ? 8 * l + 7 : 127 - 8 * l));
					v.unit(2, 0x8c) = d;
					to[l] = static_cast<float>(q31(d) * 128);
					v.unit(2, 0x78) = sweptIncrement(l, b, 4);
					for(auto& s : inc.s)
						s[l] = static_cast<uint32_t>(v.unit(2, 0x78)) << 8;
					for(int n = 0; n <= BlockSize; ++n)
					{
						const double t = (b * BlockSize + n) / 48000.0;
						const double x = 2 * Pi * (70 + 29 * l) * t;
						const double m = 0.95 * std::exp(-t / (0.03 + 0.03 * l)) * (0.64 * std::sin(x) +
							0.32 * std::sin(2 * x));
						v.sram.at(bufferA(3) + 4 * n) = toQ31(m);
						if(n < BlockSize)
							in.s[n][l] = static_cast<float>(q31(v.sram.at(bufferA(3) + 4 * n)));
					}
					pmOscillator(2, v, rom);
					for(int n = 0; n < BlockSize; ++n)
						r.ref[l].push_back(q31(v.sram.at(bufferB(2) + 4 * n)));
				}
				fast::pmWave(out, &in, from, to, phase, inc, waveU2.data());
				std::copy_n(to, Lanes, from);
				for(int l = 0; l < Lanes; ++l)
					for(int n = 0; n < BlockSize; ++n)
						r.test[l].push_back(out.s[n][l]);
			}
			ok &= r.print("PM wave (unit 2)");
		}

		// The noise's high-pass (0x40005016) driven into its clip by the gated noise, at full scale as the machine's,
		// and doubled: the fast stage at twice the words, its output doubled and clipped
		{
			Runs r;
			const auto seeds = exactNoiseSeeds(_s7);
			int32_t s[Lanes][5], c[Lanes][9];
			alignas(16) uint32_t w[4][Lanes];
			alignas(16) float state[4][Lanes] = {};
			const int32_t words[5] = {0x1a968ebc, static_cast<int32_t>(0xcad2e288), 0x1a968ebc,
				static_cast<int32_t>(0x977daa47), 0x2c322027};
			float k[5];
			for(int i = 0; i < 5; ++i)
				k[i] = static_cast<float>(2 * q31(words[i]));
			for(int l = 0; l < Lanes; ++l)
			{
				const int32_t* drawn = seeds[static_cast<size_t>(l)].data();
				for(int i = 0; i < 4; ++i)
					w[i][l] = static_cast<uint32_t>(s[l][i] = drawn[2 + i]);	// the noise's words, the last four
				s[l][4] = 0x2a3d70a4;
				std::fill_n(c[l], 9, 0);
				std::copy_n(words, 5, c[l]);
			}
			for(int b = 0; b < Blocks; ++b)
			{
				fast::Block x;
				int32_t ref[Lanes][BlockSize];
				for(int l = 0; l < Lanes; ++l)
				{
					gatedNoise(ref[l], s[l]);
					biquad(ref[l], c[l]);
					for(int n = 0; n < BlockSize; ++n)
						r.ref[l].push_back(q31(ref[l][n]));
				}
				fast::gatedNoise(x, w, 0x2a3d70a4);
				fast::biquad(x, k, state);
				for(int l = 0; l < Lanes; ++l)
					for(int n = 0; n < BlockSize; ++n)
						r.test[l].push_back(std::clamp(2.0 * x.s[n][l], -1.0, 1.0));
			}
			ok &= r.print("noise high-pass into its clip");
		}

		// The drive (0x400047ea) with SD VINTAGE's coefficients, on on lanes 0-7, off on 8-15 (bench::q3DriveClip)
		const DriveWords drive = {
			{0x01f10e1d, static_cast<int32_t>(0x83e21c3a), 0x00117fe6, static_cast<int32_t>(0x8022ffcb), 0x20000000,
				INT32_MAX, 0x60000000},
			{0x01f10e1d, static_cast<int32_t>(0x83e21c3a), 0x00117fe6, static_cast<int32_t>(0x8022ffcb), INT32_MAX,
				0x20000000, INT32_MAX}};
		ok &= q3DriveClip(st, drive, "drive clip (on, off)");
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
	const Exacts exact([&](const Nudge& _p) { return renderExact(s7, Seconds, _p); }, fast::SdVintage::Units);
	q4(exact, renderFast(48000, Seconds));
	deterministic(renderExact(withoutTable(s7, FeedbackTable), Seconds), renderFast(48000, Seconds, true));
	q15(renderFast(48000, Seconds + 0.05, false, true), renderFast(44100, Seconds + 0.05, false, true),
		Seconds);
	printCost<fast::SdVintage>("SD VINTAGE", s7, Hits);

	std::printf(g_failures ? "%d FAILED\n" : "all passed\n", g_failures);
	return g_failures ? 1 : 0;
}
