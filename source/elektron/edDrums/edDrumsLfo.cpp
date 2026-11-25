#include "edDrumsLfo.h"

#include <algorithm>
#include <cmath>

namespace elektron::drums
{
	namespace
	{
		constexpr double Pi = 3.14159265358979323846;
		constexpr int RandomsPerCycle = 16;		// as a Syntakt's RND is heard
		constexpr double FallOctaves = 12;		// as its EXP is

		// splitmix64's mix, as -1 to 1
		double randomOf(const uint64_t _n)
		{
			uint64_t z = _n * 0x9e3779b97f4a7c15ull;
			z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
			z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
			z ^= z >> 31;
			return static_cast<double>(z >> 11) / 4503599627370496.0 - 1;
		}

		double shapeAt(const LfoShape _shape, const double _phase)
		{
			switch(_shape)
			{
			case LfoShape::Sine:		return std::sin(2 * Pi * _phase);
			case LfoShape::Square:		return _phase < 0.5 ? 1 : -1;
			case LfoShape::Saw:			return 1 - 2 * _phase;
			case LfoShape::Exponential:	return std::pow(2.0, -FallOctaves * _phase);
			case LfoShape::Ramp:		return _phase < 0.5 ? 2 * _phase : 0;
			default:					break;
			}
			return _phase < 0.25 ? 4 * _phase : _phase < 0.75 ? 2 - 4 * _phase : 4 * _phase - 4;
		}
	}

	double lfoQuarters(const int _division, const LfoFeel _feel)
	{
		const double length = std::ldexp(1.0, _division - LfoQuarterNote);
		return _feel == LfoFeel::Dotted ? length * 1.5 : _feel == LfoFeel::Triplet ? length * 2 / 3 : length;
	}

	LfoRate lfoDivisionOf(const double _quarters)
	{
		LfoRate nearest;
		if(_quarters >= lfoQuarters(LfoDivisions - 1, LfoFeel::Dotted))
		{
			nearest.division = LfoDivisions - 1;
			nearest.feel = LfoFeel::Dotted;
			return nearest;
		}
		double distance = HUGE_VAL;
		for(int d = 0; d < LfoDivisions; ++d)
			for(const auto feel : {LfoFeel::Straight, LfoFeel::Dotted, LfoFeel::Triplet})
			{
				const double away = std::abs(std::log(_quarters / lfoQuarters(d, feel)));
				if(away < distance)
				{
					distance = away;
					nearest.division = d;
					nearest.feel = feel;
				}
			}
		return nearest;
	}

	LfoRate lfoRateOfSyntakt(const int _speed, const int _multiplier)
	{
		constexpr int Multipliers = 12;
		constexpr double CycleOfAQuarter = 512;		// SPD x MULT
		const bool fixed = _multiplier >= Multipliers;
		const double cycles = std::abs(_speed) * std::ldexp(1.0, fixed ? _multiplier - Multipliers : _multiplier) / CycleOfAQuarter;
		if(cycles == 0)
			return lfoDivisionOf(HUGE_VAL);
		if(!fixed)
			return lfoDivisionOf(1 / cycles);
		LfoRate rate;
		rate.sync = false;
		rate.hz = cycles * 120 / 60;
		return rate;
	}

	LfoRate lfoRateOfMachinedrum(const int _speed)
	{
		// A fit, as near as a division reads it: a cycle a quarter at 44, twice as fast every 19 steps past 64
		const double speed = std::clamp(_speed, 0, 127);
		return lfoDivisionOf(44 / (speed * std::exp(std::max(0.0, speed - 64) / 28)));
	}

	void Lfo::set(const LfoSettings& _settings, const uint64_t _seed)
	{
		m_rate = _settings.rate;
		m_rate.division = std::clamp(m_rate.division, 0, LfoDivisions - 1);
		m_shape = _settings.shape;
		m_mode = _settings.mode;
		m_length = lfoQuarters(m_rate.division, m_rate.feel);
		m_seed = _seed;
		m_holding = m_holding && m_mode == LfoMode::Hold;
		m_atStop = false;
	}

	bool Lfo::onTransport() const
	{
		return m_rate.sync && !m_modulated && (m_mode == LfoMode::Free || m_mode == LfoMode::Hold);
	}

	void Lfo::modulate(const double _speed, const bool _modulated)
	{
		m_speed = _speed;
		m_modulated = _modulated;
	}

	double Lfo::end() const
	{
		return m_mode == LfoMode::One ? 1 : m_mode == LfoMode::Half ? 0.5 : 0;
	}

	void Lfo::trig(const double _position)
	{
		if(m_mode == LfoMode::Free)
			return;
		if(m_mode == LfoMode::Hold)
		{
			if(onTransport())
				m_cycles = _position / m_length;
			m_holding = false;
			m_held = value();
			m_holding = true;
			return;
		}
		m_cycles = 0;
		m_atStop = false;
		++m_restarts;
	}

	void Lfo::advance(const double _position, const double _quarters, const double _seconds)
	{
		if(m_atStop)
			return;
		if(onTransport())
		{
			m_cycles = _position / m_length;
			return;
		}
		m_cycles += (m_rate.sync ? _quarters / m_length : m_rate.hz * _seconds) * m_speed;
		const double stop = end();
		if(stop != 0 && m_cycles >= stop)
		{
			m_cycles = stop;
			m_atStop = m_shape != LfoShape::Random;
		}
	}

	double Lfo::phase() const
	{
		const double stop = end();
		return stop != 0 && m_cycles >= stop ? stop : m_cycles - std::floor(m_cycles);
	}

	double Lfo::value() const
	{
		if(m_holding)
			return m_held;
		if(m_atStop)
			return m_mode == LfoMode::Half && m_shape == LfoShape::Ramp ? 1 : 0;
		if(m_shape == LfoShape::Random)
		{
			const auto draw = static_cast<int64_t>(std::floor(m_cycles * RandomsPerCycle));
			return randomOf(static_cast<uint64_t>(draw) + (m_restarts << 40) + (m_seed << 58));
		}
		return shapeAt(m_shape, phase());
	}

	// ---- 16 Tracks' ----

	void LfoBank::wait(const int _track, const int _target)
	{
		m_targets[static_cast<size_t>(_track)] |= uint64_t{1} << _target;
		m_waiting |= 1u << _track;
	}

	void LfoBank::taken(const int _track, const int _target)
	{
		auto& targets = m_targets[static_cast<size_t>(_track)];
		targets &= ~(uint64_t{1} << _target);
		if(!targets)
			m_waiting &= ~(1u << _track);
	}

	// Its output into its target's sum. An LFO 2 adds what its LFO 1 gives its depth to its own. A sum for an LFO 2 waits
	// for no Track: the bank takes it itself
	void LfoBank::move(const int _lfo)
	{
		auto& run = m_runs[static_cast<size_t>(_lfo)];
		int depth = run.depth * 128;
		if(_lfo & 1)
			depth = std::clamp(depth + m_sum[static_cast<size_t>(_lfo / 2)][static_cast<size_t>(LfoTarget::Lfo2Depth)],
				-127 * 128, 127 * 128);
		const int offset = static_cast<int>(std::lround(run.lfo.value() * depth));
		if(offset == run.offset || run.target == LfoTarget::None)
			return;
		m_sum[static_cast<size_t>(run.track)][static_cast<size_t>(run.target)] += offset - run.offset;
		run.offset = offset;
		if(!aimsAtLfo(run.target))
			wait(run.track, static_cast<int>(run.target));
	}

	void LfoBank::leave(Run& _run)
	{
		if(_run.target == LfoTarget::None || !_run.offset)
			return;
		m_sum[static_cast<size_t>(_run.track)][static_cast<size_t>(_run.target)] -= _run.offset;
		_run.offset = 0;
		if(!aimsAtLfo(_run.target))
			wait(_run.track, static_cast<int>(_run.target));
	}

	void LfoBank::set(const int _track, const int _index, const LfoSettings& _settings)
	{
		const int lfo = 2 * _track + _index;
		auto& run = m_runs[static_cast<size_t>(lfo)];
		// An LFO 2 aims at no LFO; LFO 1 at its own Track's
		const auto target = aimsAtLfo(_settings.target) && _index ? LfoTarget::None : _settings.target;
		const int track = _settings.track < 0 || aimsAtLfo(target) ? _track : std::min(_settings.track, Tracks - 1);
		const int depth = target == LfoTarget::None ? 0 : std::clamp(_settings.depth, -127, 127);
		run.lfo.set(_settings, static_cast<uint64_t>(lfo));
		run.depth = depth;
		// An LFO 2 at depth 0 whose LFO 1 moves its depth keeps its share
		const bool depthMoved = _index && running(lfo - 1)
			&& m_runs[static_cast<size_t>(lfo - 1)].target == LfoTarget::Lfo2Depth;
		if((!depth && !depthMoved) || track != run.track || target != run.target)
		{
			leave(run);
			run.track = track;
			run.target = target;
		}
		m_on = depth ? m_on | 1u << lfo : m_on & ~(1u << lfo);

		// LFO 2 runs at depth 0 too while LFO 1 moves its depth
		const int first = 2 * _track;
		auto& second = m_runs[static_cast<size_t>(first + 1)];
		const bool on = second.target != LfoTarget::None
			&& (second.depth || (running(first) && m_runs[static_cast<size_t>(first)].target == LfoTarget::Lfo2Depth));
		if(!on)
			leave(second);
		m_on = on ? m_on | 2u << first : m_on & ~(2u << first);
	}

	void LfoBank::trig(const int _track, const double _position)
	{
		for(int lfo = 2 * _track; lfo < 2 * _track + 2; ++lfo)
		{
			if(!running(lfo))
				continue;
			m_runs[static_cast<size_t>(lfo)].lfo.trig(_position);
			move(lfo);
		}
	}

	// LFO 1 before LFO 2: what LFO 1 gives its LFO 2 is the block's own
	void LfoBank::step(const uint32_t _tracks, const double _position, const double _quarters, const double _seconds)
	{
		constexpr double SpeedOctaves = 2;		// each way, at depth 127
		for(uint32_t bits = m_on, i = 0; bits; bits >>= 1, ++i)
		{
			if(!(bits & 1) || !(_tracks >> (i / 2) & 1))
				continue;
			auto& run = m_runs[i];
			if(i & 1)
			{
				const int speed = m_sum[i / 2][static_cast<size_t>(LfoTarget::Lfo2Speed)];
				run.lfo.modulate(std::exp2(SpeedOctaves * speed / (127.0 * 128)),
					running(static_cast<int>(i) - 1) && m_runs[i - 1].target == LfoTarget::Lfo2Speed);
			}
			run.lfo.advance(_position, _quarters, _seconds);
			move(static_cast<int>(i));
		}
	}

	double LfoBank::phase(const int _track, const int _index) const
	{
		return m_runs[static_cast<size_t>(2 * _track + _index)].lfo.phase();
	}
}
