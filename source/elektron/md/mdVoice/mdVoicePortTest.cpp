// The voice DSP's port (md::voice::Voice) against mdEngine's VoiceEngine, OS 1.63's own program on the dsp56300
// emulator: the same slots into both, Hit by Hit, then every block of the 16 voices compared sample for sample, and
// every voice's 64-word state blocks word for word, Y:S and X:S (a count limited when stored back changes no sample for
// millions of them, only the word).
// Two phases, the same each run. First the slots as HostModel's tick writes them: every voice's packet from the OS's
// own coefficient functions (MachineRunner, Musashi) on every block, with Hits, knob changes and machine changes on
// the ported machines, the ROM ones playing the flash image's UW bank. Then hand-made packets at the edges the OS's
// functions never give or rarely reach: counts across blocks, negative, $800000, samples at full scale, and the ROM
// machines' pitch at its extremes, reversed, started past the end, retriggered, ended mid-block and looped, on samples
// added to the bank: two of full-scale codes, four of 0 to 3 samples, and one that gets shorter under the voices
// playing it. The run must reach every edge it names, and the ROM render's branches its probes count, or it proves
// nothing. The harness then shows it finds a fault: one of the port's samples, and one of its state words, changed must
// fail there. Last, on the port alone (the DSP would stall): a loop shorter than a block's advance renders silence.
// Its own process: Musashi serves one CPU class per executable (CLAUDE.md). GEARMULATOR_MD_FIRMWARE_BIN names the
// flash image; exit 77 without it.
// usage: mdVoicePortTest

#include "mdVoice.h"
#include "mdVoiceInternal.h"

#include "Firmware.h"
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
#include <map>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	using md::engine::VoiceEngine;
	using md::voice::Voice;
	namespace hooks = md::voice::testHooks;

	constexpr int kVoices = VoiceEngine::kVoices;
	constexpr int kFrames = Voice::kBlockFrames;
	constexpr int kOsBlocks = 4000;		// 2.9 s at 44.1 kHz from the OS's functions
	constexpr int kEdgeBlocks = 1000;	// then the hand-made packets
	constexpr int kBlocks = kOsBlocks + kEdgeBlocks;

	// The machines the first phase plays: those md::voice ports (machine id = type - 1). ROM-01, -02, -17 and -32 hold
	// factory samples, ROM-33 and -46 none
	constexpr std::array<uint8_t, 8> kMachines = {0, 3, 128, 129, 144, 159, 176, 189};	// GND--, GND-IM, ROM-..
	constexpr uint32_t kGndIm = 4;
	bool isRom(const uint32_t _type)
	{
		return (_type >= 129 && _type <= 160) || (_type >= 177 && _type <= 192);
	}
	// A ROM type's directory entry
	uint32_t entry(const uint32_t _type)
	{
		return _type - 129;
	}

	// GND-IM's packet words at the edges: its two counts, then its two samples
	constexpr std::array<uint32_t, 8> kEdgeCounts = {0, 1, 31, 32, 33, 100, 0x800000, 0xffffff};
	constexpr std::array<uint32_t, 6> kEdgeSamples = {0, 1, 0x7fffff, 0x800000, 0xffffff, 0x123456};

	// The samples the harness adds to the bank: ROM-41 to -44 of 0 to 3 samples, ROM-45 of 600 that becomes 200 and
	// back every 50 blocks of the edge phase, ROM-47 of 45 (no loop) and ROM-48 of 600 looping from 100, all of
	// full-scale codes
	constexpr uint32_t kTinyType = 185, kShrinkType = 189, kShortType = 191, kLoopType = 192;
	constexpr uint32_t kShrinkLength = 600, kShrunkLength = 200, kShortLength = 45, kLoopLength = 600, kLoopStart = 100;
	// The ROM machines the edge phase plays: ROM-01 and -32 (factory), ROM-33 (empty) and the harness's
	constexpr std::array<uint32_t, 10> kEdgeRoms = {129, 160, 177, 185, 186, 187, 188, 189, 191, 192};

	// The ROM packet's words 1-9 (PTCH's step, the decay after HOLD, STRT, HOLD, END, BRR's two, RTRG, RTIM): the edge
	// phase draws each from the lowest and highest the OS gave in the first phase and from these, past them where the
	// DSP takes it. Not past the OS's END ($400000, the sample's end) nor its STRT ($7F0000): a position past the
	// sample's length makes the forward fill's count negative, a loop of 2^24 that stalls the DSP (and the oracle).
	constexpr std::array<std::array<uint32_t, 3>, 10> kRomEdgeWords = {{
		{},
		{0, 1, 0x7fffff},				// stopped, nearly, and the top step
		{0x800000, 0xffffff, 0xc00000},	// no decay, all at once
		{0, 0x7f0000, 0x400000},
		{0, 0x7fffff, 1},				// no HOLD, for ever
		{0, 0x400000, 0x200000},
		{0x000008, 0x200000, 0x7fffff},
		{0x000020, 0x7fa800, 0x7fffff},
		{0, 1, 0x7f0000},				// no retrigger, one, for ever
		{1, 0x44, 0x7fffff},			// a retrigger every block
	}};

	// The probes the run must reach (all but Stall, which the oracle cannot survive)
	constexpr std::array<const char*, hooks::ProbeCount> kProbeNames = {"forward fill's odd first sample",
		"reverse fill's even first sample", "last sample held", "reversed stop at the start",
		"reversed retrigger with an odd first part", "stall"};

	// What both gave, block after block: [block][voice][frame] and [block][voice][word]
	struct Recording
	{
		std::vector<int32_t> oracle, port;
		std::vector<uint32_t> oracleState, portState;	// Y:S then X:S of each voice
		int hits = 0;
		std::array<int64_t, 256> sounding{};	// the oracle's non-zero samples by type
		int64_t negative = 0;					// the oracle's samples below zero
		int64_t carried = 0;					// GND-IM's non-zero samples in a block without a Hit: across blocks
		std::map<std::string, int64_t> edges;	// the ROM edges reached, each a count
		std::array<int64_t, hooks::ProbeCount> probes{};
		int64_t osStrayReads = 0, strayReads = 0;	// words read through the stray hook, in the first phase and all
		std::array<uint32_t, 10> wordMax{};		// the highest ROM words the OS gave
	};

	struct Location
	{
		int block = 0, voice = 0, index = 0;
	};

	std::optional<Location> firstDifference(const std::vector<int32_t>& _a, const std::vector<int32_t>& _b,
		const int _width)
	{
		const auto [ia, ib] = std::mismatch(_a.begin(), _a.end(), _b.begin());
		if(ia == _a.end())
			return std::nullopt;
		const auto i = static_cast<int>(ia - _a.begin());
		return Location{i / (kVoices * _width), i / _width % kVoices, i % _width};
	}

	int32_t signExtend(const uint32_t _w)
	{
		return static_cast<int32_t>(_w << 8) >> 8;
	}

	// The tables the ROM machines read, as the voice DSP holds them after its init
	md::voice::RomTables romTables(VoiceEngine& _oracle)
	{
		auto& mem = _oracle.dsp().memory();
		md::voice::RomTables t;
		for(uint32_t k = 0; k < t.decode.size(); ++k)
			t.decode[k] = mem.get(dsp56k::MemArea_Y, 0x146000 + k);
		for(uint32_t k = 0; k < t.interpolator.size(); ++k)
			t.interpolator[k] = mem.get(dsp56k::MemArea_Y, 0x163 + k);
		for(uint32_t k = 0; k < t.decimator.size(); ++k)
			t.decimator[k] = mem.get(dsp56k::MemArea_X, 0x244 + k);
		return t;
	}

	// A bank's directory and data, which md::voice::RomBank views
	struct Bank
	{
		std::vector<uint32_t> directory = std::vector<uint32_t>(md::voice::RomBank::DirectoryWords, 0);
		std::vector<uint32_t> data;
		md::voice::RomBank view() const { return {directory.data(), data.data(), data.size()}; }

		// A square of the highest and lowest codes, 8 samples a half, at a ROM type's entry
		void add(const md::voice::RomTables& _tables, const uint32_t _type, const uint32_t _length,
			const uint32_t _loop, const uint32_t _room = 0)
		{
			const auto [lo, hi] = std::minmax_element(_tables.decode.begin(), _tables.decode.end(),
				[](const uint32_t _a, const uint32_t _b) { return signExtend(_a) < signExtend(_b); });
			const auto top = static_cast<uint32_t>(hi - _tables.decode.begin());
			const auto bottom = static_cast<uint32_t>(lo - _tables.decode.begin());
			const auto addr = md::voice::RomBank::DataAddr + static_cast<uint32_t>(data.size());
			for(uint32_t k = 0; k < (std::max(_length, _room) + 1) / 2; ++k)
				data.push_back(k / 4 % 2 ? (bottom << 12) | bottom : (top << 12) | top);
			const uint32_t words[4] = {addr, _length, _loop, 0x40000};
			std::copy(std::begin(words), std::end(words), directory.begin() + 4 * entry(_type));
		}
	};

	// The flash image's UW bank, and the harness's samples after its data
	Bank makeBank(const md::fw::RomBank& _uw, const md::voice::RomTables& _tables)
	{
		Bank bank;
		for(const auto& e : _uw.directory)
		{
			const auto at = bank.directory.begin() + (e.addr - md::fw::RomBank::DirectoryAddr);
			std::copy(std::begin(e.words), std::end(e.words), at);
		}
		bank.data = _uw.data;
		for(uint32_t n = 0; n < 4; ++n)
			bank.add(_tables, kTinyType + n, n, 0xffffff);
		bank.add(_tables, kShrinkType, kShrinkLength, 0xffffff);
		bank.add(_tables, kShortType, kShortLength, 0xffffff);
		bank.add(_tables, kLoopType, kLoopLength, kLoopStart);
		return bank;
	}

	void loadBank(VoiceEngine& _oracle, const Bank& _bank)
	{
		_oracle.writeP(md::voice::RomBank::DirectoryAddr, _bank.directory.data(), _bank.directory.size());
		_oracle.writeP(md::voice::RomBank::DataAddr, _bank.data.data(), _bank.data.size());
	}

	Recording record(const md::fw::FlashOs& _os, const std::vector<uint8_t>& _flash)
	{
		VoiceEngine oracle(_os.firmware);
		const auto tables = romTables(oracle);
		auto bank = makeBank(md::fw::loadRomBankFromFlash(_flash), tables);
		if(bank.data.size() < 1000)
			throw std::runtime_error("the flash image holds no UW sample bank");
		loadBank(oracle, bank);
		const auto bankView = bank.view();

		// The words the port does not hold, a stray pointer's, as the emulator reads them: the address masked to its
		// 2M words (the oracle's memory after the block, which a stray pointer finds the same: samples or code)
		struct Stray
		{
			const dsp56k::Memory* memory;
			int64_t reads = 0;
		} stray{&oracle.dsp().memory()};
		Recording rec;
		auto& h = hooks::hooks();
		h.stray = [](const void* _user, const uint32_t _addr) -> uint32_t
		{
			auto& s = *const_cast<Stray*>(static_cast<const Stray*>(_user));
			++s.reads;
			return s.memory->get(dsp56k::MemArea_Y, _addr & 0x1fffff);
		};
		h.strayUser = &stray;
		h.probes = &rec.probes;

		// What the 16 voices share, its scratch from what the DSP's own init left in it
		md::voice::Shared shared{&tables, &bankView, {}};
		for(uint32_t a = 0; a < shared.scratch.x.size(); ++a)
			shared.scratch.x[a] = oracle.dsp().memory().get(dsp56k::MemArea_X, a);
		for(uint32_t a = 0; a < shared.scratch.y.size(); ++a)
			shared.scratch.y[a] = oracle.dsp().memory().get(dsp56k::MemArea_Y, a);

		md::engine::MachineRunner runner(_os.osImage);
		std::vector<Voice> port(kVoices, Voice(shared));

		std::mt19937 random(0x0d0e2);
		std::array<uint8_t, kVoices> machine{};
		std::array<uint32_t, kVoices> type{};
		std::array<std::array<uint16_t, 24>, kVoices> params{};
		std::array<std::array<uint32_t, 10>, kVoices> packet{};	// the ROM words of each voice's last Hit
		std::array<uint32_t, 10> wordMin, wordMax;
		wordMin.fill(0xffffff);
		wordMax.fill(0);
		std::array<std::array<uint32_t, Voice::kStateWords>, kVoices> before{};
		VoiceEngine::Block expected{};
		for(int block = 0; block < kBlocks; ++block)
		{
			if(block == kOsBlocks)
				rec.osStrayReads = stray.reads;
			// ROM-45's sample replaced by a shorter one and back, under the voices playing it
			if(block >= kOsBlocks && block % 50 == 0)
			{
				const uint32_t at = 4 * entry(kShrinkType) + 1;
				bank.directory[at] = block / 50 % 2 ? kShrunkLength : kShrinkLength;
				oracle.writeP(md::voice::RomBank::DirectoryAddr + at, &bank.directory[at], 1);
			}
			std::array<bool, kVoices> hit{};
			for(int v = 0; v < kVoices; ++v)
			{
				const auto event = random() % 64;
				hit[v] = event < 2;	// 0: a machine change, which only its Hit makes heard; 1: a Hit; 2: a knob turned
				uint32_t words[VoiceEngine::kSlotWords] = {};
				int count = 0;
				if(block < kOsBlocks)
				{
					if(event == 0)
						machine[v] = kMachines[random() % kMachines.size()];
					if(event < 3)
						for(auto& p : params[v])
							p = static_cast<uint16_t>((random() % 128) << 7);
					// Every block, as the tick rewrites every audio machine's packet
					const int n = runner.compute(machine[v], params[v].data(), words, VoiceEngine::kSlotWords, hit[v]);
					if(n < 0)
						throw std::runtime_error("a coefficient function faulted");
					count = std::clamp(n, 1, VoiceEngine::kSlotWords);
					words[0] = hit[v] ? static_cast<uint32_t>(machine[v]) + 1 : 0;
					if(isRom(static_cast<uint32_t>(machine[v]) + 1))
						for(int w = 1; w < 10; ++w)
						{
							wordMin[w] = std::min(wordMin[w], words[w] & 0xffffff);
							wordMax[w] = std::max(wordMax[w], words[w] & 0xffffff);
						}
				}
				else if(hit[v] && random() % 2)
				{
					words[0] = kGndIm;
					words[1] = kEdgeCounts[random() % kEdgeCounts.size()];
					words[2] = kEdgeSamples[random() % kEdgeSamples.size()];
					words[3] = kEdgeCounts[random() % kEdgeCounts.size()];
					words[4] = kEdgeSamples[random() % kEdgeSamples.size()];
					count = 5;
				}
				else if(hit[v] || (isRom(type[v]) && event == 2))
				{
					// A ROM machine's Hit, or a knob turned while it plays: each word an extreme
					words[0] = hit[v] ? kEdgeRoms[random() % kEdgeRoms.size()] : 0;
					for(int w = 1; w < 10; ++w)
					{
						const auto pick = random() % 5;
						words[w] = pick == 0 ? wordMin[w] : pick == 1 ? wordMax[w] : kRomEdgeWords[w][pick - 2];
					}
					count = 10;
				}
				if(count == 0)
					continue;
				oracle.setSlot(v, words, count);
				port[static_cast<size_t>(v)].setSlot(words, count);
				if(hit[v])
				{
					type[v] = words[0];
					std::copy(words, words + 10, packet[v].begin());
				}
				rec.hits += hit[v];
			}
			if(!oracle.renderBlock(expected))
				throw std::runtime_error("the voice DSP faulted at block " + std::to_string(block) + ": "
					+ oracle.faultReason());
			for(int v = 0; v < kVoices; ++v)
			{
				Voice::Block got{};
				port[static_cast<size_t>(v)].render(got);
				const auto& want = expected[static_cast<size_t>(v)];
				rec.oracle.insert(rec.oracle.end(), want.begin(), want.end());
				rec.port.insert(rec.port.end(), got.begin(), got.end());
				int64_t sounding = 0, fullScale = 0;
				for(const int32_t s : want)
				{
					rec.sounding[type[v]] += s != 0;
					sounding += s != 0;
					fullScale += s >= 0x600000 || s <= -0x600000;
					if(type[v] == kGndIm)
					{
						rec.negative += s < 0;
						rec.carried += s != 0 && !hit[v];
					}
				}
				const auto base = VoiceEngine::kSlotBase + VoiceEngine::kSlotStride * static_cast<uint32_t>(v);
				auto& mem = oracle.dsp().memory();
				std::array<uint32_t, Voice::kStateWords> y{};
				for(int w = 0; w < Voice::kStateWords; ++w)
					y[static_cast<size_t>(w)] = mem.get(dsp56k::MemArea_Y, base + static_cast<uint32_t>(w));
				rec.oracleState.insert(rec.oracleState.end(), y.begin(), y.end());
				for(int w = 0; w < Voice::kStateWords; ++w)
					rec.oracleState.push_back(mem.get(dsp56k::MemArea_X, base + static_cast<uint32_t>(w)));
				const auto& voice = port[static_cast<size_t>(v)];
				rec.portState.insert(rec.portState.end(), voice.state().begin(), voice.state().end());
				rec.portState.insert(rec.portState.end(), voice.xState().begin(), voice.xState().end());

				// The ROM edges, read in the oracle's state
				auto& prev = before[static_cast<size_t>(v)];
				if(isRom(type[v]))
				{
					const auto sx = signExtend;
					const auto& p = packet[v];
					const bool edge = block >= kOsBlocks;
					const std::string phase = edge ? "edge " : "os ";
					if(hit[v] && bank.directory[4 * entry(type[v]) + 1] == 0 && sounding == 0)
						++rec.edges[phase + "empty slot silent"];
					if(sounding && sx(y[0x21]) < 0)
						++rec.edges[phase + "reversed"];
					if(prev[0x22] == 0 && y[0x22] == 0x810625 && sounding)
						++rec.edges[phase + "ended mid-block"];
					if(y[0x1b] > prev[0x1b] && !hit[v])
						++rec.edges[phase + "retriggered"];
					if(!hit[v] && sx(y[0x21]) >= 0 && sounding && sx(y[0x10]) < sx(prev[0x10]) && sx(y[0x14]) >= 0
						&& y[0x1b] == prev[0x1b])
						++rec.edges[phase + "looped"];
					if(hit[v] && p[3] > p[5])
						++rec.edges[phase + "started past the end"];
					if(sounding && edge && p[1] >= wordMax[1])
						++rec.edges["edge highest pitch sounding"];
					if(sounding && edge && p[1] <= 1)
						++rec.edges["edge lowest pitch sounding"];
					if(fullScale && (type[v] == kShortType || type[v] == kLoopType))
						++rec.edges[phase + "full scale"];
					if(sounding && type[v] > kTinyType && type[v] < kTinyType + 4)
						++rec.edges[phase + "1 to 3 samples sounding"];
					if(type[v] == kShrinkType && y[0x13] < y[0x1c])
						++rec.edges[phase + "sample shortened under the voice"];
				}
				prev = y;
			}
		}
		std::printf("the OS's ROM words 1-9, lowest and highest:");
		for(int w = 1; w < 10; ++w)
			std::printf(" $%06x-$%06x", wordMin[w], wordMax[w]);
		std::printf("\n");
		rec.strayReads = stray.reads;
		rec.wordMax = wordMax;
		h = {};
		return rec;
	}

	std::vector<int32_t> asSigned(const std::vector<uint32_t>& _words)
	{
		return {_words.begin(), _words.end()};
	}

	// The port alone: a loop shorter than a block's advance at the top step (10 samples, then 0: a loop start at the
	// length), where the DSP's fill would count 2^24 passes, must render the stalled blocks silent and at once
	bool stallsSilent(const md::voice::RomTables& _tables, const std::array<uint32_t, 10>& _wordMax)
	{
		bool ok = true;
		for(const uint32_t loopStart : {590u, 600u})
		{
			Bank bank;
			bank.add(_tables, 129, 600, loopStart);
			const auto view = bank.view();
			md::voice::Shared shared{&_tables, &view, {}};
			Voice voice(shared);
			std::array<int64_t, hooks::ProbeCount> probes{};
			hooks::hooks().probes = &probes;
			const uint32_t words[10] = {129, 0x7fffff, 0x800000, 0, 0x7fffff, 0x400000, _wordMax[6], _wordMax[7], 0,
				0x7fffff};
			voice.setSlot(words, 10);
			int64_t sounded = 0, stalled = 0, loud = 0;
			double slowest = 0;
			for(int block = 0; block < 400; ++block)
			{
				const auto before = probes[hooks::Stall];
				Voice::Block out{};
				const auto t0 = std::chrono::steady_clock::now();
				voice.render(out);
				slowest = std::max(slowest, std::chrono::duration<double, std::milli>(
					std::chrono::steady_clock::now() - t0).count());
				const bool silent = std::all_of(out.begin(), out.end(), [](const int32_t _s) { return _s == 0; });
				sounded += !silent;
				if(probes[hooks::Stall] > before)
				{
					++stalled;
					loud += !silent;
				}
			}
			hooks::hooks().probes = nullptr;
			std::printf("loop start %u of 600 at the top step: %lld blocks sounding, %lld stalled (%lld of them not "
				"silent), slowest block %.3f ms\n", loopStart, static_cast<long long>(sounded),
				static_cast<long long>(stalled), static_cast<long long>(loud), slowest);
			ok &= sounded > 0 && stalled > 0 && loud == 0 && slowest < 2.0;
		}
		return ok;
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
		const auto rec = record(os, flash);
		int64_t rom = 0;
		for(uint32_t t = 0; t < rec.sounding.size(); ++t)
			rom += isRom(t) ? rec.sounding[t] : 0;
		std::printf("%d blocks of %d voices, %d Hits; GND-IM sounding samples %lld, below zero %lld, across blocks "
			"%lld; ROM sounding samples %lld\n", kBlocks, kVoices, rec.hits,
			static_cast<long long>(rec.sounding[kGndIm]), static_cast<long long>(rec.negative),
			static_cast<long long>(rec.carried), static_cast<long long>(rom));
		std::printf("words read through a stray pointer: %lld in the OS's phase, %lld in all\n",
			static_cast<long long>(rec.osStrayReads), static_cast<long long>(rec.strayReads));
		static const char* const named[] = {"os empty slot silent", "os reversed", "os ended mid-block",
			"os retriggered", "os started past the end", "edge empty slot silent", "edge reversed",
			"edge ended mid-block", "edge retriggered", "edge looped", "edge started past the end",
			"edge highest pitch sounding", "edge lowest pitch sounding", "edge full scale",
			"edge 1 to 3 samples sounding", "edge sample shortened under the voice"};
		bool missed = rec.sounding[kGndIm] == 0 || rec.negative == 0 || rec.carried == 0 || rom == 0;
		for(const char* name : named)
		{
			const auto it = rec.edges.find(name);
			const auto n = it == rec.edges.end() ? 0 : it->second;
			std::printf("  ROM %s: %lld\n", name, static_cast<long long>(n));
			missed |= n == 0;
		}
		for(int p = 0; p < hooks::ProbeCount; ++p)
		{
			std::printf("  ROM probe %s: %lld\n", kProbeNames[static_cast<size_t>(p)],
				static_cast<long long>(rec.probes[static_cast<size_t>(p)]));
			missed |= p != hooks::Stall && rec.probes[static_cast<size_t>(p)] == 0;
		}
		if(missed)
		{
			std::printf("FAIL: the scenario misses an edge it names, which proves nothing\n");
			return 1;
		}
		if(rec.probes[hooks::Stall] != 0)
		{
			std::printf("FAIL: the port stalled where the DSP did not\n");
			return 1;
		}

		if(const auto d = firstDifference(rec.oracle, rec.port, kFrames))
		{
			const auto i = static_cast<size_t>((d->block * kVoices + d->voice) * kFrames + d->index);
			std::printf("FAIL: block %d, voice %d, frame %d: the oracle gives %d, the port %d\n", d->block, d->voice,
				d->index, rec.oracle[i], rec.port[i]);
			return 1;
		}
		constexpr int kStateWidth = 2 * Voice::kStateWords;
		const auto oracleState = asSigned(rec.oracleState);
		if(const auto d = firstDifference(oracleState, asSigned(rec.portState), kStateWidth))
		{
			const auto i = static_cast<size_t>((d->block * kVoices + d->voice) * kStateWidth + d->index);
			std::printf("FAIL: block %d, voice %d, state word %c:S+$%02x: the oracle holds $%06x, the port $%06x\n",
				d->block, d->voice, d->index < Voice::kStateWords ? 'Y' : 'X', d->index % Voice::kStateWords,
				rec.oracleState[i], rec.portState[i]);
			return 1;
		}

		// A sample and a state word of the port changed on purpose: the comparisons must find them where they are
		const Location corrupt{kBlocks / 2, 7, 13};
		auto faulty = rec.port;
		faulty[static_cast<size_t>((corrupt.block * kVoices + corrupt.voice) * kFrames + corrupt.index)] ^= 1;
		const auto found = firstDifference(rec.oracle, faulty, kFrames);
		auto faultyState = asSigned(rec.portState);
		faultyState[static_cast<size_t>((corrupt.block * kVoices + corrupt.voice) * kStateWidth + Voice::kStateWords
			+ corrupt.index)] ^= 1;
		const auto foundState = firstDifference(oracleState, faultyState, kStateWidth);
		if(!found || found->block != corrupt.block || found->voice != corrupt.voice || found->index != corrupt.index
			|| !foundState || foundState->block != corrupt.block || foundState->voice != corrupt.voice
			|| foundState->index != Voice::kStateWords + corrupt.index)
		{
			std::printf("FAIL: the harness did not find the sample or the X state word changed at block %d, voice %d,"
				" index %d\n", corrupt.block, corrupt.voice, corrupt.index);
			return 1;
		}
		std::printf("a sample and an X state word changed on purpose are found at block %d, voice %d, index %d\n",
			corrupt.block, corrupt.voice, corrupt.index);

		VoiceEngine engine(os.firmware);
		if(!stallsSilent(romTables(engine), rec.wordMax))
		{
			std::printf("FAIL: a loop shorter than a block's advance did not render the stalled blocks silent\n");
			return 1;
		}
		std::printf("PASS: the port matches the voice DSP sample for sample and word for word\n");
		return 0;
	}
	catch(const std::exception& e)
	{
		std::printf("FAIL: %s\n", e.what());
		return 1;
	}
}
