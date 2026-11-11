#pragma once

#include <string>
#include <variant>

#include "edDrumsSamples.h"

namespace juce
{
	class File;
}

namespace elektron::drums
{
	// A WAV, AIFF or FLAC file as the Sampler's sample (makeSample): its channels' mean, at its own rate up to 48 kHz,
	// taken down to 44.1 kHz above, cut to Sample::MaxSourceLength; named after the file. An error that says why otherwise.
	// Off the audio thread: it reads the file
	std::variant<Sample, std::string> readSampleFile(const juce::File& _file);
}
