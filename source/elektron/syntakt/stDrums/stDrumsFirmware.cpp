#include "stDrumsFirmware.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

#include "stSection7.h"

namespace stDrums
{
	namespace
	{
		constexpr size_t MessageBody = 126;		// between F0 and F7: a 9-byte header, 116 of 8-in-7 data, a checksum
		constexpr size_t BodyHeader = 9;
		constexpr size_t BodyData = 116;
		constexpr uint32_t SectionCount = 0x1c;
		constexpr uint32_t SectionTable = 0x20;
		constexpr uint32_t SectionEntry = 16;

		uint32_t be32(const std::vector<uint8_t>& _bytes, const size_t _at)
		{
			if(_at + 4 > _bytes.size())
				throw std::runtime_error("the container ends inside its section table");
			return syntakt::be32(_bytes.data() + _at);
		}

		// The SysEx messages' data, 8-in-7 decoded: each group of up to 7 bytes after the byte that holds their top
		// bits, the first byte's in its bit 6
		std::vector<uint8_t> decodeSysex(const std::vector<uint8_t>& _syx)
		{
			std::vector<uint8_t> out;
			out.reserve(_syx.size());
			for(size_t i = 0; i < _syx.size();)
			{
				if(_syx[i] != 0xf0)
					throw std::runtime_error("not a SysEx file: no F0 at byte " + std::to_string(i));
				const auto end = std::find(_syx.begin() + static_cast<std::ptrdiff_t>(i), _syx.end(), uint8_t(0xf7));
				if(end == _syx.end())
					throw std::runtime_error("a SysEx message without its F7");
				const size_t body = i + 1;
				const auto length = static_cast<size_t>(end - _syx.begin()) - body;
				i = static_cast<size_t>(end - _syx.begin()) + 1;
				// The start and end messages are shorter: they carry no data
				if(length != MessageBody)
					continue;
				const uint8_t* p = _syx.data() + body + BodyHeader;
				for(size_t k = 0; k < BodyData;)
				{
					const uint8_t high = p[k++];
					for(int n = 0; n < 7 && k < BodyData; ++n)
						out.push_back(static_cast<uint8_t>(p[k++] | ((high >> (6 - n)) & 1) << 7));
				}
			}
			return out;
		}

		// The control bits, most significant first, from tag bytes taken as they are needed
		class Bits
		{
		public:
			Bits(const uint8_t* _data, const size_t _size) : m_data(_data), m_size(_size) {}

			uint8_t byte()
			{
				if(m_pos >= m_size)
					throw std::runtime_error("a packed section ends before its end marker");
				return m_data[m_pos++];
			}

			uint32_t bit()
			{
				m_tag = (m_tag << 1) & 0x1ff;
				if((m_tag & 0xff) == 0)
				{
					const uint32_t b = byte();
					m_tag = b << 1 | 1;
					return b >> 7;
				}
				return m_tag >> 8;
			}

			// Interlaced Elias gamma: a data bit, then a stop bit (1 stops)
			uint32_t gamma()
			{
				uint32_t v = 1;
				for(;;)
				{
					v = v << 1 | bit();
					if(bit())
						return v;
					if(v > 0x02000000)
						throw std::runtime_error("a packed section's length overflows");
				}
			}

			size_t position() const { return m_pos; }

		private:
			const uint8_t* m_data;
			size_t m_size;
			size_t m_pos = 0;
			uint32_t m_tag = 0;
		};

		// The LZ stream _data of _size bytes: 1, a literal byte; 0, a match, gamma g then (g == 2) the last offset or
		// a byte, raw = g << 8 | byte, 767 the end, offset raw - 767; two bits for a length of 1-3, 00 a gamma + 2;
		// beyond offset 3328 one more; the copy takes length + 1 bytes
		std::vector<uint8_t> depack(const uint8_t* _data, const size_t _size)
		{
			constexpr uint32_t Bias = 767;
			constexpr uint32_t Reuse = 2;
			constexpr uint32_t Far = 3328;

			Bits s(_data, _size);
			std::vector<uint8_t> out;
			out.reserve(4 << 20);
			uint32_t last = 1;
			for(;;)
			{
				if(s.bit())
				{
					out.push_back(s.byte());
					continue;
				}
				const uint32_t g = s.gamma();
				uint32_t offset;
				if(g == Reuse)
				{
					offset = last;
				}
				else
				{
					const uint32_t raw = (g << 8) + s.byte();
					if(raw == Bias)
						break;
					offset = raw - Bias;
					last = offset;
				}
				const uint32_t shortLength = 2 * s.bit();
				const uint32_t length0 = shortLength + s.bit();
				uint32_t length = length0 ? length0 : s.gamma() + 2;
				if(offset > Far)
					++length;
				if(offset == 0 || offset > out.size())
					throw std::runtime_error("a packed section's match reaches before its start");
				for(uint32_t n = 0; n <= length; ++n)
					out.push_back(out[out.size() - offset]);
			}
			if(s.position() != _size)
				throw std::runtime_error("a packed section's end marker is not at its end");
			return out;
		}

		// A section's bytes as the file stores them: packed when its 8-byte header holds (the length fits, the bytes
		// after it sum to the sum), else stored, without the header when its sum is 0
		std::vector<uint8_t> section(const uint8_t* _stream, const size_t _size)
		{
			if(_size >= 8)
			{
				const uint32_t length = syntakt::be32(_stream);
				const uint32_t sum = syntakt::be32(_stream + 4);
				if(static_cast<uint64_t>(length) + 8 <= _size)
				{
					uint32_t s = 0;
					for(size_t i = 0; i < length; ++i)
						s += _stream[8 + i];
					if(s == sum)
						return depack(_stream + 8, length);
				}
				if(sum == 0)
					return {_stream + 8, _stream + _size};
			}
			return {_stream, _stream + _size};
		}

		uint64_t fnv(const std::vector<uint8_t>& _bytes)
		{
			uint64_t h = 0xcbf29ce484222325;
			for(const uint8_t b : _bytes)
				h = (h ^ b) * 0x100000001b3;
			return h;
		}
	}

	bool unpack(const std::vector<uint8_t>& _syx, Sections& _sections, std::string& _error)
	{
		try
		{
			const auto stream = decodeSysex(_syx);
			static constexpr uint8_t Magic[] = {'E', 'L', 'E', '3'};
			const auto magic = std::search(stream.begin(), stream.end(), std::begin(Magic), std::end(Magic));
			if(magic == stream.end())
				throw std::runtime_error("no ELE3 container in the SysEx data");
			const std::vector<uint8_t> container(magic, stream.end());

			const uint32_t count = be32(container, SectionCount);
			bool found3 = false, found7 = false;
			for(uint32_t k = 0; k < count; ++k)
			{
				const size_t entry = SectionTable + SectionEntry * static_cast<size_t>(k);
				const uint32_t id = be32(container, entry);
				const uint32_t offset = be32(container, entry + 4);
				const uint32_t length = be32(container, entry + 8);
				if(id != 3 && id != 7)
					continue;
				if(static_cast<uint64_t>(offset) + length > container.size())
					throw std::runtime_error("section " + std::to_string(id) + " lies beyond the container");
				auto bytes = section(container.data() + offset, length);
				if(id == 3)
				{
					_sections.section3 = std::move(bytes);
					found3 = true;
				}
				else
				{
					_sections.section7 = std::move(bytes);
					found7 = true;
				}
			}
			if(!found3 || !found7)
				throw std::runtime_error("the container lacks section 3 or 7");
			return true;
		}
		catch(const std::exception& _e)
		{
			_error = _e.what();
			return false;
		}
	}

	bool load(const std::vector<uint8_t>& _syx, Firmware& _firmware, std::string& _error)
	{
		Sections sections;
		if(!unpack(_syx, sections, _error))
			return false;
		if(sections.section3.size() != syntakt::main::Section3Size
			|| fnv(sections.section3) != syntakt::main::Section3Fnv || sections.section7.size() != syntakt::Section7Size || fnv(sections.section7) != syntakt::Section7Fnv)
		{
			_error = "not Syntakt OS 1.41: its sections 3 and 7 are not 1.41's";
			return false;
		}
		_firmware.section3.bytes = std::move(sections.section3);
		_firmware.section7.resize(sections.section7.size() / 4);
		for(size_t i = 0; i < _firmware.section7.size(); ++i)
			_firmware.section7[i] = static_cast<int32_t>(syntakt::be32(sections.section7.data() + 4 * i));
		return true;
	}
}
