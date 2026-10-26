// The voice DSP's port (md::voice::Voice) against mdEngine's VoiceEngine, OS 1.63's own program on the dsp56300
// emulator: the same slots into both, Hit by Hit, then every block of the 16 voices compared sample for sample, and
// every voice's 64-word state block word for word (a count limited when stored back changes no sample for millions of
// them, only the word).
// Two phases, the same each run. First the slots as HostModel's tick writes them: every voice's packet from the OS's
// own coefficient functions (MachineRunner, Musashi) on every block, with Hits, knob changes and machine changes on
// the ported machines. Then hand-made packets at the edges the OS's functions never give: counts across blocks,
// negative, $800000, and samples at full scale. The run must reach every edge it names, or it proves nothing. The
// harness then shows it finds a fault: one of the port's samples changed must fail at that sample.
// Its own process: Musashi serves one CPU class per executable (CLAUDE.md). GEARMULATOR_MD_FIRMWARE_BIN names the
// flash image; exit 77 without it.
// usage: mdVoicePortTest

#include "mdVoice.h"

#include "Firmware.h"
#include "MachineRunner.h"
#include "VoiceEngine.h"

#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/memory.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	using md::engine::VoiceEngine;
	using md::voice::Voice;

	constexpr int kVoices = VoiceEngine::kVoices;
	constexpr int kFrames = Voice::kBlockFrames;
	constexpr int kOsBlocks = 4000;		// 2.9 s at 44.1 kHz from the OS's functions
	constexpr int kEdgeBlocks = 1000;	// then the hand-made packets
	constexpr int kBlocks = kOsBlocks + kEdgeBlocks;

	// The machines the first phase plays: those md::voice ports (machine id = type - 1)
	constexpr std::array<uint8_t, 2> kMachines = {0, 3};	// GND--, GND-IM
	constexpr uint32_t kGndIm = 4;

	// GND-IM's packet words at the edges: its two counts, then its two samples
	constexpr std::array<uint32_t, 8> kEdgeCounts = {0, 1, 31, 32, 33, 100, 0x800000, 0xffffff};
	constexpr std::array<uint32_t, 6> kEdgeSamples = {0, 1, 0x7fffff, 0x800000, 0xffffff, 0x123456};

	// What both gave, block after block: [block][voice][frame] and [block][voice][word]
	struct Recording
	{
		std::vector<int32_t> oracle, port;
		std::vector<uint32_t> oracleState, portState;
		int hits = 0;
		std::array<int64_t, 5> sounding{};	// the oracle's non-zero samples by type
		int64_t negative = 0;				// the oracle's samples below zero
		int64_t carried = 0;				// non-zero samples in a block without a Hit: a count across blocks
	};

	struct Location
	{
		int block = 0, voice = 0, index = 0;
	};

	std::optional<Location> firstDifference(const std::vector<int32_t>& _a, const std::vector<int32_t>& _b, const int _width)
	{
		const auto [ia, ib] = std::mismatch(_a.begin(), _a.end(), _b.begin());
		if(ia == _a.end())
			return std::nullopt;
		const auto i = static_cast<int>(ia - _a.begin());
		return Location{i / (kVoices * _width), i / _width % kVoices, i % _width};
	}

	Recording record(const md::fw::FlashOs& _os)
	{
		VoiceEngine oracle(_os.firmware);
		md::engine::MachineRunner runner(_os.osImage);
		std::array<Voice, kVoices> port;

		std::mt19937 random(0x0d0e2);
		std::array<uint8_t, kVoices> machine{};
		std::array<uint32_t, kVoices> type{};
		std::array<std::array<uint16_t, 24>, kVoices> params{};
		Recording rec;
		VoiceEngine::Block expected{};
		for(int block = 0; block < kBlocks; ++block)
		{
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
				}
				else if(hit[v])
				{
					words[0] = kGndIm;
					words[1] = kEdgeCounts[random() % kEdgeCounts.size()];
					words[2] = kEdgeSamples[random() % kEdgeSamples.size()];
					words[3] = kEdgeCounts[random() % kEdgeCounts.size()];
					words[4] = kEdgeSamples[random() % kEdgeSamples.size()];
					count = 5;
				}
				if(count == 0)
					continue;
				oracle.setSlot(v, words, count);
				port[static_cast<size_t>(v)].setSlot(words, count);
				if(hit[v])
					type[v] = words[0];
				rec.hits += hit[v];
			}
			if(!oracle.renderBlock(expected))
				throw std::runtime_error("the voice DSP faulted: " + oracle.faultReason());
			for(int v = 0; v < kVoices; ++v)
			{
				Voice::Block got{};
				port[static_cast<size_t>(v)].render(got);
				const auto& want = expected[static_cast<size_t>(v)];
				rec.oracle.insert(rec.oracle.end(), want.begin(), want.end());
				rec.port.insert(rec.port.end(), got.begin(), got.end());
				for(const int32_t s : want)
				{
					rec.sounding[type[v]] += s != 0;
					rec.negative += s < 0;
					rec.carried += s != 0 && !hit[v];
				}
				const auto base = VoiceEngine::kSlotBase + VoiceEngine::kSlotStride * static_cast<uint32_t>(v);
				for(int w = 0; w < Voice::kStateWords; ++w)
					rec.oracleState.push_back(oracle.dsp().memory().get(dsp56k::MemArea_Y, base + static_cast<uint32_t>(w)));
				const auto& state = port[static_cast<size_t>(v)].state();
				rec.portState.insert(rec.portState.end(), state.begin(), state.end());
			}
		}
		return rec;
	}

	std::vector<int32_t> asSigned(const std::vector<uint32_t>& _words)
	{
		return {_words.begin(), _words.end()};
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
		const auto rec = record(md::fw::loadFirmwareFromFlash(md::fw::readFile(path)));
		std::printf("%d blocks of %d voices, %d Hits; GND-IM sounding samples %lld, below zero %lld, across blocks %lld\n",
			kBlocks, kVoices, rec.hits, static_cast<long long>(rec.sounding[kGndIm]),
			static_cast<long long>(rec.negative), static_cast<long long>(rec.carried));
		if(rec.sounding[kGndIm] == 0 || rec.negative == 0 || rec.carried == 0)
		{
			std::printf("FAIL: the scenario misses an edge it names, which proves nothing\n");
			return 1;
		}

		if(const auto d = firstDifference(rec.oracle, rec.port, kFrames))
		{
			const auto i = static_cast<size_t>((d->block * kVoices + d->voice) * kFrames + d->index);
			std::printf("FAIL: block %d, voice %d, frame %d: the oracle gives %d, the port %d\n", d->block, d->voice,
				d->index, rec.oracle[i], rec.port[i]);
			return 1;
		}
		if(const auto d = firstDifference(asSigned(rec.oracleState), asSigned(rec.portState), Voice::kStateWords))
		{
			const auto i = static_cast<size_t>((d->block * kVoices + d->voice) * Voice::kStateWords + d->index);
			std::printf("FAIL: block %d, voice %d, state word %d: the oracle holds $%06x, the port $%06x\n", d->block,
				d->voice, d->index, rec.oracleState[i], rec.portState[i]);
			return 1;
		}

		// A sample of the port changed on purpose: the comparison must find it where it is
		const Location corrupt{kBlocks / 2, 7, 13};
		auto faulty = rec.port;
		faulty[static_cast<size_t>((corrupt.block * kVoices + corrupt.voice) * kFrames + corrupt.index)] ^= 1;
		const auto found = firstDifference(rec.oracle, faulty, kFrames);
		if(!found || found->block != corrupt.block || found->voice != corrupt.voice || found->index != corrupt.index)
		{
			std::printf("FAIL: the harness did not find the sample changed at block %d, voice %d, frame %d\n",
				corrupt.block, corrupt.voice, corrupt.index);
			return 1;
		}
		std::printf("a sample changed on purpose is found at block %d, voice %d, frame %d\n", corrupt.block,
			corrupt.voice, corrupt.index);
		std::printf("PASS: the port matches the voice DSP sample for sample and word for word\n");
		return 0;
	}
	catch(const std::exception& e)
	{
		std::printf("FAIL: %s\n", e.what());
		return 1;
	}
}
