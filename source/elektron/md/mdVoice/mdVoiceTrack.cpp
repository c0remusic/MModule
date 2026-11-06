#include "mdVoiceTrack.h"

#include <algorithm>

namespace md::voice
{
	void Track::setMachine(const uint8_t _id, const Defaults& _defaults)
	{
		m_pending = _id;
		for(size_t p = 0; p < _defaults.size(); ++p)
		{
			m_kit[p] = _defaults[p];
			if(!(m_locked >> p & 1))
				m_smoother.setTarget(p, _defaults[p]);
		}
	}

	void Track::knob(const size_t _param, const uint8_t _value)
	{
		m_kit[_param] = _value;
		if(m_locked >> _param & 1)
			m_lockValue[_param] = _value;
		m_smoother.setTarget(_param, _value);
	}

	void Track::lock(const size_t _param, const uint8_t _value)
	{
		m_lockPending |= 1u << _param;
		m_lockPendingValue[_param] = _value;
	}

	void Track::clearMachineLocks()
	{
		constexpr uint32_t machine = 0xff;
		for(size_t p = 0; p < 8; ++p)
			if(m_locked >> p & 1)
				m_smoother.setTarget(p, m_kit[p]);
		m_locked &= ~machine;
		m_lockPending &= ~machine;
	}

	void Track::hit()
	{
		m_trigger = true;
		if(m_pending < 0)
			return;
		m_machine = static_cast<uint8_t>(m_pending);
		m_pending = -1;
		for(size_t p = 0; p < m_kit.size(); ++p)
			m_smoother.snap(p, m_kit[p]);
	}

	uint8_t Track::target(const size_t _param) const
	{
		return m_locked >> _param & 1 ? m_lockValue[_param] : m_kit[_param];
	}

	void Track::takeLocks()
	{
		if(m_trigger)
		{
			const uint32_t changed = m_locked | m_lockPending;
			m_locked = m_lockPending;
			m_lockValue = m_lockPendingValue;
			for(size_t p = 0; p < kParams; ++p)
				if(changed >> p & 1)
					m_smoother.snap(p, target(p));
		}
		m_lockPending = 0;
	}

	void Track::modulate(const size_t _param, const int _offset)
	{
		if(m_modulation[_param] == _offset)
			return;
		m_modulation[_param] = _offset;
		m_modulated = _offset ? m_modulated | 1u << _param : m_modulated & ~(1u << _param);
		m_moved = true;
	}

	void Track::update(const OsTables& _tables, const uint32_t _tempo, Voice& _voice)
	{
		// The OS's tick runs the LFO's restart and this Track's LFO on its destination here, before the coefficient
		// function (HostModel::updateVoice)
		if(isAudioMachine(m_machine))
		{
			Smoother::Values moved;
			const uint16_t* values = m_smoother.values().data();
			if(m_modulated)
			{
				moved = m_smoother.values();
				for(size_t p = 0; p < m_modulation.size(); ++p)
					moved[p] = static_cast<uint16_t>(std::clamp(moved[p] + m_modulation[p], 0, 0x3fff));
				values = moved.data();
			}
			Packet packet;
			const int n = coefficients(_tables, m_machine, values, m_trigger, _tempo, packet);
			_voice.setSlot(packet.data(), n);
		}
		else if(m_trigger)
		{
			constexpr uint32_t gnd = 1;	// GND--'s type: its INIT and update do nothing, its render is silence
			_voice.setSlot(&gnd, 1);
		}
		m_trigger = false;
		m_moved = false;
	}

	void prepareBlock(std::array<Track, 16>& _tracks, std::vector<Voice>& _voices, const OsTables& _tables,
		const uint32_t _tempo, const int64_t _block, const uint32_t _trackMask, const uint32_t _live)
	{
		for(uint32_t bits = _trackMask, t = 0; bits; bits >>= 1, ++t)
			if(bits & 1)
				_tracks[t].takeLocks();
		for(uint32_t bits = _trackMask, t = 0; bits; bits >>= 1, ++t)
		{
			if(!(bits & 1))
				continue;
			const bool tick = _tracks[t].ticks(_block);
			if(_tracks[t].triggered() || ((tick || _tracks[t].moved()) && (_live >> t & 1)))
				_tracks[t].update(_tables, _tempo, _voices[t]);
			if(tick)
				_tracks[t].tick();
		}
	}
}
