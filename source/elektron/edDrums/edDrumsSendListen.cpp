// Renders a beat through our chorus, phaser and flanger, by hand, not a test, for listening against OsTIrus's:
// edDrumsSendListen OUT_FOLDER. GEARMULATOR_MD_FIRMWARE_BIN names the image. What it writes stays out of the
// repository.
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "edDrumsLibrary.h"
#include "edDrumsTestSupport.h"
#include "stFastMetrics.h"

using namespace elektron::drums;
namespace bench = syntakt::fast::bench;

namespace
{
	constexpr int Samples = 44100 * 6, Step = 5512;		// sixteenths at 120 BPM

	struct Take
	{
		const char* name;
		Send send;
		std::vector<std::pair<int, uint8_t>> bytes;
	};

	void render(const Library& _library, const Take& _take, const std::string& _folder)
	{
		auto e = std::make_unique<Engine>(_library);
		const uint8_t machines[3] = {16, 17, 22};		// TRX-BD, SD, CH
		for(int t = 0; t < 3; ++t)
		{
			e->setMachine(t, {Family::Machinedrum, machines[t]});
			e->setLevel(t, 110);
			e->setChain(t, ChainSettings{});
			e->setSend(t, _take.send, 127);
		}
		auto& fx = e->sends();
		for(const auto& [byte, value] : _take.bytes)
			if(_take.send == Send::Chorus)
				fx.setChorus(byte, value);
			else
				fx.setPhaser(byte, value);

		test::Take take(Samples);
		for(int at = 0, step = 0; at < Samples - Step; at += Step, ++step)
		{
			if(step % 8 == 0 || step % 16 == 10)
				e->noteOn(0, 120);
			if(step % 8 == 4)
				e->noteOn(1, 110);
			if(step % 2 == 0)
				e->noteOn(2, 80);
			take.render(*e, at, Step);
		}
		std::vector<float> stereo(2 * static_cast<size_t>(Samples), 0.0f);
		for(int c = 0; c < 2; ++c)
			for(size_t n = 0; n < static_cast<size_t>(Samples); ++n)
				stereo[2 * n + static_cast<size_t>(c)] = take.channel(c)[n];
		bench::writeWav(_folder + "/" + _take.name + ".wav", {stereo.begin(), stereo.end()}, 44100, 0.5, 0, 2);
	}
}

int main(const int _argc, char** _argv)
{
	if(_argc < 2)
		return 2;
	const auto image = Library::findFlashImage({});
	if(image.empty())
	{
		std::printf("no flash image\n");
		return 77;
	}
	const Library library(image);
	const std::vector<Take> takes = {
		{"dry", Send::Chorus, {}},
		{"chorus_slow", Send::Chorus,
			{{ChorusRate, 64}, {ChorusDepth, 64}, {ChorusDelay, 40}, {ChorusFeedback, 64}, {ChorusLevel, 90}}},
		{"chorus_fast", Send::Chorus,
			{{ChorusRate, 92}, {ChorusDepth, 40}, {ChorusDelay, 16}, {ChorusFeedback, 64}, {ChorusLevel, 90}}},
		{"phaser", Send::Phaser,
			{{PhaserMode, 0}, {PhaserStages, 4}, {PhaserRate, 60}, {PhaserDepth, 90}, {PhaserFrequency, 48},
				{PhaserSpread, 90}, {PhaserFeedback, 96}, {PhaserLevel, 100}}},
		{"flanger", Send::Phaser,
			{{PhaserMode, 1}, {PhaserRate, 50}, {PhaserDepth, 120}, {PhaserFrequency, 64}, {PhaserFeedback, 104},
				{PhaserLevel, 100}}},
	};
	for(const auto& take : takes)
		render(library, take, _argv[1]);
	return 0;
}
