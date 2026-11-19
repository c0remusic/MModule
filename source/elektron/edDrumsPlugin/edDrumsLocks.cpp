#include "edDrumsLocks.h"

#include <algorithm>
#include <cmath>

namespace elektron::drums
{
	const Lock* lockAt(const Pattern& _p, const int _track, const int _step, const int _parameter)
	{
		const auto* l = _p.locks(_track, _step);
		for(int i = 0; l && i < _p.step(_track, _step).lockCount; ++i)
			if(l[i].parameter == _parameter)
				return &l[i];
		return nullptr;
	}

	void Locks::setTargets(std::vector<LockTarget> _targets)
	{
		m_targets = std::move(_targets);
		m_held.assign(m_targets.size(), 0);
		m_value.assign(m_targets.size(), 0.0f);
		m_note.fill(-1);
		std::array<size_t, Tracks> count{};
		for(size_t i = 0; i < m_targets.size(); ++i)
		{
			const auto& t = m_targets[i];
			if(t.track < 0 || t.track >= Tracks || t.kind == LockKind::None)
				continue;
			++count[static_cast<size_t>(t.track)];
			if(t.kind == LockKind::Note)
				m_note[static_cast<size_t>(t.track)] = static_cast<int>(i);
		}
		// Each of a Track's targets once at most: the lists never grow past this on the audio thread
		for(size_t t = 0; t < Tracks; ++t)
		{
			m_heldBy[t].clear();
			m_heldBy[t].reserve(count[t]);
			m_slides[t] = {};
			m_slides[t].glides.reserve(count[t]);
		}
	}

	bool Locks::lockable(const int _parameter, const int _track) const
	{
		if(_parameter < 0 || _parameter >= size())
			return false;
		const auto& t = target(_parameter);
		return t.track == _track && t.kind != LockKind::None;
	}

	void Locks::hold(const int _track, const int _parameter)
	{
		auto& held = m_held[static_cast<size_t>(_parameter)];
		if(held)
			return;
		held = 1;
		m_heldBy[static_cast<size_t>(_track)].push_back(_parameter);
	}

	void Locks::release(const int _track, Sink& _sink)
	{
		endSlide(_track);
		auto& list = m_heldBy[static_cast<size_t>(_track)];
		for(const int i : list)
			m_held[static_cast<size_t>(i)] = 0;
		for(const int i : list)
			_sink.apply(i);
		list.clear();
	}

	void Locks::forget(const int _track)
	{
		auto& list = m_heldBy[static_cast<size_t>(_track)];
		for(const int i : list)
			m_held[static_cast<size_t>(i)] = 0;
		list.clear();
		endSlide(_track);
	}

	void Locks::forgetAll()
	{
		for(int t = 0; t < Tracks; ++t)
			forget(t);
	}

	void Locks::endSlide(const int _track)
	{
		auto& slide = m_slides[static_cast<size_t>(_track)];
		slide.next = NoSlide;
		slide.glides.clear();
	}

	int64_t Locks::nextSlideStep() const
	{
		int64_t step = NoSlide;
		for(const auto& slide : m_slides)
			step = std::min(step, slide.next);
		return step;
	}

	void Locks::stepSlides(const int64_t _at, Sink& _sink)
	{
		for(int t = 0; t < Tracks; ++t)
			if(m_slides[static_cast<size_t>(t)].next == _at)
				stepSlide(t, _sink);
	}

	// A slide's step: each value where the line stands at this sample, the last on the next trig's value
	void Locks::stepSlide(const int _track, Sink& _sink)
	{
		auto& slide = m_slides[static_cast<size_t>(_track)];
		const int64_t elapsed = std::min(slide.next - slide.start, slide.length);
		const float f = static_cast<float>(elapsed) / static_cast<float>(slide.length);
		for(const auto& g : slide.glides)
		{
			m_value[static_cast<size_t>(g.parameter)] = g.from + (g.to - g.from) * f;
			_sink.apply(g.parameter);
		}
		slide.next = elapsed >= slide.length ? NoSlide : slide.next + SlideStep;
	}

	void Locks::play(const Event& _event, const int64_t _at, Sink& _sink)
	{
		const int t = _event.track;
		if(_event.off)
			return;
		// A ratchet's later Hit keeps what its step locked, and locks Machine 1-8 again, which Engine::lock holds a
		// Hit; it leaves its step's slide going
		if(!_event.retrig)
			release(t, _sink);
		const bool slides = _event.slide && !_event.retrig && _event.slideSamples > 0;
		auto& slide = m_slides[static_cast<size_t>(t)];
		for(int k = 0; k < _event.lockCount; ++k)
		{
			const auto& lock = _event.locks[k];
			// The sample, for each of the step's Hits, a ratchet's later ones too; nothing without a Hit
			if(isSampleLock(lock.parameter))
			{
				if(_event.trig)
					_sink.hitSample(t, lock.sample, lock.parameter == SampleLayerLock);
				continue;
			}
			if(lock.parameter >= m_targets.size())
				continue;
			const auto& target = m_targets[lock.parameter];
			if(target.track != t || target.kind == LockKind::None)
				continue;
			if(_event.retrig && target.kind != LockKind::Machine)
				continue;
			const float value = _sink.snap(lock.parameter, lock.value);
			const bool hitLock = _event.trig && target.kind == LockKind::Machine;
			// A ratchet's later Hit locks where its step's slide stands, not back at the slide's start
			const float held = _event.retrig && m_held[lock.parameter] ? m_value[lock.parameter] : value;
			if(hitLock)
				_sink.hitLock(t, target.a, static_cast<uint8_t>(std::lrint(held)));
			if(hitLock && !slides)
				continue;
			// A Machine 1-8 the Hit locks is held while it slides, and takes the host's value again where the slide
			// ends
			m_value[lock.parameter] = value;
			hold(t, lock.parameter);
			if(!hitLock)
				_sink.apply(lock.parameter);
			if(!slides)
				continue;
			float to = _sink.host(lock.parameter);
			for(int n = 0; n < _event.nextLockCount; ++n)
				if(_event.nextLocks[n].parameter == lock.parameter)
					to = _sink.snap(lock.parameter, _event.nextLocks[n].value);
			// A step may lock a parameter twice: one glide each, so that the list stays within what it reserved
			const auto same = std::find_if(slide.glides.begin(), slide.glides.end(),
				[&](const Glide& _g) { return _g.parameter == lock.parameter; });
			if(same != slide.glides.end())
				*same = {lock.parameter, value, to};
			else
				slide.glides.push_back({lock.parameter, value, to});
		}
		if(slides && !slide.glides.empty())
		{
			slide.start = _at;
			slide.length = _event.slideSamples;
			slide.next = _at + SlideStep;
		}
		// The step's note is a Lock of the Track's Note, over a Lock of Note the step may hold
		const int note = m_note[static_cast<size_t>(t)];
		if(_event.trig && _event.note != NoNote && note >= 0)
		{
			m_value[static_cast<size_t>(note)] = _event.note;
			hold(t, note);
			_sink.apply(note);
		}
	}

	// As the sequencer plays the Track's steps (edDrumsSequencer.cpp) and play() applies them: a step with a trig or a
	// Lock is an event, and an event releases what the Track's last one locked and locks its own; so a Lock holds to
	// the next event, then the base comes back, across the loop's end too. A trig's Lock of Machine 1-24 without SLIDE
	// is the Hit's (Engine::lock), which only the Track's next trig releases, or a trigless Lock of it. A trig with
	// SLIDE glides what it locks towards the next trig's Lock, or the base, until the next event. On a trig, a step's
	// note plays over a Lock of Note. A step whose condition is not Always may not play (Sequencer::emit): it neither
	// releases nor locks here, and its own Lock is drawn as one that may play
	std::vector<Locks::LaneStep> Locks::lane(const Pattern& _p, const int _track, const int _parameter,
		const float _base, const std::function<float(float)>& _normal, int* _locks) const
	{
		std::vector<LaneStep> steps(static_cast<size_t>(SeqSteps), {_base, _base, false});
		const int length = std::min(SeqSteps, _p.trackLength[static_cast<size_t>(_track)]);
		if(_locks)
			*_locks = 0;
		const bool note = _parameter == m_note[static_cast<size_t>(_track)];
		const bool machine = _parameter >= 0 && _parameter < size() && target(_parameter).kind == LockKind::Machine;
		// Each step's own Lock, the step's note over it on a trig; whether a Lock in its list slides; whether the step
		// is an event, sure to play; whether its Lock is the Hit's; the value it locks
		std::array<bool, SeqSteps> own{}, events{}, hits{};
		std::array<float, SeqSteps> value;
		value.fill(_base);
		// Past the Track's length, a step's Lock is counted only
		for(int s = 0; s < SeqSteps; ++s)
		{
			const auto* lock = lockAt(_p, _track, s, _parameter);
			if(_locks && lock)
				++*_locks;
			if(s >= length)
				continue;
			const auto i = static_cast<size_t>(s);
			auto& st = steps[i];
			const auto& step = _p.step(_track, s);
			const bool locks = lock != nullptr;
			bool has = locks;
			if(lock)
				value[i] = _normal(lock->value);
			if(note && step.trig && step.note != NoNote)
			{
				value[i] = _normal(static_cast<float>(step.note));
				has = true;
			}
			if(step.condition.kind != ConditionKind::Always)
			{
				st.may = has;
				st.would = value[i];
				continue;
			}
			st.locked = has;
			own[i] = locks;
			events[i] = step.trig || step.lockCount > 0;
			hits[i] = machine && step.trig && locks && !step.slide;
		}
		// Two laps, the second drawn: what the first left held is what every later lap starts with
		float held = _base;
		bool hitHeld = false;
		for(int n = 0; n < 2 * length; ++n)
		{
			const auto i = static_cast<size_t>(n % length);
			if(events[i] && (_p.step(_track, n % length).trig || steps[i].locked || !hitHeld))
			{
				held = steps[i].locked ? value[i] : _base;
				hitHeld = hits[i];
			}
			if(n >= length)
				steps[i].from = steps[i].to = held;
		}
		for(int s = 0; s < length; ++s)
		{
			const auto& step = _p.step(_track, s);
			if(!step.trig || !step.slide || !own[static_cast<size_t>(s)])
				continue;
			for(int d = 1; d <= length; ++d)
			{
				const int next = (s + d) % length;
				if(!_p.step(_track, next).trig)
					continue;
				const float v0 = steps[static_cast<size_t>(s)].from;
				const auto* lock = lockAt(_p, _track, next, _parameter);
				const float vt = lock ? _normal(lock->value) : _base;
				for(int k = 0; k < d && (k == 0 || !events[static_cast<size_t>((s + k) % length)]); ++k)
				{
					auto& st = steps[static_cast<size_t>((s + k) % length)];
					st.from = v0 + (vt - v0) * static_cast<float>(k) / static_cast<float>(d);
					st.to = v0 + (vt - v0) * static_cast<float>(k + 1) / static_cast<float>(d);
				}
				break;
			}
		}
		return steps;
	}
}
