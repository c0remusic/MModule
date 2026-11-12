#include "edDrumsAnalogVoice.h"

#include <algorithm>
#include <cmath>

namespace elektron::drums::analog
{
	namespace
	{
		enum class Family { Kick, Snare, Synth, Metal, Utility };

		Family familyOf(const Machine _m)
		{
			const int i = static_cast<int>(_m);
			if(i < static_cast<int>(Machine::SnareHard))
				return Family::Kick;
			if(i < static_cast<int>(Machine::SynthDual))
				return Family::Snare;
			if(i < FirstMetal)
				return Family::Synth;
			return i < FirstUtility ? Family::Metal : Family::Utility;
		}

		void renderOf(const Machine _m, const VoiceIo& _io, Circuit& _c, float* _out, const int _n, const double _rate)
		{
			switch(familyOf(_m))
			{
			case Family::Kick:		circuit::kickRender(_m, _io, _c, _out, _n, _rate); break;
			case Family::Snare:		circuit::snareRender(_m, _io, _c, _out, _n, _rate); break;
			case Family::Synth:		circuit::synthRender(_m, _io, _c, _out, _n, _rate); break;
			case Family::Metal:		circuit::metalRender(_m, _io, _c, _out, _n, _rate); break;
			case Family::Utility:	circuit::utilityRender(_m, _io, _c, _out, _n, _rate); break;
			}
		}
	}

	void trigCircuit(const Machine _m, Circuit& _c)
	{
		switch(familyOf(_m))
		{
		case Family::Kick:		circuit::kickTrig(_m, _c); break;
		case Family::Snare:		circuit::snareTrig(_m, _c); break;
		case Family::Synth:		circuit::synthTrig(_m, _c); break;
		case Family::Metal:		circuit::metalTrig(_m, _c); break;
		case Family::Utility:	circuit::utilityTrig(_m, _c); break;
		}
	}

	void paramsOf(const Machine _m, const uint8_t* _controls, std::array<uint8_t, 20>& _params)
	{
		switch(familyOf(_m))
		{
		case Family::Kick:		circuit::kickParams(_m, _controls, _params); break;
		case Family::Snare:		circuit::snareParams(_m, _controls, _params); break;
		case Family::Synth:		circuit::synthParams(_m, _controls, _params); break;
		case Family::Metal:		circuit::metalParams(_m, _controls, _params); break;
		case Family::Utility:	circuit::utilityParams(_m, _controls, _params); break;
		}
	}

	std::array<uint8_t, 8> defaultsOf(const Machine _m)
	{
		switch(familyOf(_m))
		{
		case Family::Kick:		return circuit::kickDefaults(_m);
		case Family::Snare:		return circuit::snareDefaults(_m);
		case Family::Synth:		return circuit::synthDefaults(_m);
		case Family::Metal:		return circuit::metalDefaults(_m);
		default:				return circuit::utilityDefaults(_m);
		}
	}
	void advanceOf(const Machine _m, Circuit& _c, const int64_t _samples, const double _rate)
	{
		switch(familyOf(_m))
		{
		case Family::Kick:		circuit::kickAdvance(_m, _c, _samples, _rate); break;
		case Family::Snare:		circuit::snareAdvance(_m, _c, _samples, _rate); break;
		case Family::Synth:		circuit::synthAdvance(_m, _c, _samples, _rate); break;
		case Family::Metal:		circuit::metalAdvance(_m, _c, _samples, _rate); break;
		case Family::Utility:	circuit::utilityAdvance(_m, _c, _samples, _rate); break;
		}
	}

	bool usesControl(const Machine _m, const int _index)
	{
		// the routines that read nothing there: the phase kick's eighth word, the impulse's fourth to seventh
		if(_m == Machine::KickFm)
			return _index != 7;
		if(_m == Machine::Impulse)
			return _index < 3 || _index > 6;
		return _index >= 0 && _index < 8;
	}

	bool endedOf(const Machine _m, const VoiceIo& _io, const Circuit& _c)
	{
		switch(familyOf(_m))
		{
		case Family::Kick:		return circuit::kickEnded(_m, _io, _c);
		case Family::Snare:		return circuit::snareEnded(_m, _io, _c);
		case Family::Synth:		return circuit::synthEnded(_m, _io, _c);
		case Family::Metal:		return circuit::metalEnded(_m, _io, _c);
		default:				return circuit::utilityEnded(_m, _io, _c);
		}
	}

	void Voice::setMachine(const Machine _m)
	{
		m_pending = _m;
	}

	void Voice::setControls(const uint8_t* _controls8)
	{
		std::copy_n(_controls8, m_controls.size(), m_controls.begin());
	}

	void Voice::setNote(const int _note)
	{
		m_note = _note;
	}

	void Voice::trig(const int64_t _now)
	{
		// what ran free while the engine left the voice frozen: a step, as many samples as passed since its last
		if(_now >= 0 && m_sounding && _now > m_clock)
			advanceOf(m_machine, m_circuit, _now - m_clock, m_rate);
		if(_now >= 0)
			m_clock = _now;
		// a machine change takes effect at a Hit, as on the original; track 12's voice sets its topology first
		const bool changed = m_pending != m_machine || !m_sounding;
		m_machine = m_pending;
		m_io.voice = isMetal(m_machine) ? MetalVoice : 0;
		if(changed)
		{
			m_io = VoiceIo{};
			m_io.voice = isMetal(m_machine) ? MetalVoice : 0;
			// the state as the boot leaves it: no envelope running
			setBe32(m_io.state.data(), 0xffffffffu);
			setup(m_machine, m_io);
			// another machine's circuit leaves nothing: a family keeps what survives a Hit only across its own machine's Hits
			m_circuit = Circuit{};
		}
		analog::trig(m_io);
		trigCircuit(m_machine, m_circuit);
		m_sounding = true;
		m_ticks = 0;
		m_count = m_pos = 0;
	}

	void Voice::tick()
	{
		paramsOf(m_machine, m_controls.data(), m_io.params);
		m_io.pitch = m_note * 65536;
		control(m_machine, m_io);
	}

	void Voice::render(float* _out, const int _n, const double _rate)
	{
		if(!m_sounding)
			return;
		const auto rate = static_cast<int64_t>(std::llround(_rate));
		m_rate = _rate;
		m_clock += _n;
		for(int done = 0; done < _n;)
		{
			if(m_pos == m_count)
			{
				tick();
				const int64_t from = m_ticks * TickSamples * rate / 48000;
				const int64_t to = (m_ticks + 1) * TickSamples * rate / 48000;
				m_count = static_cast<int>(to - from);
				std::fill_n(m_buf.begin(), m_count, 0.0f);
				renderOf(m_machine, m_io, m_circuit, m_buf.data(), m_count, _rate);
				endBlock(m_io);
				++m_ticks;
				m_pos = 0;
			}
			const int n = std::min(_n - done, m_count - m_pos);
			for(int i = 0; i < n; ++i)
				_out[done + i] += m_buf[static_cast<size_t>(m_pos + i)];
			m_pos += n;
			done += n;
		}
	}
}
