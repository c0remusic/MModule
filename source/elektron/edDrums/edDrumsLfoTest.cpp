// Two LFOs a Track (ticket 13 of .scratch/elektron-drums-v1), each measure printed:
//
//   - the shapes against syntakt::main::lfoWave, the Syntakt's speed and multiplier against syntakt::main::lfos, the
//     Machinedrum's speed against mdDrums::Lfo::increment, the divisions;
//   - the modes; a phase on the transport a function of the position alone;
//   - the engine on its transport: the same samples whatever the host's blocks, the same phase at the same position, a
//     jump followed, a tempo followed, a stopped transport running on;
//   - targets: of the same Track and of another, every family to every family; under a held square a Hit is that of a Kit at
//     the value moved, on the chain's values, SYN1-8, level and pan; what meets an LFO: a lock, a knob, its end, a family;
//     the bounds of what reaches the tables;
//   - alignTicks: how late a Track's ticks read an LFO, by where its Hit fell;
//   - depth 0 and TRIG: the same samples as without an LFO on every machine, the same Hit wherever its note falls, the
//     latency and its jitter;
//   - Q12: a square LFO at full depth on level, pan and the filter's frequency, the slope at its target whatever moves it, the
//     other targets against themselves held still;
//   - what the reviews found: a voice under a closed filter, moves left in a voice, a Hit under a ramp, locks, ONE and HALF;
//   - nothing allocated.
//
// GEARMULATOR_MD_FIRMWARE_BIN names the flash image; exit 77 without it.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "edDrumsChain.h"
#include "edDrumsEngine.h"
#include "edDrumsTestSupport.h"
#include "mdDrumsAllocationCounter.h"
#include "mdDrumsLfo.h"
#include "stFastMetrics.h"
#include "stMainLfo.h"

namespace
{
	using namespace elektron::drums;
	using namespace elektron::drums::test;
	using elektron::Block;
	using elektron::BlockSize;
	using syntakt::fast::bench::highShare;
	using syntakt::fast::bench::Pi;

	constexpr double Bpm = 120;
	constexpr double QuartersPerSample = Bpm / 60 / SampleRate;

	void require(const bool _condition, const std::string& _message)
	{
		if(!_condition)
			throw std::runtime_error(_message);
	}

	struct Tone
	{
		const char* name;
		Machine machine;
		std::array<uint8_t, 8> params;
		int note;
	};

	// What holds near a 200 Hz sine, as edDrumsEngineTest's Q12
	const std::array<Tone, 2> Tones{{
		{"MD sine", {Family::Machinedrum, 1}, {35, 127, 0, 0, 0, 0, 0, 0}, 0},
		{"Syntakt held", {Family::Syntakt, 0}, {64, 0, 0, 0, 0, 0, 127, 127}, 79},
	}};

	ChainSettings filterOf(const FilterType _type, const int _frequency)
	{
		ChainSettings s;
		s.filter.type = _type;
		s.filter.mode = 0;
		s.filter.frequency = _frequency;
		s.filter.width = 30;
		return s;
	}

	void setTone(Engine& _e, const int _track, const Tone& _tone)
	{
		_e.setMachine(_track, _tone.machine);
		for(int i = 0; i < 8; ++i)
			_e.setParameter(_track, i, _tone.params[static_cast<size_t>(i)]);
		_e.setNote(_track, _tone.note);
		_e.setLevel(_track, 127);
	}

	LfoSettings aimed(const LfoTarget _target, const int _depth, const LfoShape _shape = LfoShape::Triangle, const int _division = LfoQuarterNote,
		const LfoMode _mode = LfoMode::Free, const int _track = -1)
	{
		LfoSettings s;
		s.rate.division = _division;
		s.shape = _shape;
		s.mode = _mode;
		s.depth = _depth;
		s.track = _track;
		s.target = _target;
		return s;
	}

	// The envelope of a tone: the peak of each _window samples
	std::vector<float> envelope(const std::vector<float>& _x, const int _window)
	{
		std::vector<float> e;
		for(size_t n = 0; n + static_cast<size_t>(_window) <= _x.size(); n += static_cast<size_t>(_window))
		{
			float peak = 0;
			for(int i = 0; i < _window; ++i)
				peak = std::max(peak, std::abs(_x[n + static_cast<size_t>(i)]));
			e.push_back(peak);
		}
		return e;
	}

	// Samples between a tone's rising zero crossings, the mean over _from to _to
	double periodOf(const std::vector<float>& _x, const int _from, const int _to)
	{
		int first = -1, last = -1, count = 0;
		for(int n = _from + 1; n < _to; ++n)
			if(_x[static_cast<size_t>(n - 1)] <= 0 && _x[static_cast<size_t>(n)] > 0)
			{
				if(first < 0)
					first = n;
				else
					++count;
				last = n;
			}
		return count ? static_cast<double>(last - first) / count : 0;
	}

	double after(Lfo& _lfo, const double _position, const double _quarters, const double _seconds)
	{
		_lfo.advance(_position, _quarters, _seconds);
		return _lfo.value();
	}

	// Track 0 on _machine at full level and _prepare, a Hit at sample _at: _samples of it, left then right
	std::vector<float> hitAt(const Library& _library, const Machine& _machine, const int _at, const std::function<void(Engine&)>& _prepare, const int _samples = 6000)
	{
		auto e = std::make_unique<Engine>(_library);
		e->setMachine(0, _machine);
		e->setLevel(0, 127);
		e->setTempo(Bpm);
		if(_prepare)
			_prepare(*e);
		Take take(_at + _samples);
		take.render(*e, _at);
		e->noteOn(0, 127);
		take.render(*e, _samples);
		std::vector<float> y(take.channel(0).begin() + _at, take.channel(0).end());
		y.insert(y.end(), take.channel(1).begin() + _at, take.channel(1).end());
		return y;
	}

	// ---- the laws ----

	void shapes()
	{
		// syntakt::main::lfoWave takes the cycle as 2^32, as a signed word
		const std::array<LfoShape, 6> list{LfoShape::Triangle, LfoShape::Sine, LfoShape::Square, LfoShape::Saw, LfoShape::Exponential, LfoShape::Ramp};
		constexpr int Steps = 4096;
		double worst = 0;
		for(size_t w = 0; w < list.size(); ++w)
		{
			Lfo lfo;
			LfoSettings s;
			s.rate.sync = false;
			s.rate.hz = 1;
			s.shape = list[w];
			lfo.set(s, 0);
			double away = 0;
			for(int n = 0; n < Steps; ++n)
			{
				if(n)
					lfo.advance(0, 0, 1.0 / Steps);
				const auto phase = static_cast<int32_t>(static_cast<uint32_t>(static_cast<uint64_t>(n) * (uint64_t(1) << 32) / Steps));
				const double firmware = syntakt::main::lfoWave(static_cast<int32_t>(w), phase) / 2147483648.0;
				away = std::max(away, std::abs(lfo.value() - firmware));
			}
			std::printf("  %s against lfoWave: at most %.5f\n", std::array<const char*, 6>{"TRI", "SIN", "SQR", "SAW", "EXP", "RMP"}[w], away);
			worst = std::max(worst, away);
		}
		// Its sine is a polynomial, its exponential 2^-12 lower: forms, not words
		require(worst <= 0.003, "a shape's form");

		// Random: of the transport's sixteenth of a cycle, its seed's, over its whole range, from before the transport's start
		LfoSettings s;
		s.rate.division = LfoQuarterNote;
		s.shape = LfoShape::Random;
		Lfo a, b, c;
		a.set(s, 1);
		b.set(s, 1);
		c.set(s, 2);
		double low = 1, high = -1, last = 2;
		for(int n = 0; n < 4096; ++n)
		{
			const double position = n / 64.0 - 8;		// four steps to a sixteenth
			const double v = after(a, position, 0, 0);
			require(v >= -1 && v <= 1, "Random past -1 to 1");
			low = std::min(low, v);
			high = std::max(high, v);
			require(v == after(b, position, 0, 0), "Random: the same position, another value");
			require((n % 4 == 0) == (v != last), "Random: a value each sixteenth of a cycle");
			require(v != after(c, position, 0, 0), "Random: a seed's own");
			last = v;
		}
		require(low < -0.9 && high > 0.9, "Random: its whole range");
	}

	void divisions()
	{
		const std::array<double, LfoDivisions> quarters{0.0625, 0.125, 0.25, 0.5, 1, 2, 4, 8, 16, 32, 64};
		for(int d = 0; d < LfoDivisions; ++d)
		{
			const double q = quarters[static_cast<size_t>(d)];
			require(lfoQuarters(d, LfoFeel::Straight) == q, "a straight division");
			require(lfoQuarters(d, LfoFeel::Dotted) == q * 1.5, "a dotted division");
			require(std::abs(lfoQuarters(d, LfoFeel::Triplet) - q * 2 / 3) < 1e-12, "a triplet");
			for(const auto feel : {LfoFeel::Straight, LfoFeel::Dotted, LfoFeel::Triplet})
			{
				const auto rate = lfoDivisionOf(lfoQuarters(d, feel));
				require(rate.sync && rate.division == d && rate.feel == feel, "a division's own length gives it");
			}
		}
		require(lfoDivisionOf(1e-3).division == 0 && lfoDivisionOf(1e-3).feel == LfoFeel::Triplet, "past 1/64");
		require(lfoDivisionOf(1e4).division == LfoDivisions - 1 && lfoDivisionOf(1e4).feel == LfoFeel::Dotted, "past 16 bars");
	}

	// The cycles a quarter note the Syntakt's own routine runs at _speed and _multiplier
	double syntaktCycles(const int _speed, const int _multiplier, const double _bpm)
	{
		namespace m = syntakt::main;
		constexpr int Blocks = 200000;
		static const m::Section3 rom{};
		auto mem = std::make_unique<m::Memory>(rom);
		const uint32_t p = m::Memory::SramBase;
		const uint32_t params = p + 0x1c, base = params + 8;
		mem->setHalf(base - 6, static_cast<int16_t>(0x4000 + _speed * 256));
		mem->setByte(base - 4, static_cast<uint8_t>(_multiplier));
		mem->setHalf(base - 2, 0x4000);
		mem->setByte(base, 0xff);		// no destination
		const uint32_t state = m::LfoState - m::LfoStride;
		double cycles = 0;
		int32_t last = 0;
		for(int b = 0; b < Blocks; ++b)
		{
			m::lfos(*mem, p, static_cast<int32_t>(_bpm * 120), 0);
			const int32_t phase = mem->word(state - 0x24);
			cycles += static_cast<double>(phase < last ? phase + m::LfoPeriod - last : phase - last) / m::LfoPeriod;
			last = phase;
		}
		const double seconds = Blocks * 32 / 48000.0;
		return cycles / (seconds * _bpm / 60);
	}

	void syntaktSpeeds()
	{
		for(const auto& s : {std::array<int, 2>{16, 3}, {32, 3}, {63, 2}, {8, 7}, {1, 9}, {3, 6}, {25, 5}, {63, 7}})
		{
			const double cycles = syntaktCycles(s[0], s[1], Bpm);
			const double law = s[0] * std::ldexp(1.0, s[1]) / 512;
			std::printf("  SPD %d, MULT %d: %.5f cycles a quarter, SPD x MULT / 512 says %.5f\n", s[0], s[1], cycles, law);
			require(std::abs(cycles / law - 1) < 0.002, "the Syntakt's law of speed");

			// The division it takes is the nearest: within a ratio of 4/3's root
			const auto rate = lfoRateOfSyntakt(s[0], s[1]);
			require(rate.sync, "a multiplier at the tempo is a division");
			const double ratio = lfoQuarters(rate.division, rate.feel) * law;
			require(ratio < 1.156 && ratio > 1 / 1.156, "not the nearest division");
		}
		const auto bar = lfoRateOfSyntakt(16, 3);
		require(lfoQuarters(bar.division, bar.feel) == 4, "SPD 16, MULT 8: a bar");
		require(lfoRateOfSyntakt(-16, 3).division == bar.division, "the sign dropped");
		const auto bars = lfoRateOfSyntakt(0, 3);
		require(bars.division == LfoDivisions - 1 && bars.feel == LfoFeel::Dotted, "SPD 0, the slowest");

		// At the fixed rate: the same at any tempo, as HZ
		const auto fixed = lfoRateOfSyntakt(16, 15);
		const double measured = syntaktCycles(16, 15, 90) * 90 / 60;
		std::printf("  SPD 16, MULT .8: %.5f Hz at 90 BPM, %.5f Hz said\n", measured, fixed.hz);
		require(!fixed.sync && std::abs(measured / fixed.hz - 1) < 0.002, "a fixed multiplier in Hz");
	}

	void machinedrumSpeeds()
	{
		for(const int speed : {0, 1, 5, 11, 22, 44, 63, 64, 70, 80, 100, 110})
		{
			const int32_t tempo = mdDrums::Lfo::tempo(Bpm);
			const auto word = static_cast<uint16_t>(speed << 7);
			const double cycles = mdDrums::Lfo::increment(word, tempo) * mdDrums::Lfo::TickRate / mdDrums::Lfo::PhaseCount / (Bpm / 60);
			const auto rate = lfoRateOfMachinedrum(speed);
			if(cycles < 1e-3 || cycles > 16)
				continue;
			const double ratio = lfoQuarters(rate.division, rate.feel) * cycles;
			std::printf("  LFOS %d: %.4f cycles a quarter, a cycle of %.4f quarters taken\n", speed, cycles, lfoQuarters(rate.division, rate.feel));
			require(rate.sync && ratio < 1.16 && ratio > 1 / 1.16, "LFOS's nearest division");
		}
	}

	// Where LFOs add up, which Tracks a block steps, what waits: no engine
	void routing()
	{
		constexpr int Level = static_cast<int>(LfoTarget::Level), Pan = static_cast<int>(LfoTarget::Pan);
		const auto held = [](const LfoTarget _target, const int _depth, const int _track = -1) { return aimed(_target, _depth, LfoShape::Square, 4, LfoMode::Free, _track); };
		LfoBank bank;
		require(!bank.waiting(), "nothing set, nothing waits");

		// Two LFOs of two Tracks on one target add, at their Tracks' blocks alone
		bank.set(3, 0, held(LfoTarget::Level, 10));
		bank.set(5, 1, held(LfoTarget::Level, 20, 3));
		bank.step(1u << 3, 0, 0, 0);
		require(bank.offset(3, Level) == 10 * 128, "an LFO steps at its Track's blocks");
		bank.step(1u << 5, 0, 0, 0);
		require(bank.offset(3, Level) == 30 * 128, "two LFOs add");
		require(bank.waiting() == 1u << 3 && bank.waiting(3) == 1u << Level, "the target's Track waits, the LFO's does not");

		bank.taken(3, Level);
		require(!bank.waiting(), "taken, nothing waits");
		bank.step(~0u, 0.1, 0, 0);
		require(!bank.waiting(), "what does not move does not wait");

		// A target it leaves gets its share back; depth 0 too, and it is stepped no more
		bank.set(5, 1, held(LfoTarget::Pan, 20, 3));
		require(bank.offset(3, Level) == 10 * 128 && bank.waiting(3) == 1u << Level, "a target left waits");
		bank.taken(3, Level);
		bank.step(1u << 5, 0, 0, 0);
		require(bank.offset(3, Pan) == 20 * 128, "the new target");
		bank.set(5, 1, LfoSettings{});
		require(bank.offset(3, Pan) == 0, "off, nothing left on its target");
		bank.taken(3, Pan);
		bank.step(1u << 5, 0, 0, 0);
		require(!bank.waiting(), "off, it is not stepped");

		// A Hit of its Track moves it at once: a HOLD on the low half of its square
		auto hold = held(LfoTarget::Level, 10);
		hold.mode = LfoMode::Hold;
		bank.set(3, 0, hold);
		bank.trig(3, 0.75);
		require(bank.offset(3, Level) == -10 * 128, "a Hit's sample reaches its target");
		bank.trig(5, 0.25);
		require(bank.offset(3, Level) == -10 * 128, "a Hit of another Track");
		std::printf("  an LFO bank: sums, clocks, what waits, leaving, a Hit\n");
	}

	void modes()
	{
		const LfoSettings base = aimed(LfoTarget::Level, 127, LfoShape::Saw, LfoQuarterNote);

		// Each mode, a cycle a quarter note
		auto s = base;
		s.mode = LfoMode::Trig;
		Lfo trig;
		trig.set(s, 0);
		trig.advance(0, 0.75, 0);
		require(std::abs(trig.phase() - 0.75) < 1e-12, "TRIG counts from where it stands");
		trig.trig(5.3);
		require(trig.phase() == 0 && trig.value() == 1, "TRIG restarts at a Hit");
		trig.advance(99, 0.25, 0);
		require(std::abs(trig.phase() - 0.25) < 1e-12, "TRIG follows the quarters, not the position");

		s.mode = LfoMode::Free;
		Lfo free;
		free.set(s, 0);
		free.trig(3);
		require(after(free, 3.25, 1, 0) == 0.5, "FREE keeps the transport");

		s.mode = LfoMode::Hold;
		Lfo hold;
		hold.set(s, 0);
		require(after(hold, 0.25, 1, 0) == 0.5, "HOLD runs until a Hit");
		hold.trig(0.75);
		require(hold.value() == -0.5, "HOLD samples at the Hit's position");
		require(after(hold, 0.9, 1, 0) == -0.5, "HOLD holds");
		require(hold.phase() != 0.75, "its phase goes on");
		hold.trig(0.25);
		require(hold.value() == 0.5, "the next Hit samples again");

		s.mode = LfoMode::One;
		Lfo one;
		one.set(s, 0);
		one.trig(0);
		one.advance(0, 0.5, 0);
		require(one.value() == 0, "ONE runs");
		one.advance(0, 0.75, 0);
		require(one.value() == 0 && one.phase() == 1, "ONE stops at the cycle's end");
		one.advance(0, 7, 0);
		require(one.value() == 0, "ONE stays");
		one.trig(1);
		require(one.value() == 1, "ONE restarts at a Hit");

		s.mode = LfoMode::Half;
		Lfo half;
		half.set(s, 0);
		half.trig(0);
		half.advance(0, 0.75, 0);
		require(half.value() == 0 && half.phase() == 0.5, "HALF stops at half the cycle");

		// HZ takes seconds, not quarters
		s.mode = LfoMode::Free;
		s.rate.sync = false;
		s.rate.hz = 2;
		Lfo hz;
		hz.set(s, 0);
		hz.advance(123, 456, 0.125);
		require(hz.phase() == 0.25, "HZ takes its seconds");

		std::printf("  the modes: TRIG, HOLD, ONE, HALF, FREE, HZ\n");
	}

	// ---- the transport ----

	constexpr int TransportSamples = 40000;

	// One take of a tone under _lfo from position 0, in host blocks from _sizes in turn, each told the transport as a host does
	std::vector<float> onTransport(const Library& _library, const Tone& _tone, const std::vector<int>& _sizes, const LfoSettings& _lfo)
	{
		auto e = std::make_unique<Engine>(_library);
		setTone(*e, 0, _tone);
		e->setChain(0, filterOf(FilterType::Syntakt, 40));		// a filter the frequency moves
		e->setTempo(Bpm);
		e->setLfo(0, 0, _lfo);
		Take take(TransportSamples);
		e->noteOn(0, 127);
		size_t s = 0;
		while(take.at() < TransportSamples)
		{
			const int count = std::min(_sizes[s++ % _sizes.size()], TransportSamples - take.at());
			e->setPosition(take.at() * QuartersPerSample);
			take.render(*e, count);
		}
		return take.channel(0);
	}

	void blockSizes(const Library& _library)
	{
		std::mt19937 random(13);
		std::vector<int> sizes;
		for(int i = 0; i < 2000; ++i)
			sizes.push_back(1 + static_cast<int>(random() % 700));
		for(const auto& tone : Tones)
			for(const auto target : {LfoTarget::Level, LfoTarget::Frequency, LfoTarget::Machine1})
			{
				const auto lfo = aimed(target, 60, LfoShape::Sine, 2);
				const auto whole = onTransport(_library, tone, {TransportSamples}, lfo);
				const auto small = onTransport(_library, tone, {32}, lfo);
				const auto host = onTransport(_library, tone, {128}, lfo);
				const auto cut = onTransport(_library, tone, sizes, lfo);
				require(whole == small && whole == host && whole == cut, std::string(tone.name) + ": the host's blocks moved the samples");
				const auto plain = onTransport(_library, tone, {128}, LfoSettings{});
				require(whole != plain, std::string(tone.name) + ": the LFO moved nothing");
			}
		std::printf("  the same samples on 3 targets of both families in blocks of %d, 128, 32 and at random\n", TransportSamples);
	}

	void samePhase(const Library& _library)
	{
		// The phase at a position, wherever the engine's own time stands and whatever came before
		const auto lfo = aimed(LfoTarget::Level, 100, LfoShape::Triangle, 3, LfoMode::Free);
		const auto phaseAt = [&](const int _idle, const double _from, const double _to)
		{
			auto e = std::make_unique<Engine>(_library);
			setTone(*e, 0, Tones[0]);
			e->setTempo(Bpm);
			e->setLfo(0, 0, lfo);
			Take take(_idle + 70000);
			take.render(*e, _idle);
			e->setPosition(_from);
			const int samples = static_cast<int>((_to - _from) / QuartersPerSample / BlockSize) * BlockSize;
			while(take.at() < _idle + samples)
				take.render(*e, std::min(128, _idle + samples - take.at()));
			return e->lfoPhase(0, 0);
		};
		const double base = phaseAt(0, 0, 3.17);
		double worst = 0;
		for(const int idle : {1024, 3200, 32000})
			worst = std::max(worst, std::abs(phaseAt(idle, 0, 3.17) - base));
		std::printf("  the same position after 1024 to 32000 samples of engine: phases %.3g apart (a block is %.5f of this cycle)\n", worst,
			BlockSize * QuartersPerSample / 0.5);
		require(worst < 1e-9, "the same position, another phase");

		// Off the blocks' grid, the last step's position differs by less than a block
		const double offGrid = std::abs(phaseAt(1000, 0, 3.17) - base);
		std::printf("  ... after 1000: %.5f\n", offGrid);
		require(offGrid <= BlockSize * QuartersPerSample / 0.5 + 1e-9, "more than a block off");

		// A host that jumps: where it landed, the phase is the position's, the same as when it first passed there
		auto e = std::make_unique<Engine>(_library);
		setTone(*e, 0, Tones[1]);
		e->setTempo(Bpm);
		e->setLfo(0, 0, lfo);
		Take take(120000);
		const auto run = [&](const int _samples)
		{
			for(int done = 0; done < _samples; done += 128)
				take.render(*e, 128);
		};
		run(19200);
		const double first = e->position();
		run(19200);
		require(std::abs(e->position() - 2 * first) < 1e-9, "the transport ran at its tempo");
		const double far = e->position();
		e->setPosition(far - 1);
		require(std::abs(e->position() - (far - 1)) < 1e-12, "a jump");
		e->setPosition(e->position() + Engine::PositionTolerance / 2);
		require(std::abs(e->position() - (far - 1)) < 1e-12, "agreeing to the tolerance, nothing moves");
		e->setPosition(e->position() + Engine::PositionTolerance * 2);
		require(std::abs(e->position() - (far - 1)) > Engine::PositionTolerance, "past the tolerance, it follows");
		std::printf("  a jump on the transport followed, a host's rounding left alone\n");

		// A tempo changed at any sample leaves the position where it stood
		take.render(*e, 128);
		const double before = e->position();
		e->setTempo(77);
		require(std::abs(e->position() - before) < 1e-12, "a tempo moved the position");
	}

	// Rising edges, in samples, of a square on a tone's level
	std::vector<int> edges(const std::vector<float>& _x)
	{
		constexpr int Window = 256;
		const auto e = envelope(_x, Window);
		const float floor = *std::max_element(e.begin(), e.end()) / 1000;
		std::vector<int> found;
		for(size_t i = 1; i < e.size(); ++i)
			if(e[i - 1] < floor && e[i] >= floor)
				found.push_back(static_cast<int>(i) * Window);
		return found;
	}

	void tempoFollowed(const Library& _library)
	{
		// A quarter note's square: told by the host, then running on alone, at two tempos
		const auto lfo = aimed(LfoTarget::Level, 127, LfoShape::Square, LfoQuarterNote);
		for(const auto& tone : Tones)
			for(const bool told : {true, false})
			{
				constexpr int Samples = 400000;
				auto e = std::make_unique<Engine>(_library);
				setTone(*e, 0, tone);
				e->setLevel(0, 64);
				e->setTempo(Bpm);
				e->setLfo(0, 0, lfo);
				Take take(Samples);
				e->noteOn(0, 127);
				constexpr int Change = 200000;
				double position = 0;
				while(take.at() < Samples)
				{
					const double bpm = take.at() < Change ? Bpm : 80;
					e->setTempo(bpm);
					if(take.at() % (512 * 8) == 0)		// the tone's own fall away
						e->noteOn(0, 127);
					if(told)
						e->setPosition(position);
					const int count = std::min(512, Samples - take.at());
					take.render(*e, count);
					position += count * bpm / 60 / SampleRate;
				}
				const auto found = edges(take.channel(0));
				double before = 0, after = 0;
				int nBefore = 0, nAfter = 0;
				for(size_t i = 1; i < found.size(); ++i)
				{
					const int period = found[i] - found[i - 1];
					if(found[i] < Change)
					{
						before += period;
						++nBefore;
					}
					else if(found[i - 1] > Change)
					{
						after += period;
						++nAfter;
					}
				}
				require(nBefore > 2 && nAfter > 2, std::string(tone.name) + ": no edges");
				before /= nBefore;
				after /= nAfter;
				std::printf("  %s, transport %s: a quarter's square %.1f samples at 120 BPM (22050), %.1f at 80 (33075)\n", tone.name,
					told ? "told" : "alone", before, after);
				require(std::abs(before - 22050) < 256 && std::abs(after - 33075) < 256, "the period did not follow the tempo");
			}
	}

	// A mode that restarts, and HZ, count their family's blocks: the same seconds on either
	void clocks(const Library& _library)
	{
		for(const auto& tone : Tones)
			for(const bool hz : {false, true})
			{
				auto e = std::make_unique<Engine>(_library);
				setTone(*e, 0, tone);
				e->setTempo(90);
				auto lfo = aimed(LfoTarget::Level, 10, LfoShape::Triangle, 8, LfoMode::Trig);
				lfo.rate.sync = !hz;
				lfo.rate.hz = 0.25;
				e->setLfo(0, 0, lfo);
				Take take(44100);
				take.render(*e, 1000);
				e->noteOn(0, 127);
				take.render(*e, 44100 - 1000);
				const double seconds = 43100 / SampleRate;
				const double expected = hz ? seconds * 0.25 : seconds * 90 / 60 / lfoQuarters(8, LfoFeel::Straight);
				const double phase = e->lfoPhase(0, 0);
				std::printf("  %s, %s: phase %.5f after a second, %.5f said\n", tone.name, hz ? "HZ" : "TRIG", phase, expected);
				require(std::abs(phase - expected) < 3 * BlockSize / SampleRate * 0.25, "a restarted LFO's clock");
			}
	}

	// The envelope a TRIG LFO's depth moves starts at a Hit where the LFO stands at 0, and Random draws anew at each restart
	void restarts(const Library& _library)
	{
		const auto render = [&](const int _attack)
		{
			return hitAt(_library, Tones[1].machine, 0, [&](Engine& _e)
			{
				setTone(_e, 0, Tones[1]);
				auto chain = filterOf(FilterType::Syntakt, 30);
				chain.filter.envelope.attack = _attack;
				chain.filter.envelope.sustain = 127;
				_e.setChain(0, chain);
				_e.setLfo(0, 0, aimed(LfoTarget::EnvelopeDepth, 40, LfoShape::Triangle, 7, LfoMode::Trig));
			}, 20000);
		};
		require(render(0) != render(100), "the envelope did not start under an LFO at 0");

		LfoSettings s;
		s.shape = LfoShape::Random;
		s.mode = LfoMode::Trig;
		Lfo lfo;
		lfo.set(s, 0);
		lfo.trig(0);
		const double first = lfo.value();
		lfo.trig(0);
		require(lfo.value() != first, "Random drew the same value after a restart");
		std::printf("  an envelope under an LFO at 0, Random after a restart\n");
	}

	// ---- targets ----

	void acrossTracks(const Library& _library)
	{
		constexpr int Samples = 70000;
		const std::array<Machine, 3> sources{Machine{}, Tones[0].machine, Tones[1].machine};
		const std::array<const char*, 3> names{"an Off Track", "an MD Track", "a Syntakt Track"};
		for(size_t s = 0; s < sources.size(); ++s)
			for(const auto& tone : Tones)
			{
				const auto render = [&](const bool _lfo)
				{
					auto e = std::make_unique<Engine>(_library);
					e->setMachine(0, sources[s]);
					setTone(*e, 1, tone);
					e->setOutput(1, 1);
					e->setLevel(1, 64);
					e->setTempo(Bpm);
					if(_lfo)
						e->setLfo(0, 0, aimed(LfoTarget::Level, 64, LfoShape::Square, 5, LfoMode::Free, 1));
					Take take(Samples);
					e->noteOn(1, 127);
					take.render(*e, Samples);
					return envelope(take.channel(2), 512);
				};
				const auto plain = render(false), moved = render(true);
				float low = 1e9f, high = 0;
				for(size_t i = 8; i < plain.size(); ++i)
				{
					const float ratio = moved[i] / plain[i];
					low = std::min(low, ratio);
					high = std::max(high, ratio);
				}
				// Level 64 moved to 128 and to 0 by a depth of 64, a half note each: the gain's square law gives 4, and 0
				std::printf("  from %s on %s's level: gain %.3f to %.3f of the Track's own\n", names[s], tone.name, low, high);
				require(high > 3.5f && high < 4.5f && low < 0.05f, "an LFO on another Track's level");
			}
	}

	// What a target means: a TRIG square held high is the value moved by its depth, so that a Hit under it is, bit for bit, the
	// Hit of a Kit that holds that value. SYN1-8 on two machines of each family, level, pan and the chain's values on each
	void targets(const Library& _library)
	{
		constexpr int Value = 40, Depth = 30, Samples = 8000;
		using Set = std::function<void(Engine&, ChainSettings&, int)>;
		struct Target
		{
			const char* name;
			LfoTarget target;
			Set set;
		};
		const auto machine = [](const int _index) -> Set { return [_index](Engine& _e, ChainSettings&, const int _v) { _e.setParameter(0, _index, static_cast<uint8_t>(_v)); }; };
		const auto syntaktFilter = [](ChainSettings& _s) { _s.filter.type = FilterType::Syntakt; _s.filter.mode = 0; _s.filter.frequency = _s.filter.resonance = 90; };
		const auto mdFilter = [](ChainSettings& _s) { _s.filter.type = FilterType::Machinedrum; _s.filter.frequency = 30; _s.filter.width = _s.filter.resonance = 90; };
		const std::array<Target, 22> list{{
			{"SYN1", LfoTarget::Machine1, machine(0)}, {"SYN2", LfoTarget::Machine2, machine(1)},
			{"SYN3", LfoTarget::Machine3, machine(2)}, {"SYN4", LfoTarget::Machine4, machine(3)},
			{"SYN5", LfoTarget::Machine5, machine(4)}, {"SYN6", LfoTarget::Machine6, machine(5)},
			{"SYN7", LfoTarget::Machine7, machine(6)}, {"SYN8", LfoTarget::Machine8, machine(7)},
			{"Level", LfoTarget::Level, [](Engine& _e, ChainSettings&, const int _v) { _e.setLevel(0, _v); }},
			{"Pan", LfoTarget::Pan, [](Engine& _e, ChainSettings&, const int _v) { _e.setPan(0, _v); }},
			{"Drive A", LfoTarget::DriveA, [](Engine&, ChainSettings& _s, const int _v) { _s.drive[0] = {DriveType::Syntakt, false, _v, 127}; }},
			{"Drive B", LfoTarget::DriveB, [](Engine&, ChainSettings& _s, const int _v) { _s.drive[1] = {DriveType::MdDist, true, _v, 127}; }},
			{"BASE", LfoTarget::BandA, [](Engine&, ChainSettings& _s, const int _v) { _s.band.type = BandType::BaseWidth; _s.band.values = {_v, 100}; }},
			{"WIDTH", LfoTarget::BandB, [](Engine&, ChainSettings& _s, const int _v) { _s.band.type = BandType::BaseWidth; _s.band.values = {10, _v}; }},
			{"EQF", LfoTarget::BandA, [](Engine&, ChainSettings& _s, const int _v) { _s.band.type = BandType::MdEq; _s.band.values = {_v, 90}; }},
			{"EQG", LfoTarget::BandB, [](Engine&, ChainSettings& _s, const int _v) { _s.band.type = BandType::MdEq; _s.band.values = {40, _v}; }},
			{"Low band", LfoTarget::BandA, [](Engine&, ChainSettings& _s, const int _v) { _s.band.type = BandType::ThreeBand; _s.band.values = {60, _v, 70, 64, 100, 64}; }},
			{"Middle band", LfoTarget::BandB, [](Engine&, ChainSettings& _s, const int _v) { _s.band.type = BandType::ThreeBand; _s.band.values = {60, 64, 70, _v, 100, 64}; }},
			{"Frequency", LfoTarget::Frequency, [=](Engine&, ChainSettings& _s, const int _v) { syntaktFilter(_s); _s.filter.frequency = _v; }},
			{"Resonance", LfoTarget::Resonance, [=](Engine&, ChainSettings& _s, const int _v) { syntaktFilter(_s); _s.filter.resonance = _v; }},
			{"FLTF", LfoTarget::Frequency, [=](Engine&, ChainSettings& _s, const int _v) { mdFilter(_s); _s.filter.frequency = _v; }},
			{"FLTW", LfoTarget::Width, [=](Engine&, ChainSettings& _s, const int _v) { mdFilter(_s); _s.filter.width = _v; }},
		}};
		int checked = 0;
		for(const Machine m : {Machine{Family::Machinedrum, 17}, Machine{Family::Machinedrum, 2}, Machine{Family::Syntakt, 1}, Machine{Family::Syntakt, 6}})
			for(const auto& target : list)
			{
				const auto render = [&](const int _value, const bool _lfo)
				{
					return hitAt(_library, m, 0, [&](Engine& _e)
					{
						_e.setLevel(0, 100);
						for(int i = 0; i < 8; ++i)
							_e.setParameter(0, i, 64);
						ChainSettings chain;
						target.set(_e, chain, _value);
						_e.setChain(0, chain);
						if(_lfo)
							_e.setLfo(0, 0, aimed(target.target, Depth, LfoShape::Square, LfoDivisions - 1, LfoMode::Trig));
					}, Samples);
				};
				const auto moved = render(Value, true), kit = render(Value + Depth, false);
				double power = 0, apart = 0;
				for(size_t n = 0; n < moved.size(); ++n)
				{
					power += kit[n] * kit[n];
					apart += (moved[n] - kit[n]) * (moved[n] - kit[n]);
				}
				if(moved != kit)
					std::printf("  machine %d of family %d, %s: %.1f dB from the Kit's own value\n", m.id, static_cast<int>(m.family), target.name, 10 * std::log10(apart / power));
				require(moved == kit, std::string(target.name) + ": not the value it says");
				++checked;
			}
		std::printf("  %d targets on 4 machines: under a held square, a Hit is that of a Kit at the value moved, bit for bit\n", checked);

	}

	// What meets an LFO: a sign, another LFO, a lock, a knob, a change of family, its own end, another target
	void interplay(const Library& _library)
	{
		constexpr int Samples = 8000, Later = 2048;
		using Action = std::function<void(Engine&)>;
		const auto held = [](const LfoTarget _target, const int _depth) { return aimed(_target, _depth, LfoShape::Square, LfoDivisions - 1, LfoMode::Trig); };
		for(const Machine m : {Machine{Family::Machinedrum, 17}, Machine{Family::Syntakt, 1}})
		{
			const auto render = [&](const Action& _before, const Action& _atHit = {}, const Action& _later = {})
			{
				auto e = std::make_unique<Engine>(_library);
				e->setMachine(0, m);
				e->setLevel(0, 90);
				_before(*e);
				Take take(Samples);
				if(_atHit)
					_atHit(*e);
				e->noteOn(0, 127);
				take.render(*e, Later);
				if(_later)
					_later(*e);
				take.render(*e, Samples - Later);
				return take.channel(0);
			};
			const auto first = [](const int _value) { return [_value](Engine& _e) { _e.setParameter(0, 0, static_cast<uint8_t>(_value)); }; };

			require(render([&](Engine& _e) { first(70)(_e); _e.setLfo(0, 0, held(LfoTarget::Machine1, -30)); }) == render(first(40)), "a negative depth");

			require(render([&](Engine& _e)
			{
				first(40)(_e);
				_e.setLfo(0, 0, held(LfoTarget::Machine1, 18));
				_e.setLfo(0, 1, held(LfoTarget::Machine1, 12));
			}) == render(first(70)), "two LFOs add");

			require(render([&](Engine& _e) { first(64)(_e); _e.setLfo(0, 0, held(LfoTarget::Machine1, 30)); }, [](Engine& _e) { _e.lock(0, 0, 20); })
				== render(first(64), [](Engine& _e) { _e.lock(0, 0, 50); }), "an LFO over a lock");

			require(render([&](Engine& _e) { first(40)(_e); _e.setLfo(0, 0, held(LfoTarget::Machine1, 30)); }, {}, first(50))
				== render(first(70), {}, first(80)), "an LFO over a knob");

			// Its depth down to 0: the level is the Track's again, the LFO off
			const auto level = [&](Engine& _e) { _e.setLfo(0, 0, held(LfoTarget::Level, 30)); };
			auto ending = render(level, {}, [&](Engine& _e) { _e.setLfo(0, 0, held(LfoTarget::Level, 0)); });
			auto taken = render(level, {}, [](Engine& _e) { _e.setLfo(0, 0, aimed(LfoTarget::Level, 30, LfoShape::Square, LfoDivisions - 1, LfoMode::Trig, 1)); });
			const auto plain = render([](Engine&) {});
			constexpr size_t Settled = Later + 2 * Engine::HitDelay + 4 * BlockSize;
			require(std::equal(ending.begin() + Settled, ending.end(), plain.begin() + Settled), "an LFO's end left its target moved");
			require(std::equal(taken.begin() + Settled, taken.end(), plain.begin() + Settled), "another target left the first moved");
			require(!std::equal(ending.begin(), ending.begin() + Later, plain.begin()), "the level did not move");
		}

		// A move goes with its Track to the other family's chain, and leaves nothing behind in the first
		constexpr int Wait = 3200, Length = 12000;
		const Machine md{Family::Machinedrum, 2}, st{Family::Syntakt, 1};
		const auto journey = [&](const bool _lfo)
		{
			auto e = std::make_unique<Engine>(_library);
			e->setMachine(0, md);
			e->setChain(0, filterOf(FilterType::Syntakt, 40));
			if(_lfo)
				e->setLfo(0, 0, held(LfoTarget::Frequency, 30));
			Take take(4 * Length);
			e->noteOn(0, 127);
			take.render(*e, Wait);
			e->setMachine(0, st);
			e->setChain(0, filterOf(FilterType::Syntakt, 40));
			take.render(*e, Length - Wait);
			e->noteOn(0, 127);
			take.render(*e, Length);
			e->setLfo(0, 0, held(LfoTarget::Frequency, 0));
			take.render(*e, Length);
			e->setMachine(0, md);
			e->setChain(0, filterOf(FilterType::Syntakt, 40));
			e->noteOn(0, 127);
			take.render(*e, Length);
			return take.channel(0);
		};
		const auto moved = journey(true), still = journey(false);

		auto syntakt = std::make_unique<Engine>(_library);
		syntakt->setMachine(0, st);
		syntakt->setChain(0, filterOf(FilterType::Syntakt, 70));
		Take kit(2 * Length);
		kit.render(*syntakt, Length);
		syntakt->noteOn(0, 127);
		kit.render(*syntakt, Length);
		constexpr int Tail = 400;		// what the last voice left
		require(std::equal(kit.channel(0).begin() + Length + Tail, kit.channel(0).end() - Tail, moved.begin() + Length + Tail), "a move did not follow its Track to the other family");
		require(std::equal(moved.begin() + 3 * Length + Tail, moved.end(), still.begin() + 3 * Length + Tail), "a move stayed in the first family's chain");
		std::printf("  an LFO's sign, two on a target, a lock, a knob, its end, another target, a change of family\n");
	}

	// A band moved under a voice that sounds comes to where a Kit has it
	void bandsWhileSounding(const Library& _library)
	{
		constexpr int Samples = 30000, Move = 8000, Settled = 22000, Depth = 30;
		for(const LfoTarget target : {LfoTarget::BandA, LfoTarget::BandB})
		{
			const auto render = [&](const int _low, const int _middle, const bool _lfo)
			{
				auto e = std::make_unique<Engine>(_library);
				setTone(*e, 0, Tones[1]);
				ChainSettings chain;
				chain.band.type = BandType::ThreeBand;
				chain.band.values = {60, _low, 70, _middle, 100, 64};
				e->setChain(0, chain);
				Take take(Samples);
				e->noteOn(0, 127);
				take.render(*e, Move);
				if(_lfo)
					e->setLfo(0, 0, aimed(target, Depth, LfoShape::Square, LfoDivisions - 1, LfoMode::Free));
				take.render(*e, Samples - Move);
				return take.channel(0);
			};
			const bool low = target == LfoTarget::BandA;
			const auto moved = render(40, 40, true), kit = render(low ? 40 + Depth : 40, low ? 40 : 40 + Depth, false);
			double power = 0, apart = 0;
			for(size_t n = Settled; n < Samples; ++n)
			{
				power += kit[n] * kit[n];
				apart += (kit[n] - moved[n]) * (kit[n] - moved[n]);
			}
			require(power > 1e-3, "no voice left to measure");
			const double dB = 10 * std::log10(apart / power + 1e-30);
			std::printf("  a band moved under a sounding voice: %.1f dB from a Kit at that value\n", dB);
			require(dB < -60, "a band's move did not reach its band");
		}
	}

	// What reaches the tables: past a value's ends a move is that end, in both directions
	void bounds(const Library& _library)
	{
		const auto render = [&](const Machine& _machine, const int _value, const int _depth, const LfoTarget _target)
		{
			return hitAt(_library, _machine, 0, [&](Engine& _e)
			{
				for(int i = 0; i < 8; ++i)
					_e.setParameter(0, i, static_cast<uint8_t>(_value));
				_e.setLevel(0, _target == LfoTarget::Level ? _value : 100);
				_e.setPan(0, _target == LfoTarget::Pan ? _value : 64);
				ChainSettings chain;
				chain.drive[0] = {DriveType::Syntakt, false, _value, 127};
				chain.drive[1] = {DriveType::MdDist, true, _value, 127};
				chain.band.type = BandType::MdEq;
				chain.band.values = {_value, _value};
				chain.filter.type = FilterType::Machinedrum;
				chain.filter.frequency = chain.filter.resonance = chain.filter.width = _value;
				chain.filter.depth = _value;
				_e.setChain(0, chain);
				if(_depth)
					_e.setLfo(0, 0, aimed(_target, _depth, LfoShape::Square, LfoDivisions - 1, LfoMode::Trig));
			}, 8192);
		};
		int checked = 0;
		for(const auto& machine : {Machine{Family::Machinedrum, 1}, Machine{Family::Syntakt, 0}})
			for(int t = static_cast<int>(LfoTarget::Machine1); t < static_cast<int>(LfoTarget::Count); ++t)
			{
				const auto target = static_cast<LfoTarget>(t);
				// The machine's words end at $3FFF on the Machinedrum, as the OS's apply limits them, past the Kit's $3F80
				const bool word = machine.family == Family::Machinedrum && target <= LfoTarget::Machine8;
				require(render(machine, 127, 127, target) == render(machine, 127, word ? 1 : 0, target), "over the top");
				require(render(machine, 0, -127, target) == render(machine, 0, 0, target), "under the bottom");
				++checked;
			}
		std::printf("  %d targets at both ends of their ranges, at full depth: the end itself\n", checked);

		// The Machinedrum's words reach $3FFF, as the OS's apply lets them, past the Kit's $3F80
		require(render(Machine{Family::Machinedrum, 1}, 127, 1, LfoTarget::Machine1) != render(Machine{Family::Machinedrum, 1}, 127, 0, LfoTarget::Machine1),
			"the machine's word stopped at the Kit's top");
	}

	// How late a Track reads an LFO, by where its Hit fell: alignTicks puts each Track's ticks on its Hit. A square on TRX-less
	// GND-SN's pitch, of this Track or of another, and when the tone's period changes after the square's edge
	void ticks(const Library& _library)
	{
		constexpr int BlocksPerTick = 11;
		const Tone tone{"MD sine", {Family::Machinedrum, 1}, {80, 127, 0, 0, 0, 0, 0, 0}, 0};
		for(const bool own : {true, false})
		{
			int earliest = 1 << 30, latest = -1;
			for(int at = 0; at < BlocksPerTick; ++at)
			{
				auto e = std::make_unique<Engine>(_library);
				setTone(*e, 1, tone);
				e->setOutput(1, 1);
				e->setTempo(Bpm);
				Take take(88200);
				e->setLfo(own ? 1 : 0, 0, aimed(LfoTarget::Machine1, 6, LfoShape::Square, 4, LfoMode::Free, 1));
				// The Hit on a block _at blocks after the first tick, the edge at 22050: the half period of 1/4
				take.render(*e, 2000 * 32 / BlockSize + at * BlockSize);
				e->noteOn(1, 127);
				take.render(*e, 88200 - take.at());
				const auto& y = take.channel(2);

				// After the edge, the first half-cycle of the tone at the lower pitch
				constexpr int Edge = 22050 + Engine::HitDelay;
				const double before = periodOf(y, Edge - 8000, Edge - 200);
				int changed = -1;
				for(int n = Edge - 2000, last = -1; n < Edge + 4000; ++n)
					if(y[static_cast<size_t>(n - 1)] <= 0 && y[static_cast<size_t>(n)] > 0)
					{
						if(last >= 0 && std::abs((n - last) / before - 1) > 0.02)
						{
							changed = last - Edge;
							break;
						}
						last = n;
					}
				require(changed > -2000, "the LFO moved no pitch");
				earliest = std::min(earliest, changed);
				latest = std::max(latest, changed);
			}
			std::printf("  an LFO of %s Track on SYN1: the pitch moves %d to %d samples after the square's edge, 11 Hit positions (%.2f ms apart)\n",
				own ? "the same" : "another", earliest, latest, (latest - earliest) / 44.1);
			// The words move at the next block, wherever the Hit put the Track's ticks
			require(latest - earliest <= 2 * BlockSize, "SYN moved by more than the block");
		}

		// The level goes by the block: where the same square opens another Track's level, at the same 11 Hits
		int earliest = 1 << 30, latest = -1;
		for(int at = 0; at < BlocksPerTick; ++at)
		{
			auto e = std::make_unique<Engine>(_library);
			setTone(*e, 1, tone);
			e->setOutput(1, 1);
			e->setLevel(1, 0);
			e->setTempo(Bpm);
			Take take(40000);
			e->setLfo(0, 0, aimed(LfoTarget::Level, 127, LfoShape::Square, 4, LfoMode::Free, 1));
			take.render(*e, 2000 + at * BlockSize);
			e->noteOn(1, 127);
			take.render(*e, 40000 - take.at());
			const auto& y = take.channel(2);
			constexpr int Edge = 22050 + Engine::HitDelay;
			int opened = 15000;		// it closed at 11025
			while(opened < 40000 && std::abs(y[static_cast<size_t>(opened)]) < 1e-4f)
				++opened;
			earliest = std::min(earliest, opened - Edge);
			latest = std::max(latest, opened - Edge);
		}
		std::printf("  an LFO of another Track on level: it opens %d to %d samples after the square's edge, the same 11 Hits\n", earliest, latest);
		require(latest - earliest <= 2 * BlockSize, "level by more than the block");
	}

	// ---- what an LFO leaves alone ----

	void depthZero(const Library& _library)
	{
		const std::array<int, 12> positions{0, 1, 7, 31, 32, 33, 100, 145, 146, 147, 251, 977};
		int compared = 0;
		for(const auto& machine : machines())
			for(const int at : positions)
			{
				const auto plain = hitAt(_library, machine, at, {});
				const auto off = hitAt(_library, machine, at, [&](Engine& _e)
				{
					for(int t = 0; t < Tracks; ++t)
						for(int i = 0; i < 2; ++i)
						{
							auto s = aimed(static_cast<LfoTarget>(1 + (t + 8 * i) % (static_cast<int>(LfoTarget::Count) - 1)), 0, LfoShape::Square, 0,
								i ? LfoMode::Trig : LfoMode::Free, (t + i) % Tracks);
							_e.setLfo(t, i, s);
						}
				});
				require(plain == off, "depth 0 moved a Hit");

				// A depth, and a RMP in its half at 0 all through the Hit: nothing moves, nothing changes
				const auto still = hitAt(_library, machine, at, [&](Engine& _e)
				{
					_e.setPosition(2.1);
					for(int i = 0; i < 2; ++i)
						_e.setLfo(0, i, aimed(i ? LfoTarget::Frequency : LfoTarget::Machine1, 127, LfoShape::Ramp, 6));
				});
				require(plain == still, "an LFO standing at 0 moved a Hit");
				++compared;
			}
		std::printf("  depth 0 on all 32 LFOs: the same samples, %zu machines at %zu positions, %d Hits\n", machines().size(), positions.size(), compared);
	}

	void trigHits(const Library& _library)
	{
		// TRIG: the phase in blocks from the Hit, so that the Hit keeps its samples wherever its note falls, on every target at once
		const auto lfos = [](Engine& _e)
		{
			_e.setLfo(0, 0, aimed(LfoTarget::Level, 40, LfoShape::Sine, 1, LfoMode::Trig));
			_e.setLfo(0, 1, aimed(LfoTarget::Machine2, 40, LfoShape::Saw, 0, LfoMode::Trig));
		};
		const std::array<int, 12> positions{0, 1, 7, 31, 32, 33, 100, 145, 146, 147, 251, 977};
		for(const Machine machine : {Machine{Family::Machinedrum, 2}, Machine{Family::Machinedrum, 16}, Machine{Family::Syntakt, 0}, Machine{Family::Syntakt, 1}})
		{
			const auto first = hitAt(_library, machine, positions[0], lfos);
			const auto plain = hitAt(_library, machine, positions[0], {});
			require(first != plain, "the TRIG LFOs moved nothing");
			const auto start = [](const std::vector<float>& _y) { return static_cast<int>(std::find_if(_y.begin(), _y.end(), [](const float _v) { return _v != 0; }) - _y.begin()); };
			const int firstSample = start(first), plainFirst = start(plain);
			for(const int at : positions)
				require(hitAt(_library, machine, at, lfos) == first, "a TRIG LFO's Hit moved with its note");
			std::printf("  TRIG LFOs on level and SYN2, machine %d of family %d: bit-identical at %zu positions, first sample %d after the note, plain %d (HitDelay %d, jitter 0)\n",
				machine.id, static_cast<int>(machine.family), positions.size(), firstSample, plainFirst, Engine::HitDelay);
			require(firstSample == plainFirst, "an LFO moved the latency");
		}
	}

	void otherTracks(const Library& _library)
	{
		// An LFO of Track 5 on Track 5 leaves Track 0 as it was, bit for bit
		for(const auto& tone : Tones)
		{
			const auto render = [&](const bool _lfo)
			{
				auto e = std::make_unique<Engine>(_library);
				setTone(*e, 0, tone);
				setTone(*e, 5, tone);
				e->setOutput(5, 5);
				if(_lfo)
				{
					e->setLfo(5, 0, aimed(LfoTarget::Frequency, 80, LfoShape::Sine, 0));
					e->setLfo(5, 1, aimed(LfoTarget::Level, 80, LfoShape::Square, 0));
				}
				Take take(20000);
				e->noteOn(0, 127);
				e->noteOn(5, 127);
				take.render(*e, 20000);
				return take.channel(0);
			};
			require(render(false) == render(true), "an LFO reached a Track it does not aim at");
		}
		std::printf("  an LFO aimed at its own Track leaves the others' samples\n");
	}

	// ---- anti-click ----

	double peakBetween(const std::vector<float>& _y, const int _from, const int _to)
	{
		float peak = 0;
		for(int n = _from; n < _to; ++n)
			peak = std::max(peak, std::abs(_y[static_cast<size_t>(n)]));
		return peak;
	}

	// HF at each edge of a square of _division, _late samples on, on a window that must hold something
	double edgesHf(const Library& _library, const Tone& _tone, const int _division, const std::function<void(Engine&)>& _prepare, const int _late = 0)
	{
		constexpr int Samples = 200000, Warm = 12000, Around = 4096;
		auto e = std::make_unique<Engine>(_library);
		setTone(*e, 0, _tone);
		e->setTempo(Bpm);
		_prepare(*e);
		Take take(Samples);
		e->noteOn(0, 127);
		take.render(*e, Samples);
		const auto& y = take.channel(0);
		const int half = static_cast<int>(lfoQuarters(_division, LfoFeel::Straight) / 2 / QuartersPerSample);
		double worst = -300;
		for(int edge = Warm + half - Warm % half; edge + Around < Samples; edge += half)
		{
			const int at = edge + Engine::HitDelay + _late;
			require(peakBetween(y, at - Around / 2, at + Around / 2) > 1e-3, "an edge's window holds nothing");
			worst = std::max(worst, highShare(y, at, SampleRate, Around));
		}
		return worst;
	}

	void q12(const Library& _library)
	{
		struct Case
		{
			const char* name;
			LfoTarget target;
			std::function<void(Engine&)> prepare;
		};
		const std::array<Case, 4> required{{
			{"level", LfoTarget::Level, {}},
			{"pan", LfoTarget::Pan, {}},
			{"frequency, Syntakt's filter", LfoTarget::Frequency, [](Engine& _e) { _e.setChain(0, filterOf(FilterType::Syntakt, 40)); }},
			{"frequency, MD's filter", LfoTarget::Frequency, [](Engine& _e) { _e.setChain(0, filterOf(FilterType::Machinedrum, 30)); }},
		}};
		double worst = -300;
		for(const auto& tone : Tones)
			for(const auto& c : required)
				for(const int division : {3, 4, 5})
				{
					const double hf = edgesHf(_library, tone, division, [&](Engine& _e)
					{
						if(c.prepare)
							c.prepare(_e);
						_e.setLfo(0, 0, aimed(c.target, 127, LfoShape::Square, division));
					});
					std::printf("  Q12, %s, %s, square of division %d, full depth: HF %.1f dB at its edges\n", tone.name, c.name, division, hf);
					worst = std::max(worst, hf);
				}
		std::printf("  Q12: a square at full depth: HF %.1f dB at worst (threshold -50)\n", worst);
		require(worst <= -50, "Q12 on a square LFO");
	}

	// The other targets' edges: a square at full depth on a value at 0, against the same target held still at each of its ends:
	// what the edge adds to what the stage does on its own is the click. DIST, SRR and the Syntakt drive add nothing, their HF is
	// their own at 127; BASE/WIDTH, whose poles jump at the block, -49 against -84. Bounded at what is measured, plus 1 dB
	void otherClicks(const Library& _library)
	{
		using Set = std::function<void(Engine&, int)>;
		const auto chain = [](const std::function<void(ChainSettings&, int)>& _set) -> Set
		{
			return [_set](Engine& _e, const int _value)
			{
				ChainSettings s;
				_set(s, _value);
				_e.setChain(0, s);
			};
		};
		struct Case
		{
			const char* name;
			LfoTarget target;
			Set set;
			std::array<double, 2> bound;
		};
		const std::array<Case, 8> list{{
			{"Syntakt drive", LfoTarget::DriveA, chain([](ChainSettings& _s, const int _v) { _s.drive[0] = {DriveType::Syntakt, false, _v, 127}; }), {-48.4, -40.6}},
			{"MD DIST", LfoTarget::DriveA, chain([](ChainSettings& _s, const int _v) { _s.drive[0] = {DriveType::MdDist, false, _v, 127}; }), {-31.0, -25.6}},
			{"MD SRR", LfoTarget::DriveA, chain([](ChainSettings& _s, const int _v) { _s.drive[0] = {DriveType::MdSrr, false, _v, 127}; }), {-17.2, -17.8}},
			{"three bands, low", LfoTarget::BandA, chain([](ChainSettings& _s, const int _v) { _s.band.type = BandType::ThreeBand; _s.band.values = {60, _v, 70, 64, 100, 64}; }), {-62.7, -61.6}},
			{"three bands, middle", LfoTarget::BandB, chain([](ChainSettings& _s, const int _v) { _s.band.type = BandType::ThreeBand; _s.band.values = {60, 64, 70, _v, 100, 64}; }), {-74.0, -75.4}},
			{"MD EQ gain", LfoTarget::BandB, chain([](ChainSettings& _s, const int _v) { _s.band.type = BandType::MdEq; _s.band.values = {40, _v}; }), {-71.1, -87.3}},
			{"BASE/WIDTH", LfoTarget::BandB, chain([](ChainSettings& _s, const int _v) { _s.band.type = BandType::BaseWidth; _s.band.values = {0, _v}; }), {-48.0, -48.7}},
			{"Syntakt resonance", LfoTarget::Resonance, chain([](ChainSettings& _s, const int _v) { _s = filterOf(FilterType::Syntakt, 60); _s.filter.resonance = _v; }), {-100.6, -92.0}},
		}};
		for(const auto& c : list)
		{
			std::printf("  %s:", c.name);
			for(size_t t = 0; t < Tones.size(); ++t)
			{
				constexpr int Division = 4;
				const double edge = edgesHf(_library, Tones[t], Division, [&](Engine& _e)
				{
					c.set(_e, 0);
					_e.setLfo(0, 0, aimed(c.target, 127, LfoShape::Square, Division));
				});
				const double low = edgesHf(_library, Tones[t], Division, [&](Engine& _e) { c.set(_e, 0); });
				const double high = edgesHf(_library, Tones[t], Division, [&](Engine& _e) { c.set(_e, 127); });
				std::printf(" %s %.1f at its edges, held %.1f and %.1f,", Tones[t].name, edge, low, high);
				require(edge <= c.bound[t], std::string(c.name) + ": the edge's HF");
			}
			std::printf(" dB\n");
		}
	}

	void allocations(const Library& _library)
	{
		auto e = std::make_unique<Engine>(_library);
		for(int t = 0; t < Tracks; ++t)
		{
			e->setMachine(t, t % 2 ? Machine{Family::Syntakt, static_cast<uint8_t>(t / 2)} : Machine{Family::Machinedrum, 2});
			for(int i = 0; i < 2; ++i)
				e->setLfo(t, i, aimed(static_cast<LfoTarget>(1 + (t * 3 + i * 7) % (static_cast<int>(LfoTarget::Count) - 1)), 90, LfoShape::Random, 0,
					static_cast<LfoMode>(t % 5), (t + i) % Tracks));
		}
		std::mt19937 random(5);
		Take take(128 * 2000);
		double position = 0;
		const auto allocated = countAllocations([&]
		{
			for(int b = 0; b < 2000; ++b)
			{
				e->setTempo(60 + b % 100);
				e->setPosition(position += 0.01);
				e->noteOn(static_cast<int>(random() % Tracks), 100);
				take.render(*e, 128);
			}
		});
		require(allocated == 0, std::to_string(allocated) + " allocations on the audio path");
		std::printf("  2000 blocks of 32 LFOs on every target, Hits and a moving transport: nothing allocated\n");
	}

	// ---- what the reviews found ----

	// A held voice is not over because its filter is closed; one that its Track's amp ended is
	void closedFilter(const Library& _library)
	{
		constexpr int Quarter = 22050, Samples = 5 * Quarter;
		const auto opens = [&](const std::function<void(Engine&, Take&)>& _run)
		{
			auto e = std::make_unique<Engine>(_library);
			setTone(*e, 0, Tones[1]);
			e->setChain(0, filterOf(FilterType::Machinedrum, 30));
			e->setTempo(Bpm);
			Take take(Samples);
			e->noteOn(0, 127);
			_run(*e, take);
			std::printf("  its peak where the filter reopens: %.3f, of %.3f held\n", peakBetween(take.channel(0), 16000, Samples), peakBetween(take.channel(0), 8000, 16000));
			const double before = peakBetween(take.channel(0), 8000, 16000), after = peakBetween(take.channel(0), Samples - 8000, Samples);
			return after / before;
		};
		const double square = opens([&](Engine& _e, Take& _take)
		{
			// 127 - 97 is the 30 that lets the tone through, 127 closes on it for 2 quarters, 1 s: past the 341 ms of SyntaktQuietBlocks
			_e.setChain(0, filterOf(FilterType::Machinedrum, 127));
			_e.setLfo(0, 0, aimed(LfoTarget::Frequency, -97, LfoShape::Square, 6));
			_e.setPosition(4 - 3.0);
			_take.render(_e, Samples);
		});
		const double automation = opens([&](Engine& _e, Take& _take)
		{
			_take.render(_e, 2 * Quarter);
			_e.setChain(0, filterOf(FilterType::Machinedrum, 127));
			_take.render(_e, 2 * Quarter);
			_e.setChain(0, filterOf(FilterType::Machinedrum, 30));
			_take.render(_e, Samples - 4 * Quarter);
		});
		std::printf("  a held voice past 1 s of closed filter: back at %.3f by a square, %.3f by automation\n", square, automation);
		require(square > 0.5, "a square's closed filter ended a held voice");
		require(automation > 0.5, "an automated closed filter ended a held voice");

		// ... nor under an amp that holds it
		const double underAmp = opens([&](Engine& _e, Take& _take)
		{
			auto held = filterOf(FilterType::Machinedrum, 127);
			held.amp = syntaktChain(true).amp;
			_e.setChain(0, held);
			_e.setLfo(0, 0, aimed(LfoTarget::Frequency, -97, LfoShape::Square, 6));
			_e.setPosition(4 - 3.0);
			_take.render(_e, Samples);
		});
		require(underAmp > 0.5, "a closed filter under an amp ended a held voice");

		// The amp: the voice under it goes on, its Track is over
		auto e = std::make_unique<Engine>(_library);
		setTone(*e, 0, Tones[1]);
		auto chain = syntaktChain();
		chain.amp.on = true;
		chain.amp.hold = 0;
		chain.amp.decay = 10;
		e->setChain(0, chain);
		Take take(4 * Quarter);
		e->noteOn(0, 127);
		take.render(*e, 4 * Quarter);
		require(!(e->liveTracks() & 1), "a voice its amp ended stayed");
	}

	// Machine moves left behind in a voice the Track came back to
	void strandedMoves(const Library& _library)
	{
		constexpr int Length = 12000;
		const Machine md{Family::Machinedrum, 17};
		bool stranded = false;
		for(const Machine away : {Machine{Family::Syntakt, 1}, Machine{}})
		{
			const auto journey = [&](const bool _lfo)
			{
				auto e = std::make_unique<Engine>(_library);
				e->setMachine(0, md);
				if(_lfo)
					e->setLfo(0, 0, aimed(LfoTarget::Machine1, 30, LfoShape::Square, LfoDivisions - 1, LfoMode::Trig));
				Take take(3 * Length);
				e->noteOn(0, 127);
				take.render(*e, Length);
				e->setMachine(0, away);
				e->setLfo(0, 0, LfoSettings{});
				take.render(*e, Length);
				e->setMachine(0, md);
				e->noteOn(0, 127);
				take.render(*e, Length);
				return std::vector<float>(take.channel(0).begin() + 2 * Length + 400, take.channel(0).end());
			};
			if(journey(true) != journey(false))
			{
				std::printf("  a Machine1 move stayed through %s\n", away.family == Family::Off ? "Off" : "the Syntakt");
				stranded = true;
			}
		}
		require(!stranded, "a machine's move left in its voice");
		std::printf("  a machine's move ended away from its family leaves nothing in its voice\n");
	}

	// The slope is the target's, whatever moves it
	void slopes(const Library& _library)
	{
		const auto square = [](const LfoTarget _target, const int _track = -1) { return aimed(_target, 127, LfoShape::Square, 3, LfoMode::Free, _track); };
		const auto chainOf = [](Engine& _e) { _e.setChain(0, filterOf(FilterType::Machinedrum, 30)); };
		double worst = -300;
		const auto bound = [&](const char* _name, const double _hf)
		{
			std::printf("  Q12, %s: HF %.1f dB\n", _name, _hf);
			worst = std::max(worst, _hf);
		};
		for(const auto& tone : Tones)
		{
			bound("two squares in phase on level", edgesHf(_library, tone, 3, [&](Engine& _e)
			{
				_e.setLevel(0, 64);
				_e.setLfo(0, 0, square(LfoTarget::Level));
				_e.setLfo(0, 1, square(LfoTarget::Level));
			}));
			bound("two squares in phase on the MD filter", edgesHf(_library, tone, 3, [&](Engine& _e)
			{
				chainOf(_e);
				_e.setLfo(0, 0, square(LfoTarget::Frequency));
				_e.setLfo(0, 1, square(LfoTarget::Frequency));
			}, 42 * BlockSize));
		}
		bound("a Syntakt LFO on an MD filter", edgesHf(_library, Tones[0], 3, [&](Engine& _e)
		{
			chainOf(_e);
			_e.setMachine(1, Tones[1].machine);
			_e.setLfo(1, 0, square(LfoTarget::Frequency, 0));
		}));
		bound("a Syntakt LFO on an MD Track's level", edgesHf(_library, Tones[0], 3, [&](Engine& _e)
		{
			_e.setMachine(1, Tones[1].machine);
			_e.setLfo(1, 0, square(LfoTarget::Level, 0));
		}));

		// Around a retarget: the level it leaves ramps back
		for(const auto& tone : Tones)
		{
			constexpr int Samples = 16384, At = 11000;
			auto e = std::make_unique<Engine>(_library);
			setTone(*e, 0, tone);
			e->setLevel(0, 64);
			e->setLfo(0, 0, aimed(LfoTarget::Level, 63, LfoShape::Square, LfoDivisions - 1, LfoMode::Trig));
			Take take(Samples);
			e->noteOn(0, 127);
			take.render(*e, At);
			e->setLfo(0, 0, aimed(LfoTarget::Level, 63, LfoShape::Square, LfoDivisions - 1, LfoMode::Trig, 1));
			take.render(*e, Samples - At);
			bound("around a retarget", highShare(take.channel(0), At + Engine::HitDelay, SampleRate));
		}
		// A TRIG LFO aimed at another Track restarts under a voice that goes on: there it keeps to its ramp
		{
			auto e = std::make_unique<Engine>(_library);
			setTone(*e, 1, Tones[0]);
			e->setOutput(1, 1);
			e->setLevel(1, 64);
			e->setTempo(Bpm);
			e->setMachine(0, Machine{Family::Machinedrum, 1});
			e->setLevel(0, 0);
			e->setLfo(0, 0, aimed(LfoTarget::Level, 63, LfoShape::Square, 6, LfoMode::Trig, 1));
			constexpr int Samples = 40000, Low = 44100 + 8000;		// past half a bar the square is low
			Take take(Low + Samples);
			e->noteOn(1, 127);
			take.render(*e, 44100);
			e->noteOn(1, 127);
			take.render(*e, 8000);
			e->noteOn(0, 127);
			take.render(*e, Samples);
			bound("a TRIG LFO's restart under another Track's voice", highShare(take.channel(2), Low + Engine::HitDelay, SampleRate));
		}
		std::printf("  Q12, the slope at its target: HF %.1f dB at worst (threshold -50)\n", worst);
		require(worst <= -50, "Q12 at the target");
	}

	// A Hit plays what is aimed at its Track where it stands, not where its ramp has got to
	void settled(const Library& _library)
	{
		constexpr int Samples = 6000, Base = 70, Depth = 30, HitAt = 128;
		const double before = 2 * BlockSize * QuartersPerSample;
		for(const auto& tone : Tones)
			for(const LfoTarget target : {LfoTarget::Level, LfoTarget::Frequency})
				for(const LfoMode mode : {LfoMode::Hold, LfoMode::Free})
				{
					const bool hold = mode == LfoMode::Hold;
					const auto hit = [&](const bool _lfo, const int _value)
					{
						return hitAt(_library, tone.machine, HitAt, [&](Engine& _e)
						{
							setTone(_e, 0, tone);
							_e.setChain(0, filterOf(FilterType::Syntakt, target == LfoTarget::Frequency ? _value : 90));
							_e.setLevel(0, target == LfoTarget::Level ? _value : 100);
							_e.setMachine(1, Machine{Family::Machinedrum, 2});
							if(_lfo)
								_e.setLfo(hold ? 0 : 1, 0, aimed(target, Depth, LfoShape::Square, 7, mode, 0));
							_e.setPosition(4 - before);		// a 2-bar square falls at 4 quarters, 2 blocks before the Hit
						}, Samples);
					};
					require(hit(true, Base) == hit(false, Base - Depth), std::string(tone.name) + ": a Hit under a ramp's middle");
				}
		std::printf("  a HOLD square and a FREE square of another Track, 2 blocks after the edge: the Hit of the held value\n");

		// The snap under a tail that goes on: what the Hit's step costs, on a ringing tone
		for(const auto& tone : Tones)
			for(const LfoTarget target : {LfoTarget::Level, LfoTarget::Frequency})
			{
				constexpr int Crest = 12000, Length = 16384;
				const auto retrigger = [&](const bool _lfo)
				{
					auto e = std::make_unique<Engine>(_library);
					setTone(*e, 0, tone);
					e->setChain(0, filterOf(FilterType::Syntakt, 60));
					e->setLevel(0, 90);
					e->setMachine(1, Machine{Family::Machinedrum, 2});
					e->setTempo(Bpm);
					if(_lfo)
						e->setLfo(1, 0, aimed(target, 30, LfoShape::Square, 7, LfoMode::Free, 0));
					e->setPosition(4 - (Crest + 64) * QuartersPerSample);
					Take take(Length);
					take.render(*e, 200);
					e->noteOn(0, 127);
					take.render(*e, Crest + 64 + 2 * BlockSize - 200);
					e->noteOn(0, 127);
					take.render(*e, Length - take.at());
					return highShare(take.channel(0), Crest + 64 + 2 * BlockSize + Engine::HitDelay, SampleRate);
				};
				std::printf("  a Hit 2 blocks into a square's fall, %s, %s: HF %.1f dB, %.1f without the LFO\n", tone.name, target == LfoTarget::Level ? "level" : "frequency",
					retrigger(true), retrigger(false));
			}
	}

	// The envelope runs under a neutral depth, so that an LFO's rise finds it where it stands
	void envelopeUnderARise()
	{
		constexpr int Blocks = 400, Rise = 150;
		ChainSettings s;
		s.filter.type = FilterType::Syntakt;
		s.filter.mode = 0;
		s.filter.frequency = 40;
		s.filter.envelope.decay = 70;
		s.filter.envelope.sustain = 40;
		const auto run = [&](const int _from)
		{
			auto chain = std::make_unique<Chain>(48000, true);
			chain->configure(0, s);
			auto block = std::make_unique<Block>();
			std::vector<double> y;
			chain->hit(0, 127);
			for(int b = 0; b < Blocks; ++b)
			{
				if(b == _from)
					chain->modulate(0, VelocityTarget::EnvelopeDepth, 40 << 7);
				for(int n = 0; n < BlockSize; ++n)
					block->s[n][0] = static_cast<float>(0.5 * std::sin(2 * Pi * 1500 * (b * BlockSize + n) / 48000.0));
				chain->process(*block, 1);
				for(int n = 0; n < BlockSize; ++n)
					y.push_back(block->s[n][0]);
			}
			return y;
		};
		// Moved from the Hit, the envelope never stood at a neutral depth: a rise at block Rise must join it
		const auto always = run(0), risen = run(Rise);
		double power = 0, apart = 0;
		for(size_t n = (Rise + 100) * BlockSize; n < always.size(); ++n)
		{
			power += always[n] * always[n];
			apart += (always[n] - risen[n]) * (always[n] - risen[n]);
		}
		const double dB = 10 * std::log10(apart / power + 1e-30);
		std::printf("  an envelope at a rise after %d blocks of neutral depth: %.1f dB from one under a depth all along\n", Rise, dB);
		require(dB < -60, "the envelope stood still under a neutral depth");
	}

	void lockValues(const Library& _library)
	{
		constexpr int First = 3200, Samples = 8000;
		bool wrong = false;
		for(const Machine m : {Machine{Family::Machinedrum, 17}, Machine{Family::Syntakt, 1}})
		{
			// A Hit's lock B, the knob moved between lock and Hit
			const auto second = [&](const bool _lockFirst)
			{
				auto e = std::make_unique<Engine>(_library);
				e->setMachine(0, m);
				Take take(Samples);
				e->lock(0, 0, 20);
				e->noteOn(0, 127);
				take.render(*e, First);
				if(_lockFirst)
				{
					e->lock(0, 0, 90);
					e->setParameter(0, 0, 40);
				}
				else
				{
					e->setParameter(0, 0, 40);
					e->lock(0, 0, 90);
				}
				e->noteOn(0, 127);
				take.render(*e, Samples - First);
				return take.channel(0);
			};
			if(second(true) != second(false))
			{
				std::printf("  machine %d: the knob took a Hit's lock\n", m.id);
				wrong = true;
			}

			// A lock with no Hit is dropped: what plays stays the last Hit's
			const auto dropped = [&](const bool _lock)
			{
				auto e = std::make_unique<Engine>(_library);
				e->setMachine(0, m);
				e->setLfo(0, 0, aimed(LfoTarget::Machine1, 10, LfoShape::Sine, 1));
				Take take(Samples);
				e->lock(0, 0, 50);
				e->noteOn(0, 127);
				take.render(*e, First);
				if(_lock)
					e->lock(0, 0, 90);
				take.render(*e, Samples - First);
				return take.channel(0);
			};
			if(dropped(true) != dropped(false))
			{
				std::printf("  machine %d: a lock without a Hit reached the voice\n", m.id);
				wrong = true;
			}
		}
		require(!wrong, "a lock's value");
		std::printf("  locks: a Hit's own under a knob, none without a Hit, both families\n");

		// A knob over a locked parameter stays under a move that comes after it
		constexpr int Depth = 10, Fall = 22050;		// a half note's square falls at a quarter note
		const auto tone = [&](const bool _knob)
		{
			auto e = std::make_unique<Engine>(_library);
			setTone(*e, 0, Tones[1]);
			e->setTempo(Bpm);
			e->setLfo(0, 0, aimed(LfoTarget::Machine1, Depth, LfoShape::Square, 5, LfoMode::Trig));
			Take take(2 * Fall + 8192);
			if(_knob)
				e->lock(0, 0, 40);
			e->noteOn(0, 127);
			take.render(*e, 4096);
			if(_knob)
				e->setParameter(0, 0, 64);
			take.render(*e, 2 * Fall + 8192 - 4096);
			return periodOf(take.channel(0), Fall + 8192, Fall + 16384);
		};
		const double knob = tone(true), plain = tone(false);
		std::printf("  a knob over a lock, under a square's fall: a period of %.2f samples, %.2f unlocked\n", knob, plain);
		require(std::abs(knob / plain - 1) < 0.01, "a move took the lock back from a knob");
	}

	// ONE and HALF stop where the Syntakt's stop, its tables read from section 3 as its routine runs: what stays here is a form
	bool stops()
	{
		namespace m = syntakt::main;
		const auto* path = std::getenv("SYNTAKT_SECTION3");
		if(!path)
			return false;
		const auto rom = m::loadSection3(path);
		if(rom.bytes.empty())
			return false;
		constexpr int Waves = 6;
		const std::array<const char*, Waves> names{"TRI", "SIN", "SQR", "SAW", "EXP", "RMP"};
		bool same = true;
		for(const LfoMode mode : {LfoMode::One, LfoMode::Half})
			for(int w = 0; w < Waves; ++w)
			{
				auto mem = std::make_unique<m::Memory>(rom);
				const uint32_t p = m::Memory::SramBase;
				const uint32_t base = p + 0x1c + 8;
				mem->setHalf(base - 6, static_cast<int16_t>(0x4000 + 16 * 256));
				mem->setByte(base - 4, 8);
				mem->setHalf(base - 2, 0x4000);
				mem->setByte(base, 0xff);
				mem->setByte(base + 2, static_cast<uint8_t>(w));
				mem->setByte(base + 6, static_cast<uint8_t>(mode == LfoMode::One ? 3 : 4));
				for(int b = 0; b < 3000; ++b)
					m::lfos(*mem, p, static_cast<int32_t>(Bpm * 120), b == 0 ? 1 : 0);
				const double firmware = mem->word(m::LfoState - m::LfoStride - 0x20) / 2147483648.0;

				LfoSettings s;
				s.rate.sync = false;
				s.shape = static_cast<LfoShape>(w);
				s.mode = mode;
				Lfo lfo;
				lfo.set(s, 0);
				lfo.trig(0);
				const double here = after(lfo, 0, 0, 2.0);
				const bool match = std::abs(here - firmware) < 0.01;
				std::printf("  %s stops, %s: here %.4f, the Syntakt %.4f%s\n", mode == LfoMode::One ? "ONE" : "HALF", names[static_cast<size_t>(w)], here, firmware, match ? "" : "  DIFFERENT");
				same = same && match;
			}
		require(same, "ONE or HALF stops elsewhere than the Syntakt");
		return true;
	}
}

int main()
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	const auto flash = Library::findFlashImage({});
	if(flash.empty())
	{
		std::printf("SKIP: set GEARMULATOR_MD_FIRMWARE_BIN to the Machinedrum UW flash image\n");
		return 77;
	}
	int failed = 0;
	const auto run = [&](const std::function<void()>& _section)
	{
		try
		{
			_section();
		}
		catch(const std::exception& e)
		{
			std::printf("FAIL: %s\n", e.what());
			++failed;
		}
	};

	run(shapes);
	run(divisions);
	run(syntaktSpeeds);
	run(machinedrumSpeeds);
	run(modes);
	run(routing);
	run(envelopeUnderARise);

	const Library library(flash);
	for(const auto section : {blockSizes, samePhase, tempoFollowed, clocks, restarts, acrossTracks, targets, interplay, bounds, ticks, depthZero,
		trigHits, otherTracks, q12, otherClicks, bandsWhileSounding, allocations, closedFilter, strandedMoves, slopes, settled, lockValues})
		run([&] { section(library); });

	bool section3 = false;
	run([&] { section3 = stops(); });
	if(failed)
	{
		std::printf("FAILED: %d sections\n", failed);
		return 1;
	}
	if(!section3)
	{
		std::printf("SKIP: set SYNTAKT_SECTION3 to compare where ONE and HALF stop\n");
		return 77;
	}
	std::printf("PASS: two LFOs a Track, on the transport, on any Track\n");
	return 0;
}
