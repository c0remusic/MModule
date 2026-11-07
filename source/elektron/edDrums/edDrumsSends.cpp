#include "edDrumsSends.h"

#include <algorithm>
#include <cmath>

namespace elektron::drums
{
	Sends::Sends(const md::voice::OsTables& _tables, const uint32_t* const _sine)
		: m_words(_tables), m_echo(std::make_unique<md::fast::Echo>()),
		  m_gateBox(std::make_unique<md::fast::GateBox>(_sine)),
		  m_stDelay(std::make_unique<syntakt::fast::Delay>(Rate)),
		  m_stReverb(std::make_unique<syntakt::fast::Reverb>(Rate)),
		  m_chorus(std::make_unique<ModulatedDelay>(ModulatedDelay::ChorusStep)),
		  m_flanger(std::make_unique<ModulatedDelay>(ModulatedDelay::FlangerStep)),
		  m_phaser(std::make_unique<Phaser>())
	{
		// Nothing fed back until said otherwise
		m_chorusBytes[ChorusFeedback] = m_phaserBytes[PhaserFeedback] = 64;
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

	namespace
	{
		template<typename Block> void ramp(Block& _block, const float _from, const float _to)
		{
			for(auto& channel : _block)
				for(size_t i = 0; i < channel.size(); ++i)
					channel[i] *= _from + (_to - _from) * static_cast<float>(i) / static_cast<float>(channel.size());
		}
	}

	void Sends::fade(const Send _send)
	{
		const auto s = static_cast<size_t>(_send);
		const float from = m_entering[s] ? 0.0f : 1.0f, to = m_wanted[s] != m_source[s] ? 0.0f : 1.0f;
		m_entering[s] = false;
		if(from == to && to == 1)
			return;
		ramp(m_out[s], from, to);
	}

	void Sends::changeSource(const Send _send)
	{
		const auto s = static_cast<size_t>(_send);
		if(m_wanted[s] == m_source[s])
			return;
		const bool fromSyntakt = m_source[s] == SendSource::Syntakt;
		if(_send == Send::Phaser && m_source[s] == SendSource::Flanger)
			m_flanger->clear();
		else if(_send == Send::Phaser)
			m_phaser->clear();
		else if(_send == Send::Reverb && fromSyntakt)
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

	void Sends::setChorus(const int _byte, const uint8_t _value)
	{
		m_chorusBytes[static_cast<size_t>(_byte)] = _value;
		const auto& b = m_chorusBytes;
		m_chorus->set(b[ChorusDelay], b[ChorusDepth], b[ChorusRate], b[ChorusFeedback]);
	}

	void Sends::setPhaser(const int _byte, const uint8_t _value)
	{
		m_phaserBytes[static_cast<size_t>(_byte)] = _value;
		const auto& b = m_phaserBytes;
		m_phaser->set(b[PhaserStages], b[PhaserFrequency], b[PhaserSpread], b[PhaserDepth], b[PhaserRate],
			b[PhaserFeedback]);
		m_flanger->set(b[PhaserFrequency], b[PhaserDepth], b[PhaserRate], b[PhaserFeedback]);
		setSource(Send::Phaser, b[PhaserMode] ? SendSource::Flanger : SendSource::Phaser);
	}

	template<typename Effect>
	void Sends::runOurs(const Send _send, Effect& _effect, const float _level, float& _played)
	{
		auto& out = m_out[static_cast<size_t>(_send)];
		const auto& in = m_in[static_cast<size_t>(_send)];
		_effect.process(in[0].data(), in[1].data(), out[0].data(), out[1].data());
		ramp(out, _played, _level);
		_played = _level;
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

		const bool asleep = !sounding();
		const bool fed = m_fed;
		m_fed = false;
		if(asleep && !fed && m_wanted == m_source)
			return;

		// The OS's step; nothing to hear in a jump when something comes to wake them
		// PRE jumps: a slewed read position moves in dozens of whole samples, 4.6 times the step of one jump on a tone
		for(size_t w = 0; w < m_stWords.size(); ++w)
		{
			const bool jump = asleep || w == PreWord;
			m_stWords[w] = jump ? m_stTarget[w] : m_stTarget[w] + 3 * (m_stWords[w] - m_stTarget[w]) / 4;
		}
		giveSyntaktWords();
		if(asleep)
		{
			m_stDelay->settle();
			m_echo->settle();
			m_chorus->settle();
			m_flanger->settle();
			m_phaser->settle();
		}

		if(fed || m_wanted != m_source)
			m_quiet = 0;

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
		runOurs(Send::Chorus, *m_chorus, sendFxLevel(m_chorusBytes[ChorusLevel]), m_chorusPlayed);
		const float phaserLevel = sendFxLevel(m_phaserBytes[PhaserLevel]);
		if(m_source[static_cast<size_t>(Send::Phaser)] == SendSource::Flanger)
			runOurs(Send::Phaser, *m_flanger, phaserLevel, m_phaserPlayed);
		else
			runOurs(Send::Phaser, *m_phaser, phaserLevel, m_phaserPlayed);
		fade(Send::Phaser);
		changeSource(Send::Delay);
		changeSource(Send::Reverb);
		changeSource(Send::Phaser);
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
