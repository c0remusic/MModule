#include "edDrumsHouseWaves.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <vector>

namespace elektron::drums
{
	namespace
	{
		constexpr double Rate = 44100.0, Tau = 6.283185307179586;

		uint32_t toWord(const double _x) { return static_cast<uint32_t>(std::llround(_x * 8388608.0)) & 0xffffff; }

		// A raised cosine from 1 to 0 over the last _fade of _count samples, 1 before them
		double fadeOut(const size_t _k, const size_t _count, const size_t _fade)
		{
			return _k + _fade < _count ? 1.0
				: 0.5 + 0.5 * std::cos(Tau / 2 * static_cast<double>(_k + _fade - _count) / static_cast<double>(_fade - 1));
		}

		// A sine from zero whose pitch falls from _from to _to Hz across the words, decaying with _tau samples and faded
		// to zero over its last _fade, so that what follows it starts without a step. _drive above 0 squares it, _rise
		// samples open it, and a steady partial of _ringLevel at _ringHz rides on it
		template<size_t N> void burst(std::array<uint32_t, N>& _words, const double _from, const double _to,
			const double _tau, const size_t _fade, const double _level, const double _drive = 0, const size_t _rise = 1,
			const double _ringHz = 0, const double _ringLevel = 0)
		{
			double phase = 0;
			for(size_t k = 0; k < N; ++k)
			{
				const double wave = _drive > 0 ? std::tanh(_drive * std::sin(phase)) / std::tanh(_drive) : std::sin(phase);
				const double t = static_cast<double>(k);
				const double rise = std::min(t / static_cast<double>(_rise), 1.0);
				const double ring = _ringLevel * std::sin(Tau * _ringHz * t / Rate);
				_words[k] = toWord(fadeOut(k, N, _fade) * rise * (_level * std::exp(-t / _tau) * wave + ring));
				phase += Tau * _from * std::pow(_to / _from, static_cast<double>(k) / static_cast<double>(N)) / Rate;
			}
		}

		struct Mode
		{
			double hz, level, tau;
		};

		struct Band
		{
			double hz, level;
			double y1 = 0, y2 = 0;

			// A two-pole around hz; what drives it has zeros at DC and at half the rate
			double ring(const double _change)
			{
				const double y = _change + 2 * 0.9 * std::cos(Tau * hz / Rate) * y1 - 0.9 * 0.9 * y2;
				y2 = y1;
				y1 = y;
				return level * y;
			}
		};

		// What a struck body gives: decaying sines plus a noise through _bands that decays with _noiseTau, _peak at its
		// highest
		template<size_t N> void strike(std::array<uint32_t, N>& _words, const std::initializer_list<Mode> _modes,
			std::vector<Band> _bands, const double _noiseTau, const size_t _fade, const double _peak)
		{
			uint32_t state = 0x2545f491;
			double last = 0, older = 0, highest = 0;
			std::vector<double> wave(N);
			for(size_t k = 0; k < N; ++k)
			{
				const double t = static_cast<double>(k);
				double x = 0;
				for(const Mode& m : _modes)
					x += std::min(t / 12, 1.0) * m.level * std::exp(-t / m.tau) * std::sin(Tau * m.hz * t / Rate);
				state ^= state << 13;
				state ^= state >> 17;
				state ^= state << 5;
				const double white = static_cast<double>(state) / 2147483648.0 - 1;
				double noise = 0;
				for(Band& band : _bands)
					noise += band.ring(white - older);
				older = last;
				last = white;
				x += std::min(t / 64, 1.0) * std::exp(-t / _noiseTau) * noise;
				wave[k] = fadeOut(k, N, _fade) * x;
				highest = std::max(highest, std::abs(wave[k]));
			}
			for(size_t k = 0; k < N; ++k)
				_words[k] = toWord(_peak * wave[k] / highest);
		}
	}

	void makeHouseWaves(md::voice::TrxTables& _t)
	{
		// TRX-BD's nine blocks, a squared cycle low enough to lead into its sines. The second wave weighs in with a packet
		// word: a plain sine a little higher, decaying
		burst(_t.bdWaveA, 168, 140, 1e9, 20, 0.95, 4, 12, 1550, 0.045);
		burst(_t.bdWaveB, 170, 170, 200, 48, 0.1);
		// TRX-XT's two blocks and TRX-XC's one, each half a cycle into what follows
		burst(_t.xtWave, 345, 345, 1e9, 4, 0.85);
		burst(_t.xcWave, 690, 690, 1e9, 6, 0.85, 2);
		// TRX-SD's rattle, 128 blocks, added to its tone and noise; TRX-RS's rim, 24 blocks, before its high-passes
		strike(_t.sdWave, {}, {{1700, 0.6}, {2800, 1.3}, {4800, 1.0}, {8500, 0.3}}, 900, 256, 0.9);
		strike(_t.rsWave, {{490, 0.6, 900}, {905, 0.5, 600}, {1430, 0.8, 200}, {1940, 0.8, 160}, {2480, 0.6, 120}},
			{{1100, 0.75}, {3200, 0.7}}, 150, 64, 0.98);
	}
}
