// The send effects the 16 Tracks share: one of each, stereo returns, in parallel. The delay is the Machinedrum's Echo
// or the Syntakt's, the reverb its Gate Box or the Syntakt's, chosen apart. They run on the engine's blocks of 32, so
// that a return comes 32 samples after its send whatever the host's blocks; delay before reverb in a block, as on the
// machines. Nothing here allocates after the constructor.
#pragma once

#include <array>
#include <cstdint>
#include <memory>

#include "mdFastEcho.h"
#include "mdFastGateBox.h"
#include "edDrumsSendFx.h"
#include "mdFastMasterWords.h"
#include "stFastDelay.h"
#include "stFastReverb.h"

namespace elektron::drums
{
	enum class Send : uint8_t { Delay, Reverb, Chorus, Phaser };
	constexpr int SendCount = 4;
	// The delay's and the reverb's two, the Phaser send's three
	enum class SendSource : uint8_t { Machinedrum, Syntakt, Dp4, Phaser = Machinedrum, Flanger = Syntakt };

	// Our chorus's and phaser's settings, 0-127, but Stages 1-6. Mode 0 phaser, 1 flanger, whose delay Frequency sets
	// and which has no Stages or Spread, 2 the DP/4's phaser: Frequency its centre, Depth its width, Spread its notch
	// depth, no Stages. A change of Mode goes as a change of source does
	enum ChorusByte { ChorusRate, ChorusDepth, ChorusDelay, ChorusFeedback, ChorusLevel, ChorusBytes };
	enum PhaserByte
	{
		PhaserMode, PhaserStages, PhaserRate, PhaserDepth, PhaserFrequency, PhaserSpread, PhaserFeedback, PhaserLevel,
		PhaserBytes
	};

	// The Syntakt's bytes of each effect, in the order of its words
	enum SyntaktDelayByte { StDelayTime, StDelayCross, StDelayWidth, StDelayFdbk, StDelayHp, StDelayLp, StDelayBytes };
	enum SyntaktReverbByte
	{
		StReverbPre, StReverbDec, StReverbFreq, StReverbGain, StReverbHp, StReverbLp, StReverbBytes
	};

	class Sends
	{
	public:
		static constexpr int BlockSize = 32;
		static constexpr double Rate = 44100;

		// _tables and _sine outlive this
		Sends(const md::voice::OsTables& _tables, const uint32_t* _sine);

		// The effects' master bytes, md::fast's EchoByte and GateBoxByte, 0-127
		void setEcho(const int _byte, const uint8_t _value) { m_words.setEcho(_byte, _value); }
		void setGateBox(const int _byte, const uint8_t _value) { m_words.setGateBox(_byte, _value); }
		// Our chorus's and phaser's bytes, 0-127, from the next block
		void setChorus(int _byte, uint8_t _value);
		void setPhaser(int _byte, uint8_t _value);

		// The Syntakt's bytes, 0-127. Their words, value << 8, slew at each block by the OS's step
		// (md::voice::Smoother's, rounded so that a word reaches its value), as the Echo's bytes slew at each tick.
		// REV, the delay's return into its reverb, stays 0 until set
		void setSyntaktDelay(const int _byte, const uint8_t _value) { target(_byte, _value); }
		void setSyntaktReverb(const int _byte, const uint8_t _value) { target(StDelayBytes + _byte, _value); }
		void setSyntaktDelaySend(const uint8_t _value) { target(StDelayBytes + StReverbBytes, _value); }
		// Beats per minute x 24; held to the OS's 720 to 7200
		void setTempo(uint32_t _tempo);
		// A delay time of 1 / _hz seconds in place of TIME's and the tempo's, on either delay; 0 gives TIME back. The
		// Syntakt's goes from 0.125 Hz to 1378 Hz, a block. The Echo's stops at 339 Hz, the shortest its pointers keep,
		// and under 0.4536 Hz, 97,244 samples, its pointers slow by whole words a block: 2.2 s x 32 / k of them,
		// 0.441 Hz, 0.427 Hz and so on to 0.114 Hz
		void setDelayFree(double _hz);

		// The return fades out over the next block, the effect left is emptied, so that no tail of it can come back,
		// and the other fades in over the block after. The chorus has one, so never it
		void setSource(const Send _send, const SendSource _source) { m_wanted[static_cast<size_t>(_send)] = _source; }

		// 0 Main, 1-15 Out 01-15
		void setOutput(const Send _send, const int _output) { m_output[static_cast<size_t>(_send)] = _output; }
		int output(const Send _send) const { return m_output[static_cast<size_t>(_send)]; }

		// This block's input to add to, and last block's return, at the block's sample _at. Taking the input says a
		// Track sends something
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

		// The block just mixed goes in, its return comes out for the next. Past QuietBlocks with nothing in, nothing
		// runs
		void block();

	private:
		using Block = std::array<std::array<float, BlockSize>, 2>;

		// 10 s of silent blocks: past the Echo's longest line at any feedback that dies out
		static constexpr int QuietBlocks = 13781;
		static constexpr int TickBlocks = 11;	// the OS's tick
		static constexpr float Silence = 1e-5f;
		static constexpr size_t PreWord = StDelayBytes + StReverbPre;
		static constexpr int SyntaktWords = StDelayBytes + StReverbBytes + 1;	// the delay's, the reverb's, its REV

		void target(const int _word, const uint8_t _value)
		{
			m_stTarget[static_cast<size_t>(_word)] = _value << 8;
		}
		void giveSyntaktWords();
		void fade(Send _send);
		void changeSource(Send _send);
		template<typename Effect> void runOurs(Send _send, Effect& _effect, float _level, float& _played);

		md::fast::SendWords m_words;
		std::unique_ptr<md::fast::Echo> m_echo;
		std::unique_ptr<md::fast::GateBox> m_gateBox;
		std::unique_ptr<syntakt::fast::Delay> m_stDelay;
		std::unique_ptr<syntakt::fast::Reverb> m_stReverb;
		std::array<int, SyntaktWords> m_stTarget{}, m_stWords{};
		int32_t m_echoFree = 0;								// a free time in samples; 0: TIME's
		std::unique_ptr<ModulatedDelay> m_chorus, m_flanger;
		std::unique_ptr<Phaser> m_phaser;
		std::unique_ptr<Dp4Phaser> m_dp4;
		std::array<uint8_t, ChorusBytes> m_chorusBytes{};
		std::array<uint8_t, PhaserBytes> m_phaserBytes{};
		float m_chorusPlayed = 0, m_phaserPlayed = 0;		// the gain the last block ended on
		std::array<SendSource, SendCount> m_source{};		// what plays
		std::array<SendSource, SendCount> m_wanted{};		// what setSource asks: until then, it fades out
		std::array<bool, SendCount> m_entering{};			// what plays fades in
		std::array<Block, SendCount> m_in{}, m_out{};
		std::array<int, SendCount> m_output{};
		int m_quiet = QuietBlocks;
		bool m_fed = false;
		int m_tick = 0;
	};
}
