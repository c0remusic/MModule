#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "edDrumsLayer.h"

// A sound of the Analog Rytm MKII imported into the sample layer (ticket 21 of .scratch/elektron-drums-v2, spec v2 §7).
// The sound's SysEx dump (SOUND 0x53, or 0x59 for the work buffer) is read as bsp2/libanalogrytm (MIT) describes it:
// 201 bytes, the 162-byte sound 7-bit packed in groups of eight, a 14-bit checksum and length. Only its SAMPLE page is
// read, which OS 1.74 holds in the same order and widths in a track's parameter words: TUNE, FINE, SMP, BR, STRT, END,
// LOOP, LEV, and the chromatic mode. The dump holds the sample's slot on the Rytm's +Drive, never its name or its data:
// the sample comes from the user's Locker.
namespace elektron::drums::rytm
{
	struct Sound
	{
		std::string name;
		int tune = 64, fine = 64;	// semitones from 64; FINE 1/64 of one from 64
		int sample = 0;				// the +Drive's slot, 0 for none
		int bitReduction = 0;
		int start = 0, end = 120 << 8;	// value << 8, 0 to 120, the low byte a fraction
		bool loop = false;
		int level = 100;
		bool chromatic = false;		// the sample follows the note (CHROMATIC SAMPLE or SYN+SMP)
	};

	constexpr size_t SoundDumpSize = 201;

	// The sound of a dump, or nothing and why (for the user)
	std::optional<Sound> readSound(const uint8_t* _data, size_t _size, std::string& _error);
	// Its dump as the Rytm writes one (_id 0x53, or 0x59 for the work buffer), the rest of the sound 0: for the tests
	std::vector<uint8_t> writeSound(const Sound& _sound, uint8_t _id = 0x53);

	// The layer that plays the sound as the Rytm plays its sample: TUNE and FINE within the Rytm's +-24 semitones; STRT
	// and END (fractions of 120 of the sample) as START and LENGTH, END before STRT reversed, LOOP going round the
	// portion; LEV and BR as they are (the layer's laws are the Rytm's); the envelope open, no offset. Its end without
	// LOOP is the Rytm's too, which the importer gives the layer apart (SampleLayers::setRytmEnd)
	LayerSettings layerOf(const Sound& _sound);
}
