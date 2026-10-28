// syntakt::fast::SdBasic, SD BASIC in float on 16 Tracks, measured against the exact integer port (stEngine) and
// against itself at 44.1 kHz, by the criteria BD MODERN's test set (stFastBdModernTest, stFastBench.h):
//	Q2	the linear stages (the mix, the VCA, unit 2's high-pass, unit 3's low-pass) against the integer stages fed the
//		same input: null residual RMS <= -100 dBFS and max <= -90 dBFS
//	Q3	the non-linear stages (unit 0's feedback sine, unit 2's and unit 3's phase-modulated sines, the drive's clip)
//		against the integer stages fed the same input, over 1 s: SNR >= 70 dB and >= 60 dB in every 10 ms window
//		above -60 dB, no residual third octave above -60 dB
//	Q4	the whole voice, 16 Hits over SD BASIC's parameters (2 s each), against syntakt::sdBasic at 48 kHz, by
//		bench::q4's measures where not chaotic: Hilbert envelope within -39 dB of the peak, short-time third octaves
//		within 1 dB, pitch within 1 cent, fatal; the null and the 1 ms envelope printed. The same Hits with unit 0's
//		feedback table at 0 in both engines, which leaves nothing chaotic: the same measures, fatal; the null printed
//	Q15	the same Hits at 44.1 kHz, rendered at 48 and resampled (Host), against 48 kHz (bench::q15): envelope below
//		18 kHz within 0.2 dB, spectrum below 10 kHz within 0.5 dB per third octave, nothing excused
//	cost: microseconds per 32-sample block of 16 voices playing, fast against exact, and at 44.1 kHz
// A Hit is chaotic where the exact port, its phases 1 LSB on, fails Q4's measures against itself, a lane of Q3's
// feedback sine where it misses the null (unit 0's feedback is never below 0.525 cycle per unit). It is judged by its
// distributions (the third-octave spectrum and the 10 ms envelope) within a factor times the exact port's own
// dispersion over 16 nudges + 0.5 dB, the factor calibrated so that the exact port's held-out nudges pass 95 % of the
// time (bench::calibrate).
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
#include "stFastSdBasic.h"
#include "stSection7.h"
#include "stStages.h"

namespace
{
	using namespace syntakt;
	using fast::Lanes;
	using namespace fast::bench;

	constexpr int Machine = 1;	// SD BASIC's id

	// The 16 Hits: TUNE, the ratios, unit 0's feedback with unit 3's modulation (0 its low-pass's other pole), the
	// sweep, the units' envelopes, the drive, HOLD, DECAY. The retrigger (106.7 ms) sets the sweep and the drive to 127
	// and the note 2 up; the gate-off is at 213.3 ms
	const Hit Hits[Lanes] = {
		{{64, 0, 0, 0, 0, 0, 0, 64}, {}, 60, false, false},
		{{64, 64, 64, 64, 64, 0, 0, 64}, {}, 60, false, false},
		{{64, 127, 127, 127, 127, 1, 0, 127}, {}, 48, false, false},
		{{76, 32, 20, 8, 100, 0, 0, 40}, {}, 55, false, false},
		{{52, 96, 90, 30, 10, 1, 0, 90}, {}, 62, false, false},
		{{64, 10, 5, 100, 50, 0, 0, 20}, {}, 40, false, false},
		{{64, 64, 40, 64, 0, 1, 0, 110}, {}, 72, false, false},
		{{100, 50, 110, 20, 120, 0, 0, 70}, {}, 36, false, false},
		{{64, 64, 64, 64, 64, 0, 1, 90}, {}, 60, false, true},
		{{30, 110, 30, 90, 40, 1, 0, 60}, {}, 67, false, false},
		{{64, 70, 50, 50, 70, 0, 0, 96}, {128, 77, 200, 0, 33, 0, 0, 0}, 58, false, false},
		{{64, 64, 64, 64, 64, 1, 0, 100}, {}, 60, true, false},
		{{127, 40, 1, 40, 40, 0, 0, 80}, {}, 50, false, false},
		{{0, 90, 70, 127, 90, 1, 0, 85}, {}, 84, false, false},
		{{64, 120, 15, 80, 60, 1, 1, 120}, {}, 45, false, true},
		{{64, 64, 100, 0, 64, 0, 0, 30}, {}, 60, true, false},
	};

	constexpr double Seconds = 2;
	constexpr uint32_t FeedbackTable = 0x40026e38, DepthU3Table = 0x40027438, LowPassTable = 0x40026238;

	// The modulation depths the update sets, as words: unit 2's by unit 1, unit 3's by unit 1, and by unit 0 for the
	// table's word _d
	const int32_t DepthU2Word = frac(0x01000000, 0x1d70a3d7), DepthU3Word = frac(0x01000000, 0x20000000);
	int32_t depth2Word(const int32_t _d) { return frac(0x10000000, _d); }

	// A phase-modulation depth word in cycles per unit of input
	float cycles(const int32_t _depth) { return static_cast<float>(q31(_depth) * 128); }

	// _seconds of the 16 Hits from the fast engine at _fs (bench::fastHits), unit 0's feedback at 0 (_noFeedback)
	std::vector<std::vector<double>> renderFast(const double _fs, const double _seconds, const bool _noFeedback = false,
		const bool _fromTrig = false)
	{
		return fastHits<fast::SdBasic>(_fs, _seconds, Hits, [&](fast::SdBasic& _e)
		{
			if(_noFeedback)
				_e.testWithoutChaos();
		}, _fromTrig);
	}

	// The same from the exact port at 48 kHz, every Track on SD BASIC
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

		// The mix (0x400049f4) at SD BASIC's fixed gains, unit 2 at 1 and unit 3 at 0.5
		{
			Residual r;
			alignas(16) float gx[Lanes], gy[Lanes];
			std::fill_n(gx, Lanes, q(INT32_MAX));
			std::fill_n(gy, Lanes, q(0x40000000));
			fast::Block x, y, out;
			const uint32_t X = bufferA(2), Y = bufferA(3), Out = 0x80009c58;
			for(int b = 0; b < Blocks; ++b)
			{
				int32_t ref[Lanes][BlockSize];
				for(int l = 0; l < Lanes; ++l)
				{
					Voice v = st.voice(l);
					v.at(0x08) = v.at(0x18) = INT32_MAX;
					v.at(0x0c) = v.at(0x1c) = 0x40000000;
					for(int n = 0; n < BlockSize; ++n)
					{
						v.sram.at(X + 4 * n) = noise(0.6);
						v.sram.at(Y + 4 * n) = noise(0.6);
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

		// The modulator's VCA (0x40004674): a gain ramped from the previous block's level
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
					v.at(0x448) = level;
					v.at(0x44c) = prev[l];
					g0[l] = -q(prev[l]);
					g1[l] = -q(level);
					for(int n = 0; n < BlockSize; ++n)
					{
						ref[l][n] = noise(0.95);
						x.s[n][l] = q(ref[l][n]);
					}
					modulatorVca(ref[l], v);
					prev[l] = level;
				}
				fast::rampedGain(x, g0, g1);
				for(int l = 0; l < Lanes; ++l)
					for(int n = 0; n < BlockSize; ++n)
						r.add(q31(ref[l][n]), x.s[n][l]);
			}
			ok &= r.print("ramped gain (VCA)");
		}

		// Unit 2's high-pass (0x40004e6c) with SD BASIC's coefficients
		{
			Residual r;
			const int32_t c[3] = {0x4b9047ab, static_cast<int32_t>(0xb46fb855), static_cast<int32_t>(0x841f111a)};
			int32_t state[Lanes][8] = {};
			alignas(16) float x1[Lanes] = {}, y1[Lanes] = {};
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
						x.s[n][l] = q(ref[l][n]);
					std::copy_n(c, 3, state[l]);
					onePole(ref[l], state[l]);
				}
				fast::onePole(x, f, x1, y1);
				for(int l = 0; l < Lanes; ++l)
					for(int n = 0; n < BlockSize; ++n)
						r.add(q31(ref[l][n]), x.s[n][l]);
			}
			ok &= r.print("one-pole high-pass");
		}

		// Unit 3's low-pass (0x40004e14), each product truncated alone, its pole entry 127 on even lanes and 126 on
		// odd ones, as the update computes its coefficients
		{
			Residual r;
			int32_t state[Lanes][8] = {};
			alignas(16) float bb[Lanes], aa[Lanes], x1[Lanes] = {}, y1[Lanes] = {};
			for(int l = 0; l < Lanes; ++l)
			{
				const int32_t c2 = st.rom.entry(LowPassTable, 127 - (l & 1));
				state[l][2] = c2;
				state[l][0] = state[l][1] = frac(0x33333333, addWrap(c2 >> 1, 0x40000000));
				bb[l] = q(state[l][0]);
				aa[l] = -q(c2);
			}
			fast::Block x;
			for(int b = 0; b < Blocks; ++b)
			{
				int32_t ref[Lanes][BlockSize];
				for(int l = 0; l < Lanes; ++l)
				{
					for(int n = 0; n < BlockSize; ++n)
					{
						ref[l][n] = noise(0.45);
						x.s[n][l] = q(ref[l][n]);
					}
					sdBasic::firstOrder(ref[l], state[l]);
				}
				fast::onePoleLanes(x, bb, aa, x1, y1);
				for(int l = 0; l < Lanes; ++l)
					for(int n = 0; n < BlockSize; ++n)
						r.add(q31(ref[l][n]), x.s[n][l]);
			}
			ok &= r.print("one-pole low-pass");
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

		q3FeedbackSine(st, FeedbackTable, "parameter 3 at", ok, chaoticOk);	// SD BASIC's feedback table

		// A decaying modulator into _addr, as unit 1 and unit 0 under their envelopes feed units 2 and 3
		const auto modulator = [](Voice& _v, const uint32_t _addr, fast::Block& _in, const int _l, const int _b,
			const double _hz)
		{
			for(int n = 0; n <= BlockSize; ++n)
			{
				const double t = (_b * BlockSize + n) / 48000.0;
				const double m = 0.95 * std::exp(-t / (0.02 + 0.02 * _l)) * std::sin(2 * Pi * (_hz + 25 * _l) * t);
				_v.sram.at(_addr + 4 * n) = toQ31(m);
				if(n < BlockSize)
					_in.s[n][_l] = static_cast<float>(q31(_v.sram.at(_addr + 4 * n)));
			}
		};

		// Unit 2 (0x4000290e): the sine modulated by unit 1 at SD BASIC's depth
		{
			Runs r;
			alignas(16) uint32_t phase[Lanes] = {};
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
						v.unit(2, 0x9c) = static_cast<int32_t>(bufferA(1));
						v.unit(2, 0x8c) = v.unit(2, 0x94) = DepthU2Word;
						v.unit(2, 0x90) = v.unit(2, 0x98) = 0;
						v.unit(2, 0xb0) = static_cast<int32_t>(Sine);
					}
					v.unit(2, 0x78) = sweptIncrement(l, b, 1);
					for(auto& s : inc.s)
						s[l] = static_cast<uint32_t>(v.unit(2, 0x78)) << 8;
					modulator(v, bufferA(1), in, l, b, 60);
					pmOscillator(2, v, rom);
					for(int n = 0; n < BlockSize; ++n)
						r.ref[l].push_back(q31(v.sram.at(bufferB(2) + 4 * n)));
				}
				fast::pmSine(out, &in, phase, inc, cycles(DepthU2Word));
				for(int l = 0; l < Lanes; ++l)
					for(int n = 0; n < BlockSize; ++n)
						r.test[l].push_back(out.s[n][l]);
			}
			ok &= r.print("PM sine (unit 2)");
		}

		// Unit 3: the sine modulated by unit 1 at 0.25 and by unit 0 at a depth ramped from block to block, from the
		// table across its range (lane l at entry 8 l + 7, then at 127 - 8 l from 0.5 s)
		{
			Runs r;
			alignas(16) uint32_t phase[Lanes] = {};
			alignas(16) float depth2[Lanes] = {}, target[Lanes];
			fast::Increments inc;
			for(int b = 0; b < Blocks; ++b)
			{
				fast::Block in1, in0, out;
				for(int l = 0; l < Lanes; ++l)
				{
					Voice v = st.voice(l);
					if(b == 0)
					{
						v.unit(3, Voice::Phase) = 0;
						v.unit(3, 0x9c) = static_cast<int32_t>(bufferA(1));
						v.unit(3, 0xa0) = static_cast<int32_t>(bufferA(0));
						v.unit(3, 0x8c) = v.unit(3, 0x94) = DepthU3Word;
						v.unit(3, 0x98) = 0;
						v.unit(3, 0xb0) = static_cast<int32_t>(Sine);
					}
					const int32_t d = depth2Word(rom.entry(DepthU3Table, b < 750 ? 8 * l + 7 : 127 - 8 * l));
					v.unit(3, 0x90) = d;
					target[l] = cycles(d);
					v.unit(3, 0x78) = sweptIncrement(l, b, 3);
					for(auto& s : inc.s)
						s[l] = static_cast<uint32_t>(v.unit(3, 0x78)) << 8;
					modulator(v, bufferA(1), in1, l, b, 60);
					modulator(v, bufferA(0), in0, l, b, 95);
					pmOscillator(3, v, rom);
					for(int n = 0; n < BlockSize; ++n)
						r.ref[l].push_back(q31(v.sram.at(bufferB(3) + 4 * n)));
				}
				fast::pmSine2(out, in1, cycles(DepthU3Word), in0, depth2, target, phase, inc);
				for(int l = 0; l < Lanes; ++l)
					for(int n = 0; n < BlockSize; ++n)
						r.test[l].push_back(out.s[n][l]);
			}
			ok &= r.print("PM sine, two sources (unit 3)");
		}

		// The drive (0x400047ea) with SD BASIC's coefficients, on on lanes 0-7, off on 8-15, fed a full-scale decaying
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
						v.at(0x414) = v.at(0x418) = 0x01f10e1d;
						v.at(0x41c) = static_cast<int32_t>(0x83e21c3a);
						v.at(0x420) = v.at(0x424) = 0x001f7c5c;
						v.at(0x428) = static_cast<int32_t>(0x803ef8b8);
						v.at(Voice::DriveFloor) = on ? 0x40000000 : INT32_MAX;
						v.at(Voice::DrivePreGain) = on ? INT32_MAX : 0x20000000;
						v.at(Voice::DriveGain) = on ? 0x40000000 : INT32_MAX;
						v.at(Voice::DriveOn) = on;
						v.at(Voice::DriveX1) = v.at(Voice::DriveY1) = INT32_MAX;
					}
					from[l] = static_cast<float>(on ? q31(v.at(Voice::DriveY1)) : q31(INT32_MAX));
					drive[l] = on ? 4.0f : 1.0f;
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
		printCost<fast::SdBasic>("SD BASIC", Hits, costPerBlock(e, Hits, [&] { e.render(); }));
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
	deterministic(renderExact(withoutTable(s7, FeedbackTable), Seconds), renderFast(48000, Seconds, true));
	q15(renderFast(48000, Seconds + 0.05, false, true), renderFast(44100, Seconds + 0.05, false, true), Seconds);
	cost(s7);

	std::printf(g_failures ? "%d FAILED\n" : "all passed\n", g_failures);
	return g_failures ? 1 : 0;
}
