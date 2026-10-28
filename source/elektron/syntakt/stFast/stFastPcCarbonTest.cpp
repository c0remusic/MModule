// syntakt::fast::PcCarbon, PC CARBON in float on 16 Tracks, measured against the exact integer port (stEngine) and
// against itself at 44.1 kHz, by the criteria BD MODERN's test set (stFastBdModernTest, stFastBench.h):
//	Q2	the linear stages (the mix, the VCA ramped from silence) against the integer stages fed the same input: null
//		residual RMS <= -100 dBFS and max <= -90 dBFS
//	Q3	the non-linear stages (unit 0's wave, unit 1's phase-modulated sine, the drive's clip) against the integer
//		stages fed the same input, over 1 s: SNR >= 70 dB and >= 60 dB in every 10 ms window above -60 dB, no residual
//		third octave above -60 dB
//	Q4	the whole voice, 16 Hits over PC CARBON's parameters (2 s each), against syntakt::pcCarbon at 48 kHz, by
//		bench::q4's measures: Hilbert envelope within -39 dB of the peak, short-time third octaves within 1 dB, pitch
//		within 1 cent, fatal; the null and the 1 ms envelope printed. PC CARBON has no feedback: nothing in it is
//		chaotic, so this Q4 is already deterministic and the voice needs no render without its feedback
//	Q15	the same Hits at 44.1 kHz, rendered at 48 and resampled (Host), against 48 kHz (bench::q15): envelope below
//		18 kHz within 0.2 dB, every third octave below 10 kHz within 0.5 dB, nothing excused
//	cost: microseconds per 32-sample block of 16 voices playing, fast against exact, and at 44.1 kHz
// A Hit would be chaotic where the exact port, its phases 1 LSB on, failed Q4's measures against itself, and would be
// judged by its distributions (bench::Exacts): Q4 prints it, should one be.
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
#include "stFastPcCarbon.h"
#include "stFastTables.h"
#include "stMemoryMap.h"
#include "stSection7.h"
#include "stStages.h"

namespace
{
	using namespace syntakt;
	using fast::Lanes;
	using namespace fast::bench;

	constexpr int Machine = 3;	// PC CARBON's id

	// The 16 Hits: TUNE, the units' ratios, the depth, the sweep, units 0 and 2's levels at rest, the drive, HOLD,
	// DECAY. The retrigger (106.7 ms) sets the sweep and the drive to 127 and the note 2 up; the gate-off is at
	// 213.3 ms
	const HitSet Hits = {{
		{{64, 64, 64, 64, 64, 0, 0, 64}, {}, 48, false, false},
		{{64, 0, 0, 0, 0, 0, 0, 40}, {}, 48, false, false},
		{{64, 127, 127, 127, 127, 1, 0, 90}, {}, 48, false, false},
		{{76, 32, 96, 20, 100, 0, 0, 110}, {}, 40, false, false},
		{{52, 96, 32, 110, 10, 1, 0, 70}, {}, 52, false, false},
		{{64, 10, 127, 64, 64, 1, 0, 127}, {}, 36, false, false},
		{{64, 127, 0, 127, 0, 0, 0, 100}, {}, 45, false, false},
		{{100, 50, 110, 30, 120, 0, 0, 80}, {}, 30, false, false},
		{{64, 64, 64, 64, 64, 0, 1, 90}, {}, 48, false, true},
		{{30, 110, 30, 90, 40, 1, 0, 60}, {}, 55, false, false},
		{{64, 70, 50, 50, 70, 0, 0, 96}, {128, 77, 200, 0, 33, 0, 0, 0}, 48, false, false},
		{{64, 64, 64, 64, 64, 1, 0, 100}, {}, 48, true, false},
		{{127, 40, 1, 40, 40, 0, 0, 80}, {}, 50, false, false},
		{{0, 90, 70, 127, 90, 1, 0, 85}, {}, 72, false, false},
		{{64, 120, 15, 80, 60, 1, 1, 120}, {}, 42, true, true},
		{{64, 64, 100, 0, 64, 0, 0, 30}, {}, 48, true, false},
	}, {3, 5}, Machine};

	constexpr double Seconds = 2;
	constexpr uint32_t DepthU1Table = 0x40023404, WaveTable = 0x40024804;

	// A phase-modulation depth word in cycles per unit of input
	float cycles(const int32_t _depth) { return static_cast<float>(q31(_depth) * 128); }

	// The sine the fast engine reads (fast::sineTable)
	std::vector<float> sine()
	{
		std::vector<float> s(258);
		fast::sineTable(s.data());
		return s;
	}

	// _seconds of the 16 Hits from the fast engine at _fs (bench::fastHits)
	std::vector<std::vector<double>> renderFast(const double _fs, const double _seconds, const bool _fromTrig = false)
	{
		return fastHits<fast::PcCarbon>(_fs, _seconds, Hits, [](fast::PcCarbon&) {}, _fromTrig);
	}

	// The same from the exact port at 48 kHz, every Track on PC CARBON
	std::vector<std::vector<double>> renderExact(const std::vector<int32_t>& _s7, const double _seconds,
		const Nudge& _nudge = {})
	{
		return exactHits(_s7, _seconds, _nudge, Hits);
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

		// The mix (0x400049f4) at PC CARBON's gains, drive on (unit 3 at 0.5, unit 1 at 0.25) on lanes 0-7 and off
		// (0.25, 0.125) on 8-15, without a ramp
		{
			Residual r;
			alignas(16) float gx[Lanes], gy[Lanes];
			for(int l = 0; l < Lanes; ++l)
			{
				gx[l] = l < 8 ? q(0x40000000) : q(0x20000000);
				gy[l] = l < 8 ? q(0x20000000) : q(0x10000000);
			}
			fast::Block x, y, out;
			const uint32_t X = bufferA(3), Y = bufferA(1), Out = 0x80009c58;
			for(int b = 0; b < Blocks; ++b)
			{
				int32_t ref[Lanes][BlockSize];
				for(int l = 0; l < Lanes; ++l)
				{
					Voice v = st.voice(l);
					v.at(0x08) = v.at(0x18) = l < 8 ? 0x40000000 : 0x20000000;
					v.at(0x0c) = v.at(0x1c) = l < 8 ? 0x20000000 : 0x10000000;
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
				fast::mix(x, y, out, gx, gx, gy, gy);
				for(int l = 0; l < Lanes; ++l)
					for(int n = 0; n < BlockSize; ++n)
						r.add(q31(ref[l][n]), out.s[n][l]);
			}
			ok &= r.print("mix");
		}

		// The amp's VCA (0x400045a0): its level ramped from the previous block's, as the engine reads it on the block
		// clock (fast::gain of the ramp); every 8th block from silence, as after a trig
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
					if(b % 8 == 0)
						prev[l] = 0;
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
		check(ok, "Q2: the linear stages null to <= -100 dBFS RMS, <= -90 dBFS max");
	}

	// ---- Q3: the non-linear stages against their integer originals ----

	void q3(const std::vector<int32_t>& _s7)
	{
		Stage st(_s7);
		const Rom& rom = st.rom;
		constexpr int Blocks = 1500;	// 1 s
		bool ok = true;
		const auto wave = fastWave(fast::tables::pcCarbonWave());
		const auto sineTable = sine();
		alignas(16) static constexpr float Zero[Lanes] = {};

		// Unit 0 (0x4000290e): its ROM wave unmodulated, swept, lane l at note 24 + 3 l
		{
			Runs r;
			alignas(16) uint32_t phase[Lanes] = {};
			fast::Increments inc;
			for(int b = 0; b < Blocks; ++b)
			{
				fast::Block out;
				for(int l = 0; l < Lanes; ++l)
				{
					Voice v = st.voice(l);
					if(b == 0)
					{
						v.unit(0, Voice::Phase) = 0;
						v.unit(0, 0x8c) = v.unit(0, 0x94) = v.unit(0, 0x90) = v.unit(0, 0x98) = 0;
						v.unit(0, 0xb0) = static_cast<int32_t>(WaveTable);
					}
					v.unit(0, 0x78) = sweptIncrement(l, b, 1);
					for(auto& s : inc.s)
						s[l] = static_cast<uint32_t>(v.unit(0, 0x78)) << 8;
					pmOscillator(0, v, rom);
					for(int n = 0; n < BlockSize; ++n)
						r.ref[l].push_back(q31(v.sram.at(bufferB(0) + 4 * n)));
				}
				fast::pmWave(out, nullptr, Zero, Zero, phase, inc, wave.data());
				for(int l = 0; l < Lanes; ++l)
					for(int n = 0; n < BlockSize; ++n)
						r.test[l].push_back(out.s[n][l]);
			}
			ok &= r.print("wave (unit 0)");
		}

		// Unit 1 (0x4000290e): the RAM sine modulated by a decaying wave, as unit 0 under its envelope feeds it, at a
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
						v.unit(1, 0xb0) = static_cast<int32_t>(Sine);
					}
					const int32_t d = frac(0x02000000, rom.entry(DepthU1Table, b < 750 ? 8 * l + 7 : 127 - 8 * l));
					v.unit(1, 0x8c) = d;
					to[l] = cycles(d);
					v.unit(1, 0x78) = sweptIncrement(l, b, 1.5);
					for(auto& s : inc.s)
						s[l] = static_cast<uint32_t>(v.unit(1, 0x78)) << 8;
					for(int n = 0; n <= BlockSize; ++n)
					{
						const double t = (b * BlockSize + n) / 48000.0;
						const double x = 2 * Pi * (60 + 23 * l) * t;
						const double m = 0.95 * std::exp(-t / (0.02 + 0.02 * l)) *
							(0.75 * std::sin(x) + 0.19 * std::sin(5 * x) + 0.06 * std::sin(9 * x));
						v.sram.at(bufferA(0) + 4 * n) = toQ31(m);
						if(n < BlockSize)
							in.s[n][l] = static_cast<float>(q31(v.sram.at(bufferA(0) + 4 * n)));
					}
					pmOscillator(1, v, rom);
					for(int n = 0; n < BlockSize; ++n)
						r.ref[l].push_back(q31(v.sram.at(bufferB(1) + 4 * n)));
				}
				fast::pmWave(out, &in, from, to, phase, inc, sineTable.data());
				std::copy_n(to, Lanes, from);
				for(int l = 0; l < Lanes; ++l)
					for(int n = 0; n < BlockSize; ++n)
						r.test[l].push_back(out.s[n][l]);
			}
			ok &= r.print("PM sine (units 1 and 3)");
		}

		// The drive (0x400047ea) with PC CARBON's coefficients, on on lanes 0-7, off on 8-15 (bench::q3DriveClip)
		const DriveWords drive = {
			{0x012c0f26, static_cast<int32_t>(0x82581e4b), 0x0013af8c, static_cast<int32_t>(0x80275f18), 0x20000000,
				0x2a3d70a4, INT32_MAX},
			{0x012c0f26, static_cast<int32_t>(0x82581e4b), 0x0013af8c, static_cast<int32_t>(0x80275f18), INT32_MAX,
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
	const Exacts exact([&](const Nudge& _p) { return renderExact(s7, Seconds, _p); }, fast::PcCarbon::Units);
	q4(exact, renderFast(48000, Seconds));
	q15(renderFast(48000, Seconds + 0.05, true), renderFast(44100, Seconds + 0.05, true), Seconds);
	printCost<fast::PcCarbon>("PC CARBON", s7, Hits);

	std::printf(g_failures ? "%d FAILED\n" : "all passed\n", g_failures);
	return g_failures ? 1 : 0;
}
