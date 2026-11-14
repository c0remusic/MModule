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
		  m_vibrato(std::make_unique<Vibrato>()), m_air(std::make_unique<Air>()),
		  m_vintage(std::make_unique<Vintage>()), m_phaser(std::make_unique<Phaser>()), m_dp4(std::make_unique<Dp4Phaser>())
	{
		// Nothing fed back until said otherwise
		m_chorusBytes[ChorusFeedback] = m_phaserBytes[PhaserFeedback] = 64;
		m_chorusBytes[ChorusShape] = 1;
		m_phaserBytes[PhaserSpread] = 64;		// the DP/4's notch at its middle, nothing mixed back
		m_phaserBytes[PhaserDp4Phase] = 1;
		m_phaserBytes[PhaserDp4DelayFeedback] = 64;
	}

	void Sends::setTempo(const uint32_t _tempo)
	{
		const uint32_t tempo = std::clamp(_tempo, 720u, 7200u);
		m_words.setTempo(tempo);
		m_stDelay->setTempo(tempo / 24.0);
	}

	void Sends::setDelayFree(const double _hz)
	{
		m_freeHz = _hz;
		giveDelayTime();
	}

	void Sends::setDelaySync(const bool _sync)
	{
		m_delaySync = _sync;
		giveDelayTime();
	}

	void Sends::setDelayNote(const double _quarters)
	{
		m_delayNote = _quarters;
		giveDelayTime();
	}

	void Sends::setLfoSync(const Send _send, const bool _sync)
	{
		m_lfoSync[_send == Send::Chorus ? 0 : 1] = _sync;
		setLfoNote(_send, m_lfoNote[_send == Send::Chorus ? 0 : 1]);
	}

	void Sends::setLfoNote(const Send _send, const double _quarters)
	{
		m_lfoNote[_send == Send::Chorus ? 0 : 1] = _quarters;
		// The bytes' rates first, for a sync that ends
		if(_send == Send::Chorus)
			setChorus(ChorusRate, m_chorusBytes[ChorusRate]);
		else
			setPhaser(PhaserRate, m_phaserBytes[PhaserRate]);
	}

	void Sends::setBpm(const double _bpm)
	{
		if(_bpm == m_bpm || !(_bpm > 0))
			return;
		m_bpm = _bpm;
		if(m_delaySync)
			giveDelayTime();
		giveLfoRates();
	}

	void Sends::giveDelayTime()
	{
		const double hz = m_delaySync && m_delayNote > 0 ? m_bpm / 60 / m_delayNote : m_freeHz;
		m_stDelay->setFree(hz);
		constexpr double Longest = 8 * Rate;
		m_echoFree = hz > 0 ? static_cast<int32_t>(std::lround(std::clamp(Rate / hz, 1.0, Longest))) : 0;
	}

	void Sends::giveLfoRates()
	{
		// Air's sweep follows its depth alone, as the Virus's: no rate to sync
		if(m_lfoSync[0] && m_lfoNote[0] > 0)
		{
			m_chorus->setHz(m_bpm / 60 / m_lfoNote[0]);
			m_vibrato->setHz(m_bpm / 60 / m_lfoNote[0]);
			m_vintage->setHz(m_bpm / 60 / m_lfoNote[0]);
		}
		if(m_lfoSync[1] && m_lfoNote[1] > 0)
		{
			const double hz = m_bpm / 60 / m_lfoNote[1];
			m_phaser->setHz(hz);
			m_flanger->setHz(hz);
			m_dp4->setHz(hz);
		}
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
		if(_send == Send::Chorus && m_source[s] == SendSource::Vibrato)
			m_vibrato->clear();
		else if(_send == Send::Chorus && m_source[s] == SendSource::Air)
			m_air->clear();
		else if(_send == Send::Chorus && m_source[s] == SendSource::Vintage)
			m_vintage->clear();
		else if(_send == Send::Chorus)
			m_chorus->clear();
		else if(_send == Send::Phaser && m_source[s] == SendSource::Flanger)
			m_flanger->clear();
		else if(_send == Send::Phaser && m_source[s] == SendSource::Dp4)
			m_dp4->clear();
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
		// What comes in starts at its settings, not where it last ran
		if(_send == Send::Phaser)
		{
			m_phaser->settle();
			m_flanger->settle();
			m_dp4->settle();
		}
		else if(_send == Send::Chorus)
		{
			m_chorus->settle();
			m_vibrato->settle();
			m_air->settle();
			m_vintage->settle();
		}
	}

	void Sends::setChorus(const int _byte, const uint8_t _value)
	{
		m_chorusBytes[static_cast<size_t>(_byte)] = _value;
		const auto& b = m_chorusBytes;
		m_chorus->set(b[ChorusDelay], b[ChorusDepth], b[ChorusRate], b[ChorusFeedback],
			static_cast<ModulatedDelay::Shape>(std::min<int>(b[ChorusShape], 5)));
		m_vibrato->set(b[ChorusDepth], b[ChorusRate]);
		m_air->set(b[ChorusDepth]);
		m_vintage->set(b[ChorusDepth], b[ChorusRate]);
		giveLfoRates();
		static constexpr SendSource Types[] = {SendSource::Classic, SendSource::Vibrato, SendSource::Air,
			SendSource::Vintage};
		setSource(Send::Chorus, Types[std::min<int>(b[ChorusType], 3)]);
	}

	void Sends::setPhaser(const int _byte, const uint8_t _value)
	{
		m_phaserBytes[static_cast<size_t>(_byte)] = _value;
		const auto& b = m_phaserBytes;
		m_phaser->set(b[PhaserStages], b[PhaserFrequency], b[PhaserSpread], b[PhaserDepth], b[PhaserRate],
			b[PhaserFeedback]);
		m_flanger->set(b[PhaserFrequency], b[PhaserDepth], b[PhaserRate], b[PhaserFeedback]);
		m_dp4->set({b[PhaserFrequency], b[PhaserDepth], b[PhaserRate], b[PhaserFeedback], b[PhaserSpread],
			b[PhaserDp4Phase] != 0, b[PhaserDp4DelayLeft], b[PhaserDp4DelayRight], b[PhaserDp4DelayFeedback],
			b[PhaserDp4SampleHold]});
		giveLfoRates();
		static constexpr SendSource Modes[] = {SendSource::Phaser, SendSource::Flanger, SendSource::Dp4};
		setSource(Send::Phaser, Modes[std::min<int>(b[PhaserMode], 2)]);
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
			m_vibrato->settle();
			m_air->settle();
			m_vintage->settle();
			m_flanger->settle();
			m_dp4->settle();
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
		const float chorusLevel = sendFxLevel(m_chorusBytes[ChorusLevel]);
		const auto type = m_source[static_cast<size_t>(Send::Chorus)];
		if(type == SendSource::Vibrato)
			runOurs(Send::Chorus, *m_vibrato, chorusLevel, m_chorusPlayed);
		else if(type == SendSource::Air)
			runOurs(Send::Chorus, *m_air, chorusLevel, m_chorusPlayed);
		else if(type == SendSource::Vintage)
			runOurs(Send::Chorus, *m_vintage, chorusLevel, m_chorusPlayed);
		else
			runOurs(Send::Chorus, *m_chorus, chorusLevel, m_chorusPlayed);
		fade(Send::Chorus);
		const float phaserLevel = sendFxLevel(m_phaserBytes[PhaserLevel]);
		const auto mode = m_source[static_cast<size_t>(Send::Phaser)];
		if(mode == SendSource::Flanger)
			runOurs(Send::Phaser, *m_flanger, phaserLevel, m_phaserPlayed);
		else if(mode == SendSource::Dp4)
			runOurs(Send::Phaser, *m_dp4, phaserLevel, m_phaserPlayed);
		else
			runOurs(Send::Phaser, *m_phaser, phaserLevel, m_phaserPlayed);
		fade(Send::Phaser);
		changeSource(Send::Delay);
		changeSource(Send::Reverb);
		changeSource(Send::Chorus);
		changeSource(Send::Phaser);
		for(auto& send : m_in)
			for(auto& channel : send)
				channel.fill(0.0f);

		// 20 dB/s a block of 32 at 44.1 kHz
		constexpr float Fall = 0.998331f;
		float returned = 0;
		for(size_t s = 0; s < m_out.size(); ++s)
		{
			float peak = 0;
			for(const auto& channel : m_out[s])
				for(const float x : channel)
					peak = std::max(peak, std::abs(x));
			returned = std::max(returned, peak);
			m_peak[s].store(std::max(peak, m_peak[s].load(std::memory_order_relaxed) * Fall), std::memory_order_relaxed);
		}
		if(returned > Silence || fed)
			m_quiet = 0;
		else if(++m_quiet == QuietBlocks)
			for(auto& send : m_out)
				for(auto& channel : send)
					channel.fill(0.0f);
	}
}
