#include "mdFastMasterWords.h"

#include <algorithm>

namespace md::fast
{
	EchoWords echoWords(const voice::OsTables& _tables, const MasterValues& _values, const uint32_t _tempo)
	{
		// $20B4AE. Above $3F3D, TIME 127's value, the delay jumps by a full 128 steps
		const uint32_t time = _values[EchoTime];
		uint32_t product = time * 44100u;
		if(time > 0x3f3d)
			product += 128 * 44100u;
		const int32_t delay = static_cast<int32_t>((product >> 10) * 360) / static_cast<int32_t>(_tempo);

		EchoWords w{};
		echoDelayWords(delay, w);
		w[2] = _values[EchoMod] * 2u;
		w[3] = (static_cast<uint32_t>(_values[EchoMfrq]) << 4) + 0x200;
		w[4] = static_cast<uint32_t>(_values[EchoFb]) << 9;
		const uint32_t highCut = _values[EchoFltf] >> 6;
		const uint32_t lowCut = std::min(highCut + (_values[EchoFltw] >> 6), 0xffu);
		w[5] = 0x7fffff - _tables.onePole[highCut];
		w[6] = 0x7fffff - _tables.onePole[lowCut];
		w[7] = (0x3fffu - _values[EchoMono]) << 9;
		w[8] = static_cast<uint32_t>(_values[EchoLev]) << 8;
		return w;
	}

	void echoDelayWords(const int32_t _delay, EchoWords& _words)
	{
		// The lines hold $17ADC words: within them the read pointer's distance sets the delay, at the step $4001, which
		// the block's truncation brings to one word a sample; a longer delay slows both pointers instead
		constexpr int32_t lineWords = 0x17adc;
		if(_delay < lineWords)
		{
			_words[0] = static_cast<uint32_t>(lineWords - _delay);
			_words[1] = 0x4001;
		}
		else
		{
			_words[0] = 0x20;
			_words[1] = static_cast<uint32_t>(0x5ebf0000 / _delay);
		}
	}

	GateBoxWords gateBoxWords(const voice::OsTables& _tables, const MasterValues& _values)
	{
		// $20B940
		GateBoxWords w{};
		w[0] = 0x7fffff - _tables.onePole[_values[GateHp] >> 6];
		w[1] = 0x7fffff - _tables.onePole[_values[GateLp] >> 6];
		w[2] = _values[GateDec] >> 8;
		w[3] = _values[GateDamp] >> 8;
		w[4] = _tables.curve[_values[GateLev] >> 7] << 7;

		// The gate's hold in blocks, from the pitch table's octaves; its top values never close it
		const uint32_t gate = _values[GateGate];
		if(gate > 0x3f70)
		{
			w[5] = 0x7fffff;
		}
		else
		{
			const uint32_t v = gate >> 5;
			const auto pitch = static_cast<int32_t>(_tables.pitch[v & 0x7f]) >> (5 - (v >> 7));
			w[5] = static_cast<uint32_t>(pitch) * 44100u >> 15;
		}
		w[6] = static_cast<uint32_t>(_values[GateDvol]) << 9;
		w[7] = _values[GatePred] & 0xfffeu;
		return w;
	}

	MasterValues SendWords::values(const int _first) const
	{
		MasterValues v;
		std::copy_n(m_smoother.values().begin() + _first, v.size(), v.begin());
		return v;
	}

	SendWords::Due SendWords::tick()
	{
		m_smoother.tick();
		const uint32_t turn = m_ticks++ & 3;
		if(turn == 0)
		{
			m_echo = echoWords(m_tables, values(EchoFirst), m_tempo);
			return Due::Echo;
		}
		if(turn == 3)
		{
			m_gateBox = gateBoxWords(m_tables, values(0));
			return Due::GateBox;
		}
		return Due::None;
	}
}
