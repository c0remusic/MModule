#pragma once

#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace syntakt
{
	constexpr size_t Section7Size = 383760;	// OS 1.41's

	inline uint32_t be32(const uint8_t* _p)
	{
		return uint32_t(_p[0]) << 24 | uint32_t(_p[1]) << 16 | uint32_t(_p[2]) << 8 | _p[3];
	}

	inline std::vector<uint8_t> readFile(const std::string& _path)
	{
		std::ifstream f(_path, std::ios::binary);
		return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
	}

	// The user's section 7 as the words a Rom reads; empty when the file is not OS 1.41's section 7
	inline std::vector<int32_t> loadSection7(const std::string& _path)
	{
		const auto bytes = readFile(_path);
		if(bytes.size() != Section7Size)
			return {};
		std::vector<int32_t> words(bytes.size() / 4);
		for(size_t i = 0; i < words.size(); ++i)
			words[i] = static_cast<int32_t>(be32(bytes.data() + 4 * i));
		return words;
	}
}
