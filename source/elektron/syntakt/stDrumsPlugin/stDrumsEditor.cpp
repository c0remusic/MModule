#include "stDrumsEditor.h"

#include "stDrumsProcessor.h"

namespace stDrums
{
	namespace
	{
		constexpr int Header = 40;
	}

	Editor::Editor(Processor& _processor)
		: AudioProcessorEditor(_processor), m_processor(_processor), m_parameters(_processor)
	{
		addAndMakeVisible(m_status);
		addChildComponent(m_choose);
		addAndMakeVisible(m_parameters);
		m_choose.onClick = [this] { choose(); };
		update();
		setResizable(true, false);
		setSize(560, Header + 600);
		startTimerHz(4);
	}

	Editor::~Editor()
	{
		stopTimer();
	}

	void Editor::paint(juce::Graphics& _g)
	{
		_g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
	}

	void Editor::resized()
	{
		auto area = getLocalBounds();
		auto header = area.removeFromTop(Header).reduced(8, 6);
		if(m_choose.isVisible())
			m_choose.setBounds(header.removeFromRight(220));
		m_status.setBounds(header);
		m_parameters.setBounds(area);
	}

	void Editor::timerCallback()
	{
		update();
	}

	void Editor::update()
	{
		if(m_status.getText() != m_processor.status())
			m_status.setText(m_processor.status(), juce::dontSendNotification);
		const bool missing = !m_processor.hasFirmware();
		if(m_choose.isVisible() != missing)
		{
			m_choose.setVisible(missing);
			resized();
		}
	}

	void Editor::choose()
	{
		m_chooser = std::make_unique<juce::FileChooser>("Choose the Syntakt OS 1.41 update file",
			juce::File::getSpecialLocation(juce::File::userHomeDirectory), "*.syx");
		m_chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
			[this](const juce::FileChooser& _chooser)
			{
				const auto file = _chooser.getResult();
				if(file.existsAsFile())
					m_processor.loadFirmware(file, true);
				update();
			});
	}
}
