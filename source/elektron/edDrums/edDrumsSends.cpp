#include "edDrumsSends.h"

#include <algorithm>
#include <cmath>

namespace elektron::drums
{
	Sends::Sends(const md::voice::OsTables& _tables, const uint32_t* const _sine)
		: m_words(_tables), m_echo(std::make_unique<md::fast::Echo>()),
		  m_gateBox(std::make_unique<md::fast::GateBox>(_sine))
	{
	}

	void Sends::block()
	{
		if(++m_tick == TickBlocks)
		{
			m_tick = 0;
			const auto due = m_words.tick();
			if(due == md::fast::SendWords::Due::Echo)
				m_echo->setWords(m_words.echo());
			else if(due == md::fast::SendWords::Due::GateBox)
				m_gateBox->setWords(m_words.gateBox());
		}

		const bool fed = m_fed;
		m_fed = false;
		if(fed)
			m_quiet = 0;
		else if(!sounding())
			return;

		auto& delay = m_out[static_cast<size_t>(Send::Delay)];
		auto& reverb = m_out[static_cast<size_t>(Send::Reverb)];
		const auto& delayIn = m_in[static_cast<size_t>(Send::Delay)];
		const auto& reverbIn = m_in[static_cast<size_t>(Send::Reverb)];
		m_echo->process(delayIn[0].data(), delayIn[1].data(), delay[0].data(), delay[1].data());
		m_gateBox->process(reverbIn[0].data(), reverbIn[1].data(), delay[0].data(), delay[1].data(), reverb[0].data(),
			reverb[1].data());
		for(auto& send : m_in)
			for(auto& channel : send)
				channel.fill(0.0f);

		float returned = 0;
		for(const auto& send : m_out)
			for(const auto& channel : send)
				for(const float x : channel)
					returned = std::max(returned, std::abs(x));
		if(returned > Silence || fed)
			m_quiet = 0;
		else if(++m_quiet == QuietBlocks)
			for(auto& send : m_out)
				for(auto& channel : send)
					channel.fill(0.0f);
	}
}
