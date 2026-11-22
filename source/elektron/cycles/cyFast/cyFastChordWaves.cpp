#include "cyFastChordWaves.h"

#include <fstream>
#include <iterator>

namespace cycles::fast
{
	namespace
	{
		constexpr uint32_t Load = 0x40000400;		// where section 3 sits in the address space
		constexpr uint32_t Pointers = 0x40118590;	// CHORD's 32 wave pointers
		constexpr uint32_t Boot = 0x4000045c;		// the boot's copy of the SRAM images

		uint64_t fnv1a64(const std::vector<uint8_t>& _bytes)
		{
			uint64_t h = 0xcbf29ce484222325ull;
			for(const uint8_t b : _bytes)
				h = (h ^ b) * 0x100000001b3ull;
			return h;
		}

		// A big-endian word or half-word at an address of the section
		uint32_t read32(const std::vector<uint8_t>& _s, const uint32_t _address)
		{
			const size_t i = _address - Load;
			return static_cast<uint32_t>(_s[i]) << 24 | static_cast<uint32_t>(_s[i + 1]) << 16
				| static_cast<uint32_t>(_s[i + 2]) << 8 | _s[i + 3];
		}
		uint32_t read16(const std::vector<uint8_t>& _s, const uint32_t _address)
		{
			const size_t i = _address - Load;
			return static_cast<uint32_t>(_s[i]) << 8 | _s[i + 1];
		}

		// One SRAM image as the boot copies it: words from source to end at destination, then 0 to cleared
		struct Image
		{
			uint32_t source, end, destination, cleared;
		};

		// The boot's two loops (0x4000045c): lea source,a2; lea destination,a1; ... cmpa.l #end,a2; ... cmpa.l
		// #cleared,a0. Each instruction's opcode is checked before its operand is taken
		bool readImages(const std::vector<uint8_t>& _s, Image* _images, std::string& _error)
		{
			// Per image: the offsets from Boot of its four instructions and their opcodes
			constexpr uint32_t At[2][4]{{0x02, 0x08, 0x14, 0x22}, {0x2a, 0x30, 0x3c, 0x4a}};
			constexpr uint32_t Opcode[4]{0x45f9, 0x43f9, 0xb5fc, 0xb1fc};
			for(int k = 0; k < 2; ++k)
			{
				uint32_t operand[4];
				for(int j = 0; j < 4; ++j)
				{
					const uint32_t a = Boot + At[k][j];
					if(read16(_s, a) != Opcode[j])
					{
						_error = "the boot's SRAM copy is not where OS 1.13 has it";
						return false;
					}
					operand[j] = read32(_s, a + 2);
				}
				_images[k] = {operand[0], operand[2], operand[1], operand[3]};
			}
			return true;
		}
	}

	bool readChordWaves(const std::vector<uint8_t>& _section3, ChordWaves& _waves, std::string& _error)
	{
		if(_section3.size() != ChordSection3Size || fnv1a64(_section3) != ChordSection3Fnv)
		{
			_error = "not the Model:Cycles OS 1.13's section 3 (section_3_MAIN_OS.bin)";
			return false;
		}
		Image images[2];
		if(!readImages(_section3, images, _error))
			return false;
		const uint32_t end = Load + static_cast<uint32_t>(_section3.size());
		constexpr uint32_t Bytes = 4 * ChordWaves::Entries;
		for(int i = 0; i < ChordWaves::Count; ++i)
		{
			const uint32_t pointer = read32(_section3, Pointers + 4 * static_cast<uint32_t>(i));
			// The section's own address for the wave: in the section, or in an image's copied words
			uint32_t at = 0;
			if(pointer >= Load && pointer <= end - Bytes)
				at = pointer;
			for(const Image& m : images)
				if(pointer >= m.destination && pointer - m.destination <= m.end - m.source - Bytes)
					at = m.source + (pointer - m.destination);
			if(!at)
			{
				_error = "wave " + std::to_string(i + 1) + " is not in the section's data";
				return false;
			}
			_waves.address[static_cast<size_t>(i)] = pointer;
			for(uint32_t n = 0; n < ChordWaves::Entries; ++n)
			{
				const auto word = static_cast<int32_t>(read32(_section3, at + 4 * n));
				_waves.wave[static_cast<size_t>(i)][n] = static_cast<float>(word / 2147483648.0);
			}
		}
		return true;
	}

	bool loadChordWaves(const std::string& _path, ChordWaves& _waves, std::string& _error)
	{
		std::ifstream f(_path, std::ios::binary);
		if(!f)
		{
			_error = "cannot read " + _path;
			return false;
		}
		const std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
		return readChordWaves(bytes, _waves, _error);
	}
}
