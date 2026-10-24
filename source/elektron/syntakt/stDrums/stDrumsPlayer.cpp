#include "stDrumsPlayer.h"

#include <algorithm>
#include <cmath>

#include "synthLib/sampleRateTime.h"

namespace stDrums
{
	using syntakt::Syntakt;

	int machineIndex(const int _machine)
	{
		for(size_t i = 0; i < Machines.size(); ++i)
			if(Machines[i] == _machine)
				return static_cast<int>(i);
		return -1;
	}

	float mainSample(const int32_t* const* _outputs, const size_t _s, const int _side)
	{
		// The four buses as the analog mixer takes them, each Q31 and saturated apart: summed in double, which holds
		// the sum exactly, then one rounding to float
		const double sum = static_cast<double>(_outputs[Syntakt::DirectLeft + _side][_s])
			+ static_cast<double>(_outputs[Syntakt::FxLeft + _side][_s])
			+ static_cast<double>(_outputs[Syntakt::DirectLeftB + _side][_s])
			+ static_cast<double>(_outputs[Syntakt::FxLeftB + _side][_s]);
		return static_cast<float>(sum * (1.0 / 2147483648.0));
	}

	int valueOf(const uint16_t _word)
	{
		return std::clamp((_word + 0x80) >> 8, 0, 127);
	}

	Engine::Engine(std::shared_ptr<const Firmware> _firmware)
		: m_firmware(std::move(_firmware))
		, m_module(std::make_unique<Syntakt>(m_firmware->section3, m_firmware->section7, Syntakt::MaxTracks))
	{
		// The descriptors' ranges, read here once: the audio thread takes them from the tables
		for(size_t m = 0; m < Machines.size(); ++m)
			for(int id = 0; id < Syntakt::TrackParameterEnd; ++id)
				m_ranges[m][static_cast<size_t>(id)] = m_module->parameterRange(Machines[m], id);
		for(int id = 0; id < Syntakt::FxParameterEnd; ++id)
			m_fxRanges[static_cast<size_t>(id)] = m_module->fxParameterRange(id);
	}

	uint16_t Engine::trackWord(const int _machineIndex, const int _id, const int _value) const
	{
		const int machine = std::clamp(_machineIndex, 0, static_cast<int>(Machines.size()) - 1);
		const auto& r = m_ranges[static_cast<size_t>(machine)]
			[static_cast<size_t>(std::clamp(_id, 0, Syntakt::TrackParameterEnd - 1))];
		return static_cast<uint16_t>(std::clamp(std::clamp(_value, 0, 127) << 8, static_cast<int>(r.min),
			static_cast<int>(r.max)));
	}

	uint16_t Engine::fxWord(const int _id, const int _value) const
	{
		const auto& r = m_fxRanges[static_cast<size_t>(std::clamp(_id, 0, Syntakt::FxParameterEnd - 1))];
		return static_cast<uint16_t>(std::clamp(std::clamp(_value, 0, 127) << 8, static_cast<int>(r.min),
			static_cast<int>(r.max)));
	}

	void Player::prepare(const double _hostRate, const int _maxBlock, const std::array<bool, Channels>& _used)
	{
		m_usedCount = 0;
		for(int c = 0; c < Channels; ++c)
			if(c < 2 || _used[static_cast<size_t>(c)])
				m_used[static_cast<size_t>(m_usedCount++)] = c;

		m_hostRate = _hostRate;
		m_host = 0;
		m_source = 0;
		m_eventCount = m_eventNext = 0;
		m_q31.assign(static_cast<size_t>(Syntakt::Outputs16) * Chunk, 0);
		for(int c = 0; c < Syntakt::Outputs16; ++c)
			m_q31Channels[static_cast<size_t>(c)] = m_q31.data() + static_cast<size_t>(c) * Chunk;

		m_resampler.reset();
		m_latency = 0;
		if(std::lround(_hostRate) == Rate)
			return;
		m_resampler = std::make_unique<synthLib::Resampler>(static_cast<float>(Rate), static_cast<float>(_hostRate),
			synthLib::Resampler::Mode::MameHq);
		m_resampler->prepare(static_cast<uint32_t>(m_usedCount), static_cast<uint32_t>(std::max(_maxBlock, 1)));
		// The filter's delay in source samples, as the host's
		m_latency = static_cast<int>(std::lround(m_resampler->getGroupDelay() * _hostRate / Rate));
	}

	void Player::addEvent(const int _offset, const int _track, const int _note, const int _velocity)
	{
		if(m_eventCount == MaxEvents || _track < 0 || _track >= Tracks)
			return;
		const uint64_t host = m_host + static_cast<uint64_t>(std::max(_offset, 0));
		// The source sample the host's falls on, rounded up: the resampler renders the source up to the host block's
		// end so rounded, so an event is never behind what was rendered
		uint64_t at = m_resampler ? synthLib::rescaleSamplesCeil(host, static_cast<float>(m_hostRate),
			static_cast<float>(Rate)) : host;
		at = std::max(at, m_source);
		if(m_eventCount > m_eventNext)
			at = std::max(at, m_events[static_cast<size_t>(m_eventCount - 1)].at);
		m_events[static_cast<size_t>(m_eventCount++)] = {at, _track, _note, _velocity};
	}

	void Player::noteOn(const int _offset, const int _track, const int _note, const int _velocity)
	{
		addEvent(_offset, _track, std::clamp(_note, 0, 127), std::clamp(_velocity, 1, 127));
	}

	void Player::noteOff(const int _offset, const int _track)
	{
		addEvent(_offset, _track, -1, 0);
	}

	void Player::applyEvents(const uint64_t _until)
	{
		for(; m_eventNext < m_eventCount && m_events[static_cast<size_t>(m_eventNext)].at <= _until; ++m_eventNext)
		{
			const auto& e = m_events[static_cast<size_t>(m_eventNext)];
			if(!m_engine)
				continue;
			if(e.note < 0)
				m_engine->module().noteOff(e.track);
			else
				m_engine->module().noteOn(e.track, e.note, e.velocity);
		}
		if(m_eventNext == m_eventCount)
			m_eventNext = m_eventCount = 0;
	}

	void Player::renderSource(synthLib::TAudioOutputs& _outputs, const uint32_t _count)
	{
		for(uint32_t done = 0; done < _count;)
		{
			applyEvents(m_source);
			uint64_t end = m_source + (_count - done);
			if(m_eventNext < m_eventCount)
				end = std::min(end, m_events[static_cast<size_t>(m_eventNext)].at);
			const auto length = static_cast<uint32_t>(std::min<uint64_t>(end - m_source, Chunk));

			// _outputs holds the Channels used, in order: Main first
			if(!m_engine)
			{
				for(int k = 0; k < m_usedCount; ++k)
					std::fill_n(_outputs[static_cast<size_t>(k)] + done, length, 0.0f);
			}
			else
			{
				m_engine->module().render(m_q31Channels.data(), length);
				const int32_t* const* q = m_q31Channels.data();
				float* left = _outputs[0] + done;
				float* right = _outputs[1] + done;
				for(uint32_t s = 0; s < length; ++s)
				{
					left[s] = mainSample(q, s, 0);
					right[s] = mainSample(q, s, 1);
				}
				for(int k = 2; k < m_usedCount; ++k)
				{
					const int t = m_used[static_cast<size_t>(k)] - 2;
					float* out = _outputs[static_cast<size_t>(k)] + done;
					for(uint32_t s = 0; s < length; ++s)
						out[s] = stemSample(q, s, t);
				}
			}
			m_source += length;
			done += length;
		}
		// A trig made at the last sample rendered takes effect at the same block as one made before the next
		applyEvents(m_source);
	}

	void Player::render(float* const* _outputs, const int _count)
	{
		if(_count <= 0)
			return;
		synthLib::TAudioOutputs outputs{};
		for(int k = 0; k < m_usedCount; ++k)
			outputs[static_cast<size_t>(k)] = _outputs[m_used[static_cast<size_t>(k)]];
		if(!m_resampler)
			renderSource(outputs, static_cast<uint32_t>(_count));
		else
			m_resampler->process(outputs, static_cast<uint32_t>(m_usedCount), static_cast<uint32_t>(_count), false,
				[this](synthLib::TAudioOutputs& _source, const uint32_t _n) { renderSource(_source, _n); });
		m_host += static_cast<uint64_t>(_count);
	}
}
