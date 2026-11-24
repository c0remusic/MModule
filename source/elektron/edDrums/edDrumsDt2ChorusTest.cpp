// The Digitakt II's send chorus (ticket 15 of .scratch/elektron-drums-v2):
//   - against the chorus run in its OS (DT2_VECTORS, vectors-dt2\sends: cho_*, dt2_send_oracle.py), from the state
//     the oracle starts it in (cho_port.txt, made by elektron-work's dt2-chorus\port_index.py): SNR over each case
//     >= 115 dB, the worst 10 ms window >= 100 dB (the port's tables are formulas, the DT2's words within 3e-8);
//   - Q15: a Hit through it at 44.1 kHz against 48 kHz at 6 settings: 10 ms envelopes within 0.2 dB, third octaves to
//     16 kHz within 0.5 dB;
//   - settled at its knobs, a change of DPTH or WDTH glides; the cost at 44.1 kHz.
// Exit 77 without the vectors, once the rest has run.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <xmmintrin.h>

#include "edDrumsDt2Chorus.h"
#include "stFastMetrics.h"

namespace
{
	using namespace elektron::drums;
	namespace bench = syntakt::fast::bench;
	constexpr int BlockSize = Dt2Chorus::BlockSize;

	void require(const bool _condition, const std::string& _message)
	{
		if(!_condition)
			throw std::runtime_error(_message);
	}

	float bitsToFloat(const std::string& _hex)
	{
		const auto bits = static_cast<uint32_t>(std::stoul(_hex, nullptr, 16));
		float f;
		std::memcpy(&f, &bits, sizeof f);
		return f;
	}

	// The oracle's starting state, in cho_port.txt's order (port_index.py)
	Dt2Chorus::State readState(std::istream& _in)
	{
		Dt2Chorus::State s;
		std::string tag, h;
		_in >> tag;
		require(tag == "state", "cho_port.txt: no state line");
		_in >> s.write;
		const auto next = [&] { _in >> h; return bitsToFloat(h); };
		s.delay[0] = next();
		s.delay[1] = next();
		s.phase = next();
		s.increment = next();
		s.triangle[0] = next();
		s.triangle[1] = next();
		s.lfo[0] = next();
		s.lfo[1] = next();
		s.depth = next();
		s.width = next();
		for(auto& f : s.highpass)
		{
			f[3] = next();
			f[4] = next();
		}
		for(auto& channel : s.lowpass)
			for(auto& z : channel)
				for(auto& v : z)
					v = next();
		s.glide = next();
		s.glideTarget = next();
		s.mix[0] = next();
		s.mix[1] = next();
		s.mixTarget = next();
		for(auto& line : s.line)
		{
			_in >> tag;
			for(auto& v : line)
				v = next();
		}
		return s;
	}

	std::vector<float> readFloats(const std::filesystem::path& _file)
	{
		std::ifstream in(_file, std::ios::binary);
		require(in.good(), "cannot read " + _file.string());
		in.seekg(0, std::ios::end);
		std::vector<float> v(static_cast<size_t>(in.tellg()) / sizeof(float));
		in.seekg(0);
		in.read(reinterpret_cast<char*>(v.data()), static_cast<std::streamsize>(v.size() * sizeof(float)));
		return v;
	}

	double snr(const std::vector<double>& _ref, const std::vector<double>& _test)
	{
		double residual = 0, signal = 0;
		for(size_t i = 0; i < _ref.size(); ++i)
		{
			residual += (_test[i] - _ref[i]) * (_test[i] - _ref[i]);
			signal += _ref[i] * _ref[i];
		}
		return residual == 0 ? 300 : 10 * std::log10(signal / residual);
	}

	bool vectors()
	{
		const char* dir = std::getenv("DT2_VECTORS");
		const auto sends = dir && *dir ? std::filesystem::path(dir) / "sends" : std::filesystem::path();
		if(sends.empty() || !std::filesystem::exists(sends / "cho_port.txt"))
		{
			std::printf("DT2_VECTORS is not set or holds no cho_port.txt: the chorus against its OS skipped\n");
			return false;
		}
		std::ifstream index(sends / "cho_port.txt");
		const auto start = readState(index);
		double worst = 300, worstLocal = 300;
		std::string worstCase;
		int cases = 0;
		std::string tag;
		while(index >> tag)
		{
			require(tag == "case", "cho_port.txt: a line is no case");
			std::string name;
			int blocks = 0, settle = 0, knobs[4], at = -1, changed[4];
			index >> name >> blocks >> settle;
			for(auto& k : knobs)
				index >> k;
			index >> at;
			for(auto& k : changed)
				index >> k;
			const auto frames = readFloats(sends / (name + ".f32"));
			require(frames.size() == static_cast<size_t>(blocks * BlockSize * 4),
				name + ": not 4 columns of the blocks");

			Dt2Chorus chorus(48000);
			const auto give = [&](const int (&_k)[4])
			{
				chorus.set(static_cast<uint8_t>(_k[0]), static_cast<uint8_t>(_k[1]), static_cast<uint8_t>(_k[2]),
					static_cast<uint8_t>(_k[3]));
			};
			give(knobs);
			chorus.state() = start;
			float zero[BlockSize]{}, outL[BlockSize], outR[BlockSize];
			for(int b = 0; b < settle; ++b)
				chorus.process(zero, zero, outL, outR);
			std::vector<double> ref[2], test[2];
			for(int b = 0; b < blocks; ++b)
			{
				if(b == at)
					give(changed);
				float inL[BlockSize], inR[BlockSize];
				for(int n = 0; n < BlockSize; ++n)
				{
					const size_t f = static_cast<size_t>(b * BlockSize + n) * 4;
					inL[n] = frames[f];
					inR[n] = frames[f + 1];
					ref[0].push_back(frames[f + 2]);
					ref[1].push_back(frames[f + 3]);
				}
				chorus.process(inL, inR, outL, outR);
				for(int n = 0; n < BlockSize; ++n)
				{
					test[0].push_back(outL[n]);
					test[1].push_back(outR[n]);
				}
			}
			for(int c = 0; c < 2; ++c)
			{
				const double s = snr(ref[c], test[c]);
				const double local = s >= 300 ? 300 : bench::localSnr(ref[c], test[c], 48000);
				if(s < worst || local < worstLocal)
					worstCase = name;
				worst = std::min(worst, s);
				worstLocal = std::min(worstLocal, local);
			}
			++cases;
		}
		std::printf("  against the DT2's OS, %d cases: SNR %.1f dB at worst (bound 115), worst 10 ms window %.1f dB "
			"(bound 100; %s)\n", cases, worst, worstLocal, worstCase.c_str());
		require(cases >= 15, "fewer than the 15 chorus cases");
		require(worst >= 115 && worstLocal >= 100, "the chorus is off the DT2's: " + worstCase);
		return true;
	}

	// ---- Q15 ----

	// A Hit as a function of time: 48 partials from 40 Hz to 15 kHz at fixed phases, a 2 ms attack and a 40 ms decay
	std::vector<float> hit(const double _rate, const double _seconds)
	{
		std::vector<float> x(static_cast<size_t>(_seconds * _rate));
		for(size_t i = 0; i < x.size(); ++i)
		{
			const double t = static_cast<double>(i) / _rate;
			double v = 0;
			for(int k = 0; k < 48; ++k)
				v += std::sin(2 * bench::Pi * 40 * std::pow(375.0, k / 47.0) * t + 2.4 * k * k) / std::sqrt(48.0);
			x[i] = static_cast<float>(0.3 * v * std::min(t / 0.002, 1.0) * std::exp(-t / 0.04));
		}
		return x;
	}

	// The left channel, the right fed the same
	std::vector<double> render(const double _rate, const int (&_k)[4], const double _seconds)
	{
		Dt2Chorus chorus(_rate);
		chorus.set(static_cast<uint8_t>(_k[0]), static_cast<uint8_t>(_k[1]), static_cast<uint8_t>(_k[2]),
			static_cast<uint8_t>(_k[3]));
		chorus.settle();
		const auto x = hit(_rate, _seconds);
		std::vector<double> y;
		float in[BlockSize], outL[BlockSize], outR[BlockSize];
		for(size_t at = 0; at < x.size(); at += BlockSize)
		{
			for(int n = 0; n < BlockSize; ++n)
				in[n] = at + static_cast<size_t>(n) < x.size() ? x[at + static_cast<size_t>(n)] : 0.0f;
			chorus.process(in, in, outL, outR);
			for(int n = 0; n < BlockSize; ++n)
				y.push_back(outL[n]);
		}
		y.resize(x.size());
		const auto taper = static_cast<size_t>(0.01 * _rate);
		for(size_t i = 0; i < taper; ++i)
			y[y.size() - 1 - i] *=
				0.5 - 0.5 * std::cos(bench::Pi * static_cast<double>(i) / static_cast<double>(taper));
		return y;
	}

	void rates()
	{
		constexpr double Seconds = 0.5;
		const int settings[][4]{{127, 32, 0, 127}, {0, 32, 0, 127}, {64, 127, 64, 64}, {127, 0, 127, 0},
			{127, 127, 0, 127}, {32, 64, 32, 32}};
		// Q15's 0.5 dB where the chorus at 48 kHz is within 15 dB of its loudest third octave; below, in its three
		// 7 kHz lowpasses' stopband (16 dB down at 8 kHz, 26 at 10 kHz, 45 at 12.7 kHz), the measured bounds the user
		// took on 2026-11-24: 1.0 dB to 30 dB down, 2.5 dB further. The DT2's bilinear without prewarp and its linear
		// read do not carry over to another rate exactly
		constexpr double Bounds[]{0.5, 1.0, 2.5};
		double worstEnv = 0, worstBand[3] = {}, worstAt[3] = {};
		for(const auto& k : settings)
		{
			const auto a = render(48000, k, Seconds), b = render(44100, k, Seconds);
			worstEnv = std::max(worstEnv,
				bench::envelopeDiff(bench::envelope(a, 48000, 10), bench::envelope(b, 44100, 10)));
			const auto ra = bench::bands(a, 48000, -17, 12), rb = bench::bands(b, 44100, -17, 12);
			const double loud = *std::max_element(ra.begin(), ra.end());
			for(size_t i = 0; i < ra.size(); ++i)
			{
				if(ra[i] < loud * 1e-6)
					continue;
				const size_t stop = ra[i] < loud * 1e-3 ? 2 : ra[i] < loud * std::pow(10.0, -1.5) ? 1 : 0;
				const double d = std::abs(bench::db(rb[i] / ra[i]));
				if(d > worstBand[stop])
				{
					worstBand[stop] = d;
					worstAt[stop] = 1000 * std::exp2((static_cast<int>(i) - 17) / 3.0);
				}
			}
		}
		std::printf("  Q15: 44.1 against 48 kHz, 6 settings: 10 ms envelope within %.3f dB (bound 0.2); third octaves "
			"to 16 kHz within %.3f dB (bound 0.5; %.0f Hz), 15 to 30 dB down within %.3f dB (bound 1.0; %.0f Hz), "
			"further within %.3f dB (bound 2.5; %.0f Hz)\n", worstEnv, worstBand[0], worstAt[0], worstBand[1],
			worstAt[1], worstBand[2], worstAt[2]);
		require(worstEnv <= 0.2 && worstBand[0] <= Bounds[0] && worstBand[1] <= Bounds[1] && worstBand[2] <= Bounds[2],
			"Q15: the chorus at 44.1 kHz is off its 48 kHz sound");
	}

	// Settled, DPTH and WDTH at their knobs at once; a change of DPTH after glides over blocks, not in one
	void glides()
	{
		Dt2Chorus chorus(44100);
		chorus.set(64, 32, 0, 96);
		chorus.settle();
		require(chorus.state().depth == Dt2Chorus::knob(64) && chorus.state().width == Dt2Chorus::knob(96)
			&& chorus.state().mix[0] == 1, "settle does not start at the knobs");
		float zero[BlockSize]{}, outL[BlockSize], outR[BlockSize];
		chorus.set(127, 32, 0, 96);
		chorus.process(zero, zero, outL, outR);
		const float once = chorus.state().depth;
		require(once > Dt2Chorus::knob(64) && once < Dt2Chorus::knob(127) * 0.2f + Dt2Chorus::knob(64),
			"DPTH does not glide");
		for(int b = 0; b < 400; ++b)
			chorus.process(zero, zero, outL, outR);
		require(std::abs(chorus.state().depth - Dt2Chorus::knob(127)) < 1e-4f, "DPTH does not reach its knob");
		std::printf("  settled at its knobs; DPTH 64 to 127 glides, %.4f after a block, there after 400\n", once);
	}

	void cost()
	{
		Dt2Chorus chorus(44100);
		float inL[BlockSize], inR[BlockSize], outL[BlockSize], outR[BlockSize];
		double worst = 0, total = 0;
		constexpr int Rounds = 20000;
		for(int r = 0; r < Rounds; ++r)
		{
			for(int n = 0; n < BlockSize; ++n)
			{
				inL[n] = static_cast<float>(0.5 * std::sin(0.01 * (r * BlockSize + n)));
				inR[n] = -inL[n];
			}
			const auto t0 = std::chrono::steady_clock::now();
			chorus.process(inL, inR, outL, outR);
			const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
			worst = std::max(worst, us);
			total += us;
		}
		std::printf("  the cost at 44.1 kHz: %.3f %% of a core, worst block %.1f us\n",
			total / Rounds / (BlockSize / 44100.0 * 1e6) * 100, worst);
	}
}

int main()
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	// Denormals flushed, as the plug-in's processBlock has them (ScopedNoDenormals) and the DT2's SHARC
	_mm_setcsr(_mm_getcsr() | 0x8040);
	try
	{
		glides();
		rates();
		cost();
		if(!vectors())
		{
			std::printf("PASS (no vectors)\n");
			return 77;
		}
		std::printf("PASS: the DT2's chorus, against its OS and at 44.1 kHz\n");
		return 0;
	}
	catch(const std::exception& e)
	{
		std::printf("FAIL: %s\n", e.what());
		return 1;
	}
}
