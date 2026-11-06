#pragma once

#include <array>
#include <vector>

#include "edDrumsEngine.h"

// What the engine's tests share
namespace elektron::drums::test
{
	constexpr int Channels = 2 * Outputs;

	// Every machine that can sound, both families
	inline std::vector<Machine> machines()
	{
		std::vector<Machine> m;
		for(int id = 0; id < SyntaktMachines; ++id)
			m.push_back({Family::Syntakt, static_cast<uint8_t>(id)});
		for(int id = 1; id < 256; ++id)
			if(md::voice::coefficientsPorted(static_cast<uint8_t>(id)))
				m.push_back({Family::Machinedrum, static_cast<uint8_t>(id)});
		return m;
	}

	class Take
	{
	public:
		explicit Take(const int _samples)
			: m_samples(_samples), m_buffers(Channels, std::vector<float>(static_cast<size_t>(_samples), 0.0f))
		{
		}

		// Renders _count samples at _at
		void render(Engine& _e, const int _at, const int _count)
		{
			std::array<float*, Channels> p{};
			for(size_t c = 0; c < Channels; ++c)
				p[c] = m_buffers[c].data() + _at;
			_e.render(p.data(), _count);
			m_at = _at + _count;
		}

		// ... on from where it stands
		void render(Engine& _e, const int _count) { render(_e, m_at, _count); }

		const std::vector<float>& channel(const int _c) const { return m_buffers[static_cast<size_t>(_c)]; }
		int samples() const { return m_samples; }
		int at() const { return m_at; }

	private:
		int m_samples;
		int m_at = 0;
		std::vector<std::vector<float>> m_buffers;
	};
}
