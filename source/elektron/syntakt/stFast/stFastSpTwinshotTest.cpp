// syntakt::fast::SpTwinshot, SP TWINSHOT on 16 Tracks, measured against the exact integer port (stEngine) and against
// itself at 44.1 kHz, by the criteria BD MODERN's test set (stFastBdModernTest, stFastBench.h):
//	player	the sample player (0x40006764), in the machine's integers, against the integer stage on the same records:
//		every sample and every record word equal, every slot of the sample set forward and reversed, looping or not,
//		at rates from 1/32 to 64 samples a sample, the next slot changing every block
//	Q2	the band-pass (0x400068ea) below saturation against the integer stage fed the same input, the poles across
//		their table: null to <= -100 dBFS RMS, <= -90 dBFS max
//	Q3	the band-pass saturating (a full-scale square at open poles), over 1 s: SNR >= 70 dB and >= 60 dB in every
//		window of 480 samples above -60 dB, no residual third octave above -60 dB
//	Q4	the whole voice, 16 Hits over SP TWINSHOT's parameters and slots (2 s each), against syntakt::spTwinshot at
//		48 kHz, by bench::q4's measures: Hilbert envelope within -39 dB of the peak, short-time third octaves within
//		1 dB, pitch within 1 cent, fatal; the null and the 1 ms envelope printed. It has no oscillator: the bench's
//		nudges move nothing, and the fixed thresholds judge it. No feedback: no render without it
//	Q15	the same Hits at 44.1 kHz, rendered at 48 and resampled (Host), against 48 kHz (bench::q15)
//	cost: microseconds per 32-sample block of 16 voices playing, fast against exact, and at 44.1 kHz
// The voice is checked word for word block by block (bench::blockByBlock) and as its parameters change while it
// sounds (changesWhileSounding). The firmware's words a stage runs with are read from the exact port's tables, never
// written here.
//
// SYNTAKT_SECTION7 names the user's OS 1.41 section 7, SYNTAKT_SAMPLES the sample set the exact port's vectors were
// recorded with (twinshot_samples.bin). Exit 77 without them.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <memory>
#include <vector>

#include "stEngine.h"
#include "stEngines.h"
#include "stFastBench.h"
#include "stFastSpTwinshot.h"
#include "stSection7.h"

namespace
{
	using namespace syntakt;
	using fast::Lanes;
	using namespace fast::bench;
	namespace rec = fast::twinshotRecord;

	constexpr int Machine = 44;	// SP TWINSHOT: machine 44, the exact port's engine 11

	// The 16 Hits: TUNE and sample of the first player, the band-pass, the crossfade, TUNE and sample of the second,
	// HOLD, DECAY. A sample byte b plays slot b - 64 forward from 64, slot 63 - b reversed below. The sample set's
	// slots (twinshot_samples.py): every waveform kind (one to three samples, the ramp, the sine, the noise, the
	// decaying sine, the pulse), slot 7 empty, 10 longer than the data and than the clamp (reversed, it starts past
	// the position's range and stops), 62 a stretch of the ramp, 63 the loopable sine; slot 9, a length and no data,
	// is played in changesWhileSounding and by the player's check. Every Hit sounds. The band-pass at both ends,
	// beside and at its centre; the crossfade at 0, 64 and 127, with fines; HOLD (the second player loops) on by its value and by its fine alone,
	// forward and reversed; DECAY 127, which holds; the pitch clamped at 127 (Hit 12) and at 0 (Hit 13). The retrigger
	// (106.7 ms) sets the first sample, the crossfade and the second sample to 127 and the note 2 up; the gate-off is
	// at 213.3 ms
	const HitSet Hits = {{
		{{64, 70, 64, 64, 64, 67, 0, 64}, {}, 60, false, false},
		{{64, 0, 0, 0, 0, 0, 0, 0}, {}, 60, false, false},
		{{127, 127, 127, 127, 127, 127, 127, 127}, {0, 0, 200, 0, 77, 0, 1, 0}, 48, false, false},
		{{64, 65, 63, 32, 76, 69, 0, 100}, {}, 60, false, false},
		{{52, 58, 65, 96, 40, 57, 0, 70}, {}, 64, false, false},
		{{64, 74, 100, 64, 64, 127, 64, 110}, {}, 60, false, false},
		{{64, 53, 64, 64, 64, 75, 0, 90}, {}, 72, false, false},
		{{100, 71, 30, 100, 100, 77, 0, 80}, {}, 48, false, false},
		{{64, 126, 64, 64, 64, 1, 0, 90}, {0, 0, 0, 0, 0, 0, 1, 0}, 60, false, true},
		{{30, 66, 110, 90, 40, 75, 0, 60}, {}, 55, false, false},
		{{64, 70, 50, 50, 70, 50, 0, 96}, {128, 77, 200, 150, 33, 99, 0, 0}, 62, false, false},
		{{64, 67, 64, 65, 64, 64, 0, 100}, {}, 60, true, false},
		{{127, 69, 40, 0, 127, 70, 0, 80}, {}, 100, false, false},
		{{0, 69, 95, 127, 0, 70, 0, 85}, {}, 10, false, false},
		{{64, 72, 15, 80, 60, 9, 1, 120}, {}, 36, true, true},
		{{64, 68, 100, 0, 64, 127, 0, 30}, {}, 60, true, false},
	}, {1, 3, 5}, Machine};

	constexpr double Seconds = 2;

	// The voice's words (SpTwinshot::testWords): the two players' records, the band-pass's coefficients, the mix's
	// levels and where they ramp from, the amp's stage and release flag
	constexpr uint32_t RecordA = 0x6b0, RecordB = 0x6d4;
	const std::vector<uint32_t> Fields = []
	{
		std::vector<uint32_t> f;
		for(const uint32_t r : {RecordA, RecordB})
			for(uint32_t k = 0; k < rec::Words; ++k)
				f.push_back(r + 4 * k);
		for(const uint32_t o : {0x6f8u, 0x6fcu, 0x08u, 0x0cu, 0x18u, 0x1cu, 0x3dcu, 0x3e4u})
			f.push_back(o);
		return f;
	}();

	void words(const fast::SpTwinshot& _m, const int _t, int32_t* _w) { _m.testWords(_t, _w); }

	std::vector<std::vector<double>> renderFast(const double _fs, const double _seconds, const bool _fromTrig = false)
	{
		return fastHits<fast::SpTwinshot>(_fs, _seconds, Hits, [](fast::SpTwinshot&) {}, _fromTrig);
	}

	std::vector<std::vector<double>> renderExact(const std::vector<int32_t>& _s7, const double _seconds,
		const Nudge& _nudge = {})
	{
		return exactHits(_s7, _seconds, _nudge, Hits);
	}

	constexpr uint32_t PoleTable = 0x40014980;

	// ---- the player, bit for bit ----

	void player(const std::vector<int32_t>& _s7)
	{
		Stage st(_s7);
		const Samples& samples = *g_samples;
		const int32_t rates[] = {128, 1000, 4096, 4096 + 333, 9000, 65536, 262144};
		int64_t count = 0, differ = 0, sounding = 0;
		for(int c = 0; c < 256; ++c)
		{
			// Case c: slot c % 64, reversed for (c / 64) odd, looping from 128, at one of the rates; the next slot
			// moves every block as the update moves it, where its length is not 0
			const int slot = c % 64;
			const bool reverse = (c / 64) % 2, loop = c >= 128;
			int32_t r[rec::Words] = {};
			const auto length = [&](const int _s)
			{
				return static_cast<int32_t>(std::min<uint32_t>(samples.directory[static_cast<size_t>(_s)].length,
					0x7ffff) << 12);
			};
			const auto& e = samples.directory[static_cast<size_t>(slot)];
			r[rec::Pointer] = r[rec::NextPointer] = static_cast<int32_t>(e.pointer);
			r[rec::Length] = r[rec::NextLength] = length(slot);
			r[rec::Playing] = length(slot) != 0;
			r[rec::Position] = length(slot) && reverse ? static_cast<int32_t>(static_cast<uint32_t>(e.length - 1) << 12)
				: 0;
			r[rec::Rate] = rates[c % 7];
			r[rec::Reverse] = reverse;
			r[rec::Loop] = loop;
			int32_t exactRecord[rec::Words];
			std::copy(std::begin(r), std::end(r), exactRecord);
			for(int b = 0; b < 200; ++b)
			{
				const int next = (c + b / 3) % 64;
				if(length(next))
				{
					for(int32_t* x : {r, exactRecord})
					{
						x[rec::NextPointer] = static_cast<int32_t>(samples.directory[static_cast<size_t>(next)].pointer);
						x[rec::NextLength] = length(next);
					}
				}
				int32_t ref[BlockSize];
				spTwinshot::player(ref, exactRecord, st.rom);
				fast::Block out;
				fast::twinshotPlayer(out, 0, r, &g_twinshotSamples);
				for(int n = 0; n < BlockSize; ++n, ++count)
				{
					differ += static_cast<int64_t>(std::llround(out.s[n][0] * 2147483648.0)) != ref[n];
					sounding += ref[n] != 0;
				}
				for(int k = 0; k < rec::Words; ++k, ++count)
					differ += r[k] != exactRecord[k];
			}
		}
		std::printf("  player: %lld samples and record words, %lld sounding, %lld differ\n",
			static_cast<long long>(count), static_cast<long long>(sounding), static_cast<long long>(differ));
		check(differ == 0 && sounding > 0, "player: every sample and record word the exact port's, in the machine's "
			"integers");
	}

	// ---- Q2 and Q3: the band-pass ----

	// Lane l's poles: the low-pass from entry _lowFrom to open with the high-pass open (l < 8), then the high-pass
	// across with the low-pass open, as the band-pass's word sets them
	void poles(const Rom& _rom, const int _l, const int _lowFrom, int32_t* _c)
	{
		_c[0] = _rom.entry(PoleTable, _l < 8 ? _lowFrom + (127 - _lowFrom) * _l / 7 : 127);
		_c[1] = _rom.entry(PoleTable, _l < 8 ? 0 : 18 * (_l - 8) + 1);
	}

	// The band-pass over 1 s of _input(lane, sample index) (Q31 doubles), the integer stage and the float one, the
	// poles from _lowFrom (poles); returns how many blocks end with a state of the integer stage at full scale
	template<typename Input, typename Compare>
	int bandPassRun(const Rom& _rom, const int _lowFrom, Input&& _input, Compare&& _compare)
	{
		int saturated = 0;
		int32_t c[Lanes][4] = {};
		alignas(16) float c0[Lanes], c1[Lanes], s1[Lanes] = {}, s2[Lanes] = {};
		for(int l = 0; l < Lanes; ++l)
		{
			poles(_rom, l, _lowFrom, c[l]);
			c0[l] = static_cast<float>(q31(c[l][0]));
			c1[l] = static_cast<float>(q31(c[l][1]));
		}
		for(int b = 0; b < 1500; ++b)
		{
			fast::Block x;
			int32_t in[Lanes][BlockSize], out[Lanes][BlockSize];
			for(int l = 0; l < Lanes; ++l)
				for(int n = 0; n < BlockSize; ++n)
				{
					in[l][n] = toQ31(_input(l, b * BlockSize + n));
					x.s[n][l] = static_cast<float>(q31(in[l][n]));
				}
			for(int l = 0; l < Lanes; ++l)
			{
				spTwinshot::bandPass(in[l], out[l], c[l]);
				for(int k = 2; k < 4; ++k)
					saturated += c[l][k] == INT32_MAX || c[l][k] == INT32_MIN;
			}
			fast::twinshotBandPass(x, c0, c1, s1, s2);
			for(int l = 0; l < Lanes; ++l)
				for(int n = 0; n < BlockSize; ++n)
					_compare(l, q31(out[l][n]), x.s[n][l]);
		}
		return saturated;
	}

	void q2(const std::vector<int32_t>& _s7)
	{
		Stage st(_s7);
		Residual r;
		// Three decaying sines at a quarter of full scale: the stage stays below its saturation. The poles across the
		// table: where the low-pass is nearly closed, the integer stage's truncations (about -110 dBFS) are judged
		// here, absolutely, its output being too small for Q3's SNR
		const int saturated = bandPassRun(st.rom, 0, [](const int _l, const int _i)
		{
			const double t = _i / 48000.0;
			return 0.25 * std::exp(-t / (0.2 + 0.05 * _l)) * (std::sin(2 * Pi * (40 + 13 * _l) * t) +
				0.5 * std::sin(2 * Pi * (900 + 170 * _l) * t) + 0.3 * std::sin(2 * Pi * (7000 + 900 * _l) * t)) / 1.8;
		}, [&](int, const double _ref, const double _test) { r.add(_ref, _test); });
		check(r.print("band-pass") && !saturated, "Q2: the band-pass, unsaturated, null to <= -100 dBFS RMS, <= -90 "
			"dBFS max");
	}

	void q3(const std::vector<int32_t>& _s7)
	{
		Stage st(_s7);
		Runs r;
		// A full-scale square, its period lane by lane, the low-pass from half open: x - s and the states pass full
		// scale, and saturate
		const int saturated = bandPassRun(st.rom, 64, [](const int _l, const int _i)
		{
			return ((_i / (7 + 5 * _l)) % 2 ? 0.99 : -0.99) * std::exp(-_i / 48000.0 / 0.4);
		}, [&](const int _l, const double _ref, const double _test)
		{
			r.ref[_l].push_back(_ref);
			r.test[_l].push_back(_test);
		});
		std::printf("  Q3 band-pass: %d blocks end with a state saturated\n", saturated);
		check(r.print("band-pass, saturating") && saturated > 0, "Q3: the band-pass saturating, SNR >= 70 dB, "
			">= 60 dB in every window above -60 dB, no residual third octave above -60 dB");
	}

	// ---- the voice changed while it sounds ----

	// Every Track sounding 1 s from a trig while one of its parameters changes every 8 blocks from block 8, cycling
	// through its values: Track t and t + 8 parameter t, the second with a fine of 37 t on all but HOLD. TUNE to 0 and
	// 127 at notes that clamp the pitch, the samples across 64 (the direction turning while it plays) and onto the
	// empty slot and the one without data, the second while it loops (the change taken at its loop), the band-pass
	// across its centre, the crossfade, DECAY to 127 and back; HOLD (Tracks 6 and 14, 14 on by its fine alone) with a
	// retrigger every third change, a retrigger and a gate-off in one block, and a gate-off alone. The words (Fields)
	// the exact port's every block, fatal; every Track by Q4's fixed measures, fatal
	void changesWhileSounding(const std::vector<int32_t>& _s7)
	{
		const std::vector<int> values[2][8] = {
			{{127, 0, 76, 40, 100, 64}, {0, 63, 64, 127, 70, 57, 74, 53, 71}, {0, 127, 63, 64, 65, 110, 30},
				{0, 127, 64, 30, 90}, {127, 0, 64, 30, 90}, {127, 0, 73, 54, 71, 64, 66}, {1, 0, 1, 0, 0, 1},
				{20, 127, 35, 90, 64}},
			{{0, 127, 64, 30, 90}, {127, 64, 63, 0, 69, 58}, {64, 127, 0, 90}, {127, 0, 64}, {64, 127, 0, 90},
				{0, 127, 75, 62, 1}, {0, 0, 0, 0, 0, 0}, {127, 0, 50, 100}},
		};
		const int notes[2][8] = {{70, 48, 40, 52, 60, 44, 48, 36}, {10, 72, 55, 30, 100, 64, 50, 116}};
		Exact16 exact(_s7);
		auto machine = std::make_unique<fast::SpTwinshot>();
		machine->setSamples(&g_twinshotSamples);
		for(int t = 0; t < Lanes; ++t)
			exact.setMachine(t, Machine);
		std::vector<std::vector<double>> ref(Lanes), test(Lanes);
		int64_t count = 0, differ = 0;
		for(int b = 0; b <= 48000 / BlockSize; ++b)
		{
			const auto both = [&](auto&& _f) { _f(exact); _f(*machine); };
			for(int t = 0; t < Lanes; ++t)
			{
				const int p = t & 7, half = t >> 3;
				const auto fine = static_cast<uint8_t>(half && p != 6 ? 37 * t : 0);
				if(b == 0)
				{
					// The second player looping where its sample changes
					const uint8_t v[8] = {64, static_cast<uint8_t>(70 + t / 2), static_cast<uint8_t>(127 - 8 * t), 64,
						64, 127, static_cast<uint8_t>(p == 5), 100};
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
			exact.render();
			const fast::Block& out = machine->renderBlock();
			for(int t = 0; t < Lanes; ++t)
			{
				if(b >= 1)
				{
					std::vector<int32_t> w(Fields.size());
					words(*machine, t, w.data());
					for(size_t k = 0; k < Fields.size(); ++k, ++count)
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
		std::printf("  changed while sounding: %lld words, %lld differ\n", static_cast<long long>(count),
			static_cast<long long>(differ));
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
	if(s7.empty() || !loadSamplesOrSkip())
		return 77;

	player(s7);
	q2(s7);
	q3(s7);
	blockByBlock<fast::SpTwinshot>(s7, Hits, "SP TWINSHOT", [](fast::SpTwinshot&) {}, Fields, words);
	changesWhileSounding(s7);
	const Exacts exact([&](const Nudge& _p) { return renderExact(s7, Seconds, _p); }, fast::SpTwinshot::Units);
	q4(exact, renderFast(48000, Seconds));
	q15(renderFast(48000, Seconds + 0.05, true), renderFast(44100, Seconds + 0.05, true), Seconds);
	printCost<fast::SpTwinshot>("SP TWINSHOT", s7, Hits);

	std::printf(g_failures ? "%d FAILED\n" : "all passed\n", g_failures);
	return g_failures ? 1 : 0;
}
