// What processBlock tells the engine of the host's transport (ticket 13 of .scratch/elektron-drums-v1), in process, as no host
// parameter reaches an LFO yet: playing, the position and the tempo; a host's jump; stopped, the LFOs run on; no playhead.
//
// GEARMULATOR_MD_FIRMWARE_BIN names the flash image; exit 77 without it.

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <string>

#include "edDrumsProcessor.h"

namespace
{
	using namespace elektron::drums;

	constexpr int Block = 128;

	void require(const bool _condition, const std::string& _message)
	{
		if(!_condition)
			throw std::runtime_error(_message);
	}

	class Transport final : public juce::AudioPlayHead
	{
	public:
		juce::Optional<PositionInfo> getPosition() const override
		{
			PositionInfo info;
			info.setBpm(bpm);
			info.setIsPlaying(playing);
			if(hasQuarters)
				info.setPpqPosition(quarters);
			return info;
		}

		double bpm = 120, quarters = 0;
		bool playing = true, hasQuarters = true;
	};

	void block(Processor& _processor)
	{
		juce::AudioBuffer<float> buffer(2 * Outputs, Block);
		juce::MidiBuffer midi;
		_processor.processBlock(buffer, midi);
	}
}

int main()
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	if(Library::findFlashImage({}).empty())
	{
		std::printf("SKIP: set GEARMULATOR_MD_FIRMWARE_BIN to the Machinedrum UW flash image\n");
		return 77;
	}
	const juce::ScopedJuceInitialiser_GUI juce;
	try
	{
		Processor processor;
		Transport transport;
		processor.setPlayHead(&transport);
		auto layout = processor.getBusesLayout();
		for(auto& bus : layout.outputBuses)
			bus = juce::AudioChannelSet::stereo();
		require(processor.setBusesLayout(layout), "all the outputs");
		processor.prepareToPlay(SampleRate, Block);
		auto& engine = *processor.engine();

		// Playing: where the host says, at each block's start, and on at its tempo
		const double perBlock = 120.0 / 60 / SampleRate * Block;
		transport.quarters = 7.5;
		block(processor);
		require(std::abs(engine.position() - (7.5 + perBlock)) < 1e-9, "the transport's position");
		std::printf("  playing: position %.6f after a block from 7.5\n", engine.position());

		// A jump
		transport.quarters = 2;
		block(processor);
		require(std::abs(engine.position() - (2 + perBlock)) < 1e-9, "a host's jump");

		// Its tempo
		transport.bpm = 60;
		transport.quarters = engine.position();
		const double from = engine.position();
		block(processor);
		require(std::abs(engine.position() - from - perBlock / 2) < 1e-9, "the host's tempo");

		// Stopped: the position it gives stands still, the LFOs do not
		transport.playing = false;
		transport.quarters = 100;
		const double stopped = engine.position();
		block(processor);
		require(std::abs(engine.position() - stopped - perBlock / 2) < 1e-9, "a stopped transport runs on");

		// Started again: the host's
		transport.playing = true;
		block(processor);
		require(std::abs(engine.position() - (100 + perBlock / 2)) < 1e-9, "play takes the host's position");

		// Playing with no position to give: it runs on
		transport.hasQuarters = false;
		const double told = engine.position();
		block(processor);
		require(std::abs(engine.position() - told - perBlock / 2) < 1e-9, "a host with no position");
		transport.hasQuarters = true;

		// What a host must not send stays out
		for(const double wrong : {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()})
		{
			const double before = engine.position();
			transport.bpm = 60;
			transport.quarters = wrong;
			block(processor);
			require(std::abs(engine.position() - before - perBlock / 2) < 1e-9, "a position that is no number");
			transport.bpm = wrong;
			transport.playing = false;
			const double held = engine.position();
			block(processor);
			require(std::abs(engine.position() - held - perBlock) < 1e-9, "a tempo that is no number: the parameter's, 120");
			transport.bpm = 60;
			transport.quarters = engine.position();
			transport.playing = true;
			block(processor);
		}

		// An LFO's phase is the host's: a cycle of half a quarter note, wherever the host goes
		LfoSettings lfo;
		lfo.rate.division = 3;
		lfo.depth = 10;
		lfo.target = LfoTarget::Level;
		engine.setLfo(0, 0, lfo);
		transport.bpm = 120;
		for(const double quarters : {1.3, 7.21, 2.05})
		{
			transport.quarters = quarters;
			block(processor);
			const double phase = engine.lfoPhase(0, 0);
			const double expected = (quarters + perBlock) / 0.5 - std::floor((quarters + perBlock) / 0.5);
			std::printf("  the host at %.2f: phase %.4f, %.4f at the block's end\n", quarters, phase, expected);
			require(std::abs(phase - expected) <= 2 * 32 * perBlock / Block / 0.5, "an LFO's phase is not the host's");
		}

		// No playhead: the Tempo parameter, from where it stands
		processor.setPlayHead(nullptr);
		const double alone = engine.position();
		block(processor);
		require(std::abs(engine.position() - alone - perBlock) < 1e-9, "no playhead");
		std::printf("PASS: the host's transport reaches the engine\n");
		return 0;
	}
	catch(const std::exception& e)
	{
		std::printf("FAIL: %s\n", e.what());
		return 1;
	}
}
