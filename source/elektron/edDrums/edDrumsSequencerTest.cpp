// The sequencer's core (ticket 17): steps at their samples whatever the block size, scale, length, swing, micro-timing,
// ratchets, conditions, locks, pattern change at the pattern's end, jumps of the host's position, a stopped transport.
#include "edDrumsSequencer.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

using namespace elektron::drums;

namespace
{
	int g_failures = 0;

	void check(const bool _ok, const std::string& _what)
	{
		std::printf("  %s %s\n", _ok ? "ok  " : "FAIL", _what.c_str());
		if(!_ok)
			++g_failures;
	}

	struct Rec
	{
		int64_t sample = 0;
		int track = 0;
		int velocity = 0;
		bool accent = false;
		bool trig = false;
		std::vector<Lock> locks;

		bool operator==(const Rec& _o) const
		{
			if(sample != _o.sample || track != _o.track || velocity != _o.velocity || accent != _o.accent || trig != _o.trig
				|| locks.size() != _o.locks.size())
				return false;
			for(size_t i = 0; i < locks.size(); ++i)
				if(locks[i].parameter != _o.locks[i].parameter || locks[i].value != _o.locks[i].value)
					return false;
			return true;
		}
	};
	using Recs = std::vector<Rec>;

	Events g_events;

	void append(Recs& _r, const int64_t _base)
	{
		for(int i = 0; i < g_events.count; ++i)
		{
			const Event& e = g_events.events[i];
			Rec r;
			r.sample = _base + e.offset;
			r.track = e.track;
			r.velocity = e.velocity;
			r.accent = e.accent;
			r.trig = e.trig;
			for(int k = 0; k < e.lockCount; ++k)
				r.locks.push_back(e.locks[k]);
			_r.push_back(r);
		}
	}

	// Plays _total samples from _startQuarters in blocks of _block, the way a host counts its position. _before runs ahead
	// of each block with the sample index
	Recs render(Sequencer& _s, const double _bpm, const double _sr, const int _block, const int64_t _total,
		const double _startQuarters = 0, const std::function<void(int64_t)>& _before = {})
	{
		Recs out;
		const double quartersPerSample = _bpm / (60 * _sr);
		for(int64_t at = 0; at < _total; at += _block)
		{
			if(_before)
				_before(at);
			Transport t;
			t.playing = true;
			t.quarters = _startQuarters + static_cast<double>(at) * quartersPerSample;
			t.bpm = _bpm;
			t.sampleRate = _sr;
			t.length = static_cast<int>(std::min<int64_t>(_block, _total - at));
			_s.process(t, g_events);
			append(out, at);
		}
		return out;
	}

	int64_t expected(const long double _quarters, const double _bpm, const double _sr)
	{
		const long double spq = static_cast<long double>(_sr) * 60 / static_cast<long double>(_bpm);
		return static_cast<int64_t>(std::ceil(_quarters * spq - 1e-3L));
	}

	Recs ofTrack(const Recs& _r, const int _track)
	{
		Recs o;
		for(const Rec& r : _r)
			if(r.track == _track)
				o.push_back(r);
		return o;
	}

	int64_t samplesOf(const double _quarters, const double _bpm, const double _sr)
	{
		return static_cast<int64_t>(std::ceil(_quarters * _sr * 60 / _bpm));
	}

	bool sameWhateverBlock(const std::function<Recs(int)>& _run, const Recs& _reference)
	{
		for(const int block : {37, 64, 512, 1000, 1024, 4096})
			if(_run(block) != _reference)
				return false;
		return true;
	}

	// ---- tests

	void testGrid()
	{
		std::printf("grid: steps at their sample\n");
		for(const double bpm : {120.0, 133.0})
		{
			const double sr = 44100;
			auto run = [&](const int _block)
			{
				Sequencer s;
				Pattern& p = s.pattern(0);
				for(int i = 0; i < 16; i += 4)
					p.setTrig(0, i, true, 100 + i);
				for(int i = 0; i < 16; ++i)
					p.setTrig(1, i, true, 90);
				return render(s, bpm, sr, _block, samplesOf(32, bpm, sr));
			};
			const Recs reference = run(512);
			const Recs t0 = ofTrack(reference, 0);
			bool exact = t0.size() == 4 * 8;
			for(size_t k = 0; k < t0.size() && exact; ++k)
				exact = t0[k].sample == expected(static_cast<long double>(k) * 1.0L, bpm, sr);
			const Recs t1 = ofTrack(reference, 1);
			bool exact1 = t1.size() == 16 * 8;
			for(size_t k = 0; k < t1.size() && exact1; ++k)
				exact1 = t1[k].sample == expected(static_cast<long double>(k) * 0.25L, bpm, sr);
			const std::string tag = std::to_string(static_cast<int>(bpm)) + " BPM";
			check(exact && exact1, tag + ": " + std::to_string(t0.size()) + " + " + std::to_string(t1.size())
				+ " Hits on the expected sample (step 4 at "
				+ std::to_string(t1.size() > 4 ? t1[4].sample : -1) + ", step 1 at " + std::to_string(t1[1].sample)
				+ ")");
			check(sameWhateverBlock(run, reference), tag + ": the same events at blocks of 37, 64, 512, 1000, 1024, 4096");
			check(t0.size() > 2 && t0[2].velocity == 108, tag + ": velocity of step 8 is 108");
		}
	}

	void testScaleLength()
	{
		std::printf("scale and length per Track\n");
		const double bpm = 120, sr = 48000;
		Sequencer s;
		Pattern& p = s.pattern(0);
		p.trackLength[2] = 3;
		p.trackScale[2] = StepScale::Two;
		p.setTrig(2, 0, true);
		p.trackScale[3] = StepScale::Half;
		p.setTrig(3, 0, true);
		p.setTrig(3, 1, true);
		p.trackLength[4] = 5;
		p.trackScale[4] = StepScale::ThreeQuarters;
		p.setTrig(4, 4, true);
		const Recs r = render(s, bpm, sr, 512, samplesOf(8, bpm, sr));
		const Recs t2 = ofTrack(r, 2), t3 = ofTrack(r, 3), t4 = ofTrack(r, 4);
		// Track 3: steps of 0.5, 8 of them in the pattern's 4 quarters: Hits at 0 and 0.5 each pass
		bool ok2 = t2.size() == 11 * 2;
		for(size_t k = 0; k < t2.size() && ok2; ++k)
			ok2 = t2[k].sample == expected((k / 11) * 4.0L + (k % 11) * 0.375L, bpm, sr);
		bool ok3 = t3.size() == 4;
		for(size_t k = 0; k < t3.size() && ok3; ++k)
			ok3 = t3[k].sample == expected((k / 2) * 4.0L + (k % 2) * 0.5L, bpm, sr);
		// Track 4: steps of 1/3 quarter, loops of 5, its trig at step 4: n = 4, 9, 14... (n < 12 steps in 4 quarters)
		bool ok4 = t4.size() == 2 * 2;
		const long double third = 1.0L / 3.0L;
		for(size_t k = 0; k < t4.size() && ok4; ++k)
			ok4 = t4[k].sample == expected((k / 2) * 4.0L + (k % 2 ? 9 : 4) * 0.25L / 0.75L, bpm, sr);
		(void)third;
		check(ok2, "length 3 at 2x: " + std::to_string(t2.size()) + " Hits, 3 steps of 1/8 quarter apart, restarting each pattern");
		check(ok3, "1/2x: Hits at 0 and half a quarter, " + std::to_string(t3.size()) + " in two passes");
		check(ok4, "length 5 at 3/4x: step 4 loops, " + std::to_string(t4.size()) + " Hits");
	}

	void testSwingMicroTiming()
	{
		std::printf("swing and micro-timing\n");
		const double bpm = 120, sr = 44100;
		{
			Sequencer s;
			Pattern& p = s.pattern(0);
			p.swing = 75;
			for(int i = 0; i < 16; ++i)
				p.setTrig(0, i, true);
			const Recs r = render(s, bpm, sr, 512, samplesOf(4, bpm, sr));
			bool ok = r.size() == 16;
			for(size_t k = 0; k < r.size() && ok; ++k)
				ok = r[k].sample == expected(k * 0.25L + ((k & 1) ? 0.125L : 0), bpm, sr);
			check(ok, "swing 75 %: odd steps half a step late (step 1 at " + std::to_string(r[1].sample) + ", straight "
				+ std::to_string(expected(0.25L, bpm, sr)) + ")");
			Sequencer straight;
			straight.pattern(0).swing = 50;
			for(int i = 0; i < 16; ++i)
				straight.pattern(0).setTrig(0, i, true);
			const Recs r50 = render(straight, bpm, sr, 512, samplesOf(4, bpm, sr));
			check(r50[1].sample == expected(0.25L, bpm, sr) && r50[1].sample < r[1].sample, "swing 50 % is straight");
		}
		{
			Sequencer s;
			Pattern& p = s.pattern(0);
			p.setTrig(0, 0, true);
			p.step(0, 0).timing = -12;			// before the pattern's start: it sounds at the start
			p.setTrig(0, 4, true);
			p.step(0, 4).timing = -12;			// half a step early
			p.setTrig(0, 8, true);
			p.step(0, 8).timing = 23;
			p.setTrig(0, 12, true);
			p.step(0, 12).timing = -23;
			auto run = [&](const int _b) { return render(s, bpm, sr, _b, samplesOf(8, bpm, sr)); };
			const Recs r = run(512);
			bool ok = r.size() == 8
				&& r[0].sample == 0
				&& r[1].sample == expected(0.875L, bpm, sr)
				&& r[2].sample == expected(2.0L + 0.25L * 23 / 24, bpm, sr)
				&& r[3].sample == expected(3.0L - 0.25L * 23 / 24, bpm, sr);
			check(ok, "micro-timing: -12 on step 4 " + std::to_string(r[1].sample) + " (expected "
				+ std::to_string(expected(0.875L, bpm, sr)) + "), +23/24 and -23/24, step 0 clamped to 0");
			check(sameWhateverBlock(run, r), "micro-timing identical at every block size");
		}
	}

	void testRatchets()
	{
		std::printf("ratchets\n");
		const double bpm = 120, sr = 44100;
		Sequencer s;
		Pattern& p = s.pattern(0);
		p.setTrig(0, 0, true, 100);
		p.step(0, 0).ratchets = 4;
		p.step(0, 0).ratchetVelocity = -10;
		p.setTrig(0, 8, true, 100);
		p.step(0, 8).ratchets = 3;
		p.step(0, 8).ratchetRate = 6;		// 6 to the step, 3 of them
		auto run = [&](const int _b) { return render(s, bpm, sr, _b, samplesOf(4, bpm, sr)); };
		const Recs r = run(512);
		bool ok = r.size() == 7;
		for(int k = 0; k < 4 && ok; ++k)
			ok = r[k].sample == expected(k * 0.0625L, bpm, sr) && r[k].velocity == 100 - 10 * k;
		for(int k = 0; k < 3 && ok; ++k)
			ok = r[4 + k].sample == expected(2.0L + k * 0.25L / 6, bpm, sr) && r[4 + k].velocity == 100;
		check(ok, "4 Hits a quarter step apart, velocities 100 90 80 70; 3 of 6 to the step; "
			+ std::to_string(r.size()) + " events");
		check(sameWhateverBlock(run, r), "ratchets identical at every block size, across block edges");

		// The step's locks go with each of its Hits
		p.setLock(0, 0, 7, 0.5f);
		const Recs locked = run(512);
		bool lockedOk = locked.size() == 7;
		for(int k = 0; k < 4 && lockedOk; ++k)
			lockedOk = locked[k].locks.size() == 1 && locked[k].locks[0].parameter == 7 && locked[k].locks[0].value == 0.5f;
		check(lockedOk, "a ratchet's 4 Hits each carry the step's lock");
		check(sameWhateverBlock(run, locked), "a ratchet's locks identical at every block size");
	}

	void testDrift()
	{
		std::printf("a small forward drift of the host's position\n");
		const double sr = 44100;
		Sequencer s;
		s.pattern(0).masterLength = 12;
		s.pattern(0).setTrig(0, 0, true, 50);
		s.pattern(1).setTrig(0, 0, true, 77);
		s.pattern(1).setTrig(0, 8, true, 77);
		// Pattern 0 (3 quarters), then pattern 1 from 3: its Hits at 3, 5, 7, 9, 11. The tempo goes from 120 to 130 BPM
		// halfway through the block at 3.48 quarters, which the next block's position shows (a drift of ~20 samples)
		constexpr int Block = 512;
		double quarters = 0, bpm = 120;
		std::vector<double> hits;
		for(int b = 0; b < 500; ++b)
		{
			if(b == 1)
				s.requestPattern(1);
			Transport t;
			t.playing = true;
			t.quarters = quarters;
			t.bpm = bpm;
			t.sampleRate = sr;
			t.length = Block;
			s.process(t, g_events);
			const double spq = sr * 60 / bpm;
			for(int i = 0; i < g_events.count; ++i)
				if(g_events.events[i].velocity == 77)
					hits.push_back(quarters + g_events.events[i].offset / spq);
			if(b == 150)
			{
				quarters += Block / 2 * 120 / (60 * sr) + Block / 2 * 130 / (60 * sr);
				bpm = 130;
			}
			else
			{
				quarters += Block * bpm / (60 * sr);
			}
		}
		bool ok = hits.size() >= 5;
		for(size_t k = 0; k < hits.size() && ok; ++k)
			ok = std::abs(hits[k] - (3.0 + 2.0 * static_cast<double>(k))) * sr * 60 / 130 < 1.5;
		std::string at;
		for(const double h : hits)
			at += " " + std::to_string(h);
		check(ok, "pattern 1 keeps its phase through the tempo change: Hits at" + at);
	}

	void testConditions()
	{
		std::printf("conditions\n");
		const double bpm = 120, sr = 44100;
		const int passes = 400;
		const int64_t total = samplesOf(4.0 * passes, bpm, sr);
		const int64_t passSamples = samplesOf(4, bpm, sr);
		auto passOf = [&](const Rec& _r) { return static_cast<int>((_r.sample + 1) / passSamples); };

		{
			Sequencer s;
			Pattern& p = s.pattern(0);
			p.setTrig(0, 0, true);
			p.step(0, 0).condition = {ConditionKind::Ratio, 1, 2};
			p.setTrig(1, 0, true);
			p.step(1, 0).condition = {ConditionKind::Ratio, 2, 3};
			p.setTrig(2, 0, true);
			p.step(2, 0).condition = {ConditionKind::Ratio, 3, 8};
			const Recs r = render(s, bpm, sr, 512, samplesOf(4.0 * 48, bpm, sr));
			bool ok = true;
			int n0 = 0, n1 = 0, n2 = 0;
			for(const Rec& e : r)
			{
				const int pass = passOf(e);
				if(e.track == 0) { ok = ok && pass % 2 == 0; ++n0; }
				if(e.track == 1) { ok = ok && pass % 3 == 1; ++n1; }
				if(e.track == 2) { ok = ok && pass % 8 == 2; ++n2; }
			}
			check(ok && n0 == 24 && n1 == 16 && n2 == 6,
				"A:B exact over 48 passes: 1:2 -> " + std::to_string(n0) + ", 2:3 -> " + std::to_string(n1) + ", 3:8 -> "
				+ std::to_string(n2));
		}
		{
			auto run = [&](const uint32_t _seed, const int _block)
			{
				Sequencer s;
				s.setSeed(_seed);
				Pattern& p = s.pattern(0);
				for(int i = 0; i < 16; ++i)
				{
					p.setTrig(0, i, true);
					p.step(0, i).condition = {ConditionKind::Probability, 25, 0};
					p.setTrig(1, i, true);
					p.step(1, i).condition = {ConditionKind::Probability, 90, 0};
				}
				return render(s, bpm, sr, _block, total);
			};
			const Recs a = run(7, 512);
			const int n0 = static_cast<int>(ofTrack(a, 0).size()), n1 = static_cast<int>(ofTrack(a, 1).size());
			check(std::abs(n0 - 1600) < 150 && std::abs(n1 - 5760) < 150,
				"probability over " + std::to_string(passes) + " passes of 16 steps: 25 % -> " + std::to_string(n0)
				+ " of 6400 (1600 +-150), 90 % -> " + std::to_string(n1) + " (5760)");
			check(run(7, 37) == a && run(7, 4096) == a, "same seed, same Hits whatever the block size");
			check(run(8, 512) != a, "another seed, other Hits");
		}
		{
			// Pre, NotPre and Nei read earlier decisions
			Sequencer s;
			Pattern& p = s.pattern(0);
			p.setTrig(0, 0, true);
			p.step(0, 0).condition = {ConditionKind::Probability, 50, 0};
			p.setTrig(0, 1, true);
			p.step(0, 1).condition = {ConditionKind::Pre, 0, 0};
			p.setTrig(0, 2, true);
			p.step(0, 2).condition = {ConditionKind::NotPre, 0, 0};
			p.setTrig(1, 0, true);
			p.step(1, 0).condition = {ConditionKind::Nei, 0, 0};
			p.setTrig(2, 0, true);
			p.step(2, 0).condition = {ConditionKind::NotNei, 0, 0};
			const Recs r = render(s, bpm, sr, 37, total);
			std::vector<std::array<bool, 5>> seen(passes, std::array<bool, 5>{});
			for(const Rec& e : r)
			{
				const int pass = passOf(e);
				const int64_t rel = e.sample - static_cast<int64_t>(pass) * passSamples;
				if(pass >= passes)
					continue;
				if(e.track == 0)
					seen[pass][rel < 2000 ? 0 : (rel < 8000 ? 1 : 2)] = true;
				else
					seen[pass][e.track + 2] = true;
			}
			bool ok = true;
			int fired = 0;
			for(const auto& v : seen)
			{
				ok = ok && v[1] == v[0] && v[2] == !v[1] && v[3] == v[0] && v[4] == !v[0];
				fired += v[0];
			}
			check(ok, "Pre follows its predecessor, NotPre its opposite, Nei/NotNei the Track above ("
				+ std::to_string(fired) + " of " + std::to_string(passes) + " passes fired)");
		}
		{
			Sequencer s;
			Pattern& p = s.pattern(0);
			p.setTrig(0, 0, true);
			p.step(0, 0).condition = {ConditionKind::Fill, 0, 0};
			p.setTrig(1, 0, true);
			p.step(1, 0).condition = {ConditionKind::NotFill, 0, 0};
			p.setTrig(2, 0, true);
			p.step(2, 0).condition = {ConditionKind::First, 0, 0};
			p.setTrig(3, 0, true);
			p.step(3, 0).condition = {ConditionKind::NotFirst, 0, 0};
			s.setFill(false);
			const Recs off = render(s, bpm, sr, 512, samplesOf(12, bpm, sr));
			Sequencer s2;
			s2.pattern(0) = s.pattern(0);
			s2.setFill(true);
			const Recs on = render(s2, bpm, sr, 512, samplesOf(12, bpm, sr));
			check(ofTrack(off, 0).empty() && ofTrack(off, 1).size() == 3 && ofTrack(on, 0).size() == 3 && ofTrack(on, 1).empty(),
				"Fill plays only with fill on, NotFill only without (3 passes)");
			check(ofTrack(off, 2).size() == 1 && ofTrack(off, 3).size() == 2, "First on the first pass only, NotFirst on the others");
		}
	}

	void testLocks()
	{
		std::printf("locks\n");
		const double bpm = 120, sr = 44100;
		Sequencer s;
		Pattern& p = s.pattern(0);
		p.setTrig(3, 2, true, 80);
		p.setLock(3, 2, 5, 0.25f);
		p.setLock(3, 6, 1, 0.5f);				// no trig: a lock without note
		p.setLock(3, 2, 9, 0.75f);				// the step's locks are not last in the pool any more: they move
		p.setLock(3, 6, 2, 0.625f);
		p.setLock(3, 2, 5, 0.125f);				// updated in place
		const Recs r = render(s, bpm, sr, 512, samplesOf(4, bpm, sr));
		bool ok = r.size() == 2 && r[0].trig && r[0].velocity == 80 && r[0].locks.size() == 2
			&& r[0].locks[0].parameter == 5 && r[0].locks[0].value == 0.125f
			&& r[0].locks[1].parameter == 9 && r[0].locks[1].value == 0.75f
			&& !r[1].trig && r[1].velocity == 0 && r[1].locks.size() == 2 && r[1].locks[1].value == 0.625f
			&& r[1].sample == expected(1.5L, bpm, sr);
		check(ok, "a Hit carries its locks (5 -> 0.125, 9 -> 0.75); a step with locks and no trig is a lock-only event at 1.5 quarters");

		for(uint16_t k = 0; k < 300; ++k)
			p.setLock(0, 0, k, static_cast<float>(k));
		p.setTrig(0, 0, true);
		const Recs big = render(s, bpm, sr, 512, samplesOf(1, bpm, sr));
		bool ok300 = !ofTrack(big, 0).empty() && ofTrack(big, 0)[0].locks.size() == 300;
		for(size_t k = 0; k < 300 && ok300; ++k)
			ok300 = ofTrack(big, 0)[0].locks[k].value == static_cast<float>(k);
		check(ok300, "300 locks on one step, no cap");

		check(p.removeLock(3, 2, 5) && p.locks(3, 2)[0].parameter == 9 && p.step(3, 2).lockCount == 1, "a lock removed, the others stay");
		p.setTrig(3, 2, false);
		const Recs gone = render(s, bpm, sr, 512, samplesOf(4, bpm, sr));
		bool lockOnlyKept = ofTrack(gone, 3).size() == 1 && ofTrack(gone, 3)[0].locks.size() == 2;
		check(lockOnlyKept, "removing a trig removes its locks, the lock-only step beside it is kept");
	}

	void testPatternChange()
	{
		std::printf("pattern change\n");
		const double bpm = 120, sr = 44100;
		auto run = [&](const int _block)
		{
			Sequencer s;
			s.pattern(0).setTrig(0, 0, true, 100);
			s.pattern(0).setTrig(0, 8, true, 100);
			s.pattern(1).masterLength = 8;
			s.pattern(1).setTrig(0, 0, true, 77);
			const int64_t askAt = samplesOf(1.3, bpm, sr);
			return render(s, bpm, sr, _block, samplesOf(12, bpm, sr), 0,
				[&](const int64_t _at) { if(_at <= askAt && askAt < _at + _block) s.requestPattern(1); });
		};
		const Recs r = run(512);
		// Pattern 0: Hits at 0 and 2; it plays to 4; pattern 1 (2 quarters): 4, 6, 8 ... Hits of velocity 77
		bool ok = r.size() == 6 && r[0].velocity == 100 && r[1].velocity == 100 && r[2].velocity == 77
			&& r[2].sample == expected(4.0L, bpm, sr) && r[3].sample == expected(6.0L, bpm, sr)
			&& r[4].sample == expected(8.0L, bpm, sr) && r[5].sample == expected(10.0L, bpm, sr);
		check(ok, "asked at 1.3 quarters, pattern 1 starts at 4.0 (sample " + std::to_string(r.size() > 2 ? r[2].sample : -1)
			+ "), pattern 0 played to its end and pattern 1 repeats every 2 quarters");
		check(sameWhateverBlock(run, r), "the same whatever the block size, with the end of the pattern inside a block");
	}

	void testJumps()
	{
		std::printf("jumps of the host's position\n");
		const double bpm = 120, sr = 44100;
		auto make = [](Sequencer& _s)
		{
			for(int i = 0; i < 16; ++i)
				_s.pattern(0).setTrig(0, i, true, 10 + i);
		};
		const int64_t block = 4096;
		{
			Sequencer s;
			make(s);
			Transport t;
			t.playing = true;
			t.quarters = 1.25;
			t.bpm = bpm;
			t.sampleRate = sr;
			t.length = block;
			s.process(t, g_events);
			check(g_events.count > 0 && g_events.events[0].offset == 0 && g_events.events[0].velocity == 15,
				"start at 1.25 quarters: step 5 at sample 0 (velocity 15)");
		}
		{
			Sequencer s;
			make(s);
			Transport t;
			t.playing = true;
			t.quarters = 1.3;
			t.bpm = bpm;
			t.sampleRate = sr;
			t.length = 8192;
			s.process(t, g_events);
			const int64_t want = static_cast<int64_t>(std::ceil((1.5 - 1.3) * sr * 60 / bpm - 1e-3));
			check(g_events.count > 0 && g_events.events[0].offset == want && g_events.events[0].velocity == 16,
				"start at 1.3: step 6 at sample " + std::to_string(g_events.events[0].offset) + " (expected "
				+ std::to_string(want) + ")");
		}
		{
			Sequencer s;
			make(s);
			Transport t;
			t.playing = true;
			t.bpm = bpm;
			t.sampleRate = sr;
			t.length = 512;
			t.quarters = 0;
			s.process(t, g_events);
			t.quarters = 512 * bpm / (60 * sr);
			s.process(t, g_events);
			t.quarters = 9.0;			// a jump to the pattern's second pass, step 4
			s.process(t, g_events);
			const bool forward = g_events.count > 0 && g_events.events[0].offset == 0 && g_events.events[0].velocity == 14;
			t.quarters = 9.0 + 512 * bpm / (60 * sr);
			s.process(t, g_events);
			t.quarters = 0;				// the host's loop brace: back to the start
			s.process(t, g_events);
			const bool back = g_events.count > 0 && g_events.events[0].offset == 0 && g_events.events[0].velocity == 10;
			check(forward, "jump 0.02 -> 9.0: step 4 (velocity 14) at sample 0");
			check(back, "loop back to 0: step 0 (velocity 10) at sample 0");
		}
	}

	void testStopped()
	{
		std::printf("stopped transport\n");
		Sequencer s;
		for(int i = 0; i < 16; ++i)
			s.pattern(0).setTrig(0, i, true);
		int events = 0;
		Transport t;
		t.playing = false;
		t.bpm = 120;
		t.sampleRate = 44100;
		t.length = 512;
		for(int b = 0; b < 200; ++b)
		{
			t.quarters = b * 0.0116;
			s.process(t, g_events);
			events += g_events.count;
		}
		check(events == 0, "200 blocks stopped: " + std::to_string(events) + " events");
		t.playing = true;
		t.quarters = 0;
		s.process(t, g_events);
		check(g_events.count == 1 && g_events.events[0].offset == 0, "playing again from 0: step 0 at sample 0");
	}

	void testEditing()
	{
		std::printf("mutes, copy, clear\n");
		const double bpm = 120, sr = 44100;
		Sequencer s;
		Pattern& p = s.pattern(0);
		p.setTrig(0, 0, true);
		p.setTrig(0, 4, true);
		p.setLock(0, 4, 3, 0.5f);
		p.trackLength[0] = 8;
		p.copyTrackFrom(p, 0, 5);
		s.setMute(0, true);
		const Recs muted = render(s, bpm, sr, 512, samplesOf(4, bpm, sr));
		check(ofTrack(muted, 0).empty() && ofTrack(muted, 5).size() == 4 && ofTrack(muted, 5)[1].locks.size() == 1,
			"Track 0 muted, its copy on Track 5 plays with its lock");
		s.setMute(0, false);
		p.clearTrack(0);
		s.copyPattern(0, 7);
		s.clearPattern(0);
		check(s.pattern(0).step(5, 4).trig == false && s.pattern(7).step(5, 4).trig && s.pattern(7).locks(5, 4)[0].value == 0.5f,
			"pattern 0 cleared, pattern 7 keeps the copy");
		s.requestPattern(7);
		Transport stopped;
		s.process(stopped, g_events);		// a stopped transport applies the request at its next start
		const Recs seven = render(s, bpm, sr, 512, samplesOf(4, bpm, sr));
		check(s.currentPattern() == 7 && ofTrack(seven, 5).size() == 4, "pattern 7 played from the start");
	}

	void testCost()
	{
		std::printf("cost\n");
		Sequencer s;
		Pattern& p = s.pattern(0);
		for(int t = 0; t < 16; ++t)
		{
			p.trackScale[t] = StepScale::Two;
			p.trackLength[t] = 64;
			for(int i = 0; i < 64; ++i)
			{
				p.setTrig(t, i, true);
				p.setLock(t, i, 1, 0.5f);
				p.step(t, i).condition = {ConditionKind::Probability, 50, 0};
				p.step(t, i).ratchets = 2;
			}
		}
		const int blocks = 20000;
		const auto t0 = std::chrono::steady_clock::now();
		long events = 0;
		Transport t;
		t.playing = true;
		t.bpm = 120;
		t.sampleRate = 44100;
		t.length = 512;
		for(int b = 0; b < blocks; ++b)
		{
			t.quarters = b * 512 * 120 / (60.0 * 44100);
			s.process(t, g_events);
			events += g_events.count;
		}
		const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / blocks;
		std::printf("  16 full Tracks at 2x with locks, conditions and ratchets: %.2f us a block of 512 (%ld events)\n", us, events);
		check(us < 100, "under 100 us a block");
	}
}

int main()
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	testGrid();
	testScaleLength();
	testSwingMicroTiming();
	testRatchets();
	testConditions();
	testLocks();
	testPatternChange();
	testJumps();
	testDrift();
	testStopped();
	testEditing();
	testCost();
	std::printf(g_failures ? "FAILED: %d\n" : "ALL OK\n", g_failures);
	return g_failures ? 1 : 0;
}
