#include "MdsPackage.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace md::mds
{
	namespace
	{
		uint32_t be16(const std::vector<uint8_t>& _b, const size_t _at)
		{
			return static_cast<uint32_t>(_b[_at] << 8 | _b[_at + 1]);
		}

		uint32_t be32(const std::vector<uint8_t>& _b, const size_t _at)
		{
			return static_cast<uint32_t>(_b[_at]) << 24 | static_cast<uint32_t>(_b[_at + 1]) << 16 |
				static_cast<uint32_t>(_b[_at + 2]) << 8 | _b[_at + 3];
		}

		std::string text(const std::vector<uint8_t>& _b, const size_t _at, const size_t _size)
		{
			std::string s;
			for(size_t i = 0; i < _size && _b[_at + i]; ++i)
				s.push_back(static_cast<char>(_b[_at + i]));
			return s;
		}

		// The reflected CRC-32 of zlib ($EDB88320, initial and final XOR $FFFFFFFF)
		uint32_t crc32(const std::vector<uint8_t>& _b, const size_t _begin, const size_t _end, const size_t _zeroFrom = 0,
			const size_t _zeroTo = 0)
		{
			uint32_t crc = 0xffffffff;
			for(size_t i = _begin; i < _end; ++i)
			{
				const uint8_t v = i >= _zeroFrom && i < _zeroTo ? 0 : _b[i];
				crc ^= v;
				for(int k = 0; k < 8; ++k)
					crc = crc & 1 ? (crc >> 1) ^ 0xedb88320 : crc >> 1;
			}
			return ~crc;
		}

		void require(const bool _condition, const char* _what)
		{
			if(!_condition)
				throw std::runtime_error(_what);
		}
	}

	Package parsePackage(const std::vector<uint8_t>& _bytes)
	{
		const auto& b = _bytes;
		require(b.size() >= 128 && b[0] == 'M' && b[1] == 'D' && b[2] == 'S' && b[3] == '1', "not an MDS1 package");
		require(be16(b, 4) == 1 && be16(b, 6) == 128, "unsupported MDS version or header size");
		require(be32(b, 8) == b.size() && b.size() % 4 == 0, "package size does not match");
		require(be16(b, 16) == 0x5603 && be16(b, 18) == 1 && b[20] == 0, "not raw DSP56303 code");
		require(crc32(b, 0, b.size(), 84, 88) == be32(b, 84), "package CRC-32 mismatch");

		Package p;
		p.bytes = b;
		const auto programWords = be16(b, 22);
		const auto importCount = be16(b, 24);
		p.xWords = static_cast<uint16_t>(be16(b, 26));
		p.yWords = static_cast<uint16_t>(be16(b, 28));
		p.versionMajor = b[30];
		p.versionMinor = b[31];
		require(p.xWords <= 64 && p.yWords <= 64, "more than 64 words of track memory");

		const auto programOffset = be32(b, 32);
		const auto programLength = be32(b, 36);
		require(programOffset == 128 && programLength == programWords * 3 && programLength == be32(b, 40) &&
			programOffset + programLength <= b.size(), "invalid program section");
		require(crc32(b, programOffset, programOffset + programLength) == be32(b, 76), "program CRC-32 mismatch");
		for(uint32_t i = 0; i < programWords; ++i)
		{
			const auto at = programOffset + i * 3;
			p.program.push_back(static_cast<uint32_t>(b[at]) << 16 | static_cast<uint32_t>(b[at + 1]) << 8 | b[at + 2]);
		}

		const auto relocationOffset = be32(b, 44);
		require(relocationOffset + (programWords + 7) / 8 <= b.size(), "relocation flags out of the package");
		for(uint32_t i = 0; i < programWords; ++i)
			p.relocate.push_back((b[relocationOffset + i / 8] >> (i % 8) & 1) != 0);

		const auto importOffset = be32(b, 48);
		require(importCount <= 64 && importOffset + importCount * 4 <= b.size(), "import table out of the package");
		for(uint32_t i = 0; i < importCount; ++i)
		{
			const auto at = importOffset + i * 4;
			Import im{static_cast<uint16_t>(be16(b, at)), b[at + 2], b[at + 3]};
			require(im.word < programWords && !p.relocate[im.word], "import on a relocated or missing word");
			p.imports.push_back(im);
		}

		p.initOffset = static_cast<uint16_t>(be16(b, 64));
		p.trigOffset = static_cast<uint16_t>(be16(b, 66));
		p.renderOffset = static_cast<uint16_t>(be16(b, 68));
		p.declaredCycles = static_cast<uint16_t>(be16(b, 70));
		require(p.initOffset < programWords && p.trigOffset < programWords && p.renderOffset < programWords,
			"routine outside the program");

		const auto descriptionSize = be16(b, 52);
		const auto d = be32(b, 56);
		require(descriptionSize >= 128 && d + descriptionSize + 128 <= b.size(), "description out of the package");
		require(b[d] == 'M' && b[d + 1] == 'D' && b[d + 2] == 'M' && b[d + 3] == '1' && b[d + 4] == 1,
			"not an MDM1 description");
		require(crc32(b, d, d + descriptionSize) == be32(b, 80), "description CRC-32 mismatch");
		const auto count = b[d + 5];
		require(count <= 8, "more than 8 parameters");
		p.flags = b[d + 6];
		p.linkMask = b[d + 7];
		p.category = text(b, d + 8, 4);
		p.code = text(b, d + 12, 2);
		p.name = text(b, d + 16, 16);
		p.id = text(b, d + 32, 32);
		for(uint32_t i = 0; i < count; ++i)
		{
			const auto at = d + 64 + i * 8;
			Parameter par;
			par.name = text(b, at, 4);
			par.defaultValue = b[at + 4];
			par.min = b[at + 5];
			par.max = b[at + 6];
			par.style = b[at + 7] >> 4;
			par.conversion = static_cast<Conversion>(b[at + 7] & 0xf);
			require(b[at + 7] <= 0x54 && (b[at + 7] & 0xf) <= 7, "unknown parameter conversion");
			p.parameters.push_back(par);
		}
		const auto sprite = d + descriptionSize;
		for(size_t x = 0; x < p.sprite.size(); ++x)
			p.sprite[x] = be32(b, sprite + x * 4);
		return p;
	}

	std::vector<uint8_t> packageFromSysex(const std::vector<uint8_t>& _sysex)
	{
		std::vector<uint8_t> package;
		uint32_t size = 0;
		bool header = false;
		bool end = false;
		int nextSequence = 0;

		size_t i = 0;
		while(i < _sysex.size())
		{
			if(_sysex[i] != 0xf0)
			{
				++i;
				continue;
			}
			size_t j = i;
			while(j < _sysex.size() && _sysex[j] != 0xf7)
				++j;
			require(j < _sysex.size(), "SysEx message without its end");
			const std::vector<uint8_t> m(_sysex.begin() + static_cast<ptrdiff_t>(i), _sysex.begin() + static_cast<ptrdiff_t>(j + 1));
			i = j + 1;

			// F0 00 20 3C 02 00 74 01 command tid tid slot sequence flags body... cs cs len len F7
			if(m.size() < 19 || m[1] != 0 || m[2] != 0x20 || m[3] != 0x3c || m[4] != 2 || m[5] != 0 || m[6] != 0x74 || m[7] != 1)
				continue;
			const auto command = m[8];
			const size_t checkedBegin = 9;
			const size_t checkedEnd = m.size() - 5;
			uint32_t sum = 0;
			for(size_t k = checkedBegin; k < checkedEnd; ++k)
				sum += m[k];
			require((sum & 0x3fff) == (static_cast<uint32_t>(m[checkedEnd]) << 7 | m[checkedEnd + 1]), "SysEx checksum mismatch");
			require((checkedEnd - checkedBegin + 4) == (static_cast<uint32_t>(m[checkedEnd + 2]) << 7 | m[checkedEnd + 3]),
				"SysEx length mismatch");

			std::vector<uint8_t> body;
			for(size_t k = checkedBegin + 5; k < checkedEnd;)
			{
				const auto mask = m[k++];
				for(int bit = 6; bit >= 0 && k < checkedEnd; --bit)
					body.push_back(static_cast<uint8_t>(m[k++] | ((mask >> bit) & 1) << 7));
			}

			const auto sequence = m[12];
			if(command == 0x01)
			{
				require(body.size() == 42, "dump header is not 42 bytes");
				size = be32(body, 0);
				header = true;
			}
			else if(command == 0x02)
			{
				require(header && sequence == nextSequence, "data packet out of order");
				package.insert(package.end(), body.begin(), body.end());
				++nextSequence;
			}
			else if(command == 0x7b)
			{
				end = true;
			}
		}
		require(header && end, "not a complete User Machine dump");
		require(package.size() == size, "dump size does not match its header");
		return package;
	}

	DecayTable decayTable(const std::vector<uint8_t>& _osImage)
	{
		constexpr size_t at = DecayTableAddress - 0x200000;
		if(_osImage.size() < at + 4 * 512)
			throw std::runtime_error("the OS image ends before its decay table");
		DecayTable t{};
		for(size_t i = 0; i < t.size(); ++i)
			t[i] = be32(_osImage, at + 4 * i);
		return t;
	}

	DecayTable approximateDecayTable()
	{
		// T[i] = 2^23 exp(-1/tau), tau = 2^(1.7541 + 0.033880 i) samples
		DecayTable t{};
		for(size_t i = 0; i < t.size(); ++i)
		{
			const double tau = std::pow(2.0, 1.7541 + 0.033880 * static_cast<double>(i));
			t[i] = static_cast<uint32_t>(std::min(std::lround(8388608.0 * std::exp(-1.0 / tau)), 0x7fffffl));
		}
		return t;
	}

	uint32_t convert(const Parameter& _parameter, const int _setting, const DecayTable& _decay)
	{
		return convertFine(_parameter, static_cast<uint16_t>(std::clamp(_setting, 0, 127) << 7), _decay);
	}

	uint32_t convertFine(const Parameter& _parameter, const uint16_t _value, const DecayTable& _decay)
	{
		// As X.20's $21aeba: v clamped to the parameter's range, then the low nibble's conversion (research/10, section 2)
		const int v = std::clamp(static_cast<int>(_value), _parameter.min << 7, _parameter.max << 7);
		switch(_parameter.conversion)
		{
		case Conversion::Raw14:			return static_cast<uint32_t>(v);							// 64 becomes 8192
		case Conversion::UnipolarQ23:	// 64 becomes 0.5; 0 below one setting, $7fffff from 127
			return v <= 0x7f ? 0u : v >= 0x3f80 ? 0x7fffffu : static_cast<uint32_t>(v) << 9;
		case Conversion::SquaredQ23:	return static_cast<uint32_t>(static_cast<int64_t>(v) * v >> 5);	// 64 becomes 0.25
		case Conversion::BipolarQ23:	return static_cast<uint32_t>((v - 8192) * 1024) & 0xffffff;	// 0 is -1, 64 zero
		case Conversion::Boolean:		return v != 0 ? 1u : 0u;
		case Conversion::Index:			return static_cast<uint32_t>((v >> 7) - _parameter.min);
		case Conversion::Pitch:
			{
				// DEFAULT mode: L[(v >> 4) & 127] << (v >> 11), L's 128 entries 2048 * 2^(k / 127.5) as X.20's $21bd6c
				// builds them in 24 fraction bits (the same at TUNING 0 and under the legacy tuning): 16 settings an octave
				static const auto table = []
				{
					std::array<uint32_t, 128> l{};
					uint64_t x = uint64_t(2048) << 24;
					for(auto& e : l)
					{
						e = static_cast<uint32_t>((x + (1u << 23)) >> 24);
						x = (x * 0x1016541 + (1u << 23)) >> 24;
					}
					return l;
				}();
				return table[static_cast<size_t>((v >> 4) & 127)] << (v >> 11);
			}
		case Conversion::Decay:			return _decay[static_cast<size_t>(v >> 5)];			// four entries a setting
		}
		return 0;
	}

	std::vector<uint8_t> buildPackage(const Draft& _draft)
	{
		std::vector<uint8_t> b(128, 0);
		const auto align = [&b] { while(b.size() % 4) b.push_back(0); };
		const auto put16 = [&b](const size_t _at, const uint32_t _v) { b[_at] = uint8_t(_v >> 8); b[_at + 1] = uint8_t(_v); };
		const auto put32 = [&b](const size_t _at, const uint32_t _v)
		{
			b[_at] = uint8_t(_v >> 24); b[_at + 1] = uint8_t(_v >> 16); b[_at + 2] = uint8_t(_v >> 8); b[_at + 3] = uint8_t(_v);
		};
		const auto text = [&b](const size_t _at, const std::string& _s, const size_t _size)
		{
			for(size_t i = 0; i < _s.size() && i < _size; ++i)
				b[_at + i] = static_cast<uint8_t>(_s[i]);
		};

		const size_t program = b.size();
		for(const auto w : _draft.program)
		{
			b.push_back(uint8_t(w >> 16)); b.push_back(uint8_t(w >> 8)); b.push_back(uint8_t(w));
		}
		const size_t programLength = b.size() - program;
		align();
		const size_t relocation = b.size();
		b.resize(b.size() + (_draft.program.size() + 7) / 8, 0);
		for(const auto w : _draft.relocated)
			b[relocation + w / 8] |= static_cast<uint8_t>(1 << (w % 8));
		align();
		const size_t imports = b.size();
		for(const auto& im : _draft.imports)
		{
			b.push_back(uint8_t(im.word >> 8)); b.push_back(uint8_t(im.word)); b.push_back(im.kind); b.push_back(im.symbol);
		}
		align();
		const size_t description = b.size();
		b.resize(b.size() + 128, 0);
		text(description, "MDM1", 4);
		b[description + 4] = 1;
		b[description + 5] = static_cast<uint8_t>(_draft.parameters.size());
		b[description + 6] = _draft.flags;
		text(description + 8, _draft.category, 4);
		text(description + 12, _draft.code, 2);
		text(description + 16, _draft.name, 16);
		text(description + 32, _draft.id, 32);
		for(size_t i = 0; i < _draft.parameters.size() && i < 8; ++i)
		{
			const auto& p = _draft.parameters[i];
			const auto at = description + 64 + i * 8;
			text(at, p.name, 4);
			b[at + 4] = p.defaultValue;
			b[at + 5] = p.min;
			b[at + 6] = p.max;
			b[at + 7] = static_cast<uint8_t>(p.style << 4 | static_cast<uint8_t>(p.conversion));
		}
		b.resize(b.size() + 128, 0);	// the sprite: none

		text(0, "MDS1", 4);
		put16(4, 1);
		put16(6, 128);
		put32(8, static_cast<uint32_t>(b.size()));
		put16(16, 0x5603);
		put16(18, 1);
		put16(22, static_cast<uint32_t>(_draft.program.size()));
		put16(24, static_cast<uint32_t>(_draft.imports.size()));
		put16(26, _draft.xWords);
		put16(28, _draft.yWords);
		put32(32, static_cast<uint32_t>(program));
		put32(36, static_cast<uint32_t>(programLength));
		put32(40, static_cast<uint32_t>(programLength));
		put32(44, static_cast<uint32_t>(relocation));
		put32(48, static_cast<uint32_t>(imports));
		put16(52, 128);
		put32(56, static_cast<uint32_t>(description));
		put16(64, _draft.initOffset);
		put16(66, _draft.trigOffset);
		put16(68, _draft.renderOffset);
		put16(70, _draft.declaredCycles);
		put32(76, crc32(b, program, program + programLength));
		put32(80, crc32(b, description, description + 128));
		put32(84, crc32(b, 0, b.size(), 84, 88));
		return b;
	}
}
