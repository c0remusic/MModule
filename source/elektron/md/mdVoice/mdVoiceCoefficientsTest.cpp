// The OS's side of a voice ported to C++ (mdVoiceCoefficients) against the OS's own 68k code in Musashi
// (MachineRunner). First the tables, each from its formula against the OS's words. Then every ported coefficient
// function: each of its eight parameters over the OS's whole range, 0 to $3FFF, the others random, then all eight
// random, a trigger or not and, for the ROM machines, any tempo, every word of the packet compared. Then 16 voices
// without MachineRunner: the port's smoothing and coefficient functions into md::voice::Voice, against mdEngine's
// HostModel playing VoiceEngine, the same Hits, knobs, locks and machines, MID and CTR ones too, the product's own
// md::voice::Track driving them, the parameters, the samples and the state compared;
// the ROM machines' tables, the scratch and a stray pointer still come from the oracle's DSP, as in mdVoicePortTest.
// Last, the cost. The harness shows it finds a fault: a packet's type, count or word changed must fail. The filters'
// tables are mdVoicePortTest's to check.
// Its own process: Musashi serves one CPU class per executable (CLAUDE.md). GEARMULATOR_MD_FIRMWARE_BIN names the flash
// image; exit 77 without it.
// usage: mdVoiceCoefficientsTest

#include "mdVoice.h"
#include "mdVoiceCoefficients.h"
#include "mdVoiceDsp.h"
#include "mdVoiceInternal.h"
#include "mdVoicePiImport.h"
#include "mdVoiceTrack.h"

#include "Firmware.h"
#include "HostModel.h"
#include "MachineRunner.h"
#include "VoiceEngine.h"

#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/memory.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
	using md::engine::HostModel;
	using md::engine::MachineRunner;
	using md::engine::VoiceEngine;
	using md::voice::OsTables;
	using md::voice::Packet;
	using md::voice::Smoother;
	using md::voice::Track;
	using md::voice::Voice;

	constexpr uint32_t kTempoAddr = 0x100150c;	// BPM x 24
	constexpr uint32_t kMinTempo = 720, kMaxTempo = 7200;
	constexpr int kVoices = VoiceEngine::kVoices;
	constexpr int kBlocks = 6000;
	constexpr uint8_t kRomFirst = 128, kRomSecond = 152;	// the two ROM functions' first machines
	static_assert(md::voice::kBlocksPerTick == HostModel<VoiceEngine>::kBlocksPerTick);

	std::vector<uint8_t> portedMachines()
	{
		std::vector<uint8_t> m;
		for(int id = 0; id < 256; ++id)
			if(md::voice::coefficientsPorted(static_cast<uint8_t>(id)))
				m.push_back(static_cast<uint8_t>(id));
		return m;
	}

	template<size_t N>
	void checkOsTable(const char* _name, const std::array<uint32_t, N>& _table, const MachineRunner& _os,
		const uint32_t _addr)
	{
		for(uint32_t k = 0; k < N; ++k)
			if(_table[k] != _os.peek32(_addr + 4 * k))
				throw std::runtime_error(std::string(_name) + " differs from the OS's at word " + std::to_string(k));
	}

	void checkTables(const OsTables& _tables, const MachineRunner& _os)
	{
		checkOsTable("the pitch table", _tables.pitch, _os, 0x24bd94);
		checkOsTable("the decay table", _tables.decay, _os, 0x24bf94);
		checkOsTable("the rise table", _tables.rise, _os, 0x24c794);
		checkOsTable("the octaves table", _tables.octaves, _os, 0x24d794);
		checkOsTable("the curve table", _tables.curve, _os, 0x24bb90);
		checkOsTable("the imported one-pole table", _tables.onePole, _os, md::voice::PiBank::OnePoleAddr);
		checkOsTable("P-I-MA's chance", _tables.maChance, _os, 0x2462e8);
		checkOsTable("P-I-MA's grains", _tables.maGrains, _os, 0x2464e8);
		checkOsTable("P-I-MA's decay", _tables.maDecay, _os, 0x2466e8);
		checkOsTable("P-I-MA's cosine", _tables.maCosine, _os, 0x2468e8);
		std::printf("the tables from their formulas: pitch 128, decay 512, rise 512, octaves 128, curve 128, P-I-MA's "
			"four of 128, 0 differences\n");
	}

	// A Lock's snap holds until something else moves the target, which the scenario below never shows
	void checkSnap()
	{
		Smoother smoother;
		smoother.setTarget(0, 10);
		smoother.snap(0, 100);
		smoother.tick();
		if(smoother.values()[0] != 100 << 7)
			throw std::runtime_error("a snap did not take the target");
	}

	// Every machine with a render has its function, as ticket 11's engine needs of them, and none without
	void checkMachines()
	{
		for(int id = 0; id < 255; ++id)
			if(md::voice::coefficientsPorted(static_cast<uint8_t>(id)) != Voice::ported(static_cast<uint32_t>(id) + 1))
				throw std::runtime_error("machine " + std::to_string(id) + " has a render or a function, not both");
	}

	using OsWords = std::array<uint32_t, VoiceEngine::kSlotWords>;

	// The OS's function and the port on the same parameters, every word, as HostModel makes the OS's a slot: the first
	// word that differs, -2 for the count, or -1
	int firstDifference(const OsWords& _os, const int _osCount, const uint8_t _machine, const bool _trigger,
		const Packet& _port, const int _portCount)
	{
		if(std::clamp(_osCount, 1, VoiceEngine::kSlotWords) != _portCount)
			return -2;
		if(_port[0] != (_trigger ? static_cast<uint32_t>(_machine) + 1 : 0))
			return 0;
		for(size_t k = 1; k < _os.size(); ++k)
			if(_os[k] != (k < _port.size() ? _port[k] : 0))
				return static_cast<int>(k);
		return -1;
	}

	// Each parameter over the whole range, the others random; then all random. Machines that share a function (the ROM
	// ones) get the sweep once and the random part each.
	void checkFunctions(MachineRunner& _os, const OsTables& _tables)
	{
		int64_t packets = 0;
		const auto check = [&](const uint8_t _machine, const std::array<uint16_t, 8>& _params, const bool _trigger,
			const uint32_t _tempo)
		{
			_os.poke32(kTempoAddr, _tempo);
			OsWords os{};
			const int n = _os.compute(_machine, _params.data(), os.data(), VoiceEngine::kSlotWords, _trigger);
			if(n < 0)
				throw std::runtime_error("the OS's function faulted on machine " + std::to_string(_machine));
			Packet port;
			const int count = md::voice::coefficients(_tables, _machine, _params.data(), _trigger, _tempo, port);
			const int d = firstDifference(os, n, _machine, _trigger, port, count);
			if(d != -1)
			{
				std::string where = "machine " + std::to_string(_machine) + ", trigger " + std::to_string(_trigger)
					+ ", tempo " + std::to_string(_tempo) + ", count " + std::to_string(n) + " and "
					+ std::to_string(count) + ", difference at word " + std::to_string(d) + ", parameters";
				for(const auto p : _params)
					where += " " + std::to_string(p);
				throw std::runtime_error(where);
			}
			++packets;
		};
		std::mt19937 random(0x68000);
		const auto param = [&] { return static_cast<uint16_t>(random() & 0x3fff); };
		const auto tempo = [&] { return kMinTempo + random() % (kMaxTempo - kMinTempo + 1); };
		for(const uint8_t machine : portedMachines())
		{
			const bool rom = machine >= kRomFirst;
			std::array<uint16_t, 8> p{};
			if(!rom || machine == kRomFirst || machine == kRomSecond)
				for(size_t k = 0; k < p.size(); ++k)
					for(uint32_t v = 0; v <= 0x3fff; ++v)
					{
						for(auto& w : p)
							w = param();
						p[k] = static_cast<uint16_t>(v);
						check(machine, p, random() & 1, tempo());
					}
			for(int n = 0; n < (rom ? 2000 : 20000); ++n)
			{
				for(auto& w : p)
					w = random() % 4 == 0 ? static_cast<uint16_t>((random() % 128) << 7) : param();
				check(machine, p, random() & 1, tempo());
			}
		}
		// The extremes of the tempo
		std::array<uint16_t, 8> p{};
		for(const uint32_t t : {kMinTempo, kMaxTempo})
			for(int n = 0; n < 20000; ++n)
			{
				for(auto& w : p)
					w = param();
				check(n & 1 ? kRomFirst : kRomSecond, p, random() & 1, t);
			}
		std::printf("%lld packets of %zu machines compared word for word, 0 differences\n",
			static_cast<long long>(packets), portedMachines().size());

		// A word, the type and the count of the port's changed on purpose must be found
		const std::array<uint16_t, 8> some = {1 << 7, 2 << 7, 3 << 7, 4 << 7, 5 << 7, 6 << 7, 7 << 7, 8 << 7};
		OsWords os{};
		const int n = _os.compute(16, some.data(), os.data(), VoiceEngine::kSlotWords, false);
		Packet port;
		const int count = md::voice::coefficients(_tables, 16, some.data(), false, 3000, port);
		port[5] ^= 1;
		const bool word = firstDifference(os, n, 16, false, port, count) == 5;
		port[5] ^= 1;
		port[0] ^= 1;
		const bool type = firstDifference(os, n, 16, false, port, count) == 0;
		port[0] ^= 1;
		if(!word || !type || firstDifference(os, n, 16, false, port, count + 1) != -2)
			throw std::runtime_error("the harness did not find a change made on purpose");
	}

	template<size_t N>
	void fill(std::array<uint32_t, N>& _a, const dsp56k::Memory& _mem, const dsp56k::EMemArea _area,
		const uint32_t _base)
	{
		for(uint32_t k = 0; k < N; ++k)
			_a[k] = _mem.get(_area, _base + k);
	}

	void checkVoices(const md::fw::FlashOs& _os, const std::vector<uint8_t>& _flash, const OsTables& _tables)
	{
		VoiceEngine oracle(_os.firmware);
		MachineRunner runner(_os.osImage);
		HostModel<VoiceEngine> host(runner, oracle);

		const auto& memory = oracle.dsp().memory();
		md::voice::RomTables rom;
		fill(rom.interpolator, memory, dsp56k::MemArea_Y, 0x163);
		fill(rom.decimator, memory, dsp56k::MemArea_X, 0x244);
		const auto uw = md::fw::loadRomBankFromFlash(_flash);
		std::vector<uint32_t> directory(md::voice::RomBank::DirectoryWords, 0);
		for(const auto& e : uw.directory)
			std::copy(std::begin(e.words), std::end(e.words),
				directory.begin() + (e.addr - md::fw::RomBank::DirectoryAddr));
		oracle.writeP(md::voice::RomBank::DirectoryAddr, directory.data(), directory.size());
		oracle.writeP(md::voice::RomBank::DataAddr, uw.data.data(), uw.data.size());
		const md::voice::RomBank bank{directory.data(), uw.data.data(), uw.data.size()};
		md::voice::TrxTables trx;
		md::voice::importTrx(_os.firmware, trx);

		// The stray pointer of a reversed ROM voice reads what the oracle's memory holds, as mdVoicePortTest has it
		md::voice::testHooks::hooks().stray = [](const void* _user, const md::voice::testHooks::Area _area,
			const uint32_t _addr) -> uint32_t
		{
			const auto& mem = *static_cast<const dsp56k::Memory*>(_user);
			return mem.get(_area == md::voice::testHooks::AreaX ? dsp56k::MemArea_X : dsp56k::MemArea_Y, _addr);
		};
		md::voice::testHooks::hooks().strayUser = &memory;

		// The E12 machines' samples as mdVoicePortTest takes them
		const auto e12 = md::fw::dspWords(_os.firmware.dspA, md::voice::E12Bank::Addr, md::voice::E12Bank::Words);
		const auto pi = md::voice::importPi(_os);
		auto owned = std::make_unique<md::voice::Shared>();
		auto& shared = *owned;
		shared.tables = &rom;
		shared.bank = &bank;
		shared.trx = &trx;
		shared.e12 = e12.data();
		shared.pi = pi.data();
		fill(shared.scratch.x, memory, dsp56k::MemArea_X, 0);
		fill(shared.scratch.y, memory, dsp56k::MemArea_Y, 0);
		std::vector<Voice> voices;
		for(int v = 0; v < kVoices; ++v)
			voices.emplace_back(shared, static_cast<uint32_t>(v));
		std::array<Track, kVoices> tracks{};

		std::mt19937 random(0x10002e0);
		// The ported machines and the MID and CTR ones, whose Hit stops the voice
		auto machines = portedMachines();
		for(int id = 0x60; id <= 0x7b; ++id)
			if(runner.machine(static_cast<uint8_t>(id)))
				machines.push_back(static_cast<uint8_t>(id));
		uint32_t tempo = 3000;
		std::array<int64_t, 256> sounding{};
		int64_t hits = 0, knobs = 0, locks = 0;
		VoiceEngine::Block expected{};
		for(int block = 0; block < kBlocks; ++block)
		{
			if(block % 500 == 0)
			{
				tempo = (60 + random() % 200) * 24;
				host.setTempo(tempo / 24.0);
			}
			for(int v = 0; v < kVoices; ++v)
			{
				auto& track = tracks[static_cast<size_t>(v)];
				const auto event = random() % 64;
				if(event == 0)
				{
					const auto id = machines[random() % machines.size()];
					host.setMachine(v, id);
					track.setMachine(id, runner.machine(id)->defaults);
				}
				else if(event == 1)
				{
					// Locks before or after their Hit, in its block
					const bool before = random() & 1;
					const int locked = random() % 3 ? 0 : 1 + static_cast<int>(random() % 3);
					std::array<std::pair<int, int>, 3> values{};
					for(int k = 0; k < locked; ++k)
						values[static_cast<size_t>(k)] = {static_cast<int>(random() % 8), static_cast<int>(random() % 128)};
					const auto lock = [&]
					{
						for(int k = 0; k < locked; ++k)
						{
							const auto [p, value] = values[static_cast<size_t>(k)];
							host.lock(v, p, value);
							track.lock(static_cast<size_t>(p), static_cast<uint8_t>(value));
							++locks;
						}
					};
					if(before)
						lock();
					host.trigger(v, 100);
					track.hit();
					if(!before)
						lock();
					++hits;
				}
				else if(event < 6)
				{
					const int p = static_cast<int>(random() % 8), value = static_cast<int>(random() % 128);
					host.knob(v, p, value);
					track.knob(static_cast<size_t>(p), static_cast<uint8_t>(value));
					++knobs;
				}
				else if(event == 6)
				{
					// A lock with no Hit in its block, unless the Hit comes by chance: dropped
					const int p = static_cast<int>(random() % 8), value = static_cast<int>(random() % 128);
					host.lock(v, p, value);
					track.lock(static_cast<size_t>(p), static_cast<uint8_t>(value));
				}
			}
			md::voice::prepareBlock(tracks, voices, _tables, tempo, block, 0xffffu, 0xffffu);
			if(!host.renderBlock(expected))
				throw std::runtime_error("the voice DSP faulted: " + oracle.faultReason());
			for(int v = 0; v < kVoices; ++v)
			{
				const auto i = static_cast<size_t>(v);
				const auto where = "block " + std::to_string(block) + ", voice " + std::to_string(v) + ", machine "
					+ std::to_string(tracks[i].machine());
				const auto& values = tracks[i].values();
				const auto* const params = host.voiceParams(v);
				for(size_t p = 0; p < values.size(); ++p)
					if(values[p] != params[p])
						throw std::runtime_error("parameter " + std::to_string(p) + " at " + where + ": the OS's "
							+ std::to_string(params[p]) + ", the port's " + std::to_string(values[p]));
				Voice::Block got{};
				voices[i].render(got);
				for(size_t f = 0; f < got.size(); ++f)
				{
					if(got[f] != expected[i][f])
						throw std::runtime_error("frame " + std::to_string(f) + " at " + where + ": the oracle's "
							+ std::to_string(expected[i][f]) + ", the port's " + std::to_string(got[f]));
					sounding[tracks[i].machine()] += got[f] != 0;
				}
				const auto base = VoiceEngine::kSlotBase + VoiceEngine::kSlotStride * static_cast<uint32_t>(v);
				for(uint32_t w = 0; w < Voice::kStateWords; ++w)
					if(voices[i].state()[w] != memory.get(dsp56k::MemArea_Y, base + w))
						throw std::runtime_error("state word " + std::to_string(w) + " at " + where);
			}
		}
		md::voice::testHooks::hooks() = {};
		int silent = 0;
		for(const uint8_t m : machines)
			silent += m != 0 && (m < 176) && md::voice::isAudioMachine(m) && sounding[m] == 0;
		std::printf("%d blocks of %d voices without MachineRunner, %lld Hits, %lld knobs, %lld locks: parameters, samples "
			"and state the same; %d machines of %zu never sounded\n", kBlocks, kVoices, static_cast<long long>(hits),
			static_cast<long long>(knobs), static_cast<long long>(locks), silent, machines.size());
		if(hits == 0 || knobs == 0 || locks == 0 || silent > 0)
			throw std::runtime_error("a machine played nothing, which proves nothing");
	}

	// What the OS's side of one voice costs, a tick being 11 blocks of 32 samples, here and in Musashi
	void measure(MachineRunner& _os, const OsTables& _tables)
	{
		using Clock = std::chrono::steady_clock;
		const auto machines = portedMachines();
		constexpr int kRounds = 300;
		std::mt19937 random(1);
		std::vector<std::array<uint16_t, 8>> params(256);
		for(auto& p : params)
			for(auto& w : p)
				w = static_cast<uint16_t>(random() & 0x3fff);
		uint32_t sink = 0;
		// A pass's ns per call, the median of 9
		const auto median = [](const auto& _pass, const double _calls)
		{
			std::vector<double> runs;
			for(int pass = 0; pass < 9; ++pass)
			{
				const auto t0 = Clock::now();
				_pass();
				runs.push_back(std::chrono::duration<double, std::nano>(Clock::now() - t0).count());
			}
			std::sort(runs.begin(), runs.end());
			return runs[runs.size() / 2] / _calls;
		};
		const auto everyMachine = [&](const auto& _call)
		{
			for(int r = 0; r < kRounds; ++r)
				for(const uint8_t m : machines)
					_call(m, params[static_cast<size_t>(r) % params.size()]);
		};
		const double packets = kRounds * static_cast<double>(machines.size());
		const double port = median([&]
		{
			everyMachine([&](const uint8_t _m, const std::array<uint16_t, 8>& _p)
			{
				Packet packet;
				md::voice::coefficients(_tables, _m, _p.data(), false, 3000, packet);
				sink += packet[1];
			});
		}, packets);
		const double os = median([&]
		{
			everyMachine([&](const uint8_t _m, const std::array<uint16_t, 8>& _p)
			{
				uint32_t out[VoiceEngine::kSlotWords];
				_os.compute(_m, _p.data(), out, VoiceEngine::kSlotWords, false);
				sink += out[1];
			});
		}, packets);
		Smoother smoother;
		for(size_t p = 0; p < Smoother::kParams; ++p)
			smoother.setTarget(p, static_cast<uint8_t>(p * 5));
		const double smooth = median([&]
		{
			for(int r = 0; r < 100000; ++r)
			{
				smoother.tick();
				sink += smoother.values()[static_cast<size_t>(r) % Smoother::kParams];
			}
		}, 100000);
		const double tickNs = md::voice::kBlocksPerTick * Voice::kBlockFrames / 44100.0 * 1e9;
		std::printf("a packet: %.0f ns (Musashi %.0f ns), a Track's smoothing: %.0f ns, per tick; a voice: %.4f %% of "
			"a core, 16 voices %.3f %% (Musashi's packets alone: %.3f %%) [%u]\n", port, os, smooth,
			(port + smooth) / tickNs * 100, 16 * (port + smooth) / tickNs * 100, 16 * os / tickNs * 100, sink & 1);
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
		MachineRunner runner(os.osImage);
		const auto pi = md::voice::importPi(os);
		const OsTables tables(pi.data());
		checkTables(tables, runner);
		checkMachines();
		checkSnap();
		checkFunctions(runner, tables);
		checkVoices(os, flash, tables);
		measure(runner, tables);
		std::printf("PASS: the OS's coefficient functions and smoothing run without Musashi, word for word and sample "
			"for sample\n");
		return 0;
	}
	catch(const std::exception& e)
	{
		std::printf("FAIL: %s\n", e.what());
		return 1;
	}
}
