#pragma once

#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>

namespace stDrums
{
	class Processor;

	// The firmware's status and, while none plays, a button that chooses the .syx (copied into the roms folder);
	// below, JUCE's generic editor of every parameter
	class Editor final : public juce::AudioProcessorEditor, juce::Timer
	{
	public:
		explicit Editor(Processor& _processor);
		~Editor() override;

		void resized() override;
		void paint(juce::Graphics& _g) override;

	private:
		void timerCallback() override;
		void update();
		void choose();

		Processor& m_processor;
		juce::Label m_status;
		juce::TextButton m_choose{"Choose Syntakt OS 1.41 .syx..."};
		juce::GenericAudioProcessorEditor m_parameters;
		std::unique_ptr<juce::FileChooser> m_chooser;
	};
}
