#pragma once

#include "mdDrumsSampleAudio.h"

#include <string>
#include <variant>

namespace juce
{
	class File;
}

namespace mdDrums
{
	// A file read as a Sample's audio (ticket 09 of the Samples map): WAV, AIFF or FLAC, its channels mixed to mono (their
	// mean, so that a full-scale stereo file stays in range), at its own rate up to 48 kHz and taken down to 44.1 kHz
	// above, its loop the first of a WAV's smpl chunk, moved with the rate. An error that says why it cannot be read, or
	// that it lasts more than _maxSamples at its rate (the room left, checked before the file is decoded). On the message
	// thread, never the audio thread: it reads the whole file.
	std::variant<SampleAudio, std::string> readSampleFile(const juce::File& _file,
		size_t _maxSamples = 2 * (Samples::End - Samples::Base));
}
