#include "edDrumsSends.h"

#include <algorithm>
#include <cmath>

namespace elektron::drums
{
	Sends::Sends(const md::voice::OsTables& _tables, const uint32_t* const _sine)
		: m_words(_tables), m_echo(std::make_unique<md::fast::Echo>()),
		  m_gateBox(std::make_unique<md::fast::GateBox>(_sine)),
		  m_stDelay(std::make_unique<syntakt::fast::Delay>(Rate)),
		  m_stReverb(std::make_unique<syntakt::fast::Reverb>(Rate))
	{
	}

	void Sends::setTempo(const uint32_t _tempo)
	{
		const uint32_t tempo = std::clamp(_tempo, 720u, 7200u);
		m_words.setTempo(tempo);
		m_stDelay->setTempo(tempo / 24.0);
	}

	void Sends::setDelayFree(const double _hz)
	{
		m_stDelay->setFree(_hz);
		constexpr double Longest = 8 * Rate;
		m_echoFree = _hz > 0 ? static_cast<int32_t>(std::lround(std::clamp(Rate / _hz, 1.0, Longest))) : 0;
	}

	void Sends::giveSyntaktWords()
	{
		const auto* const delay = m_stWords.data();
		const auto* const reverb = delay + StDelayBytes;
		m_stDelay->setWords({delay[0], delay[1], delay[2], delay[3], delay[4], delay[5]});
		m_stReverb->setWords({reverb[0], reverb[1], reverb[2], reverb[3], reverb[4], reverb[5]});
		m_stReverb->setDelaySend(reverb[StReverbBytes]);
	}

	void Sends::fade(const Send _send)
	{
		const auto s = static_cast<size_t>(_send);
		const float from = m_entering[s] ? 0.0f : 1.0f, to = m_wanted[s] != m_source[s] ? 0.0f : 1.0f;
		m_entering[s] = false;
		if(from == to && to == 1)
			return;
		for(auto& channel : m_out[s])
			for(int i = 0; i < BlockSize; ++i)
				channel[static_cast<size_t>(i)] *= from + (to - from) * static_cast<float>(i) / BlockSize;
	}

	void Sends::changeSource(const Send _send)
	{
		const auto s = static_cast<size_t>(_send);
		if(m_wanted[s] == m_source[s])
			return;
		const bool fromSyntakt = m_source[s] == SendSource::Syntakt;
		if(_send == Send::Reverb && fromSyntakt)
			m_stReverb->clear();
		else if(_send == Send::Reverb)
			m_gateBox->clear();
		else if(fromSyntakt)
			m_stDelay->clear();
		else
		{
			m_echo->clear();
			std::copy_n(m_stTarget.begin(), StDelayBytes, m_stWords.begin());
			giveSyntaktWords();
			m_stDelay->settle();
		}
		m_source[s] = m_wanted[s];
		m_entering[s] = true;
	}

	void Sends::block()
	{
		if(++m_tick == TickBlocks)
		{
			m_tick = 0;
			const auto due = m_words.tick();
			if(due == md::fast::SendWords::Due::Echo)
			{
				auto words = m_words.echo();
				if(m_echoFree)
					md::fast::echoDelayWords(m_echoFree, words);
				m_echo->setWords(words);
			}
			else if(due == md::fast::SendWords::Due::GateBox)
				m_gateBox->setWords(m_words.gateBox());
		}

		// The OS's step; nothing to hear in a jump when something comes to wake them
		const bool asleep = !sounding();
		const bool fed = m_fed;
		m_fed = false;
		// PRE jumps: a slewed read position moves in dozens of whole samples, 4.6 times the step of one jump on a tone
		for(size_t w = 0; w < m_stWords.size(); ++w)
			m_stWords[w] = (fed && asleep) || w == PreWord ? m_stTarget[w] : m_stTarget[w] + 3 * (m_stWords[w] - m_stTarget[w]) / 4;
		giveSyntaktWords();
		if(fed && asleep)
		{
			m_stDelay->settle();
			m_echo->settle();
		}

		if(fed || m_wanted != m_source)
			m_quiet = 0;
		else if(asleep)
			return;

		auto& delay = m_out[static_cast<size_t>(Send::Delay)];
		auto& reverb = m_out[static_cast<size_t>(Send::Reverb)];
		const auto& delayIn = m_in[static_cast<size_t>(Send::Delay)];
		const auto& reverbIn = m_in[static_cast<size_t>(Send::Reverb)];
		if(m_source[static_cast<size_t>(Send::Delay)] == SendSource::Machinedrum)
			m_echo->process(delayIn[0].data(), delayIn[1].data(), delay[0].data(), delay[1].data());
		else
			m_stDelay->process(delayIn[0].data(), delayIn[1].data(), delay[0].data(), delay[1].data());
		fade(Send::Delay);
		if(m_source[static_cast<size_t>(Send::Reverb)] == SendSource::Machinedrum)
			m_gateBox->process(reverbIn[0].data(), reverbIn[1].data(), delay[0].data(), delay[1].data(),
				reverb[0].data(), reverb[1].data());
		else
			m_stReverb->process(reverbIn[0].data(), reverbIn[1].data(), delay[0].data(), delay[1].data(),
				reverb[0].data(), reverb[1].data());
		fade(Send::Reverb);
		changeSource(Send::Delay);
		changeSource(Send::Reverb);
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
