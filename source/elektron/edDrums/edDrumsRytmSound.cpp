#include "edDrumsRytmSound.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace elektron::drums::rytm
{
	namespace
	{
		constexpr size_t Header = 10;		// F0, 00 20 3C, 07, device, dump id, version (2), number
		constexpr size_t Raw = 162;			// the sound decoded (FW1.70 on)
		constexpr size_t Packed = 186;		// ... 7-bit packed: 23 groups of 8 and one of 2
		constexpr size_t OldDumpSize = 207;	// FW1.50 to 1.61B, whose sound is laid out otherwise

		// The SAMPLE page's bytes in the sound decoded
		constexpr size_t Name = 0x0C, NameLength = 15;
		constexpr size_t Tune = 0x2C, Fine = 0x2E, Slot = 0x30, Br = 0x32, Start = 0x34, End = 0x36, Loop = 0x38,
			Level = 0x3A, Modes = 0x7D;

		int word(const std::array<uint8_t, Raw>& _raw, const size_t _at)
		{
			return _raw[_at] << 8 | _raw[_at + 1];
		}
	}

	std::optional<Sound> readSound(const uint8_t* _data, const size_t _size, std::string& _error)
	{
		static constexpr uint8_t Rytm[] = {0xF0, 0x00, 0x20, 0x3C, 0x07};
		if(_size < Header || !std::equal(std::begin(Rytm), std::end(Rytm), _data) || (_data[6] != 0x53
			&& _data[6] != 0x59))
		{
			_error = "not a Rytm sound";
			return std::nullopt;
		}
		if(_size == OldDumpSize)
		{
			_error = "a Rytm sound from an OS before 1.70";
			return std::nullopt;
		}
		if(_size != SoundDumpSize || _data[_size - 1] != 0xF7)
		{
			_error = "not a whole Rytm sound";
			return std::nullopt;
		}
		const uint8_t* packed = _data + Header;
		int sum = 0;
		for(size_t i = 0; i < Packed; ++i)
			sum += packed[i];
		const uint8_t* tail = packed + Packed;
		if((tail[0] << 7 | tail[1]) != (sum & 0x3FFF) || (tail[2] << 7 | tail[3]) != static_cast<int>(Packed + 5))
		{
			_error = "a Rytm sound whose checksum is wrong";
			return std::nullopt;
		}
		// Each group: a byte of the seven's top bits, the first's at bit 6, then their low seven bits
		std::array<uint8_t, Raw> raw{};
		size_t r = 0;
		for(size_t g = 0; g < Packed; g += 8)
		{
			const uint8_t msb = packed[g];
			for(size_t j = 0; j < 7 && g + 1 + j < Packed; ++j)
				raw[r++] = static_cast<uint8_t>(packed[g + 1 + j] | (msb >> (6 - j) & 1) << 7);
		}
		Sound s;
		// Printable ASCII only, as the Rytm writes it: a byte past it would not read as UTF-8
		for(size_t i = 0; i < NameLength && raw[Name + i]; ++i)
			if(raw[Name + i] >= 0x20 && raw[Name + i] < 0x7F)
				s.name.push_back(static_cast<char>(raw[Name + i]));
		s.tune = raw[Tune];
		s.fine = raw[Fine];
		s.sample = raw[Slot];
		s.bitReduction = raw[Br];
		s.start = word(raw, Start);
		s.end = word(raw, End);
		s.loop = raw[Loop] != 0;
		s.level = raw[Level];
		s.chromatic = (raw[Modes] >> 4 & 3) >= 2;
		return s;
	}

	std::vector<uint8_t> writeSound(const Sound& _sound, const uint8_t _id)
	{
		std::array<uint8_t, Raw> raw{};
		for(size_t i = 0; i < _sound.name.size() && i < NameLength; ++i)
			raw[Name + i] = static_cast<uint8_t>(_sound.name[i]);
		raw[Tune] = static_cast<uint8_t>(_sound.tune);
		raw[Fine] = static_cast<uint8_t>(_sound.fine);
		raw[Slot] = static_cast<uint8_t>(_sound.sample);
		raw[Br] = static_cast<uint8_t>(_sound.bitReduction);
		raw[Start] = static_cast<uint8_t>(_sound.start >> 8);
		raw[Start + 1] = static_cast<uint8_t>(_sound.start);
		raw[End] = static_cast<uint8_t>(_sound.end >> 8);
		raw[End + 1] = static_cast<uint8_t>(_sound.end);
		raw[Loop] = _sound.loop ? 1 : 0;
		raw[Level] = static_cast<uint8_t>(_sound.level);
		raw[Modes] = _sound.chromatic ? 2 << 4 : 0;
		std::vector<uint8_t> m{0xF0, 0x00, 0x20, 0x3C, 0x07, 0x00, _id, 0x01, 0x01, 0x00};
		int sum = 0;
		for(size_t g = 0; g < Raw; g += 7)
		{
			uint8_t msb = 0;
			for(size_t j = 0; j < 7 && g + j < Raw; ++j)
				msb = static_cast<uint8_t>(msb | (raw[g + j] >> 7) << (6 - j));
			m.push_back(msb);
			sum += msb;
			for(size_t j = 0; j < 7 && g + j < Raw; ++j)
			{
				m.push_back(raw[g + j] & 0x7F);
				sum += raw[g + j] & 0x7F;
			}
		}
		sum &= 0x3FFF;
		constexpr int size = static_cast<int>(Packed) + 5;
		m.insert(m.end(), {static_cast<uint8_t>(sum >> 7), static_cast<uint8_t>(sum & 0x7F),
			static_cast<uint8_t>(size >> 7 & 0x7F), static_cast<uint8_t>(size & 0x7F), 0xF7});
		return m;
	}

	LayerSettings layerOf(const Sound& _sound)
	{
		LayerSettings l;
		l.on = true;
		// The Rytm's pitch: (TUNE - 64) + (FINE - 64) / 64 semitones, within +-24; the layer's FINE counts 1/128 of one
		// a half semitone either way. The Rytm bounds it with the note added, which the layer's KEY TRACK adds after:
		// a chromatic sound played far from note 60 goes past +-24 here
		const double s = std::clamp((_sound.tune - 64) + (_sound.fine - 64) / 64.0, -24.0, 24.0);
		const double whole = std::floor(s + 0.5);
		l.tune = 64 + static_cast<int>(whole);
		l.fine = std::clamp(64 + static_cast<int>(std::lround((s - whole) * 128)), 0, 127);
		// STRT and END: fractions of 120 of the sample, END saturated; the layer's START and LENGTH are 127ths
		const auto fraction = [](const int _word) { return std::clamp(_word / (120.0 * 256), 0.0, 1.0); };
		const double a = fraction(_sound.start), b = fraction(_sound.end);
		const bool reverse = b < a;
		const auto from = static_cast<int>(std::lround(127 * std::min(a, b)));
		const auto to = static_cast<int>(std::lround(127 * std::max(a, b)));
		l.start = from;
		l.length = to - from;
		// PLAY's four modes, a quarter of the range each: FWD, REV, FWD LOOP, REV LOOP; LOOP 0 loops from the start
		l.play = 32 * ((reverse ? 1 : 0) + (_sound.loop ? 2 : 0));
		l.loop = 0;
		l.level = std::clamp(_sound.level, 0, 127);
		l.bitReduction = std::clamp(_sound.bitReduction, 0, 127);
		l.noteTrack = _sound.chromatic;
		return l;
	}
}
