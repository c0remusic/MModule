// syntakt::fast::BdModern, BD MODERN in float on 16 Tracks, measured against the exact integer port (stEngine) and
// against itself at another rate (.scratch/elektron-drums/research/19-qualite-audio.md, section 11):
//	Q2	the linear stages (mix, ramped gains, one-pole filters, the sum) against the integer stages fed the same input:
//		null residual RMS <= -100 dBFS and max <= -90 dBFS
//	Q3	the non-linear stages (the feedback and phase-modulated oscillators, the drive's clip) against the integer
//		stages fed the same input, over 1 s: SNR >= 60 dB, no third octave of the residual (20 Hz to Nyquist) above
//		-60 dB relative to the reference's whole energy
//	Q4	the whole voice, 16 Hits over BD MODERN's parameters (2 s each), against syntakt::bdModern at 48 kHz, from
//		the exact port's onset: the Hilbert envelope (5 ms) within -39 dB of the exact port's peak, the third octaves
//		of 1024-sample frames within 1 dB where the exact port's are within 30 dB of its loudest, pitch (spectral
//		peak) within 1 cent; the null and the 1 ms envelope printed, not fatal (bench::q4)
//	Q15	the same Hits at 44.1 kHz, rendered at 48 and resampled (Host), against 48 kHz (bench::q15): envelope below
//		18 kHz within 0.2 dB above -40 dB, spectrum below 10 kHz within 0.5 dB per third octave, nothing excused
//	cost: microseconds per 32-sample block of 16 voices playing, fast against exact
// A Hit is chaotic where the exact port, its phases 1 LSB on, fails Q4's measures against itself (TRANSIENT's
// feedback), a lane of Q3's feedback sine where it misses the null: no rewrite can do better. There Q4 and Q3 give
// way to distributions, the mean third-octave spectrum and the 10 ms RMS envelope, which pass within a factor times the
// exact port's own dispersion over 16 perturbations of its phases (1 to 8 LSB either way) plus 0.5 dB, the factor
// calibrated so that the exact port's held-out perturbations pass 95 % of the time (bench::calibrate); at 44.1
// against 48 kHz, below 10 kHz, against the same dispersion. The voice with unit 0's feedback table at 0 in both
// engines is not chaotic: there every Hit is judged by Q4's measures, fatal, its null printed.
// Q3 and Q4's thresholds were set by listening (ticket 05 of elektron-drums-v1): Q3 also asks 60 dB in every 10 ms
// window above -60 dB, and with Q2 and Q15 fails the test; so do Q4's envelope, short-time spectrum and pitch where
// not chaotic and the chaotic Hits' distributions. Q4's null does not: a sweep a little off drifts the phase, nulls
// badly and sounds the same (the ABX of 06-sweep's pairs).
//
// SYNTAKT_SECTION7 names the user's OS 1.41 section 7. Exit 77 without it.

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "stEmac.h"
#include "stEngine.h"
#include "stEngines.h"
#include "stBoot.h"
#include "stFastBench.h"
#include "stFastBdModern.h"
#include "stFastBdModernHits.h"
#include "stSection7.h"
#include "stStages.h"

namespace
{
	using namespace syntakt;
	using fast::Lanes;
	using namespace fast::bench;

	// _seconds of the 16 Hits from the fast engine at _fs (bench::fastHits), unit 0's feedback at 0 (_noFeedback)
	std::vector<std::vector<double>> renderFast(const double _fs, const double _seconds, const bool _noFeedback = false,
		const bool _fromTrig = false)
	{
		return fastHits<fast::BdModern>(_fs, _seconds, BdModernHits, [&](fast::BdModern& _e)
		{
			if(_noFeedback)
				_e.testWithoutChaos();
		}, _fromTrig);
	}

	// The same from the exact port at 48 kHz (exactHits)
	std::vector<std::vector<double>> renderExact(const std::vector<int32_t>& _s7, const double _seconds,
		const Nudge& _nudge = {})
	{
		return exactHits(_s7, _seconds, _nudge, BdModernHits);
	}

	// ---- Q2 and Q3: a stage against its integer original ----

	void q2(const std::vector<int32_t>& _s7)
	{
		Stage st(_s7);
		std::mt19937 rng(1234);
		std::uniform_real_distribution<double> u(-1, 1);
		constexpr int Blocks = 500;
		const auto noise = [&](const double _a) { return toQ31(_a * u(rng)); };
		bool ok = true;

		// mix (0x400049f4): two inputs, two gains ramped from the previous block's to this one's
		{
			Residual r;
			alignas(16) float gx0[Lanes], gx1[Lanes], gy0[Lanes], gy1[Lanes];
			int32_t cur[Lanes][2] = {};
			fast::Block x, y, out;
			constexpr uint32_t X = 0x800096a4, Y = 0x80008c60, Out = 0x80009c58;
			for(int b = 0; b < Blocks; ++b)
			{
				int32_t ref[Lanes][BlockSize];
				for(int l = 0; l < Lanes; ++l)
				{
					Voice v = st.voice(l);
					const int32_t t0 = noise(0.5), t1 = noise(0.5);
					v.at(0x08) = t0;
					v.at(0x0c) = t1;
					v.at(0x18) = cur[l][0];
					v.at(0x1c) = cur[l][1];
					gx0[l] = static_cast<float>(q31(cur[l][0]));
					gy0[l] = static_cast<float>(q31(cur[l][1]));
					gx1[l] = static_cast<float>(q31(t0));
					gy1[l] = static_cast<float>(q31(t1));
					for(int n = 0; n <= BlockSize; ++n)
					{
						v.sram.at(X + 4 * n) = noise(0.95);
						v.sram.at(Y + 4 * n) = noise(0.95);
						if(n < BlockSize)
						{
							x.s[n][l] = static_cast<float>(q31(v.sram.at(X + 4 * n)));
							y.s[n][l] = static_cast<float>(q31(v.sram.at(Y + 4 * n)));
						}
					}
					mix(v.sram.ptr(X), v.sram.ptr(Y), v.sram.ptr(Out), 0, 1, v);
					std::copy_n(v.sram.ptr(Out), BlockSize, ref[l]);
					cur[l][0] = t0;
					cur[l][1] = t1;
				}
				fast::mix(x, y, out, gx0, gx1, gy0, gy1);
				for(int l = 0; l < Lanes; ++l)
					for(int n = 0; n < BlockSize; ++n)
						r.add(q31(ref[l][n]), out.s[n][l]);
			}
			ok &= r.print("mix");
		}

		// The VCA (0x400045a0, and 0x40004674 alike): a gain ramped from the previous block's level
		{
			Residual r;
			alignas(16) float g0[Lanes], g1[Lanes];
			int32_t prev[Lanes] = {};
			fast::Block x;
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
					g0[l] = static_cast<float>(-q31(prev[l]));
					g1[l] = static_cast<float>(-q31(level));
					for(int n = 0; n < BlockSize; ++n)
					{
						ref[l][n] = noise(0.95);
						x.s[n][l] = static_cast<float>(q31(ref[l][n]));
					}
					vca(ref[l], v);
					prev[l] = level;
				}
				fast::rampedGain(x, g0, g1);
				for(int l = 0; l < Lanes; ++l)
					for(int n = 0; n < BlockSize; ++n)
						r.add(q31(ref[l][n]), x.s[n][l]);
			}
			ok &= r.print("ramped gain (VCA)");
		}

		// The click's two first-order filters (0x40004e6c), with BD MODERN's coefficients
		using Coefficients = std::array<int32_t, 3>;
		const auto word = [](const uint32_t _w) { return static_cast<int32_t>(_w); };
		for(const auto& c : {Coefficients{0x7df07773, word(0x820f888d), word(0x841f111a)},
				Coefficients{0x17db49cb, 0x17db49cb, word(0xafb69397)}})
		{
			Residual r;
			int32_t state[Lanes][8] = {};
			alignas(16) float x1[Lanes] = {}, y1[Lanes] = {};
			const auto q = [](const int32_t _v) { return static_cast<float>(q31(_v)); };
			const fast::OnePole f{q(c[0]), q(c[1]), -q(c[2])};
			fast::Block x;
			for(int b = 0; b < Blocks; ++b)
			{
				int32_t ref[Lanes][BlockSize + 1];
				for(int l = 0; l < Lanes; ++l)
				{
					for(int n = 0; n <= BlockSize; ++n)
						ref[l][n] = noise(0.45);
					for(int n = 0; n < BlockSize; ++n)
						x.s[n][l] = static_cast<float>(q31(ref[l][n]));
					std::copy_n(c.data(), 3, state[l]);
					onePole(ref[l], state[l]);
				}
				fast::onePole(x, f, x1, y1);
				for(int l = 0; l < Lanes; ++l)
					for(int n = 0; n < BlockSize; ++n)
						r.add(q31(ref[l][n]), x.s[n][l]);
			}
			ok &= r.print(c[0] == 0x7df07773 ? "one-pole high-pass" : "one-pole low-pass");
		}

		// The sum (0x400046dc), saturated
		{
			Residual r;
			fast::Block a, bb, out;
			for(int b = 0; b < Blocks; ++b)
			{
				int32_t ra[Lanes][BlockSize], rb[Lanes][BlockSize], ref[Lanes][BlockSize];
				for(int l = 0; l < Lanes; ++l)
				{
					for(int n = 0; n < BlockSize; ++n)
					{
						ra[l][n] = noise(0.9);
						rb[l][n] = noise(0.9);
						a.s[n][l] = static_cast<float>(q31(ra[l][n]));
						bb.s[n][l] = static_cast<float>(q31(rb[l][n]));
					}
					bdModern::sum(ra[l], rb[l], ref[l]);
				}
				fast::sum(a, bb, out);
				for(int l = 0; l < Lanes; ++l)
					for(int n = 0; n < BlockSize; ++n)
						r.add(q31(ref[l][n]), out.s[n][l]);
			}
			ok &= r.print("sum");
		}
		check(ok, "Q2: the linear stages null to <= -100 dBFS RMS, <= -90 dBFS max");
	}

	void q3(const std::vector<int32_t>& _s7)
	{
		Stage st(_s7);
		const Rom& rom = st.rom;
		constexpr int Blocks = 1500;	// 1 s
		bool ok = true, chaoticOk = true;

		const auto inc24 = sweptIncrement;	// lane l's note 24 + 3 l, an octave up for units 0-2

		q3FeedbackSine(st, 0x4001fc00, "TRANSIENT", ok, chaoticOk);	// BD MODERN's feedback table

		// Units 2 and 3 (0x4000290e): the ROM wave modulated by a sine at -0.11 cycle, the sine modulated by a
		// decaying modulator at 1 cycle
		std::vector<float> wave(258);
		for(int i = 0; i < 257; ++i)
			wave[i] = static_cast<float>(q31(rom.entry(0x4001e7fc, i)));
		wave[257] = wave[256];
		for(const int k : {2, 3})
		{
			Runs r;
			alignas(16) uint32_t phase[Lanes] = {};
			fast::Increments inc;
			const int32_t depth = k == 2 ? static_cast<int32_t>(0xffe3d70a) : 0x00ffffff;
			const uint32_t src = k == 2 ? bufferB(1) : 0x80009c58;
			for(int b = 0; b < Blocks; ++b)
			{
				fast::Block in, out;
				for(int l = 0; l < Lanes; ++l)
				{
					Voice v = st.voice(l);
					const auto u = static_cast<uint32_t>(k);
					if(b == 0)
					{
						v.unit(u, Voice::Phase) = 0;
						v.unit(u, 0x9c) = static_cast<int32_t>(src);
						v.unit(u, 0x8c) = v.unit(u, 0x94) = depth;
						v.unit(u, 0x90) = v.unit(u, 0x98) = 0;
						if(k == 2)
							v.unit(u, 0xb0) = 0x4001e7fc;
					}
					v.unit(u, 0x78) = inc24(l, b, k == 2 ? 2 : 1);
					for(auto& s : inc.s)
						s[l] = static_cast<uint32_t>(v.unit(u, 0x78)) << 8;
					for(int n = 0; n <= BlockSize; ++n)
					{
						const double t = (b * BlockSize + n) / 48000.0;
						const double m = 0.95 * std::exp(-t / (0.02 + 0.02 * l)) * std::sin(2 * Pi * (60 + 25 * l) * t);
						v.sram.at(src + 4 * n) = toQ31(m);
						if(n < BlockSize)
							in.s[n][l] = static_cast<float>(q31(v.sram.at(src + 4 * n)));
					}
					pmOscillator(k, v, rom);
					for(int n = 0; n < BlockSize; ++n)
						r.ref[l].push_back(q31(v.sram.at(bufferB(u) + 4 * n)));
				}
				if(k == 2)
					fast::pmWaveBd(out, in, phase, inc, static_cast<float>(q31(depth) * 128), wave.data());
				else
					fast::pmSine(out, &in, phase, inc, static_cast<float>(q31(depth) * 128));
				for(int l = 0; l < Lanes; ++l)
					for(int n = 0; n < BlockSize; ++n)
						r.test[l].push_back(out.s[n][l]);
			}
			ok &= r.print(k == 2 ? "PM wave (unit 2)" : "PM sine (unit 3)");
		}

		// The drive (0x400047ea) on lanes 0-7, clean on 8-15 (bench::q3DriveClip)
		const DriveWords drive = {
			{0x02e3f426, static_cast<int32_t>(0x85c7e84d), 0x004e99d0, static_cast<int32_t>(0x809d339f), 0x40000000,
				0x40000000, 0x3eb851ec},
			{INT32_MAX, 0, INT32_MAX, 0, 0x66666666, 0x20000000, 0x370a3d71}};
		ok &= q3DriveClip(st, drive, "drive clip (on, off)");
		checkValidated(ok, "Q3: the non-linear stages where not chaotic, SNR >= 70 dB, >= 60 dB in every 10 ms "
			"window above -60 dB, no residual third octave above -60 dB");
		checkValidated(chaoticOk, "Q3: the chaotic feedback sine's distributions within the calibrated factor x the "
			"exact port's dispersion + 0.5 dB");
	}

	constexpr double Seconds = 2;
}

int main()
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	const auto s7 = loadSection7OrSkip();
	if(s7.empty())
		return 77;

	q2(s7);
	q3(s7);
	const Exacts exact([&](const Nudge& _p) { return renderExact(s7, Seconds, _p); }, fast::BdModern::Units);
	q4(exact, renderFast(48000, Seconds));
	deterministic(renderExact(withoutTable(s7, 0x4001fc00), Seconds), renderFast(48000, Seconds, true));
	q15(renderFast(48000, Seconds + 0.05, false, true), renderFast(44100, Seconds + 0.05, false, true), Seconds);
	printCost<fast::BdModern>("BD MODERN", s7, BdModernHits);

	std::printf(g_failures ? "%d FAILED\n" : "all passed\n", g_failures);
	return g_failures ? 1 : 0;
}
