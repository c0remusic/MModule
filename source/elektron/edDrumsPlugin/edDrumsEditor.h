#pragma once

#include <array>
#include <functional>
#include <memory>
#include <set>
#include <vector>

#include "edDrumsEditorPages.h"
#include "edDrumsProcessor.h"

// The instrument's editor in JUCE alone, after the Lockstep mockup (.scratch/elektron-drums/concepts: t.html, HANDOFF.md,
// t-ui-spec.md, t-style-guide.md) and spec section 10: a hardware unit drawn from rendered plates (drawImage), a screen
// drawn in paint on a grid of millimetres scaled to the window, seven mode keys (Sound, Seq, Master, FX, Kits, Browse,
// Menu), Page and Follow, and 16 Tracks under the screen, each with its LCD, pad, step key and lock strip. No knob:
// every value is a cell on the screen, set by a horizontal drag, the mouse alone, no right-click, no modifier.
//
// Every value change is one host gesture (beginChangeGesture, setValueNotifyingHost, endChangeGesture). A 30 Hz timer
// reads the parameters and what the Processor reports, and repaints only the areas whose state changed. A sample file
// dropped on a Track's LCD or pad, or on the Sound page's Sample block, loads into that Track's Sampler.
//
// Lock edit: a click on a Track's strip selects its step and opens Sound, where a cell dragged writes a Lock on the
// steps selected (Processor's sequencer, the parameter's index and plain value) instead of the parameter.
namespace elektron::drums
{
	class Editor final : public juce::AudioProcessorEditor, public juce::FileDragAndDropTarget, juce::Timer
	{
	public:
		enum class Page { Sound, Seq, Master, Fx, Kits, Browse };
		static constexpr int Width = 1600, Height = 1000, SmallWidth = 1280, SmallHeight = 800;

		explicit Editor(Processor& _processor);
		~Editor() override;

		void showPage(Page _page);
		Page page() const { return m_page; }
		void showTrack(int _track);
		int track() const { return m_track; }
		void showMenu(bool _open);
		void showLfo(int _lfo);

		// For tests: the page shown's cells, hidden ones included (an LFO's tab, a source's set); a cell's and a step
		// key's area in pixels
		const std::vector<ui::Cell>& cells() const { return m_cells; }
		juce::Rectangle<int> cellArea(const juce::String& _parameterId) const;
		juce::Rectangle<int> stepKeyArea(int _key) const;
		// Kits: a Slot's row, and an action's button (LOAD, SAVE HERE, RENAME, DELETE, IMPORT, EXPORT)
		juce::Rectangle<int> kitSlotArea(int _slot) const;
		juce::Rectangle<int> kitActionArea(int _action) const;
		int kitSelected() const { return m_kitSel; }
		// The timer's work, once
		void tick();

		void paint(juce::Graphics& _g) override;
		void resized() override;
		void mouseDown(const juce::MouseEvent& _e) override;
		void mouseDrag(const juce::MouseEvent& _e) override;
		void mouseUp(const juce::MouseEvent& _e) override;
		void mouseMove(const juce::MouseEvent& _e) override;
		void mouseWheelMove(const juce::MouseEvent& _e, const juce::MouseWheelDetails& _wheel) override;

		bool isInterestedInFileDrag(const juce::StringArray& _files) override;
		void filesDropped(const juce::StringArray& _files, int _x, int _y) override;

	private:
		// A list opened under a cell: eight entries a column; a choice closes it, unless it stays (Chokes)
		struct Selector
		{
			bool open = false;
			juce::StringArray items;
			std::function<bool(int)> current;
			std::function<void(int)> choose;
			std::function<bool(int)> disabled;
			float x = 0, y = 0;
			bool stays = false;
		};

		// The press under way
		struct Press
		{
			int hit = -1;
			juce::Point<float> down, last;	// mm
			bool moved = false;
			float accumulated = 0;			// mm not yet turned into steps
			float value = 0;				// a cell's value as the drag goes, normalised
			bool gesture = false;			// a parameter's gesture begun
		};

		// A block's title line: a rule in its hue, its name
		struct Block
		{
			float x, y, w;
			juce::String title, section;
			const ui::Hue* hue;
		};

		// A gesture on Detail's trig row or a lock line: a click toggles, the first 2 mm fix the direction, horizontal
		// paints, vertical sets the velocity or the Lock's value. _line -1: the trigs, else a parameter's index
		struct GridGesture
		{
			int line = -1;
			int step = -1, last = -1;
			bool was = false, to = false;
			char direction = 0;
			float start = 0;
			juce::Point<float> travel;
		};

		void timerCallback() override { tick(); }
		float scale() const { return static_cast<float>(getWidth()) / ui::UnitWidth; }
		juce::Point<float> toMm(juce::Point<float> _px) const { return _px / scale(); }
		juce::Rectangle<int> toPx(juce::Rectangle<float> _mm) const;

		// The layout of what is shown, rebuilt when what decides it changes (layoutKey)
		juce::String layoutKey() const;
		void layout();
		int addCell(ui::Cell _cell);
		void addHit(ui::Hit _hit) { m_hits.push_back(std::move(_hit)); }
		void watch(juce::Rectangle<float> _r, std::function<uint64_t()> _state);
		void buildHardware();
		void buildSound();
		void buildSeq();
		void buildMaster();
		void buildFx();
		void buildKits();
		void buildBrowse();
		void buildMenu();
		void buildSelector();
		// A block: its title line, and a press anywhere in it gives it the focus
		void block(float _x, float _y, float _w, const juce::String& _section, const juce::String& _title,
			const ui::Hue& _hue);
		juce::Rectangle<float> selectorBox(float& _columnWidth) const;

		// A cell's gestures
		void cellDown(const ui::Cell& _cell);
		void cellDrag(const ui::Cell& _cell, juce::Point<float> _delta);
		void cellUp(const ui::Cell& _cell);
		bool lockable(const ui::Cell& _cell) const;
		bool sectionOff(const juce::String& _section) const;

		// Drawing
		void drawSprites(juce::Graphics& _g);
		void drawHardware(ui::Painter& _p);
		void drawPage(ui::Painter& _p);
		void drawHeader(ui::Painter& _p, const juce::String& _title, const juce::String& _sub);
		void drawCell(ui::Painter& _p, const ui::Cell& _cell);
		void drawBlockTitle(ui::Painter& _p, const Block& _block);
		void drawSoundPictures(ui::Painter& _p);
		void drawSeq(ui::Painter& _p);
		void drawMaster(ui::Painter& _p);
		void drawFx(ui::Painter& _p);
		void drawKits(ui::Painter& _p);
		void drawBrowse(ui::Painter& _p);
		void drawMenu(ui::Painter& _p);
		void drawSelector(ui::Painter& _p);
		void drawButton(ui::Painter& _p, float _x, float _y, float _w, float _h, const juce::String& _text, bool _on,
			bool _enabled = true);
		void drawSegments(ui::Painter& _p, float _x, float _y, float _w, const juce::StringArray& _items, int _on,
			const ui::Hue* _hue = nullptr);
		void sprite(juce::Graphics& _g, const juce::Image& _plate, juce::Rectangle<float> _mm, float _plateY) const;

		// Values
		juce::RangedAudioParameter* trackParameter(const juce::String& _suffix, int _track = -1) const;
		int choiceOf(const juce::String& _id) const;
		float valueOf(const juce::String& _id) const;
		void setNormalised(juce::RangedAudioParameter& _p, float _value);
		void setGestured(juce::RangedAudioParameter& _p, float _normalised);
		void cellSet(const ui::Cell& _cell, float _normalised);
		bool lockOf(const ui::Cell& _cell, float& _normalised) const;
		void removeLocks(const ui::Cell& _cell);
		bool lockEdit() const { return !m_lockSteps.empty() && m_page == Page::Sound; }
		juce::String machineName(int _track) const;

		// The sequencer, the pattern playing
		const Pattern& pattern() const;
		void editPattern(const std::function<void(Pattern&)>& _edit);
		void toggleTrig(int _track, int _step);
		int trackLength(int _track) const;
		std::vector<int> lockLineList() const;	// Detail's lines: the parameters the Track shown locks, then the added
		void toast(const juce::String& _text);

		void openSelector(float _x, float _y, const juce::StringArray& _items, std::function<bool(int)> _current,
			std::function<void(int)> _choose, std::function<bool(int)> _disabled = {}, bool _stays = false);
		void loadSample(int _track, const juce::File& _file);
		void chooseSample(int _track);
		void renameKit(int _slot);

		Processor& m_processor;
		Page m_page = Page::Sound;
		int m_track = 0;
		bool m_menu = false;
		int m_lfo = 0;
		int m_sampleTab = 0;
		juce::String m_focus = "f";
		int m_stepPage = 0;
		bool m_follow = false;
		std::set<int> m_lockSteps;			// the steps of the Track shown whose Locks a cell writes
		int m_seqStep = 0;					// Detail's step
		int m_seqMode = 0;					// VEL, ACCENT, SLIDE
		std::array<std::vector<int>, 16> m_lockLines;	// Detail's lines added without a Lock yet, parameter indices
		int m_lockScroll = 0;
		juce::Point<float> m_mouse{-1, -1};	// mm, for what shows under the mouse alone (a lock line's X)
		int m_browseItem = 0, m_browseScroll = 0;
		int m_kitSel = 0;					// Kits' Slot selected
		int m_kitDelete = -1;				// the Slot whose DELETE asks again
		juce::String m_toast;
		double m_toastUntil = 0;
		std::unique_ptr<Pattern> m_trackClipboard;
		Selector m_selector;
		Press m_press;
		juce::String m_key;
		GridGesture m_grid;
		int m_armedLine = -1;				// Detail's line whose X asks AGAIN
		bool m_stripDragOn = false;			// a drag over the strips adds steps, else takes them off
		int m_stripFrom = -1;
		std::vector<Block> m_blocks;
		std::vector<ui::Cell> m_cells;
		std::vector<ui::Hit> m_hits;
		std::vector<ui::Region> m_regions;
		std::array<float, 16> m_activity{};		// a Track's voice sounding, falling after
		std::array<std::array<juce::Image, 2>, 4> m_plates;	// b0, b1, b2, sa at 1x and 2x
		std::unique_ptr<juce::FileChooser> m_chooser;
	};
}
