// The send effects the 16 Tracks share: one of each, stereo returns, in parallel. So far the Machinedrum's Echo and Gate
// Box (md::fast). They run on the engine's blocks of 32, so that a return comes 32 samples after its send whatever the
// host's blocks; delay before reverb in a block, as on the machine. Nothing here allocates after the constructor.
#pragma once

#include <array>
#include <cstdint>
#include <memory>

#include "mdFastEcho.h"
#include "mdFastGateBox.h"
#include "mdFastMasterWords.h"

namespace elektron::drums
{
	enum class Send : uint8_t { Delay, Reverb };
	constexpr int SendCount = 2;

	class Sends
	{
	public:
		static constexpr int BlockSize = 32;

		// _tables and _sine outlive this
		Sends(const md::voice::OsTables& _tables, const uint32_t* _sine);

		// The effects' master bytes, md::fast's EchoByte and GateBoxByte, 0-127
		void setEcho(const int _byte, const uint8_t _value) { m_words.setEcho(_byte, _value); }
		void setGateBox(const int _byte, const uint8_t _value) { m_words.setGateBox(_byte, _value); }
		void setTempo(const uint32_t _tempo) { m_words.setTempo(_tempo); }

		// 0 Main, 1-15 Out 01-15
		void setOutput(const Send _send, const int _output) { m_output[static_cast<size_t>(_send)] = _output; }
		int output(const Send _send) const { return m_output[static_cast<size_t>(_send)]; }

		// This block's input to add to, and last block's return, at the block's sample _at. Taking the input says a Track
		// sends something
		float* input(const Send _send, const int _channel, const int _at)
		{
			m_fed = true;
			return &m_in[static_cast<size_t>(_send)][static_cast<size_t>(_channel)][static_cast<size_t>(_at)];
		}
		const float* output(const Send _send, const int _channel, const int _at) const
		{
			return &m_out[static_cast<size_t>(_send)][static_cast<size_t>(_channel)][static_cast<size_t>(_at)];
		}

		// Whether a return may hold anything: mixing nothing costs nothing
		bool sounding() const { return m_quiet < QuietBlocks; }

		// The block just mixed goes in, its return comes out for the next. Past QuietBlocks with nothing in, nothing runs
		void block();

	private:
		using Block = std::array<std::array<float, BlockSize>, 2>;

		// 10 s of silent blocks: past the Echo's longest line at any feedback that dies out
		static constexpr int QuietBlocks = 13781;
		static constexpr int TickBlocks = 11;	// the OS's tick
		static constexpr float Silence = 1e-5f;

		md::fast::SendWords m_words;
		std::unique_ptr<md::fast::Echo> m_echo;
		std::unique_ptr<md::fast::GateBox> m_gateBox;
		std::array<Block, SendCount> m_in{}, m_out{};
		std::array<int, SendCount> m_output{};
		int m_quiet = QuietBlocks;
		bool m_fed = false;
		int m_tick = 0;
	};
}
