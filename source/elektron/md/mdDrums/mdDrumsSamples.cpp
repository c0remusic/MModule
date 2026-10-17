#include "mdDrumsSamples.h"

#include "Firmware.h"
#include "VoiceEngine.h"

#include <algorithm>

namespace mdDrums
{
	namespace
	{
		constexpr std::array<uint32_t, 4> g_none{0, 0, Sample::NoLoop, 0};
	}

	Samples::Samples(const md::fw::RomBank& _factory) : m_codes(_factory.codes)
	{
		m_factory.fill(g_none);
		for(const auto& e : _factory.directory)
		{
			for(int rom = 0; rom < RomCount; ++rom)
				if(entryAddress(rom) == e.addr)
					std::copy(std::begin(e.words), std::end(e.words), m_factory[static_cast<size_t>(rom)].begin());
		}
	}

	uint32_t Samples::entryAddress(const int _rom)
	{
		// ROM-01 to -32 at entries 0-31, ROM-33 to -48 at 48-63: 32-47 belong to the RAM Machines
		return md::fw::RomBank::DirectoryAddr + 4 * static_cast<uint32_t>(_rom < 32 ? _rom : _rom + 16);
	}

	std::array<uint32_t, 4> Samples::entry(const int _rom) const
	{
		const auto& s = m_samples[static_cast<size_t>(_rom)];
		if(!s)
			return m_factory[static_cast<size_t>(_rom)];
		return {m_address[static_cast<size_t>(_rom)], s->length, s->loop, s->rate};
	}

	size_t Samples::room() const
	{
		size_t used = 0;
		for(const auto& s : m_samples)
			used += s ? s->words.size() : 0;
		return End - Base - std::min<size_t>(used, End - Base);
	}

	bool Samples::set(const int _rom, std::shared_ptr<const Sample> _sample)
	{
		if(_rom < 0 || _rom >= RomCount)
			return false;
		const auto at = static_cast<size_t>(_rom);
		const auto now = m_samples[at] ? m_samples[at]->words.size() : 0;
		if(_sample && _sample->words.size() > room() + now)
			return false;

		m_samples[at] = std::move(_sample);
		uint32_t address = Base;
		for(int rom = 0; rom < RomCount; ++rom)
		{
			const auto& s = m_samples[static_cast<size_t>(rom)];
			m_address[static_cast<size_t>(rom)] = address;
			if(s)
				address += static_cast<uint32_t>(s->words.size());
		}
		// A write under way starts again from the first ROM either one moves
		m_from = m_phase == Phase::Idle ? _rom : std::min(m_from, _rom);
		m_phase = Phase::Clear;
		return true;
	}

	void Samples::write(md::engine::VoiceEngine& _voices, size_t _words)
	{
		if(m_phase == Phase::Clear)
		{
			// What moves points at nothing while its words are written; what goes back to the factory's is done
			for(int rom = m_from; rom < RomCount; ++rom)
			{
				const auto e = m_samples[static_cast<size_t>(rom)] ? g_none : entry(rom);
				_voices.writeP(entryAddress(rom), e.data(), e.size());
			}
			m_rom = m_from;
			m_offset = 0;
			m_phase = Phase::Words;
		}
		while(m_phase == Phase::Words && _words > 0)
		{
			if(m_rom >= RomCount)
			{
				m_phase = Phase::Entries;
				break;
			}
			const auto& s = m_samples[static_cast<size_t>(m_rom)];
			const auto left = s ? s->words.size() - m_offset : 0;
			if(left == 0)
			{
				++m_rom;
				m_offset = 0;
				continue;
			}
			const auto n = std::min(left, _words);
			_voices.writeP(m_address[static_cast<size_t>(m_rom)] + static_cast<uint32_t>(m_offset), s->words.data() + m_offset, n);
			m_offset += n;
			_words -= n;
		}
		if(m_phase == Phase::Entries)
		{
			for(int rom = m_from; rom < RomCount; ++rom)
			{
				const auto e = entry(rom);
				_voices.writeP(entryAddress(rom), e.data(), e.size());
			}
			m_phase = Phase::Idle;
		}
	}
}
