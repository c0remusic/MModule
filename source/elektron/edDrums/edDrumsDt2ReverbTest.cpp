// The Digitakt II's send reverb, Dense Verb (ticket 16 of .scratch/elektron-drums-v2):
//   - against the float32 model its formulas come from (DT2_VECTORS, vectors-dt2\sends\dense: the "formula" cases of
//     dense_port.txt, made by elektron-work's dt2-reverb\dense_refs.py) at 48 and 44.1 kHz: the port itself;
//   - against the reverb on the DT2's own tables and lengths at 48 kHz (the "exact" cases, the model that matches the
//     reverb run in its OS bit for bit): the formulas' error, within the bounds the user took on 2026-11-24, third
//     octaves of the tail within 0.5 dB and each octave's T60 within 1 %;
//   - Q15, the bounds measured and taken on 2026-11-24: its lengths scaled to 44.1 kHz move its echoes, so 44.1
//     against 48 kHz is held to each octave's T60 within 3 %, 50 ms windows from 200 ms within 2 dB, third octaves to
//     10 kHz within 1.5 dB, at 12.7 and 16 kHz 2.5 dB (the first-order shelf's bilinear near Nyquist, kept);
//   - its cost at 44.1 kHz.
// Exit 77 without the vectors, once the rest has run.

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <xmmintrin.h>

#include "edDrumsDt2Reverb.h"
#include "stFastMetrics.h"

namespace
{
	using namespace elektron::drums;
	namespace bench = syntakt::fast::bench;
	constexpr int BlockSize = Dt2Reverb::BlockSize;
	constexpr double Seconds = 2;

	void require(const bool _condition, const std::string& _message)
	{
		if(!_condition)
			throw std::runtime_error(_message);
	}

	using Knobs = std::array<int, 6>;	// PRE DEC FREQ GAIN HPF LPF

	void give(Dt2Reverb& _reverb, const Knobs& _k)
	{
		_reverb.set(static_cast<uint8_t>(_k[0]), static_cast<uint8_t>(_k[1]), static_cast<uint8_t>(_k[2]),
			static_cast<uint8_t>(_k[3]), static_cast<uint8_t>(_k[4]), static_cast<uint8_t>(_k[5]));
	}

	struct Stereo
	{
		std::vector<double> c[2];
	};

	// _in fed to both channels after _settle silent blocks; at block _at the knobs become _after
	Stereo render(const double _rate, const std::vector<float>& _in, const Knobs& _knobs, const int _settle = 0,
		const int _at = -1, const Knobs& _after = {})
	{
		Dt2Reverb reverb(_rate);
		give(reverb, _knobs);
		float zero[BlockSize]{}, outL[BlockSize], outR[BlockSize];
		for(int b = 0; b < _settle; ++b)
			reverb.process(zero, zero, outL, outR);
		Stereo y;
		for(size_t at = 0, b = 0; at < _in.size(); at += BlockSize, ++b)
		{
			if(static_cast<int>(b) == _at)
				give(reverb, _after);
			float in[BlockSize];
			for(int n = 0; n < BlockSize; ++n)
				in[n] = at + static_cast<size_t>(n) < _in.size() ? _in[at + static_cast<size_t>(n)] : 0.0f;
			reverb.process(in, in, outL, outR);
			for(int n = 0; n < BlockSize; ++n)
			{
				y.c[0].push_back(outL[n]);
				y.c[1].push_back(outR[n]);
			}
		}
		for(auto& c : y.c)
			c.resize(_in.size());
		return y;
	}

	// The same energy per second at every rate
	std::vector<float> impulse(const double _rate, const double _seconds = Seconds)
	{
		std::vector<float> x(static_cast<size_t>(_seconds * _rate) / BlockSize * BlockSize);
		x[0] = static_cast<float>(0.5 * std::sqrt(_rate / 48000));
		return x;
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

	std::vector<double> from(const std::vector<double>& _x, const double _seconds, const double _rate)
	{
		return {_x.begin() + static_cast<std::ptrdiff_t>(_seconds * _rate), _x.end()};
	}

	// ---- the measures of dt2-reverb's measure.py ----

	constexpr double Octaves[]{125, 250, 500, 1000, 2000, 4000, 8000};

	// The octave [_low, _high] of _x through scipy's butter(4, bandpass) as measure.py runs it (sosfilt): the analog
	// Butterworth's poles moved to the band, then by the bilinear transform at edges prewarped; its zeros at z = 1
	// and -1. Its gain is left out: a decay in dB/s does not depend on it. A band cut square in the spectrum would
	// spread the early echoes over the tail through its sinc, and slow the fast decays
	std::vector<double> octave(const std::vector<double>& _x, const double _rate, const double _low, const double _high)
	{
		constexpr int Order = 4;
		const double fs2 = 2 * _rate, w1 = fs2 * std::tan(bench::Pi * _low / _rate),
			w2 = fs2 * std::tan(bench::Pi * _high / _rate), w0 = std::sqrt(w1 * w2), bw = w2 - w1;
		std::vector<double> y = _x;
		for(int k = 0; k < Order; ++k)
		{
			const std::complex<double> lp = std::polar(1.0, bench::Pi * (2 * k + Order + 1) / (2 * Order));
			const std::complex<double> half = lp * bw / 2.0, root = std::sqrt(half * half - w0 * w0);
			for(const auto& s : {half + root, half - root})
			{
				// One pole of each conjugate pair (both upper half-plane ones give the same biquads)
				if(s.imag() < 0)
					continue;
				const std::complex<double> z = (fs2 + s) / (fs2 - s);
				const double a1 = -2 * z.real(), a2 = std::norm(z);
				double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
				for(double& v : y)
				{
					const double out = v - x2 - a1 * y1 - a2 * y2;
					x2 = x1;
					x1 = v;
					y2 = y1;
					y1 = out;
					v = out;
				}
			}
		}
		return y;
	}

	// Each octave's decay in dB/s: the octave alone, its 10 ms levels from 100 ms on while within 50 dB of their
	// loudest there, a line through them
	std::vector<double> decays(const std::vector<double>& _x, const double _rate)
	{
		std::vector<double> out;
		for(const double centre : Octaves)
		{
			const auto y = octave(_x, _rate, centre / std::sqrt(2.0), std::min(centre * std::sqrt(2.0), 0.45 * _rate));
			const auto level = bench::envelope(y, _rate, 10);
			double loudest = -1e9;
			for(size_t k = 10; k < level.size(); ++k)
				loudest = std::max(loudest, bench::db(level[k]));
			double n = 0, st = 0, sl = 0, stt = 0, stl = 0;
			for(size_t k = 10; k < level.size(); ++k)
			{
				const double l = bench::db(level[k]), t = static_cast<double>(k) * 0.01;
				if(l <= loudest - 50)
					continue;
				n += 1;
				st += t;
				sl += l;
				stt += t * t;
				stl += t * l;
			}
			out.push_back((n * stl - st * sl) / (n * stt - st * st));
		}
		return out;
	}

	double t60(const double _rate)
	{
		return _rate < -1e-6 ? -60 / _rate : 1e9;
	}

	// measure.py's third octaves: 50 2^(k/3) Hz while the band's top is under 18 kHz (to 12.7 kHz), then 16 kHz
	std::vector<double> thirds()
	{
		std::vector<double> centres;
		for(int k = 0; k < 25; ++k)
			if(50 * std::exp2(k / 3.0) * std::exp2(1 / 6.0) < 18000)
				centres.push_back(50 * std::exp2(k / 3.0));
		centres.push_back(16000);
		return centres;
	}

	// The third octaves of the tail from 100 ms, under a Hann window, as measure.py's: their level difference in dB,
	// _test's less _ref's. Each band's energy per second, the spectrum's bins in [fc 2^(-1/6), fc 2^(1/6))
	std::vector<double> bandDiffs(const std::vector<double>& _ref, const double _refRate,
		const std::vector<double>& _test, const double _testRate)
	{
		const auto centres = thirds();
		const auto bandsOf = [&](std::vector<double> _x, const double _rate)
		{
			_x = from(_x, 0.1, _rate);
			const double last = static_cast<double>(_x.size() - 1);
			for(size_t i = 0; i < _x.size(); ++i)
				_x[i] *= 0.5 - 0.5 * std::cos(2 * bench::Pi * static_cast<double>(i) / last);
			const auto p = bench::powerSpectrum(_x, size_t(1) << 20);
			const double n = 2.0 * static_cast<double>(p.size());
			std::vector<double> e(centres.size(), 0.0);
			for(size_t k = 1; k < p.size(); ++k)
			{
				const double f = static_cast<double>(k) * _rate / n;
				for(size_t b = 0; b < centres.size(); ++b)
					if(f >= centres[b] * std::exp2(-1 / 6.0) && f < centres[b] * std::exp2(1 / 6.0))
						e[b] += p[k] / (n * _rate);
			}
			return e;
		};
		const auto a = bandsOf(_ref, _refRate), b = bandsOf(_test, _testRate);
		std::vector<double> d(a.size());
		for(size_t i = 0; i < a.size(); ++i)
			d[i] = bench::db(b[i] / a[i]);
		return d;
	}

	struct Compared
	{
		double envelope = 0, envelope50 = 0, t60 = 0, decay = 0, bands = 0, bands10k = 0, bandsTop = 0;
		double t60At = 0, bandAt = 0;
	};

	// _ref against _test: 10 ms windows (both channels), 50 ms windows from 200 ms; the left channel's T60 per octave
	// and third octaves
	Compared compare(const Stereo& _ref, const double _refRate, const Stereo& _test, const double _testRate)
	{
		Compared r;
		for(int c = 0; c < 2; ++c)
		{
			r.envelope = std::max(r.envelope, bench::envelopeDiff(bench::envelope(_ref.c[c], _refRate, 10),
				bench::envelope(_test.c[c], _testRate, 10)));
			r.envelope50 = std::max(r.envelope50, bench::envelopeDiff(
				bench::envelope(from(_ref.c[c], 0.2, _refRate), _refRate, 50),
				bench::envelope(from(_test.c[c], 0.2, _testRate), _testRate, 50)));
		}
		const auto da = decays(_ref.c[0], _refRate), db = decays(_test.c[0], _testRate);
		for(size_t i = 0; i < da.size(); ++i)
		{
			r.decay = std::max(r.decay, std::abs(db[i] - da[i]));
			const double ta = t60(da[i]), tb = t60(db[i]);
			if(ta < 60 && tb < 60 && std::abs(tb / ta - 1) * 100 > r.t60)
			{
				r.t60 = std::abs(tb / ta - 1) * 100;
				r.t60At = Octaves[i];
			}
		}
		const auto d = bandDiffs(_ref.c[0], _refRate, _test.c[0], _testRate);
		const auto centres = thirds();
		for(size_t i = 0; i < d.size(); ++i)
		{
			const double v = std::abs(d[i]);
			if(v > r.bands)
			{
				r.bands = v;
				r.bandAt = centres[i];
			}
			if(centres[i] < 10500)
				r.bands10k = std::max(r.bands10k, v);
			else
				r.bandsTop = std::max(r.bandsTop, v);
		}
		return r;
	}

	// ---- against the models ----

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

	bool vectors()
	{
		const char* dir = std::getenv("DT2_VECTORS");
		const auto dense = dir && *dir ? std::filesystem::path(dir) / "sends" / "dense" : std::filesystem::path();
		if(dense.empty() || !std::filesystem::exists(dense / "dense_port.txt"))
		{
			std::printf("DT2_VECTORS is not set or holds no sends\\dense\\dense_port.txt: the reverb against its "
				"models skipped\n");
			return false;
		}
		std::ifstream index(dense / "dense_port.txt");
		std::string tag;
		int formulas = 0, exacts = 0;
		double worstSnr = 300, worstLocal = 300, worstBands = 0, worstT60 = 0, exactSnr = 300, exactEnv = 0;
		std::string worstCase, worstBandCase, worstT60Case;
		while(index >> tag)
		{
			require(tag == "case", "dense_port.txt: a line is no case");
			std::string name, model;
			double rate = 0;
			size_t frames = 0;
			int settle = 0, at = -1;
			Knobs knobs{}, after{};
			index >> name >> model >> rate >> frames >> settle;
			for(auto& k : knobs)
				index >> k;
			index >> at;
			for(auto& k : after)
				index >> k;
			const auto data = readFloats(dense / (name + ".f32"));
			require(data.size() == frames * 3, name + ": not 3 columns of its frames");
			std::vector<float> in(frames);
			Stereo ref;
			for(size_t i = 0; i < frames; ++i)
			{
				in[i] = data[i * 3];
				ref.c[0].push_back(data[i * 3 + 1]);
				ref.c[1].push_back(data[i * 3 + 2]);
			}
			const auto test = render(rate, in, knobs, settle, at, after);
			if(model == "formula")
			{
				for(int c = 0; c < 2; ++c)
				{
					const double s = snr(ref.c[c], test.c[c]), local = bench::localSnr(ref.c[c], test.c[c], rate);
					if(s < worstSnr || local < worstLocal)
						worstCase = name;
					worstSnr = std::min(worstSnr, s);
					worstLocal = std::min(worstLocal, local);
				}
				++formulas;
				continue;
			}
			require(model == "exact" && rate == 48000, name + ": an unknown model");
			const auto r = compare(ref, rate, test, rate);
			exactSnr = std::min({exactSnr, snr(ref.c[0], test.c[0]), snr(ref.c[1], test.c[1])});
			exactEnv = std::max(exactEnv, r.envelope);
			if(r.bands > worstBands)
			{
				worstBands = r.bands;
				worstBandCase = name + " at " + std::to_string(static_cast<int>(r.bandAt)) + " Hz";
			}
			if(r.t60 > worstT60)
			{
				worstT60 = r.t60;
				worstT60Case = name + " at " + std::to_string(static_cast<int>(r.t60At)) + " Hz";
			}
			++exacts;
		}
		std::printf("  against its formulas' model, %d cases at 48 and 44.1 kHz: SNR %.1f dB at worst (bound 100), "
			"worst 10 ms window %.1f dB (bound 80; %s)\n", formulas, worstSnr, worstLocal, worstCase.c_str());
		std::printf("  against the DT2's tables, %d cases at 48 kHz: third octaves of the tail within %.3f dB (bound "
			"0.5; %s), each octave's T60 within %.2f %% (bound 1; %s); SNR %.1f dB at worst, 10 ms windows within "
			"%.3f dB\n", exacts, worstBands, worstBandCase.c_str(), worstT60, worstT60Case.c_str(), exactSnr,
			exactEnv);
		require(formulas >= 36 && exacts >= 16, "fewer cases than dense_refs.py writes");
		require(worstSnr >= 100 && worstLocal >= 80, "the port is off its model: " + worstCase);
		require(worstBands <= 0.5 && worstT60 <= 1, "the formulas are off the DT2's tables");
		return true;
	}

	// ---- Q15 ----

	void rates()
	{
		// measure.py's settings: PRE DEC FREQ GAIN HPF LPF
		struct Setting
		{
			const char* name;
			Knobs knobs;
		};
		const Setting settings[]{{"defaults", {16, 31, 64, 50, 32, 96}}, {"DEC 0", {16, 0, 64, 50, 32, 96}},
			{"DEC 64", {16, 64, 64, 50, 32, 96}}, {"DEC 100", {16, 100, 64, 50, 32, 96}},
			{"DEC 120", {16, 120, 64, 50, 32, 96}}, {"DEC 127", {16, 127, 64, 50, 32, 96}},
			{"FREQ 0", {16, 31, 0, 50, 32, 96}}, {"FREQ 127", {16, 31, 127, 50, 32, 96}},
			{"GAIN 0", {16, 31, 64, 0, 32, 96}}, {"GAIN 127", {16, 31, 64, 127, 32, 96}},
			{"HPF 0", {16, 31, 64, 50, 0, 96}}, {"HPF 127", {16, 31, 64, 50, 127, 96}},
			{"HPF 0 LPF 0", {16, 31, 64, 50, 0, 0}}, {"HPF 0 LPF 64", {16, 31, 64, 50, 0, 64}},
			{"PRE 0", {0, 31, 64, 50, 32, 96}}};
		Compared worst;
		std::string at[4];
		for(const auto& s : settings)
		{
			const auto& k = s.knobs;
			const auto a = render(48000, impulse(48000), k), b = render(44100, impulse(44100), k);
			auto r = compare(a, 48000, b, 44100);
			// From DEC 100 on, the T60s (6 to 45 s) need more than 2 s to be told apart: 10 s
			if(k[1] >= 100)
				r.t60 = compare(render(48000, impulse(48000, 10), k), 48000, render(44100, impulse(44100, 10), k),
					44100).t60;
			const auto note = [&](double& _worst, const double _v, std::string& _at)
			{
				if(_v > _worst)
				{
					_worst = _v;
					_at = s.name;
				}
			};
			note(worst.t60, r.t60, at[0]);
			note(worst.envelope50, r.envelope50, at[1]);
			note(worst.bands10k, r.bands10k, at[2]);
			note(worst.bandsTop, r.bandsTop, at[3]);
			worst.decay = std::max(worst.decay, r.decay);
			worst.envelope = std::max(worst.envelope, r.envelope);
		}
		std::printf("  Q15: 44.1 against 48 kHz, %zu settings: T60 per octave within %.2f %% (bound 3; %s), 50 ms "
			"windows from 200 ms within %.2f dB (bound 2; %s), third octaves to 10 kHz within %.2f dB (bound 1.5; %s), "
			"at 12.7 and 16 kHz %.2f dB (bound 2.5; %s); decays within %.2f dB/s, 10 ms windows within %.2f dB\n",
			std::size(settings), worst.t60, at[0].c_str(), worst.envelope50, at[1].c_str(), worst.bands10k,
			at[2].c_str(), worst.bandsTop, at[3].c_str(), worst.decay, worst.envelope);
		require(worst.t60 <= 3 && worst.envelope50 <= 2 && worst.bands10k <= 1.5 && worst.bandsTop <= 2.5,
			"Q15: the reverb at 44.1 kHz is off its 48 kHz sound");
	}

	void cost()
	{
		Dt2Reverb reverb(44100);
		float inL[BlockSize], inR[BlockSize], outL[BlockSize], outR[BlockSize];
		double worst = 0, total = 0;
		constexpr int Rounds = 20000;
		for(int r = 0; r < Rounds; ++r)
		{
			for(int n = 0; n < BlockSize; ++n)
			{
				inL[n] = static_cast<float>(0.5 * std::sin(0.01 * (r * BlockSize + n)));
				inR[n] = -0.5f * inL[n];
			}
			const auto t0 = std::chrono::steady_clock::now();
			reverb.process(inL, inR, outL, outR);
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
		const bool found = vectors();
		rates();
		cost();
		if(!found)
		{
			std::printf("PASS (no vectors)\n");
			return 77;
		}
		std::printf("PASS: the DT2's reverb, against its models and at 44.1 kHz\n");
		return 0;
	}
	catch(const std::exception& e)
	{
		std::printf("FAIL: %s\n", e.what());
		return 1;
	}
}
