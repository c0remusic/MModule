// syntakt::fast::SyChord, SY CHORD in float on 16 Tracks, measured against the exact integer port (stEngine) and
// against itself at 44.1 kHz, by the criteria BD MODERN's test set (stFastBdModernTest, stFastBench.h):
//	Q2	the two low-passes (0x40004e6c) against the integer stage fed the same input, their poles from the pole table
//		across its range: null residual RMS <= -100 dBFS and max <= -90 dBFS
//	Q3	the drive's clip against the integer stage fed the same input, over 1 s: SNR >= 70 dB and >= 60 dB in every
//		window of 480 samples above -60 dB, no residual third octave above -60 dB
//	Q4	the whole voice, 16 Hits over SY CHORD's parameters (2 s each), against syntakt::syChord at 48 kHz, by
//		bench::q4's measures: Hilbert envelope within -39 dB of the peak, short-time third octaves within 1 dB, pitch
//		within 1 cent, fatal; the null and the 1 ms envelope printed. SY CHORD has no feedback: nothing in it is
//		chaotic, so this Q4 is already deterministic and the voice needs no render without its feedback
//	Q15	the same Hits at 44.1 kHz, rendered at 48 and resampled (Host), against 48 kHz (bench::q15): envelope below
//		18 kHz within 0.2 dB, every third octave below 10 kHz within 0.5 dB, nothing excused
//	cost: microseconds per 32-sample block of 16 voices playing, fast against exact, and at 44.1 kHz
// The voice is checked word for word as its parameters change while it sounds: the wave by one index either way, by
// more and not at all, TUNE, the cutoff, DECAY, HOLD, the shape and the balance (changesWhileSounding). A Hit would be chaotic where the exact port, its phases 1 LSB on, failed Q4's
// measures against itself, and would be judged by its distributions (bench::Exacts): Q4 prints it, should one be. The
// firmware's words a stage runs with are read from the exact port's voice after its update, never written here.
//
// SYNTAKT_SECTION7 names the user's OS 1.41 section 7. Exit 77 without it.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <memory>
#include <random>
#include <utility>
#include <vector>

#include "stEmac.h"
#include "stEngine.h"
#include "stEngines.h"
#include "stFastBench.h"
#include "stFastSyChord.h"
#include "stFastTables.h"
#include "stMemoryMap.h"
#include "stSection7.h"

namespace
{
	using namespace syntakt;
	using fast::Lanes;
	using namespace fast::bench;

	constexpr int Machine = 5;	// SY CHORD's id

	// The 16 Hits: TUNE, the detune or balance, the shape, the wave, the cutoff, the drive, HOLD, DECAY. The shapes:
	// 0-2 (two, three and four units detuned, the detune at 0, 64, 120 and 127), chords with unit 3 silent (3, 5, 17,
	// 36) and not, past the last (50, and the retrigger's 127); the balance below 32 (units fading in) and above
	// (steps, with each unit an octave down or up); the waves at every kind of law (the sine at index 0, 8, the
	// harmonic lists 19-27, 31 and its copy), their position by the fines (Hit 10); DECAY at 0, the attacks of 30 and
	// 127. The retrigger (106.7 ms) sets the detune or balance, the shape, the wave and the drive to 127 and the note 2
	// up, in the block where the voice still sounds, fading: the wave moves by one index (Hit 11) and by 31 (Hit 15);
	// the gate-off is at 213.3 ms. The extremes: the pitch clamped at 0 (Hit 13), the note at 96 with units past 2217
	// Hz leaving the mix (Hit 14), and with units an octave down sounding, which the retrigger's octaves up take past
	// it, keeping their increments (Hit 9), TUNE at 0 and 127. The pitch clamped at 127 needs a unit that sounded
	// below 2217 Hz first: changesWhileSounding reaches it
	const HitSet Hits = {{
		{{64, 64, 0, 64, 64, 0, 0, 64}, {}, 48, false, false},
		{{64, 0, 0, 0, 0, 0, 0, 0}, {}, 48, false, false},
		{{64, 127, 2, 127, 127, 1, 0, 127}, {}, 48, false, false},
		{{76, 20, 1, 30, 100, 0, 0, 30}, {}, 40, false, false},
		{{52, 96, 3, 110, 10, 1, 0, 70}, {}, 52, false, false},
		{{64, 15, 8, 4, 127, 1, 0, 127}, {}, 36, false, false},
		{{64, 45, 12, 76, 0, 0, 0, 100}, {}, 45, false, false},
		{{100, 105, 20, 100, 120, 0, 0, 80}, {}, 30, false, false},
		{{64, 64, 17, 64, 64, 0, 1, 90}, {}, 48, false, true},
		{{64, 50, 7, 88, 40, 1, 0, 60}, {}, 96, true, false},
		{{64, 70, 30, 50, 70, 0, 0, 96}, {128, 77, 200, 150, 33, 0, 0, 0}, 48, false, false},
		{{64, 125, 37, 120, 64, 1, 0, 100}, {}, 48, true, false},
		{{127, 40, 36, 40, 40, 0, 0, 80}, {}, 20, false, false},
		{{0, 90, 5, 95, 127, 1, 0, 85}, {}, 10, false, false},
		{{64, 120, 2, 80, 60, 1, 1, 120}, {}, 110, true, true},
		{{64, 64, 50, 0, 64, 0, 0, 30}, {}, 48, true, false},
	}, {1, 2, 3, 5}, Machine};

	constexpr double Seconds = 2;
	constexpr uint32_t PoleTable = 0x40025608;

	// The voice's words the fast machine computes in integers (SyChord::testWords): the units' increments, the
	// filter's cutoff, sweep and pole, the crossfade's gains and index, the units' levels in the mix
	constexpr uint32_t Fields[] = {0x78, 0xf8, 0x178, 0x1f8, 0x3f4, 0x400, 0x4a4, 0xa8, 0xac, 0xc0, 0x08, 0x0c, 0x10,
		0x14};

	std::vector<std::vector<double>> renderFast(const double _fs, const double _seconds, const bool _fromTrig = false)
	{
		return fastHits<fast::SyChord>(_fs, _seconds, Hits, [](fast::SyChord&) {}, _fromTrig);
	}

	std::vector<std::vector<double>> renderExact(const std::vector<int32_t>& _s7, const double _seconds,
		const Nudge& _nudge = {})
	{
		return exactHits(_s7, _seconds, _nudge, Hits);
	}

	Voice updated(Stage& _st, const int _l, const int32_t _pitch, const std::vector<std::pair<int, uint8_t>>& _bytes)
	{
		return updatedVoice(_st, _l, _pitch, _bytes, syntakt::syChord::update);
	}

	// ---- Q2: the low-passes against their integer original ----

	// White noise at 0.45 (where neither one-pole's store saturates) into the two in cascade, each lane's pole the
	// table's at entry 8 l + 7 for the first 250 blocks, then at 127 - 8 l, the coefficients as the render computes them
	// from it
	void q2(const std::vector<int32_t>& _s7)
	{
		const Rom rom{_s7.data()};
		std::mt19937 rng(1234);
		std::uniform_real_distribution<double> u(-1, 1);
		constexpr int Blocks = 500;
		Residual r;
		int32_t c[Lanes][2][8] = {};
		alignas(16) float b[Lanes], a[Lanes], x1[2][Lanes] = {}, y1[2][Lanes] = {};
		fast::Block buf;
		for(int k = 0; k < Blocks; ++k)
		{
			int32_t ref[Lanes][BlockSize + 1];
			for(int l = 0; l < Lanes; ++l)
			{
				const int32_t c2 = rom.entry(PoleTable, k < Blocks / 2 ? 8 * l + 7 : 127 - 8 * l);
				const int32_t c0 = addWrap(c2 >> 1, 0x40000000);
				for(auto& s : c[l])
				{
					s[0] = s[1] = c0;
					s[2] = c2;
				}
				b[l] = static_cast<float>(q31(c0));
				a[l] = static_cast<float>(-q31(c2));
				for(int n = 0; n < BlockSize; ++n)
				{
					ref[l][n] = toQ31(0.45 * u(rng));
					buf.s[n][l] = static_cast<float>(q31(ref[l][n]));
				}
				ref[l][BlockSize] = 0;
				syntakt::onePole(ref[l], c[l][0]);
				syntakt::onePole(ref[l], c[l][1]);
			}
			for(int i = 0; i < 2; ++i)
				fast::onePoleLanes(buf, b, a, x1[i], y1[i]);
			for(int l = 0; l < Lanes; ++l)
				for(int n = 0; n < BlockSize; ++n)
					r.add(q31(ref[l][n]), buf.s[n][l]);
		}
		check(r.print("low-passes"), "Q2: the low-passes null to <= -100 dBFS RMS, <= -90 dBFS max");
	}

	// ---- Q3: the drive against its integer original ----

	void q3(const std::vector<int32_t>& _s7)
	{
		Stage st(_s7);
		// The drive (0x400047ea) with SY CHORD's words, as its update sets them at a trig with the drive on and off,
		// on lanes 0-7 and 8-15 (bench::q3DriveClip)
		const auto side = [&](const bool _on)
		{
			const Voice v = updated(st, 0, 48 << 16, {{12, static_cast<uint8_t>(_on)}});
			return DriveWords::Side{v.at(0x414), v.at(0x41c), v.at(0x420), v.at(0x428), v.at(Voice::DriveFloor),
				v.at(Voice::DrivePreGain), v.at(Voice::DriveGain)};
		};
		const bool ok = q3DriveClip(st, {side(true), side(false)}, "drive clip (on, off)");
		checkValidated(ok, "Q3: the drive's clip, SNR >= 70 dB, >= 60 dB in every window above -60 dB, no residual "
			"third octave above -60 dB");
	}

	// ---- the voice changed while it sounds ----

	// Every Track sounding 1 s from a trig while its parameters change, both engines given the same events. Tracks 0-7
	// on shapes 0-2 and chords, their wave moving every 4 blocks from block 4 by +1, -1, +3, -1, 0 and -2 indices (+4,
	// -4, +12, -4, 0, -8 in value, within 0-127) in turn, each from its own place in the turn, its fine at 37 t. Tracks
	// 8-15 change one thing every 8 blocks from block 8, cycling: 8 TUNE (to 127 at note 70, the pitch clamped at 127
	// and every unit past 2217 Hz keeping its increment), 9 the cutoff, 10 DECAY, 11 HOLD (with a retrigger and a
	// gate-off in one block, the gate-off lost in the trig's pending block, and a gate-off alone), 12 the shape through
	// 0, 18, 1, 21, 35, 7 and 2 (the units a shape leaves keeping the factors another set), 13 the shape between 0-2
	// and the detune with it, 14 TUNE at the note 96 (a chord whose units an octave down sound, then past 2217 Hz
	// keeping their increments), 15 the balance. The words (Fields) the exact port's every block, fatal; every Track by
	// Q4's fixed measures, fatal
	void changesWhileSounding(const std::vector<int32_t>& _s7)
	{
		constexpr int Steps[6] = {4, -4, 12, -4, 0, -8};
		struct Changes
		{
			int param;
			std::vector<int> values;
		};
		const Changes changes[8] = {
			{0, {76, 52, 127, 64, 100, 30}}, {4, {0, 127, 30, 90, 64, 10}}, {7, {20, 127, 35, 90, 64}},
			{6, {1, 1, 0, 1, 1, 0}}, {2, {0, 18, 1, 21, 35, 7, 2}}, {2, {1, 0, 2, 1, 2, 0}}, {0, {127, 64, 100, 64}},
			{1, {10, 25, 40, 70, 105, 127}},
		};
		const uint8_t base[8][3] = {{3, 64, 70}, {12, 64, 40}, {2, 100, 50}, {17, 64, 48}, {2, 60, 45}, {2, 80, 52},
			{7, 50, 96}, {21, 0, 44}};	// shape, the second parameter, the note
		Exact16 exact(_s7);
		auto machine = std::make_unique<fast::SyChord>();
		std::vector<std::vector<double>> ref(Lanes), test(Lanes);
		int wave[Lanes];
		int64_t words = 0, differ = 0;
		for(int t = 0; t < Lanes; ++t)
		{
			exact.setMachine(t, Machine);
			wave[t] = 20 + 6 * t;
		}
		for(int b = 0; b <= 48000 / BlockSize; ++b)
		{
			const auto both = [&](auto&& _f) { _f(exact); _f(*machine); };
			for(int t = 0; t < Lanes; ++t)
			{
				const auto fine = static_cast<uint8_t>(37 * t);
				const int c = t - 8;
				if(b == 0)
				{
					uint8_t v[8] = {64, static_cast<uint8_t>(8 * t), static_cast<uint8_t>(t % 3 + (t > 4 ? 4 * t : 0)),
						static_cast<uint8_t>(wave[t]), 64, 0, 0, 100};
					int note = 36 + 2 * t;
					if(c >= 0)
					{
						v[2] = base[c][0];
						v[1] = base[c][1];
						note = base[c][2];
					}
					both([&](auto& _e)
					{
						for(int i = 0; i < 8; ++i)
							_e.setParameter(t, i, v[i], i == 3 ? fine : 0);
						_e.setNote(t, note);
						_e.trig(t);
					});
				}
				else if(c < 0 && b % 4 == 0)
				{
					wave[t] = std::clamp(wave[t] + Steps[(b / 4 + t) % 6], 0, 127);
					both([&](auto& _e) { _e.setParameter(t, 3, static_cast<uint8_t>(wave[t]), fine); });
				}
				else if(c >= 0 && b % 8 == 0)
				{
					const int s = b / 8 - 1;
					const Changes& ch = changes[c];
					const auto value = static_cast<uint8_t>(ch.values[static_cast<size_t>(s) % ch.values.size()]);
					both([&](auto& _e)
					{
						_e.setParameter(t, ch.param, value, 0);
						if(c == 5)	// the detune with the shape
							_e.setParameter(t, 1, static_cast<uint8_t>(30 * s % 128), 0);
						if(c == 3 && s % 6 == 1)
						{
							_e.trig(t);
							_e.gateOff(t);
						}
						if(c == 3 && s % 6 == 3)
							_e.gateOff(t);
					});
				}
			}
			exact.render();
			const fast::Block& out = machine->renderBlock();
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
		std::printf("  changed while sounding: %lld words, %lld differ\n", static_cast<long long>(words),
			static_cast<long long>(differ));
		check(differ == 0, "the machine's words are the exact port's every block as its parameters change while it "
			"sounds");
		check(ok, "changed while sounding: every Track's Hilbert envelope within -39 dB of the peak, third octave cells "
			"within 1 dB");
	}

	// DECAY above 39 attacks as at 39: the exact port's word at DECAY 40 and 127 (its update, at a trig) and the fast
	// machine's table at 39, printed, equal, fatal
	void attackAbove39(const std::vector<int32_t>& _s7)
	{
		Stage st(_s7);
		const int32_t table = fast::tables::syChordAttack()[39];
		bool ok = true;
		for(const uint8_t d : {40, 127})
		{
			const int32_t word = updated(st, 0, 48 << 16, {{16, d}}).at(0x54);
			std::printf("  attack at DECAY %d: the exact port's word %d, the table's at 39 %d\n", d, word, table);
			ok &= word == table;
		}
		check(ok, "DECAY above 39: the units' attack word is the table's at 39");
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
	// Block by block (bench::blockByBlock), the words the machine computes in integers (Fields) the exact port's
	blockByBlock<fast::SyChord>(s7, Hits, "SY CHORD", [](fast::SyChord&) {},
		{std::begin(Fields), std::end(Fields)},
		[](const fast::SyChord& _m, const int _t, int32_t* _w) { _m.testWords(_t, _w); });
	changesWhileSounding(s7);
	attackAbove39(s7);
	const Exacts exact([&](const Nudge& _p) { return renderExact(s7, Seconds, _p); }, fast::SyChord::Units);
	q4(exact, renderFast(48000, Seconds));
	q15(renderFast(48000, Seconds + 0.05, true), renderFast(44100, Seconds + 0.05, true), Seconds);
	printCost<fast::SyChord>("SY CHORD", s7, Hits);

	std::printf(g_failures ? "%d FAILED\n" : "all passed\n", g_failures);
	return g_failures ? 1 : 0;
}
