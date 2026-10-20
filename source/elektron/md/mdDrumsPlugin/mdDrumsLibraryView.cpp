#include "mdDrumsLibraryView.h"

#include "mdDrumsController.h"
#include "mdDrumsDom.h"
#include "mdDrumsEditor.h"
#include "mdDrumsFrame.h"
#include "mdDrumsLibrary.h"
#include "mdDrumsProcessor.h"
#include "mdDrumsStatusView.h"
#include "mdDrumsTrackView.h"

#include "MdsPackage.h"

#include "juceRmlUi/juceRmlComponent.h"
#include "juceRmlUi/rmlEventListener.h"

#include "RmlUi/Core/ElementDocument.h"
#include "RmlUi/Core/StringUtilities.h"

namespace mdDrums
{
	namespace
	{
		// The browser's grid: c4's left, a column's step, the first row's top, a row's step (mdDrums.rcss)
		constexpr int g_left = 256;
		constexpr int g_column = 80;
		constexpr int g_top = 288;
		constexpr int g_row = 32;

		// What kind of Machines a family of X.20's holds, as the built-in families say theirs ("analog", "FM"), from their
		// names (TX-8 BASS DRUM, BBD CHORUS, E PIANO...); none for a family MoDule does not know
		const char* kindOf(const std::string& _category)
		{
			if(_category == "TX8" || _category == "TX9")
				return "drums";
			if(_category == "TXZ")
				return "synth sounds";
			if(_category == "NFX")
				return "effects";
			if(_category == "P-I")
				return "physical";
			if(_category == "SYN")
				return "synth parts";
			return "";
		}

		bool samePackages(const std::vector<std::shared_ptr<const md::mds::Package>>& _a,
			const std::vector<std::shared_ptr<const md::mds::Package>>& _b)
		{
			return std::equal(_a.begin(), _a.end(), _b.begin(), _b.end(),
				[](const auto& _x, const auto& _y) { return _x->bytes == _y->bytes; });
		}
	}

	LibraryView::LibraryView(Editor& _editor) : m_editor(_editor)
	{
		m_browser = m_editor.element("browser");
		m_library = m_editor.element("library");
		m_open = m_editor.element("library_open");
		m_what = m_editor.element("browser_what");
		m_editor.addClick("library_open", [this](Rml::Event&) { show(true); });
		m_editor.addClick("browser_builtin", [this](Rml::Event&) { show(false); });
	}

	LibraryView::~LibraryView() = default;

	void LibraryView::show(const bool _shown)
	{
		m_shown = _shown;
		if(m_browser)
			m_browser->SetClass("library", _shown);
		setText(m_what, _shown ? "Choose a user machine" : "Choose a machine");
		if(_shown)
			followChosen(m_editor.controller().getCurrentPart());
	}

	bool LibraryView::escape()
	{
		if(!m_shown)
			return false;
		show(false);
		return true;
	}

	bool LibraryView::step(const Frame& _frame)
	{
		const auto* track = m_editor.getTrackView();
		if(!track || track->getOverlay() != TrackView::Overlay::Browser || !m_library)
		{
			// Closed, the browser opens on its own families next time
			if(!m_shown)
				return false;
			show(false);
			return true;
		}
		bool wrote = false;
		// The Library four times a second: a drop, the Menu or another instance installs
		if(_frame.every(m_nextMs, 4))
		{
			const auto* library = m_editor.processor().getLibrary();
			static const std::vector<std::shared_ptr<const md::mds::Package>> none;
			const auto& machines = library ? library->machines() : none;
			if(!samePackages(machines, m_machines))
			{
				m_machines = machines;
				wrote |= build();
			}
			if(m_countShown != m_machines.size())
			{
				m_countShown = m_machines.size();
				wrote |= setText(m_open, m_machines.empty() ? std::string("none installed yet")
					: "all " + std::to_string(m_machines.size()) + " installed");
			}
		}
		if(m_shown)
			wrote |= followChosen(_frame.shown);
		return wrote;
	}

	bool LibraryView::build()
	{
		while(m_library->HasChildNodes())
			m_library->RemoveChild(m_library->GetFirstChild());
		m_cells.clear();
		m_chosen = -1;
		auto* document = m_library->GetOwnerDocument();
		if(!document)
			return true;

		// Family by family, as the Library sorts them (by category, then code); a family takes as many columns as its
		// cells need, shared evenly as the built-in TRX's 13 are (7 and 6), and starts a column of its own
		int column = 0;
		for(size_t first = 0; first < m_machines.size();)
		{
			const auto& category = m_machines[first]->category;
			auto end = first;
			while(end < m_machines.size() && m_machines[end]->category == category)
				++end;
			const auto count = static_cast<int>(end - first);
			const auto columns = (count + Rows - 1) / Rows;
			const auto rows = (count + columns - 1) / columns;
			if(column + columns > Columns)
				break;

			auto head = document->CreateElement("div");
			head->SetClassNames("a grp fam");
			head->SetProperty("left", dp(g_left + g_column * column));
			head->SetProperty("width", dp(64 + g_column * (columns - 1)));
			head->SetInnerRML("<span>" + Rml::StringUtilities::EncodeRml(category) + "<span class=\"count\">"
				+ std::to_string(count) + "</span></span>" + (columns > 1 ? "<div class=\"line\"/>" : ""));
			m_library->AppendChild(std::move(head));
			if(const auto* kind = kindOf(category); *kind)
			{
				auto line = document->CreateElement("div");
				line->SetClassNames("a kind");
				line->SetProperty("left", dp(g_left + g_column * column));
				line->SetProperty("width", dp(64 + g_column * (columns - 1)));
				line->SetInnerRML(kind);
				m_library->AppendChild(std::move(line));
			}

			for(int i = 0; i < count; ++i)
			{
				const auto index = first + static_cast<size_t>(i);
				auto cell = document->CreateElement("div");
				cell->SetClassNames("a mach");
				cell->SetProperty("left", dp(g_left + g_column * (column + i / rows)));
				cell->SetProperty("top", dp(g_top + g_row * (i % rows)));
				cell->SetInnerRML(Rml::StringUtilities::EncodeRml(m_machines[index]->code));
				juceRmlUi::EventListener::AddClick(cell.get(), [this, index] { choose(index); });
				m_cells.push_back(m_library->AppendChild(std::move(cell)));
			}
			column += columns;
			first = end;
		}
		return true;
	}

	bool LibraryView::followChosen(const uint8_t _track)
	{
		// The cell of the User Machine the Track plays, where this instance holds one at its number
		int chosen = -1;
		const auto* machine = m_editor.controller().getParameter("Machine", _track);
		const auto id = machine ? static_cast<int>(machine->getUnnormalizedValue()) : -1;
		if(id >= Engine::FirstUserMachine && id < Engine::FirstUserMachine + Engine::UserMachineCount)
			if(const auto held = m_editor.processor().userMachine(id - Engine::FirstUserMachine))
				for(size_t i = 0; i < m_cells.size() && chosen < 0; ++i)
					if(m_machines[i]->bytes == held->bytes)
						chosen = static_cast<int>(i);
		if(chosen == m_chosen)
			return false;
		if(m_chosen >= 0 && m_chosen < static_cast<int>(m_cells.size()))
			m_cells[static_cast<size_t>(m_chosen)]->SetClass("chosen", false);
		if(chosen >= 0)
			m_cells[static_cast<size_t>(chosen)]->SetClass("chosen", true);
		m_chosen = chosen;
		return true;
	}

	void LibraryView::choose(const size_t _index)
	{
		if(_index >= m_machines.size())
			return;
		const auto part = m_editor.controller().getCurrentPart();
		const auto [id, error] = m_editor.processor().chooseUserMachine(part, m_machines[_index]);
		if(id < 0)
		{
			if(auto* status = m_editor.getStatusView())
				status->say(m_machines[_index]->displayName() + ": " + error);
			return;
		}
		followChosen(part);
		if(const auto* track = m_editor.getTrackView(); track && track->isListening())
			m_editor.controller().audition(part, TrackView::ListenVelocity);
	}
}
