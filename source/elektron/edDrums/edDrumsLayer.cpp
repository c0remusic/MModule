#include "edDrumsLayer.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "edDrumsVintage.h"

namespace elektron::drums
{
	namespace
	{
		// Under this, a decaying voice is over (-100 dB)
		constexpr float Over = 1e-5f;
		constexpr double MaxOffsetSeconds = 0.1;
	}

	double SampleLayers::reductionStep(const int _br)
	{
		// The Analog Rytm's BR, measured (lockstep-post-v1, decision 11): a continuous step of 2^(6.06 + 0.0729 BR) LSB of
		// the 16-bit input, truncated, about 9.9 - 0.073 BR bits kept; BR 0 leaves the input whole
		if(_br <= 0)
			return 1;
		return std::exp2(6.06 + 0.0729 * std::min(_br, 127));
	}

	double SampleLayers::attackSeconds(const int _attack)
	{
		const double a = std::clamp(_attack, 0, 127) / 127.0;
		return 2 * a * a;
	}

	SampleLayers::SampleLayers(const double _rate) : m_rate(_rate)
	{
		m_note.fill(60);
		for(int v = 0; v < Voices; ++v)
		{
			m_players[static_cast<size_t>(v)].setOutputRate(_rate);
			m_players[static_cast<size_t>(v)].setSamples(&m_samples);
		}
		for(int t = 0; t < Lanes; ++t)
			apply(t);
	}

	void SampleLayers::apply(const int _track)
	{
		const auto i = static_cast<size_t>(_track);
		const LayerSettings& s = m_settings[i];
		// TUNE in semitones, FINE the 1/256 of one the player's fine part counts, a half semitone either way
		const int tune = std::clamp(s.tune * 256 + (s.fine - 64) * 2, 0, 127 * 256 + 255);
		for(auto& player : m_players)
		{
			player.setParameter(_track, Dt2Player::Tune, static_cast<uint8_t>(tune >> 8), static_cast<uint8_t>(tune & 255));
			player.setParameter(_track, Dt2Player::Mode, 0, 0);
			player.setParameter(_track, Dt2Player::Loop, static_cast<uint8_t>(s.loop), 0);
			player.setParameter(_track, Dt2Player::Start, static_cast<uint8_t>(s.start), 0);
			player.setParameter(_track, Dt2Player::Length, static_cast<uint8_t>(s.length), 0);
			player.setParameter(_track, Dt2Player::Play, static_cast<uint8_t>(s.play), 0);
			player.setNote(_track, s.noteTrack ? m_note[i] : 60);
		}
		const float level = static_cast<float>(std::clamp(s.level, 0, 127)) / 127.0f;
		m_level[i] = level * level;
		m_step[i] = s.bitReduction > 0 ? static_cast<float>(reductionStep(s.bitReduction) / 32768) : 0.0f;
	}

	void SampleLayers::setSample(const int _track, const Dt2Samples::Slot& _slot)
	{
		cut(_track);
		m_samples.slots[static_cast<size_t>(_track)] = _slot;
	}

	void SampleLayers::setSettings(const int _track, const LayerSettings& _settings)
	{
		const bool wasOn = m_settings[static_cast<size_t>(_track)].on;
		m_settings[static_cast<size_t>(_track)] = _settings;
		apply(_track);
		if(wasOn && !_settings.on)
			cut(_track);
	}

	void SampleLayers::setNote(const int _track, const int _note)
	{
		const auto i = static_cast<size_t>(_track);
		m_note[i] = _note;
		for(auto& player : m_players)
			player.setNote(_track, m_settings[i].noteTrack ? _note : 60);
	}

	void SampleLayers::hit(const int _track, const int _startIn, const int _moved)
	{
		const auto i = static_cast<size_t>(_track);
		// What sounds goes on from where it was written to: over the _moved samples the ring already holds, silence; after a
		// gap, which the ring holds at its last sample, on from there. A voice waiting starts where it would have
		for(auto& voices : m_voices)
		{
			Voice& voice = voices[i];
			if(voice.wait >= 0)
				voice.wait = std::max(voice.wait + _moved, 0);
			else if(voice.live && _moved > 0)
				voice.silence += _moved;
		}
		const LayerSettings& s = m_settings[i];
		if(!s.on || !m_samples.slots[i].pcm)
			return;
		const auto offset = static_cast<int>(std::lround(std::clamp(s.offset, 0, 127) / 127.0 * MaxOffsetSeconds * m_rate));
		// A Hit before the last one's voice has started takes its place; otherwise the other voice, the last one fading
		// under it when it starts
		int v = m_current[i];
		if(m_voices[static_cast<size_t>(v)][i].wait < 0)
			v = 1 - v;
		m_voices[static_cast<size_t>(v)][i].wait = _startIn + offset;
		m_current[i] = v;
		m_sounding |= 1u << _track;
	}

	void SampleLayers::cut(const int _track)
	{
		const auto i = static_cast<size_t>(_track);
		for(auto& voices : m_voices)
		{
			Voice& voice = voices[i];
			voice.wait = -1;
			if(voice.live && !voice.tail)
				voice.fade = true;
		}
		updateSounding(_track);
	}

	void SampleLayers::stop(const int _track)
	{
		const auto i = static_cast<size_t>(_track);
		for(int v = 0; v < Voices; ++v)
		{
			m_voices[static_cast<size_t>(v)][i] = {};
			m_players[static_cast<size_t>(v)].stop(_track);
		}
		m_sounding &= ~(1u << _track);
	}

	void SampleLayers::startVoice(const int _track, const int _voice)
	{
		const auto i = static_cast<size_t>(_track);
		const LayerSettings& s = m_settings[i];
		auto& player = m_players[static_cast<size_t>(_voice)];
		Voice& voice = m_voices[static_cast<size_t>(_voice)][i];
		player.start(_track);
		// Its place inside the block: as much silence first
		voice.silence = voice.wait;
		voice.written = voice.read = 0;
		voice.wait = -1;
		voice.live = player.playing(_track);
		voice.fade = voice.tail = false;
		const double attack = attackSeconds(s.attack);
		voice.attacking = attack > 0;
		voice.gain = voice.attacking ? 0.0f : 1.0f;
		voice.attackStep = voice.attacking ? static_cast<float>(1 / (attack * m_rate)) : 1.0f;
		voice.holdLeft = std::lround(vintage::holdSeconds(s.hold) * m_rate);
		const double decay = vintage::decaySeconds(s.decay);
		voice.fall = std::isfinite(decay) ? static_cast<float>(std::pow(10.0, -3 / (decay * m_rate))) : 1.0f;
	}

	void SampleLayers::renderVoice(const int _track, const int _voice, float* _x)
	{
		const auto i = static_cast<size_t>(_track);
		auto& player = m_players[static_cast<size_t>(_voice)];
		Voice& voice = m_voices[static_cast<size_t>(_voice)][i];
		// Over: the delay line gives what it still holds, nothing more is written to it
		if(voice.tail)
		{
			drain(voice, _x);
			return;
		}
		float block[BlockSize];
		player.renderLane(_track, block, std::exchange(voice.fade, false));
		voice.tail = !player.playing(_track);
		// BR, then the envelope, then the level
		const float step = m_step[i], level = m_level[i];
		if(step > 0)
		{
			const float inverse = 1 / step;
			for(float& s : block)
				s = std::floor(s * inverse) * step;
		}
		// A gain that stays the block through: held for the whole block, or neither held nor falling
		const bool still = !voice.attacking && (voice.holdLeft >= BlockSize || (voice.holdLeft == 0 && voice.fall == 1));
		if(still)
		{
			voice.holdLeft = std::max<int64_t>(voice.holdLeft - BlockSize, 0);
			const float g = voice.gain * level;
			for(float& s : block)
				s *= g;
		}
		else for(float& s : block)
		{
			s *= voice.gain * level;
			if(voice.attacking)
			{
				voice.gain += voice.attackStep;
				if(voice.gain >= 1)
				{
					voice.gain = 1;
					voice.attacking = false;
				}
			}
			else if(voice.holdLeft > 0)
			{
				--voice.holdLeft;
			}
			else
			{
				voice.gain *= voice.fall;
			}
		}
		// The decay has closed the voice: nothing more to read
		if(!voice.tail && !voice.attacking && voice.holdLeft == 0 && voice.gain < Over)
		{
			player.stop(_track);
			voice.tail = true;
		}
		for(const float s : block)
			voice.line[voice.written++ & (Delay - 1)] = s;
		drain(voice, _x);
	}

	void SampleLayers::drain(Voice& _voice, float* _x)
	{
		// The delay line's next block: the silence it owes first, then what it holds
		for(int n = 0; n < BlockSize; ++n)
		{
			if(_voice.silence > 0)
				--_voice.silence;
			else if(_voice.read < _voice.written)
				_x[n] += _voice.line[_voice.read++ & (Delay - 1)];
		}
		if(_voice.tail && _voice.read == _voice.written && _voice.silence == 0)
			_voice.live = false;
	}

	void SampleLayers::updateSounding(const int _track)
	{
		const auto i = static_cast<size_t>(_track);
		bool sounds = false;
		for(const auto& voices : m_voices)
			sounds = sounds || voices[i].live || voices[i].wait >= 0;
		m_sounding = sounds ? m_sounding | 1u << _track : m_sounding & ~(1u << _track);
	}

	void SampleLayers::render(const int _track, float* _x)
	{
		if(!sounding(_track))
			return;
		const auto i = static_cast<size_t>(_track);
		// The voices whose wait ends in this block start, the other fading under the one that does
		for(int v = 0; v < Voices; ++v)
		{
			Voice& voice = m_voices[static_cast<size_t>(v)][i];
			if(voice.wait < 0)
				continue;
			if(voice.wait >= BlockSize)
			{
				voice.wait -= BlockSize;
				continue;
			}
			startVoice(_track, v);
			Voice& other = m_voices[static_cast<size_t>(1 - v)][i];
			if(other.live && !other.tail)
				other.fade = true;
		}
		for(int v = 0; v < Voices; ++v)
			if(m_voices[static_cast<size_t>(v)][i].live)
				renderVoice(_track, v, _x);
		updateSounding(_track);
	}
}
