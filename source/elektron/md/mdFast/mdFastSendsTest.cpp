// The Rhythm Echo and the Gate Box (mdFastEcho, mdFastGateBox, mdFastMasterWords) against what they port. First the
// OS's side: the Gate Box's two tables from their formulas against the mixer program's words, then every word of both
// effects against the OS's own 68k code (MachineRunner), over each byte's whole range. Then the returns against
// mdEngine's MasterEngine, the emulated mixer DSP, over the whole render: real Hits and noise as the sends, every
// setting's extremes and a parameter moved mid-signal. The Echo, integers from send to return, must be the DSP's sample
// for sample; the Gate Box, in float, Q2 of research/19-qualite-audio.md section 11, its loop's gain under 1 whatever
// the settings. The harness shows it sees a wrong effect. Last Q14's denormals, no allocation on the audio path, and
// the cost.
//
// The oracle's returns: MasterEngine adds them to the dry mix and runs the EQ and Dynamix on the sum, which the v1's
// master does not use. Here its program stops at P:$66C, where the sum starts (a jump to the harness's stub, written
// into its P memory: no state of the two effects lives after that point), and the returns are read where they lie:
// the Echo's in X:$600 (over the delay send) and the Gate Box's in Y:$02-$41.
//
// Its own process: Musashi serves one CPU class per executable (CLAUDE.md). GEARMULATOR_MD_FIRMWARE_BIN names the flash
// image; exit 77 without it.
// usage: mdFastSendsTest

#include "mdDrumsAllocationCounter.h"

#include "mdFastEcho.h"
#include "mdFastGateBox.h"
#include "mdFastMasterWords.h"
#include "stFastMetrics.h"

#include "mdVoice.h"
#include "mdVoiceCoefficients.h"
#include "mdVoicePiImport.h"

#include "Firmware.h"
#include "MachineRunner.h"
#include "MasterEngine.h"

#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/memory.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
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

namespace
{
	using md::engine::MachineRunner;
	using md::engine::MasterEngine;
	using md::fast::Echo;
	using md::fast::EchoWords;
	using md::fast::GateBox;
	using md::fast::GateBoxWords;
	using md::fast::MasterValues;
	using md::fast::SendWords;
	using md::voice::OsTables;
	using md::voice::TrxTables;
	using syntakt::fast::bench::db;
	using syntakt::fast::bench::energy;
	using syntakt::fast::bench::localSnr;

	constexpr int Block = 32;
	constexpr double Fs = 44100, Full = 8388608;
	constexpr double BlockUs = Block / Fs * 1e6;
	constexpr int InputStart = 256;		// blocks: the words of both effects are in, as the OS's first ticks leave them

	constexpr uint32_t Tempo = 125 * 24;

	// ---- the OS ----

	constexpr uint32_t kLevel = 0x1000d7c, kLevelTarget = 0x1000f5c, kLevelSmooth = 0x100029e, kTempo = 0x100150c;
	constexpr uint32_t kGateValues = kLevel + 2 * 16, kEchoValues = kGateValues + 2 * 8;
	constexpr uint32_t kGateTargets = kLevelTarget + 16, kEchoTargets = kGateTargets + 8;
	constexpr uint32_t kEchoBody = 0x20b4ae, kEchoExit = 0x20b5f8, kEchoStaging = 0x1001af4;
	constexpr uint32_t kGateBody = 0x20b940, kGateExit = 0x20ba4c, kGateStaging = 0x1001b74;
	constexpr uint16_t kRts = 0x4e75;

	class Os
	{
	public:
		explicit Os(std::vector<uint8_t> _image) : m_runner(std::move(_image))
		{
			m_runner.poke16(kEchoExit, kRts);
			m_runner.poke16(kGateExit, kRts);
		}

		EchoWords echo(const MasterValues& _values, const uint32_t _tempo)
		{
			for(uint32_t k = 0; k < _values.size(); ++k)
				m_runner.poke16(kEchoValues + 2 * k, _values[k]);
			m_runner.poke32(kTempo, _tempo);
			return runEcho();
		}

		GateBoxWords gateBox(const MasterValues& _values)
		{
			for(uint32_t k = 0; k < _values.size(); ++k)
				m_runner.poke16(kGateValues + 2 * k, _values[k]);
			return runGateBox();
		}

		// The bodies, on the values and the tempo the OS holds
		EchoWords runEcho()
		{
			EchoWords w{};
			run(kEchoBody, kEchoStaging, w);
			return w;
		}

		GateBoxWords runGateBox()
		{
			GateBoxWords w{};
			run(kGateBody, kGateStaging, w);
			return w;
		}

		MachineRunner& runner() { return m_runner; }

	private:
		template<size_t N>
		void run(const uint32_t _body, const uint32_t _staging, std::array<uint32_t, N>& _words)
		{
			if(m_runner.call(_body, {}) < 0)
				throw std::runtime_error("the OS's body faulted");
			for(uint32_t k = 0; k < N; ++k)
				_words[k] = m_runner.peek32(_staging + 4 * k) & 0xffffff;
		}

		MachineRunner m_runner;
	};

	template<size_t N>
	void compare(const char* _name, const std::array<uint32_t, N>& _os, const std::array<uint32_t, N>& _port,
		const MasterValues& _values, const uint32_t _tempo)
	{
		for(size_t k = 0; k < N; ++k)
		{
			if(_os[k] == (_port[k] & 0xffffff))
				continue;
			std::string values;
			for(const auto v : _values)
				values += " " + std::to_string(v);
			throw std::runtime_error(std::string(_name) + " word " + std::to_string(k) + ": the OS's "
				+ std::to_string(_os[k]) + ", the port's " + std::to_string(_port[k]) + ", values" + values + ", tempo "
				+ std::to_string(_tempo));
		}
	}

	void checkTables(const md::fw::FlashOs& _os, const TrxTables& _trx, MasterEngine& _oracle)
	{
		const auto& tables = GateBox::tables();
		const auto check = [&](const char* _name, const GateBox::Table& _table, const uint32_t _addr, const int _limit)
		{
			int differ = 0, worst = 0;
			const auto words = md::fw::dspWords(_os.firmware.dspB, _addr, GateBox::Rows * GateBox::Lines);
			for(size_t r = 0; r < GateBox::Rows; ++r)
			{
				for(size_t l = 0; l < GateBox::Lines; ++l)
				{
					const int diff = std::abs(static_cast<int>(_table[r][l])
						- static_cast<int>(words[r * GateBox::Lines + l]));
					differ += diff != 0;
					worst = std::max(worst, diff);
				}
			}
			std::printf("%s from its formula: %d of 384 words differ, by at most %d\n", _name, differ, worst);
			if(worst > _limit)
				throw std::runtime_error(std::string(_name) + " is too far from the program's");
		};
		check("the Gate Box's decay table", tables.decay, 0x1439c0, 1);
		check("the Gate Box's damping table", tables.damping, 0x143b40, 0);

		const auto& memory = _oracle.dsp().memory();
		for(uint32_t k = 0; k < _trx.sine.size(); ++k)
			if(_trx.sine[k] != (memory.get(dsp56k::MemArea_Y, 0x148000 + k) & 0xffffff))
				throw std::runtime_error("the mixer DSP's sine differs from the voice DSP's");
		std::printf("the mixer DSP's sine: md::voice::TrxTables::sine, word for word\n");
	}

	void checkWords(Os& _os, const OsTables& _tables)
	{
		std::mt19937 random(0x20b4ae);
		const auto randomValues = [&]
		{
			MasterValues v;
			for(auto& x : v)
				x = static_cast<uint16_t>(random() % 0x4000);
			return v;
		};
		const auto tempo = [&] { return 720u + random() % (7200 - 720 + 1); };

		int64_t count = 0;
		// Each byte's whole range, the smoothed word at every value, the others random
		for(size_t p = 0; p < 8; ++p)
		{
			for(uint32_t value = 0; value <= 0x3fff; ++value)
			{
				auto v = randomValues();
				v[p] = static_cast<uint16_t>(value);
				const uint32_t t = tempo();
				compare("Echo", _os.echo(v, t), md::fast::echoWords(_tables, v, t), v, t);
				v = randomValues();
				v[p] = static_cast<uint16_t>(value);
				compare("Gate Box", _os.gateBox(v), md::fast::gateBoxWords(_tables, v), v, 0);
				count += 2;
			}
		}
		// The bytes that act together: FLTF with FLTW; TIME with the tempo at every delay either side of the line's end
		for(uint32_t f = 0; f < 128; ++f)
		{
			for(uint32_t w = 0; w < 128; ++w)
			{
				auto v = randomValues();
				v[md::fast::EchoFltf] = static_cast<uint16_t>(f << 7);
				v[md::fast::EchoFltw] = static_cast<uint16_t>(w << 7);
				compare("Echo", _os.echo(v, Tempo), md::fast::echoWords(_tables, v, Tempo), v, Tempo);
				++count;
			}
		}
		for(uint32_t time = 0; time <= 0x3fff; time += 7)
		{
			for(const uint32_t t : {720u, 1000u, 1440u, 3000u, 5000u, 7200u})
			{
				auto v = randomValues();
				v[md::fast::EchoTime] = static_cast<uint16_t>(time);
				compare("Echo", _os.echo(v, t), md::fast::echoWords(_tables, v, t), v, t);
				++count;
			}
		}
		std::printf("the Echo's and the Gate Box's words: %lld sets, word for word\n", static_cast<long long>(count));
	}

	// The tick against the OS's: the same bytes moved at the same ticks, the values and the words of each turn
	void checkTicks(Os& _os, const OsTables& _tables)
	{
		auto& os = _os.runner();
		os.poke32(kTempo, Tempo);
		for(uint32_t k = 0; k < 16; ++k)
		{
			os.poke16(kGateValues + 2 * k, 0);
			os.poke8(kGateTargets + k, 0);
		}
		SendWords port(_tables);
		port.setTempo(Tempo);
		std::mt19937 random(0x100029e);
		for(int tick = 0; tick < 4000; ++tick)
		{
			if(random() % 3 == 0)
			{
				const uint32_t byte = random() % 16;
				const auto value = static_cast<uint8_t>(random() % 128);
				os.poke8(kGateTargets + byte, value);
				if(byte < 8)
					port.setGateBox(static_cast<int>(byte), value);
				else
					port.setEcho(static_cast<int>(byte) - 8, value);
			}
			if(os.call(kLevelSmooth, {}) < 0)
				throw std::runtime_error("the OS's smoothing faulted");
			const auto due = port.tick();
			if(due == SendWords::Due::Echo && _os.runEcho() != port.echo())
				throw std::runtime_error("a tick's Echo words differ");
			if(due == SendWords::Due::GateBox && _os.runGateBox() != port.gateBox())
				throw std::runtime_error("a tick's Gate Box words differ");
		}
		std::printf("the smoothing and the turns of the words: 4000 ticks with the OS's\n");

		// A tempo outside the OS's range is held to it: a 0 would divide by 0 in the tick
		SendWords held(_tables);
		held.setTempo(0);
		for(int tick = 0; tick < 4; ++tick)
			held.tick();
		if(held.echo() != md::fast::echoWords(_tables, MasterValues{}, 720))
			throw std::runtime_error("a tempo of 0 is not held to 720");
	}

	// ---- the oracle ----

	class Oracle
	{
	public:
		explicit Oracle(const md::fw::Firmware& _fw) : m_master(_fw)
		{
			// jmp >$C00, the harness's stub (MasterEngine.cpp), at the sum of the returns
			m_master.dsp().memWriteP(0x66c, 0x0af080);
			m_master.dsp().memWriteP(0x66d, 0x000c00);
		}

		// Below the Echo's first line lie 64 zeros, then the boot's last code words, which a read at -65 or lower plays
		// as samples: the words only the firmware holds. Zeros there, as the port reads
		void clearBootCode()
		{
			const std::array<uint32_t, 32> zeros{};
			m_master.setX(0x1000da - 32, zeros.data(), 32);
		}

		void setEcho(const EchoWords& _words)
		{
			m_master.setWords(0x150, _words.data(), static_cast<int>(_words.size()));
		}
		void setGateBox(const GateBoxWords& _words)
		{
			m_master.setWords(0x185, _words.data(), static_cast<int>(_words.size()));
		}

		// The sends in, the returns out
		void process(const md::engine::Mixer::Stereo& _rev, const md::engine::Mixer::Stereo& _del,
			std::array<int32_t, 2 * Block>& _echo, std::array<int32_t, 2 * Block>& _gate)
		{
			if(!m_master.process(m_dry, _rev, _del, m_out))
				throw std::runtime_error("the oracle faulted: " + m_master.faultReason());
			const auto& memory = m_master.dsp().memory();
			const auto sign = [](const uint32_t _w) { return static_cast<int32_t>(_w << 8) >> 8; };
			for(uint32_t k = 0; k < 2 * Block; ++k)
			{
				_echo[k] = sign(memory.get(dsp56k::MemArea_X, 0x600 + k));
				_gate[k] = sign(m_master.readY(2 + k));
			}
		}

	private:
		MasterEngine m_master;
		md::engine::Mixer::Stereo m_dry{}, m_out{};
	};

	// ---- what plays ----

	struct Bytes
	{
		std::array<uint8_t, 8> gate, echo;
	};

	// The Engine's defaults
	constexpr Bytes Defaults = {{0, 0, 68, 50, 1, 82, 71, 109}, {16, 0, 32, 27, 0, 44, 0, 71}};

	// A stereo send, 24-bit samples
	using Send = std::vector<std::array<int32_t, 2>>;

	void setBytes(SendWords& _words, const Bytes& _bytes, const uint32_t _tempo = Tempo)
	{
		for(int k = 0; k < 8; ++k)
		{
			_words.setGateBox(k, _bytes.gate[static_cast<size_t>(k)]);
			_words.setEcho(k, _bytes.echo[static_cast<size_t>(k)]);
		}
		_words.setTempo(_tempo);
	}

	// The two effects as a host runs them: the Echo's return into the Gate Box
	using Frames = std::array<std::array<float, Block>, 2>;

	struct Effects
	{
		std::unique_ptr<Echo> echo = std::make_unique<Echo>();
		std::unique_ptr<GateBox> gate;
		Frames ret{}, out{};

		explicit Effects(const uint32_t* _sine) : gate(std::make_unique<GateBox>(_sine)) {}

		// The words the bytes settle at
		void settle(SendWords& _words)
		{
			for(int t = 0; t < 64; ++t)
				_words.tick();
			echo->setWords(_words.echo());
			gate->setWords(_words.gateBox());
		}

		void render(const Frames& _toEcho, const Frames& _toGate)
		{
			echo->process(_toEcho[0].data(), _toEcho[1].data(), ret[0].data(), ret[1].data());
			gate->process(_toGate[0].data(), _toGate[1].data(), ret[0].data(), ret[1].data(), out[0].data(),
				out[1].data());
		}
	};

	Send noiseBurst(const double _seconds, const double _gain, const uint32_t _seed)
	{
		Send s(static_cast<size_t>(_seconds * Fs));
		std::mt19937 random(_seed);
		std::uniform_real_distribution<double> u(-1.0, 1.0);
		for(auto& f : s)
			for(auto& v : f)
				v = static_cast<int32_t>(std::lround(u(random) * _gain * 8388607));
		return s;
	}

	// Real Hits: a pattern of TRX-BD, TRX-SD and TRX-CH on three voices, panned, at their default knobs
	class Hits
	{
	public:
		Hits(const TrxTables& _trx, const OsTables& _tables, MachineRunner& _os) : m_tables(_tables)
		{
			m_shared = std::make_unique<md::voice::Shared>();
			m_shared->trx = &_trx;
			for(size_t v = 0; v < m_machine.size(); ++v)
				m_defaults[v] = _os.machine(m_machine[v])->defaults;
		}

		Send render(const double _seconds, const double _gain) const
		{
			Send s(static_cast<size_t>(_seconds * Fs) / Block * Block);
			std::vector<md::voice::Voice> voices;
			for(uint32_t v = 0; v < 3; ++v)
				voices.emplace_back(*m_shared, v);
			constexpr int Step = 2940 / Block;		// 15 Hz, a sixteenth at 112.5 BPM
			constexpr int Pattern[3] = {0b0000000010001001, 0b0001000000010000, 0b0101010101010101};
			constexpr double PanL[3] = {1.0, 0.8, 0.4}, PanR[3] = {1.0, 0.4, 0.8};
			for(size_t b = 0; b < s.size() / Block; ++b)
			{
				const bool stepStart = b % Step == 0;
				const int step = static_cast<int>(b / Step) % 16;
				for(size_t v = 0; v < 3; ++v)
				{
					const bool trigger = stepStart && (Pattern[v] >> step & 1);
					if(trigger || b % md::voice::kBlocksPerTick == 0)
					{
						std::array<uint16_t, 8> params;
						for(size_t p = 0; p < 8; ++p)
							params[p] = static_cast<uint16_t>(m_defaults[v][p] << 7);
						md::voice::Packet packet;
						const int n = md::voice::coefficients(m_tables, m_machine[v], params.data(), trigger, Tempo,
							packet);
						voices[v].setSlot(packet.data(), n);
					}
					md::voice::Voice::Block out{};
					voices[v].render(out);
					for(size_t i = 0; i < Block; ++i)
					{
						auto& f = s[b * Block + i];
						f[0] += static_cast<int32_t>(out[i] * _gain * PanL[v]);
						f[1] += static_cast<int32_t>(out[i] * _gain * PanR[v]);
					}
				}
			}
			for(auto& f : s)
				for(auto& v : f)
					v = std::clamp(v, -8388608, 8388607);
			return s;
		}

	private:
		const OsTables& m_tables;
		std::unique_ptr<md::voice::Shared> m_shared;
		std::array<uint8_t, 3> m_machine{16, 17, 22};	// TRX-BD, TRX-SD, TRX-CH
		std::array<std::array<uint8_t, 8>, 3> m_defaults{};
	};

	// ---- the measures ----

	struct Q
	{
		double rms, max;	// of the null over the whole render, dBFS
		double snr, local;	// Q3's
		double level;		// the oracle's return, RMS dBFS over the whole render
		size_t clipped;		// samples at full scale in the oracle's return
	};

	Q measure(const std::vector<double>& _ref, const std::vector<double>& _test)
	{
		Q q{};
		std::vector<double> residual(_ref.size());
		double peak = 0;
		for(size_t i = 0; i < _ref.size(); ++i)
		{
			residual[i] = _test[i] - _ref[i];
			peak = std::max(peak, std::fabs(residual[i]));
			q.clipped += std::fabs(_ref[i]) >= 8388607.0 / Full;
		}
		const auto size = static_cast<double>(_ref.size());
		q.rms = db(energy(residual) / size);
		q.max = db(peak * peak);
		q.level = db(energy(_ref) / size);
		q.snr = q.level - q.rms;
		q.local = localSnr(_ref, _test, Fs);
		return q;
	}

	struct Result
	{
		Q echo, gate;
	};

	struct Move
	{
		int block;				// counted from the start of the input
		Bytes bytes;
		uint32_t tempo;
	};

	struct Scenario
	{
		std::string name;
		Bytes bytes;
		const Send* send;
		double seconds;		// from the start of the input
		bool toEcho = true, toGate = true;
		uint32_t tempo = Tempo;
		std::vector<Move> moves;
		std::function<void(EchoWords&, GateBoxWords&)> spoil;	// the controls': what changes the port's words
		bool clearBootCode = false;		// Oracle::clearBootCode
	};

	// What a run needs of the firmware and the tables
	struct Setup
	{
		const md::fw::Firmware& firmware;
		const OsTables& tables;
		const uint32_t* sine;
	};

	Result run(const Scenario& _s, const Setup& _setup)
	{
		Oracle oracle(_setup.firmware);
		if(_s.clearBootCode)
			oracle.clearBootCode();
		Effects effects(_setup.sine);
		SendWords words(_setup.tables);
		setBytes(words, _s.bytes, _s.tempo);

		const int blocks = static_cast<int>(_s.seconds * Fs / Block);
		std::array<std::vector<double>, 4> rendered;
		for(auto& samples : rendered)
			samples.reserve(static_cast<size_t>(blocks) * 2 * Block);
		auto& [refEcho, refGate, testEcho, testGate] = rendered;

		for(int b = -InputStart; b < blocks; ++b)
		{
			for(const auto& move : _s.moves)
				if(move.block == b)
					setBytes(words, move.bytes, move.tempo);
			if((b + InputStart) % md::voice::kBlocksPerTick == 0)
			{
				const auto due = words.tick();
				auto e = words.echo();
				auto g = words.gateBox();
				if(_s.spoil)
					_s.spoil(e, g);
				if(due == SendWords::Due::Echo)
				{
					oracle.setEcho(words.echo());
					effects.echo->setWords(e);
				}
				else if(due == SendWords::Due::GateBox)
				{
					oracle.setGateBox(words.gateBox());
					effects.gate->setWords(g);
				}
			}

			md::engine::Mixer::Stereo rev{}, del{};
			Frames inEcho{}, inGate{};
			for(size_t i = 0; i < Block; ++i)
			{
				const auto n = static_cast<int64_t>(b) * Block + static_cast<int64_t>(i);
				if(n < 0 || n >= static_cast<int64_t>(_s.send->size()))
					continue;
				const auto& f = (*_s.send)[static_cast<size_t>(n)];
				for(size_t c = 0; c < 2; ++c)
				{
					if(_s.toEcho)
					{
						del[i][c] = f[c];
						inEcho[c][i] = static_cast<float>(f[c] / Full);
					}
					if(_s.toGate)
					{
						rev[i][c] = f[c];
						inGate[c][i] = static_cast<float>(f[c] / Full);
					}
				}
			}

			std::array<int32_t, 2 * Block> oracleEcho, oracleGate;
			oracle.process(rev, del, oracleEcho, oracleGate);
			effects.render(inEcho, inGate);

			if(b < 0)
				continue;
			for(size_t i = 0; i < Block; ++i)
			{
				for(size_t c = 0; c < 2; ++c)
				{
					refEcho.push_back(oracleEcho[2 * i + c] / Full);
					refGate.push_back(oracleGate[2 * i + c] / Full);
					testEcho.push_back(effects.ret[c][i]);
					testGate.push_back(effects.out[c][i]);
				}
			}
		}
		return {measure(refEcho, testEcho), measure(refGate, testGate)};
	}

	// A return that sounds: without it the nulls below prove nothing
	bool sounds(const Q& _q) { return _q.level > -60; }

	// Q2. The Echo's stricter: not one sample differs, measured on the words
	bool meetsQ2(const Q& _q) { return sounds(_q) && _q.rms <= -100 && _q.max <= -90; }
	bool exact(const Q& _q) { return sounds(_q) && _q.max < -500; }

	void print(const char* _effect, const Q& _q)
	{
		std::printf("  %s: null %7.1f dBFS RMS, %7.1f max; SNR %6.1f, worst 10 ms %6.1f; return %6.1f dBFS RMS, %zu at "
			"full scale\n", _effect, _q.rms, _q.max, _q.snr, _q.local, _q.level, _q.clipped);
	}

	int runScenarios(const std::vector<Scenario>& _list, const Setup& _setup)
	{
		int failures = 0;
		for(const auto& s : _list)
		{
			const auto t0 = std::chrono::steady_clock::now();
			const auto r = run(s, _setup);
			const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
			const bool echoOk = !s.toEcho || exact(r.echo);
			const bool gateOk = (!s.toGate && s.bytes.gate[md::fast::GateDvol] == 0) || meetsQ2(r.gate);
			std::printf("%s (%.1f s of audio, %.1f s here)%s\n", s.name.c_str(), s.seconds, took,
				echoOk && gateOk ? "" : "  FAILS");
			if(s.toEcho)
				print("Echo", r.echo);
			if(s.toGate || s.bytes.gate[md::fast::GateDvol])
				print("Gate Box", r.gate);
			failures += !(echoOk && gateOk);
		}
		return failures;
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

	void measureCost(const Setup& _setup)
	{
		Frames in{};
		const auto send = noiseBurst(0.1, 0.5, 1);
		for(size_t i = 0; i < Block; ++i)
			for(size_t c = 0; c < 2; ++c)
				in[c][i] = static_cast<float>(send[i][c] / Full);

		SendWords words(_setup.tables);
		for(const auto& [name, bytes] : {std::pair<const char*, Bytes>{"defaults", Defaults},
			{"at their busiest (MOD, DEC, DVOL, no gate)", {{127, 80, 127, 0, 0, 127, 127, 127},
				{32, 127, 127, 60, 0, 127, 64, 127}}}})
		{
			setBytes(words, bytes);
			Effects effects(_setup.sine);
			effects.settle(words);
			auto& ret = effects.ret;
			auto& out = effects.out;
			const double echoUs = usPerBlock([&]
			{
				effects.echo->process(in[0].data(), in[1].data(), ret[0].data(), ret[1].data());
			});
			const double gateUs = usPerBlock([&]
			{
				effects.gate->process(in[0].data(), in[1].data(), ret[0].data(), ret[1].data(), out[0].data(),
					out[1].data());
			});
			std::printf("cost, %s: Echo %.2f us a block, %.3f %% of a core; Gate Box %.2f us, %.3f %%; both %.3f %%\n",
				name, echoUs, echoUs / BlockUs * 100, gateUs, gateUs / BlockUs * 100,
				(echoUs + gateUs) / BlockUs * 100);
			std::printf("memory: the Echo's lines %zu bytes (object %zu), the Gate Box's %zu bytes (object %zu)\n",
				effects.echo->lineBytes(), sizeof(Echo), effects.gate->lineBytes(), sizeof(GateBox));
		}

		// The reference: the whole master section on the DSP emulator, the same two sends, the dry mix silent
		MasterEngine master(_setup.firmware);
		md::engine::Mixer::Stereo dry{}, rev{}, del{}, main{};
		for(size_t i = 0; i < Block; ++i)
		{
			for(size_t c = 0; c < 2; ++c)
			{
				rev[i][c] = send[i][c];
				del[i][c] = send[i][c];
			}
		}
		const auto loaded = words.echo();
		master.setWords(0x150, loaded.data(), static_cast<int>(loaded.size()));
		const auto loadedGate = words.gateBox();
		master.setWords(0x185, loadedGate.data(), static_cast<int>(loadedGate.size()));
		const double masterUs = usPerBlock([&]
		{
			if(!master.process(dry, rev, del, main))
				throw std::runtime_error("the master faulted");
		}, 2000, 5);
		std::printf("cost, the oracle's whole master section (with its EQ and Dynamix): %.2f us a block, %.2f %% of a "
			"core\n", masterUs, masterUs / BlockUs * 100);
	}

	// Q14: a block's time in the tail, against an active block
	void checkDenormals(const Setup& _setup)
	{
		SendWords words(_setup.tables);
		setBytes(words, {{0, 0, 127, 0, 0, 127, 127, 127}, {16, 0, 32, 60, 0, 127, 64, 127}});
		Effects effects(_setup.sine);
		effects.settle(words);

		const auto send = noiseBurst(1, 0.7, 2);
		Frames in{};
		const auto render = [&] { effects.render(in, in); };
		const auto play = [&](const double _seconds)
		{
			for(int b = 0; b < static_cast<int>(_seconds * Fs / Block); ++b)
				render();
		};

		for(size_t b = 0; b < send.size() / Block; ++b)
		{
			for(size_t i = 0; i < Block; ++i)
				for(size_t c = 0; c < 2; ++c)
					in[c][i] = static_cast<float>(send[b * Block + i][c] / Full);
			render();
		}
		const double active = usPerBlock(render);
		for(auto& channel : in)
			channel.fill(0);
		play(10);
		const double after10 = usPerBlock(render);
		play(120);
		const double after130 = usPerBlock(render);
		double peak = 0;
		for(size_t i = 0; i < Block; ++i)
			peak = std::max({peak, std::fabs(double(effects.out[0][i])), std::fabs(double(effects.ret[0][i]))});
		std::printf("Q14: a block %.2f us playing, %.2f us 10 s after, %.2f us 130 s after (peak then %.1f dBFS): "
			"%.2f x, %.2f x\n", active, after10, after130, db(peak * peak), after10 / active, after130 / active);
		if(after10 > 1.2 * active || after130 > 1.2 * active)
			throw std::runtime_error("Q14: a tail costs more than 1.2 x an active block");
	}

	void checkNoAllocation(const Setup& _setup)
	{
		SendWords words(_setup.tables);
		setBytes(words, Defaults);
		Effects effects(_setup.sine);
		Frames in{};
		in[0][0] = 0.5f;
		const auto allocations = countAllocations([&]
		{
			for(int b = 0; b < 1000; ++b)
			{
				const auto due = b % md::voice::kBlocksPerTick == 0 ? words.tick() : SendWords::Due::None;
				if(due == SendWords::Due::Echo)
					effects.echo->setWords(words.echo());
				else if(due == SendWords::Due::GateBox)
					effects.gate->setWords(words.gateBox());
				effects.render(in, in);
			}
		});
		if(allocations != 0)
			throw std::runtime_error("the audio path allocated");
		std::printf("no allocation in 1000 blocks of words and audio\n");
	}
}

int main()
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	const char* path = std::getenv("GEARMULATOR_MD_FIRMWARE_BIN");
	if(!path || !*path)
	{
		std::printf("SKIP: set GEARMULATOR_MD_FIRMWARE_BIN to the Machinedrum UW flash image\n");
		return 77;
	}
	try
	{
		const auto flash = md::fw::readFile(path);
		const auto os = md::fw::loadFirmwareFromFlash(flash);
		const auto pi = md::voice::importPi(os);
		const OsTables tables(pi.data());
		auto trx = std::make_unique<TrxTables>();
		md::voice::importTrx(os.firmware, *trx);

		Os osCode(os.osImage);
		{
			MasterEngine master(os.firmware);
			checkTables(os, *trx, master);
		}
		checkWords(osCode, tables);
		checkTicks(osCode, tables);

		const Hits hits(*trx, tables, osCode.runner());
		const auto pattern = hits.render(4, 0.7);
		const auto loud = hits.render(4, 1.5);
		const auto noise = noiseBurst(0.3, 0.5, 3);
		const auto fullNoise = noiseBurst(0.3, 1.0, 4);
		const auto impulse = [] { Send s(32); s[0] = {8000000, 6000000}; return s; }();
		const Setup setup{os.firmware, tables, trx->sine.data()};

		auto list = [&]
		{
			using S = Scenario;
			constexpr bool E = true, G = false;
			// An effect's bytes, the other's defaults; the send to that effect alone
			auto only = [&](const bool _echo, const std::string& _name, const std::array<uint8_t, 8>& _bytes,
				const Send& _send, const double _seconds, const uint32_t _tempo = Tempo)
			{
				S s;
				s.name = (_echo ? "Echo: " : "Gate Box: ") + _name;
				s.bytes = Defaults;
				(_echo ? s.bytes.echo : s.bytes.gate) = _bytes;
				s.send = &_send;
				s.seconds = _seconds;
				s.toEcho = _echo;
				s.toGate = !_echo;
				s.tempo = _tempo;
				return s;
			};
			std::vector<S> v;
			v.push_back(only(E, "defaults, Hits", Defaults.echo, pattern, 8));
			v.push_back(only(E, "defaults, noise", Defaults.echo, noise, 5));
			v.push_back(only(E, "no filter, unity feedback, long tail", {32, 0, 0, 64, 0, 127, 127, 127}, impulse, 30));
			v.push_back(only(E, "modulated read, deep and fast", {24, 127, 127, 40, 0, 127, 64, 100}, pattern, 8));
			v.push_back(only(E, "modulated read, deep and slow", {24, 127, 0, 40, 0, 127, 64, 100}, pattern, 8));
			v.push_back(only(E, "FB 127, saturating, ping-pong", {16, 0, 0, 127, 0, 127, 0, 100}, pattern, 10));
			v.push_back(only(E, "FB 127, narrow band", {8, 30, 40, 127, 60, 40, 64, 100}, loud, 10));
			v.push_back(only(E, "full-scale noise, FB 100", {12, 0, 0, 100, 0, 127, 32, 127}, fullNoise, 6));
			v.push_back(only(E, "TIME 127 at 30 BPM, slowed", {127, 0, 0, 50, 0, 127, 64, 100}, pattern, 14, 720));
			v.push_back(only(E, "TIME 127, slowed, modulated", {127, 127, 64, 50, 0, 127, 64, 100}, pattern, 14, 720));
			v.push_back(only(E, "TIME 0", {0, 0, 0, 60, 0, 127, 64, 100}, pattern, 6));
			v.push_back(only(E, "TIME 1 at 300 BPM", {1, 0, 0, 60, 0, 127, 64, 100}, pattern, 6, 7200));
			v.push_back(only(E, "FLTF 127, FLTW 0", {16, 0, 0, 60, 127, 0, 64, 127}, noise, 4));
			v.push_back(only(E, "FLTF 0, FLTW 0", {16, 0, 0, 60, 0, 0, 64, 127}, noise, 4));
			{
				// The lowest read a host can reach, -67 (searched over every MFRQ and Echo tick): the distance at the
				// line's top, then the slowest tempo and the deepest triangle at once. Past the DSP's 64 zeros
				auto s = only(E, "the read pointer below the line", {0, 0, 51, 60, 0, 127, 64, 100}, pattern, 19);
				auto b = s.bytes;
				b.echo[md::fast::EchoTime] = 127;
				b.echo[md::fast::EchoMod] = 127;
				s.moves.push_back({24464 - InputStart, b, 720});
				s.clearBootCode = true;
				v.push_back(s);
			}
			{
				auto s = only(E, "TIME and FB moved mid-signal", {16, 0, 32, 40, 0, 100, 32, 100}, pattern, 10);
				auto b = s.bytes;
				b.echo[md::fast::EchoTime] = 90;
				b.echo[md::fast::EchoFb] = 70;
				s.moves.push_back({600, b, Tempo});
				b.echo[md::fast::EchoTime] = 6;
				b.echo[md::fast::EchoMod] = 60;
				s.moves.push_back({1500, b, Tempo});
				v.push_back(s);
			}

			v.push_back(only(G, "defaults, Hits", Defaults.gate, pattern, 10));
			v.push_back(only(G, "defaults, noise", Defaults.gate, noise, 8));
			v.push_back(only(G, "DEC 127, no damping, gate open, tail", {0, 0, 127, 0, 0, 127, 127, 127}, pattern, 24));
			v.push_back(only(G, "DEC 0, DAMP 127", {0, 0, 0, 127, 0, 127, 127, 127}, pattern, 6));
			v.push_back(only(G, "gate 30", {0, 0, 68, 20, 1, 100, 30, 120}, pattern, 8));
			v.push_back(only(G, "gate 0", {0, 0, 100, 20, 1, 100, 0, 120}, pattern, 8));
			v.push_back(only(G, "gate 100", {0, 0, 100, 20, 1, 100, 100, 120}, pattern, 8));
			v.push_back(only(G, "PRED 127", {0, 127, 68, 20, 1, 100, 127, 120}, pattern, 8));
			v.push_back(only(G, "PRED 3", {0, 3, 68, 20, 1, 100, 127, 120}, pattern, 8));
			v.push_back(only(G, "HP 127, LP 0", {0, 0, 80, 0, 127, 0, 127, 127}, noise, 3));
			v.push_back(only(G, "full-scale noise, return limited", {0, 0, 127, 0, 0, 127, 127, 127}, fullNoise, 10));
			{
				auto s = only(G, "DEC, DAMP, PRED, GATE moved", {0, 0, 68, 20, 1, 100, 120, 110}, pattern, 10);
				auto b = s.bytes;
				b.gate[md::fast::GateDec] = 120;
				b.gate[md::fast::GatePred] = 40;
				s.moves.push_back({600, b, Tempo});
				b.gate[md::fast::GateDamp] = 90;
				b.gate[md::fast::GateGate] = 50;
				s.moves.push_back({1500, b, Tempo});
				v.push_back(s);
			}
			{
				S s;
				s.name = "both: the Echo into the Gate Box, DVOL 127";
				s.bytes = {{127, 0, 90, 20, 1, 100, 127, 120}, {16, 40, 40, 50, 0, 100, 32, 110}};
				s.send = &pattern;
				s.seconds = 12;
				v.push_back(s);
			}
			return v;
		}();

		int failures = runScenarios(list, setup);

		{
			// What that scenario leaves of the machine: the DSP's boot code played as samples, here a click
			const auto lowest = std::find_if(list.begin(), list.end(), [](const Scenario& _s)
			{
				return _s.clearBootCode;
			});
			auto s = *lowest;
			s.clearBootCode = false;
			const auto r = run(s, setup);
			std::printf("the same with the oracle reading its boot code, which the port plays as zeros:\n");
			print("Echo", r.echo);
			if(exact(r.echo))
				throw std::runtime_error("the read below the line no longer reaches the boot code");
		}

		// The controls: the wrong effect must fail, on what a port could get wrong
		{
			const auto named = [&](const std::string& _name) -> const Scenario&
			{
				const auto found = std::find_if(list.begin(), list.end(), [&](const Scenario& _s)
				{
					return _s.name == _name;
				});
				if(found == list.end())
					throw std::runtime_error("no scenario " + _name);
				return *found;
			};
			const auto spoiled = [&](const char* _name, const Scenario& _base, auto _spoil)
			{
				auto s = _base;
				s.name = _name;
				s.spoil = _spoil;
				const auto r = run(s, setup);
				const bool seen = s.toEcho ? !exact(r.echo) && !meetsQ2(r.echo) : !meetsQ2(r.gate);
				std::printf("control, %s: %s\n", _name, seen ? "seen" : "NOT SEEN");
				print(s.toEcho ? "Echo" : "Gate Box", s.toEcho ? r.echo : r.gate);
				if(!seen)
					throw std::runtime_error(std::string("the harness did not see: ") + _name);
			};
			const auto& echoHits = named("Echo: defaults, Hits");
			const auto& gateHits = named("Gate Box: defaults, Hits");
			const auto& gate30 = named("Gate Box: gate 30");
			spoiled("the Echo's FB one step off", echoHits, [](EchoWords& _e, GateBoxWords&) { _e[4] += 1 << 16; });
			spoiled("the Echo's distance one word off", echoHits, [](EchoWords& _e, GateBoxWords&) { _e[0] += 1; });
			spoiled("the Gate Box's decay a row off", gateHits, [](EchoWords&, GateBoxWords& _g) { _g[2] += 1; });
			spoiled("the Gate Box's gate 10 blocks late", gate30, [](EchoWords&, GateBoxWords& _g) { _g[5] += 10; });

			// The silent return: a null of nothing against nothing must not pass
			auto silent = named("Echo: defaults, Hits");
			silent.name = "control, a send to nothing";
			silent.bytes.echo[md::fast::EchoLev] = 0;
			if(exact(run(silent, setup).echo))
				throw std::runtime_error("the harness passed a silent return");
			std::printf("control, a silent return: seen\n");
		}

		checkDenormals(setup);
		checkNoAllocation(setup);
		measureCost(setup);

		if(failures)
		{
			std::printf("FAIL: %d scenarios miss their threshold\n", failures);
			return 1;
		}
		std::printf("PASS: the Rhythm Echo sample for sample, the Gate Box within Q2, against the mixer DSP\n");
		return 0;
	}
	catch(const std::exception& e)
	{
		std::printf("FAIL: %s\n", e.what());
		return 1;
	}
}
