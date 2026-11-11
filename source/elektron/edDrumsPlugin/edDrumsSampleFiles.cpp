#include "edDrumsSampleFiles.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace elektron::drums
{
	std::variant<Sample, std::string> readSampleFile(const juce::File& _file)
	{
		const auto name = _file.getFileName().toStdString();
		juce::AudioFormatManager formats;
		formats.registerBasicFormats();
		const std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(_file));
		if(!reader)
			return name + ": the Sampler reads WAV, AIFF and FLAC files";
		const auto channels = static_cast<int>(reader->numChannels);
		const double from = reader->sampleRate;
		if(reader->lengthInSamples <= 0 || channels <= 0 || from <= 0.0)
			return name + " holds no audio";

		// What the Sampler can play of it, read alone: a few samples more for the resampler
		const double rate = from > Sample::MaxRate + 0.5 ? 44100.0 : from;
		const double ratio = from / rate;
		const auto frames = static_cast<int>(std::min<int64_t>(reader->lengthInSamples,
			static_cast<int64_t>(std::ceil(Sample::MaxLength * ratio)) + 16));
		juce::AudioBuffer<float> buffer(channels, frames);
		if(!reader->read(&buffer, 0, frames, 0, true, true))
			return name + " could not be read";
		std::vector<float> mono(static_cast<size_t>(frames), 0.0f);
		for(int c = 0; c < channels; ++c)
		{
			const float* s = buffer.getReadPointer(c);
			for(int n = 0; n < frames; ++n)
				mono[static_cast<size_t>(n)] += s[n] / static_cast<float>(channels);
		}

		// Above 48 kHz, to 44.1 kHz by Lagrange interpolation, which filters nothing: what lies above 22 kHz aliases
		if(ratio != 1.0)
		{
			const auto length = static_cast<int>(std::floor(frames / ratio));
			std::vector<float> out(static_cast<size_t>(length));
			juce::LagrangeInterpolator interpolator;
			interpolator.process(ratio, mono.data(), out.data(), length);
			mono = std::move(out);
		}
		return makeSample(name, mono, rate);
	}
}
