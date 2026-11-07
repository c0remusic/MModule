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
			double y1 = 0, y2 = 0, last = 0, older = 0;

			// A two-pole around hz, driven by _white with a zero at DC and the top octave eased; its output decays with
			// tau samples, _t being the wave's sample. Each band takes a white of its own: bands sharing one cancel
			// between them, and as their decays differ that notch moves and sounds like a phaser
			double ring(const double _white, const double _t)
			{
				const double change = _white - last + 0.17 * (last - older);
				const double y = change + 2 * 0.9 * std::cos(Tau * hz / Rate) * y1 - 0.9 * 0.9 * y2;
				y2 = y1;
				y1 = y;
				older = last;
				last = _white;
				return level * std::exp(-_t / tau) * y;
			}
		};

		// What a struck body gives: decaying sines plus a noise through _bands, opening over _rise samples, _peak at its
		// highest. _drive above 0 rounds its peaks, for a louder wave within the words' range
		void strike(uint32_t* const _words, const size_t _count, const std::initializer_list<Mode> _modes,
			std::vector<Band> _bands, const double _rise, const size_t _fade, const double _peak,
			const double _drive = 0)
		{
			uint32_t state = 0x2545f491;
			double highest = 0;
			std::vector<double> wave(_count);
			for(size_t k = 0; k < _count; ++k)
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
				wave[k] = fadeOut(k, _count, _fade) * std::min(t / _rise, 1.0) * (x + noise);
				highest = std::max(highest, std::abs(wave[k]));
			}
			for(size_t k = 0; k < _count; ++k)
				_words[k] = toWord(_peak * driven(wave[k] / highest, _drive));
		}
	}

	void makeHouseWaves(md::voice::TrxTables& _t)
	{
		// TRX-CH's and CY's metal: six squares, each a cycle of its loop, even but for the lowest
		for(const auto& wave : md::voice::TrxTables::ChWaves)
		{
			const uint32_t high = wave.offset ? wave.size / 2 : 72;
			for(uint32_t k = 0; k < wave.size; ++k)
				_t.chWaves[wave.offset + k] = toWord(k < high ? 0.9999 : -0.9999);
		}
		// TRX-BD's nine blocks, a squared cycle low enough to lead into its sines. The second wave weighs in with a
		// packet word: a plain sine a little higher, decaying
		burst(_t.bdWaveA, 168, 140, 1e9, 20, 0.95, 4, 12, 1550, 0.045);
		burst(_t.bdWaveB, 170, 170, 200, 48, 0.1);
		// TRX-XT's two blocks and TRX-XC's one, each half a cycle into what follows
		burst(_t.xtWave, 345, 345, 1e9, 4, 0.85);
		burst(_t.xcWave, 690, 690, 1e9, 6, 0.85, 2);
		// TRX-SD's rattle, 128 blocks, added to its tone and noise; TRX-RS's rim, 24 blocks, before its high-passes
		strike(_t.sdWave.data(), _t.sdWave.size(), {}, {{1350, 0.5, 450}, {1900, 0.8, 700}, {3000, 0.9, 950},
			{4400, 1.2, 650}, {9000, 0.35, 950}, {14000, 0.1, 450}}, 40, 256, 0.98, 1);
		strike(_t.rsWave.data(), _t.rsWave.size(), {{250, -0.7, 500}, {490, 0.5, 800}, {945, 1.1, 260}, {1063, 0.3, 110},
			{1450, 1.3, 170}, {1825, 0.6, 200}, {2362, 0.85, 140}, {2989, 0.6, 140}, {3985, 0.5, 110}},
			{{2600, 0.3, 170}}, 20, 64, 0.98);
	}

	std::vector<uint32_t> makeHousePi()
	{
		using Bank = md::voice::PiBank;
		std::vector<uint32_t> words(Bank::Words);

		// A range's first words: eight modes at _ratios of _pitch, heard in the machine's output, damped by _ring from
		// _first up to _split, ringing as long as the bank allows elsewhere, and gains that fall by _fall from _gain, the
		// second mode's by _second more, held inside what a word can say
		struct Drum
		{
			const double* ratios;
			double pitch;
			uint32_t ring, first, split;
			double gain, fall, second;
		};
		static constexpr double BdModes[8] = {1, 1.57, 2.2, 2.85, 3.8, 4.4, 5.2, 5.8};
		static constexpr double SdModes[8] = {1, 1.56, 1.87, 2.76, 4.22, 4.6, 7, 7.3};
		static constexpr double MtModes[8] = {1, 1.58, 2, 2.27, 3.04, 4.49, 6.6, 6.9};
		static constexpr double MlModes[8] = {1, 1.17, 1.28, 1.69, 1.84, 1.94, 3.48, 3.62};
		const auto modes = [](uint32_t* const _head, const Drum& _d)
		{
			for(uint32_t m = 0; m < 8; ++m)
			{
				_head[m] = static_cast<uint32_t>(std::llround(_d.pitch * _d.ratios[m])) & 0xffffff;
				_head[8 + m] = m >= _d.first && m < _d.split ? _d.ring : 511;
				_head[16 + m] = toWord(std::clamp(_d.gain * std::pow(_d.fall, m) * (m == 1 ? _d.second : 1), -0.999, 0.999));
			}
		};
		const auto at = [&](const uint32_t _range)
		{
			uint32_t offset = 0;
			for(uint32_t i = 0; i < _range; ++i)
				offset += Bank::Ranges[i].words;
			return words.data() + offset;
		};

		constexpr uint32_t BdStrike = 1356, SdStrike = 750, MtStrike = 1675, MlStrike = 2236, RsStrike = 586;
		static_assert(24 + BdStrike <= Bank::Ranges[0].words && 24 + SdStrike <= Bank::Ranges[1].words
			&& 48 + MtStrike <= Bank::Ranges[2].words && 24 + MlStrike <= Bank::Ranges[3].words
			&& 24 + RsStrike <= Bank::Ranges[4].words);

		uint32_t* const bd = at(0);
		modes(bd, {BdModes, 96000, 110, 1, 4, -0.3, 0.25, 13});
		strike(bd + 24, BdStrike, {{54, 0.5, 500}}, {{500, 1, 220}, {800, 0.8, 180}, {1200, 0.3, 140}}, 96, 64, 0.62, 2);

		uint32_t* const sd = at(1);
		modes(sd, {SdModes, 177000, 25, 0, 5, -0.95, 0.5, 1});
		strike(sd + 24, SdStrike, {{220, 1, 250}, {470, 0.7, 220}, {810, 0.35, 180}, {1230, 0.2, 150}, {1790, 0.12, 120}},
			{}, 30, 64, 0.5);

		// P-I-MT's two sets of modes, mixed: a pitch, then a damping and a gain, in that order
		uint32_t* const mt = at(2);
		constexpr double MtPitch = -225000;
		uint32_t head[24];
		modes(head, {MtModes, MtPitch, 256, 0, 8, -0.14, 0.62, 1});
		for(uint32_t m = 0; m < 8; ++m)
		{
			// A second of these words is twice the first for the modes that move with the mix, about their pitch
			const double pitch = MtPitch * MtModes[m];
			const bool moves = m % 3 == 1;
			mt[m] = static_cast<uint32_t>(std::llround(moves ? pitch / 1.5 : pitch)) & 0xffffff;
			mt[8 + m] = static_cast<uint32_t>(std::llround(moves ? pitch * 4 / 3 : pitch)) & 0xffffff;
			mt[16 + m] = head[8 + m];
			mt[24 + m] = 511 - 50 * m;
			mt[32 + m] = head[16 + m];
			mt[40 + m] = toWord(-0.14 * std::pow(0.68, m));
		}
		strike(mt + 48, MtStrike, {{170, 0.9, 500}, {285, 1, 450}, {400, 0.9, 400}, {480, 0.8, 350}, {590, 0.6, 300},
			{890, 0.5, 250}, {1250, 0.5, 200}, {1700, 0.4, 170}, {2300, 0.3, 140}},
			{{3200, 1.1, 110}, {6500, 1.1, 90}}, 4, 64, 0.85);

		uint32_t* const ml = at(3);
		modes(ml, {MlModes, 430000, 511, 0, 0, -0.999, 1, 1});
		strike(ml + 24, MlStrike, {{144, 0.3, 500}, {430, 0.3, 400}},
			{{850, 1, 500}, {1600, 0.8, 500}, {10000, 0.8, 700}, {14000, 0.8, 600}, {18000, 0.8, 500}}, 48, 64, 0.8);

		uint32_t* const rs = at(4);
		modes(rs, {SdModes, 177000, 25, 0, 5, -0.13, 0.5, 1});
		strike(rs + 24, RsStrike, {{264, 0.25, 90}}, {{420, 1, 120}, {700, 0.9, 110}, {1000, 0.3, 100}}, 96, 64, 0.28);

		// $24DA14's place, a one-pole's word by the index: e^(-2 pi f / rate) for a cutoff of our own that climbs with it
		uint32_t* const table = words.data() + Bank::ProgramWords;
		for(size_t i = 0; i < Bank::OnePoleWords; ++i)
		{
			const double x = static_cast<double>(i);
			const double cutoff = 26 + 1.9 * x + 0.012 * x * x + 0.01 * std::exp(x / 18);
			table[i] = toWord(std::exp(-Tau * cutoff / Rate));
		}
		return words;
	}
}
