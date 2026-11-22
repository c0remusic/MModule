// FMD PERC and FMD TONE against the Model:Cycles OS 1.13's PERC and TONE run at its oracle (CYCLES_VECTORS:
// elektron-work's vectors-cycles, the voice's output per case, 1000 blocks of 32 at 48 kHz, trig mask at block 1;
// CYCLES_GATE_PERC_VECTORS, CYCLES_GATE_TONE_VECTORS: vectors-cycles-gate-perc and -tone, GATE on and its note-offs),
// outside the repository:
//   - Q3: SNR over the case at least 60 dB and its worst 10 ms window at least 50; TONE's SWEP at its top excepted,
//     where operator 0's feedback is chaotic (from SWEP about 72) and no port follows the samples: third octaves to
//     16 kHz over its first 0.5 s within ChaosBand dB, a bound the OS sets itself (see there);
//   - GATE: the cases where a note-off releases the hold, is lost in the trig's block, or never comes, judged as the
//     others;
//   - 16 lanes on the same case give lane 0's samples, with a retrig, and 4 lanes rendered alone those of 16;
//   - Q15: 44.1 kHz through Host against 48 kHz: 10 ms envelopes below 18 kHz within 0.2 dB, third octaves to 16 kHz
//     within 0.5 dB;
//   - the cost of 16 lanes playing, printed.
// Exit 77 without the vectors, once the rest has run.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "cyFastPerc.h"
#include "cyFastTone.h"
#include "stFastMetrics.h"
#include "stFastResample.h"

namespace
{
	using cycles::fast::Block;
	using cycles::fast::BlockSize;
	using cycles::fast::Lanes;
	using cycles::fast::Perc;
	using cycles::fast::Tone;
	namespace bench = syntakt::fast::bench;

	void require(const bool _condition, const std::string& _message)
	{
		if(!_condition)
			throw std::runtime_error(_message);
	}

	std::vector<double> readVoice(const std::string& _path)
	{
		std::ifstream f(_path, std::ios::binary);
		require(f.good(), "cannot read " + _path);
		std::vector<char> bytes{std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
		std::vector<int32_t> v(bytes.size() / 4);
		std::memcpy(v.data(), bytes.data(), v.size() * 4);
		std::vector<double> y(v.size());
		for(size_t i = 0; i < v.size(); ++i)
			y[i] = v[i] / 2147483648.0;
		return y;
	}

	// A case: the words it changes from the machine's sound (the OS's word numbers), its trig masks, the blocks whose
	// note-off bit is set
	struct Word
	{
		int word, value;
	};
	struct Case
	{
		const char* name;
		bool gateFolder;
		std::vector<Word> words;
		std::vector<int> trigs, offs;
		bool chaotic = false;
	};

	std::vector<int> range(const int _from, const int _to)
	{
		std::vector<int> r;
		for(int b = _from; b < _to; ++b)
			r.push_back(b);
		return r;
	}

	// The vectors' cases, the same names for both machines: the standard ones (note-offs in blocks 1 to 64, or 1 for
	// gate1; velocity never reaches the voice), then the GATE folder's
	std::vector<Case> cases(const bool _tone)
	{
		const auto all = range(1, 65);
		std::vector<Case> c{
			{"default", false, {}, {1}, all}, {"ptch-12", false, {{10, 0x3400}}, {1}, all},
			{"ptch+12", false, {{10, 0x4c00}}, {1}, all}, {"dec08", false, {{18, 0x0800}}, {1}, all},
			{"dec60", false, {{18, 0x6000}}, {1}, all}, {"punch", false, {{15, 0x100}}, {1}, all},
			{"colr00", false, {{11, 0}}, {1}, all}, {"colr7f", false, {{11, 0x7f00}}, {1}, all},
			{"shpe00", false, {{12, 0}}, {1}, all}, {"shpemax", false, {{12, 0x7f00}}, {1}, all},
			{"swep00", false, {{13, 0}}, {1}, all}, {"swep7f", false, {{13, 0x7f00}}, {1}, all, _tone},
			{"cont00", false, {{14, 0}}, {1}, all}, {"cont7f", false, {{14, 0x7f00}}, {1}, all},
			{"vel100", false, {}, {1}, all}, {"vel64", false, {}, {1}, all}, {"vel32", false, {}, {1}, all},
			{"gate1", false, {}, {1}, {1}},
			{"gateon_off40", true, {{16, 0x100}}, {1}, {40}}, {"gateon_off2", true, {{16, 0x100}}, {1}, {2}},
			{"gateon_off1", true, {{16, 0x100}}, {1}, {1}}, {"gateon_never", true, {{16, 0x100}}, {1}, {}},
			{"gateon_held", true, {{16, 0x100}}, {1}, all}, {"gateoff_off40", true, {}, {1}, {40}},
		};
		return c;
	}

	// The OS's word on Machine 1-8: PTCH 10, DEC 18, COLR 11, SHPE 12, SWEP 13, CONT 14, PUNCH 15 and GATE 16 (on/off)
	template<typename M> void apply(M& _m, const int _lane, const Case& _c)
	{
		constexpr int Words[]{10, 18, 11, 12, 13, 14, 15, 16};
		for(const auto& w : _c.words)
			for(int s = 0; s < M::Parameters; ++s)
				if(Words[s] == w.word)
				{
					const bool toggle = s == M::Punch || s == M::Gate;
					const int v = toggle ? (w.value ? 127 : 0) : w.value >> 8;
					const auto fine = static_cast<uint8_t>(toggle ? 0 : w.value & 255);
					_m.setParameter(_lane, s, static_cast<uint8_t>(v), fine);
				}
	}

	bool has(const std::vector<int>& _blocks, const int _b)
	{
		return std::find(_blocks.begin(), _blocks.end(), _b) != _blocks.end();
	}

	template<typename M> std::vector<double> render(M& _m, const int _lane, const Case& _c, const int _blocks)
	{
		std::vector<double> y;
		for(int b = 0; b < _blocks; ++b)
		{
			if(has(_c.trigs, b))
				_m.trig(_lane);
			if(has(_c.offs, b))
				_m.gateOff(_lane);
			const Block& out = _m.renderBlock();
			for(int n = 0; n < BlockSize; ++n)
				y.push_back(out.s[n][_lane]);
		}
		return y;
	}

	double snrOf(const std::vector<double>& _ref, const std::vector<double>& _test)
	{
		double s = 0, r = 0;
		for(size_t i = 0; i < _ref.size(); ++i)
		{
			s += _ref[i] * _ref[i];
			r += (_test[i] - _ref[i]) * (_test[i] - _ref[i]);
		}
		return 10 * std::log10(s / std::max(r, 1e-30));
	}

	// The worst 10 ms window from the onset, windows above -100 dBFS
	double worstWindow(const std::vector<double>& _ref, const std::vector<double>& _test)
	{
		constexpr size_t W = 480;
		double worst = 1e9;
		for(size_t a = 64; a + W <= _ref.size(); a += W)
		{
			double s = 0, r = 0;
			for(size_t i = a; i < a + W; ++i)
			{
				s += _ref[i] * _ref[i];
				r += (_test[i] - _ref[i]) * (_test[i] - _ref[i]);
			}
			if(s / W < 1e-10)
				continue;
			worst = std::min(worst, 10 * std::log10(s / std::max(r, 1e-30)));
		}
		return worst;
	}

	// Third octaves to 16 kHz over the first 0.5 s from the onset: the worst difference in dB where the oracle is
	// above its loudest - 60 dB
	double worstBand(const std::vector<double>& _ref, const std::vector<double>& _test)
	{
		const std::vector<double> a(_ref.begin() + 64, _ref.begin() + 64 + 24000);
		const std::vector<double> b(_test.begin() + 64, _test.begin() + 64 + 24000);
		const auto ra = bench::bands(a, 48000, -17, 12), rb = bench::bands(b, 48000, -17, 12);
		const double loud = *std::max_element(ra.begin(), ra.end());
		double worst = 0;
		for(size_t k = 0; k < ra.size(); ++k)
			if(ra[k] >= loud * 1e-6)
				worst = std::max(worst, std::abs(bench::db(rb[k] / ra[k])));
		return worst;
	}

	// A chaotic case's bound on worstBand: the OS's own spread. Its exact integer model (elektron-work's
	// cycles-perc-tone-chord/chaos_floor_bands.py), op 0's phase moved by 1, 2 or 7 LSB after the trig's block, lands
	// 2.95 to 4.74 dB off TONE swep7f's vector in this measure (4.20 and 4.74 dB for +1 and -1 LSB): no port can be
	// asked to do better than one LSB of the OS's own phase
	constexpr double ChaosBand = 4.75;

	template<typename M> void oracleCases(const char* _machine, const bool _tone, const std::string& _base,
		const std::string& _gate)
	{
		std::printf("%s against the oracle: case, SNR dB, worst 10 ms dB, worst third octave dB\n", _machine);
		std::vector<std::string> failed;
		for(const auto& c : cases(_tone))
		{
			const auto voice = readVoice((c.gateFolder ? _gate : _base) + "/" + _machine + "_" + c.name + ".voice.i32");
			auto m = std::make_unique<M>();
			apply(*m, 0, c);
			const auto y = render(*m, 0, c, static_cast<int>(voice.size()) / BlockSize);
			const double snr = snrOf(voice, y), window = worstWindow(voice, y), band = worstBand(voice, y);
			std::printf("  %-17s %7.1f %7.1f %7.3f%s\n", c.name, snr, window, band,
				c.chaotic ? "  (chaotic: bands)" : "");
			const std::string name = std::string(_machine) + " " + c.name;
			if(c.chaotic && band > ChaosBand)
				failed.push_back(name + ": a third octave off by more than the OS's own spread");
			if(!c.chaotic && snr < 60)
				failed.push_back(name + ": SNR under 60 dB");
			if(!c.chaotic && window < 50)
				failed.push_back(name + ": a 10 ms window under 50 dB");
		}
		std::string all;
		for(const auto& f : failed)
			all += (all.empty() ? "" : "; ") + f;
		require(failed.empty(), all);
	}

	// ---- 16 lanes, 4 lanes ----
	template<typename M> void lanes(const char* _machine)
	{
		auto one = std::make_unique<M>(), all = std::make_unique<M>(), four = std::make_unique<M>();
		four->setLanes(4);
		require(one->renderBlock().lanes == Lanes, std::string(_machine) + ": a new machine's block does not carry its"
			" 16 lanes");
		for(int b = 0; b < 300; ++b)
		{
			if(b == 1 || b == 200)
			{
				one->trig(0);
				for(int l = 0; l < Lanes; ++l)
					all->trig(l);
				for(int l = 0; l < 4; ++l)
					four->trig(l);
			}
			const Block& a = one->renderBlock();
			const Block& m = all->renderBlock();
			const Block& f = four->renderBlock();
			for(int n = 0; n < BlockSize; ++n)
			{
				for(int l = 0; l < Lanes; ++l)
					require(m.s[n][l] == a.s[n][0],
						std::string(_machine) + ": a lane of 16 differs from the lane alone");
				for(int l = 0; l < 4; ++l)
					require(f.s[n][l] == a.s[n][0],
						std::string(_machine) + ": a lane of 4 differs from the lane alone");
				for(int l = 4; l < Lanes; ++l)
					require(f.s[n][l] == 0, std::string(_machine) + ": a lane left out is not silent");
			}
		}
		std::printf("PASS: %s, 16 lanes and 4 lanes play the samples of 1\n", _machine);
	}

	// _x low-passed at 18 kHz, the same at any rate _fs (stFastBench.h's lowPass18k, which this test cannot link): a
	// zero-phase FIR, a sinc at 18 kHz under a Kaiser window (beta 8) 2 ms long, the signal taken as 0 outside
	std::vector<double> lowPass18k(const std::vector<double>& _x, const double _fs)
	{
		constexpr double Pi = 3.14159265358979323846, Cutoff = 18000, Beta = 8, Seconds = 0.002;
		const auto i0 = [](const double _v)
		{
			double sum = 1, term = 1;
			for(int k = 1; k < 60; ++k)
			{
				term *= (_v / (2 * k)) * (_v / (2 * k));
				sum += term;
			}
			return sum;
		};
		const int half = static_cast<int>(std::lround(Seconds * _fs / 2));
		std::vector<double> h(static_cast<size_t>(2 * half + 1));
		for(int j = -half; j <= half; ++j)
		{
			const double fc = Cutoff / _fs, r = static_cast<double>(j) / half;
			const double sinc = j == 0 ? 2 * fc : std::sin(2 * Pi * fc * j) / (Pi * j);
			h[static_cast<size_t>(j + half)] = sinc * i0(Beta * std::sqrt(std::max(0.0, 1 - r * r))) / i0(Beta);
		}
		std::vector<double> y(_x.size());
		const auto n = static_cast<int>(_x.size());
		for(int i = 0; i < n; ++i)
		{
			double s = 0;
			for(int j = std::max(-half, i - n + 1); j <= std::min(half, i); ++j)
				s += h[static_cast<size_t>(j + half)] * _x[static_cast<size_t>(i - j)];
			y[static_cast<size_t>(i)] = s;
		}
		return y;
	}

	// ---- Q15: 44.1 kHz through Host against 48 kHz, 16 lanes a semitone apart from 8 below ----
	template<typename M> void q15(const char* _machine)
	{
		auto at48 = std::make_unique<M>();
		auto at44 = std::make_unique<syntakt::fast::Host<M>>(44100.0f);
		for(int l = 0; l < Lanes; ++l)
			for(auto* m : {static_cast<M*>(at48.get()), static_cast<M*>(at44.get())})
			{
				m->setParameter(l, M::Pitch, static_cast<uint8_t>(56 + l), 0);
				m->trig(l);
			}
		constexpr double Seconds = 0.3;
		const int blocks48 = static_cast<int>(Seconds * 48000 / BlockSize) + 1;
		const int blocks44 = static_cast<int>(Seconds * 44100 / BlockSize) + 1;
		std::vector<std::vector<double>> a(Lanes), b(Lanes);
		for(int bl = 0; bl < blocks48; ++bl)
		{
			const Block& out = at48->renderBlock();
			for(int n = 0; n < BlockSize; ++n)
				for(int l = 0; l < Lanes; ++l)
					a[static_cast<size_t>(l)].push_back(out.s[n][l]);
		}
		std::vector<std::vector<float>> buffers(Lanes, std::vector<float>(BlockSize));
		std::vector<float*> outputs(Lanes);
		for(int l = 0; l < Lanes; ++l)
			outputs[static_cast<size_t>(l)] = buffers[static_cast<size_t>(l)].data();
		for(int bl = 0; bl < blocks44; ++bl)
		{
			at44->render(outputs.data());
			for(int n = 0; n < BlockSize; ++n)
				for(int l = 0; l < Lanes; ++l)
					b[static_cast<size_t>(l)].push_back(buffers[static_cast<size_t>(l)][static_cast<size_t>(n)]);
		}
		double worstEnv = 0, worstBandDb = 0;
		for(size_t l = 0; l < Lanes; ++l)
		{
			auto& x = a[l];
			auto& y = b[l];
			x.erase(x.begin(), x.begin() + static_cast<std::ptrdiff_t>(bench::onsetOf(x)));
			y.erase(y.begin(), y.begin() + static_cast<std::ptrdiff_t>(bench::onsetOf(y)));
			x.resize(static_cast<size_t>(0.25 * 48000));
			y.resize(static_cast<size_t>(0.25 * 44100));
			// The envelope below 18 kHz, the band 44.1 kHz holds
			const double env = bench::envelopeDiff(bench::envelope(lowPass18k(x, 48000), 48000, 10),
				bench::envelope(lowPass18k(y, 44100), 44100, 10));
			worstEnv = std::max(worstEnv, env);
			const auto ra = bench::bands(x, 48000, -17, 12), rb = bench::bands(y, 44100, -17, 12);
			const double loud = *std::max_element(ra.begin(), ra.end());
			for(size_t k = 0; k < ra.size(); ++k)
				if(ra[k] >= loud * 1e-6)
					worstBandDb = std::max(worstBandDb, std::abs(bench::db(rb[k] / ra[k])));
		}
		std::printf("Q15: %s, 44.1 against 48 kHz, 16 lanes: 10 ms envelope within %.3f dB, third octaves to 16 kHz"
			" within %.3f dB\n",
			_machine, worstEnv, worstBandDb);
		require(worstEnv <= 0.2, std::string(_machine) + " Q15: the envelope at 44.1 kHz is off by more than 0.2 dB");
		require(worstBandDb <= 0.5,
			std::string(_machine) + " Q15: a third octave at 44.1 kHz is off by more than 0.5 dB");
	}

	// ---- the cost: 16 lanes playing, GATE on so that they keep sounding ----
	template<typename M> void cost(const char* _machine)
	{
		auto m = std::make_unique<M>();
		for(int l = 0; l < Lanes; ++l)
		{
			m->setParameter(l, M::Gate, 127, 0);
			m->setParameter(l, M::Pitch, static_cast<uint8_t>(50 + l), 0);
			m->trig(l);
		}
		m->renderBlock();
		constexpr int Blocks = 3000;
		double best = 1e9;
		for(int run = 0; run < 5; ++run)
		{
			const auto t0 = std::chrono::steady_clock::now();
			for(int b = 0; b < Blocks; ++b)
				m->renderBlock();
			best = std::min(best, std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
		}
		const double share = 100 * best / (Blocks * BlockSize / 48000.0);
		std::printf("cost: %s, 16 lanes playing, %.3f %% of a core at 48 kHz (%.3f %% a lane, %.2f us a block)\n",
			_machine, share, share / Lanes, 1e6 * best / Blocks);
	}

	bool readable(const char* _folder, const std::string& _file)
	{
		return _folder && *_folder && std::ifstream(std::string(_folder) + "/" + _file).good();
	}
}

int main()
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	try
	{
		cost<Perc>("perc");
		cost<Tone>("tone");
		lanes<Perc>("perc");
		lanes<Tone>("tone");
		q15<Perc>("perc");
		q15<Tone>("tone");
		const char* base = std::getenv("CYCLES_VECTORS");
		const char* percGate = std::getenv("CYCLES_GATE_PERC_VECTORS");
		const char* toneGate = std::getenv("CYCLES_GATE_TONE_VECTORS");
		if(!readable(base, "perc_default.voice.i32") || !readable(base, "tone_default.voice.i32")
			|| !readable(percGate, "perc_gateon_off40.voice.i32") || !readable(toneGate, "tone_gateon_off40.voice.i32"))
		{
			std::printf("SKIP: CYCLES_VECTORS, CYCLES_GATE_PERC_VECTORS or CYCLES_GATE_TONE_VECTORS does not name its"
				" folder\n");
			return 77;
		}
		oracleCases<Perc>("perc", false, base, percGate);
		oracleCases<Tone>("tone", true, base, toneGate);
	}
	catch(const std::exception& e)
	{
		std::printf("FAIL: %s\n", e.what());
		return 1;
	}
	std::printf("PASS\n");
	return 0;
}
