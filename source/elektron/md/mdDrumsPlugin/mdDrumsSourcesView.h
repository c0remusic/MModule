#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace md::mds
{
	struct Package;
}

namespace Rml
{
	class Element;
}

namespace mdDrums
{
	class Editor;
	struct Frame;

	// What a NEIGHBOUR User Machine on the shown Track hears (tickets 30 and 31 of the User Machines map, a prototype):
	// the Hit screen's title says "hears track 11" when the Track plays one, and opens the "hears" menu over the Hit
	// screen (TrackView::Overlay::Sources): a tab for each Track the package reads (T-1, and T-2 to T-4 for one that names
	// them or a SIDE), then the Track it takes, or Auto for the one that many before it. The Tracks it hears are marked
	// in the Tracks list. The Controller holds the choice (Controller::setSources); the editor's frame follows a Kit
	// loaded, a state brought back or another Track shown.
	class SourcesView
	{
	public:
		explicit SourcesView(Editor& _editor);
		~SourcesView();

		SourcesView(const SourcesView&) = delete;
		SourcesView& operator=(const SourcesView&) = delete;

		// The editor's frame: true when it changed the document
		bool step(const Frame& _frame);

		// How many Tracks a NEIGHBOUR package reads, one to four: T-2, T-3 or T-4 among its parameters' names, or a
		// SIDE (a sidechain from two back), say it reads more than the Track before (research/12 §1.4)
		static int heardCount(const md::mds::Package& _package);

		void chooseWord(int _word);
		void chooseTrack(int _track);	// -1: Auto

	private:
		struct Shown
		{
			int track = -1;
			int heard = 0;		// 0 when the Track plays no NEIGHBOUR
			int word = 0;
			std::array<uint8_t, 4> sources{};
			bool operator==(const Shown& _other) const
			{
				return track == _other.track && heard == _other.heard && word == _other.word && sources == _other.sources;
			}
		};
		Shown current(uint8_t _track) const;
		void refresh(const Shown& _shown);
		Rml::Element* find(const std::string& _id) const;

		Editor& m_editor;
		Shown m_shown;
		int m_word = 0;		// the tab chosen
		uint16_t m_marked = 0;	// the Tracks marked in the list
	};
}
