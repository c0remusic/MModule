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

		// _x from -1 to 1 with its peaks rounded, as it is at a _drive of 0
		double driven(const double _x, const double _drive)
		{
			return _drive > 0 ? std::tanh(_drive * _x) / std::tanh(_drive) : _x;
		}

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
				const double wave = driven(std::sin(phase), _drive);
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
			double hz, level, tau;
			double y1 = 0, y2 = 0, last = 0;

			// A two-pole around hz, driven by _white with a zero at DC; its output decays with tau samples, _t being
			// the wave's sample. Each band takes a white of its own: bands sharing one cancel between them, and as
			// their decays differ that notch moves and sounds like a phaser
			double ring(const double _white, const double _t)
			{
				const double y = _white - last + 2 * 0.9 * std::cos(Tau * hz / Rate) * y1 - 0.9 * 0.9 * y2;
				y2 = y1;
				y1 = y;
				last = _white;
				return level * std::exp(-_t / tau) * y;
			}
		};

		// What a struck body gives: decaying sines plus a noise through _bands, opening over _rise samples, _peak at its
		// highest. _drive above 0 rounds its peaks, for a louder wave within the words' range
		template<size_t N> void strike(std::array<uint32_t, N>& _words, const std::initializer_list<Mode> _modes,
			std::vector<Band> _bands, const double _rise, const size_t _fade, const double _peak,
			const double _drive = 0)
		{
			uint32_t state = 0x2545f491;
			double highest = 0;
			std::vector<double> wave(N);
			for(size_t k = 0; k < N; ++k)
			{
				const double t = static_cast<double>(k);
				double x = 0;
				for(const Mode& m : _modes)
					x += m.level * std::exp(-t / m.tau) * std::sin(Tau * m.hz * t / Rate);
				double noise = 0;
				for(Band& band : _bands)
				{
					state ^= state << 13;
					state ^= state >> 17;
					state ^= state << 5;
					noise += band.ring(static_cast<double>(state) / 2147483648.0 - 1, t);
				}
				wave[k] = fadeOut(k, N, _fade) * std::min(t / _rise, 1.0) * (x + noise);
				highest = std::max(highest, std::abs(wave[k]));
			}
			for(size_t k = 0; k < N; ++k)
				_words[k] = toWord(_peak * driven(wave[k] / highest, _drive));
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
		strike(_t.sdWave, {}, {{1450, 0.5, 450}, {2050, 0.8, 700}, {3200, 0.9, 950}, {4700, 1.2, 650},
			{9000, 0.45, 950}, {14000, 0.22, 450}}, 40, 256, 0.98, 1);
		strike(_t.rsWave, {{250, -0.7, 500}, {490, 0.5, 800}, {945, 1.1, 260}, {1063, 0.3, 110}, {1450, 1.3, 170},
			{1825, 0.6, 200}, {2362, 0.85, 140}, {2989, 0.6, 140}, {3985, 0.5, 110}}, {{2600, 0.3, 170}}, 20, 64, 0.98);
	}
}
