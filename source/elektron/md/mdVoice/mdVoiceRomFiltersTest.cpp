// The ROM machines on our own interpolator and decimator (md::voice::RomTables) against the same machines on the voice
// DSP's: the only two tables with no formula, which a product must not read from the OS, so that what they change is
// measured here rather than proved equal. ROM-01 to -32, a Hit of 2 s at the machine's defaults and at three random
// settings, BRR at 0. The limits are a guard against regressions, at what these filters measure, and no verdict on the
// sound, which belongs to listening (ticket 08): the envelope, and each third octave to 10 kHz within 40 dB of the
// loudest. The bands above 10 kHz are written, not judged. BRR is written, not judged too: it quantizes, so that a step
// taken a sample earlier moves a tail by decibels whatever the filter. A control shows the measure sees a change: the
// render on a decimator of half the taps' gain must fail.
// GEARMULATOR_MD_FIRMWARE_BIN names the flash image; exit 77 without it.
// usage: mdVoiceRomFiltersTest

#include "mdVoice.h"
#include "mdVoiceCoefficients.h"
#include "mdVoicePiImport.h"

#include "Firmware.h"
#include "MachineRunner.h"
#include "VoiceEngine.h"

#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/memory.h"

#include "stFastMetrics.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <numeric>
#include <random>
#include <stdexcept>
#include <vector>

namespace
{
	using md::voice::Voice;
	using namespace syntakt::fast::bench;

	constexpr double kRate = 44100;
	constexpr int kBlocks = 2 * 44100 / Voice::kBlockFrames;
	constexpr double kEnvelopeLimit = 0.6, kBandLimit = 0.6;
	// Third octaves around 1 kHz: 31 Hz to 10 kHz, then to 18 kHz
	constexpr int kFirstBand = -15, kLastJudged = 10, kLastBand = 12;
	constexpr size_t kBrr = 3;		// a ROM machine's parameters: PTCH, DEC, HOLD, BRR


	std::vector<double> hit(const md::voice::RomTables& _rom, const md::voice::RomBank& _bank,
		const md::voice::OsTables& _os, const uint8_t _machine, const std::array<uint16_t, 8>& _params)
	{
		md::voice::Shared shared{&_rom, &_bank, {}, nullptr};
		Voice voice(shared, 0);
		std::vector<double> out;
		out.reserve(static_cast<size_t>(kBlocks) * Voice::kBlockFrames);
		for(int block = 0; block < kBlocks; ++block)
		{
			md::voice::Packet packet;
			if(block % md::voice::kBlocksPerTick == 0)
			{
				const int n = md::voice::coefficients(_os, _machine, _params.data(), block == 0, 3000, packet);
				voice.setSlot(packet.data(), n);
			}
			Voice::Block frames{};
			voice.render(frames);
			for(const int32_t s : frames)
				out.push_back(s / 8388608.0);
		}
		return out;
	}

	struct Worst
	{
		double envelope = 0, band = 0, high = 0;
		int hits = 0;
	};

	Worst compare(const md::voice::RomTables& _ours, const md::voice::RomTables& _dsp, const md::voice::RomBank& _bank,
		const md::voice::OsTables& _os, const md::engine::MachineRunner& _runner, const bool _reduced)
	{
		Worst worst;
		std::mt19937 random(0x14600);
		for(int machine = 128; machine < 160; ++machine)
			for(int setting = 0; setting < 4; ++setting)
			{
				std::array<uint16_t, 8> params{};
				const auto& defaults = _runner.machine(static_cast<uint8_t>(machine))->defaults;
				for(size_t p = 0; p < params.size(); ++p)
					params[p] = static_cast<uint16_t>((setting == 0 ? defaults[p] : random() % 128) << 7);
				params[kBrr] = _reduced ? static_cast<uint16_t>((40 + random() % 88) << 7) : 0;
				const auto ref = hit(_dsp, _bank, _os, static_cast<uint8_t>(machine), params);
				if(energy(ref) == 0)
					continue;
				const auto test = hit(_ours, _bank, _os, static_cast<uint8_t>(machine), params);
				++worst.hits;
				// The measures below are blind to the polarity, which a mix hears
				if(std::inner_product(ref.begin(), ref.end(), test.begin(), 0.0) <= 0)
					throw std::runtime_error("our filters turn a Hit upside down");
				worst.envelope = std::max(worst.envelope, envelopeDiff(envelope(ref, kRate), envelope(test, kRate)));
				const auto a = bands(ref, kRate, kFirstBand, kLastBand);
				const auto b = bands(test, kRate, kFirstBand, kLastBand);
				const double total = *std::max_element(a.begin(), a.end());
				for(size_t k = 0; k < a.size(); ++k)
				{
					if(a[k] < total * 1e-4)		// 40 dB under the loudest band
						continue;
					auto& w = static_cast<int>(k) + kFirstBand <= kLastJudged ? worst.band : worst.high;
					w = std::max(w, std::abs(db(b[k] / a[k])));
				}
			}
		return worst;
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
		md::engine::VoiceEngine oracle(os.firmware);
		md::engine::MachineRunner runner(os.osImage);
		const auto& memory = oracle.dsp().memory();

		md::voice::RomTables dsp;
		for(uint32_t k = 0; k < dsp.interpolator.size(); ++k)
			dsp.interpolator[k] = memory.get(dsp56k::MemArea_Y, 0x163 + k);
		for(uint32_t k = 0; k < dsp.decimator.size(); ++k)
			dsp.decimator[k] = memory.get(dsp56k::MemArea_X, 0x244 + k);

		const auto uw = md::fw::loadRomBankFromFlash(flash);
		std::vector<uint32_t> directory(md::voice::RomBank::DirectoryWords, 0);
		for(const auto& e : uw.directory)
			std::copy(std::begin(e.words), std::end(e.words),
				directory.begin() + (e.addr - md::fw::RomBank::DirectoryAddr));
		const md::voice::RomBank bank{directory.data(), uw.data.data(), uw.data.size()};
		const auto pi = md::voice::importPi(os);
		const md::voice::OsTables tables(pi.data());

		const md::voice::RomTables ours;
		const auto w = compare(ours, dsp, bank, tables, runner, false);
		std::printf("%d Hits, worst: envelope %.3f dB (limit %.1f), third octave to 10 kHz %.3f dB (limit %.1f), 10 to "
			"18 kHz %.3f dB, not judged\n", w.hits, w.envelope, kEnvelopeLimit, w.band, kBandLimit, w.high);

		const auto r = compare(ours, dsp, bank, tables, runner, true);
		std::printf("with BRR, not judged: %d Hits, envelope %.3f dB, third octave %.3f dB\n", r.hits, r.envelope,
			r.band);

		// The control: the measure must see a filter that is not ours
		md::voice::RomTables halved = ours;
		for(auto& t : halved.decimator)
			t = (t & 0x800000 ? t | 0xff000000 : t) >> 1 & 0xffffff;
		const auto c = compare(halved, dsp, bank, tables, runner, false);
		std::printf("the control, the decimator's taps halved: envelope %.3f dB, third octave %.3f dB\n", c.envelope,
			c.band);
		if(c.envelope <= kEnvelopeLimit || c.band <= kBandLimit)
			throw std::runtime_error("the measure does not see a filter changed on purpose");

		if(w.hits == 0 || w.envelope > kEnvelopeLimit || w.band > kBandLimit)
			throw std::runtime_error("our filters are past their limits");
		std::printf("PASS: the ROM machines on our filters stay within the limits\n");
		return 0;
	}
	catch(const std::exception& e)
	{
		std::printf("FAIL: %s\n", e.what());
		return 1;
	}
}
