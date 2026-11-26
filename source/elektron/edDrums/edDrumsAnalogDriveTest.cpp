// ANALOG ≈ (ticket 02 of .scratch/elektron-drums-v2): the Syntakt's Analog FX DRIVE approached as a Track drive, judged
// by the behaviours the model is to give (.scratch/lockstep-post-v1/issues/02-modele-overdrive-master.md):
//   - its laws: the gain 0 to +30 dB, the coupling high-pass's cutoff;
//   - monotone: a tone's output rises with DRIVE, at a low level and at a high one;
//   - THD+N rises with the input's level;
//   - compression: the output's level over the input's, its slope near 1 when quiet and under 0.3 near full scale;
//   - the low end: 50 Hz against 1 kHz kept within 2 dB at DRIVE 64, lost by 10 dB or more at 127;
//   - hiss: rising with DRIVE under a held input, and the output falling to silence once the input does;
//   - 16 lanes at their own DRIVEs give the samples of each lane alone;
//   - rates: a chain at 44.1 kHz and one at 48, direct and raised 2x, within 0.05 dB on the tone and 1 dB on its THD+N;
//   - its cost, reported.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "edDrumsChain.h"
#include "edDrumsRate.h"
#include "stFastMetrics.h"

namespace
{
	using namespace elektron::drums;
	using elektron::Block;
	using elektron::BlockSize;
	using elektron::Lanes;
	namespace bench = syntakt::fast::bench;

	void require(const bool _condition, const std::string& _message)
	{
		if(!_condition)
			throw std::runtime_error(_message);
	}

	ChainSettings analog(const int _amount)
	{
		ChainSettings s;
		s.drive[0] = {DriveType::SyntaktAnalog, true, _amount, 127};
		return s;
	}

	// _in through lane 0 of a chain of _settings at _rate, raised _factor times, from silence
	std::vector<double> through(const std::vector<double>& _in, const ChainSettings& _settings, const double _rate,
		const int _factor = 1)
	{
		auto chain = std::make_unique<Chain>(_rate, _rate == 48000);
		chain->setOversampling(_factor);
		chain->configure(0, _settings);
		auto block = std::make_unique<Block>();
		std::vector<double> y(_in.size());
		for(size_t at = 0; at + BlockSize <= _in.size(); at += BlockSize)
		{
			for(int n = 0; n < BlockSize; ++n)
				for(int l = 0; l < Lanes; ++l)
					block->s[n][l] = static_cast<float>(_in[at + static_cast<size_t>(n)]);
			chain->process(*block, 1);
			for(int n = 0; n < BlockSize; ++n)
				y[at + static_cast<size_t>(n)] = block->s[n][0];
		}
		return y;
	}

	std::vector<double> sine(const double _level, const double _hz, const double _rate, const size_t _samples)
	{
		std::vector<double> x(_samples);
		for(size_t n = 0; n < _samples; ++n)
			x[n] = _level * std::sin(2 * bench::Pi * _hz * static_cast<double>(n) / _rate);
		return x;
	}

	constexpr size_t Samples = 32768, Settle = 8192;

	// A tone's amplitude at the output, and its THD+N in dB: what is not the tone over the tone
	struct Tone
	{
		double amplitude, thd;
	};

	Tone tone(const double _level, const double _hz, const int _amount, const double _rate = 44100,
		const int _factor = 1)
	{
		const auto y = through(sine(_level, _hz, _rate, Samples), analog(_amount), _rate, _factor);
		const auto fit = bench::fitSine(y, 2 * bench::Pi * _hz / _rate, Settle);
		return {fit.amplitude, bench::db(fit.residual / fit.power)};
	}

	double dbOf(const double _amplitude) { return 20 * std::log10(_amplitude); }

	void laws()
	{
		require(AnalogDrive::gainDb(0) == 0 && AnalogDrive::gainDb(127) == 30, "the gain's ends");
		require(std::abs(AnalogDrive::cutoff(0) - 12) < 1e-9 && std::abs(AnalogDrive::cutoff(64) - 30.70) < 0.01
			&& std::abs(AnalogDrive::cutoff(127) - 302) < 1e-9, "the coupling high-pass's cutoffs");
		std::printf("  laws: gain 0 to +30 dB, coupling high-pass %.1f, %.1f, %.1f Hz at DRIVE 0, 64, 127\n",
			AnalogDrive::cutoff(0), AnalogDrive::cutoff(64), AnalogDrive::cutoff(127));
	}

	void monotone()
	{
		for(const double level : {0.05, 0.5})
		{
			double last = 0;
			std::string line;
			for(int amount = 0; amount <= 127; amount += 9)
			{
				const double a = tone(level, 1000, amount).amplitude;
				require(a > last, "the output does not rise with DRIVE at " + std::to_string(amount));
				last = a;
				if(amount % 36 == 0)
					line += " " + std::to_string(static_cast<int>(std::lround(10 * dbOf(a)))) + "/10";
			}
			std::printf("  monotone at %.0f dBFS: the tone rises with DRIVE, %s dBFS at 0, 36, 72, 108\n", dbOf(level),
				line.c_str());
		}
	}

	void distortion()
	{
		double last = -1e9;
		std::printf("  THD+N at DRIVE 64 by level:");
		for(const double db : {-40.0, -30.0, -20.0, -12.0, -6.0, 0.0})
		{
			const double thd = tone(std::pow(10, db / 20), 1000, 64).thd;
			std::printf(" %.0f dB %.1f,", db, thd);
			require(thd > last, "THD+N does not rise with the level at " + std::to_string(db));
			last = thd;
		}
		std::printf(" in dB\n");
	}

	void compression()
	{
		std::vector<double> out;
		const double levels[]{-40, -30, -20, -12, -6, 0};
		std::printf("  compression at DRIVE 96, dB out by dB in:");
		for(const double db : levels)
		{
			out.push_back(dbOf(tone(std::pow(10, db / 20), 1000, 96).amplitude));
			std::printf(" %.0f %.1f,", db, out.back());
		}
		const double quiet = (out[1] - out[0]) / 10, loud = (out[5] - out[4]) / 6;
		std::printf(" slope %.2f quiet, %.2f loud\n", quiet, loud);
		require(quiet > 0.9 && quiet < 1.1, "the slope is not near 1 when quiet");
		require(loud < 0.3, "no compression near full scale");
	}

	void lowEnd()
	{
		for(const int amount : {64, 127})
		{
			const double low = tone(0.001, 50, amount).amplitude, mid = tone(0.001, 1000, amount).amplitude;
			const double relative = dbOf(low / mid);
			std::printf("  low end at DRIVE %d: 50 Hz %.1f dB against 1 kHz\n", amount, relative);
			require(amount == 64 ? relative > -2 : relative < -10, "the low end at DRIVE " + std::to_string(amount));
		}
	}

	void hiss()
	{
		// A held input that the coupling high-pass takes away: what comes out is the hiss
		double last = 0;
		std::printf("  hiss under a held -6 dBFS:");
		for(const int amount : {0, 64, 127})
		{
			std::vector<double> x(Samples, 0.5);
			x.resize(Samples + 48000, 0.0);
			const auto y = through(x, analog(amount), 44100);
			double power = 0;
			for(size_t n = Samples - 8192; n < Samples; ++n)
				power += y[n] * y[n];
			const double rms = std::sqrt(power / 8192);
			std::printf(" DRIVE %d %.1f dBFS,", amount, dbOf(rms));
			require(rms > last, "the hiss does not rise with DRIVE");
			last = rms;
			double tail = 0;
			for(size_t n = Samples + 24000; n < y.size(); ++n)
				tail = std::max(tail, std::abs(y[n]));
			require(tail < 1e-5, "the output does not fall to silence after its input");
		}
		std::printf(" silent 0.5 s after the input stops\n");
	}

	void lanes()
	{
		std::mt19937 random(2);
		AnalogDrive all(44100);
		std::vector<AnalogDrive> alone(Lanes, AnalogDrive(44100));
		for(int l = 0; l < Lanes; ++l)
		{
			const int amount = static_cast<int>(random() % 128);
			all.set(l, amount << 7);
			alone[static_cast<size_t>(l)].set(l, amount << 7);
		}
		const uint16_t masks[] = {0xffff, 0x0ff0, 0x00f3, 0x8001, 0xf0f0};
		int64_t compared = 0;
		for(int b = 0; b < 200; ++b)
		{
			const uint16_t lanes = masks[static_cast<size_t>(b / 8) % std::size(masks)];
			Block in;
			for(auto& row : in.s)
				for(auto& v : row)
					v = static_cast<float>(static_cast<int>(random() % 4001) - 2000) / 2000.0f;
			Block out = in;
			all.process(out, lanes);
			for(int l = 0; l < Lanes; ++l)
			{
				if(!(lanes >> l & 1))
				{
					for(int n = 0; n < BlockSize; ++n)
						require(out.s[n][l] == in.s[n][l], "a lane outside the mask changed");
					continue;
				}
				Block one = in;
				alone[static_cast<size_t>(l)].process(one, static_cast<uint16_t>(1 << l));
				for(int n = 0; n < BlockSize; ++n, ++compared)
					require(std::memcmp(&out.s[n][l], &one.s[n][l], sizeof(float)) == 0,
						"a lane among 16 is not the lane alone");
			}
		}
		std::printf("  lanes: 16 at their own DRIVEs, %lld samples, each the lane alone's\n",
			static_cast<long long>(compared));
	}

	void rates()
	{
		double worstLevel = 0, worstThd = 0;
		for(const int factor : {1, 2})
			for(const int amount : {32, 64, 127})
				for(const double level : {0.05, 0.5})
				{
					const auto a = tone(level, 1000, amount, 44100, factor);
					const auto b = tone(level, 1000, amount, 48000, factor);
					worstLevel = std::max(worstLevel, std::abs(dbOf(a.amplitude / b.amplitude)));
					worstThd = std::max(worstThd, std::abs(a.thd - b.thd));
				}
		std::printf("  rates: 44.1 against 48 kHz, direct and raised 2x: the tone within %.3f dB, THD+N within %.2f dB"
			" (thresholds 0.05, 1)\n", worstLevel, worstThd);
		require(worstLevel < 0.05 && worstThd < 1, "44.1 and 48 kHz differ");
	}

	void cost()
	{
		AnalogDrive stage(44100);
		for(int l = 0; l < Lanes; ++l)
			stage.set(l, (60 + 4 * l) << 7);
		Block block;
		constexpr int Blocks = 20000;
		double best = 1e9;
		for(int pass = 0; pass < 5; ++pass)
		{
			for(int n = 0; n < BlockSize; ++n)
				for(int l = 0; l < Lanes; ++l)
					block.s[n][l] = 0.3f * static_cast<float>(std::sin(0.1 * n + l));
			const auto t0 = std::chrono::steady_clock::now();
			for(int b = 0; b < Blocks; ++b)
				stage.process(block, 0xffff);
			best = std::min(best, std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
		}
		const double percent = 100 * best / (Blocks * BlockSize / 44100.0);
		std::printf("  cost: 16 lanes, %.3f %% of a core at 44.1 kHz, %.4f %% a Track (guide 0.1 %%, before "
			"oversampling)\n", percent, percent / Lanes);
	}
}

int main()
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	try
	{
		laws();
		monotone();
		distortion();
		compression();
		lowEnd();
		hiss();
		lanes();
		rates();
		cost();
	}
	catch(const std::exception& _e)
	{
		std::printf("FAIL: %s\n", _e.what());
		return 1;
	}
	std::printf("PASS\n");
	return 0;
}
