// A Track's chain on its own, no firmware (ticket 12 of .scratch/elektron-drums-v1), each measure printed:
//
//   - every place's stage at unity: the families' chains and each stage alone, DIST's gain measured;
//   - Q5: nothing changed through a chain of Off slots;
//   - lanes: a lane is not touched by another's stages, nor its state by a change of the lane beside it;
//   - the order of spec 4: drives before and after the filter, BAND before and after it, against the stages by hand;
//   - Q6: ASR of a sine at 2.5 kHz x 8 through a drive raised 2x and 4x; MIX on the raised path;
//   - Q7: the half-bands' round trip; their delay, which Oversampler::delay states;
//   - Q9: the three bands' filter at its cutoff from 20 Hz to 20 kHz, its stability under a sweep;
//   - Q12: every stage type changed on a 200 Hz tone at its crest; the filter's envelope, moving the Machinedrum's band.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "edDrumsChain.h"
#include "stFastMetrics.h"

namespace
{
	using namespace elektron::drums;
	using elektron::Block;
	using elektron::BlockSize;
	using elektron::Lanes;
	using syntakt::fast::bench::db;
	using syntakt::fast::bench::fitSine;
	using syntakt::fast::bench::highShare;
	using syntakt::fast::bench::Pi;
	using syntakt::fast::bench::powerSpectrum;

	void require(const bool _condition, const std::string& _message)
	{
		if(!_condition)
			throw std::runtime_error(_message);
	}

	struct Rates
	{
		double rate;
		bool syntakt;
	};

	constexpr Rates Machinedrum{44100, false}, Syntakt{48000, true};

	// A block of _signal on every lane
	template<typename Signal>
	void fill(Block& _b, const int64_t _at, Signal&& _signal)
	{
		for(int n = 0; n < BlockSize; ++n)
		{
			const auto x = static_cast<float>(_signal(_at + n));
			for(int l = 0; l < Lanes; ++l)
				_b.s[n][l] = x;
		}
	}

	// Lane 0 through _chain from silence
	template<typename Signal>
	std::vector<double> through(Chain& _chain, const int _samples, Signal&& _signal, const std::function<void(int)>& _atBlock = {})
	{
		auto block = std::make_unique<Block>();
		std::vector<double> y;
		for(int b = 0; b < _samples / BlockSize; ++b)
		{
			if(_atBlock)
				_atBlock(b);
			fill(*block, b * BlockSize, _signal);
			_chain.process(*block, 1);
			for(int n = 0; n < BlockSize; ++n)
				y.push_back(block->s[n][0]);
		}
		return y;
	}

	double rms(const std::vector<double>& _y, const size_t _from)
	{
		double sum = 0;
		for(size_t n = _from; n < _y.size(); ++n)
			sum += _y[n] * _y[n];
		return std::sqrt(sum / static_cast<double>(_y.size() - _from));
	}

	double decibels(const double _gain) { return 20 * std::log10(std::max(_gain, 1e-30)); }

	// A sine's gain through _settings on a chain at _rates
	double gainOf(const ChainSettings& _settings, const Rates _rates, const double _level = 0.05, const double _hz = 1000)
	{
		auto chain = std::make_unique<Chain>(_rates.rate, _rates.syntakt);
		chain->configure(0, _settings);
		const auto y = through(*chain, 16384, [&](const int64_t _n) { return _level * std::sin(2 * Pi * _hz * static_cast<double>(_n) / _rates.rate); });
		return rms(y, 8192) / (_level / std::sqrt(2.0));
	}

	void levels()
	{
		const double md = decibels(gainOf(machinedrumChain(), Machinedrum)), st = decibels(gainOf(syntaktChain(), Syntakt));
		std::printf("  a family's chain at 1 kHz: Machinedrum %+.3f dB, Syntakt %+.3f dB\n", md, st);
		require(std::abs(st) < 0.01, "the Syntakt's chain is not at unity");
		require(std::abs(md) < 0.1, "the Machinedrum's chain is not at unity");

		const auto drive = [](const DriveType _type)
		{
			ChainSettings s;
			s.drive[0].type = _type;
			return s;
		};
		const auto band = [](const BandType _type, const std::array<int, 6> _values)
		{
			ChainSettings s;
			s.band.type = _type;
			s.band.values = _values;
			return s;
		};
		ChainSettings mdFilter;
		mdFilter.filter.type = FilterType::Machinedrum;
		mdFilter.filter.frequency = 0;
		ChainSettings stFilter;
		stFilter.filter.type = FilterType::Syntakt;

		const std::pair<const char*, ChainSettings> stages[] = {{"Syntakt drive at 0", drive(DriveType::Syntakt)},
			{"DIST at 0", drive(DriveType::MdDist)}, {"SRR at 0", drive(DriveType::MdSrr)},
			{"BASE/WIDTH open", band(BandType::BaseWidth, {0, 127})}, {"EQ flat", band(BandType::MdEq, {64, 63})},
			{"three bands flat", band(BandType::ThreeBand, {30, 64, 64, 64, 100, 64})}, {"Machinedrum's filter open", mdFilter},
			{"Syntakt's filter off", stFilter}};
		for(const auto& [name, settings] : stages)
			for(const auto& rates : {Machinedrum, Syntakt})
			{
				auto s = settings;
				const double gain = decibels(gainOf(s, rates));
				std::printf("    %s at %.1f kHz: %+.3f dB\n", name, rates.rate / 1000, gain);
				require(std::abs(gain) < 0.1, std::string(name) + " is not at unity");
			}
	}

	void neutral()
	{
		auto chain = std::make_unique<Chain>(48000.0, true);
		chain->configure(0, ChainSettings{});
		chain->setOversampling(4);
		std::mt19937 random(1);
		std::uniform_real_distribution<float> uniform(-1, 1);
		auto block = std::make_unique<Block>();
		for(int b = 0; b < 2000; ++b)
		{
			for(auto& sample : block->s)
				for(auto& x : sample)
					x = uniform(random);
			const Block in = *block;
			chain->process(*block, 0xffff);
			for(int n = 0; n < BlockSize; ++n)
				for(int l = 0; l < Lanes; ++l)
					require(block->s[n][l] == in.s[n][l], "a chain of Off slots changed a sample");
		}
		std::printf("  Q5: all slots Off: every sample the same, 0 at -inf dBFS (threshold -120)\n");

		// Off taken over, the Track sounding, by a stage: from its input to what that stage gives, by FadeSamples
		ChainSettings driven;
		driven.drive[0] = {DriveType::Syntakt, false, 90, 127};
		auto late = std::make_unique<Chain>(48000.0, true);
		late->configure(1, ChainSettings{});
		auto wet = std::make_unique<Chain>(48000.0, true);
		wet->configure(1, driven);
		double worst = 0;
		float changed = 0;
		for(int b = 0; b < 8; ++b)
		{
			for(auto& sample : block->s)
				for(auto& x : sample)
					x = uniform(random);
			const Block in = *block;
			Block other = in;
			if(b == 2)
				late->configure(1, driven);
			late->process(*block, 2);
			if(b >= 2)
				wet->process(other, 2);
			for(int n = 0; n < BlockSize; ++n)
			{
				const int done = (b - 2) * BlockSize - Chain::WarmSamples + n + 1;
				const float g = std::clamp(static_cast<float>(done) / Chain::FadeSamples, 0.0f, 1.0f);
				const float expected = in.s[n][1] + g * (other.s[n][1] - in.s[n][1]);
				worst = std::max<double>(worst, std::abs(block->s[n][1] - expected));
				changed = std::max(changed, std::abs(other.s[n][1] - in.s[n][1]));
			}
		}
		std::printf("  a stage taken over by the crossfade: as dry and wet by the ramp, within %.1f dB, the two %.1f dB apart\n",
			decibels(worst), decibels(changed));
		require(worst < 1e-5 && changed > 0.1f, "the crossfade is not a crossfade");
	}

	// Lanes side by side, a lane alone against the same lane among others
	void lanes()
	{
		auto noise = [](const uint32_t _seed)
		{
			auto random = std::make_shared<std::mt19937>(_seed);
			return [random](const int64_t) { return std::uniform_real_distribution<double>(-0.5, 0.5)(*random); };
		};
		ChainSettings a = machinedrumChain(), b;
		b.drive[0].type = DriveType::MdSrr;
		b.drive[0].amount = 90;
		b.drive[0].post = true;
		ChainSettings changing = syntaktChain();

		auto alone = std::make_unique<Chain>(44100.0, false);
		alone->configure(0, a);
		const auto reference = through(*alone, 16384, noise(5), 0);

		auto beside = std::make_unique<Chain>(44100.0, false);
		beside->configure(0, a);
		beside->configure(1, b);
		beside->configure(2, changing);
		auto block = std::make_unique<Block>();
		auto signal = noise(5);
		std::vector<double> y;
		std::mt19937 random(2);
		for(int b2 = 0; b2 < 16384 / BlockSize; ++b2)
		{
			if(b2 % 37 == 5)
				beside->configure(2, b2 % 2 ? machinedrumChain() : syntaktChain());
			if(b2 % 53 == 7)
				beside->configure(1, b2 % 2 ? b : ChainSettings{});
			for(int n = 0; n < BlockSize; ++n)
				for(int l = 0; l < Lanes; ++l)
					block->s[n][l] = l == 0 ? static_cast<float>(signal(0)) : static_cast<float>(random()) / 4e9f;
			beside->process(*block, 7);
			for(int n = 0; n < BlockSize; ++n)
				y.push_back(block->s[n][0]);
		}
		require(y == reference, "a lane heard the stages or the changes of the lanes beside it");
		std::printf("  lanes: a Track the same alone and beside two Tracks that change stage, sample for sample\n");
	}

	// The skeleton against the stages run by hand
	void order()
	{
		const auto signal = [](const int64_t _n) { return 0.9 * std::sin(2 * Pi * 700.0 * static_cast<double>(_n) / 48000); };
		const auto run = [&](const bool _post, const bool _bandAfter)
		{
			ChainSettings s;
			s.drive[0] = {DriveType::Syntakt, _post, 127, 127};
			s.band.type = BandType::ThreeBand;
			s.band.after = _bandAfter;
			s.band.values = {0, 64, 64, 64, 40, 0};		// the high shelf down 18 dB from 500 Hz
			s.filter.type = FilterType::Syntakt;
			s.filter.mode = 0;
			s.filter.frequency = 80;
			auto chain = std::make_unique<Chain>(48000.0, true);
			chain->setOversampling(1);
			chain->configure(0, s);
			return through(*chain, 8192, signal);
		};
		const auto manual = [&](const std::vector<int>& _order)
		{
			syntakt::fast::TrackDrive drive;
			drive.set(0, 127 << 8);
			drive.clear(0);
			ThreeBand band;
			band.set(0, 0, ThreeBand::lowShelf(20 * std::pow(1000.0, 0 / 127.0), 0, 48000));
			band.set(0, 1, ThreeBand::bell(20 * std::pow(1000.0, 64 / 127.0), 0, 48000));
			band.set(0, 2, ThreeBand::highShelf(20 * std::pow(1000.0, 40 / 127.0), (0 - 64) * 18.0 / 63, 48000));
			band.clear(0);
			auto filter = std::make_unique<syntakt::fast::Multimode>(48000.0);
			filter->set(0, 0, syntakt::fast::filterCutoff(80 << 8, 64 << 8, 0, 60, 0), 0, true);
			filter->clear(0);
			auto block = std::make_unique<Block>();
			std::vector<double> y;
			for(int b = 0; b < 8192 / BlockSize; ++b)
			{
				fill(*block, b * BlockSize, signal);
				for(const int stage : _order)
				{
					if(stage == 0)
					{
						drive.process(*block);
						for(auto& s : block->s)
							for(auto& x : s)
								x *= 2;
					}
					else if(stage == 1)
					{
						band.process(*block);
					}
					else
					{
						for(auto& s : block->s)
							for(auto& x : s)
								x *= 0.5f;
						filter->process(*block);
						for(auto& s : block->s)
							for(auto& x : s)
								x *= 8;
					}
				}
				for(int n = 0; n < BlockSize; ++n)
					y.push_back(block->s[n][0]);
			}
			return y;
		};
		const auto worst = [](const std::vector<double>& _a, const std::vector<double>& _b)
		{
			double d = 0;
			for(size_t n = 512; n < _a.size(); ++n)
				d = std::max(d, std::abs(_a[n] - _b[n]));
			return d;
		};
		struct Case
		{
			bool post, after;
			std::vector<int> order;
		};
		const Case cases[] = {{false, false, {0, 1, 2}}, {false, true, {0, 2, 1}}, {true, false, {1, 2, 0}}, {true, true, {2, 1, 0}}};
		double error = 0;
		for(const auto& c : cases)
			error = std::max(error, worst(run(c.post, c.after), manual(c.order)));
		const double apart = worst(run(false, false), run(true, true));
		std::printf("  the skeleton: drives pre and post, BAND before and after, as the stages by hand within %.1f dB; the two ends "
			"apart by %.1f dB\n", decibels(error), decibels(apart));
		require(error < 1e-5, "the chain does not run in the skeleton's order");
		require(apart > 0.01, "the orders do not differ");
	}

	// ---- Q6 ----

	// ASR: of a sine at an exact bin, what is not at its harmonics over what is, in a Hann-free whole number of cycles
	double asr(const std::vector<double>& _y, const int _bin)
	{
		constexpr size_t N = 1 << 15;
		const auto power = powerSpectrum({_y.end() - N, _y.end()});
		double harmonics = 0, rest = 0;
		for(size_t i = 1; i < N / 2; ++i)
			(i % static_cast<size_t>(_bin) == 0 ? harmonics : rest) += power[i];
		return db(rest / harmonics);
	}

	void q6()
	{
		struct Case
		{
			const char* name;
			Rates rates;
			DriveType type;
			int amount;
			double bound2, bound4;
		};
		// A full-scale sine x 8: the soft clip's three passes at their top; DIST at 106, its gain 1/2 on its level of 1/32
		const Case cases[] = {{"Syntakt drive on a Machinedrum Track", Machinedrum, DriveType::Syntakt, 127, -50, -80},
			{"Syntakt drive post on a Syntakt Track", Syntakt, DriveType::Syntakt, 127, -50, -80},
			{"DIST on a Syntakt Track", Syntakt, DriveType::MdDist, 106, 0, 0}};
		for(const auto& c : cases)
		{
			double values[3];
			int i = 0;
			constexpr int Bin = 743;
			for(const int factor : {1, 2, 4})
			{
				ChainSettings s;
				s.drive[0] = {c.type, c.rates.syntakt, c.amount, 127};
				auto chain = std::make_unique<Chain>(c.rates.rate, c.rates.syntakt);
				chain->setOversampling(factor);
				chain->configure(0, s);
				const double hz = Bin * c.rates.rate / 32768;
				const auto y = through(*chain, 40960, [&](const int64_t _n) { return std::sin(2 * Pi * hz * static_cast<double>(_n) / c.rates.rate); });
				values[i++] = asr(y, Bin);
			}
			std::printf("  Q6, %s at 2.5 kHz x 8: ASR %.1f dB off, %.1f dB at 2x, %.1f dB at 4x\n", c.name, values[0], values[1], values[2]);
			if(c.bound2 < 0)
			{
				require(values[1] <= c.bound2, std::string(c.name) + ": Q6 at 2x");
				require(values[2] <= c.bound4, std::string(c.name) + ": Q6 at 4x");
			}
			else
			{
				require(values[1] < values[0] - 3 && values[2] < values[1] - 3, std::string(c.name) + ": the rate buys nothing");
			}
		}
	}

	// _signal up and down at _factor
	template<typename Signal>
	std::vector<double> roundTrip(const double _rate, const int _factor, const int _samples, Signal&& _signal)
	{
		auto over = std::make_unique<Oversampler>(_rate);
		auto in = std::make_unique<Block>();
		auto up = std::make_unique<std::array<Block, 4>>();
		auto out = std::make_unique<Block>();
		std::vector<double> y;
		for(int b = 0; b < _samples / BlockSize; ++b)
		{
			fill(*in, b * BlockSize, _signal);
			over->up(*in, up->data(), _factor, 0xf);
			over->down(up->data(), *out, _factor, 0xf);
			for(int n = 0; n < BlockSize; ++n)
				y.push_back(out->s[n][1]);
		}
		return y;
	}

	// MIX on a raised drive: at 0 the dry signal through the same half-bands, so that the phases of dry and wet meet
	void mix()
	{
		ChainSettings s;
		s.drive[0] = {DriveType::MdDist, false, 40, 127};
		const auto signal = [](const int64_t _n) { return 0.5 * std::sin(2 * Pi * 3000.0 * static_cast<double>(_n) / 48000); };
		const auto run = [&](const int _mix)
		{
			s.drive[0].mix = _mix;
			auto chain = std::make_unique<Chain>(48000.0, true);
			chain->setOversampling(2);
			chain->configure(0, s);
			return through(*chain, 8192, signal);
		};
		const auto dry = run(0), wet = run(127), half = run(64);
		const auto expected = roundTrip(48000, 2, 8192, signal);
		double worstDry = 0, worstHalf = 0;
		const double g = 64.0 / 127;
		for(size_t n = 0; n < dry.size(); ++n)
		{
			worstDry = std::max(worstDry, std::abs(dry[n] - expected[n]));
			worstHalf = std::max(worstHalf, std::abs(half[n] - (dry[n] + g * (wet[n] - dry[n]))));
		}
		std::printf("  MIX: at 0 the half-bands alone within %.1f dB, at 64 between dry and wet within %.1f dB\n", decibels(worstDry),
			decibels(worstHalf));
		require(worstDry < 2e-6 && worstHalf < 0.02, "MIX");
	}

	// ---- Q7 ----

	void q7()
	{
		for(const double rate : {44100.0, 48000.0})
			for(const int factor : {2, 4})
			{
				double gainError = 0, worstThd = -300;
				for(double hz = 1000; hz <= 20000; hz += hz < 16000 ? 1000 : 500)
				{
					const double w = 2 * Pi * hz / rate;
					const auto y = roundTrip(rate, factor, 1 << 16, [&](const int64_t _n) { return 0.9 * std::sin(w * static_cast<double>(_n)); });
					const auto fit = fitSine(y, w, 4096);
					gainError = std::max(gainError, std::abs(decibels(fit.amplitude / 0.9)));
					worstThd = std::max(worstThd, db(fit.residual / fit.power));
				}
				std::printf("  Q7, %.1f kHz %dx: gain within %.4f dB 1-20 kHz (threshold 0.01), THD+N %.1f dB at worst (threshold -120)\n",
					rate / 1000, factor, gainError, worstThd);
				require(gainError <= 0.01, "Q7's gain");
				require(worstThd <= -120, "Q7's THD+N");
			}

		// The delay of a low frequency through both, what a Machinedrum Hit takes off its wait
		for(const int factor : {2, 4})
		{
			const double w = 2 * Pi * 200 / 44100;
			const auto y = roundTrip(44100, factor, 1024 * BlockSize, [&](const int64_t _n) { return std::sin(w * static_cast<double>(_n)); });
			double xc = 0, xs = 0;
			for(size_t n = 4096; n < y.size(); ++n)
			{
				xc += y[n] * std::cos(w * static_cast<double>(n));
				xs += y[n] * std::sin(w * static_cast<double>(n));
			}
			const double delay = -std::atan2(xc, xs) / w;
			std::printf("  half-bands %dx: a low frequency %.2f samples late, stated %d\n", factor, delay, Oversampler::delay(factor));
			require(std::abs(delay - Oversampler::delay(factor)) < 1, "Oversampler::delay");
		}
	}

	// ---- Q9 ----

	// The stage's filter at Q 0.707, its low-pass output
	ThreeBand::Words lowPass(const double _hz, const double _rate)
	{
		const double g = std::tan(Pi * _hz / _rate), a1 = 1 / (1 + g * (g + std::sqrt(2.0)));
		ThreeBand::Words w;
		w.a1 = static_cast<float>(a1);
		w.a2 = static_cast<float>(g * a1);
		w.a3 = static_cast<float>(g * g * a1);
		w.m0 = 0;
		w.m2 = 1;
		return w;
	}

	void q9()
	{
		double worst = 0;
		for(const double rate : {44100.0, 48000.0})
			for(double hz = 20; hz <= 20000; hz *= 1.5)
				for(const int band : {0, 1})
				{
					// Band 0 through a low-pass at Q 0.707, the others left flat; then the shelves at half their gain at fc
					auto stage = std::make_unique<ThreeBand>();
					const auto words = band == 0 ? lowPass(hz, rate) : ThreeBand::lowShelf(hz, 12, rate);
					stage->set(0, 0, words);
					stage->clear(0);
					auto block = std::make_unique<Block>();
					const int samples = std::max(1 << 15, static_cast<int>(8 * rate / hz)) / BlockSize * BlockSize;
					std::vector<double> y;
					const double w = 2 * Pi * hz / rate;
					for(int b = 0; b < samples / BlockSize; ++b)
					{
						fill(*block, b * BlockSize, [&](const int64_t _n) { return 0.25 * std::sin(w * static_cast<double>(_n)); });
						stage->process(*block);
						for(int n = 0; n < BlockSize; ++n)
							y.push_back(block->s[n][0]);
					}
					const double gain = decibels(fitSine(y, w, y.size() / 2).amplitude / 0.25);
					const double expected = band == 0 ? -3.0103 : 6;
					worst = std::max(worst, std::abs(gain - expected));
				}
		std::printf("  Q9: the trapezoidal filter at its cutoff, Q 0.707, 20 Hz to 20 kHz, -3.01 dB and a shelf's half within %.3f dB "
			"(threshold 0.05)\n", worst);
		require(worst <= 0.05, "Q9's gain at the cutoff");

		// Cutoff and gain swept each block on noise, bursts of full scale: nothing but finite samples, within a bound
		auto stage = std::make_unique<ThreeBand>();
		auto block = std::make_unique<Block>();
		std::mt19937 random(3);
		std::uniform_real_distribution<double> uniform(0, 1);
		float loudest = 0;
		for(int b = 0; b < 40000; ++b)
		{
			for(int l = 0; l < Lanes; ++l)
			{
				const double hz = 20 * std::pow(1000.0, uniform(random)), gain = -18 + 36 * uniform(random);
				stage->set(l, 0, ThreeBand::lowShelf(hz, gain, 44100));
				stage->set(l, 1, ThreeBand::bell(hz, -gain, 44100));
				stage->set(l, 2, ThreeBand::highShelf(hz, gain, 44100));
			}
			for(auto& sample : block->s)
				for(auto& x : sample)
					x = static_cast<float>(2 * uniform(random) - 1);
			stage->process(*block);
			for(const auto& sample : block->s)
				for(const float x : sample)
				{
					require(std::isfinite(x), "Q9: a sample not finite");
					loudest = std::max(loudest, std::abs(x));
				}
		}
		std::printf("  Q9: a random sweep of frequency and gain each block over noise: every sample finite, peak %.1f\n", loudest);
		require(loudest < 2000, "Q9: the sweep ran away");
	}

	// ---- Q12 ----

	void q12()
	{
		struct Change
		{
			const char* name;
			Rates rates;
			std::function<void(ChainSettings&)> from, to;
		};
		const auto driveType = [](const DriveType _t, const bool _post)
		{
			return [=](ChainSettings& _s) { _s.drive[0] = {_t, _post, 0, 127}; };
		};
		const auto bandType = [](const BandType _t, const std::array<int, 6> _values, const bool _after = false)
		{
			return [=](ChainSettings& _s) { _s.band.type = _t; _s.band.values = _values; _s.band.after = _after; };
		};
		const auto filterType = [](const FilterType _t, const int _mode)
		{
			return [=](ChainSettings& _s) { _s.filter.type = _t; _s.filter.mode = _mode; _s.filter.frequency = _t == FilterType::Machinedrum ? 0 : 127; };
		};
		const std::vector<Change> changes = {
			{"drive Off to Syntakt", Machinedrum, driveType(DriveType::Off, false), driveType(DriveType::Syntakt, false)},
			{"drive Syntakt to DIST", Machinedrum, driveType(DriveType::Syntakt, false), driveType(DriveType::MdDist, false)},
			{"drive DIST to SRR, Syntakt Track", Syntakt, driveType(DriveType::MdDist, true), driveType(DriveType::MdSrr, true)},
			{"a drive pre to post", Syntakt, driveType(DriveType::Syntakt, false), driveType(DriveType::Syntakt, true)},
			{"BAND EQ to three bands", Machinedrum, bandType(BandType::MdEq, {64, 63}), bandType(BandType::ThreeBand, {30, 64, 64, 64, 100, 64})},
			{"BAND BASE/WIDTH to Off", Syntakt, bandType(BandType::BaseWidth, {0, 127}), bandType(BandType::Off, {})},
			{"BAND before to after", Machinedrum, bandType(BandType::MdEq, {64, 63}), bandType(BandType::MdEq, {64, 63}, true)},
			{"FILTER Off to Machinedrum", Machinedrum, filterType(FilterType::Off, 0), filterType(FilterType::Machinedrum, 0)},
			{"FILTER Machinedrum to Syntakt", Syntakt, filterType(FilterType::Machinedrum, 0), filterType(FilterType::Syntakt, 7)},
			{"FILTER Syntakt type 7 to 0", Syntakt, filterType(FilterType::Syntakt, 7), filterType(FilterType::Syntakt, 0)},
			{"FILTER Syntakt type 0 to 2", Syntakt, filterType(FilterType::Syntakt, 0), filterType(FilterType::Syntakt, 2)},
			{"FILTER Syntakt type 2 to 5, a morph", Syntakt, filterType(FilterType::Syntakt, 2), filterType(FilterType::Syntakt, 5)},
		};
		// From just before the change to past the crossfade, at each rate of the drives
		constexpr int Around = 1024;
		double worst = -300;
		for(const int factor : {1, 2, 4})
		for(const auto& change : changes)
		{
			constexpr int Samples = 32768;
			const double rate = change.rates.rate;
			const auto tone = [&](const int64_t _n) { return 0.95 * std::cos(2 * Pi * 200 * static_cast<double>(_n) / rate); };
			// The change at a block near a crest of the tone
			const int period = static_cast<int>(rate / 200);
			int block = 12000 / BlockSize;
			while((block * BlockSize) % period > 16 && block < 16000 / BlockSize)
				++block;
			ChainSettings before, after;
			change.from(before);
			change.to(after);
			auto chain = std::make_unique<Chain>(rate, change.rates.syntakt);
			chain->setOversampling(factor);
			chain->configure(0, before);
			const auto y = through(*chain, Samples, tone, [&](const int _b) { if(_b == block) chain->configure(0, after); });
			const int at = block * BlockSize + Chain::FadeSamples / 2;
			const double value = std::max(highShare(y, at, rate, Around), highShare(y, at + Chain::FadeSamples, rate, Around));

			// SRR at 48 kHz holds at the machine's 44.1, 0 included: what a stage adds itself is not the change's
			auto settled = std::make_unique<Chain>(rate, change.rates.syntakt);
			settled->setOversampling(factor);
			settled->configure(0, after);
			const double own = highShare(through(*settled, Samples, tone), at, rate, Around);
			std::printf("    Q12 %dx, %s: HF %.1f dB, of the stage alone %.1f\n", factor, change.name, value, own);
			if(own < -50)
				worst = std::max(worst, value);
			else
				require(value <= own + 1, std::string(change.name) + ": more than the stage's own HF");
		}
		std::printf("  Q12: every stage changed on a 200 Hz tone at its crest: HF %.1f dB at worst (threshold -50)\n", worst);
		require(worst <= -50, "Q12");
	}

	// Why the chain runs before the rings: there a Hit starts at a block. On a fixed grid after them it starts anywhere in one,
	// so that what steps by the block meets it later or earlier with its note. A Hit's sound delayed by 0 to 31 samples
	// against the blocks, the Hit's block telling the filter's envelope and each lock at the block, as a grid would
	void grid()
	{
		const auto hitSound = [](const int64_t _n)
		{
			return _n < 0 ? 0.0 : 0.7 * std::exp(-static_cast<double>(_n) / 900) * std::sin(2 * Pi * 180.0 * static_cast<double>(_n) / 44100);
		};
		ChainSettings envelope = machinedrumChain();
		envelope.filter.frequency = 30;
		envelope.filter.depth = 72;
		envelope.filter.envelope.decay = 30;
		const auto play = [&](const int _offset, ChainSettings _settings)
		{
			auto chain = std::make_unique<Chain>(44100.0, false);
			chain->configure(0, _settings);
			chain->hit(0, 127);
			auto y = through(*chain, 4096, [&](const int64_t _n) { return hitSound(_n - _offset); });
			y.erase(y.begin(), y.begin() + _offset);
			y.resize(3000);
			return y;
		};
		const auto reference = play(0, envelope);
		double power = 0, worst = 0;
		for(const double x : reference)
			power += x * x;
		for(int offset = 1; offset < BlockSize; ++offset)
		{
			const auto y = play(offset, envelope);
			double error = 0;
			for(size_t n = 0; n < y.size(); ++n)
				error += (y[n] - reference[n]) * (y[n] - reference[n]);
			worst = std::max(worst, error);
		}
		std::printf("  on a fixed grid after the rings, a Hit under the filter's envelope moved by its place in the block: %.1f dB of its "
			"signal at worst (before the rings: 0, its block is its own)\n", db(worst / power));
		require(db(worst / power) > -40, "a grid would not hear where a Hit falls: the rings' side is no longer decided by this");
	}

	// Two raised types at one place, one lane each in a group
	void neighbours()
	{
		for(const int factor : {2, 4})
		{
			ChainSettings mine, other;
			mine.drive[0] = {DriveType::Syntakt, false, 100, 127};
			other.drive[0] = {DriveType::MdDist, false, 100, 127};
			const auto tone = [](const int64_t _n) { return 0.7 * std::sin(2 * Pi * 900.0 * static_cast<double>(_n) / 44100); };
			auto alone = std::make_unique<Chain>(44100.0, false);
			alone->setOversampling(factor);
			alone->configure(0, mine);
			const auto reference = through(*alone, 8192, tone);
			auto beside = std::make_unique<Chain>(44100.0, false);
			beside->setOversampling(factor);
			beside->configure(0, mine);
			beside->configure(1, other);
			auto block = std::make_unique<Block>();
			std::vector<double> y;
			for(int b = 0; b < 8192 / BlockSize; ++b)
			{
				fill(*block, b * BlockSize, tone);
				beside->process(*block, 3);
				for(int n = 0; n < BlockSize; ++n)
					y.push_back(block->s[n][0]);
			}
			double worst = 0;
			for(size_t n = 0; n < y.size(); ++n)
				worst = std::max(worst, std::abs(y[n] - reference[n]));
			std::printf("  %dx: a lane beside another raised type, apart by %.1f dB\n", factor, decibels(worst));
			require(worst == 0, "a raised lane hears the type beside it");
		}
	}

	// A lane fed loud, cleared, left idle while a group-mate runs, then hit: a fresh Chain's
	void idleLane()
	{
		ChainSettings s = machinedrumChain();
		s.filter.frequency = 40;
		s.filter.resonance = 100;
		const auto run = [&](const bool _dirty)
		{
			auto chain = std::make_unique<Chain>(44100.0, false);
			chain->configure(0, s);
			chain->configure(1, machinedrumChain());
			auto block = std::make_unique<Block>();
			std::vector<double> y;
			std::mt19937 random(7);
			for(int b = 0; b < 600; ++b)
			{
				if(b == 100 && _dirty)
					chain->clear(0);
				if(b == 300)
					chain->hit(0, 127);
				const bool sounds = _dirty ? b < 100 || b >= 300 : b >= 300;
				for(int n = 0; n < BlockSize; ++n)
				{
					const float x = (static_cast<float>(random() % 2000) / 1000.0f - 1) * 0.8f;
					if(sounds || b >= 300)
						block->s[n][0] = x;
					block->s[n][1] = 0.1f * x;
				}
				if(b < 100 && !_dirty)
				{
					for(int n = 0; n < BlockSize; ++n)
						block->s[n][0] = 0;
				}
				chain->process(*block, static_cast<uint16_t>(sounds ? 3 : 2));
				if(b >= 300)
					for(int n = 0; n < BlockSize; ++n)
						y.push_back(block->s[n][0]);
			}
			return y;
		};
		const auto dirty = run(true), fresh = run(false);
		double worst = 0;
		for(size_t n = 0; n < dirty.size(); ++n)
			worst = std::max(worst, std::abs(dirty[n] - fresh[n]));
		std::printf("  a lane woken beside a running group-mate, against a fresh one: apart by %.1f dB\n", decibels(worst));
		require(worst == 0, "an idle lane was dirtied by its group");
	}

	// An EnvelopeDepth target on the neutral depth
	void depthTarget()
	{
		ChainSettings s = machinedrumChain();
		s.filter.frequency = 30;
		s.filter.envelope.decay = 40;
		const auto tone = [](const int64_t _n) { return 0.5 * std::sin(2 * Pi * 8000.0 * static_cast<double>(_n) / 44100); };
		const auto play = [&](const ChainSettings& _s)
		{
			auto chain = std::make_unique<Chain>(44100.0, false);
			chain->configure(0, _s);
			chain->hit(0, 127);
			return through(*chain, 4096, tone);
		};
		auto targeted = s;
		targeted.velocityTarget = VelocityTarget::EnvelopeDepth;
		targeted.velocityDepth = 60;
		const bool moved = play(targeted) != play(s);
		std::printf("  a velocity depth on ENV at its neutral depth %s the envelope\n", moved ? "moves" : "does not move");
		require(moved, "EnvelopeDepth target does nothing at depth 64");
	}

	// setNote during a Hit moves the key tracking and leaves what velocity set
	void noteDuringHit()
	{
		ChainSettings byVelocity = syntaktChain();
		byVelocity.filter.mode = 0;
		byVelocity.filter.frequency = 40;
		byVelocity.filter.keyTrack = 60;
		byVelocity.velocityTarget = VelocityTarget::Frequency;
		byVelocity.velocityDepth = 70;
		ChainSettings byHand = byVelocity;
		byHand.filter.frequency = 110;
		byHand.velocityTarget = VelocityTarget::None;
		const auto tone = [](const int64_t _n) { return 0.5 * std::sin(2 * Pi * 3000.0 * static_cast<double>(_n) / 48000); };
		const auto play = [&](const ChainSettings& _s, const int _note)
		{
			auto chain = std::make_unique<Chain>(48000.0, true);
			chain->configure(0, _s);
			chain->hit(0, 127);
			return through(*chain, 8192, tone, [&](const int _b) { if(_b == 100) chain->setNote(0, _note); });
		};
		const auto moved = play(byVelocity, 72);
		const bool same = moved == play(byHand, 72);
		std::printf("  setNote during a Hit moves the key tracking and keeps what velocity set: %s\n", same ? "yes" : "no");
		require(same, "setNote lost the Hit's velocity");
		require(moved != play(byVelocity, 0), "setNote does not reach the filter");
	}

	// MIX from the very first block
	void firstBlockMix()
	{
		ChainSettings s;
		s.drive[0] = {DriveType::MdDist, false, 60, 64};
		const auto signal = [](const int64_t _n) { return 0.5 * std::sin(2 * Pi * 3000.0 * static_cast<double>(_n) / 44100); };
		const auto run = [&](const int _mix)
		{
			s.drive[0].mix = _mix;
			auto chain = std::make_unique<Chain>(44100.0, false);
			chain->configure(0, s);
			return through(*chain, 512, signal);
		};
		const auto half = run(64), dry = run(0), wet = run(127);
		double worst = 0;
		const double g = 64.0 / 127;
		for(size_t n = 0; n < half.size(); ++n)
		{
			// At 0 the signal itself, at 64 half way, from the first sample
			worst = std::max(worst, std::abs(dry[n] - static_cast<float>(signal(static_cast<int64_t>(n)))));
			worst = std::max(worst, std::abs(half[n] - (dry[n] + g * (wet[n] - dry[n]))));
		}
		std::printf("  MIX from the first sample, at 0 the input and at 64 half way: within %.1f dB\n", decibels(worst));
		require(worst < 1e-5, "the first block's MIX");
	}

	// Two changes 32 samples apart on a tone
	void secondChange()
	{
		for(const int factor : {1, 2, 4})
		{
			const auto tone = [](const int64_t _n) { return 0.95 * std::cos(2 * Pi * 200 * static_cast<double>(_n) / 44100); };
			auto chain = std::make_unique<Chain>(44100.0, false);
			chain->setOversampling(factor);
			ChainSettings a, b, c;
			a.band.type = BandType::ThreeBand;
			a.band.values = {80, 106, 64, 64, 100, 64};		// 200 Hz up 12 dB
			b.band.type = BandType::MdEq;
			b.band.values = {64, 20};						// and down
			b.drive[0] = {DriveType::Syntakt, false, 0, 127};
			constexpr int Change = 12000 / BlockSize * 1;
			chain->configure(0, a);
			const auto y = through(*chain, 32768, tone, [&](const int _b)
			{
				if(_b == Change)
					chain->configure(0, b);
				if(_b == Change + 1)
					chain->configure(0, c);
			});
			const double value = highShare(y, Change * BlockSize + 32, 44100);
			std::printf("  %dx: two changes 32 samples apart: HF %.1f dB\n", factor, value);
			require(value <= -50, "a second change during a fade");
		}
	}

	// What a restored state may hold
	void wildSettings()
	{
		ChainSettings wild;
		wild.drive[0] = {DriveType::Syntakt, false, 500, 900};
		wild.drive[1] = {DriveType::MdDist, true, -40, -7};
		wild.band.type = BandType::ThreeBand;
		wild.band.values = {-30, 400, 900, -9, 999, 999};
		wild.filter.type = FilterType::Syntakt;
		wild.filter.mode = 40;
		wild.filter.frequency = 900;
		wild.filter.resonance = -4;
		wild.filter.depth = 900;
		wild.filter.envelope = {900, -4, 900, 900, 900};
		wild.amp.on = true;
		wild.amp.attack = 900;
		wild.amp.hold = -3;
		wild.amp.decay = 800;
		wild.amp.sustain = 700;
		wild.amp.release = -9;
		ChainSettings near = wild;
		near.drive[0] = {DriveType::Syntakt, false, 127, 127};
		near.drive[1] = {DriveType::MdDist, true, 0, 0};
		near.band.values = {0, 127, 127, 0, 127, 127};
		near.filter.mode = 7;
		near.filter.frequency = 127;
		near.filter.resonance = 0;
		near.filter.depth = 127;
		near.filter.envelope = {127, 0, 127, 127, 127};
		near.amp.attack = 127;
		near.amp.hold = 0;
		near.amp.decay = 127;
		near.amp.sustain = 127;
		near.amp.release = 0;
		const auto tone = [](const int64_t _n) { return 0.5 * std::sin(2 * Pi * 500.0 * static_cast<double>(_n) / 48000); };
		const auto play = [&](const ChainSettings& _s)
		{
			auto chain = std::make_unique<Chain>(48000.0, true);
			chain->setOversampling(2);
			chain->configure(0, _s);
			chain->hit(0, 127);
			return through(*chain, 8192, tone);
		};
		const bool same = play(wild) == play(near);
		std::printf("  settings past their ends play as their ends: %s\n", same ? "yes" : "no");
		require(same, "unclamped settings");
	}

	// The envelope on the Machinedrum's filter moves the band and stays in its words
	void envelope()
	{
		ChainSettings s = machinedrumChain();
		s.filter.frequency = 30;
		s.filter.width = 30;
		s.filter.depth = 127;
		s.filter.envelope.decay = 40;
		auto chain = std::make_unique<Chain>(44100.0, false);
		chain->configure(0, s);
		chain->hit(0, 127);
		const auto tone = [](const int64_t _n) { return 0.5 * std::sin(2 * Pi * 8000.0 * static_cast<double>(_n) / 44100); };
		const auto y = through(*chain, 16384, tone);
		double early = 0, late = 0;
		for(size_t n = 256; n < 1024; ++n)
			early = std::max(early, std::abs(y[n]));
		for(size_t n = 12000; n < 16000; ++n)
			late = std::max(late, std::abs(y[n]));
		std::printf("  the filter's envelope on the Machinedrum's filter: 8 kHz through the band at %.3f as it opens, %.4f after\n", early, late);
		require(early > 4 * late, "the envelope does not move the Machinedrum's band");

		// The top of everything, and below the bottom: inside the tables
		s.filter.frequency = 127;
		s.filter.depth = 127;
		s.velocityTarget = VelocityTarget::Frequency;
		s.velocityDepth = 127;
		chain->configure(0, s);
		chain->hit(0, 127);
		through(*chain, 8192, tone);
		s.filter.frequency = 0;
		s.filter.depth = 0;
		s.velocityDepth = -127;
		chain->configure(0, s);
		chain->hit(0, 127);
		through(*chain, 8192, tone);
		s.filter.type = FilterType::Syntakt;
		s.filter.keyTrack = 127;
		chain->setNote(0, 127);
		chain->configure(0, s);
		chain->hit(0, 127);
		through(*chain, 8192, tone);
		std::printf("  the filter's parameters past their ends: inside the tables\n");
	}
}

int main()
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	const syntakt::fast::FlushDenormals ftz;
	try
	{
		levels();
		neutral();
		lanes();
		neighbours();
		idleLane();
		order();
		q6();
		mix();
		firstBlockMix();
		q7();
		q9();
		q12();
		secondChange();
		grid();
		envelope();
		depthTarget();
		noteDuringHit();
		wildSettings();
	}
	catch(const std::exception& _e)
	{
		std::printf("FAIL: %s\n", _e.what());
		return 1;
	}
	std::printf("PASS\n");
	return 0;
}
