#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace md::mds
{
	struct Package;
}

namespace pluginLib
{
	class Parameter;
}

namespace Rml
{
	class Element;
	class Event;
}

namespace mdDrums
{
	class Editor;
	struct Frame;

	// The v22 skin's behaviour until the slices that give it more (tickets 17 and 18): the shown Track, chosen by a click
	// on a list row, a Mix strip, a send or by ‹ ›, and what the views say about it (its row and strip lit, "Track 01",
	// the machine's family, where it plays, the machine's names for SYN1-8 and in the LFO's menu); the overlays opened
	// and closed (the machine browser, the LFO's target menu, the Kits, the Links and Chokes menu; Esc closes them, the
	// browser as Cancel), and the browser's "Listen while choosing", with which a machine chosen plays the Track; playing
	// the Track (the play key, at the head band's velocity, which a drag sets) and stepping its machine (the
	// wheel or the arrows on its name, the arrows in the browser: ticket 23); and the top bar's Menu, which opens the
	// editor's menu (EditorState::openMenu). The editor's frame draws the Track shown, its machine, every Track's Out and
	// the open LFO menu where they changed (step); its clicks and keys write at once.
	class TrackView
	{
	public:
		enum class Overlay
		{
			None,
			Browser,
			LfoMenu,
			Kits,
			Links,		// the Track's Links and Chokes, over the Hit screen (LinksView)
			Sources		// what its NEIGHBOUR User Machine hears, over the Hit screen (SourcesView)
		};

		static constexpr int RowHeight = 40;
		static constexpr int ListTop = 176;
		static constexpr uint8_t ListenVelocity = 100;

		explicit TrackView(Editor& _editor);
		~TrackView();

		TrackView(const TrackView&) = delete;
		TrackView& operator=(const TrackView&) = delete;

		// The editor's frame: true when it changed the document
		bool step(const Frame& _frame);

		void showTrack(uint8_t _track) const;
		void stepTrack(int _delta) const;

		Overlay getOverlay() const { return m_overlay; }
		void setOverlay(Overlay _overlay);
		// Opens _overlay, or closes it when it is the one open
		void toggleOverlay(const Overlay _overlay) { setOverlay(m_overlay == _overlay ? Overlay::None : _overlay); }
		// Esc: closes the open overlay, the browser as Cancel does; false when none is open
		bool escape();

		// The browser's "Listen while choosing", kept in the plug-in's settings: on, a machine chosen plays the Track
		bool isListening() const;
		void setListening(bool _on) const;

		// The head band's velocity, 1-127, kept in the plug-in's settings: what the play key plays with
		static constexpr float PixelsPerVelocityStep = 1.5f;
		int getPlayVelocity() const;
		void setPlayVelocity(int _velocity) const;
		void play() const;

		// The shown Track's machine, the next or previous in the browser's order (wrapping), or the first of the next or
		// previous family; listening, the Track plays
		void stepMachine(int _delta);
		void stepFamily(int _delta);

		// A key, before the stack's handling: with the browser open the arrows step its machines and families and Enter
		// keeps it; on the machine's name ↑ ↓ step it. False: not handled, as Space always is, so that the host
		// takes it (Live starts or stops its transport)
		bool key(Rml::Event& _event);

	private:
		using PerTrack = std::array<pluginLib::Parameter*, 16>;

		void choose(int _machine);
		// The Track shown: its row, strip and send lit, its number in the head band and the browser
		void showPart(uint8_t _part);
		// The head band's family and the bands' SYN1-8 names, the shown Track's Out, every strip's Out, the LFO menu
		// while it is open: each drawn again where its parameters changed. True when something was
		bool followMachine();
		bool followOuts();
		bool followLfoMenu();
		bool followUserCells(const Frame& _frame);
		void updateMachine() const;
		void updateOutput() const;
		// A Mix strip's Out: a click moves the Track between Main and its natural Out (its own number, 16 to 15), the
		// wheel through Main and Out 01-15 (ticket 33)
		void updateStripOut(uint8_t _track) const;
		void setStripOut(uint8_t _track, int _out);
		static uint8_t naturalOut(uint8_t _track);
		void updateLfoMenu() const;
		Rml::Element* find(const std::string& _id) const;
		static int value(const pluginLib::Parameter* _parameter);

		Editor& m_editor;
		uint8_t m_part = 0;
		Overlay m_overlay = Overlay::None;
		int m_machineBeforeBrowser = -1;
		std::vector<int> m_machines;	// the browser's, in its order
		std::vector<int> offeredMachines() const;	// m_machines, X.20's built-ins only while the Device plays them
		// Per Track, what shows it is the one shown: the list row's number and name ("tsel"), the strip's and the
		// send's numbers ("on")
		std::array<std::array<Rml::Element*, 4>, 16> m_shownMarks{};
		float m_dragY = 0.0f;
		int m_dragFrom = 0;

		PerTrack m_machineParameters{};
		PerTrack m_outParameters{};
		PerTrack m_lfoTrackParameters{};
		// What the document shows, -1 for not yet: the shown Track's machine and Out, every strip's Out, the LFO menu's
		// Track, target Track and that Track's machine
		int m_machineShown = -1;
		// The User Machine's package drawn, held so that a new one can never take its address
		std::shared_ptr<const md::mds::Package> m_packageShown;
		uint32_t m_userCellsShown = ~0u;		// followUserCells's, all lit until the first frame dims what is empty
		double m_userCellsNextMs = 0.0;
		bool m_x20Shown = false;	// X.20's built-in Machines in the browser; hidden in the skin until the Device plays them
		int m_outShown = -1;
		std::array<int, 16> m_outs{};
		std::array<int, 3> m_menuKey{};
	};
}
