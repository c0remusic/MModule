#include "mdDrumsSampleFile.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>
#include <memory>

namespace mdDrums
{
	namespace
	{
		// Longer than every Sample together can be at the lowest rate: refused before reading it all
		constexpr double MaxSeconds = 240.0;
	}

	std::variant<SampleAudio, std::string> readSampleFile(const juce::File& _file)
	{
		const auto name = _file.getFileName().toStdString();
		juce::AudioFormatManager formats;
		formats.registerBasicFormats();
		const std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(_file));
		if(!reader)
			return name + ": MoDule reads WAV, AIFF and FLAC files";
		const auto length = reader->lengthInSamples;
		const auto channels = static_cast<int>(reader->numChannels);
		if(length <= 0 || channels <= 0 || reader->sampleRate <= 0.0)
			return name + " holds no audio";
		if(static_cast<double>(length) / reader->sampleRate > MaxSeconds)
			return name + " is too long for a Sample";

		juce::AudioBuffer<float> buffer(channels, static_cast<int>(length));
		if(!reader->read(&buffer, 0, static_cast<int>(length), 0, true, true))
			return name + " could not be read";

		SampleAudio audio;
		audio.rate = reader->sampleRate;
		audio.mono.assign(static_cast<size_t>(length), 0.0f);
		for(int c = 0; c < channels; ++c)
		{
			const auto* s = buffer.getReadPointer(c);
			for(int64_t i = 0; i < length; ++i)
				audio.mono[static_cast<size_t>(i)] += s[i] / static_cast<float>(channels);
		}
		const auto& meta = reader->metadataValues;
		if(meta.getValue("NumSampleLoops", "0").getIntValue() > 0)
		{
			audio.loopStart = meta.getValue("Loop0Start", "-1").getLargeIntValue();
			audio.loopEnd = meta.getValue("Loop0End", "-1").getLargeIntValue();
		}

		// Above 48 kHz, to 44.1 kHz, through JUCE's resampler, which filters what would alias
		if(audio.rate > SampleAudio::MaxRate + 0.5)
		{
			const auto ratio = audio.rate / 44100.0;
			juce::AudioBuffer<float> in(1, static_cast<int>(length));
			in.copyFrom(0, 0, audio.mono.data(), static_cast<int>(length));
			juce::MemoryAudioSource source(in, false);
			juce::ResamplingAudioSource resampler(&source, false, 1);
			resampler.setResamplingRatio(ratio);
			constexpr int Block = 4096;
			resampler.prepareToPlay(Block, 44100.0);
			const auto outLength = static_cast<int64_t>(std::floor(static_cast<double>(length) / ratio));
			std::vector<float> out(static_cast<size_t>(outLength));
			juce::AudioBuffer<float> block(1, Block);
			for(int64_t at = 0; at < outLength; at += Block)
			{
				const auto n = static_cast<int>(std::min<int64_t>(Block, outLength - at));
				juce::AudioSourceChannelInfo info(&block, 0, n);
				resampler.getNextAudioBlock(info);
				std::copy(block.getReadPointer(0), block.getReadPointer(0) + n, out.begin() + at);
			}
			audio.mono = std::move(out);
			if(audio.loopStart >= 0)
			{
				audio.loopStart = static_cast<int64_t>(std::llround(static_cast<double>(audio.loopStart) / ratio));
				audio.loopEnd = static_cast<int64_t>(std::llround(static_cast<double>(audio.loopEnd) / ratio));
			}
			audio.rate = 44100.0;
		}
		return audio;
	}
}
