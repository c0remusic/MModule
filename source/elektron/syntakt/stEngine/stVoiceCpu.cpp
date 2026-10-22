#include "stVoiceCpu.h"

#include <algorithm>

#include "stBoot.h"
#include "stEngine.h"

namespace syntakt
{
	namespace
	{
		// Where CPU #1's block holds track t's parameters, as receiveBlock spreads them: the 20 machine bytes (the
		// machine, then the machine's words), then the filter's and the amp's words, the pitch, the trig and gate-off bits
		constexpr uint32_t machineBytes(const int _t) { return 20 * static_cast<uint32_t>(_t); }
		constexpr uint32_t filterWords(const int _t) { return 0xa0 + 8 * static_cast<uint32_t>(_t); }
		constexpr uint32_t ampWords(const int _t) { return 0xe0 + 14 * static_cast<uint32_t>(_t); }
		constexpr uint32_t pitchWord(const int _t) { return 0x150 + 4 * static_cast<uint32_t>(_t); }
		constexpr uint32_t Trigs = 0x180;
		constexpr uint32_t GateOffs = 0x181;
	}

	int VoiceCpu::parameterCount(const Page _page)
	{
		switch(_page)
		{
		case Page::Machine:	return 9;
		case Page::Filter:	return 4;
		case Page::Amp:		return 7;
		}
		return 0;
	}

	VoiceCpu::VoiceCpu(const std::vector<int32_t>& _section7) : m_sram(std::make_unique<Sram>()), m_rom{_section7.data()}
	{
		boot(*m_sram, m_globals, m_rom);
	}

	VoiceCpu::~VoiceCpu() = default;

	bool VoiceCpu::isPorted(const int _machine) const
	{
		// The voice CPU's map sends every machine of the analog tracks (CPU #1 drives their circuits) to engine 0, as
		// BD MODERN's own id: only BD MODERN itself plays engine 0
		const int32_t id = engineOf(m_rom, _machine);
		if(id == 0 && _machine != 0)
			return false;
		return engine(static_cast<uint32_t>(id)) != nullptr;
	}

	void VoiceCpu::setMachine(const int _track, const uint8_t _machine)
	{
		m_block[machineBytes(_track)] = _machine;
	}

	void VoiceCpu::setNote(const int _track, const int _note)
	{
		// Semitones in Q16, a big-endian long
		const uint32_t pitch = static_cast<uint32_t>(_note) << 16;
		for(uint32_t i = 0; i < 4; ++i)
			m_block[pitchWord(_track) + i] = static_cast<uint8_t>(pitch >> (24 - 8 * i));
	}

	void VoiceCpu::setParameter(const int _track, const Page _page, const int _index, const uint8_t _value,
		const uint8_t _fine)
	{
		if(_index < 0 || _index >= parameterCount(_page))
			return;
		const uint32_t i = 2 * static_cast<uint32_t>(_index);
		const uint32_t at = _page == Page::Machine ? machineBytes(_track) + 2 + i
			: _page == Page::Filter ? filterWords(_track) + i : ampWords(_track) + i;
		m_block[at] = _value;
		m_block[at + 1] = _fine;
	}

	void VoiceCpu::trig(const int _track)
	{
		m_block[Trigs] |= static_cast<uint8_t>(1 << _track);
	}

	void VoiceCpu::gateOff(const int _track)
	{
		m_block[GateOffs] |= static_cast<uint8_t>(1 << _track);
	}

	void VoiceCpu::render(int32_t* const* _outputs, const size_t _count)
	{
		for(size_t done = 0; done < _count;)
		{
			if(m_position == BlockSize)
				renderBlock();
			const size_t n = std::min(_count - done, static_cast<size_t>(BlockSize - m_position));
			for(int t = 0; t < Voices; ++t)
				std::copy_n(m_out.data() + BlockSize * t + m_position, n, _outputs[t] + done);
			m_position += static_cast<int>(n);
			done += n;
		}
	}

	void VoiceCpu::renderBlock()
	{
		runBlock(*m_sram, m_globals, m_rom, m_block.data());
		m_block[Trigs] = 0;
		m_block[GateOffs] = 0;

		// The eight tracks' 32 samples are contiguous at VoiceOut
		std::copy_n(m_sram->ptr(VoiceOut), m_out.size(), m_out.data());
		m_position = 0;
	}
}
