// The Syntakt's send delay and reverb rewritten (stFastDelay, stFastReverb) against the exact port's, stEngine's
// main::delay and main::reverb run on OS 1.41's section 3 as CPU #1's mix runs them, at 48 kHz: Q2 of
// research/19-qualite-audio.md section 11, on noise and Hits, every setting's extremes and settings moved mid-signal,
// the delay's return going into the reverb. The harness shows it sees a wrong effect. First the laws of the tables
// against section 3's words, each error printed and a bound keeping it there (8e-7 for the poles, which reach 7.3e-7;
// 5e-8 for the other three, which reach 3e-8); last 44.1 kHz against 48, Q14's denormals, no allocation on the audio
// path, and the cost.
//
// SYNTAKT_SECTION3 and SYNTAKT_SECTION7 name the user's firmware; exit 77 without them.

#include "mdDrumsAllocationCounter.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <functional>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "stFastDelay.h"
#include "stFastMetrics.h"
#include "stFastReverb.h"
#include "stFastTables.h"
#include "stMainBlock.h"
#include "stMainBus.h"
#include "stMainDelay.h"
#include "stMainFx.h"
#include "stMainOutput.h"
#include "stMainReverb.h"

namespace
{
	namespace m = syntakt::main;
	namespace tables = syntakt::fast::tables;
	using syntakt::fast::Delay;
	using syntakt::fast::DelayWords;
	using syntakt::fast::Reverb;
	using syntakt::fast::ReverbWords;
	using syntakt::fast::bench::db;
	using syntakt::fast::bench::Pi;

	constexpr int Frames = 32;
	constexpr double Fs = 48000, BlockUs = 1e6 * Frames / Fs;
	constexpr double Q = 2147483648.0;
	constexpr uint32_t Params = m::Smoothed, DelayAt = Params + 0x6e6, ReverbAt = Params + 0x6fa;
	constexpr uint32_t LineIn8 = m::VoiceOutputs + 0x400;
	constexpr uint32_t GainsL = 0x800062e4, GainsR = 0x800062c0;	// the reverb's send's nine gains, each side

	void require(const bool _condition, const std::string& _message)
	{
		if(!_condition)
			throw std::runtime_error(_message);
	}

	struct Stereo
	{
		std::array<float, Frames> l{}, r{};
	};

	struct Settings
	{
		DelayWords delay;
		ReverbWords reverb;
		int toReverb = 0;		// the delay's REV
		double bpm = 120;
	};

	// ---- the signals: Hits of noise and of a pitched decay over a low tone, the two sides apart ----

	std::vector<Stereo> signal(const double _seconds, const double _level, const uint32_t _seed)
	{
		std::mt19937 random(_seed);
		std::uniform_real_distribution<double> uniform(-1, 1);
		std::vector<Stereo> blocks(static_cast<size_t>(_seconds * Fs / Frames));
		double envelope = 0, phase = 0, hz = 100;
		for(size_t n = 0; n < blocks.size() * Frames; ++n)
		{
			if(n % 12011 == 0)
			{
				envelope = 1;
				hz = 60 + 3000 * (uniform(random) + 1) / 2;
			}
			envelope *= 0.9997;
			phase += 2 * Pi * hz / Fs;
			const double low = std::sin(2 * Pi * 80.0 * static_cast<double>(n) / Fs);
			const double common = 0.5 * envelope * std::sin(phase) + 0.1 * low;
			blocks[n / Frames].l[n % Frames] = static_cast<float>(_level * (common + 0.4 * envelope * uniform(random)));
			blocks[n / Frames].r[n % Frames] = static_cast<float>(_level * (common + 0.4 * envelope * uniform(random)));
		}
		return blocks;
	}

	// ---- the exact port ----

	class Oracle
	{
	public:
		explicit Oracle(const m::Section3& _rom) : m_mem(std::make_unique<m::Memory>(_rom))
		{
			auto& mem = *m_mem;
			mem.reserveLines();
			m::loadSramImage(mem);
			m::delayInit(mem);
			m::reverbInit(mem);
			for(const uint32_t gains : {GainsL, GainsR})
				for(uint32_t k = 0; k < 9; ++k)
					mem.setWord(gains + 4 * k, k ? 0 : INT32_MIN);
		}

		void process(const Settings& _s, const Stereo& _toDelay, const Stereo& _toReverb)
		{
			auto& mem = *m_mem;
			const auto half = [&](const uint32_t _base, const std::array<int, 6>& _v)
			{
				for(uint32_t i = 0; i < 6; ++i)
					mem.setHalf(_base + 2 * i, _v[i]);
			};
			const auto& d = _s.delay;
			const auto& r = _s.reverb;
			half(DelayAt, {d.time, d.cross, d.width, d.feedback, d.highPass, d.lowPass});
			half(ReverbAt, {r.preDelay, r.decay, r.frequency, r.gain, r.highPass, r.lowPass});
			mem.setWord(m::Tempo, static_cast<int32_t>(_s.bpm * 120));

			m::prepareEffects(mem, m_acc, Params);
			m_acc.setMacsr(0xa0);
			for(uint32_t i = 0; i < Frames; ++i)
			{
				mem.setWord(m::SendBus + 8 * i, word(_toDelay.l[i]));
				mem.setWord(m::SendBus + 8 * i + 4, word(_toDelay.r[i]));
			}
			m::delay(mem, m_acc, m::DelayReturn, m::SendBus, DelayAt);
			m_acc.setMacsr(0xa0);

			// The reverb's send as the mix sums it: a channel of the tracks' and the delay's return at its REV
			const int16_t v = static_cast<int16_t>(_s.toReverb);
			const int32_t target = addWrap(neg(v * v), neg(v * v));
			for(int side = 0; side < 2; ++side)
			{
				const uint32_t gains = side ? GainsR : GainsL, bus = m::SendBus + 4 * side;
				const auto& in = side ? _toReverb.r : _toReverb.l;
				for(uint32_t i = 0; i < Frames; ++i)
					mem.setWord(m::VoiceOutputs + 4 * i, word(in[i]));
				m::copySide(mem, LineIn8, m::DelayReturn + 4 * side);
				const int32_t steps[9] = {0, 0, 0, 0, 0, 0, 0, 0, syntakt::rampStepWrap(target, mem.word(gains + 32))};
				m::sumChannels(mem, m_acc, 9, bus, m::VoiceOutputs, gains, steps);
			}
			m::reverb(mem, m_acc, m::ReverbReturn, m::SendBus, ReverbAt);
			m::advanceReverbLines(mem);
			m::writeLines(mem);
			require(mem.fault() == 0, "the exact port left its memory");

			for(uint32_t i = 0; i < Frames; ++i)
			{
				m_delay.push_back(mem.word(m::DelayReturn + 8 * i) / Q);
				m_delay.push_back(mem.word(m::DelayReturn + 8 * i + 4) / Q);
				m_reverb.push_back(mem.word(m::ReverbReturn + 8 * i) / Q);
				m_reverb.push_back(mem.word(m::ReverbReturn + 8 * i + 4) / Q);
			}
		}

		const std::vector<double>& delayReturn() const { return m_delay; }
		const std::vector<double>& reverbReturn() const { return m_reverb; }

	private:
		using Word = int32_t;

		static int32_t neg(const int _v) { return static_cast<int32_t>(0u - static_cast<uint32_t>(_v)); }
		static int32_t addWrap(const int32_t _a, const int32_t _b)
		{
			return static_cast<int32_t>(static_cast<uint32_t>(_a) + static_cast<uint32_t>(_b));
		}
		static Word word(const float _x)
		{
			return static_cast<Word>(std::clamp<long long>(std::llround(_x * Q), INT32_MIN, INT32_MAX));
		}

		std::unique_ptr<m::Memory> m_mem;
		m::Emac4 m_acc;
		std::vector<double> m_delay, m_reverb;
	};

	// ---- a scenario ----

	struct Scenario
	{
		std::string name;
		Settings settings;
		double seconds = 3;
		double toDelay = 0, toReverb = 0;			// the signal's level at each send
		std::function<void(int, Settings&)> move;	// by block, what moves
	};

	struct Null
	{
		double rms = 0, max = 0, level = 0, at = 0;
		bool sounds() const { return level > -60; }
		bool meetsQ2() const { return sounds() && rms <= -100 && max <= -90; }
	};

	Null null(const std::vector<double>& _a, const std::vector<double>& _b)
	{
		double error = 0, peak = 0, power = 0, at = 0;
		for(size_t i = 0; i < _a.size(); ++i)
		{
			const double e = _a[i] - _b[i];
			error += e * e;
			if(std::fabs(e) > peak)
			{
				peak = std::fabs(e);
				at = static_cast<double>(i / 2) / Fs;
			}
			power += _a[i] * _a[i];
		}
		const double n = static_cast<double>(_a.size());
		return {db(error / n), db(peak * peak), db(power / n), at};
	}

	void print(const char* _effect, const Null& _n)
	{
		std::printf("  %s: null %7.1f dBFS RMS, %7.1f max at %.3f s; return %6.1f dBFS RMS\n", _effect, _n.rms, _n.max,
			_n.at, _n.level);
	}

	struct Result
	{
		Null delay, reverb;
		std::vector<double> exactDelay, fastDelay;
	};

	// _wrong changes what the rewrite is given, not the exact port: the harness's own check
	Result run(const Scenario& _s, const m::Section3& _rom, const std::function<void(Settings&)>& _wrong = {})
	{
		Oracle oracle(_rom);
		auto delay = std::make_unique<Delay>(Fs);
		auto reverb = std::make_unique<Reverb>(Fs);
		const auto in = signal(_s.seconds, 1, 7);
		Settings settings = _s.settings;
		std::vector<double> delayOut, reverbOut;
		delayOut.reserve(in.size() * 2 * Frames);
		reverbOut.reserve(in.size() * 2 * Frames);
		for(size_t b = 0; b < in.size(); ++b)
		{
			if(_s.move)
				_s.move(static_cast<int>(b), settings);
			Stereo toDelay, toReverb, outDelay, outReverb;
			for(size_t i = 0; i < Frames; ++i)
			{
				toDelay.l[i] = static_cast<float>(in[b].l[i] * _s.toDelay);
				toDelay.r[i] = static_cast<float>(in[b].r[i] * _s.toDelay);
				toReverb.l[i] = static_cast<float>(in[b].l[i] * _s.toReverb);
				toReverb.r[i] = static_cast<float>(in[b].r[i] * _s.toReverb);
			}
			oracle.process(settings, toDelay, toReverb);

			Settings given = settings;
			if(_wrong)
				_wrong(given);
			delay->setWords(given.delay);
			delay->setTempo(given.bpm);
			delay->process(toDelay.l.data(), toDelay.r.data(), outDelay.l.data(), outDelay.r.data());
			reverb->setWords(given.reverb);
			reverb->setDelaySend(given.toReverb);
			reverb->process(toReverb.l.data(), toReverb.r.data(), outDelay.l.data(), outDelay.r.data(),
				outReverb.l.data(), outReverb.r.data());
			for(size_t i = 0; i < Frames; ++i)
			{
				delayOut.push_back(outDelay.l[i]);
				delayOut.push_back(outDelay.r[i]);
				reverbOut.push_back(outReverb.l[i]);
				reverbOut.push_back(outReverb.r[i]);
			}
		}
		return {null(oracle.delayReturn(), delayOut), null(oracle.reverbReturn(), reverbOut), oracle.delayReturn(),
			delayOut};
	}

	constexpr int byte(const int _v) { return _v << 8; }

	Settings base()
	{
		Settings s;
		s.delay = {byte(16), 0, byte(64), byte(32), byte(0), byte(127)};
		s.reverb = {byte(8), byte(64), byte(64), byte(64), byte(0), byte(127)};
		return s;
	}

	std::vector<Scenario> scenarios()
	{
		std::vector<Scenario> list;
		const auto delayCase = [&](const std::string& _name, const std::function<void(DelayWords&, double&)>& _set,
			const double _seconds = 3, const double _level = 0.25)
		{
			Scenario s{"delay: " + _name, base(), _seconds, _level, 0, {}};
			_set(s.settings.delay, s.settings.bpm);
			list.push_back(s);
		};
		const auto reverbCase = [&](const std::string& _name, const std::function<void(ReverbWords&)>& _set,
			const double _seconds = 3, const double _level = 0.25)
		{
			Scenario s{"reverb: " + _name, base(), _seconds, 0, _level, {}};
			_set(s.settings.reverb);
			list.push_back(s);
		};

		delayCase("the middle", [](DelayWords&, double&) {});
		delayCase("shortest time, 300 BPM", [](DelayWords& _d, double& _bpm) { _d.time = 0; _bpm = 300; });
		delayCase("longest time, 30 BPM", [](DelayWords& _d, double& _bpm) { _d.time = byte(127); _bpm = 30; }, 11);
		delayCase("FDBK 0", [](DelayWords& _d, double&) { _d.feedback = 0; });
		delayCase("FDBK 64, which sustains", [](DelayWords& _d, double&) { _d.feedback = byte(64); }, 6);
		delayCase("X, WID 0", [](DelayWords& _d, double&) { _d.cross = 1; _d.width = 0; _d.feedback = byte(50); });
		delayCase("X, WID 127", [](DelayWords& _d, double&)
		{
			_d.cross = 1;
			_d.width = byte(127);
			_d.feedback = byte(50);
		});
		delayCase("WID 0", [](DelayWords& _d, double&) { _d.width = 0; });
		delayCase("WID 32", [](DelayWords& _d, double&) { _d.width = byte(32); });
		delayCase("WID 127", [](DelayWords& _d, double&) { _d.width = byte(127); });
		delayCase("HPF 127", [](DelayWords& _d, double&)
		{
			_d.highPass = byte(127);
			_d.lowPass = 0;
			_d.feedback = byte(50);
		});
		delayCase("HPF 64, LPF 32", [](DelayWords& _d, double&)
		{
			_d.highPass = byte(64);
			_d.lowPass = byte(32);
			_d.feedback = byte(50);
		});
		delayCase("LPF 0", [](DelayWords& _d, double&) { _d.lowPass = 0; _d.feedback = byte(50); });
		delayCase("LPF 32", [](DelayWords& _d, double&) { _d.lowPass = byte(32); _d.feedback = byte(50); });

		reverbCase("the middle", [](ReverbWords&) {});
		reverbCase("DEC 0", [](ReverbWords& _r) { _r.decay = 0; });
		reverbCase("DEC 116", [](ReverbWords& _r) { _r.decay = byte(116); }, 6);
		reverbCase("DEC 126", [](ReverbWords& _r) { _r.decay = byte(126); }, 6);
		reverbCase("DEC 127, no decay", [](ReverbWords& _r) { _r.decay = byte(127); }, 6);
		reverbCase("PRE 0", [](ReverbWords& _r) { _r.preDelay = 0; });
		reverbCase("PRE 127", [](ReverbWords& _r) { _r.preDelay = byte(127); });
		reverbCase("FREQ 0, GAIN 0", [](ReverbWords& _r) { _r.frequency = 0; _r.gain = 0; _r.decay = byte(100); });
		const auto shelf = [&](const std::string& _name, const int _frequency, const int _gain)
		{
			reverbCase(_name, [=](ReverbWords& _r)
			{
				_r.frequency = byte(_frequency);
				_r.gain = byte(_gain);
				_r.decay = byte(100);
			});
		};
		shelf("FREQ 127, GAIN 0", 127, 0);
		shelf("FREQ 0, GAIN 127", 0, 127);
		shelf("FREQ 127, GAIN 127", 127, 127);
		reverbCase("HPF 127", [](ReverbWords& _r) { _r.highPass = byte(127); _r.lowPass = 0; });
		reverbCase("HPF 64, LPF 32", [](ReverbWords& _r) { _r.highPass = byte(64); _r.lowPass = byte(32); });
		reverbCase("LPF 0", [](ReverbWords& _r) { _r.lowPass = 0; });
		reverbCase("LPF 32", [](ReverbWords& _r) { _r.lowPass = byte(32); });
		reverbCase("full scale, DEC 120", [](ReverbWords& _r) { _r.decay = byte(120); }, 3, 1);
		reverbCase("full scale, DEC 127", [](ReverbWords& _r) { _r.decay = byte(127); }, 6, 1);

		const auto triangle = [](const int _b, const int _period, const int _top)
		{
			const int p = _b % (2 * _period);
			return (p < _period ? p : 2 * _period - p) * _top / _period;
		};
		list.push_back({"moved: the delay's time, FDBK, WID and filters", base(), 6, 0.25, 0,
			[=](const int _b, Settings& _s)
			{
				_s.delay.time = byte(triangle(_b, 1500, 64));
				_s.delay.feedback = byte(triangle(_b, 2100, 60));
				_s.delay.width = byte(triangle(_b, 700, 127));
				_s.delay.highPass = byte(triangle(_b, 900, 60));
				_s.delay.lowPass = byte(40 + triangle(_b, 1300, 87));
				_s.delay.cross = (_b / 2000) % 2;
				_s.bpm = 100 + triangle(_b, 3000, 60);
			}});
		list.push_back({"moved: the reverb's DEC, PRE, shelf and filters", base(), 6, 0, 0.25,
			[=](const int _b, Settings& _s)
			{
				_s.reverb.decay = byte(triangle(_b, 1500, 127));
				_s.reverb.preDelay = byte(triangle(_b, 2100, 90));
				_s.reverb.frequency = byte(triangle(_b, 700, 127));
				_s.reverb.gain = byte(triangle(_b, 1100, 127));
				_s.reverb.highPass = byte(triangle(_b, 900, 60));
				_s.reverb.lowPass = byte(40 + triangle(_b, 1300, 87));
			}});
		list.push_back({"the delay into the reverb, REV moved", base(), 6, 0.25, 0.1, [=](const int _b, Settings& _s)
			{
				_s.toReverb = byte(triangle(_b, 1700, 127));
			}});
		return list;
	}

	// ---- a loop that grows ----

	// FDBK over 64 has a loop gain over 1 at low level, which the clip limits: a slot of the loop that passes near zero
	// doubles what any arithmetic leaves in its last bits at each pass, and may settle on the other side of zero, where
	// the exact port's took the one. No rewrite stays on those samples without the machine's every truncation and word.
	// At FDBK 127 the loop settles on the rails, and the null keeps Q3 as ticket 05 settled it, SNR 70 dB overall and
	// 60 dB in the worst 10 ms window. Below, two takes of one sound, as the voices' chaotic zone (ticket 05): their
	// third octaves
	void checkSustained(const m::Section3& _rom)
	{
		struct Case
		{
			const char* name;
			int time, feedback;
			double bpm, level, seconds;
			bool rails;
		};
		const Case cases[] = {
			{"FDBK 127", byte(16), byte(127), 120, 0.25, 12, true},
			{"FDBK 127 at full scale", byte(16), byte(127), 120, 1, 12, true},
			{"FDBK 127, HPF and LPF in", byte(16), byte(127), 120, 0.25, 12, true},
			{"FDBK 90", byte(16), byte(90), 120, 0.25, 12, false},
			{"FDBK 70", byte(16), byte(70), 120, 0.25, 12, false},
			{"FDBK 127, shortest time, 300 BPM", 0, byte(127), 300, 0.25, 12, false},
		};
		for(const auto& c : cases)
		{
			Scenario s{c.name, base(), c.seconds, c.level, 0, {}};
			s.settings.delay.time = c.time;
			s.settings.delay.feedback = c.feedback;
			s.settings.bpm = c.bpm;
			if(std::string(c.name).find("HPF") != std::string::npos)
			{
				s.settings.delay.highPass = byte(40);
				s.settings.delay.lowPass = byte(60);
			}
			const auto r = run(s, _rom);
			const auto& x = r.exactDelay;
			const auto& y = r.fastDelay;

			double signal = 0, noise = 0, loudest = 0;
			constexpr size_t Window = 2 * 480;
			std::vector<std::pair<double, double>> windows;
			for(size_t w = 0; w + Window <= x.size(); w += Window)
			{
				double a = 0, e = 0;
				for(size_t i = w; i < w + Window; ++i)
				{
					a += x[i] * x[i];
					e += (x[i] - y[i]) * (x[i] - y[i]);
				}
				signal += a;
				noise += e;
				loudest = std::max(loudest, a);
				windows.push_back({a, e});
			}
			double worst = 1e9;
			for(const auto& [a, e] : windows)
				if(a > loudest * 1e-6)
					worst = std::min(worst, db(a) - db(e));
			const double snr = db(signal) - db(noise);

			const auto left = [](const std::vector<double>& _x)
			{
				std::vector<double> l(_x.size() / 2);
				for(size_t i = 0; i < l.size(); ++i)
					l[i] = _x[2 * i];
				return l;
			};
			const auto exactBands = syntakt::fast::bench::bands(left(x), Fs, -18, 12);
			const auto fastBands = syntakt::fast::bench::bands(left(y), Fs, -18, 12);
			double bandError = 0;
			for(size_t b = 0; b < exactBands.size(); ++b)
				bandError = std::max(bandError, std::fabs(db(exactBands[b]) - db(fastBands[b])));
			std::printf("delay: %-34s SNR %6.1f dB, worst 10 ms %6.1f dB, third octaves within %.3f dB; "
				"null %7.1f RMS, %7.1f max at %.3f s; return %6.1f\n", c.name, snr, worst, bandError, r.delay.rms,
				r.delay.max, r.delay.at, r.delay.level);
			require(!c.rails || (snr >= 70 && worst >= 60), std::string(c.name) + ": misses Q3");
			require(bandError <= 1, std::string(c.name) + ": the takes differ in their third octaves");
		}
	}

	// ---- the laws against section 3's words ----

	double wordAt(const m::Section3& _rom, const uint32_t _address)
	{
		const size_t o = _address - m::Section3::Base;
		const auto& b = _rom.bytes;
		const uint32_t word = uint32_t(b[o]) << 24 | uint32_t(b[o + 1]) << 16 | uint32_t(b[o + 2]) << 8 | b[o + 3];
		return static_cast<int32_t>(word) / Q;
	}

	void checkLaw(const char* _name, const double _error, const double _bound)
	{
		std::printf("%-40s within %.3g of section 3's words\n", _name, _error);
		require(_error <= _bound, std::string(_name) + ": its law moved away from the words");
	}

	void checkLaws(const m::Section3& _rom)
	{
		const auto poles = tables::sendPoles(Fs);
		double error = 0;
		for(uint32_t i = 0; i < 1024; ++i)
			error = std::max(error, std::fabs(poles[i] - wordAt(_rom, m::Poles + 4 * i)));
		checkLaw("the poles, 0x4029bffc", error, 8e-7);

		const auto shelves = tables::reverbShelves(Fs);
		error = 0;
		for(uint32_t i = 0; i < 128; ++i)
			error = std::max(error, std::fabs(shelves[i] - wordAt(_rom, 0x4029b7fc + 4 * i)));
		checkLaw("the shelves, 0x4029b7fc", error, 5e-8);

		const auto diffusion = tables::reverbDiffusion();
		error = 0;
		for(uint32_t i = 0; i < 64; ++i)
			error = std::max(error, std::fabs(diffusion[i] - wordAt(_rom, 0x4029b6fc + 4 * i)));
		checkLaw("the diffusers' gains, 0x4029b6fc", error, 5e-8);

		const auto matrix = tables::reverbMatrix();
		error = 0;
		for(uint32_t r = 0; r < 128; ++r)
			for(uint32_t j = 0; j < 8; ++j)
				error = std::max(error, std::fabs(matrix[r][j] - wordAt(_rom, 0x401df8e0 + 32 * r + 4 * j)));
		checkLaw("the feedback matrix, 0x401df8e0", error, 5e-8);
	}

	// ---- the harness sees a wrong effect ----

	void checkWrongEffects(const m::Section3& _rom)
	{
		struct Wrong
		{
			const char* name;
			bool delay;
			std::function<void(Settings&)> change;
		};
		const std::vector<Wrong> wrongs = {
			{"TIME one byte off", true, [](Settings& _s) { _s.delay.time += byte(1); }},
			{"FDBK one byte off", true, [](Settings& _s) { _s.delay.feedback += byte(1); }},
			{"HPF one step of the poles off", true, [](Settings& _s) { _s.delay.highPass += 64; }},
			{"X the other way", true, [](Settings& _s) { _s.delay.cross = !_s.delay.cross; }},
			{"PRE one byte off", false, [](Settings& _s) { _s.reverb.preDelay += byte(1); }},
			{"DEC one byte off", false, [](Settings& _s) { _s.reverb.decay += byte(1); }},
			{"FREQ one byte off", false, [](Settings& _s) { _s.reverb.frequency += byte(1); }},
			{"GAIN one byte off", false, [](Settings& _s) { _s.reverb.gain += byte(1); }},
		};
		for(const auto& w : wrongs)
		{
			Scenario s{"", base(), 1, w.delay ? 0.25 : 0, w.delay ? 0 : 0.25, {}};
			const auto r = run(s, _rom, w.change);
			const Null& n = w.delay ? r.delay : r.reverb;
			std::printf("wrong: %-32s null %7.1f dBFS RMS, %7.1f max\n", w.name, n.rms, n.max);
			require(!n.meetsQ2(), std::string("the null did not see ") + w.name);
		}
	}

	// ---- 44.1 kHz: the same seconds, not the same samples ----

	void checkRates()
	{
		const auto seconds = [](const double _rate)
		{
			Delay delay(_rate);
			Settings s = base();
			delay.setWords(s.delay);
			const int blocks = static_cast<int>(3 * _rate / Frames), strike = blocks * 2 / 3;
			std::array<float, Frames> in{}, l{}, r{}, none{};
			int at = -1;
			for(int b = 0; b < blocks; ++b)
			{
				in[0] = b == strike ? 1.0f : 0.0f;
				delay.process(in.data(), none.data(), l.data(), r.data());
				for(int i = 0; i < Frames && at < 0; ++i)
					if(b > strike && std::fabs(l[static_cast<size_t>(i)]) > 0.05f)
						at = (b - strike) * Frames + i;
			}
			return at / _rate;
		};
		const double t48 = seconds(48000), t44 = seconds(44100);
		std::printf("the delay's first echo: %.5f s at 48 kHz, %.5f s at 44.1 kHz\n", t48, t44);
		require(std::fabs(t48 - t44) < 4.0 / 44100, "44.1 kHz: the delay's time is not the same in seconds");

		// The glide, from the phase of a slow sine through the delay: 255/256 a block at 48 kHz, 172 ms to 1 - 1 / e of
		// the way from one time to another, which the integers make; the phase's window, 40 ms, reads about 5 ms later
		const auto glide = [](const double _rate)
		{
			constexpr double From = 0.05, To = 0.1, Hz = 25;
			Delay delay(_rate);
			Settings s = base();
			s.delay.feedback = 0;
			delay.setWords(s.delay);
			delay.setFree(1 / From);
			delay.settle();
			std::array<float, Frames> in{}, none{}, l{}, r{};
			const int steady = static_cast<int>(_rate / 2) / Frames, total = 3 * steady;
			const double w = 2 * Pi * Hz / _rate;
			std::vector<float> out;
			for(int b = 0; b < total; ++b)
			{
				if(b == steady)
					delay.setFree(1 / To);
				for(int i = 0; i < Frames; ++i)
					in[static_cast<size_t>(i)] = 0.05f * static_cast<float>(std::sin(w * (b * Frames + i)));
				delay.process(in.data(), none.data(), l.data(), r.data());
				out.insert(out.end(), l.begin(), l.end());
			}
			// The phase over a period of the sine, by steps of a block, unwrapped
			const int period = static_cast<int>(_rate / Hz), change = steady * Frames;
			const double mark = 2 * Pi * Hz * (To - From) * (1 - std::exp(-1.0));
			double first = 0, last = 0, unwrapped = 0;
			for(int at = change - period; at + period < static_cast<int>(out.size()); at += Frames)
			{
				std::complex<double> sum = 0;
				for(int n = at; n < at + period; ++n)
					sum += std::polar(double(out[static_cast<size_t>(n)]), -w * n);
				const double phase = std::arg(sum);
				if(at == change - period)
				{
					first = last = unwrapped = phase;
					continue;
				}
				unwrapped += std::remainder(phase - last, 2 * Pi);
				last = phase;
				if(first - unwrapped >= mark)
					return (at + period / 2 - change) / _rate;
			}
			return 0.0;
		};
		const double glide48 = glide(48000), glide44 = glide(44100);
		std::printf("the delay's glide to 1 - 1 / e: %.2f ms at 48 kHz, %.2f ms at 44.1 kHz\n", glide48 * 1e3,
			glide44 * 1e3);
		for(const double t : {glide48, glide44})
			require(std::fabs(t - 0.175) < 0.005, "the delay's glide does not take its seconds");

		// The line's swing: the return of a steady tone turns with it, once a period of the sine, whatever else it
		// holds. Its turns' frequency from a slow Fourier sum of the tone's amplitude, window by window
		const auto swing = [](const double _rate)
		{
			constexpr int Windows = 4;		// blocks of a period of the tone: an integer of them a window
			constexpr double TonePeriods = 64;
			Reverb reverb(_rate);
			Settings s = base();
			s.reverb.decay = byte(32);
			reverb.setWords(s.reverb);
			const int window = 16 * Windows * Frames;
			const double tone = 2 * Pi * TonePeriods / window;
			const double each = static_cast<double>(window) / _rate;
			std::vector<std::complex<double>> z;
			std::array<float, Frames> in{}, none{}, l{}, r{};
			for(int w = 0; w < static_cast<int>(16 / each); ++w)
			{
				std::complex<double> sum = 0;
				for(int b = 0; b < window / Frames; ++b)
				{
					for(int i = 0; i < Frames; ++i)
						in[static_cast<size_t>(i)] = 0.2f * static_cast<float>(std::sin(tone * (b * Frames + i)));
					reverb.process(in.data(), in.data(), none.data(), none.data(), l.data(), r.data());
					for(int i = 0; i < Frames; ++i)
						sum += std::polar(double(l[static_cast<size_t>(i)]), -tone * (b * Frames + i));
				}
				if(w >= static_cast<int>(2 / each))		// the reverb full
					z.push_back(sum / double(window));
			}
			std::complex<double> mean = 0;
			for(const auto& v : z)
				mean += v;
			mean /= double(z.size());
			double best = 0, bestHz = 0;
			for(double hz = 0.3; hz < 1; hz += 0.001)
			{
				std::complex<double> sum = 0;
				for(size_t k = 0; k < z.size(); ++k)
					sum += (z[k] - mean) * std::polar(1.0, -2 * Pi * hz * double(k) * each);
				if(std::abs(sum) > best)
				{
					best = std::abs(sum);
					bestHz = hz;
				}
			}
			return bestHz;
		};
		const double swing48 = swing(48000), swing44 = swing(44100);
		std::printf("the line's swing: %.3f Hz at 48 kHz, %.3f Hz at 44.1 kHz\n", swing48, swing44);
		for(const double hz : {swing48, swing44})
			require(std::fabs(hz - 0.65) < 0.02, "the line's swing does not take its seconds");

		// The reverb's fall, in 100 ms windows
		const auto fall = [](const double _rate)
		{
			Reverb reverb(_rate);
			Settings s = base();
			s.reverb.decay = byte(100);
			reverb.setWords(s.reverb);
			const int window = static_cast<int>(_rate / 10 / Frames);
			std::vector<double> levels;
			std::array<float, Frames> in{}, none{}, l{}, r{};
			std::mt19937 random(3);
			std::uniform_real_distribution<float> uniform(-0.3f, 0.3f);
			for(int w = 0; w < 30; ++w)
			{
				double power = 0;
				for(int b = 0; b < window; ++b)
				{
					for(auto& x : in)
						x = w == 0 ? uniform(random) : 0.0f;
					reverb.process(in.data(), in.data(), none.data(), none.data(), l.data(), r.data());
					for(size_t i = 0; i < Frames; ++i)
						power += double(l[i]) * l[i] + double(r[i]) * r[i];
				}
				levels.push_back(db(power / (window * Frames)));
			}
			return levels;
		};
		const auto a = fall(48000), b = fall(44100);
		double worst = 0;
		for(size_t w = 3; w < a.size(); ++w)
			worst = std::max(worst, std::fabs(a[w] - b[w]));
		std::printf("the reverb's fall at 44.1 kHz against 48 kHz: within %.2f dB in 100 ms windows over 3 s\n", worst);
		require(worst < 1.5, "44.1 kHz: the reverb does not fall as at 48 kHz");
	}

	// ---- the rest ----

	template<typename Render>
	double usPerBlock(Render&& _render, const int _blocks = 8000, const int _runs = 7)
	{
		using Clock = std::chrono::steady_clock;
		double best = 1e12;
		for(int run = 0; run < _runs; ++run)
		{
			const auto t0 = Clock::now();
			for(int b = 0; b < _blocks; ++b)
				_render();
			best = std::min(best, std::chrono::duration<double, std::micro>(Clock::now() - t0).count() / _blocks);
		}
		return best;
	}

	struct Effects
	{
		std::unique_ptr<Delay> delay;
		std::unique_ptr<Reverb> reverb;
		Stereo in, returned, out;

		explicit Effects(const double _rate, const Settings& _s)
			: delay(std::make_unique<Delay>(_rate)), reverb(std::make_unique<Reverb>(_rate))
		{
			delay->setWords(_s.delay);
			reverb->setWords(_s.reverb);
			reverb->setDelaySend(_s.toReverb);
		}

		void render()
		{
			delay->process(in.l.data(), in.r.data(), returned.l.data(), returned.r.data());
			reverb->process(in.l.data(), in.r.data(), returned.l.data(), returned.r.data(), out.l.data(), out.r.data());
		}
	};

	Settings loud()
	{
		Settings s = base();
		s.delay.feedback = byte(30);
		s.reverb.decay = byte(120);
		s.toReverb = byte(100);
		return s;
	}

	void measureCost()
	{
		const auto in = signal(1, 0.25, 11);
		for(const double rate : {48000.0, 44100.0})
		{
			Effects effects(rate, loud());
			size_t b = 0;
			const double us = usPerBlock([&]
			{
				effects.in = in[b];
				b = (b + 1) % in.size();
				effects.render();
			});
			const double budget = 1e6 * Frames / rate;
			std::printf("cost at %.1f kHz, the delay and the reverb: %.2f us a block, %.2f %% of a core\n", rate / 1000,
				us, us / budget * 100);
		}
	}

	// Q14: a block's time in the tail, against an active block
	void checkDenormals()
	{
		Effects effects(Fs, loud());
		const auto in = signal(1, 0.7, 13);
		for(const auto& block : in)
		{
			effects.in = block;
			effects.render();
		}
		const auto render = [&] { effects.render(); };
		const double active = usPerBlock(render);
		effects.in = Stereo{};
		for(int b = 0; b < static_cast<int>(10 * Fs / Frames); ++b)
			render();
		const double after10 = usPerBlock(render);
		for(int b = 0; b < static_cast<int>(120 * Fs / Frames); ++b)
			render();
		const double after130 = usPerBlock(render);
		std::printf("Q14: a block %.2f us playing, %.2f us 10 s after, %.2f us 130 s after: %.2f x, %.2f x\n", active,
			after10, after130, after10 / active, after130 / active);
		require(after10 <= 1.2 * active && after130 <= 1.2 * active,
			"Q14: a tail costs more than 1.2 x an active block");
	}

	void checkNoAllocation()
	{
		Effects effects(Fs, base());
		effects.in.l[0] = 0.5f;
		Settings s = base();
		const auto allocations = countAllocations([&]
		{
			for(int b = 0; b < 1000; ++b)
			{
				s.delay.time = byte(b % 128);
				s.reverb.decay = byte(b % 128);
				effects.delay->setWords(s.delay);
				effects.reverb->setWords(s.reverb);
				effects.render();
			}
		});
		require(allocations == 0, "the audio path allocated");
		std::printf("no allocation in 1000 blocks of words and audio\n");
	}
}

int main()
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	const char* section3 = std::getenv("SYNTAKT_SECTION3");
	const char* section7 = std::getenv("SYNTAKT_SECTION7");
	if(!section3 || !*section3 || !section7 || !*section7)
	{
		std::printf("SKIP: set SYNTAKT_SECTION3 and SYNTAKT_SECTION7 to OS 1.41's sections\n");
		return 77;
	}
	const auto rom = m::loadSection3(section3);
	if(rom.bytes.empty())
	{
		std::printf("SKIP: SYNTAKT_SECTION3 is not OS 1.41's section 3\n");
		return 77;
	}
	try
	{
		checkLaws(rom);
		int failures = 0;
		for(const auto& s : scenarios())
		{
			const auto t0 = std::chrono::steady_clock::now();
			const auto r = run(s, rom);
			const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
			const bool delayOk = s.toDelay == 0 || r.delay.meetsQ2();
			const bool reverbOk = !r.reverb.sounds() ? s.toReverb == 0 : r.reverb.meetsQ2();
			std::printf("%-46s (%.1f s here)%s\n", s.name.c_str(), took, delayOk && reverbOk ? "" : "  FAILS");
			if(s.toDelay != 0)
				print("delay ", r.delay);
			if(s.toReverb != 0 || r.reverb.sounds())
				print("reverb", r.reverb);
			failures += !(delayOk && reverbOk);
		}
		require(failures == 0, std::to_string(failures) + " scenarios miss Q2");
		checkSustained(rom);
		checkWrongEffects(rom);
		checkRates();
		checkDenormals();
		checkNoAllocation();
		measureCost();
	}
	catch(const std::exception& _e)
	{
		std::printf("FAILED: %s\n", _e.what());
		return 1;
	}
	std::printf("PASS\n");
	return 0;
}
