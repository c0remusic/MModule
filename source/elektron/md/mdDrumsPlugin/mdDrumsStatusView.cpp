#include "mdDrumsStatusView.h"

#include "mdDrumsController.h"
#include "mdDrumsDevice.h"
#include "mdDrumsEditor.h"
#include "mdDrumsFrame.h"
#include "mdDrumsProcessor.h"

#include "X20Builtins.h"
#include "mdProtocol/mdmachines.h"

#include "RmlUi/Core/Element.h"
#include "RmlUi/Core/StringUtilities.h"

namespace mdDrums
{
	namespace
	{
		constexpr const char* g_timeKey = "updateCheckTime";
		constexpr const char* g_latestKey = "updateLatest";
		constexpr const char* g_urlKey = "updateUrl";
		constexpr const char* g_installerKey = "updateInstaller";
		constexpr juce::int64 g_checkSeconds = 24 * 60 * 60;
		constexpr double g_unplayedSeconds = 5.0;
		constexpr double g_flashErrorSeconds = 8.0;

		// A note's name as Live writes it: 60 is C3
		std::string noteName(const int _note)
		{
			return juce::MidiMessage::getMidiNoteName(_note, true, true, 3).toStdString();
		}
	}

	StatusView::StatusView(Editor& _editor) : m_editor(_editor)
	{
		m_editor.addClick("status", [this](Rml::Event&) { click(); });
		if(auto* version = find("version"))
			version->SetInnerRML(MdDrumsVersion);


		// The update check, at most once a day; meanwhile what the last one found
		auto& config = m_editor.processor().getConfig();
		if(checksForUpdates(config))
		{
			offer({config.getValue(g_latestKey).toStdString(), config.getValue(g_urlKey).toStdString(),
				config.getValue(g_installerKey).toStdString()});
			const auto now = juce::Time::currentTimeMillis() / 1000;
			if(now - config.getValue(g_timeKey).getLargeIntValue() >= g_checkSeconds)
			{
				// When it tried, whatever it finds: offline or refused, the next editor waits a day too
				config.setValue(g_timeKey, now);
				m_check->start();
			}
		}
	}

	StatusView::~StatusView() = default;

	bool StatusView::checksForUpdates(const juce::PropertiesFile& _config)
	{
		return _config.getBoolValue(CheckKey, true);
	}

	Rml::Element* StatusView::find(const std::string& _id) const
	{
		return m_editor.element(_id);
	}

	std::string StatusView::text() const
	{
		return m_shown;
	}

	void StatusView::offer(const updates::Release& _release)
	{
		if(updates::compare(_release.version, MdDrumsVersion) > 0)
			m_newer = _release;
	}

	bool StatusView::step(const Frame& _frame)
	{
		const auto& telemetry = m_editor.processor().getTelemetry();
		// A note played while no frame ran is old news
		if(_frame.resumed)
			m_unplayedSeen = telemetry.unplayedCount();
		const auto seconds = _frame.now / 1000.0;
		if(m_flashErrorNew)
		{
			m_flashErrorNew = false;
			m_flashErrorUntil = seconds + g_flashErrorSeconds;
		}
		if(m_saidNew)
		{
			m_saidNew = false;
			m_saidUntil = seconds + g_flashErrorSeconds;
			m_nextMs = 0.0;
		}
		if(!_frame.every(m_nextMs, RefreshHz))
			return false;

		if(const auto release = updates::Check::latest(); release && release->version != m_latestSeen)
		{
			m_latestSeen = release->version;
			auto& config = m_editor.processor().getConfig();
			config.setValue(g_latestKey, juce::String(release->version));
			config.setValue(g_urlKey, juce::String(release->url));
			config.setValue(g_installerKey, juce::String(release->installer));
			offer(*release);
		}

		const auto [text, action] = pressing(seconds);
		if(text == m_shown && action == m_action)
			return false;
		auto* status = find("status");
		if(!status)
			return false;
		m_shown = text;
		m_action = action;
		status->SetInnerRML(Rml::StringUtilities::EncodeRml(text));
		status->SetClass("shown", !text.empty());
		status->SetClass("action", action != Action::None);
		return true;
	}

	std::pair<std::string, StatusView::Action> StatusView::pressing(const double _seconds)
	{
		const auto now = _seconds;
		const bool device = m_editor.processor().hasDevice();
		if(now < m_flashErrorUntil)
			return {device ? m_flashError : m_flashError + ": choose another", Action::ChooseFlash};
		if(!device)
			return {"No Machinedrum flash image: choose the file", Action::ChooseFlash};
		if(now < m_saidUntil)
			return {m_said, Action::None};

		const auto& telemetry = m_editor.processor().getTelemetry();
		if(const auto count = telemetry.unplayedCount(); count != m_unplayedSeen)
		{
			m_unplayedSeen = count;
			m_unplayedUntil = now + g_unplayedSeconds;
		}
		if(now < m_unplayedUntil)
		{
			return {noteName(telemetry.lastUnplayed()) + " plays nothing: Tracks are " + noteName(Device::FirstNote) + " to "
				+ noteName(Device::FirstNote + Engine::TrackCount - 1), Action::None};
		}

		// A User Machine the set holds that this build does not play, kept in the set for one that does (ticket 23 of the
		// User Machines map)
		if(const auto [index, kept] = m_editor.processor().keptUserMachine(); kept)
		{
			char number[8];
			std::snprintf(number, sizeof(number), "USR-%02d", index + 1);
			return {std::string(number) + " " + (kept->name.empty() ? std::string("in this set") : kept->name)
				+ " plays nothing here: " + kept->reason + ". The set keeps it.", Action::None};
		}
		// A Track on one of X.20's built-ins, which plays nothing without X.20's OS (ticket 08 of the map of X.20's built-in
		// Machines); GND-SN-PRO's number plays OS 1.63's GND-SN
		if(!m_editor.processor().playsX20())
		{
			for(uint8_t track = 0; track < Engine::TrackCount; ++track)
			{
				const auto* machine = m_editor.controller().getParameter("Machine", track);
				const auto id = machine ? static_cast<uint8_t>(machine->getUnnormalizedValue()) : uint8_t(0);
				if(!md::engine::x20::isBuiltin(id) || id == md::engine::x20::GndSnPro)
					continue;
				const auto* known = md::machines::find(md::MachineModel::Machinedrum, id);
				char name[4];
				std::snprintf(name, sizeof(name), "T%02d", track + 1);
				return {std::string(name) + " plays " + std::string(known ? known->name : "an X.20 Machine")
					+ ": choose MDX X.20's OS", Action::ChooseX20};
			}
		}
		// With its installer, a click downloads it (the browser saves it; the user runs it with the host closed)
		if(m_newer)
			return {"MoDule " + m_newer->version + " is out"
				+ (m_newer->installer.empty() ? std::string() : ": download the installer"), Action::OpenRelease};
		return {{}, Action::None};
	}

	void StatusView::click()
	{
		switch(m_action)
		{
		case Action::ChooseFlash:
			chooseFlashImage();
			break;
		case Action::ChooseX20:
			chooseX20Os();
			break;
		case Action::OpenRelease:
			if(m_newer)
				juce::URL(m_newer->installer.empty() ? m_newer->url : m_newer->installer).launchInDefaultBrowser();
			break;
		case Action::None:
			break;
		}
	}

	void StatusView::say(std::string _text)
	{
		m_said = std::move(_text);
		m_saidNew = true;
	}

	bool StatusView::importFiles(const std::vector<std::filesystem::path>& _files)
	{
		if(_files.empty())
			return false;
		auto& processor = m_editor.processor();
		if(_files.size() == 1)
		{
			const auto result = processor.importFile(_files.front(), m_editor.controller().getCurrentPart());
			say(result.text);
			// Said at once, with the Track's new machine
			m_editor.frame();
			return result.ok;
		}

		// X.20's User Machines come as a folder of 43: the first and the last that went, and the first refusal's reason
		size_t went = 0;
		std::string first, last, refused;
		for(const auto& file : _files)
		{
			const auto result = processor.importFile(file, -1);
			if(!result.ok)
			{
				if(refused.empty())
					refused = result.text;
				continue;
			}
			last = result.label;
			if(!went++)
				first = last;
		}
		const auto range = went ? ": " + first + (went > 1 ? " to " + last : std::string()) : std::string();
		if(went == _files.size())
			say(std::to_string(went) + " installed" + range);
		else
			say(std::to_string(went) + " of " + std::to_string(_files.size()) + " installed" + range + ". " + refused);
		m_editor.frame();
		return went == _files.size();
	}

	void StatusView::chooseUserMachine()
	{
		// A FileChooser deleted drops its callback: the one this replaces, or this view's when the editor closes
		m_chooser = std::make_unique<juce::FileChooser>("Install User Machines (MDX X.20's .syx or .mds)",
			juce::File::getSpecialLocation(juce::File::userDocumentsDirectory), "*.syx;*.mds");
		m_chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
			| juce::FileBrowserComponent::canSelectMultipleItems,
			[this](const juce::FileChooser& _chooser)
			{
				std::vector<std::filesystem::path> files;
				for(const auto& file : _chooser.getResults())
					if(file != juce::File())
						files.push_back(pathOf(file));
				importFiles(files);
			});
	}

	void StatusView::chooseX20Os()
	{
		// As a drop of the file does: importFiles tells MDX X.20's OS by its size (Processor::importKind)
		m_chooser = std::make_unique<juce::FileChooser>("Choose MDX X.20's OS (Machinedrum_SPS1-UW_OS_X.20_B.syx)",
			juce::File::getSpecialLocation(juce::File::userDocumentsDirectory), "*.syx");
		m_chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
			[this](const juce::FileChooser& _chooser)
			{
				const auto file = _chooser.getResult();
				if(file != juce::File())
					importFiles({pathOf(file)});
			});
	}

	void StatusView::chooseFlashImage()
	{
		// A FileChooser deleted drops its callback: the one this replaces, or this view's when the editor closes
		m_chooser = std::make_unique<juce::FileChooser>("Choose the Machinedrum UW flash image (OS 1.63, 8 MB)",
			juce::File::getSpecialLocation(juce::File::userDocumentsDirectory), "*");
		m_chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
			[this](const juce::FileChooser& _chooser)
			{
				const auto file = _chooser.getResult();
				if(file == juce::File())
					return;
				m_flashError = m_editor.processor().loadFlashImage(pathOf(file));
				m_flashErrorNew = !m_flashError.empty();
				m_flashErrorUntil = 0.0;
				m_nextMs = 0.0;
				// Said at once; a new Device's tables reach the Filter and EQ screen in the same frame
				m_editor.frame();
			});
	}
}
