#include "edDrumsHouseWaves.h"

#include <array>
#include <cmath>
#include <cstdint>

namespace elektron::drums
{
	namespace
	{
		constexpr double Rate = 44100.0, Tau = 6.283185307179586;

		// A sine from zero whose pitch falls from _from to _to Hz across the words, decaying with _tau samples and faded
		// to zero over its last _fade, so that what follows it starts without a step. _drive above 0 squares it
		template<size_t N> void burst(std::array<uint32_t, N>& _words, const double _from, const double _to,
			const double _tau, const size_t _fade, const double _level, const double _drive = 0)
		{
			double phase = 0;
			for(size_t k = 0; k < N; ++k)
			{
				const double fade = k + _fade < N ? 1.0
					: 0.5 + 0.5 * std::cos(Tau / 2 * static_cast<double>(k + _fade - N) / static_cast<double>(_fade - 1));
				const double wave = _drive > 0 ? std::tanh(_drive * std::sin(phase)) / std::tanh(_drive) : std::sin(phase);
				const double x = _level * fade * std::exp(-static_cast<double>(k) / _tau) * wave;
				_words[k] = static_cast<uint32_t>(std::llround(x * 8388608.0)) & 0xffffff;
				phase += Tau * _from * std::pow(_to / _from, static_cast<double>(k) / static_cast<double>(N)) / Rate;
			}
		}
	}

	void makeHouseWaves(md::voice::TrxTables& _t)
	{
		// TRX-BD's nine blocks, then its sines start from zero. The second wave weighs in with a packet word: the same
		// fall from higher up
		burst(_t.bdWaveA, 2600, 110, 400, 48, 0.99, 4);
		burst(_t.bdWaveB, 3600, 150, 60, 48, 0.1);
		// TRX-XT's two blocks, then TRX-XC's one
		burst(_t.xtWave, 1700, 500, 26, 16, 0.8);
		burst(_t.xcWave, 2400, 900, 14, 8, 0.8);
	}
}
