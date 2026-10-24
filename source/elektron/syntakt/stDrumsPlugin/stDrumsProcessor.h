#pragma once

#include <array>
#include <atomic>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>

#include "stDrumsParameters.h"
#include "stDrumsPlayer.h"

// ST Drums: the Syntakt's twelve digital machines on 16 tracks (syntakt::Syntakt), a plain JUCE AudioProcessor.
// Notes 36-51 on any channel trig tracks 1-16 at the track's Note with their velocity, their note-off ends the gate;
// outputs Main (stereo), then Track 01-15 (mono). The firmware is the user's OS 1.41 .syx in the data folder's roms
// (Documents/c0remusic/ST Drums/roms, or GEARMULATOR_DATA_ROOT/c0remusic/ST Drums/roms), read on the message thread;
// the engine made from it reaches the audio thread through an atomic pointer, which hands the one it replaces back.
namespace stDrums
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

		const juce::String getName() const override { return "ST Drums"; }
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

		// The data folder and the folder the firmware is kept in
		static juce::File dataFolder();
		static juce::File romsFolder();

		// The .syx _file read and played from the next block; with _keep, copied into the roms folder. Message thread
		bool loadFirmware(const juce::File& _file, bool _keep);
		bool hasFirmware() const { return m_firmwareLoaded; }
		// What the editor says of the firmware
		const juce::String& status() const { return m_status; }

	private:
		struct TrackParameters
		{
			juce::AudioParameterChoice* machine = nullptr;
			juce::AudioParameterInt* note = nullptr;
			juce::AudioParameterInt* level = nullptr;
			std::array<juce::AudioParameterInt*, parameters::Track.size()> values{};
		};

		// What the audio thread last gave the module, in the parameters' values
		struct Applied
		{
			int machine = 0;
			int level = 0;
			std::array<int, parameters::Track.size()> values{};
		};

		static BusesProperties buses();
		void timerCallback() override;
		void loadFromDataFolder();
		void takeKit(const Engine& _engine);
		void takeEngine();
		void syncParameters(bool _hostIsTruth);
		static void setQuietly(juce::RangedAudioParameter& _parameter, float _value);

		std::array<TrackParameters, Tracks> m_tracks{};
		std::array<juce::AudioParameterInt*, parameters::Fx.size()> m_fx{};
		juce::AudioParameterFloat* m_tempo = nullptr;

		// Message thread
		bool m_hasKit = false;			// the parameters hold a kit: the firmware's defaults or a state's
		std::vector<std::pair<juce::RangedAudioParameter*, float>> m_kit;	// the firmware's defaults
		bool m_firmwareLoaded = false;
		juce::String m_status;

		// The engine handed over: made on the message thread (pending), played (active, the audio thread's), handed
		// back (retired) for the message thread to free
		std::atomic<Engine*> m_pending{nullptr};
		Engine* m_active = nullptr;
		std::atomic<Engine*> m_retired{nullptr};
		std::atomic<uint32_t> m_stateSerial{0};	// counts the states restored
		std::atomic<bool> m_reread{false};			// the audio thread changed parameters: the host reads them again

		// Audio thread
		Player m_player;
		std::array<Applied, Tracks> m_applied{};
		std::array<int, parameters::Fx.size()> m_fxApplied{};
		double m_bpm = 0;
		uint32_t m_stateSeen = 0;
		int m_maxBlock = 0;
	};
}
