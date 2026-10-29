// syntakt::fast::SyTone, SY TONE in float on 16 Tracks, measured against the exact integer port (stEngine) and against
// itself at 44.1 kHz, by the criteria BD MODERN's test set (stFastBdModernTest, stFastBench.h):
//	Q2	the decimator (0x400051c6) against the integer stage fed the same input: null residual RMS <= -100 dBFS and
//		max <= -90 dBFS
//	Q3	unit 1's phase-modulated sine at 96 kHz and the drive's clip against the integer stages fed the same input, over
//		1 s: SNR >= 70 dB and >= 60 dB in every window of 480 samples above -60 dB, no residual third octave above
//		-60 dB (unit 1's measured as if at 48 kHz: its windows are 5 ms, its bands an octave up); unit 0's sine feeding
//		back under its envelope at 96 kHz, which the fast machine runs in the machine's integers, equal to the exact
//		port's sample for sample (fatal)
//	Q4	the whole voice, 16 Hits over SY TONE's parameters (2 s each), against syntakt::syTone at 48 kHz, by bench::q4's
//		measures where not chaotic: Hilbert envelope within -39 dB of the peak, short-time third octaves within 1 dB,
//		pitch within 1 cent, fatal; the null and the 1 ms envelope printed. The same Hits with unit 0's feedback table
//		at 0 in both engines: the same measures, fatal; the null printed
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
#include <random>
#include <utility>
#include <vector>

#include "stEmac.h"
#include "stEngine.h"
#include "stEngines.h"
#include "stFastBench.h"
#include "stFastSyTone.h"
#include "stMemoryMap.h"
#include "stSection7.h"

namespace
{
	using namespace syntakt;
	using fast::Lanes;
	using namespace fast::bench;

	constexpr int Machine = 4;	// SY TONE's id

	// The 16 Hits: TUNE, RATIO, the modulation, the feedback, the modulator's decay and level at rest, the drive,
	// HOLD, DECAY. The retrigger (106.7 ms) sets RATIO, the modulation, the feedback and the drive to 127 and the note
	// 2 up, in the block where the voice still sounds, fading (unit 1's modulation then ramps from where it was, not
	// from 0); the gate-off is at 213.3 ms. The extremes: every parameter at 0 and 127, the pitch clamped at 127 (Hit
	// 12) and at 0 (Hit 13); RATIO among the designer's ratios below 1 (Hit 5, stFastTables' exception); the fines
	// between entries (Hit 10). The modulator's decay only where its table is exact (its words then the exact port's,
	// which the block by block checks)
	const HitSet Hits = {{
		{{64, 64, 64, 64, 64, 0, 0, 64}, {}, 48, false, false},
		{{64, 0, 0, 0, 0, 0, 0, 0}, {}, 48, false, false},
		{{64, 127, 127, 127, 127, 1, 0, 127}, {}, 48, false, false},
		{{76, 32, 96, 20, 100, 0, 0, 110}, {}, 40, false, false},
		{{52, 96, 32, 110, 10, 1, 0, 70}, {}, 52, false, false},
		{{64, 4, 127, 64, 64, 1, 0, 127}, {}, 36, false, false},
		{{64, 126, 0, 127, 0, 0, 0, 100}, {}, 45, false, false},
		{{100, 50, 110, 30, 120, 0, 0, 80}, {}, 30, false, false},
		{{64, 64, 64, 64, 64, 0, 1, 90}, {}, 48, false, true},
		{{30, 110, 30, 88, 40, 1, 0, 60}, {}, 55, false, false},
		{{64, 70, 50, 50, 70, 0, 0, 96}, {128, 77, 200, 0, 33, 0, 0, 0}, 48, false, false},
		{{64, 64, 64, 65, 64, 1, 0, 100}, {}, 48, true, false},
		{{127, 40, 16, 40, 40, 0, 0, 80}, {}, 72, false, false},
		{{0, 90, 95, 127, 88, 1, 0, 85}, {}, 10, false, false},
		{{64, 120, 15, 80, 60, 1, 1, 120}, {}, 42, true, true},
		{{64, 64, 100, 0, 64, 0, 0, 30}, {}, 48, true, false},
	}, {1, 2, 3, 5}, Machine};

	constexpr double Seconds = 2;
	constexpr uint32_t FeedbackTable = 0x40021404, IndexTable = 0x40021804;
	constexpr uint32_t Header = 0x20;	// the 8 words before an operator's samples in its buffers

	std::vector<std::vector<double>> renderFast(const double _fs, const double _seconds, const bool _noFeedback = false,
		const bool _fromTrig = false)
	{
		return fastHits<fast::SyTone>(_fs, _seconds, Hits, [&](fast::SyTone& _e)
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
		return updatedVoice(_st, _l, _pitch, _bytes, syntakt::syTone::update);
	}

	// ---- Q2: the decimator against its integer original ----

	// White noise at 0.45, the halved scale unit 1's buffer holds, into the decimator (0x400051c6) from an empty
	// history, its 64 samples a block; the fast decimator fed the same at full scale, which it halves
	void q2(const std::vector<int32_t>& _s7)
	{
		Stage st(_s7);
		std::mt19937 rng(1234);
		std::uniform_real_distribution<double> u(-1, 1);
		constexpr int Blocks = 500;
		Residual r;
		alignas(16) float x[8][Lanes] = {}, y[4][Lanes] = {};
		fast::Block in[2], out;
		for(int b = 0; b < Blocks; ++b)
		{
			int32_t ref[Lanes][BlockSize];
			for(int l = 0; l < Lanes; ++l)
			{
				Voice v = st.voice(l);
				if(b == 0)
					std::fill_n(v.sram.ptr(static_cast<uint32_t>(v.at(Voice::DecimatorHistory))), 12, 0);
				for(uint32_t n = 0; n < 2 * BlockSize; ++n)
				{
					const int32_t w = toQ31(0.45 * u(rng));
					v.sram.at(bufferB(1) + Header + 4 * n) = w;
					in[n / BlockSize].s[n % BlockSize][l] = static_cast<float>(2 * q31(w));
				}
				syntakt::syTone::decimate(v, bufferB(1), ref[l]);
			}
			fast::syTone::decimate(in, out, x, y);
			for(int l = 0; l < Lanes; ++l)
				for(int n = 0; n < BlockSize; ++n)
					r.add(q31(ref[l][n]), out.s[n][l]);
		}
		check(r.print("decimator"), "Q2: the decimator nulls to <= -100 dBFS RMS, <= -90 dBFS max");
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

		// Unit 0 (0x40003aac), which the fast machine runs in the machine's integers (syTone::feedbackUnit): the RAM
		// sine feeding back on its own output under an envelope that decays from full (time constant 0.1 to 1.2 s
		// across the lanes), the feedback from the table across its range (lane l at entry 8 l + 7, then at 127 - 8 l
		// from 0.5 s), up to 2 cycles per unit, where it is chaotic: every sample equal, fatal
		{
			alignas(16) uint32_t phase[Lanes] = {};
			int32_t fb[Lanes] = {}, fbWord[Lanes], last[Lanes] = {}, from[Lanes], to[Lanes];
			std::fill_n(from, Lanes, INT32_MIN);
			fast::Increments inc;
			int64_t samples = 0, differ = 0;
			for(int b = 0; b < Blocks; ++b)
			{
				fast::Block scaled[2];
				int32_t ref[Lanes][2 * BlockSize];
				for(int l = 0; l < Lanes; ++l)
				{
					Voice v = st.voice(l);
					if(b == 0)
					{
						v.unit(0, Voice::Phase) = 0;
						v.at(0x28) = v.at(0x30) = 0;
						v.unit(0, 0xb0) = static_cast<int32_t>(Sine);
						v.unit(0, Voice::EnvLevelPrev) = INT32_MIN;
					}
					fbWord[l] = v.at(0x2c) = rom.entry(FeedbackTable, b < 750 ? 8 * l + 7 : 127 - 8 * l);
					const double t = (b + 1) * BlockSize / 48000.0;
					to[l] = v.unit(0, Voice::EnvLevel) = toQ31(-std::exp(-t / (0.1 + 0.07 * l)));
					v.unit(0, 0x78) = sweptIncrement(l, b, 3) / 2;	// at 96 kHz
					for(auto& s : inc.s)
						s[l] = static_cast<uint32_t>(v.unit(0, 0x78)) << 8;
					syntakt::syTone::feedbackOperator(0, v, rom);
					std::copy_n(v.sram.ptr(static_cast<uint32_t>(v.unit(0, 0xc8)) + Header), 2 * BlockSize, ref[l]);
				}
				fast::syTone::feedbackUnit(scaled, phase, inc, fb, fbWord, last, from, to);
				for(int l = 0; l < Lanes; ++l)
				{
					differ += last[l] != st.voice(l).at(0x28);
					for(int n = 0; n < 2 * BlockSize; ++n, ++samples)
						differ += scaled[n / BlockSize].s[n % BlockSize][l] != static_cast<float>(q31(ref[l][n]));
				}
			}
			std::printf("  Q3 feedback sine (unit 0)     %lld samples, %lld differ (its last word too)\n",
				static_cast<long long>(samples), static_cast<long long>(differ));
			check(differ == 0, "Q3: unit 0's feedback sine in the machine's integers is the exact port's, every "
				"sample");
		}

		// Unit 1 (0x40002c16): the RAM sine modulated by a decaying wave at the halved scale unit 0 feeds it, at a
		// depth ramped from block to block, from the table across its range (lane l at entry 8 l + 7, then at 127 - 8 l
		// from 0.5 s); the fast one's output halved, as the machine's buffer holds it
		{
			Runs r;
			alignas(16) uint32_t phase[Lanes] = {};
			alignas(16) float from[Lanes] = {}, mid[Lanes], to[Lanes];
			fast::Increments inc;
			for(int b = 0; b < Blocks; ++b)
			{
				fast::Block in[2], out[2];
				for(int l = 0; l < Lanes; ++l)
				{
					Voice v = st.voice(l);
					if(b == 0)
					{
						v.unit(1, Voice::Phase) = 0;
						v.unit(1, 0x9c) = v.unit(1, 0xa0) = static_cast<int32_t>(bufferA(0));
						v.unit(1, 0x90) = v.unit(1, 0x94) = v.unit(1, 0x98) = 0;
						v.unit(1, 0xb0) = static_cast<int32_t>(Sine);
					}
					const int32_t d = frac(0x02000000, rom.entry(IndexTable, b < 750 ? 8 * l + 7 : 127 - 8 * l));
					v.unit(1, 0x8c) = d;
					to[l] = static_cast<float>(2.0 * d / 16777216.0);
					mid[l] = 0.5f * (from[l] + to[l]);
					v.unit(1, 0x78) = sweptIncrement(l, b, 1) / 2;
					for(auto& s : inc.s)
						s[l] = static_cast<uint32_t>(v.unit(1, 0x78)) << 8;
					for(uint32_t n = 0; n <= 2 * BlockSize; ++n)
					{
						const double t = (b * 2 * BlockSize + n) / 96000.0;
						const double m = 0.475 * std::exp(-t / (0.03 + 0.03 * l)) *
							std::sin(2 * Pi * (210 + 87 * l) * t);
						v.sram.at(bufferA(0) + Header + 4 * n) = toQ31(m);
						if(n < 2 * BlockSize)
							in[n / BlockSize].s[n % BlockSize][l] =
								static_cast<float>(q31(v.sram.at(bufferA(0) + Header + 4 * n)));
					}
					syntakt::syTone::pmOperator(1, v, rom);
					for(uint32_t n = 0; n < 2 * BlockSize; ++n)
						r.ref[l].push_back(q31(v.sram.at(static_cast<uint32_t>(v.unit(1, 0xcc)) + Header + 4 * n)));
				}
				fast::pmWave(out[0], &in[0], from, mid, phase, inc, sine.data());
				fast::pmWave(out[1], &in[1], mid, to, phase, inc, sine.data());
				std::copy_n(to, Lanes, from);
				for(int l = 0; l < Lanes; ++l)
					for(int n = 0; n < 2 * BlockSize; ++n)
						r.test[l].push_back(0.5 * out[n / BlockSize].s[n % BlockSize][l]);
			}
			ok &= r.print("PM sine (unit 1)");
		}

		// The drive (0x400047ea) with SY TONE's words, as its update sets them at a trig with the drive on and off, on
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
}

int main()
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	const auto s7 = loadSection7OrSkip();
	if(s7.empty())
		return 77;

	q2(s7);
	q3(s7);
	// Block by block (bench::blockByBlock), unit 0's words the exact port's: its envelope's level and its ramp's
	// start (0x40003c0e's, the voice's 0x7c and 0x80), its last output (0x28), its feedback (0x30)
	blockByBlock<fast::SyTone>(s7, Hits, "SY TONE", [](fast::SyTone&) {}, {0x7c, 0x80, 0x28, 0x30},
		[](const fast::SyTone& _m, const int _t, int32_t* _w) { _m.testWords(_t, _w); });
	const Exacts exact([&](const Nudge& _p) { return renderExact(s7, Seconds, _p); }, fast::SyTone::Units);
	q4(exact, renderFast(48000, Seconds));
	deterministic(renderExact(withoutTable(s7, FeedbackTable), Seconds), renderFast(48000, Seconds, true));
	q15(renderFast(48000, Seconds + 0.05, false, true), renderFast(44100, Seconds + 0.05, false, true),
		Seconds);
	printCost<fast::SyTone>("SY TONE", s7, Hits);

	std::printf(g_failures ? "%d FAILED\n" : "all passed\n", g_failures);
	return g_failures ? 1 : 0;
}
