#include "edDrumsSamples.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "mdVoice.h"

namespace elektron::drums
{
	namespace
	{
		constexpr char Magic[4] = {'E', 'D', 'S', 'M'};
		constexpr uint32_t Version = 1;

		// The 12-bit code whose value is nearest _v: the decoding rises with the code, -1 at 0, 0 at 2048
		uint32_t code(const int16_t _v)
		{
			static const md::voice::RomTables tables;
			const auto value = [&](const uint32_t _code)
			{
				const uint32_t w = tables.decode[_code] & 0xffffff;
				return static_cast<int32_t>(w ^ 0x800000) - 0x800000;
			};
			const int32_t x = static_cast<int32_t>(_v) * 256;
			uint32_t lo = 0, hi = 0xfff;
			while(lo < hi)
			{
				const uint32_t mid = (lo + hi) / 2;
				if(value(mid) < x)
					lo = mid + 1;
				else
					hi = mid;
			}
			if(lo > 0 && x - value(lo - 1) < value(lo) - x)
				--lo;
			return lo;
		}

		Sample fromPcm(std::string _name, std::vector<int16_t> _pcm, const double _rate)
		{
			Sample s;
			s.name = std::move(_name);
			s.rate = _rate;
			s.pcm = std::move(_pcm);
			const size_t played = std::min<size_t>(s.pcm.size(), Sample::MaxLength);
			s.words.resize((played + 1) / 2);
			for(size_t w = 0; w < s.words.size(); ++w)
			{
				const uint32_t second = 2 * w + 1 < played ? code(s.pcm[2 * w + 1]) : code(0);
				s.words[w] = code(s.pcm[2 * w]) << 12 | second;
			}
			s.rateWord = static_cast<uint32_t>(std::lround(_rate * Sample::Unity / 44100.0));
			s.source = std::make_shared<const std::vector<int16_t>>(s.pcm);
			return s;
		}

		void put32(std::vector<uint8_t>& _out, const uint32_t _v)
		{
			for(int b = 0; b < 4; ++b)
				_out.push_back(static_cast<uint8_t>(_v >> (8 * b)));
		}

		uint32_t get32(const uint8_t* _p)
		{
			return _p[0] | _p[1] << 8 | _p[2] << 16 | static_cast<uint32_t>(_p[3]) << 24;
		}
	}

	std::variant<Sample, std::string> makeSample(std::string _name, const std::vector<float>& _mono, const double _rate)
	{
		if(_mono.empty())
			return std::string("The file holds no audio");
		if(_rate < Sample::MinRate - 0.5 || _rate > Sample::MaxRate + 0.5)
			return "The file plays at " + std::to_string(std::lround(_rate)) + " Hz: a sample plays at 4 to 48 kHz";
		std::vector<int16_t> pcm(std::min<size_t>(_mono.size(), Sample::MaxSourceLength));
		for(size_t n = 0; n < pcm.size(); ++n)
			pcm[n] = static_cast<int16_t>(std::lround(std::clamp(_mono[n], -1.0f, 1.0f) * 32767.0f));
		return fromPcm(std::move(_name), std::move(pcm), _rate);
	}

	std::vector<uint8_t> sampleToBytes(const Sample& _sample)
	{
		std::vector<uint8_t> out(Magic, Magic + 4);
		put32(out, Version);
		put32(out, static_cast<uint32_t>(_sample.name.size()));
		out.insert(out.end(), _sample.name.begin(), _sample.name.end());
		uint64_t rate;
		std::memcpy(&rate, &_sample.rate, sizeof(rate));
		put32(out, static_cast<uint32_t>(rate));
		put32(out, static_cast<uint32_t>(rate >> 32));
		put32(out, static_cast<uint32_t>(_sample.pcm.size()));
		for(const int16_t v : _sample.pcm)
		{
			out.push_back(static_cast<uint8_t>(v));
			out.push_back(static_cast<uint8_t>(static_cast<uint16_t>(v) >> 8));
		}
		return out;
	}

	std::optional<Sample> sampleFromBytes(const uint8_t* _data, const size_t _size)
	{
		if(_size < 12 || std::memcmp(_data, Magic, 4) != 0 || get32(_data + 4) != Version)
			return std::nullopt;
		const size_t nameSize = get32(_data + 8);
		if(_size < 12 + nameSize + 12)
			return std::nullopt;
		const uint8_t* p = _data + 12;
		std::string name(reinterpret_cast<const char*>(p), nameSize);
		p += nameSize;
		const uint64_t bits = get32(p) | static_cast<uint64_t>(get32(p + 4)) << 32;
		double rate;
		std::memcpy(&rate, &bits, sizeof(rate));
		const size_t count = get32(p + 8);
		p += 12;
		if(count == 0 || count > Sample::MaxSourceLength || static_cast<size_t>(_data + _size - p) != 2 * count
			|| !(rate >= Sample::MinRate - 0.5 && rate <= Sample::MaxRate + 0.5))
			return std::nullopt;
		std::vector<int16_t> pcm(count);
		for(size_t n = 0; n < count; ++n)
			pcm[n] = static_cast<int16_t>(p[2 * n] | p[2 * n + 1] << 8);
		return fromPcm(std::move(name), std::move(pcm), rate);
	}
}
