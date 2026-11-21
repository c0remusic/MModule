// FMD KICK (ticket 18 of .scratch/elektron-drums-v2) against the Model:Cycles OS 1.13's KICK run at its oracle
// (CYCLES_VECTORS: elektron-work's vectors-cycles, the voice's output per case, 1000 blocks of 32 at 48 kHz, trig mask
// at block 1; CYCLES_GATE_VECTORS: vectors-cycles-gate, GATE on and its note-offs), outside the repository:
//   - Q3: SNR over the case at least 60 dB and its worst 10 ms window at least 50; SHPE at its top excepted, where
//     operator 0's feedback is chaotic (no other implementation follows the OS's integers there): third octaves to
//     16 kHz over its first 0.5 s within 1 dB;
//   - GATE: the cases where a note-off releases the hold, at the block, judged as the others;
//   - 16 lanes on the same case give lane 0's samples, and 4 lanes rendered alone those of 16;
//   - Q15: 44.1 kHz through Host against 48 kHz: 10 ms envelopes within 0.2 dB, third octaves within 0.5 dB;
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

#include "cyFastKick.h"
#include "stFastMetrics.h"
#include "stFastResample.h"

namespace
{
	using cycles::fast::Kick;
	using cycles::fast::Block;
	using cycles::fast::BlockSize;
	using cycles::fast::Lanes;
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

	// A case: the words it changes from the machine's sound (the OS's word numbers), the blocks whose second mask is
	// set (a note-off), the trig mask at block 1
	struct Word
	{
		int word, value;
	};
	struct Case
	{
		const char* name;
		bool gateFolder;
		std::vector<Word> words;
		int offFrom, offTo;		// note-offs in blocks [offFrom, offTo)
		bool chaotic = false;
	};

	const std::vector<Case>& cases()
	{
		static const std::vector<Case> c{
			{"default", false, {}, 1, 65}, {"ptch-12", false, {{10, 0x3400}}, 1, 65},
			{"ptch+12", false, {{10, 0x4c00}}, 1, 65},
			{"dec08", false, {{18, 0x0800}}, 1, 65}, {"dec60", false, {{18, 0x6000}}, 1, 65},
			{"punch", false, {{15, 0x100}}, 1, 65},
			{"colr00", false, {{11, 0}}, 1, 65}, {"colr7f", false, {{11, 0x7f00}}, 1, 65},
			{"shpe00", false, {{12, 0}}, 1, 65},
			{"shpemax", false, {{12, 0x7f00}}, 1, 65, true}, {"swep00", false, {{13, 0}}, 1, 65},
			{"swep7f", false, {{13, 0x7f00}}, 1, 65}, {"cont00", false, {{14, 0}}, 1, 65},
			{"cont7f", false, {{14, 0x7f00}}, 1, 65},
			{"gate1", false, {}, 1, 2},
			{"gateon_off40", true, {{16, 0x100}}, 40, 41}, {"gateon_off2", true, {{16, 0x100}}, 2, 3},
			{"gateon_held", true, {{16, 0x100}}, 1, 65}, {"gateon_off1", true, {{16, 0x100}}, 1, 2},
			{"gateon_never", true, {{16, 0x100}}, 0, 0}, {"gateoff_off40", true, {}, 40, 41},
		};
		return c;
	}

	// The OS's word on Machine 1-8: PTCH 10, DEC 18, COLR 11, SHPE 12, SWEP 13, CONT 14, PUNCH 15 and GATE 16 (on/off)
	void apply(Kick& _k, const int _lane, const Case& _c)
	{
		constexpr int Words[]{10, 18, 11, 12, 13, 14, 15, 16};
		for(const auto& w : _c.words)
			for(int s = 0; s < Kick::Parameters; ++s)
				if(Words[s] == w.word)
				{
					const bool toggle = s == Kick::Punch || s == Kick::Gate;
					const int v = toggle ? (w.value ? 127 : 0) : w.value >> 8;
					const auto fine = static_cast<uint8_t>(toggle ? 0 : w.value & 255);
					_k.setParameter(_lane, s, static_cast<uint8_t>(v), fine);
				}
	}

	std::vector<double> render(Kick& _k, const int _lane, const Case& _c, const int _blocks)
	{
		std::vector<double> y;
		for(int b = 0; b < _blocks; ++b)
		{
			if(b == 1)
				_k.trig(_lane);
			if(b >= _c.offFrom && b < _c.offTo)
				_k.gateOff(_lane);
			const Block& out = _k.renderBlock();
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

	void oracleCases(const std::string& _base, const std::string& _gate)
	{
		std::printf("against the oracle: case, SNR dB, worst 10 ms dB, worst third octave dB\n");
		for(const auto& c : cases())
		{
			const auto voice = readVoice((c.gateFolder ? _gate : _base) + "/kick_" + c.name + ".voice.i32");
			auto k = std::make_unique<Kick>();
			apply(*k, 0, c);
			const auto y = render(*k, 0, c, static_cast<int>(voice.size()) / BlockSize);
			const double snr = snrOf(voice, y), window = worstWindow(voice, y), band = worstBand(voice, y);
			std::printf("  %-15s %7.1f %7.1f %7.3f%s\n", c.name, snr, window, band,
				c.chaotic ? "  (chaotic: bands)" : "");
			if(c.chaotic)
				require(band <= 1.0, std::string(c.name) + ": a third octave off by more than 1 dB");
			else
			{
				require(snr >= 60, std::string(c.name) + ": SNR under 60 dB");
				require(window >= 50, std::string(c.name) + ": a 10 ms window under 50 dB");
			}
		}
	}

	// ---- 16 lanes, 4 lanes ----
	void lanes()
	{
		auto one = std::make_unique<Kick>(), all = std::make_unique<Kick>(), four = std::make_unique<Kick>();
		four->setLanes(4);
		require(one->renderBlock().lanes == Lanes, "a new machine's block does not carry its 16 lanes");
		for(int b = 0; b < 300; ++b)
		{
			if(b == 1)
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
					require(m.s[n][l] == a.s[n][0], "a lane of 16 differs from the lane alone");
				for(int l = 0; l < 4; ++l)
					require(f.s[n][l] == a.s[n][0], "a lane of 4 differs from the lane alone");
				for(int l = 4; l < Lanes; ++l)
					require(f.s[n][l] == 0, "a lane left out is not silent");
			}
		}
		std::printf("PASS: 16 lanes and 4 lanes play the samples of 1\n");
	}

	// ---- Q15: 44.1 kHz through Host against 48 kHz, 16 lanes a semitone apart from 8 below ----
	void q15()
	{
		auto at48 = std::make_unique<Kick>();
		auto at44 = std::make_unique<syntakt::fast::Host<Kick>>(44100.0f);
		for(int l = 0; l < Lanes; ++l)
			for(auto* k : {static_cast<Kick*>(at48.get()), static_cast<Kick*>(at44.get())})
			{
				k->setParameter(l, Kick::Pitch, static_cast<uint8_t>(56 + l), 0);
				k->trig(l);
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
			const double env = bench::envelopeDiff(bench::envelope(x, 48000, 10), bench::envelope(y, 44100, 10));
			worstEnv = std::max(worstEnv, env);
			const auto ra = bench::bands(x, 48000, -17, 12), rb = bench::bands(y, 44100, -17, 12);
			const double loud = *std::max_element(ra.begin(), ra.end());
			for(size_t k = 0; k < ra.size(); ++k)
				if(ra[k] >= loud * 1e-6)
					worstBandDb = std::max(worstBandDb, std::abs(bench::db(rb[k] / ra[k])));
		}
		std::printf("Q15: 44.1 against 48 kHz, 16 lanes: 10 ms envelope within %.3f dB, third octaves to 16 kHz"
			" within %.3f dB\n",
			worstEnv, worstBandDb);
		require(worstEnv <= 0.2, "Q15: the envelope at 44.1 kHz is off by more than 0.2 dB");
		require(worstBandDb <= 0.5, "Q15: a third octave at 44.1 kHz is off by more than 0.5 dB");
	}

	// ---- the cost: 16 lanes playing, GATE on so that they keep sounding ----
	void cost()
	{
		auto k = std::make_unique<Kick>();
		for(int l = 0; l < Lanes; ++l)
		{
			k->setParameter(l, Kick::Gate, 127, 0);
			k->setParameter(l, Kick::Pitch, static_cast<uint8_t>(50 + l), 0);
			k->trig(l);
		}
		k->renderBlock();
		constexpr int Blocks = 3000;
		double best = 1e9;
		for(int run = 0; run < 5; ++run)
		{
			const auto t0 = std::chrono::steady_clock::now();
			for(int b = 0; b < Blocks; ++b)
				k->renderBlock();
			best = std::min(best, std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
		}
		const double share = 100 * best / (Blocks * BlockSize / 48000.0);
		std::printf("cost: 16 lanes playing, %.3f %% of a core at 48 kHz (%.3f %% a lane, %.2f us a block)\n", share,
			share / Lanes, 1e6 * best / Blocks);
	}
}

int main()
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	try
	{
		cost();
		lanes();
		q15();
		const char* base = std::getenv("CYCLES_VECTORS");
		const char* gate = std::getenv("CYCLES_GATE_VECTORS");
		if(!base || !*base || !gate || !*gate || !std::ifstream(std::string(base) + "/kick_default.voice.i32").good()
			|| !std::ifstream(std::string(gate) + "/kick_gateon_off40.voice.i32").good())
		{
			std::printf("SKIP: CYCLES_VECTORS or CYCLES_GATE_VECTORS does not name its folder\n");
			return 77;
		}
		oracleCases(base, gate);
	}
	catch(const std::exception& e)
	{
		std::printf("FAIL: %s\n", e.what());
		return 1;
	}
	std::printf("PASS\n");
	return 0;
}
