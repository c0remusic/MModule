#include "edDrumsSequencer.h"

#include <algorithm>
#include <cmath>

namespace elektron::drums
{
	namespace
	{
		constexpr double Tolerance = 1e-3;		// samples: a host's rounding, under which two blocks agree on a sample
		constexpr double Epsilon = 1e-9;

		// speeds 1/8, 1/4, 1/2, 3/4, 1, 3/2, 2 as numerator over denominator
		constexpr int SpeedNumerator[] = {1, 1, 1, 3, 1, 3, 2};
		constexpr int SpeedDenominator[] = {8, 4, 2, 4, 1, 2, 1};

		uint64_t mix(uint64_t _z)
		{
			_z += 0x9e3779b97f4a7c15ull;
			_z = (_z ^ (_z >> 30)) * 0xbf58476d1ce4e5b9ull;
			_z = (_z ^ (_z >> 27)) * 0x94d049bb133111ebull;
			return _z ^ (_z >> 31);
		}

		int sampleOf(const double _time, const double _blockStart, const double _samplesPerQuarter)
		{
			return static_cast<int>(std::ceil((_time - _blockStart) * _samplesPerQuarter - Tolerance));
		}
	}

	double stepQuarters(const StepScale _scale)
	{
		const int i = std::min(static_cast<int>(_scale), static_cast<int>(StepScale::Count) - 1);
		return 0.25 * SpeedDenominator[i] / SpeedNumerator[i];
	}

	// ---- Pattern

	Pattern::Pattern()
	{
		trackLength.fill(16);
		trackScale.fill(StepScale::One);
	}

	const Lock* Pattern::locks(const int _track, const int _index) const
	{
		const Step& s = step(_track, _index);
		return s.lockCount ? m_locks.data() + s.lockFirst : nullptr;
	}

	void Pattern::setLock(const int _track, const int _index, const uint16_t _parameter, const float _value)
	{
		Step& s = step(_track, _index);
		for(uint32_t i = s.lockFirst; i < s.lockFirst + s.lockCount; ++i)
		{
			if(m_locks[i].parameter != _parameter)
				continue;
			m_locks[i].value = _value;
			return;
		}
		// Appended at the pool's end: a step whose locks are not last moves them there first
		if(s.lockCount && s.lockFirst + s.lockCount != m_locks.size())
		{
			const uint32_t from = s.lockFirst;
			s.lockFirst = static_cast<uint32_t>(m_locks.size());
			for(uint32_t i = 0; i < s.lockCount; ++i)
				m_locks.push_back(m_locks[from + i]);
		}
		else if(!s.lockCount)
		{
			s.lockFirst = static_cast<uint32_t>(m_locks.size());
		}
		m_locks.push_back({_parameter, _value});
		++s.lockCount;
	}

	bool Pattern::removeLock(const int _track, const int _index, const uint16_t _parameter)
	{
		Step& s = step(_track, _index);
		for(uint32_t i = s.lockFirst; i < s.lockFirst + s.lockCount; ++i)
		{
			if(m_locks[i].parameter != _parameter)
				continue;
			m_locks.erase(m_locks.begin() + i);
			--s.lockCount;
			for(auto& t : m_steps)
				for(auto& o : t)
					if(o.lockCount && o.lockFirst > i)
						--o.lockFirst;
			return true;
		}
		return false;
	}

	void Pattern::setTrig(const int _track, const int _index, const bool _on, const int _velocity)
	{
		Step& s = step(_track, _index);
		s.trig = _on;
		if(_on)
		{
			s.velocity = static_cast<uint8_t>(std::clamp(_velocity, 1, 127));
			return;
		}
		s.lockCount = 0;
		compact();
	}

	void Pattern::clearTrack(const int _track)
	{
		m_steps[_track].fill(Step());
		compact();
	}

	void Pattern::clear()
	{
		for(auto& t : m_steps)
			t.fill(Step());
		m_locks.clear();
	}

	void Pattern::copyTrackFrom(const Pattern& _from, const int _fromTrack, const int _toTrack)
	{
		if(&_from == this && _fromTrack == _toTrack)
			return;
		const auto source = _from.m_steps[_fromTrack];
		const std::vector<Lock> sourceLocks = _from.m_locks;
		m_steps[_toTrack] = source;
		trackLength[_toTrack] = _from.trackLength[_fromTrack];
		trackScale[_toTrack] = _from.trackScale[_fromTrack];
		for(Step& s : m_steps[_toTrack])
		{
			const uint32_t first = s.lockFirst;
			s.lockFirst = static_cast<uint32_t>(m_locks.size());
			for(uint32_t i = 0; i < s.lockCount; ++i)
				m_locks.push_back(sourceLocks[first + i]);
		}
		compact();
	}

	// Rebuilds the pool from the steps that use it
	void Pattern::compact()
	{
		std::vector<Lock> pool;
		pool.reserve(m_locks.size());
		for(auto& t : m_steps)
		{
			for(Step& s : t)
			{
				const uint32_t first = static_cast<uint32_t>(pool.size());
				for(uint32_t i = 0; i < s.lockCount; ++i)
					pool.push_back(m_locks[s.lockFirst + i]);
				s.lockFirst = first;
			}
		}
		m_locks.swap(pool);
	}

	// ---- Sequencer

	Sequencer::Sequencer() : m_patterns(SeqPatterns)
	{
		m_mute.fill(false);
		m_last.fill(false);
	}

	void Sequencer::copyPattern(const int _from, const int _to)
	{
		if(_from != _to)
			m_patterns[_to] = m_patterns[_from];
	}

	void Sequencer::clearPattern(const int _index)
	{
		m_patterns[_index].clear();
	}

	void Sequencer::setMute(const int _track, const bool _on)
	{
		m_mute[_track] = _on;
	}

	void Sequencer::requestPattern(const int _index)
	{
		if(_index >= 0 && _index < SeqPatterns)
			m_requested.store(_index);
	}

	int Sequencer::stepAt(const int _track, const double _quarters) const
	{
		if(!m_playing)
			return -1;
		const Pattern& p = m_patterns[m_current];
		const double length = stepQuarters(p.trackScale[_track]);
		const int n = std::max(0, static_cast<int>(std::floor((_quarters - m_patternStart) / length + Epsilon)));
		return n % std::clamp(p.trackLength[_track], 1, SeqSteps);
	}

	void Sequencer::copyEdits(const Sequencer& _from)
	{
		m_patterns = _from.m_patterns;
		m_mute = _from.m_mute;
		m_seed = _from.m_seed;
		m_fill = _from.m_fill;
	}

	void Sequencer::takePlayback(const Sequencer& _from)
	{
		m_last = _from.m_last;
		m_playing = _from.m_playing;
		m_current = _from.m_current;
		m_requested.store(_from.m_requested.load());
		m_expected = _from.m_expected;
		m_patternStart = _from.m_patternStart;
		m_pass = _from.m_pass;
		m_retrigs = _from.m_retrigs;
		m_retrigCount = _from.m_retrigCount;
	}

	// The transport starts, or jumps: the pattern restarts on its own grid under the position
	void Sequencer::restart(const double _quarters)
	{
		if(!m_playing)
		{
			const int request = m_requested.exchange(-1);
			if(request >= 0)
				m_current = request;
		}
		const double length = std::clamp(m_patterns[m_current].masterLength, 1, 64) * 0.25;
		const double pass = std::floor(_quarters / length + Epsilon);
		m_patternStart = pass * length;
		m_pass = static_cast<int64_t>(pass);
		m_last.fill(false);
		m_retrigCount = 0;
	}

	void Sequencer::process(const Transport& _t, Events& _out)
	{
		_out.count = 0;
		_out.dropped = 0;
		if(!_t.playing || _t.length <= 0 || _t.bpm <= 0)
		{
			m_playing = false;
			return;
		}

		const double samplesPerQuarter = _t.sampleRate * 60 / _t.bpm;
		const double blockStart = _t.quarters;
		// A move back, or forward by a step or more, is a jump. Less forward (a tempo change inside the last block) carries
		// the pattern on from the position, on its own grid
		const double drift = blockStart - m_expected;
		if(!m_playing || drift * samplesPerQuarter <= -0.5 || drift >= stepQuarters(StepScale::One))
			restart(blockStart);
		m_playing = true;
		m_expected = blockStart + _t.length / samplesPerQuarter;

		int lo = 0;
		while(lo < _t.length)
		{
			const Pattern& p = m_patterns[m_current];
			Segment s;
			s.blockStart = blockStart;
			s.blockEnd = m_expected;
			s.samplesPerQuarter = samplesPerQuarter;
			s.patternStart = m_patternStart;
			s.patternEnd = m_patternStart + std::clamp(p.masterLength, 1, 64) * 0.25;
			s.patternStartSample = sampleOf(s.patternStart, blockStart, samplesPerQuarter);
			s.patternEndSample = sampleOf(s.patternEnd, blockStart, samplesPerQuarter);
			s.lo = lo;
			s.hi = std::min(_t.length, s.patternEndSample);

			if(s.hi > s.lo)
			{
				collect(p, s);
				// Chronological order: Pre and Nei read what the steps before them decided
				for(int i = 1; i < m_candidateCount; ++i)
				{
					const Candidate c = m_candidates[i];
					int j = i - 1;
					for(; j >= 0 && (m_candidates[j].sample > c.sample
						|| (m_candidates[j].sample == c.sample && m_candidates[j].track > c.track)); --j)
						m_candidates[j + 1] = m_candidates[j];
					m_candidates[j + 1] = c;
				}
				for(int i = 0; i < m_candidateCount; ++i)
					emit(p, m_candidates[i], s, _out);
			}

			if(s.patternEndSample >= _t.length)
				break;

			// The pattern ends inside the block: the next starts, the one asked for if any
			m_patternStart = s.patternEnd;
			const int request = m_requested.exchange(-1);
			if(request >= 0)
			{
				m_current = request;
				m_pass = 0;
			}
			else
			{
				++m_pass;
			}
			lo = std::max(lo, s.patternEndSample);
		}

		flushRetrigs(blockStart, samplesPerQuarter, _t.length, _out);

		for(int i = 1; i < _out.count; ++i)
		{
			const Event e = _out.events[i];
			int j = i - 1;
			for(; j >= 0 && (_out.events[j].offset > e.offset
				|| (_out.events[j].offset == e.offset && _out.events[j].track > e.track)); --j)
				_out.events[j + 1] = _out.events[j];
			_out.events[j + 1] = e;
		}
	}

	// The steps of the pattern whose sample falls in the segment
	void Sequencer::collect(const Pattern& _p, const Segment& _s)
	{
		m_candidateCount = 0;
		const double patternLength = _s.patternEnd - _s.patternStart;
		for(int t = 0; t < SeqTracks; ++t)
		{
			const double length = stepQuarters(_p.trackScale[t]);
			const int trackLength = std::clamp(_p.trackLength[t], 1, SeqSteps);
			const int steps = static_cast<int>(std::ceil(patternLength / length - Epsilon));
			const double swingShift = length * (2 * std::clamp(_p.swing, 50, 80) / 100.0 - 1);

			// Swing and micro-timing move a step by less than two steps
			const int first = std::max(0, static_cast<int>(std::floor((_s.blockStart - _s.patternStart) / length)) - 2);
			const int last = std::min(steps - 1,
				static_cast<int>(std::ceil((_s.blockEnd - _s.patternStart) / length)) + 2);
			for(int n = first; n <= last; ++n)
			{
				const Step& step = _p.step(t, n % trackLength);
				if(!step.trig && !step.lockCount)
					continue;

				double time = _s.patternStart + n * length + ((n & 1) ? swingShift : 0)
					+ std::clamp<int>(step.timing, -(TimingUnits - 1), TimingUnits - 1) * length / TimingUnits;
				time = std::clamp(time, _s.patternStart, _s.patternEnd);
				int sample = sampleOf(time, _s.blockStart, _s.samplesPerQuarter);
				sample = std::clamp(sample, _s.patternStartSample, std::max(_s.patternStartSample, _s.patternEndSample - 1));
				if(sample < _s.lo || sample >= _s.hi || m_candidateCount == MaxCandidates)
					continue;
				m_candidates[m_candidateCount++] = {sample, t, n, std::min(time, _s.patternEnd)};
			}
		}
	}

	bool Sequencer::evaluate(const Pattern& _p, const Candidate& _c)
	{
		const int trackLength = std::clamp(_p.trackLength[_c.track], 1, SeqSteps);
		const int index = _c.n % trackLength;
		const Condition& cond = _p.step(_c.track, index).condition;
		if(cond.kind == ConditionKind::Always)
			return true;

		const int steps = static_cast<int>(std::ceil(
			std::clamp(_p.masterLength, 1, 64) * 0.25 / stepQuarters(_p.trackScale[_c.track]) - Epsilon));
		const int64_t loops = (steps + trackLength - 1) / trackLength;
		const int64_t cycle = m_pass * loops + _c.n / trackLength;

		bool passed = false;
		switch(cond.kind)
		{
		case ConditionKind::Always:		passed = true; break;
		case ConditionKind::Probability:
		{
			const uint64_t r = mix(mix(mix(m_seed) + static_cast<uint64_t>(cycle)) + (static_cast<uint64_t>(_c.track) << 8 | index));
			passed = (r >> 11) % 100 < cond.a;
			break;
		}
		case ConditionKind::Ratio:		passed = cond.b > 0 && cycle % cond.b == (cond.a ? cond.a - 1 : 0); break;
		case ConditionKind::Fill:		passed = m_fill; break;
		case ConditionKind::NotFill:	passed = !m_fill; break;
		case ConditionKind::Pre:		passed = m_last[_c.track]; break;
		case ConditionKind::NotPre:		passed = !m_last[_c.track]; break;
		case ConditionKind::Nei:		passed = _c.track > 0 && m_last[_c.track - 1]; break;
		case ConditionKind::NotNei:		passed = !(_c.track > 0 && m_last[_c.track - 1]); break;
		case ConditionKind::First:		passed = cycle == 0; break;
		case ConditionKind::NotFirst:	passed = cycle != 0; break;
		}
		m_last[_c.track] = passed;
		return passed;
	}

	void Sequencer::emit(const Pattern& _p, const Candidate& _c, const Segment& _s, Events& _out)
	{
		const bool passed = evaluate(_p, _c);
		if(!passed || m_mute[_c.track])
			return;
		if(_out.count == Events::Capacity)
		{
			++_out.dropped;
			return;
		}

		const int trackLength = std::clamp(_p.trackLength[_c.track], 1, SeqSteps);
		const int index = _c.n % trackLength;
		const Step& step = _p.step(_c.track, index);
		const double length = stepQuarters(_p.trackScale[_c.track]);

		Event& e = _out.events[_out.count++];
		e = Event();
		e.track = static_cast<uint8_t>(_c.track);
		e.offset = static_cast<uint32_t>(_c.sample);
		e.trig = step.trig;
		e.velocity = step.trig ? static_cast<uint8_t>(std::clamp<int>(step.velocity, 1, 127)) : 0;
		e.accent = step.trig && step.accent;
		e.locks = _p.locks(_c.track, index);
		e.lockCount = step.lockCount;

		if(!step.trig)
			return;

		if(step.slide)
		{
			// To the next trig of the Track, a lap of its steps at most
			for(int d = 1; d <= trackLength; ++d)
			{
				const int next = (index + d) % trackLength;
				if(!_p.step(_c.track, next).trig)
					continue;
				e.slide = true;
				e.slideSamples = static_cast<uint32_t>(std::lround(d * length * _s.samplesPerQuarter));
				e.nextLocks = _p.locks(_c.track, next);
				e.nextLockCount = _p.step(_c.track, next).lockCount;
				break;
			}
		}

		// The Hits after the first wait for their sample, in this block or a later one
		const int hits = std::clamp<int>(step.ratchets, 1, 8);
		const double spacing = length / std::max<int>(hits, step.ratchetRate);
		for(int k = 1; k < hits; ++k)
		{
			const double time = _c.time + k * spacing;
			if(time >= _s.patternEnd || m_retrigCount == MaxRetrigs)
				break;
			m_retrigs[m_retrigCount++] = {time, e.track,
				static_cast<uint8_t>(std::clamp(e.velocity + k * step.ratchetVelocity, 1, 127)), e.accent,
				static_cast<uint8_t>(m_current), static_cast<uint8_t>(index)};
		}
	}

	void Sequencer::flushRetrigs(const double _blockStart, const double _samplesPerQuarter, const int _length, Events& _out)
	{
		for(int i = 0; i < m_retrigCount;)
		{
			const Retrig r = m_retrigs[i];
			const int sample = std::max(0, sampleOf(r.time, _blockStart, _samplesPerQuarter));
			if(sample >= _length)
			{
				++i;
				continue;
			}
			m_retrigs[i] = m_retrigs[--m_retrigCount];
			if(m_mute[r.track])
				continue;
			if(_out.count == Events::Capacity)
			{
				++_out.dropped;
				continue;
			}
			Event& e = _out.events[_out.count++];
			e = Event();
			e.track = r.track;
			e.offset = static_cast<uint32_t>(sample);
			e.trig = true;
			e.velocity = r.velocity;
			e.accent = r.accent;
			e.retrig = true;
			const Pattern& p = m_patterns[r.pattern];
			e.locks = p.locks(r.track, r.step);
			e.lockCount = p.step(r.track, r.step).lockCount;
		}
	}
}
