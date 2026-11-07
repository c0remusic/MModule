#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "edDrumsEngine.h"

// The instrument: the engine (elektron::drums::Engine) as a plain JUCE AudioProcessor, VST3. Notes 36-51 on any channel
// hit Tracks 1-16 with their velocity, their note-off ends a held voice; outputs Main and Out 01-15, stereo. It runs at
// 44.1 kHz, the one rate both families are right at: at any other it plays silence, reporting no latency, until ticket
// 16. The Machinedrum's flash image is read once, as the instrument loads, from the variable
// GEARMULATOR_MD_FIRMWARE_BIN or the data folder's roms.
//
// The parameters of a Track, a first version that ticket 18 replaces, the IDs t01_*, the names "T01 ...": Machine and
// Chokes, which a host does not automate, Machine 1-8, Note, Level, Pan, Output.
namespace elektron::drums
{
	class Processor final : public juce::AudioProcessor, juce::Timer
	{
	public:
		Processor();
		~Processor() override;

		void prepareToPlay(double _sampleRate, int _maxBlock) override;
		void releaseResources() override {}
		bool isBusesLayoutSupported(const BusesLayout& _layout) const override;
		void processBlock(juce::AudioBuffer<float>& _buffer, juce::MidiBuffer& _midi) override;
		using AudioProcessor::processBlock;

		juce::AudioProcessorEditor* createEditor() override;
		bool hasEditor() const override { return true; }

		const juce::String getName() const override { return ED_DRUMS_NAME; }
		bool acceptsMidi() const override { return true; }
		bool producesMidi() const override { return false; }
		double getTailLengthSeconds() const override { return 2.0; }

		int getNumPrograms() override { return 1; }
		int getCurrentProgram() override { return 0; }
		void setCurrentProgram(int) override {}
		const juce::String getProgramName(int) override { return {}; }
		void changeProgramName(int, const juce::String&) override {}

		void getStateInformation(juce::MemoryBlock& _data) override;
		void setStateInformation(const void* _data, int _size) override;

		static juce::File dataFolder();

		// The machines a Track's parameter chooses from, and what each has of its data (dataOf)
		const std::vector<Machine>& machines() const { return m_machines; }
		MachineData machineData(const size_t _choice) const { return dataOf(*m_library, m_machines[_choice]); }

		// The audio thread's, after prepareToPlay, for a test between blocks: no host parameter reaches an LFO yet (ticket 18)
		Engine* engine() { return m_engine.get(); }

	private:
		struct TrackParameters
		{
			juce::AudioParameterChoice* machine = nullptr;
			std::array<juce::AudioParameterInt*, 8> values{};
			juce::AudioParameterInt* note = nullptr;
			juce::AudioParameterInt* level = nullptr;
			juce::AudioParameterInt* pan = nullptr;
			juce::AudioParameterInt* output = nullptr;
			juce::AudioParameterInt* chokes = nullptr;
		};

		static BusesProperties buses();
		void timerCallback() override;
		void sync(bool _hostIsTruth);

		std::unique_ptr<Library> m_library;
		std::vector<Machine> m_machines;	// Machine's choices
		std::array<TrackParameters, Tracks> m_tracks{};
		juce::AudioParameterFloat* m_tempo = nullptr;
		std::array<float*, 2 * Outputs> m_outputs{};

		std::unique_ptr<Engine> m_engine;	// the audio thread's after prepareToPlay, none at a rate it does not run at
		std::array<int, Tracks> m_machineSeen{};	// Machine's choice whose defaults the Track's parameters took
		std::atomic<uint32_t> m_stateSerial{0};
		uint32_t m_stateSeen = 0;
		std::atomic<bool> m_reread{false};	// the audio thread changed parameters: the host reads them again
	};
}
