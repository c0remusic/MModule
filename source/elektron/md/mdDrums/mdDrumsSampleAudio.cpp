#include "mdDrumsSampleAudio.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace mdDrums
{
	namespace
	{
		std::string seconds(const double _s)
		{
			char text[32];
			std::snprintf(text, sizeof(text), "%.1f s", _s);
			return text;
		}
	}

	std::variant<Sample, std::string> makeSample(const SampleAudio& _audio, const std::vector<uint16_t>& _codes,
		const size_t _roomWords)
	{
		if(_codes.size() != 65536)
			return std::string("No 12-bit table: the flash image holds no sample bank");
		if(_audio.mono.empty())
			return std::string("The file holds no audio");
		if(_audio.rate < SampleAudio::MinRate - 0.5 || _audio.rate > SampleAudio::MaxRate + 0.5)
			return "The file plays at " + std::to_string(std::lround(_audio.rate)) + " Hz: a Sample plays at 4 to 48 kHz";

		// The loop, as the OS takes one: at least 151 samples, inside the audio; the Sample ends where it does
		const auto count = static_cast<int64_t>(_audio.mono.size());
		Sample sample;
		auto used = count;
		if(_audio.loopStart >= 0 && _audio.loopEnd > _audio.loopStart && _audio.loopEnd < count
			&& _audio.loopEnd - _audio.loopStart > 150)
		{
			sample.loop = static_cast<uint32_t>(_audio.loopStart);
			used = _audio.loopEnd + 1;
		}

		const auto words = static_cast<size_t>((used + 1) / 2);
		if(words > _roomWords)
		{
			// The room left in tenths of a second, down, never more than there is
			const auto left = std::floor(20.0 * static_cast<double>(_roomWords) / _audio.rate) / 10.0;
			return "The file lasts " + seconds(static_cast<double>(used) / _audio.rate) + ": the Samples have "
				+ seconds(left) + " left at its rate";
		}

		// To 16 bits as the values come, then the OS's 12-bit code of each, two a word, the first in the high half; an odd
		// last sample pairs with silence
		const auto code = [&](const int64_t _k) -> uint32_t
		{
			const auto v = _k < used ? _audio.mono[static_cast<size_t>(_k)] : 0.0f;
			const auto s = static_cast<int32_t>(std::lround(std::clamp(v, -1.0f, 1.0f) * 32767.0f));
			return _codes[static_cast<uint16_t>(static_cast<int16_t>(s))] & 0xfffu;
		};
		sample.words.resize(words);
		for(size_t w = 0; w < words; ++w)
			sample.words[w] = code(static_cast<int64_t>(2 * w)) << 12 | code(static_cast<int64_t>(2 * w + 1));
		sample.length = static_cast<uint32_t>(used);
		sample.rate = static_cast<uint32_t>(std::lround(_audio.rate * Sample::Unity / 44100.0));
		return sample;
	}
}
