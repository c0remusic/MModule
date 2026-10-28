// syntakt::fast::CyAlloy, CY ALLOY in float on 16 Tracks, measured against the exact integer port (stEngine) and
// against itself at 44.1 kHz, by the criteria BD MODERN's test set (stFastBdModernTest, stFastBench.h):
//	Q2	the linear stages (the mix, the VCA, the output's high-pass below its clip) against the integer stages fed the
//		same input: null residual RMS <= -100 dBFS and max <= -90 dBFS
//	Q3	the non-linear stages (unit 2's feedback sine, units 1 and 3's phase-modulated wave, the high-pass driven into
//		its clip, the drive's clip) against the integer stages fed the same input, over 1 s: SNR >= 70 dB and >= 60 dB
//		in every 10 ms window above -60 dB, no residual third octave above -60 dB
//	Q4	the whole voice, 16 Hits over CY ALLOY's parameters (2 s each), against syntakt::cyAlloy at 48 kHz, by
//		bench::q4's measures where not chaotic: Hilbert envelope within -39 dB of the peak, short-time third octaves
//		within 1 dB, pitch within 1 cent, fatal; the null and the 1 ms envelope printed. The same Hits with the depth
//		table at 0 in both engines, which takes out unit 2's feedback and units 1 and 3's modulation, the one table
//		holding both: the same measures, fatal; the null printed
//	Q15	the same Hits at 44.1 kHz, rendered at 48 and resampled (Host), against 48 kHz (bench::q15): envelope below
//		18 kHz within 0.2 dB, every third octave below 10 kHz within 0.5 dB, nothing excused
//	cost: microseconds per 32-sample block of 16 voices playing, fast against exact, and at 44.1 kHz
// A Hit is chaotic where the exact port, its phases 1 LSB on, fails Q4's measures against itself (a lane of Q3's
// feedback sine where it misses the null), and is then judged by its distributions within the calibrated factor times
// the exact port's own dispersion + 0.5 dB (bench::calibrate).
//
// SYNTAKT_SECTION7 names the user's OS 1.41 section 7. Exit 77 without it.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

#include "stEmac.h"
#include "stEngine.h"
#include "stEngines.h"
#include "stFastBench.h"
#include "stFastCyAlloy.h"
#include "stFastTables.h"
#include "stSection7.h"
#include "stStages.h"

namespace
{
	using namespace syntakt;
	using fast::Lanes;
	using namespace fast::bench;

	constexpr int Machine = 2;	// CY ALLOY's id

	// The 16 Hits: TUNE, unit 2's ratio, the depth, units 0 and 1's ratio, units 0 and 2's levels at rest, the drive,
	// HOLD, DECAY. The retrigger (106.7 ms) sets units 0 and 1's ratio and the drive to 127 and the note 2 up; the
	// gate-off is at 213.3 ms. Hits 0, 6 and 15 hold the values the sweep (stFastSweep) found most sensitive to a
	// unit's ratio: unit 2's ratio at 50 and 126, units 0 and 1's between 66 and 67
	const Hit Hits[Lanes] = {
		{{64, 50, 64, 64, 64, 0, 0, 64}, {}, 48, false, false},
		{{64, 0, 0, 0, 0, 0, 0, 40}, {}, 48, false, false},
		{{64, 127, 127, 127, 127, 1, 0, 90}, {}, 48, false, false},
		{{76, 32, 96, 20, 100, 0, 0, 110}, {}, 40, false, false},
		{{52, 96, 32, 110, 10, 1, 0, 70}, {}, 52, false, false},
		{{64, 10, 127, 64, 64, 1, 0, 127}, {}, 36, false, false},
		{{64, 127, 64, 66, 0, 0, 0, 100}, {0, 0, 0, 128, 0, 0, 0, 0}, 45, false, false},
		{{100, 50, 110, 30, 120, 0, 0, 80}, {}, 30, false, false},
		{{64, 64, 64, 64, 64, 0, 1, 90}, {}, 48, false, true},
		{{30, 110, 30, 90, 40, 1, 0, 60}, {}, 55, false, false},
		{{64, 70, 50, 50, 70, 0, 0, 96}, {128, 77, 200, 0, 33, 0, 0, 0}, 48, false, false},
		{{64, 64, 64, 64, 64, 1, 0, 100}, {}, 48, true, false},
		{{127, 40, 1, 40, 40, 0, 0, 80}, {}, 50, false, false},
		{{0, 90, 70, 127, 90, 1, 0, 85}, {}, 72, false, false},
		{{64, 120, 15, 80, 60, 1, 1, 120}, {}, 42, false, true},
		{{64, 126, 100, 0, 64, 0, 0, 30}, {}, 48, true, false},
	};

	constexpr double Seconds = 2;
	constexpr uint32_t DepthTable = 0x40020c00, WaveTable = 0x40021000;

	// The words the update sets from the depth table's _d: units 1 and 3's modulation depth, unit 2's feedback
	int32_t depthWord(const int32_t _d) { return frac(0x02000000, _d); }

	// A phase-modulation depth word in cycles per unit of input
	float cycles(const int32_t _depth) { return static_cast<float>(q31(_depth) * 128); }

	// The wave as the fast engine reads it, from stFastTables' formula
	std::vector<float> fastWave()
	{
		const auto w = fast::tables::cyAlloyWave();
		std::vector<float> f(258);
		for(int i = 0; i < 257; ++i)
			f[static_cast<size_t>(i)] = static_cast<float>(q31(w[static_cast<size_t>(i)]));
		f[257] = f[256];
		return f;
	}

	// _seconds of the 16 Hits from the fast engine at _fs (bench::fastHits), the depth table at 0 (_noDepth)
	std::vector<std::vector<double>> renderFast(const double _fs, const double _seconds, const bool _noDepth = false,
		const bool _fromTrig = false)
	{
		return fastHits<fast::CyAlloy>(_fs, _seconds, Hits, [&](fast::CyAlloy& _e)
		{
			if(_noDepth)
				_e.testWithoutChaos();
		}, _fromTrig);
	}

	// The same from the exact port at 48 kHz, every Track on CY ALLOY
	std::vector<std::vector<double>> renderExact(const std::vector<int32_t>& _s7, const double _seconds,
		const Nudge& _nudge = {})
	{
		return exactHits(_s7, _seconds, _nudge, [](Exact16& _e, const int _b)
		{
			if(_b == 0)
				for(int t = 0; t < Lanes; ++t)
					_e.setMachine(t, Machine);
			events(_e, _b, 48000, Hits);
		});
	}

	// The output's high-pass words at 48 kHz, into the voice fields cyAlloy::biquad reads (b0-b2, x1, x2 at +0x18;
	// a1, a2, y1, y2 at +0x18), its state cleared
	void highPassWords(int32_t* _b, int32_t* _a)
	{
		std::fill_n(_b, 8, 0);
		std::fill_n(_a, 8, 0);
		_b[0] = _b[2] = 0x3a2102de;
		_b[1] = static_cast<int32_t>(0x8bbdfa44);
		_a[0] = static_cast<int32_t>(0x92100c93);
		_a[1] = 0x75283018;
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

		// The mix (0x400049f4) at CY ALLOY's gains, 0.5 each without a ramp
		{
			Residual r;
			alignas(16) float g[Lanes];
			std::fill_n(g, Lanes, q(0x40000000));
			fast::Block x, y, out;
			const uint32_t X = bufferA(3), Y = bufferA(1), Out = 0x80009c58;
			for(int b = 0; b < Blocks; ++b)
			{
				int32_t ref[Lanes][BlockSize];
				for(int l = 0; l < Lanes; ++l)
				{
					Voice v = st.voice(l);
					v.at(0x08) = v.at(0x18) = 0x40000000;
					v.at(0x0c) = v.at(0x1c) = 0x40000000;
					for(int n = 0; n < BlockSize; ++n)
					{
						v.sram.at(X + 4 * n) = noise(0.95);
						v.sram.at(Y + 4 * n) = noise(0.95);
						x.s[n][l] = q(v.sram.at(X + 4 * n));
						y.s[n][l] = q(v.sram.at(Y + 4 * n));
					}
					mix(v.sram.ptr(X), v.sram.ptr(Y), v.sram.ptr(Out), 0, 1, v);
					std::copy_n(v.sram.ptr(Out), BlockSize, ref[l]);
				}
				fast::mix(x, y, out, g, g, g, g);
				for(int l = 0; l < Lanes; ++l)
					for(int n = 0; n < BlockSize; ++n)
						r.add(q31(ref[l][n]), out.s[n][l]);
			}
			ok &= r.print("mix");
		}

		// The amp's VCA (0x400045a0): its level ramped from the previous block's, as the engine reads it on the block
		// clock (fast::gain of the ramp)
		{
			Residual r;
			int32_t prev[Lanes] = {};
			fast::Block x, g;
			for(int b = 0; b < Blocks; ++b)
			{
				int32_t ref[Lanes][BlockSize];
				for(int l = 0; l < Lanes; ++l)
				{
					Voice v = st.voice(l);
					const int32_t level = toQ31(-std::abs(u(rng)));	// negative representation
					v.at(Voice::TrigPending) = 0;
					v.at(Voice::TrigNow) = 0;
					v.at(Voice::AmpLevel) = level;
					v.at(Voice::AmpLevelPrev) = prev[l];
					for(int n = 0; n < BlockSize; ++n)
					{
						ref[l][n] = noise(0.95);
						x.s[n][l] = q(ref[l][n]);
						g.s[n][l] = -q(prev[l]) + static_cast<float>(n) / BlockSize * (q(prev[l]) - q(level));
					}
					vca(ref[l], v);
					prev[l] = level;
				}
				fast::gain(x, g);
				for(int l = 0; l < Lanes; ++l)
					for(int n = 0; n < BlockSize; ++n)
						r.add(q31(ref[l][n]), x.s[n][l]);
			}
			ok &= r.print("ramped gain (VCA)");
		}

		// The output's high-pass (0x4000512c) below its clip: noise at 0.2, its peak gain 2.5 at 3.5 kHz
		{
			Residual r;
			int32_t bw[Lanes][8], aw[Lanes][8];
			for(int l = 0; l < Lanes; ++l)
				highPassWords(bw[l], aw[l]);
			float c[5];
			fast::CyAlloy::highPass(c);
			alignas(16) float s[4][Lanes] = {};
			fast::Block x;
			for(int b = 0; b < Blocks; ++b)
			{
				int32_t ref[Lanes][BlockSize];
				for(int l = 0; l < Lanes; ++l)
				{
					for(int n = 0; n < BlockSize; ++n)
					{
						ref[l][n] = noise(0.2);
						x.s[n][l] = q(ref[l][n]);
					}
					cyAlloy::biquad(ref[l], bw[l], aw[l]);
				}
				fast::biquad(x, c, s);
				for(int l = 0; l < Lanes; ++l)
					for(int n = 0; n < BlockSize; ++n)
						r.add(q31(ref[l][n]), x.s[n][l]);
			}
			ok &= r.print("biquad high-pass");
		}
		check(ok, "Q2: the linear stages null to <= -100 dBFS RMS, <= -90 dBFS max");
	}

	// ---- Q3: the non-linear stages against their integer originals ----

	void q3(const std::vector<int32_t>& _s7)
	{
		Stage st(_s7);
		const Rom& rom = st.rom;
		constexpr int Blocks = 1500;	// 1 s
		bool ok = true, chaoticOk = true;
		const auto wave = fastWave();

		// Unit 2's feedback is the depth table's word >> 3; the bench runs the routine (0x400035de) on unit 0
		q3FeedbackSine(st, DepthTable, "parameter 3 at", ok, chaoticOk, 3);

		// Unit 1 (0x4000290e): the ROM wave modulated by a decaying sine, as unit 0 under its envelope feeds it, at a
		// depth ramped from block to block, from the table across its range (lane l at entry 8 l + 7, then at
		// 127 - 8 l from 0.5 s); unit 3 runs the same routine on unit 2's output
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
						v.unit(1, Voice::Phase) = 0;
						v.unit(1, 0x9c) = static_cast<int32_t>(bufferA(0));
						v.unit(1, 0x94) = 0;
						v.unit(1, 0x90) = v.unit(1, 0x98) = 0;
						v.unit(1, 0xb0) = static_cast<int32_t>(WaveTable);
					}
					const int32_t d = depthWord(rom.entry(DepthTable, b < 750 ? 8 * l + 7 : 127 - 8 * l));
					v.unit(1, 0x8c) = d;
					to[l] = cycles(d);
					v.unit(1, 0x78) = sweptIncrement(l, b, 1);
					for(auto& s : inc.s)
						s[l] = static_cast<uint32_t>(v.unit(1, 0x78)) << 8;
					for(int n = 0; n <= BlockSize; ++n)
					{
						const double t = (b * BlockSize + n) / 48000.0;
						const double m = 0.95 * std::exp(-t / (0.02 + 0.02 * l)) * std::sin(2 * Pi * (80 + 37 * l) * t);
						v.sram.at(bufferA(0) + 4 * n) = toQ31(m);
						if(n < BlockSize)
							in.s[n][l] = static_cast<float>(q31(v.sram.at(bufferA(0) + 4 * n)));
					}
					pmOscillator(1, v, rom);
					for(int n = 0; n < BlockSize; ++n)
						r.ref[l].push_back(q31(v.sram.at(bufferB(1) + 4 * n)));
				}
				fast::pmWave(out, &in, from, to, phase, inc, wave.data());
				std::copy_n(to, Lanes, from);
				for(int l = 0; l < Lanes; ++l)
					for(int n = 0; n < BlockSize; ++n)
						r.test[l].push_back(out.s[n][l]);
			}
			ok &= r.print("PM wave (units 1 and 3)");
		}

		// The output's high-pass driven into its clip: a decaying sine from 0.95, swept across its peak from 2 to
		// 6 kHz, up to 2.4 times the clip at 3.5 kHz
		{
			Runs r;
			int32_t bw[Lanes][8], aw[Lanes][8];
			for(int l = 0; l < Lanes; ++l)
				highPassWords(bw[l], aw[l]);
			float c[5];
			fast::CyAlloy::highPass(c);
			alignas(16) float s[4][Lanes] = {};
			double phase[Lanes] = {};
			for(int b = 0; b < Blocks; ++b)
			{
				fast::Block x;
				int32_t ref[Lanes][BlockSize];
				for(int l = 0; l < Lanes; ++l)
				{
					for(int n = 0; n < BlockSize; ++n)
					{
						const double t = (b * BlockSize + n) / 48000.0;
						const double hz = 2000 + 4000 * std::fmod(t * (0.5 + 0.25 * l), 1.0);
						phase[l] += hz / 48000;
						ref[l][n] = toQ31(0.95 * std::exp(-t / (0.2 + 0.1 * l)) * std::sin(2 * Pi * phase[l]));
						x.s[n][l] = static_cast<float>(q31(ref[l][n]));
					}
					cyAlloy::biquad(ref[l], bw[l], aw[l]);
					for(int n = 0; n < BlockSize; ++n)
						r.ref[l].push_back(q31(ref[l][n]));
				}
				fast::biquad(x, c, s);
				for(int l = 0; l < Lanes; ++l)
					for(int n = 0; n < BlockSize; ++n)
						r.test[l].push_back(x.s[n][l]);
			}
			ok &= r.print("biquad into its clip");
		}

		// The drive (0x400047ea) with CY ALLOY's coefficients, on on lanes 0-7, off on 8-15, fed a full-scale decaying
		// sine from a trig, the float clip fed the integer gain envelope's levels
		{
			Runs r;
			alignas(16) float from[Lanes], to[Lanes], drive[Lanes], post[Lanes];
			for(int b = 0; b < Blocks; ++b)
			{
				fast::Block x;
				int32_t ref[Lanes][BlockSize];
				for(int l = 0; l < Lanes; ++l)
				{
					Voice v = st.voice(l);
					const bool on = l < 8;
					v.at(Voice::TrigNow) = b == 0;
					if(b == 0)
					{
						v.at(0x414) = v.at(0x418) = 0x00a0aaaa;
						v.at(0x41c) = static_cast<int32_t>(0x81415554);
						v.at(0x420) = v.at(0x424) = 0x0013b13d;
						v.at(0x428) = static_cast<int32_t>(0x8027627b);
						v.at(Voice::DriveFloor) = on ? 0x2a3d70a4 : INT32_MAX;
						v.at(Voice::DrivePreGain) = on ? 0x2a3d70a4 : 0x20000000;
						v.at(Voice::DriveGain) = on ? INT32_MAX : 0x60000000;
						v.at(Voice::DriveOn) = on;
						v.at(Voice::DriveX1) = v.at(Voice::DriveY1) = INT32_MAX;
					}
					from[l] = static_cast<float>(on ? q31(v.at(Voice::DriveY1)) : q31(INT32_MAX));
					drive[l] = static_cast<float>(4 * q31(v.at(Voice::DrivePreGain)));
					post[l] = static_cast<float>(q31(v.at(Voice::DriveGain)));
					for(int n = 0; n < BlockSize; ++n)
					{
						const double t = (b * BlockSize + n) / 48000.0;
						const double decay = std::exp(-t / (0.05 + 0.05 * (l & 7)));
						ref[l][n] = toQ31(decay * std::sin(2 * Pi * (40 + 10 * l) * t));
						x.s[n][l] = static_cast<float>(q31(ref[l][n]));
					}
					driveClip(ref[l], v);
					to[l] = static_cast<float>(q31(v.at(Voice::DriveY1)));
					for(int n = 0; n < BlockSize; ++n)
						r.ref[l].push_back(q31(ref[l][n]));
				}
				fast::driveClip(x, from, to, drive, post);
				for(int l = 0; l < Lanes; ++l)
					for(int n = 0; n < BlockSize; ++n)
						r.test[l].push_back(x.s[n][l]);
			}
			ok &= r.print("drive clip (on, off)");
		}
		checkValidated(ok, "Q3: the non-linear stages where not chaotic, SNR >= 70 dB, >= 60 dB in every 10 ms "
			"window above -60 dB, no residual third octave above -60 dB");
		checkValidated(chaoticOk, "Q3: the chaotic feedback sine's distributions within the calibrated factor x the "
			"exact port's dispersion + 0.5 dB");
	}

	// ---- cost ----

	void cost(const std::vector<int32_t>& _s7)
	{
		Exact16 e(_s7);
		for(int t = 0; t < Lanes; ++t)
			e.setMachine(t, Machine);
		printCost<fast::CyAlloy>("CY ALLOY", Hits, costPerBlock(e, Hits, [&] { e.render(); }));
	}
}

int main()
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	const char* path = std::getenv("SYNTAKT_SECTION7");
	if(!path || !*path)
	{
		std::printf("SYNTAKT_SECTION7 not set: skipped\n");
		return 77;
	}
	const auto s7 = loadSection7(path);
	if(s7.empty())
	{
		std::printf("%s is not OS 1.41's section 7: skipped\n", path);
		return 77;
	}

	q2(s7);
	q3(s7);
	const Exacts exact([&](const Nudge& _p) { return renderExact(s7, Seconds, _p); });
	q4(exact, renderFast(48000, Seconds));
	deterministic(renderExact(withoutTable(s7, DepthTable), Seconds), renderFast(48000, Seconds, true));
	q15(renderFast(48000, Seconds + 0.05, false, true), renderFast(44100, Seconds + 0.05, false, true), Seconds);
	cost(s7);

	std::printf(g_failures ? "%d FAILED\n" : "all passed\n", g_failures);
	return g_failures ? 1 : 0;
}
