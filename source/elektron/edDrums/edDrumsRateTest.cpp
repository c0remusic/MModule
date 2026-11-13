// The engine at the host's rate (ticket 16 of .scratch/elektron-drums-v1):
//
//   - 44.1 kHz unchanged: a scene of every family, the sends, the master, LFOs, chains, Chokes and Locks renders the
//     bytes it rendered before the rates came (Golden, its FNV-1a); the same scene at every rate, finite and sounding;
//   - the FIRs: gain within 0.01 dB from 20 Hz to 18 kHz, what lies past 23.6 kHz down by 60 dB or more (Q11's
//     thresholds, tighter);
//   - Q15 (research/19-qualite-audio.md): a Hit at 44.1, 48 and 96 kHz, 88.2 and 192 too, against its family's own rate,
//     both brought to 96 kHz by an exact sinc and low-passed at 18 kHz: envelope (1 ms RMS, above -40 dB) within 0.2 dB,
//     third octaves to 16 kHz within 0.5 dB, and where it sounds within 0.1 of a host sample; a Syntakt Hit at 44.1 kHz
//     goes through Resample48To44, unchanged, within the bound its stFast test gives each machine;
//   - pitch: each Hit's spectral peak within 1 cent of its family's rate, a sine on the Sampler's two grains included;
//   - HitDelay(rate): every machine at notes all over the cycles of the rates, the same samples, jitter 0;
//   - the sends, the master, the LFOs and the sequencer at the right speed: their output against 44.1 kHz's;
//   - the cost: the worst kit of 16 Tracks (the dearest machine of every family but the Syntakt's on 7 Tracks beside the
//     nine Syntakt machines, and on all 16), every send, the master and 2 LFOs a Track on, under 15 % of a core at 44.1,
//     48, 88.2 and 96 kHz (spec 12, criterion 4), under 17 % with the drives at 4x (the user's decision of 2026-11-13),
//     and the 2x half-band path the ticket weighed, measured beside it.
//
// GEARMULATOR_MD_FIRMWARE_BIN names the flash image; exit 77 without it. SP TWINSHOT plays noise made here.

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include "edDrumsEngine.h"
#include "edDrumsOversampler.h"
#include "edDrumsRate.h"
#include "edDrumsSamples.h"
#include "edDrumsSequencer.h"
#include "edDrumsTestSupport.h"
#include "edDrumsVintage.h"
#include "stFastMetrics.h"

namespace
{
	using namespace elektron::drums;
	using namespace elektron::drums::test;
	using elektron::BlockSize;
	using syntakt::fast::bench::bands;
	using syntakt::fast::bench::db;
	using syntakt::fast::bench::envelope;
	using syntakt::fast::bench::envelopeDiff;
	using syntakt::fast::bench::peakHz;
	using syntakt::fast::bench::Pi;
	using Clock = std::chrono::steady_clock;

	constexpr std::array<double, 5> Rates{44100, 48000, 88200, 96000, 192000};
	constexpr double Grid = 96000;			// where two rates' renders are compared
	constexpr double Onset = 4.0 / 300;		// where a Hit's first sample sounds, in seconds: a whole sample at every rate
	constexpr double EnvelopeDb = 0.2, SpectrumDb = 0.5, LagSamples = 0.1, Cents = 1;
	// Main through the master or the LFOs, 10 ms envelope and third octaves: the master's detector reads the peaks between
	// 44.1 kHz's samples too, which moves its gain by a few tenths of a dB where it compresses
	constexpr double MainDb = 0.5;

	void require(const bool _condition, const std::string& _message)
	{
		if(!_condition)
			throw std::runtime_error(_message);
	}

	int samplesAt(const double _seconds, const double _rate)
	{
		return static_cast<int>(std::lround(_seconds * _rate));
	}

	std::string khz(const double _rate)
	{
		char s[16];
		std::snprintf(s, sizeof(s), "%g kHz", _rate / 1000);
		return s;
	}

	std::unique_ptr<syntakt::fast::TwinshotSamples> noise()
	{
		auto s = std::make_unique<syntakt::fast::TwinshotSamples>();
		constexpr uint32_t Length = 24000;
		s->base = 0x100000;
		s->data.resize(8 * Length);
		std::mt19937 rng(1);
		for(auto& x : s->data)
			x = static_cast<int16_t>(static_cast<int32_t>(rng() % 40000) - 20000);
		for(uint32_t i = 0; i < 64; ++i)
			s->slots[i] = {Length, s->base + 2 * (i % 8) * Length};
		return s;
	}

	// A sine of _hz at 44.1 kHz, _seconds long, for the Sampler
	Sample sine(const double _hz, const double _seconds = 0.5)
	{
		std::vector<float> mono(static_cast<size_t>(_seconds * 44100));
		for(size_t n = 0; n < mono.size(); ++n)
			mono[n] = 0.5f * static_cast<float>(std::sin(2 * Pi * _hz * static_cast<double>(n) / 44100));
		auto made = makeSample("sine", mono, 44100);
		require(std::holds_alternative<Sample>(made), "a sine makes a Sample");
		return std::get<Sample>(std::move(made));
	}

	// ---- measuring across rates ----

	// _x at _from brought to _to for measuring, never for listening: a sinc at 21.5 kHz under a Kaiser window (beta 10) of
	// 1024 taps at _from, in exact phases, the input 0 outside (as stFastBench's to48k, for any two rates). Output n stands
	// for input time n _from / _to
	std::vector<double> toGrid(const std::vector<double>& _x, const double _from, const double _to = Grid)
	{
		if(_from == _to)
			return _x;
		constexpr int Half = 512;
		const auto from = static_cast<int64_t>(_from), to = static_cast<int64_t>(_to);
		const int64_t g = std::gcd(from, to), up = to / g, down = from / g;
		static std::map<std::pair<int64_t, int64_t>, std::vector<double>> tables;
		auto& h = tables[{from, to}];
		if(h.empty())
		{
			const auto i0 = [](const double _v)
			{
				double sum = 1, term = 1;
				for(int k = 1; k < 60; ++k)
				{
					term *= (_v / (2 * k)) * (_v / (2 * k));
					sum += term;
				}
				return sum;
			};
			const double fc = 21500 / _from, beta = 10;
			h.resize(static_cast<size_t>(up * 2 * Half));
			for(int64_t p = 0; p < up; ++p)
				for(int k = -Half + 1; k <= Half; ++k)
				{
					const double x = k - static_cast<double>(p) / static_cast<double>(up);
					const double sinc = x == 0 ? 2 * fc : std::sin(2 * Pi * fc * x) / (Pi * x);
					const double r = x / Half;
					h[static_cast<size_t>(p * 2 * Half + k + Half - 1)] = sinc * i0(beta * std::sqrt(std::max(0.0, 1 - r * r))) / i0(beta);
				}
		}
		const auto n = static_cast<int64_t>(_x.size());
		std::vector<double> y(static_cast<size_t>(n * up / down));
		for(int64_t m = 0; m < static_cast<int64_t>(y.size()); ++m)
		{
			const int64_t at = m * down / up;
			const auto p = m * down % up;
			const double* c = &h[static_cast<size_t>(p * 2 * Half)];
			const int64_t first = std::max<int64_t>(-Half + 1, -at), last = std::min<int64_t>(Half, n - 1 - at);
			double s = 0;
			for(int64_t k = first; k <= last; ++k)
				s += c[k + Half - 1] * _x[static_cast<size_t>(at + k)];
			y[static_cast<size_t>(m)] = s;
		}
		return y;
	}

	// _x low-passed at 18 kHz, the band a 44.1 kHz signal holds (stFastBench's lowPass18k): a zero-phase sinc at 18 kHz
	// under a Kaiser window (beta 8) 2 ms long, flat within 0.01 dB to about 16.5 kHz and -80 dB from about 19.5 kHz
	std::vector<double> lowPass18k(const std::vector<double>& _x, const double _fs)
	{
		constexpr double Cutoff = 18000, Beta = 8, Seconds = 0.002;
		const int half = static_cast<int>(std::lround(Seconds * _fs / 2));
		const auto i0 = [](const double _v)
		{
			double sum = 1, term = 1;
			for(int k = 1; k < 60; ++k)
			{
				term *= (_v / (2 * k)) * (_v / (2 * k));
				sum += term;
			}
			return sum;
		};
		std::vector<double> h(static_cast<size_t>(2 * half + 1));
		for(int j = -half; j <= half; ++j)
		{
			const double fc = Cutoff / _fs;
			const double sinc = j == 0 ? 2 * fc : std::sin(2 * Pi * fc * j) / (Pi * j);
			const double r = static_cast<double>(j) / half;
			h[static_cast<size_t>(j + half)] = sinc * i0(Beta * std::sqrt(std::max(0.0, 1 - r * r))) / i0(Beta);
		}
		std::vector<double> y(_x.size());
		const auto n = static_cast<int>(_x.size());
		for(int i = 0; i < n; ++i)
		{
			double s = 0;
			for(int j = std::max(-half, i - n + 1); j <= std::min(half, i); ++j)
				s += h[static_cast<size_t>(j + half)] * _x[static_cast<size_t>(i - j)];
			y[static_cast<size_t>(i)] = s;
		}
		return y;
	}

	// How much later _b sounds than _a, in grid samples: the delay of _b, read between its samples by a windowed sinc,
	// that leaves the least of _a over the first 30 ms of the Hit. A cross-correlation's peak would not do: on a low tone
	// it is so flat that the window's edge moves it by tenths of a sample
	double lagOf(const std::vector<double>& _a, const std::vector<double>& _b)
	{
		constexpr int Half = 32;
		const auto from = static_cast<int64_t>((Onset - 0.001) * Grid);
		const auto to = std::min<int64_t>(static_cast<int64_t>(_a.size()) - 2 * Half, static_cast<int64_t>((Onset + 0.03) * Grid));
		const auto residual = [&](const double _tau)
		{
			// The taps depend on _tau's fraction alone
			const auto whole = static_cast<int64_t>(std::floor(_tau));
			const double frac = _tau - static_cast<double>(whole);
			std::array<double, 2 * Half> h{};
			for(int j = -Half + 1; j <= Half; ++j)
			{
				const double u = frac - j;
				const double w = 0.5 + 0.5 * std::cos(Pi * u / Half);
				h[static_cast<size_t>(j + Half - 1)] = (u == 0 ? 1 : std::sin(Pi * u) / (Pi * u)) * w;
			}
			double r = 0;
			for(int64_t i = from; i < to; ++i)
			{
				double v = 0;
				for(int j = -Half + 1; j <= Half; ++j)
					v += _b[static_cast<size_t>(i + whole + j)] * h[static_cast<size_t>(j + Half - 1)];
				r += (_a[static_cast<size_t>(i)] - v) * (_a[static_cast<size_t>(i)] - v);
			}
			return r;
		};
		double best = 0, least = residual(0);
		for(double tau = -4; tau <= 4; tau += 0.25)
			if(const double r = residual(tau); r < least)
			{
				least = r;
				best = tau;
			}
		// Golden section around it
		double lo = best - 0.25, hi = best + 0.25;
		constexpr double G = 0.6180339887498949;
		double c = hi - G * (hi - lo), d = lo + G * (hi - lo), rc = residual(c), rd = residual(d);
		while(hi - lo > 1e-4)
		{
			if(rc < rd)
			{
				hi = d;
				d = c;
				rd = rc;
				c = hi - G * (hi - lo);
				rc = residual(c);
			}
			else
			{
				lo = c;
				c = d;
				rc = rd;
				d = lo + G * (hi - lo);
				rd = residual(d);
			}
		}
		return (lo + hi) / 2;
	}

	struct Q15
	{
		double envelope = 0, envelope10 = 0, spectrum = 0, lag = 0;		// lag in the test's host samples
	};

	// _test against _ref, each at its rate, on the 96 kHz grid, low-passed at 18 kHz
	Q15 compare(const std::vector<double>& _ref, const double _refRate, const std::vector<double>& _test, const double _testRate)
	{
		auto a = lowPass18k(toGrid(_ref, _refRate), Grid);
		auto b = lowPass18k(toGrid(_test, _testRate), Grid);
		const size_t n = std::min(a.size(), b.size()) - static_cast<size_t>(Grid * 0.002);	// not the filters' padded ends
		a.resize(n);
		b.resize(n);
		Q15 q;
		q.envelope = envelopeDiff(envelope(a, Grid), envelope(b, Grid));
		q.envelope10 = envelopeDiff(envelope(a, Grid, 10), envelope(b, Grid, 10));
		const auto ra = bands(a, Grid, -17, 12), rb = bands(b, Grid, -17, 12);
		const double loud = *std::max_element(ra.begin(), ra.end());
		for(size_t k = 0; k < ra.size(); ++k)
			if(ra[k] >= loud * 1e-6)
				q.spectrum = std::max(q.spectrum, std::abs(db(rb[k] / ra[k])));
		q.lag = lagOf(a, b) * _testRate / Grid;
		return q;
	}

	// ---- a Hit on Track 0 ----

	struct Voice
	{
		std::string name;
		Machine machine;
		const Sample* sample = nullptr;
		int grain = -1;			// the Sampler's Machine 9, -1 its default
		bool tuned = false;		// its pitch a note's
	};

	double familyRate(const Voice& _v)
	{
		return _v.machine.family == Family::Syntakt ? syntakt::fast::MachineRate : 44100.0;
	}

	std::unique_ptr<Engine> engineFor(const Library& _library, const Voice& _v, const double _rate)
	{
		auto e = std::make_unique<Engine>(_library, _rate);
		e->setMachine(0, _v.machine);
		e->setLevel(0, 127);
		if(_v.sample)
			e->setSample(0, _v.sample);
		if(_v.grain >= 0)
			e->setParameter(0, vintage::GrainSlot, static_cast<uint8_t>(_v.grain));
		return e;
	}

	// Main's left, _samples long, the Hit at _note (-1: where its first sample sounds at Onset)
	std::vector<double> hit(const Library& _library, const Voice& _v, const double _rate, const int _samples, int _note = -1)
	{
		auto e = engineFor(_library, _v, _rate);
		if(_note < 0)
			_note = samplesAt(Onset, _rate) - e->hitDelay();
		std::vector<float> left(static_cast<size_t>(_samples)), right(left.size());
		std::array<float*, Channels> p{};
		p[0] = left.data();
		p[1] = right.data();
		e->render(p.data(), _note);
		e->noteOn(0, 127);
		p[0] += _note;
		p[1] += _note;
		e->render(p.data(), _samples - _note);
		return {left.begin(), left.end()};
	}

	std::vector<Voice> voices(const Sample& _sine)
	{
		std::vector<Voice> v;
		for(int id = 0; id < SyntaktMachines; ++id)
			v.push_back({syntaktMachineNames[static_cast<size_t>(id)], {Family::Syntakt, static_cast<uint8_t>(id)}, nullptr, -1, id == 0 || id == 6});
		std::vector<Machine> md;
		for(const auto& m : machines())
			if(m.family == Family::Machinedrum)
				md.push_back(m);
		for(size_t i = 0; i < md.size(); i += md.size() / 12)
			v.push_back({nameOf(md[i]), md[i]});
		for(int id = 0; id < OpenDrumMachines; ++id)
			v.push_back({"OPEN DRUM " + std::to_string(id), {Family::OpenDrum, static_cast<uint8_t>(id)}, nullptr, -1, id == 0});
		v.push_back({"SOPHIE", {Family::Sophie, 0}, nullptr, -1, true});
		v.push_back({"Sampler, ROM grain, 440 Hz", Sampler, &_sine, 0, true});
		v.push_back({"Sampler, VINTAGE, 440 Hz", Sampler, &_sine, 100, true});
		return v;
	}

	// ---- the scene ----

	// Every family on the 16 Tracks, the sends and the master on, LFOs, chains, a Choke, Locks; notes and their ends at
	// samples of no block's edge, in blocks of the host's of every size
	struct Scene
	{
		static constexpr int Seconds = 3;

		Scene() : sample(sine(440)) {}

		void setUp(Engine& _e) const
		{
			const auto md = machines();
			std::vector<Machine> mdOnly;
			for(const auto& m : md)
				if(m.family == Family::Machinedrum)
					mdOnly.push_back(m);
			for(int t = 0; t < 9; ++t)
				_e.setMachine(t, {Family::Syntakt, static_cast<uint8_t>(t)});
			_e.setMachine(9, mdOnly[0]);
			_e.setMachine(10, mdOnly[mdOnly.size() / 3]);
			_e.setMachine(11, mdOnly[2 * mdOnly.size() / 3]);
			_e.setMachine(12, {Family::OpenDrum, 1});
			_e.setMachine(13, {Family::Sophie, 0});
			_e.setMachine(14, Sampler);
			_e.setMachine(15, Sampler);
			_e.setSample(14, &sample);
			_e.setSample(15, &sample);
			_e.setParameter(15, vintage::GrainSlot, 100);		// VINTAGE
			for(int t = 0; t < Tracks; ++t)
			{
				_e.setLevel(t, 90 + t);
				_e.setPan(t, 8 * t);
				_e.setOutput(t, t % 3 == 0 ? 0 : t % 5);
				_e.setSend(t, Send::Delay, 20 + 6 * t);
				_e.setSend(t, Send::Reverb, 100 - 5 * t);
				_e.setSend(t, Send::Chorus, t % 2 ? 60 : 0);
				_e.setSend(t, Send::Phaser, t % 3 ? 0 : 70);
				_e.setNote(t, 48 + t);
			}
			ChainSettings heavy;
			heavy.drive[0] = {DriveType::Syntakt, false, 70, 127};
			heavy.filter.type = FilterType::Machinedrum;
			heavy.filter.frequency = 80;
			heavy.filter.resonance = 50;
			heavy.band.type = BandType::ThreeBand;
			heavy.band.values = {40, 80, 70, 50, 100, 70};
			_e.setChain(2, heavy);
			_e.setChain(10, heavy);
			LfoSettings lfo;
			lfo.rate.division = 3;
			lfo.depth = 60;
			lfo.shape = LfoShape::Sine;
			for(const auto& [track, target] : {std::pair<int, LfoTarget>{0, LfoTarget::Pan}, {4, LfoTarget::Level},
				{9, LfoTarget::Machine2}, {10, LfoTarget::Frequency}, {13, LfoTarget::Machine3}})
			{
				lfo.target = target;
				_e.setLfo(track, 0, lfo);
			}
			_e.setChokes(0, 1u << 1);
			auto& fx = _e.sends();
			fx.setEcho(md::fast::EchoTime, 24);
			fx.setEcho(md::fast::EchoFb, 60);
			fx.setEcho(md::fast::EchoLev, 127);
			fx.setGateBox(md::fast::GateDec, 64);
			fx.setGateBox(md::fast::GateLev, 127);
			fx.setChorus(ChorusLevel, 100);
			fx.setChorus(ChorusDepth, 80);
			fx.setPhaser(PhaserLevel, 100);
			fx.setOutput(Send::Chorus, 1);
			_e.master().set(MasterThreshold, 40);
			_e.master().set(MasterRatio, 80);
			_e.master().set(MasterDrive, 30);
			_e.setMasterOn(true);
			_e.setTempo(123.4);
		}

		// The events at their samples, in _rate's samples; Hits at a sixteenth of 123.4 BPM, each Track on its own
		void play(Engine& _e, const double _rate, std::vector<std::vector<float>>& _out) const
		{
			const auto total = static_cast<int64_t>(Seconds * _rate);
			for(auto& c : _out)
				c.assign(static_cast<size_t>(total), 0.0f);
			struct Event
			{
				int64_t at;
				int track, kind;	// 0 Hit, 1 its end, 2 a Lock then a Hit
			};
			std::vector<Event> events;
			const double sixteenth = 60.0 / 123.4 / 4;
			for(int s = 0; s < 4 * 6; ++s)
				for(int t = 0; t < Tracks; ++t)
					if((s + t) % (t < 9 ? 3 : 2) == 0)
					{
						const double at = (s + t / 17.0) * sixteenth;
						events.push_back({static_cast<int64_t>(at * _rate), t, s % 4 == 1 ? 2 : 0});
						events.push_back({static_cast<int64_t>((at + sixteenth / 2) * _rate), t, 1});
					}
			std::stable_sort(events.begin(), events.end(), [](const Event& _a, const Event& _b) { return _a.at < _b.at; });
			std::mt19937 rng(7);
			int64_t now = 0;
			size_t next = 0;
			std::array<float*, Channels> p{};
			while(now < total)
			{
				int64_t until = std::min<int64_t>(total, now + 1 + static_cast<int64_t>(rng() % 700));
				if(next < events.size())
					until = std::min(until, std::max(now, events[next].at));
				if(until > now)
				{
					for(size_t c = 0; c < Channels; ++c)
						p[c] = _out[c].data() + now;
					_e.render(p.data(), static_cast<int>(until - now));
					now = until;
				}
				for(; next < events.size() && events[next].at <= now; ++next)
				{
					const auto& ev = events[next];
					if(ev.kind == 1)
					{
						_e.noteOff(ev.track);
						continue;
					}
					if(ev.kind == 2)
						for(int i = 0; i < 4; ++i)
							_e.lock(ev.track, i, static_cast<uint8_t>(20 + 25 * i + ev.track));
					_e.noteOn(ev.track, 60 + 4 * ev.track, ev.track == 5);
				}
			}
		}

		Sample sample;
	};

	uint64_t fnv(const std::vector<std::vector<float>>& _channels)
	{
		uint64_t h = 1469598103934665603ull;
		for(const auto& c : _channels)
		{
			const auto* bytes = reinterpret_cast<const uint8_t*>(c.data());
			for(size_t i = 0; i < c.size() * sizeof(float); ++i)
				h = (h ^ bytes[i]) * 1099511628211ull;
		}
		return h;
	}

	// What the scene rendered at 44.1 kHz before ticket 16, measured on the engine of commit 3413ea64: a change meant at
	// 44.1 kHz gives this its new value, nothing else may. 2026-11-13: OPEN DRUM's REAL_FS from 44,140 to 44,100 (its
	// oscillators 1.57 cent flat before); 0xbfc5a39c2d1d1e3a until then
	constexpr uint64_t Golden = 0x43f3abf3f1df7256ull;

	void unchanged(const Library& _library, const char* _dump)
	{
		const Scene scene;
		for(const double rate : Rates)
		{
			auto e = std::make_unique<Engine>(_library, rate);
			scene.setUp(*e);
			std::vector<std::vector<float>> out(Channels);
			const auto t0 = Clock::now();
			scene.play(*e, rate, out);
			const double seconds = std::chrono::duration<double>(Clock::now() - t0).count();
			double peak = 0;
			bool finite = true;
			for(const auto& c : out)
				for(const float v : c)
				{
					finite = finite && std::isfinite(v);
					peak = std::max(peak, static_cast<double>(std::abs(v)));
				}
			require(finite, "the scene at " + khz(rate) + ": a value not finite");
			require(peak > 0.01 && peak < 4, "the scene at " + khz(rate) + " sounds, under 12 dB over full scale");
			if(rate != 44100)
			{
				std::printf("  the scene at %s: %d s, peak %.3f, rendered in %.2f s\n", khz(rate).c_str(), Scene::Seconds, peak,
					seconds);
				continue;
			}
			const uint64_t h = fnv(out);
			if(_dump)
			{
				std::ofstream f(_dump, std::ios::binary);
				for(const auto& c : out)
					f.write(reinterpret_cast<const char*>(c.data()), static_cast<std::streamsize>(c.size() * sizeof(float)));
			}
			std::printf("  the scene at 44.1 kHz: %d s on 32 channels, peak %.3f, FNV-1a %016llx (before ticket 16: %016llx)\n",
				Scene::Seconds, peak, static_cast<unsigned long long>(h), static_cast<unsigned long long>(Golden));
			require(h == Golden, "44.1 kHz renders the bytes it rendered before ticket 16");
		}
	}

	// ---- the FIRs ----

	struct Response
	{
		double pass = 0, stop = -300;	// the worst |dB| to 18 kHz, the most from _stop on
	};

	Response response(const RateFir& _fir, const double _from, const double _stop)
	{
		const int up = _fir.up(), taps = _fir.taps();
		const double fs = up * _from;
		const auto at = [&](const double _hz)
		{
			std::complex<double> s = 0;
			for(int p = 0; p < up; ++p)
				for(int k = 0; k < taps; ++k)
					s += static_cast<double>(_fir.row(p)[taps - 1 - k]) * std::polar(1.0, -2 * Pi * _hz * (p + up * k) / fs);
			return 20 * std::log10(std::abs(s) / up);
		};
		Response r;
		for(double hz = 20; hz <= 18000; hz *= 1.05)
			r.pass = std::max(r.pass, std::abs(at(hz)));
		r.pass = std::max(r.pass, std::abs(at(18000)));
		// The first sidelobes are the largest: past 100 kHz the window's lobes only fall
		for(double hz = _stop; hz <= std::min(fs / 2, 100000.0); hz += 211)
			r.stop = std::max(r.stop, at(hz));
		return r;
	}

	// Each FIR as the engine builds it: what lies past 18 kHz to the images, or the aliases, of the 20.5 kHz band, from
	// the source's rate - 20.5 kHz on (23.6 kHz from 44.1, 27.5 from 48), by 60 dB at least, Q11's
	void firs(const Library& _library)
	{
		std::printf("  FIR                         phases  taps  gain to 18 kHz  past the band's images\n");
		const auto check = [](const RateFir& _fir, const double _from, const double _to, const char* _what, const bool _stop)
		{
			// The images of a 48 kHz source's band, else of 44.1 kHz's: a ring's from 44.1, the sends' aliases on their way down
			const double from = _from == 48000 ? 48000.0 : 44100.0;
			const auto r = response(_fir, _from, from - RateFir::Cutoff);
			std::printf("  %-8s to %-8s %-7s %6d %5d %12.4f dB %8.1f dB from %.1f kHz%s\n", khz(_from).c_str(), khz(_to).c_str(),
				_what, _fir.up(), _fir.taps(), r.pass, r.stop, (from - RateFir::Cutoff) / 1000,
				_stop ? "" : " (its input holds nothing there)");
			require(r.pass <= 0.01 && (!_stop || r.stop <= -60), khz(_from) + " to " + khz(_to) + ": the FIR out of its bounds");
		};
		Sends sends(*_library.os, _library.trx.sine.data());
		for(const double rate : Rates)
		{
			if(rate == 44100)
				continue;
			check(RateFir(44100, rate), 44100, rate, "MD ring", true);
			// The Engine's: a half-band at a whole multiple of 48 kHz
			if(rate != 48000)
				check(RateFir(48000, rate, RateFir::Taps, std::lround(rate) % 48000 == 0 ? 24000 : RateFir::Cutoff), 48000, rate,
					"ST ring", true);
			// The sends' way down as SendsAtRate takes it: past 48 kHz what is sent is the rings' band alone
			const SendsAtRate bridge(sends, rate);
			check(bridge.downFir(), rate, 44100, "sends", rate == 48000);
		}
	}

	// ---- Q15 and pitch ----

	void q15(const Library& _library, const Sample& _sine)
	{
		constexpr double Seconds = Onset + 0.35;
		bool ok = true;
		double worstEnv = 0, worstSpec = 0, worstLag = 0, worstCents = 0;
		std::string failed;
		std::printf("  Q15 against the family's rate; envelope 1 ms and 10 ms, third octaves, where it sounds (host samples), pitch\n");
		for(const auto& v : voices(_sine))
		{
			const double own = familyRate(v);
			const auto ref = hit(_library, v, own, samplesAt(Seconds, own));
			std::printf("  %-28s", v.name.c_str());
			if(std::all_of(ref.begin(), ref.end(), [](const double _x) { return _x == 0; }))
			{
				std::printf(" silent, its data not in the image\n");
				continue;
			}
			// The pitch read on the family's grid, at its rate's resolution: the spectral peak of 10 to 300 ms
			const auto pitch = [&](const std::vector<double>& _x)
			{
				const auto from = static_cast<size_t>((Onset + 0.01) * own), to = static_cast<size_t>((Onset + 0.3) * own);
				return peakHz({_x.begin() + static_cast<std::ptrdiff_t>(from), _x.begin() + static_cast<std::ptrdiff_t>(to)}, own);
			};
			const double hz = pitch(ref);
			for(const double rate : Rates)
			{
				if(rate == own)
					continue;
				const auto test = hit(_library, v, rate, samplesAt(Seconds, rate));
				const auto q = compare(ref, own, test, rate);
				const double hzAt = pitch(toGrid(test, rate, own));
				const double cents = std::isfinite(hz) && std::isfinite(hzAt) ? 1200 * std::log2(hzAt / hz) : 0;
				// The Syntakt at 44.1 kHz: Resample48To44's, as before, its minimum phase moving where a band sounds: within each
				// machine's own Q15 bound, its stFast test's (judgeVoice), where it sounds not judged
				if(v.machine.family == Family::Syntakt && rate == 44100)
				{
					constexpr std::array<double, SyntaktMachines> Envelope{0.63, 2.24, 3.89, 1.18, 0.83, 4.24, 0.85, 7.25, 1.02};
					const double spectrum = v.machine.id == SyntaktTwinshot ? 1.19 : SpectrumDb;
					const bool good = q.envelope <= Envelope[v.machine.id] && q.spectrum <= spectrum && std::abs(cents) <= Cents;
					std::printf(" | 44.1: %.3f %.3f %.3f %+.3f %+.2fc (Resample48To44: bound %.2f)", q.envelope, q.envelope10,
						q.spectrum, q.lag, cents, Envelope[v.machine.id]);
					if(!good)
						failed += " " + v.name + " at 44.1 kHz,";
					ok = ok && good;
					continue;
				}
				std::printf(" | %g: %.3f %.3f %.3f %+.3f %+.2fc", rate / 1000, q.envelope, q.envelope10, q.spectrum, q.lag, cents);
				worstEnv = std::max(worstEnv, q.envelope);
				worstSpec = std::max(worstSpec, q.spectrum);
				worstLag = std::max(worstLag, std::abs(q.lag));
				worstCents = std::max(worstCents, std::abs(cents));
				const bool good = q.envelope <= EnvelopeDb && q.spectrum <= SpectrumDb && std::abs(q.lag) <= LagSamples
					&& std::abs(cents) <= Cents;
				if(!good)
					failed += " " + v.name + " at " + khz(rate) + ",";
				ok = ok && good;
			}
			if(v.tuned)
				std::printf(" | %.2f Hz", hz);
			std::printf("\n");
		}
		std::printf("  Q15 worst: envelope %.3f dB (bound %.1f), third octave %.3f dB (%.1f), where it sounds %.3f host sample (%.1f), "
			"pitch %.3f cent (%.0f)\n", worstEnv, EnvelopeDb, worstSpec, SpectrumDb, worstLag, LagSamples, worstCents, Cents);
		require(ok, "Q15 or pitch out of bounds:" + failed);

		// The sine's own pitch at every rate, both grains
		for(const int grain : {0, 100})
			for(const double rate : Rates)
			{
				const Voice v{"sine", Sampler, &_sine, grain, true};
				const auto x = toGrid(hit(_library, v, rate, samplesAt(Onset + 0.35, rate)), rate, 44100);
				const auto f = static_cast<size_t>((Onset + 0.01) * 44100), t = static_cast<size_t>((Onset + 0.3) * 44100);
				const double hz = peakHz({x.begin() + static_cast<std::ptrdiff_t>(f), x.begin() + static_cast<std::ptrdiff_t>(t)}, 44100);
				const double cents = 1200 * std::log2(hz / 440);
				if(rate == 44100)
					std::printf("  the 440 Hz sine on the %s grain, cents off 440 Hz:", grain ? "VINTAGE" : "ROM");
				std::printf(" %g kHz %+.3f", rate / 1000, cents);
				if(rate == Rates.back())
					std::printf("\n");
			}
	}

	// ---- HitDelay(rate), jitter 0 ----

	void jitter(const Library& _library, const Sample& _sine)
	{
		std::vector<Voice> all;
		for(const auto& m : machines())
			all.push_back({nameOf(m), m});
		for(int id = 0; id < OpenDrumMachines; ++id)
			all.push_back({"OPEN DRUM " + std::to_string(id), {Family::OpenDrum, static_cast<uint8_t>(id)}});
		all.push_back({"SOPHIE", {Family::Sophie, 0}});
		all.push_back({"Sampler ROM", Sampler, &_sine, 0});
		all.push_back({"Sampler VINTAGE", Sampler, &_sine, 100});
		constexpr std::array<int, 6> Positions{1, 31, 33, 152, 1021, 4703};
		for(const double rate : Rates)
		{
			if(rate == 44100)
				continue;	// edDrumsEngineTest's
			const int delay = Engine::hitDelay(rate);
			const int length = delay + samplesAt(0.034, rate);
			int sounding = 0;
			for(const auto& v : all)
			{
				const auto r = hit(_library, v, rate, length, 0);
				double peak = 0;
				for(const double x : r)
					peak = std::max(peak, std::abs(x));
				sounding += peak > 0;
				for(const int position : Positions)
				{
					const auto y = hit(_library, v, rate, position + length, position);
					for(int n = 0; n < length; ++n)
						if(y[static_cast<size_t>(position + n)] != r[static_cast<size_t>(n)])
							throw std::runtime_error(v.name + " at " + khz(rate) + ", note at " + std::to_string(position)
								+ ": differs " + std::to_string(n) + " samples after it");
				}
			}
			std::printf("  %s: HitDelay %d samples, %.3f ms (44.1 kHz: %d, %.3f ms); %zu voices, %d sounding, at %zu notes each: "
				"the same samples, jitter 0\n", khz(rate).c_str(), delay, 1000.0 * delay / rate, Engine::HitDelay,
				1000.0 * Engine::HitDelay / 44100, all.size(), sounding, Positions.size());
		}
	}

	// ---- a Hit on a voice still sounding ----

	// A 1 kHz sine on VINTAGE, hit again 0.3 s in: its new phase fades in over what the ring holds of the old voice, before
	// the new one can be read. The step a switch at once would leave (a sample of the Machinedrum's time moves a 1 kHz tone
	// by 0.14 of its peak) shows in the second difference. The old voice's samples end where its last block did, as at
	// 44.1 kHz, which is a step of its own: the Hit is moved over a Machinedrum block until they reach past the switch by
	// the FIR's half length, and the second difference from the crossfade's start to the switch is the steady tone's,
	// 10 % over at most. Its bounds: phaseFrom's, for VINTAGE in a bare chain, nothing ahead of the sample that sounds
	void retrigger(const Library& _library)
	{
		const Sample tone = sine(1000, 1.0);
		const Voice v{"sine", Sampler, &tone, 100, true};
		for(const double rate : Rates)
		{
			if(rate == 44100)
				continue;
			const auto r = static_cast<int64_t>(rate);
			const int64_t g = std::gcd(r, int64_t{44100}), up = r / g, down = 44100 / g;
			const int64_t delay = up * RateFir::Taps / 2;
			const int switchAhead = static_cast<int>((delay + down - 1) / down + 1 + (up - 1) / down);	// sounds - phaseAt
			const int fade = static_cast<int>(std::lround(Engine::CutSamples * rate / 44100));
			const int reach = static_cast<int>(std::ceil(RateFir::Taps / 2 * rate / 44100));	// a FIR's half length
			const auto play = [&](const int _offset)
			{
				auto e = engineFor(_library, v, rate);
				e->setChain(0, ChainSettings{});
				const int first = samplesAt(0.05, rate) + 7, again = samplesAt(0.3, rate) + _offset;
				const int samples = again + samplesAt(0.05, rate);
				std::vector<float> left(static_cast<size_t>(samples)), right(left.size());
				std::array<float*, Channels> p{};
				p[0] = left.data();
				p[1] = right.data();
				e->render(p.data(), first);
				e->noteOn(0, 127);
				p[0] += first;
				p[1] += first;
				e->render(p.data(), again - first);
				e->noteOn(0, 127);
				p[0] += again - first;
				p[1] += again - first;
				e->render(p.data(), samples - again);
				return std::make_pair(left, again + e->hitDelay());
			};
			// Where the old voice's samples end, before the new one's sound
			const auto oldEnd = [](const std::vector<float>& _x, const int _sounds)
			{
				int n = _sounds;
				while(n > 0 && std::abs(_x[static_cast<size_t>(n)]) < 0.2f)
					--n;
				return n;
			};
			// The worst over the Hits whose old voice reaches the switch: the old voice's end, through the FIR, reaching back
			// to it but by the window's last taps, 4 of them
			double worst = 0, around = 0, steady = 0;
			int hits = 0;
			for(int offset = 0; offset < samplesAt(2.0 * BlockSize / 44100, rate); ++offset)
			{
				const auto [x, sounds] = play(offset);
				const int switchAt = sounds - switchAhead;
				if(oldEnd(x, sounds) < switchAt - 4 + reach)
					continue;
				const auto d2 = [&](const int _from, const int _to)
				{
					double most = 0;
					for(int n = _from; n < _to; ++n)
						most = std::max(most, std::abs(static_cast<double>(x[static_cast<size_t>(n)])
							- 2.0 * x[static_cast<size_t>(n - 1)] + x[static_cast<size_t>(n - 2)]));
					return most;
				};
				const double s = d2(sounds - samplesAt(0.05, rate), sounds - samplesAt(0.01, rate));
				const double a = d2(switchAt - fade - samplesAt(0.0005, rate), switchAt + 1);
				++hits;
				if(a / s > worst)
				{
					worst = a / s;
					around = a;
					steady = s;
				}
			}
			require(hits > 0, "no Hit at " + khz(rate) + " leaves the old voice past its switch");
			std::printf("  a 1 kHz tone hit again at %s, %d positions: second difference at worst %.5f over the switch of phase, "
				"%.5f steady (%+.2f dB)\n", khz(rate).c_str(), hits, around, steady, 20 * std::log10(worst));
			require(worst <= 1.1, "a Hit on a sounding voice steps its tail at " + khz(rate));
		}
	}

	// ---- the sends, the master, the LFOs, the sequencer ----

	// Out 3 or Main, _seconds of one Hit on Track 0 through _setup at _rate, the Hit's first sample at Onset; after Lead
	// seconds of silence, taken off, in which the sends' words reach them: the Echo takes its words every 32 ms, a Hit
	// at 13 ms would land before or after the first, by a block, as the rate moves its wake
	constexpr double Lead = 0.5;

	// _later: the Hit and its output that many host samples later, against the effects' blocks
	std::vector<double> through(const Library& _library, const double _rate, const double _seconds, const int _channel,
		const std::function<void(Engine&)>& _setup, const int _later = 0)
	{
		auto e = std::make_unique<Engine>(_library, _rate);
		_setup(*e);
		const int lead = samplesAt(Lead, _rate) + _later;
		const int samples = lead + samplesAt(_seconds, _rate), note = lead + samplesAt(Onset, _rate) - e->hitDelay();
		std::vector<std::vector<float>> out(Channels, std::vector<float>(static_cast<size_t>(samples)));
		std::array<float*, Channels> p{};
		for(size_t c = 0; c < Channels; ++c)
			p[c] = out[c].data();
		e->render(p.data(), note);
		e->noteOn(0, 127);
		for(auto& x : p)
			x += note;
		// In host blocks of 100: the LFOs and the sends' blocks in no step with them
		for(int done = note; done < samples;)
		{
			const int run = std::min(100, samples - done);
			e->render(p.data(), run);
			for(auto& x : p)
				x += run;
			done += run;
		}
		return {out[static_cast<size_t>(_channel)].begin() + lead, out[static_cast<size_t>(_channel)].end()};
	}

	void effects(const Library& _library, const Sample& _sine)
	{
		const Machine snare = [&]
		{
			for(const auto& m : machines())
				if(m.family == Family::Machinedrum && dataOf(_library, m) == MachineData::Whole)
					return m;
			return Machine{};
		}();
		struct Case
		{
			const char* name;
			bool sent;		// its return on Out 3, else Main
			std::function<void(Engine&)> setup;
		};
		// The sine on VINTAGE, its return alone on Out 3
		const auto sendOnly = [&](const Send _send, const std::function<void(Sends&)>& _fx)
		{
			return [=, &_sine](Engine& _e)
			{
				_e.setMachine(0, Sampler);
				_e.setSample(0, &_sine);
				_e.setParameter(0, vintage::GrainSlot, 100);
				_e.setLevel(0, 127);
				_e.setOutput(0, 1);
				_e.setSend(0, _send, 127);
				_fx(_e.sends());
				_e.sends().setOutput(_send, 3);
			};
		};
		const std::vector<Case> cases{
			{"Echo (MD delay)", true, sendOnly(Send::Delay, [](Sends& _s)
				{
					_s.setEcho(md::fast::EchoTime, 24);
					_s.setEcho(md::fast::EchoFb, 32);
					_s.setEcho(md::fast::EchoFltw, 127);
					_s.setEcho(md::fast::EchoLev, 127);
				})},
			{"Gate Box (MD reverb)", true, sendOnly(Send::Reverb, [](Sends& _s)
				{
					_s.setGateBox(md::fast::GateDec, 64);
					_s.setGateBox(md::fast::GateDamp, 64);
					_s.setGateBox(md::fast::GateLp, 127);
					_s.setGateBox(md::fast::GateGate, 127);
					_s.setGateBox(md::fast::GateLev, 127);
				})},
			{"Syntakt delay", true, sendOnly(Send::Delay, [](Sends& _s)
				{
					_s.setSource(Send::Delay, SendSource::Syntakt);
					_s.setSyntaktDelay(StDelayTime, 24);
					_s.setSyntaktDelay(StDelayFdbk, 32);
					_s.setSyntaktDelay(StDelayLp, 127);
					_s.setSyntaktDelay(StDelayWidth, 64);
				})},
			{"Syntakt reverb", true, sendOnly(Send::Reverb, [](Sends& _s)
				{
					_s.setSource(Send::Reverb, SendSource::Syntakt);
					_s.setSyntaktReverb(StReverbDec, 64);
					_s.setSyntaktReverb(StReverbFreq, 64);
					_s.setSyntaktReverb(StReverbLp, 127);
				})},
			{"chorus", true, sendOnly(Send::Chorus, [](Sends& _s)
				{
					_s.setChorus(ChorusLevel, 127);
					_s.setChorus(ChorusDepth, 90);
					_s.setChorus(ChorusRate, 90);
				})},
			{"phaser", true, sendOnly(Send::Phaser, [](Sends& _s)
				{
					_s.setPhaser(PhaserLevel, 127);
					_s.setPhaser(PhaserRate, 90);
				})},
			{"master, compressing", false, [=](Engine& _e)
				{
					_e.setMachine(0, snare);
					_e.setLevel(0, 127);
					_e.master().set(MasterThreshold, 20);
					_e.master().set(MasterRatio, 100);
					_e.master().set(MasterAttack, 40);
					_e.master().set(MasterRelease, 40);
					_e.setMasterOn(true);
				}},
			{"LFOs, level and pan", false, [&](Engine& _e)
				{
					_e.setMachine(0, Sampler);
					_e.setSample(0, &_sine);
					_e.setParameter(0, vintage::GrainSlot, 100);
					_e.setLevel(0, 110);
					_e.setTempo(123);
					LfoSettings lfo;
					lfo.rate.division = 5;
					lfo.depth = 80;
					lfo.shape = LfoShape::Sine;
					lfo.target = LfoTarget::Level;
					_e.setLfo(0, 0, lfo);
					lfo.target = LfoTarget::Pan;
					lfo.rate.division = 4;
					_e.setLfo(0, 1, lfo);
				}},
		};
		constexpr double Seconds = 1.0;
		const auto onsetOf = [](const std::vector<double>& _x, const double _rate)
		{
			double peak = 0;
			for(const double v : _x)
				peak = std::max(peak, std::abs(v));
			size_t i = 0;
			while(i < _x.size() && std::abs(_x[i]) < 0.01 * peak)
				++i;
			return static_cast<double>(i) / _rate;
		};
		// The spectral peak of the return's first 50 to 400 ms, on 44.1 kHz's grid
		const auto pitchOf = [&](const std::vector<double>& _x, const double _rate)
		{
			const auto x = toGrid(_x, _rate, 44100);
			const double start = onsetOf(_x, _rate);
			const auto from = static_cast<size_t>((start + 0.05) * 44100);
			const auto to = std::min(x.size(), static_cast<size_t>((start + 0.4) * 44100));
			return peakHz({x.begin() + static_cast<std::ptrdiff_t>(from), x.begin() + static_cast<std::ptrdiff_t>(to)}, 44100);
		};
		bool ok = true;
		std::string failed;
		std::printf("  against 44.1 kHz         rate  10 ms envelope  third octave  pitch    return later by, and past that\n");
		for(const auto& c : cases)
		{
			const int channel = c.sent ? 6 : 0;
			const auto ref = through(_library, 44100, Seconds, channel, c.setup);
			// At 44.1 kHz itself, the same Hit elsewhere in the effects' block of 32: what where it falls in it moves
			Q15 natural;
			for(const int later : {5, 11, 16, 21, 27})
			{
				const auto q = compare(ref, 44100, through(_library, 44100, Seconds, channel, c.setup, later), 44100);
				natural.envelope10 = std::max(natural.envelope10, q.envelope10);
				natural.spectrum = std::max(natural.spectrum, q.spectrum);
			}
			std::printf("  %-22s 44.1, the Hit 5-27 later %6.3f dB %10.3f dB\n", c.name, natural.envelope10, natural.spectrum);
			for(const double rate : {48000.0, 88200.0, 96000.0, 192000.0})
			{
				const auto test = through(_library, rate, Seconds, channel, c.setup);
				// A return comes later than at 44.1 kHz, by SendsAtRate's FIRs: taken off before it is compared
				double later = 0;
				std::vector<double> shifted = test;
				if(c.sent)
				{
					auto e = std::make_unique<Engine>(_library, rate);
					const SendsAtRate bridge(e->sends(), rate, Engine::hostBlockOf(rate));
					later = bridge.returnDelay() - BlockSize * rate / 44100;
					shifted.erase(shifted.begin(), shifted.begin() + static_cast<std::ptrdiff_t>(std::lround(later)));
					shifted.resize(test.size());
				}
				const auto q = compare(ref, 44100, shifted, rate);
				const double cents = c.sent ? 1200 * std::log2(pitchOf(test, rate) / pitchOf(ref, 44100)) : 0;
				const double past = c.sent ? 1000 * (onsetOf(test, rate) - later / rate - onsetOf(ref, 44100)) : 0;
				std::printf("  %-22s %6g %10.3f dB %10.3f dB %+7.3f c  %6.3f ms, %+.3f ms\n", c.name, rate / 1000, q.envelope10,
					q.spectrum, cents, 1000 * later / rate, past);
				// A return: no further from 44.1 kHz's than 44.1 kHz's own from a Hit elsewhere in the sends' block, or than
				// a Hit's bounds; its pitch within 1 cent; where it starts within a sends' block. Main: MainDb
				const double envelopeBound = c.sent ? std::max(EnvelopeDb, natural.envelope10) : MainDb;
				const double spectrumBound = c.sent ? std::max(SpectrumDb, natural.spectrum) : MainDb;
				const bool good = q.envelope10 <= envelopeBound && q.spectrum <= spectrumBound && std::abs(cents) <= Cents
					&& std::abs(past) <= 1000.0 * BlockSize / 44100;
				if(!good)
					failed += std::string(" ") + c.name + " at " + khz(rate) + ",";
				ok = ok && good;
			}
		}
		require(ok, "the effects at another rate:" + failed);

		// The LFOs' phase, whatever the rate
		std::vector<double> phases;
		for(const double rate : Rates)
		{
			auto e = std::make_unique<Engine>(_library, rate);
			e->setMachine(0, snare);
			LfoSettings lfo;
			lfo.rate.sync = false;
			lfo.rate.hz = 3.3;
			lfo.depth = 50;
			lfo.target = LfoTarget::Level;
			e->setLfo(0, 0, lfo);
			std::vector<float> l(4096), r(4096);
			std::array<float*, Channels> p{};
			p[0] = l.data();
			p[1] = r.data();
			for(int done = 0; done < samplesAt(1.0, rate); done += 4096)
				e->render(p.data(), std::min(4096, samplesAt(1.0, rate) - done));
			phases.push_back(e->lfoPhase(0, 0));
		}
		// Within a Machinedrum block of it: the LFOs step at those blocks, the last one at 1 s rendered or not yet
		for(const double p : phases)
			require(std::abs(p - phases[0]) <= 3.3 * BlockSize / 44100 + 1e-9, "an LFO's phase after 1 s depends on the rate");
		std::printf("  an LFO at 3.3 Hz after 1 s: phase %.4f at 44.1 kHz, %.4f at 192 kHz\n", phases[0], phases.back());

		// The sequencer: a step at its time in seconds at every rate
		for(const double rate : Rates)
		{
			Sequencer s;
			s.pattern(0).setTrig(0, 5, true);
			Transport t;
			t.playing = true;
			t.bpm = 123;
			t.sampleRate = rate;
			t.length = samplesAt(1.0, rate);
			Events events;
			s.process(t, events);
			require(events.count >= 1 && events.events[0].trig, "the sequencer plays a step at " + khz(rate));
			const double at = events.events[0].offset / rate, expected = 5 * 60.0 / 123 / 4;
			require(std::abs(at - expected) <= 1 / rate, "the sequencer's step off its time at " + khz(rate));
		}
		std::printf("  the sequencer: step 6 at %.6f s at every rate\n", 5 * 60.0 / 123 / 4);
	}

	// ---- the cost ----

	void cost(const Library& _library)
	{
		// The dearest Machinedrum machine at full knobs, as edDrumsCost's search finds it, in short runs
		const auto once = [&](const double _rate, const std::array<Machine, Tracks>& _kit, const double _seconds, const bool _all,
			const int _oversampling)
		{
			auto e = std::make_unique<Engine>(_library, _rate);
			e->setOversampling(_oversampling);
			for(int t = 0; t < Tracks; ++t)
			{
				e->setMachine(t, _kit[static_cast<size_t>(t)]);
				for(int i = 0; i < 8; ++i)
					e->setParameter(t, i, 127);
				if(!_all)
				{
					e->setChain(t, ChainSettings{});
					continue;
				}
				ChainSettings s;
				const bool syntakt = _kit[static_cast<size_t>(t)].family == Family::Syntakt;
				s.drive[0] = {syntakt ? DriveType::MdDist : DriveType::Syntakt, false, 80, 127};
				s.drive[1] = {syntakt ? DriveType::Syntakt : DriveType::MdDist, syntakt, 80, 127};
				s.band.type = BandType::ThreeBand;
				s.band.values = {40, 80, 70, 50, 100, 70};
				s.filter.type = syntakt ? FilterType::Syntakt : FilterType::Machinedrum;
				s.filter.mode = 0;
				s.filter.frequency = 90;
				s.filter.resonance = 60;
				s.filter.depth = 100;
				s.filter.envelope.decay = 60;
				s.amp.on = syntakt;
				s.amp.hold = 40;
				s.amp.decay = 80;
				e->setChain(t, s);
				for(int i = 0; i < 2; ++i)
				{
					LfoSettings lfo;
					lfo.rate.division = t % 3;
					lfo.shape = LfoShape::Sine;
					lfo.depth = 40;
					lfo.target = i ? LfoTarget::BandA : LfoTarget::Frequency;
					e->setLfo(t, i, lfo);
				}
				for(int s2 = 0; s2 < SendCount; ++s2)
					e->setSend(t, static_cast<Send>(s2), 100);
			}
			if(_all)
			{
				e->sends().setEcho(md::fast::EchoLev, 127);
				e->sends().setGateBox(md::fast::GateLev, 127);
				e->sends().setChorus(ChorusLevel, 127);
				e->sends().setPhaser(PhaserLevel, 127);
				e->setMasterOn(true);
			}
			constexpr int HostBlock = 128;
			const int retrig = samplesAt(15 * 32 / 44100.0, _rate);	// the Syntakt's fastest retrig, 10 ms
			std::vector<std::vector<float>> buffers(Channels, std::vector<float>(HostBlock));
			std::array<float*, Channels> p{};
			int64_t at = 0, nextHit = 0;
			const auto block = [&]
			{
				for(int done = 0; done < HostBlock;)
				{
					if(at == nextHit)
					{
						for(int t = 0; t < Tracks; ++t)
						{
							for(int i = 0; i < 8; ++i)
								e->lock(t, i, 127);
							e->noteOn(t, 127);
						}
						nextHit += retrig;
					}
					const int run = static_cast<int>(std::min<int64_t>(HostBlock - done, nextHit - at));
					for(size_t c = 0; c < Channels; ++c)
						p[c] = buffers[c].data() + done;
					e->render(p.data(), run);
					at += run;
					done += run;
				}
			};
			const auto blocks = static_cast<int64_t>(_seconds * _rate / HostBlock);
			for(int64_t b = 0; b < blocks / 4; ++b)
				block();
			const auto t0 = Clock::now();
			for(int64_t b = 0; b < blocks; ++b)
				block();
			const double spent = std::chrono::duration<double>(Clock::now() - t0).count();
			return 100 * spent / (static_cast<double>(blocks) * HostBlock / _rate);
		};
		// The dearest machine of every family but the Syntakt's, whose cost follows its lanes (spec 12, criterion 4: the
		// worst case of the whole instrument, ticket 25)
		std::vector<Machine> candidates;
		for(const auto& m : machines())
			if(m.family == Family::Machinedrum)
				candidates.push_back(m);
		const std::pair<Family, int> others[] = {{Family::OpenDrum, OpenDrumMachines}, {Family::Sophie, SophieMachines},
			{Family::SyntaktAnalog, SyntaktAnalogMachines}, {Family::RytmAnalog, RytmAnalogMachines}};
		for(const auto& [family, count] : others)
			for(int id = 0; id < count; ++id)
				candidates.push_back({family, static_cast<uint8_t>(id)});
		const auto nameOfAny = [](const Machine& _m)
		{
			return _m.family == Family::Syntakt || _m.family == Family::Machinedrum ? nameOf(_m)
				: "family " + std::to_string(static_cast<int>(_m.family)) + " machine " + std::to_string(_m.id);
		};
		std::array<Machine, Tracks> kit;
		Machine dearest;
		double most = -1;
		for(const auto& m : candidates)
		{
			kit.fill(m);
			const double pct = once(44100, kit, 0.2, false, 2);
			if(pct > most)
			{
				most = pct;
				dearest = m;
			}
		}
		// Nine Syntakt machines, the dearest other one on the other 7; and the dearest on all 16
		std::array<Machine, Tracks> mixed;
		for(int t = 0; t < Tracks; ++t)
			mixed[static_cast<size_t>(t)] = t < SyntaktMachines ? Machine{Family::Syntakt, static_cast<uint8_t>(t)} : dearest;
		kit.fill(dearest);
		constexpr int Runs = 5;
		constexpr double Budget = 15;
		// The drives at 4x may cost up to 17 % of a core (the user's decision of 2026-11-13); 2x keeps spec 12's 15 %
		constexpr double Budget4x = 17;
		// The worst of the two kits at each rate, at least over the runs, the drives oversampled _oversampling times
		const auto worst = [&](const int _oversampling, const std::vector<double>& _rates)
		{
			std::map<double, double> best;
			for(const double rate : _rates)
			{
				for(const auto* k : {&mixed, &kit})
				{
					std::vector<double> pct;
					for(int r = 0; r < Runs; ++r)
						pct.push_back(once(rate, *k, 2.0, true, _oversampling));
					std::sort(pct.begin(), pct.end());
					best[rate] = std::max(best[rate], pct.front());
					std::printf("  %s (%s, dearest chains %dx, 2 LFOs a Track, 4 sends, the master) at %s: %.2f %% of a core "
						"at least, median %.2f\n", k == &mixed ? "9 Syntakt machines and 7 x" : "16 x",
						nameOfAny(dearest).c_str(), _oversampling, khz(rate).c_str(), pct.front(), pct[pct.size() / 2]);
				}
			}
			return best;
		};
		auto best = worst(2, {std::begin(Rates), std::end(Rates)});
		// The path the ticket weighed: the engine at 48 kHz (the Syntakt's) and its 32 channels up 2x by HIIR's half-bands
		{
			Oversampler up(48000);
			elektron::Block in{}, out[2];
			for(int n = 0; n < BlockSize; ++n)
				for(int l = 0; l < elektron::Lanes; ++l)
					in.s[n][l] = std::sin(0.01f * static_cast<float>(n + l));
			constexpr int Blocks = 48000 * 4 / BlockSize;
			const auto t0 = Clock::now();
			for(int b = 0; b < Blocks; ++b)
				for(int half = 0; half < 2; ++half)		// 32 channels: two Blocks of 16 lanes
					up.up(in, out, 2, 0xffff);
			const double pct = 100 * std::chrono::duration<double>(Clock::now() - t0).count() / 4;
			std::printf("  the 2x path at 96 kHz instead: the engine at 48 kHz %.2f %% + the half-bands on 32 channels %.2f %% = "
				"%.2f %%, a Hit placed to 2 host samples (jitter 1); the direct read %.2f %%, jitter 0\n", best[48000], pct,
				best[48000] + pct, best[96000]);
		}
		std::printf("  the worst case: %.2f %% at 44.1 kHz, %.2f at 48, %.2f at 88.2, %.2f at 96 (budget %.0f), %.2f at 192 "
			"(not budgeted)\n", best[44100], best[48000], best[88200], best[96000], Budget, best[192000]);
		for(const double rate : {44100.0, 48000.0, 88200.0, 96000.0})
			require(best[rate] < Budget, "the worst kit over 15 % of a core at " + khz(rate));

		auto best4x = worst(4, {44100.0, 48000.0, 88200.0, 96000.0});
		std::printf("  the worst case, drives at 4x: %.2f %% at 44.1 kHz, %.2f at 48, %.2f at 88.2, %.2f at 96 (budget %.0f)\n",
			best4x[44100], best4x[48000], best4x[88200], best4x[96000], Budget4x);
		for(const double rate : {44100.0, 48000.0, 88200.0, 96000.0})
			require(best4x[rate] < Budget4x, "the worst kit, drives at 4x, over 17 % of a core at " + khz(rate));
	}
}

int main(const int _argc, char** _argv)
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	const auto flash = Library::findFlashImage({});
	if(flash.empty())
	{
		std::printf("SKIP: set GEARMULATOR_MD_FIRMWARE_BIN to the Machinedrum UW flash image\n");
		return 77;
	}
	try
	{
		Library library(flash);
		const auto samples = noise();
		library.twinshot = samples.get();
		const Sample tone = sine(440);
		// edDrumsRateTest [DUMP|-] [PART]: the 44.1 kHz scene's 32 channels written to DUMP; PART alone run
		const std::string only = _argc > 2 ? _argv[2] : "";
		const auto run = [&](const char* _part) { return only.empty() || only == _part; };
		if(run("unchanged"))
			unchanged(library, _argc > 1 && std::string(_argv[1]) != "-" ? _argv[1] : nullptr);
		if(run("firs"))
			firs(library);
		if(run("q15"))
			q15(library, tone);
		if(run("jitter"))
			jitter(library, tone);
		if(run("retrigger"))
			retrigger(library);
		if(run("effects"))
			effects(library, tone);
		if(run("cost"))
			cost(library);
		std::printf("PASS: the engine at 44.1, 48, 88.2, 96 and 192 kHz\n");
		return 0;
	}
	catch(const std::exception& e)
	{
		std::printf("FAIL: %s\n", e.what());
		return 1;
	}
}
