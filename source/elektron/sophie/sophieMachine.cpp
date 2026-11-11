#include "sophieMachine.h"

#include <algorithm>
#include <cmath>

namespace sophie
{
	namespace
	{
		float linear(const uint8_t _value, const float _max)
		{
			return static_cast<float>(_value) * _max / 127.0f;
		}

		float exponential(const uint8_t _value, const float _min, const float _max)
		{
			return _min * std::pow(_max / _min, static_cast<float>(_value) / 127.0f);
		}

		float choice(const uint8_t _value, const int _count)
		{
			return static_cast<float>((_value * _count) >> 7);
		}
	}

	float machineValue(const int _index, const uint8_t _value)
	{
		const auto v = std::min<uint8_t>(_value, 127);

		switch (_index)
		{
		case SOPHIE_MODEL:			return choice(v, 6);
		case SOPHIE_TUNE:			return std::clamp((static_cast<float>(v) - 64.0f) * 0.5f, -24.0f, 24.0f);
		case SOPHIE_SWEEP:			return v < 64 ? (static_cast<float>(v) - 64.0f) * 100.0f / 64.0f
										: (static_cast<float>(v) - 64.0f) * 100.0f / 63.0f;
		case SOPHIE_DECAY:			return exponential(v, 0.03f, 4.0f);
		case SOPHIE_FILTER_TYPE:	return choice(v, 5);
		case SOPHIE_RING_TIME:		return exponential(v, 0.5f, 30.0f);
		case SOPHIE_RING_FEEDBACK:	return linear(v, 95.0f);
		case SOPHIE_METAL_DECAY:	return v == 0 ? 0.0f : 5.0f * std::pow(400.0f, static_cast<float>(v - 1) / 126.0f);
		case SOPHIE_FEEDBACK_MODE:
		case SOPHIE_TAIL_MODE:		return v >= 64 ? 1.0f : 0.0f;
		default:					return linear(v, 100.0f);	// Color, Metal, Feedback, Crush, Drive, Cutoff,
																// Resonance, Ring Mix, Ring Tone
		}
	}

	void setMachineValue(sophie_t& _sophie, const int _index, const uint8_t _value)
	{
		if (_index < 0 || _index >= MachineParamCount)
			return;
		sophie_set_param(&_sophie, static_cast<sophie_param_t>(_index), machineValue(_index, _value));
	}
}
