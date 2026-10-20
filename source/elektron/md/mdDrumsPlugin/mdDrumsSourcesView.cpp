#include "mdDrumsSourcesView.h"

#include "mdDrumsController.h"
#include "mdDrumsDom.h"
#include "mdDrumsEditor.h"
#include "mdDrumsFrame.h"
#include "mdDrumsProcessor.h"
#include "mdDrumsTrackView.h"

#include "MdsPackage.h"

#include "RmlUi/Core/Element.h"

#include <algorithm>

namespace mdDrums
{
	SourcesView::SourcesView(Editor& _editor) : m_editor(_editor)
	{
		m_editor.addClick("hit_hears", [this](Rml::Event&)
		{
			if(auto* tracks = m_editor.getTrackView())
				tracks->toggleOverlay(TrackView::Overlay::Sources);
		});
		m_editor.addClick("sourcesmenu_done", [this](Rml::Event&)
		{
			if(auto* tracks = m_editor.getTrackView())
				tracks->setOverlay(TrackView::Overlay::None);
		});
		m_editor.addClick("src_auto", [this](Rml::Event&) { chooseTrack(-1); });
		for(int w = 0; w < 4; ++w)
			m_editor.addClick("src_w" + std::to_string(w + 1), [this, w](Rml::Event&) { chooseWord(w); });
		for(int t = 0; t < Controller::TrackCount; ++t)
			m_editor.addClick("src_t" + std::to_string(t + 1), [this, t](Rml::Event&) { chooseTrack(t); });
	}

	SourcesView::~SourcesView() = default;

	Rml::Element* SourcesView::find(const std::string& _id) const
	{
		return m_editor.element(_id);
	}

	int SourcesView::heardCount(const md::mds::Package& _package)
	{
		int count = 1;
		for(const auto& parameter : _package.parameters)
		{
			if(parameter.name == "T-2" || parameter.name == "SIDE")
				count = std::max(count, 2);
			else if(parameter.name == "T-3")
				count = std::max(count, 3);
			else if(parameter.name == "T-4")
				count = std::max(count, 4);
		}
		return count;
	}

	SourcesView::Shown SourcesView::current(const uint8_t _track) const
	{
		Shown shown;
		shown.track = _track;
		const auto& controller = m_editor.controller();
		const auto* machine = controller.getParameter("Machine", _track);
		const auto id = machine ? static_cast<int>(machine->getUnnormalizedValue()) : -1;
		if(id >= Engine::FirstUserMachine && id < Engine::FirstUserMachine + Engine::UserMachineCount)
			if(const auto package = m_editor.processor().userMachine(id - Engine::FirstUserMachine);
				package && (package->flags & md::mds::FlagNeighbour))
				shown.heard = heardCount(*package);
		shown.word = std::min(m_word, std::max(shown.heard - 1, 0));
		shown.sources = controller.sources(_track);
		return shown;
	}

	bool SourcesView::step(const Frame& _frame)
	{
		const auto shown = current(_frame.shown);
		if(shown == m_shown)
			return false;
		refresh(shown);
		return true;
	}

	void SourcesView::chooseWord(const int _word)
	{
		if(m_shown.track < 0 || _word >= m_shown.heard)
			return;
		m_word = _word;
		refresh(current(static_cast<uint8_t>(m_shown.track)));
	}

	void SourcesView::chooseTrack(const int _track)
	{
		if(m_shown.track < 0 || !m_shown.heard || _track == m_shown.track)
			return;
		const auto track = static_cast<uint8_t>(m_shown.track);
		auto& controller = m_editor.controller();
		auto sources = controller.sources(track);
		const auto word = static_cast<size_t>(m_shown.word);
		sources[word] = _track < 0 ? messages::defaultSources(track)[word] : static_cast<uint8_t>(_track);
		controller.setSources(track, sources);
		refresh(current(track));
	}

	void SourcesView::refresh(const Shown& _shown)
	{
		m_shown = _shown;
		const auto track = static_cast<uint8_t>(_shown.track);
		const auto heard = _shown.heard;

		// The Hit screen's title, and the menu closed for a Track that plays no NEIGHBOUR
		if(auto* hears = find("hit_hears"))
		{
			setDisplay(hears, heard > 0, Rml::Style::Display::Inline);
			if(heard > 0)
				setText(find("hit_hears_text"), "hears track " + twoDigits(_shown.sources[0] + 1)
					+ (heard > 1 ? " and " + std::to_string(heard - 1) + " more" : std::string()));
		}
		if(!heard)
			if(auto* tracks = m_editor.getTrackView(); tracks && tracks->getOverlay() == TrackView::Overlay::Sources)
				tracks->setOverlay(TrackView::Overlay::None);

		setText(find("sourcesmenu_label"), "Track " + twoDigits(track + 1));
		for(int w = 0; w < 4; ++w)
			if(auto* tab = find("src_w" + std::to_string(w + 1)))
			{
				setDisplay(tab, w < heard);
				tab->SetClass("on", w == _shown.word);
			}
		const auto chosen = _shown.sources[static_cast<size_t>(_shown.word)];
		if(auto* automatic = find("src_auto"))
			automatic->SetClass("on", chosen == messages::defaultSources(track)[static_cast<size_t>(_shown.word)]);
		for(uint8_t t = 0; t < Controller::TrackCount; ++t)
			if(auto* cell = find("src_t" + std::to_string(t + 1)))
			{
				cell->SetClass("on", t == chosen);
				cell->SetClass("self", t == track);
			}

		// The Tracks it hears, marked in the list
		uint16_t marked = 0;
		for(int w = 0; w < heard; ++w)
			marked = static_cast<uint16_t>(marked | 1u << _shown.sources[static_cast<size_t>(w)]);
		if(marked == m_marked)
			return;
		for(int t = 0; t < Controller::TrackCount; ++t)
			if(((marked ^ m_marked) >> t) & 1)
				if(auto* number = find("row" + std::to_string(t + 1) + "_num"))
					number->SetClass("heard", (marked >> t) & 1);
		m_marked = marked;
	}
}
