// Renders the machines made here against the flash image's, by hand, not a test, for listening:
// edDrumsHouseListen OUT_FOLDER. GEARMULATOR_MD_FIRMWARE_BIN names the image. What it writes stays out of the repository.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "edDrumsLibrary.h"
#include "edDrumsTestSupport.h"
#include "stFastMetrics.h"

using namespace elektron::drums;
namespace bench = syntakt::fast::bench;

namespace
{
	constexpr int Samples = 26460;

	std::vector<float> hit(const Library& _library, const uint8_t _machine, const std::array<uint8_t, 8>& _knobs)
	{
		auto e = std::make_unique<Engine>(_library);
		e->setMachine(0, {Family::Machinedrum, _machine});
		e->setLevel(0, 127);
		e->setChain(0, ChainSettings{});
		for(int i = 0; i < 8; ++i)
			e->setParameter(0, i, _knobs[static_cast<size_t>(i)]);
		test::Take take(Samples);
		take.render(*e, 0, 32);
		e->noteOn(0, 127);
		take.render(*e, 32, Samples - 32);
		return take.channel(0);
	}

	double power(const std::vector<float>& _x, const size_t _from, const size_t _to)
	{
		double sum = 0;
		for(size_t n = _from; n < _to; ++n)
			sum += _x[n] * _x[n];
		return sum / static_cast<double>(_to - _from);
	}

}

int main(const int _argc, char** _argv)
{
	if(_argc < 2)
		return 2;
	const std::string out = _argv[1];
	const auto image = Library::findFlashImage({});
	if(image.empty())
	{
		std::printf("no flash image\n");
		return 77;
	}
	const Library house;
	const Library real(image);
	const std::array<uint8_t, 8> neutral{64, 64, 64, 64, 64, 64, 64, 64};
	for(const auto& [machine, name] : {std::pair<uint8_t, const char*>{16, "BD"}, {18, "XT"}, {27, "XC"}})
		for(int set = 0; set < 7; ++set)
		{
			auto knobs = neutral;
			if(set > 0)
				knobs[static_cast<size_t>((set - 1) / 2)] = (set - 1) % 2 ? 127 : 16;
			const auto h = hit(house, machine, knobs);
			const auto r = hit(real, machine, knobs);
			const auto tag = std::string(name) + "_" + std::to_string(set);
			bench::writeWav(out + "/" + tag + "_house.wav", {h.begin(), h.end()}, 44100, 1, 0);
			bench::writeWav(out + "/" + tag + "_real.wav", {r.begin(), r.end()}, 44100, 1, 0);
			const size_t Hit = 32 + Engine::HitDelay, Attack = Hit + 400, Tail = Hit + 8000;
			std::printf("%s rms attack %.1f / %.1f dB, tail %.1f / %.1f dB (house / real)\n", tag.c_str(),
				bench::db(power(h, Hit, Attack)), bench::db(power(r, Hit, Attack)), bench::db(power(h, Attack, Tail)),
				bench::db(power(r, Attack, Tail)));
		}
	return 0;
}
