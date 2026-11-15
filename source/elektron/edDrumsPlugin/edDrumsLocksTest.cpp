// The sequencer's Locks without a Processor (edDrumsLocks.h): which parameters take one, what an event holds and
// releases, slides, a step's note, a ratchet; and a lane drawn (Locks::lane) as Locks::play plays the sequencer's
// events

#include "edDrumsLocks.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

using namespace elektron::drums;

namespace
{
	int g_failures = 0;

	void require(const bool _ok, const std::string& _what)
	{
		std::printf("%s %s\n", _ok ? "ok  " : "FAIL", _what.c_str());
		if(!_ok)
			++g_failures;
	}

	// Parameters of Track 0, then one of Track 1, then one no Lock plays (REPEAT GAP's, say)
	enum Parameter { M1, M2, Note, Level, OtherLevel, Gap, Count };

	std::vector<LockTarget> targets()
	{
		std::vector<LockTarget> t(Count);
		t[M1] = {0, LockKind::Machine, 0};
		t[M2] = {0, LockKind::Machine, 1};
		t[Note] = {0, LockKind::Note};
		t[Level] = {0, LockKind::Level};
		t[OtherLevel] = {1, LockKind::Level};
		return t;
	}

	// The instrument a Sink plays: each parameter's value as the engine has it, a Hit's Locks apart
	struct Instrument final : Locks::Sink
	{
		const Locks& locks;
		std::vector<float> hostValue = std::vector<float>(Count, 64.0f);
		std::vector<float> played = std::vector<float>(Count, 64.0f);
		std::vector<int> hit = std::vector<int>(Count, -1);

		explicit Instrument(const Locks& _locks) : locks(_locks) {}

		float host(const int _parameter) const override { return hostValue[static_cast<size_t>(_parameter)]; }
		float snap(int, const float _value) const override
		{
			return std::round(std::min(127.0f, std::max(0.0f, _value)));
		}
		void apply(const int _parameter) override
		{
			const bool held = locks.held(_parameter);
			played[static_cast<size_t>(_parameter)] = held ? locks.value(_parameter) : host(_parameter);
		}
		void hitLock(int, const int _machine, const uint8_t _value) override
		{
			hit[static_cast<size_t>(_machine == 0 ? M1 : M2)] = _value;
		}
	};

	Event event(const int _track, const bool _trig, const std::vector<Lock>& _locks)
	{
		Event e;
		e.track = static_cast<uint8_t>(_track);
		e.trig = _trig;
		e.velocity = _trig ? 100 : 0;
		e.locks = _locks.data();
		e.lockCount = static_cast<uint16_t>(_locks.size());
		return e;
	}

	void lockable()
	{
		Locks locks;
		locks.setTargets(targets());
		require(locks.lockable(M1, 0) && locks.lockable(Level, 0) && locks.lockable(OtherLevel, 1),
			"a Track's targets take its Locks");
		require(!locks.lockable(OtherLevel, 0) && !locks.lockable(M1, 1), "another Track's do not");
		require(!locks.lockable(Gap, 0) && !locks.lockable(-1, 0) && !locks.lockable(Count, 0),
			"a parameter no Lock plays, or none, takes none");
	}

	void holdAndRelease()
	{
		Locks locks;
		locks.setTargets(targets());
		Instrument in(locks);
		const std::vector<Lock> level{{Level, 100.4f}};
		locks.play(event(0, false, level), 0, in);
		require(locks.held(Level) && in.played[Level] == 100,
			"a trigless Lock holds its value, on the parameter's steps");
		locks.play(event(1, true, {}), 10, in);
		require(locks.held(Level), "another Track's event leaves it");
		locks.play(event(0, true, {}), 20, in);
		require(!locks.held(Level) && in.played[Level] == 64, "the Track's next event gives the host's value back");
		locks.play(event(0, false, {{Gap, 3}, {OtherLevel, 3}}), 30, in);
		require(!locks.held(Gap) && !locks.held(OtherLevel),
			"a Lock of no target, or of another Track's, plays nothing");
		locks.play(event(0, false, level), 40, in);
		locks.forget(0);
		require(!locks.held(Level) && in.played[Level] == 100, "forget drops the Lock and tells the engine nothing");
	}

	void hitLocks()
	{
		Locks locks;
		locks.setTargets(targets());
		Instrument in(locks);
		locks.play(event(0, true, {{M1, 30}}), 0, in);
		require(in.hit[M1] == 30 && !locks.held(M1), "a trig's Lock of a machine parameter is its Hit's, not held");
		auto e = event(0, true, {});
		e.note = 72;
		locks.play(e, 10, in);
		require(locks.held(Note) && in.played[Note] == 72, "a trig's note is a Lock of the Track's Note");
	}

	void slides()
	{
		Locks locks;
		locks.setTargets(targets());
		Instrument in(locks);
		const std::vector<Lock> from{{Level, 0}, {M1, 20}};
		const std::vector<Lock> to{{Level, 100}};
		auto e = event(0, true, from);
		e.slide = true;
		e.slideSamples = 10 * Locks::SlideStep;
		e.nextLocks = to.data();
		e.nextLockCount = 1;
		locks.play(e, 1000, in);
		require(in.played[Level] == 0 && in.hit[M1] == 20 && locks.held(M1),
			"a slide starts at its Locks, a machine's held");
		require(locks.nextSlideStep() == 1000 + Locks::SlideStep, "its first step a SlideStep after the Hit");
		std::vector<float> level, m1;
		while(locks.nextSlideStep() != Locks::NoSlide)
		{
			locks.stepSlides(locks.nextSlideStep(), in);
			level.push_back(in.played[Level]);
			m1.push_back(in.played[M1]);
		}
		require(level.size() == 10 && std::abs(level[4] - 50) < 1e-4f && level.back() == 100,
			"Level glides to the next trig's Lock in 10 steps");
		require(std::abs(m1[4] - (20 + 44 * 0.5f)) < 1e-4f && m1.back() == 64,
			"M1, which the next trig does not lock, glides to the host's value");
		// A ratchet's later Hit keeps the slide going, and locks M1 where it stands
		locks.play(e, 5000, in);
		locks.stepSlides(locks.nextSlideStep(), in);
		locks.stepSlides(locks.nextSlideStep(), in);
		auto retrig = event(0, true, from);
		retrig.retrig = true;
		locks.play(retrig, 5000 + 2 * Locks::SlideStep, in);
		const int stands = static_cast<int>(std::lrint(20 + 44 * 0.2f));
		require(locks.nextSlideStep() == 5000 + 3 * Locks::SlideStep && in.hit[M1] == stands,
			"a ratchet keeps its step's slide, its Hit locked where the slide stands");
	}

	// A lane drawn as the sequencer's events play: Level on Track 0, its steps' Locks and slides, two laps at 120 BPM,
	// each step read at its middle in the second
	void laneAsPlayed()
	{
		constexpr double Rate = 44100, Bpm = 120;
		constexpr int Block = 128;
		const double stepSamples = Rate * 60 / Bpm / 4;
		const auto lap = [&](const std::function<void(Pattern&)>& _set, const std::string& _what)
		{
			Sequencer seq;
			auto& p = seq.pattern(seq.currentPattern());
			_set(p);
			Locks locks;
			locks.setTargets(targets());
			Instrument in(locks);
			const auto drawn = locks.lane(p, 0, Level, 64.0f / 127, [](const float _v) { return _v / 127; });
			Events events;
			std::vector<float> played(16, -1);
			int next = 16;
			for(int64_t at = 0; next < 32; at += Block)
			{
				Transport t;
				t.playing = true;
				t.quarters = static_cast<double>(at) * Bpm / (60 * Rate);
				t.bpm = Bpm;
				t.sampleRate = Rate;
				t.length = Block;
				seq.process(t, events);
				int e = 0;
				for(int s = 0; s <= Block; ++s)
				{
					const int64_t now = at + s;
					while(locks.nextSlideStep() <= now)
						locks.stepSlides(locks.nextSlideStep(), in);
					for(; e < events.count && static_cast<int>(events.events[e].offset) == s; ++e)
						locks.play(events.events[e], now, in);
					while(next < 32 && static_cast<double>(now) >= (next + 0.5) * stepSamples)
						played[static_cast<size_t>(next++ - 16)] = in.played[Level];
					if(s == Block)
						break;
				}
			}
			std::string wrong;
			for(int s = 0; s < 16; ++s)
			{
				const auto& d = drawn[static_cast<size_t>(s)];
				const float mid = (d.from + d.to) / 2 * 127;
				if(std::abs(mid - played[static_cast<size_t>(s)]) > 1.5f)
					wrong += " " + std::to_string(s + 1) + ": drawn " + std::to_string(mid) + ", played "
						+ std::to_string(played[static_cast<size_t>(s)]);
			}
			require(wrong.empty(), "a lane drawn as played, " + _what + wrong);
		};
		lap([](Pattern& _p)
		{
			_p.setTrig(0, 4, true);
			_p.setLock(0, 8, Level, 20);
		}, "a trig, then a trigless Lock");
		lap([](Pattern& _p)
		{
			_p.setTrig(0, 0, true);
			_p.setLock(0, 0, Level, 100);
			_p.step(0, 0).slide = true;
			_p.setTrig(0, 8, true);
			_p.setLock(0, 8, Level, 0);
		}, "a slide to the next trig's Lock");
		lap([](Pattern& _p)
		{
			_p.setTrig(0, 2, true);
			_p.setLock(0, 2, Level, 110);
			_p.step(0, 2).slide = true;
			_p.setTrig(0, 10, true);
		}, "a slide to the host's value");
	}
}

int main()
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	lockable();
	holdAndRelease();
	hitLocks();
	slides();
	laneAsPlayed();
	std::printf("%s\n", g_failures ? "FAILED" : "all passed");
	return g_failures ? 1 : 0;
}
