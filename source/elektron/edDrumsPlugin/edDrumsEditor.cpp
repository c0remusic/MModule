#include "edDrumsEditor.h"

#include <cmath>

#include "LockstepAssets.h"
#include "edDrumsNames.h"

// The frame: the plates, the hardware under the screen and beside it, the mouse, the timer, a dropped file. The
// screen's pages are in edDrumsEditorScreen.cpp. Positions in mm come from t_assets/t.js and HANDOFF.md
namespace elektron::drums
{
	using namespace ui;

	namespace
	{
		// The plates: b0 the unit at rest, b1 its bright states, b2 its faint ones from y 172 mm, sa the lit steps
		// from y 272 mm
		enum Plate { B0, B1, B2, Sa };
		constexpr float B2Y = 172, SaY = 272;

		juce::Image load(const char* _data, const int _size)
		{
			return juce::ImageFileFormat::loadFrom(_data, static_cast<size_t>(_size));
		}

		float keyX(const int _k) { return 12 + KeyPitch * static_cast<float>(_k); }

		bool isSampleFile(const juce::String& _path)
		{
			return juce::File(_path).hasFileExtension("wav;aif;aiff;flac");
		}
	}

	Editor::Editor(Processor& _processor) : AudioProcessorEditor(_processor), m_processor(_processor)
	{
		m_importsSeen = m_processor.imports();
		// The Track the relays for Push aim at, which a closed editor leaves as it was
		m_track = m_processor.shownTrack();
		for(int t = 0; t < 16; ++t)
			m_keyChoices[static_cast<size_t>(t)] = {trackParameter("machine", t), trackParameter("lfo1_track", t),
				trackParameter("lfo2_track", t), trackParameter("flt_type", t), trackParameter("band_type", t),
				trackParameter("amp_mode", t), trackParameter("lfo1_sync", t), trackParameter("lfo2_sync", t),
				trackParameter("note", t)};
		using namespace LockstepAssets;
		m_plates[B0] = {load(b0_1x_jpg, b0_1x_jpgSize), load(b0_2x_jpg, b0_2x_jpgSize)};
		m_plates[B1] = {load(b1_1x_jpg, b1_1x_jpgSize), load(b1_2x_jpg, b1_2x_jpgSize)};
		m_plates[B2] = {load(b2_1x_jpg, b2_1x_jpgSize), load(b2_2x_jpg, b2_2x_jpgSize)};
		m_plates[Sa] = {load(sa_1x_jpg, sa_1x_jpgSize), load(sa_2x_jpg, sa_2x_jpgSize)};
		setOpaque(true);
		// Browse's SEARCH, in the list's title line: the dark field of the mockup, its text the list's
		m_search.setColour(juce::TextEditor::backgroundColourId, colour::button);
		m_search.setColour(juce::TextEditor::textColourId, colour::ink);
		m_search.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
		m_search.setColour(juce::TextEditor::focusedOutlineColourId, hue::t.c[1]);
		m_search.setColour(juce::CaretComponent::caretColourId, colour::ink);
		m_search.setTextToShowWhenEmpty("SEARCH", colour::dim);
		m_search.setSelectAllWhenFocused(true);
		m_search.onTextChange = [this]
		{
			m_browseSearch = m_search.getText().trim();
			m_browseItem = m_browseScroll = 0;
			layout();
			repaint();
		};
		m_search.onEscapeKey = [this] { m_search.clear(); };
		addChildComponent(m_search);
		// RENAME's field, over the card's title line as SEARCH is over the list's: Return or a click elsewhere keeps the
		// name, Escape gives it up
		for(const auto id : {juce::TextEditor::backgroundColourId, juce::TextEditor::textColourId,
			juce::TextEditor::outlineColourId, juce::TextEditor::focusedOutlineColourId})
			m_rename.setColour(id, m_search.findColour(id));
		m_rename.setColour(juce::CaretComponent::caretColourId, colour::ink);
		m_rename.setInputRestrictions(40);
		m_rename.onReturnKey = [this] { endRename(true); };
		m_rename.onFocusLost = [this] { endRename(true); };
		m_rename.onEscapeKey = [this] { endRename(false); };
		addChildComponent(m_rename);
		setSize(Width, Height);
		layout();
		startTimerHz(30);
	}

	Editor::~Editor()
	{
		stopTimer();
	}

	// ---- what is shown ----

	void Editor::showPage(const Page _page)
	{
		m_page = _page;
		m_menu = false;
		m_selector.open = false;
		m_lockMenu = false;
		if(_page != Page::Sound)
			m_lockSteps.clear();
		layout();
		repaint();
	}

	void Editor::showTrack(const int _track)
	{
		if(_track == m_track)
			return;
		m_track = juce::jlimit(0, 15, _track);
		m_processor.setShownTrack(m_track);
		m_lockSteps.clear();
		m_selector.open = false;
		m_armedLine = -1;
		m_lockScroll = 0;
		m_lockMenu = false;
		layout();
		repaint();
	}

	void Editor::showMenu(const bool _open)
	{
		m_menu = _open;
		m_selector.open = false;
		layout();
		repaint();
	}

	void Editor::showLfo(const int _lfo)
	{
		m_lfo = juce::jlimit(0, 1, _lfo);
		m_focus = _lfo ? "l2" : "l1";
		layout();
		repaint();
	}

	void Editor::showSeqTab(const int _tab)
	{
		m_seqTab = juce::jlimit(0, 2, _tab);
		layout();
		repaint();
	}

	int Editor::machinePages() const
	{
		return std::max(1, (machineParameters(trackMachine(m_track)) + 7) / 8);
	}

	void Editor::showMachinePage(const int _page)
	{
		if(_page < 0 || _page >= machinePages())
			return;
		m_machinePage = _page;
		m_focus = "m";
		layout();
		repaint();
	}

	juce::Rectangle<int> Editor::toPx(const juce::Rectangle<float> _mm) const
	{
		return (_mm * scale()).getSmallestIntegerContainer();
	}

	juce::Rectangle<int> Editor::cellArea(const juce::String& _parameterId) const
	{
		for(const auto& c : m_cells)
			if(c.visible && c.parameter && c.parameter->getParameterID() == _parameterId)
				return toPx(c.area());
		return {};
	}

	juce::Rectangle<int> Editor::stepKeyArea(const int _key) const
	{
		return toPx({keyX(_key), 281.33f, KeyWidth, 28});
	}

	void Editor::resized()
	{
		// SEARCH, in the list's title line (t.js browse(): x + 52, the list's width less 64)
		const juce::Rectangle<float> search{cx(6) + 52, 24.6f, 10 * Col - 4 - 64, 6.4f};
		m_search.setBounds(toPx(search));
		auto font = Painter::font(3.2f, 500);
		font.setHeight(font.getHeight() * scale());
		m_search.applyFontToAllText(font, true);
		m_search.setIndents(juce::roundToInt(2.4f * scale()), juce::roundToInt(0.6f * scale()));
		m_rename.setBounds(toPx({cx(16), 24.6f, 8 * Col - 4, 6.4f}));
		m_rename.applyFontToAllText(font, true);
		m_rename.setIndents(juce::roundToInt(2.4f * scale()), juce::roundToInt(0.6f * scale()));
		repaint();
	}

	// ---- the layout ----

	juce::String Editor::layoutKey() const
	{
		juce::String key;
		key << static_cast<int>(m_page) << "/" << m_track << "/" << (m_menu ? 1 : 0) << "/" << m_lfo << "/"
			<< m_machinePage << "/" << m_focus << "/" << m_stepPage << "/" << (m_follow ? 1 : 0) << "/"
			<< (m_selector.open ? 1 : 0) << "/" << m_seqStep << "/" << m_seqMode << "/" << m_lockScroll << "/"
			<< m_armedLine << "/" << (m_lockMenu ? 1 : 0) << "/" << m_browseItem << "/" << m_browseScroll << "/" << getWidth() << "/" << m_browseTree << "/"
			<< m_browseFilter << "/" << m_browseSearch << "/" << static_cast<int>(m_processor.sampleLibrary().generation())
			<< "/" << static_cast<int>(m_processor.trackSounds().generation()) << "/";
		// The LOCKER: FACTORY, IN THIS SET, the source whose REMOVE asks again, the walks ended
		if(m_page == Page::Browse && browseSamples())
			key << m_processor.factorySamples().size() << "," << m_processor.setSamples().size() << "," << m_removeArmed
				<< "," << static_cast<int>(m_processor.imports()) << "/";
		// Browse's KITS: a row each Slot that holds a Kit
		if(m_page == Page::Browse && m_browseTree == 0)
			for(int i = 0; i < Processor::KitSlots; ++i)
				key << (m_processor.kitSlot(i).empty() ? "0" : "1");
		for(const int s : m_lockSteps)
			key << s << ",";
		const auto& shown = m_keyChoices[static_cast<size_t>(m_track)];
		key << "/" << choiceOf(*shown.machine) << "/" << choiceOf(*shown.fltType) << "/" << choiceOf(*shown.bandType) << "/"
			<< choiceOf(*shown.ampMode) << "/" << choiceOf(*shown.lfo1Sync) << "/" << choiceOf(*shown.lfo2Sync) << "/"
			<< choiceOf("delay_source") << "/" << choiceOf("reverb_source") << "/" << choiceOf("delay_sync")
			<< choiceOf("chorus_sync") << choiceOf("phaser_sync") << "/" << m_processor.sampleName(m_track) << "/";
		// The LFOs' TARGET lists name Machine 1-24 by the machine of the Track each aims at
		key << choiceOf(*shown.lfo1Track) << "," << choiceOf(*shown.lfo2Track) << ",";
		for(const auto& t : m_keyChoices)
			key << choiceOf(*t.machine) << ",";
		key << "/";
		if(m_page == Page::Seq)
		{
			// The lines as the pattern has them now, not m_lines: this is what tells layout() to rebuild them
			for(const int l : lockLineList())
				key << l << ",";
			key << "/" << m_seqTab << "/" << m_overviewPage << "/" << m_patternSel << "/" << m_chainPage << "/"
				<< (m_chainAppend ? 1 : 0) << "/" << m_processor.sequencer().chainLength() << "/"
				<< m_processor.chainSlot() << "/" << pattern().masterLength << "/" << m_processor.currentPattern();
		}
		return key;
	}

	void Editor::layout()
	{
		m_cells.clear();
		m_hits.clear();
		m_regions.clear();
		m_blocks.clear();
		m_menuEntries.clear();
		if(m_page == Page::Seq)
			m_lines = lockLineList();
		buildHardware();
		switch(m_page)
		{
		case Page::Sound: buildSound(); break;
		case Page::Seq: buildSeq(); break;
		case Page::Master: buildMaster(); break;
		case Page::Fx: buildFx(); break;
		case Page::Kits: buildKits(); break;
		case Page::Browse: buildBrowse(); break;
		}
		if(m_menu)
			buildMenu();
		if(m_selector.open)
			buildSelector();
		m_search.setVisible(m_page == Page::Browse && (browseSamples() || m_browseTree == 1) && !m_menu);
		if(m_rename.isVisible() && (m_menu || !(m_page == Page::Kits || (m_page == Page::Browse && m_browseTree == 2))))
			endRename(false);
		for(auto& r : m_regions)
			r.last = r.state();
		m_key = layoutKey();
	}

	int Editor::addCell(Cell _cell)
	{
		const int index = static_cast<int>(m_cells.size());
		m_cells.push_back(std::move(_cell));
		const auto& c = m_cells.back();
		if(!c.visible)
			return index;
		Hit hit;
		hit.r = c.area();
		hit.cell = index;
		hit.keepsLocks = true;
		// A double-click takes off the Lock of the steps edited, else gives the default back
		hit.doubleClick = [this, index]
		{
			const auto& cell = m_cells[static_cast<size_t>(index)];
			if(!cell.parameter || cell.off)
				return;
			if(lockEdit() && lockable(cell))
				removeLocks(cell);
			else
				setNormalised(*cell.parameter, cell.parameter->getDefaultValue());
			repaint(toPx(cell.area().expanded(0.5f)));
		};
		hit.cursor = c.look == Cell::Look::Fader ? juce::MouseCursor::UpDownResizeCursor
			: c.isList() ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::LeftRightResizeCursor;
		addHit(std::move(hit));
		watch(c.area(), [this, index]
		{
			const auto& cell = m_cells[static_cast<size_t>(index)];
			uint64_t h = mixFloat(0, cell.normalised());
			float lock = -1;
			if(lockOf(cell, lock))
				h = mixFloat(h, lock);
			h = mix(h, sectionOff(cell.section) ? 1 : 0);
			return mix(h, lockEdit() ? 1 : 0);
		});
		return index;
	}

	void Editor::watch(const juce::Rectangle<float> _r, std::function<uint64_t()> _state)
	{
		m_regions.push_back({_r, std::move(_state), 0});
	}

	// ---- the timer ----

	void Editor::tick()
	{
		// A Track shown by Push's Track relay
		showTrack(m_processor.shownTrack());
		const uint32_t live = m_processor.liveTracks();
		for(size_t t = 0; t < m_activity.size(); ++t)
			m_activity[t] = (live >> t) & 1 ? 1.0f : m_activity[t] * 0.8f;

		if(m_follow && m_lockSteps.empty())
		{
			const int step = m_processor.playingStep(m_track);
			if(step >= 0)
				m_stepPage = juce::jlimit(0, 3, step / 16);
			// Overview's page follows the Track shown's row
			if(const int row = m_processor.playingRowStep(m_track); row >= 0 && m_page == Page::Seq && m_seqTab == 1)
				m_overviewPage = row / 32;
		}
		m_repainted = 0;
		if(m_press.hit < 0 && layoutKey() != m_key)
		{
			layout();
			repaint();
			m_repainted = 1;
			return;
		}
		for(auto& r : m_regions)
		{
			const auto s = r.state();
			if(s != r.last)
			{
				r.last = s;
				repaint(toPx(r.r.expanded(0.5f)));
				++m_repainted;
			}
		}
		if(m_toastUntil > 0 && juce::Time::getMillisecondCounterHiRes() > m_toastUntil)
		{
			m_toastUntil = 0;
			repaint(toPx({ScreenX, 195, ScreenW, 20}));
			++m_repainted;
		}
		// A walk of the Locker's ended: what it found and what it missed
		if(m_importsSeen != m_processor.imports())
		{
			m_importsSeen = m_processor.imports();
			toast(lockerSummary());
		}
		// A sample an old set no longer plays, once
		if(const auto notices = m_processor.takeNotices(); !notices.isEmpty())
			toast(notices.joinIntoString("; "));
	}

	// ---- the hardware ----

	void Editor::buildHardware()
	{
		// The header's CPU, on every page: repainted when its whole percent changes
		watch({ScreenX + ScreenW - CpuWidth, ScreenY + 2, CpuWidth, 8}, [this] { return static_cast<uint64_t>(cpuPercent()); });
		for(int i = 0; i < 7; ++i)
		{
			Hit hit;
			hit.r = {486.33f, 26.0f + static_cast<float>(i) * 21, 35, 19.5f};
			hit.click = [this, i]
			{
				if(i == 6)
					showMenu(!m_menu);
				else
					showPage(static_cast<Page>(i));
			};
			addHit(std::move(hit));
		}
		{
			Hit page;
			page.r = {486.33f, 202, 35, 19.5f};
			page.click = [this]
			{
				m_stepPage = (m_stepPage + 1) % std::max(1, (trackLength(m_track) + 15) / 16);
				m_follow = false;
			};
			addHit(std::move(page));
			Hit follow;
			follow.r = {486.33f, 223, 35, 19.5f};
			follow.click = [this] { m_follow = !m_follow; };
			addHit(std::move(follow));
		}
		for(int k = 0; k < 16; ++k)
		{
			const float hx = 10 + KeyPitch * static_cast<float>(k);
			Hit pad;
			pad.r = {hx, 249.33f, KeyPitch, 27.5f};
			pad.press = [this, k] { showTrack(k); };
			addHit(std::move(pad));

			Hit step;
			step.r = {hx, 276.83f, KeyPitch, 33.5f};
			step.keepsLocks = true;
			step.click = [this, k] { toggleTrig(m_track, m_stepPage * 16 + k); };
			addHit(std::move(step));

			Hit strip;
			strip.r = {hx, 310.83f, KeyPitch, 11.5f};
			strip.keepsLocks = true;
			strip.press = [this, k]
			{
				const int n = m_stepPage * 16 + k;
				m_stripDragOn = m_lockSteps.count(n) == 0;
				m_stripFrom = k;
				if(m_stripDragOn)
					m_lockSteps.insert(n);
				else
					m_lockSteps.erase(n);
				if(m_page != Page::Sound || m_menu)
				{
					m_page = Page::Sound;
					m_menu = false;
				}
			};
			strip.drag = [this](float, float)
			{
				const int q = juce::jlimit(0, 15, static_cast<int>((m_press.last.x - 10) / KeyPitch));
				for(int j = std::min(q, m_stripFrom); j <= std::max(q, m_stripFrom); ++j)
				{
					const int n = m_stepPage * 16 + j;
					if(m_stripDragOn)
						m_lockSteps.insert(n);
					else
						m_lockSteps.erase(n);
				}
			};
			addHit(std::move(strip));

			const float x = keyX(k);
			watch({x, 252, KeyWidth, 21}, [this, k]
			{
				uint64_t h = mix(0, k == m_track ? 1 : 0);
				h = mix(h, static_cast<uint64_t>(m_activity[static_cast<size_t>(k)] * 10));
				h = mixFloat(h, trackParameter("level", k)->getValue());
				return mix(h, static_cast<uint64_t>(choiceOf(trackParameter("machine", k)->getParameterID())));
			});
			watch({x - 1, 274, KeyWidth + 2, 36}, [this, k]
			{
				const int n = m_stepPage * 16 + k;
				uint64_t h = mix(0, pattern().step(m_track, n).trig ? 1 : 0);
				h = mix(h, m_processor.playingStep(m_track) == n ? 1 : 0);
				return mix(h, n < trackLength(m_track) ? 1 : 0);
			});
			watch({x, 310.8f, KeyWidth, 11}, [this, k]
			{
				const int n = m_stepPage * 16 + k;
				return mix(mix(0, m_lockSteps.count(n)), pattern().step(m_track, n).lockCount);
			});
		}
		watch({485, 194, 38, 50}, [this]
		{
			uint64_t h = mix(static_cast<uint64_t>(m_stepPage), m_follow ? 1 : 0);
			h = mix(h, static_cast<uint64_t>(trackLength(m_track)));
			return mix(h, static_cast<uint64_t>(m_processor.playingStep(m_track) + 1));
		});
		watch({ScreenX, ScreenY, ScreenW, 10}, [this]
		{
			uint64_t h = 0;
			for(int t = 0; t < 16; ++t)
				h = mix(h, m_processor.playingStep(t) >= 0 ? 1 : 0);
			h = mix(h, static_cast<uint64_t>(m_processor.chainSlot() + 1));	// Seq's CHAIN n/m
			return mix(h, static_cast<uint64_t>(m_processor.currentPattern()));
		});
	}

	void Editor::sprite(juce::Graphics& _g, const juce::Image& _plate, const juce::Rectangle<float> _mm,
		const float _plateY) const
	{
		const float q = static_cast<float>(_plate.getWidth()) / UnitWidth;
		const auto dest = toPx(_mm);
		_g.drawImage(_plate, dest.getX(), dest.getY(), dest.getWidth(), dest.getHeight(),
			juce::roundToInt(_mm.getX() * q), juce::roundToInt((_mm.getY() - _plateY) * q),
			juce::roundToInt(_mm.getWidth() * q), juce::roundToInt(_mm.getHeight() * q));
	}

	void Editor::drawSprites(juce::Graphics& _g)
	{
		const float physical = scale() * _g.getInternalContext().getPhysicalPixelScaleFactor();
		const size_t q = physical > 3.01f ? 1 : 0;
		const auto& b0 = m_plates[B0][q];
		_g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
		_g.drawImage(b0, 0, 0, getWidth(), getHeight(), 0, 0, b0.getWidth(), b0.getHeight());
		const auto& b1 = m_plates[B1][q];
		const auto& b2 = m_plates[B2][q];
		const auto& sa = m_plates[Sa][q];

		for(int i = 0; i < 7; ++i)
			if(i == 6 ? m_menu : static_cast<int>(m_page) == i)
				sprite(_g, b1, {485.93f, 25.6f + static_cast<float>(i) * 21, 35.8f, 20.3f}, 0);
		if(m_follow)
			sprite(_g, b1, {485.93f, 222.6f, 35.8f, 20.3f}, 0);
		const int pages = (trackLength(m_track) + 15) / 16;
		for(int pg = 0; pg < 4; ++pg)
		{
			const juce::Rectangle<float> r{503.83f + (static_cast<float>(pg) - 1.5f) * 8.5f - 4.25f, 194.9f, 8.5f, 3.2f};
			if(pg == m_stepPage)
				sprite(_g, b1, r, 0);
			else if(pg < pages)
				sprite(_g, b2, r, B2Y);
		}

		const auto& p = pattern();
		const int playing = m_processor.playingStep(m_track);
		for(int k = 0; k < 16; ++k)
		{
			const float x = keyX(k);
			const int n = m_stepPage * 16 + k;
			const bool bright = k == m_track || m_activity[static_cast<size_t>(k)] > 0.5f;
			sprite(_g, bright ? b1 : b2, {x + 1, 261.83f, 26, 9}, bright ? 0 : B2Y);
			if(p.step(m_track, n).trig)
				sprite(_g, sa, {x - 0.3f, 281.03f, 28.6f, 28.6f}, SaY);
			if(playing == n)
				sprite(_g, b1, {x + KeyWidth / 2 - 4.5f, 274.83f, 9, 3}, 0);
			if(m_lockSteps.count(n))
				sprite(_g, b1, {x + 1, 311.83f, 26, 9}, 0);
			else if(p.step(m_track, n).lockCount)
				sprite(_g, b2, {x + 1, 311.83f, 26, 9}, B2Y);
		}
	}

	void Editor::drawHardware(Painter& _p)
	{
		const int length = trackLength(m_track);
		for(int k = 0; k < 16; ++k)
		{
			const float x = keyX(k);
			const bool selected = k == m_track;
			_p.text(x + 1.3f, 254.93f, juce::String(k + 1) + "  " + machineName(k), 2.6f,
				selected ? colour::ink : colour::mid, selected ? 600 : 500);
			// The LCD's meter: the level set, lit while the voice sounds
			const float level = trackParameter("level", k)->getValue();
			const float lit = m_activity[static_cast<size_t>(k)] * level;
			const float w = (KeyWidth - 2.6f) / 10;
			for(int j = 0; j < 10; ++j)
			{
				const float u = (static_cast<float>(j) + 0.5f) / 10;
				const auto c = u < lit ? (u < 0.68f ? colour::meterGreen : u < 0.9f ? colour::meterAmber
					: colour::meterRed) : u < level ? juce::Colour(0xff39423C) : colour::unlit;
				_p.rect(x + 1.3f + static_cast<float>(j) * w + 0.08f, 256.93f, w - 0.16f, 1.1f, c);
			}
			if(m_stepPage * 16 + k >= length)
				_p.rect(x, 281.33f, KeyWidth, 28, juce::Colour(0x8c000000), 1.7f);
		}
	}

	// ---- the paint ----

	void Editor::paint(juce::Graphics& _g)
	{
		drawSprites(_g);
		_g.addTransform(juce::AffineTransform::scale(scale()));
		Painter p(_g, scale());
		drawHardware(p);
		drawPage(p);
		if(m_menu)
			drawMenu(p);
		if(m_selector.open)
			drawSelector(p);
		// A sample dragged from Browse: its name under the mouse, a frame on the Track it would load on
		// ... or a Track sound
		const auto dragged = [this]() -> juce::String
		{
			const auto at = static_cast<size_t>(m_browseDrag);
			if(m_browseTree == 1)
				return at < m_processor.trackSounds().entries().size() ? m_processor.trackSounds().entries()[at].name : "";
			if(m_browseTree == 3)
			{
				const auto* c = dynamic_cast<const juce::AudioParameterChoice*>(trackParameter("machine"));
				return c && at < static_cast<size_t>(c->choices.size()) ? c->choices[static_cast<int>(at)].toUpperCase()
					: juce::String();
			}
			return at < m_processor.sampleLibrary().entries().size() + m_processor.factorySamples().size()
				+ m_processor.setSamples().size() ? browseEntry(m_browseDrag).name : "";
		};
		if(m_browseDrag >= 0 && m_press.moved && dragged().isNotEmpty())
		{
			const auto& h = m_browseTree == 1 ? hue::a : m_browseTree == 3 ? hue::k : m_browseTree == 4 ? hue::p : hue::g;
			const auto at = m_press.last;
			const int k = static_cast<int>((at.x - 10) / KeyPitch);
			if(at.y >= 244 && k >= 0 && k < 16)
				p.frame(keyX(k) - 0.8f, 248.6f, KeyWidth + 1.6f, 74, h.c[0], 1.8f, 0.6f);
			p.rect(at.x - 17, at.y - 5, 34, 10, juce::Colour(0xff2b2a26), 0.8f);
			p.frame(at.x - 17, at.y - 5, 34, 10, h.c[0], 0.8f, 0.3f);
			p.text(at.x, at.y + 1.4f, dragged().substring(0, 14), 3.2f, colour::ink,
				600, Painter::Align::Centre);
		}
		if(lockEdit())
		{
			p.rect(ScreenX + 1.2f, ScreenY + 0.8f, ScreenW - 2.4f, 0.5f, colour::lock);
			juce::StringArray steps;
			for(const int s : m_lockSteps)
				steps.add(juce::String(s + 1));
			p.text(ScreenX + 200, ScreenY + 7.4f, (m_lockSteps.size() > 1 ? "LOCK STEPS " : "LOCK STEP ")
				+ steps.joinIntoString(", "), 4, colour::lock, 600);
		}
		if(m_toastUntil > 0)
		{
			const float w = p.textWidth(m_toast, 3.2f, 600) + 10;
			p.rect(ScreenX + ScreenW / 2 - w / 2, ScreenY + 192, w, 12, juce::Colour(0xff26282A), 2);
			p.text(ScreenX + ScreenW / 2, ScreenY + 200, m_toast, 3.2f, colour::ink, 600, Painter::Align::Centre);
		}
	}

	// ---- the mouse ----

	void Editor::mouseDown(const juce::MouseEvent& _e)
	{
		if(_e.mods.isPopupMenu())
			return;
		// A click beside RENAME's field keeps its name: the editor takes no focus from it
		endRename(true);
		const auto mm = toMm(_e.position);
		int found = -1;
		for(int i = static_cast<int>(m_hits.size()) - 1; i >= 0; --i)
			if(m_hits[static_cast<size_t>(i)].r.contains(mm))
			{
				found = i;
				break;
			}
		if(!m_lockSteps.empty() && (found < 0 || !m_hits[static_cast<size_t>(found)].keepsLocks))
		{
			m_lockSteps.clear();
			repaint();
		}
		// A press disarms a lane's X; an X's press arms the line again (Press::armed)
		m_press = {};
		m_press.armed = m_armedLine;
		m_armedLine = -1;
		if(found < 0)
			return;
		m_press.hit = found;
		m_press.down = m_press.last = mm;
		const auto& hit = m_hits[static_cast<size_t>(found)];
		if(_e.getNumberOfClicks() >= 2 && hit.doubleClick)
		{
			hit.doubleClick();
			m_press.hit = -1;
			return;
		}
		// A field whose click opens the selector (LENGTH): the double click's second press falls on the selector, and
		// goes to the field beneath it
		if(_e.getNumberOfClicks() >= 2 && m_selector.open)
			for(int i = found - 1; i >= 0; --i)
			{
				const auto& under = m_hits[static_cast<size_t>(i)];
				if(!under.r.contains(mm) || !under.doubleClick)
					continue;
				m_selector.open = false;
				const auto doubleClick = under.doubleClick;
				doubleClick();
				m_press.hit = -1;
				layout();
				repaint();
				return;
			}
		if(hit.cell >= 0)
			cellDown(m_cells[static_cast<size_t>(hit.cell)]);
		if(hit.press)
			hit.press();
	}

	void Editor::mouseDrag(const juce::MouseEvent& _e)
	{
		if(m_press.hit < 0)
			return;
		const auto mm = toMm(_e.position);
		if(!m_press.moved && mm.getDistanceFrom(m_press.down) > 0.8f)
			m_press.moved = true;
		if(m_press.moved)
		{
			const auto hit = m_hits[static_cast<size_t>(m_press.hit)];
			const auto delta = mm - m_press.last;
			m_press.last = mm;
			if(hit.cell >= 0)
				cellDrag(m_cells[static_cast<size_t>(hit.cell)], delta);
			else if(hit.steps && hit.stepMm > 0)
			{
				m_press.accumulated += delta.x;
				const int n = static_cast<int>(m_press.accumulated / hit.stepMm);
				if(n)
				{
					hit.steps(n);
					m_press.accumulated -= static_cast<float>(n) * hit.stepMm;
				}
			}
			else if(hit.drag)
				hit.drag(delta.x, delta.y);
			repaint();
		}
		else
			m_press.last = mm;
	}

	void Editor::mouseUp(const juce::MouseEvent&)
	{
		if(m_press.hit < 0)
			return;
		const auto hit = m_hits[static_cast<size_t>(m_press.hit)];
		if(hit.cell >= 0)
			cellUp(m_cells[static_cast<size_t>(hit.cell)]);
		else if(!m_press.moved && hit.click)
			hit.click();
		else if(m_press.moved && hit.end)
			hit.end();
		m_press.hit = -1;
		if(layoutKey() != m_key)
			layout();
		repaint();
	}

	void Editor::mouseMove(const juce::MouseEvent& _e)
	{
		const auto mm = toMm(_e.position);
		for(auto it = m_hits.rbegin(); it != m_hits.rend(); ++it)
			if(it->r.contains(mm))
			{
				setMouseCursor(it->cursor);
				return;
			}
		setMouseCursor(juce::MouseCursor::NormalCursor);
	}

	void Editor::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& _wheel)
	{
		if(m_page != Page::Browse)
			return;
		const int n = m_browseTree != 3 ? browseCount() : static_cast<int>(m_processor.machines().size());
		const int rows = browseSamples() ? 15 : 16;
		m_browseScroll = juce::jlimit(0, std::max(0, n - rows), m_browseScroll + (_wheel.deltaY < 0 ? 4 : -4));
		layout();
		repaint();
	}

	// ---- a cell's gestures: a horizontal drag, a click, a double-click ----

	void Editor::cellDown(const Cell& _cell)
	{
		float lock = 0;
		m_press.value = lockOf(_cell, lock) ? lock : _cell.normalised();
		if(m_press.value < 0)
			m_press.value = 0;
	}

	void Editor::cellDrag(const Cell& _cell, const juce::Point<float> _delta)
	{
		if(!_cell.parameter || _cell.off || sectionOff(_cell.section))
			return;
		if(_cell.look == Cell::Look::Fader)
		{
			m_press.value = juce::jlimit(0.0f, 1.0f, m_press.value - _delta.y / (_cell.h * 0.95f));
			cellSet(_cell, m_press.value);
			return;
		}
		if(_cell.isList())
		{
			m_press.accumulated += _delta.x;
			const int n = static_cast<int>(m_press.accumulated / 4);
			if(!n)
				return;
			m_press.accumulated -= static_cast<float>(n) * 4;
			const int count = _cell.count();
			const int index = juce::jlimit(0, count - 1, juce::roundToInt(m_press.value * static_cast<float>(count - 1)) + n);
			m_press.value = count > 1 ? static_cast<float>(index) / static_cast<float>(count - 1) : 0;
			cellSet(_cell, m_press.value);
			return;
		}
		// Fine when the mouse goes 6 mm and more away from the line
		const float away = std::abs(m_press.last.y - m_press.down.y);
		const float gain = std::max(0.1f, 1 / (1 + std::max(0.0f, away - 6) / 6));
		const float width = _cell.look == Cell::Look::Small ? 40 : std::max(_cell.w - 4, 20.0f);
		m_press.value = juce::jlimit(0.0f, 1.0f, m_press.value + _delta.x / width * gain);
		cellSet(_cell, m_press.value);
	}

	void Editor::cellUp(const Cell& _cell)
	{
		auto* p = _cell.parameter;
		if(!m_press.moved && p && !_cell.off && !sectionOff(_cell.section))
		{
			const int count = _cell.count();
			const float x = _cell.look == Cell::Look::Title ? _cell.x - _cell.w : _cell.x - 1;
			const float y = _cell.look == Cell::Look::Field ? _cell.y - 4 : _cell.y;
			if(_cell.format == Cell::Format::Chokes)
			{
				openSelector(x, y, [] { juce::StringArray t; for(int i = 1; i <= 16; ++i) t.add("T" + juce::String(i)); return t; }(),
					[p](const int _i) { return (juce::roundToInt(p->convertFrom0to1(p->getValue())) >> _i) & 1; },
					[this, p](const int _i)
					{
						const int bits = juce::roundToInt(p->convertFrom0to1(p->getValue())) ^ (1 << _i);
						setNormalised(*p, p->convertTo0to1(static_cast<float>(bits)));
					},
					[this](const int _i) { return _i == m_track; }, true);
			}
			else if(p == trackParameter("machine"))
			{
				m_browseTree = 3;
				m_browseFilter = 0;
				m_browseItem = _cell.index();
				m_browseScroll = juce::jlimit(0, std::max(0, static_cast<int>(m_processor.machines().size()) - 16),
					m_browseItem - 8);
				showPage(Page::Browse);
			}
			else if(_cell.isList() && count > 2 && _cell.look != Cell::Look::Segments
				&& _cell.look != Cell::Look::Button)
			{
				const Cell cell = _cell;
				juce::StringArray items;
				for(int i = 0; i < count; ++i)
					items.add(cell.valueText(count > 1 ? static_cast<float>(i) / static_cast<float>(count - 1) : 0));
				openSelector(x, y, items, [this, cell](const int _i)
				{
					float v = cell.normalised();
					lockOf(cell, v);
					return juce::roundToInt(v * static_cast<float>(cell.count() - 1)) == _i;
				}, [this, cell](const int _i)
				{
					const float v = static_cast<float>(_i) / static_cast<float>(cell.count() - 1);
					if(lockEdit() && lockable(cell))
						cellSet(cell, v);
					else
						setNormalised(*cell.parameter, v);
				});
			}
			else if(_cell.isList() || _cell.look == Cell::Look::Button || _cell.look == Cell::Look::Segments)
			{
				const int next = (juce::roundToInt(m_press.value * static_cast<float>(count - 1)) + 1) % count;
				cellSet(_cell, count > 1 ? static_cast<float>(next) / static_cast<float>(count - 1) : 0);
			}
		}
		if(m_press.gesture)
		{
			p->endChangeGesture();
			m_press.gesture = false;
		}
	}

	bool Editor::lockable(const Cell& _cell) const
	{
		if(!_cell.parameter || m_page != Page::Sound)
			return false;
		const auto& id = _cell.parameter->getParameterID();
		if(!id.startsWith(juce::String::formatted("t%02d_", m_track + 1)))
			return false;
		return !id.endsWith("_machine") && !id.endsWith("_output") && !id.endsWith("_chokes");
	}

	void Editor::cellSet(const Cell& _cell, const float _normalised)
	{
		auto* p = _cell.parameter;
		if(!p)
			return;
		if(lockEdit() && lockable(_cell))
		{
			const auto index = static_cast<uint16_t>(p->getParameterIndex());
			const float plain = p->convertFrom0to1(_normalised);
			const std::set<int> steps = m_lockSteps;
			editPattern([&](Pattern& _p)
			{
				for(const int s : steps)
					_p.setLock(m_track, s, index, plain);
			});
			repaint(toPx(_cell.area().expanded(0.5f)));
			return;
		}
		if(m_press.hit >= 0 && m_press.hit < static_cast<int>(m_hits.size())
			&& m_hits[static_cast<size_t>(m_press.hit)].cell >= 0)
		{
			if(!m_press.gesture)
			{
				p->beginChangeGesture();
				m_press.gesture = true;
			}
			p->setValueNotifyingHost(_normalised);
		}
		else
			setNormalised(*p, _normalised);
		repaint(toPx(_cell.area().expanded(0.5f)));
	}

	// One host gesture
	void Editor::setNormalised(juce::RangedAudioParameter& _p, const float _value)
	{
		_p.beginChangeGesture();
		_p.setValueNotifyingHost(_value);
		_p.endChangeGesture();
	}

	bool Editor::lockOf(const Cell& _cell, float& _normalised) const
	{
		if(!lockEdit() || !lockable(_cell))
			return false;
		const int first = *m_lockSteps.begin();
		const auto& step = pattern().step(m_track, first);
		const auto* locks = pattern().locks(m_track, first);
		const auto index = static_cast<uint16_t>(_cell.parameter->getParameterIndex());
		for(int i = 0; locks && i < step.lockCount; ++i)
			if(locks[i].parameter == index)
			{
				_normalised = _cell.parameter->convertTo0to1(locks[i].value);
				return true;
			}
		return false;
	}

	void Editor::removeLocks(const Cell& _cell)
	{
		const auto index = static_cast<uint16_t>(_cell.parameter->getParameterIndex());
		const std::set<int> steps = m_lockSteps;
		editPattern([&](Pattern& _p)
		{
			for(const int s : steps)
				_p.removeLock(m_track, s, index);
		});
	}

	// ---- values ----

	juce::RangedAudioParameter* Editor::trackParameter(const juce::String& _suffix, const int _track) const
	{
		const int t = _track < 0 ? m_track : _track;
		auto* p = m_processor.parameter(juce::String::formatted("t%02d_", t + 1) + _suffix);
		jassert(p);
		return p;
	}

	int Editor::choiceOf(const juce::String& _id) const
	{
		const auto* p = m_processor.parameter(_id);
		return p ? choiceOf(*p) : 0;
	}

	const Machine& Editor::trackMachine(const int _track) const
	{
		const auto& machines = m_processor.machines();
		const int choice = choiceOf(*m_keyChoices[static_cast<size_t>(_track)].machine);
		return machines[static_cast<size_t>(juce::jlimit(0, static_cast<int>(machines.size()) - 1, choice))];
	}

	float Editor::valueOf(const juce::String& _id) const
	{
		const auto* p = m_processor.parameter(_id);
		return p ? p->getValue() : 0;
	}

	juce::String Editor::machineName(const int _track) const
	{
		const auto* p = dynamic_cast<const juce::AudioParameterChoice*>(trackParameter("machine", _track));
		return p ? p->getCurrentChoiceName().toUpperCase() : juce::String();
	}

	juce::String Editor::lockName(const juce::RangedAudioParameter& _parameter) const
	{
		const auto& target = m_processor.lockTarget(_parameter.getParameterIndex());
		if(target.kind == Processor::LockKind::Machine)
			if(const char* name = names::control(trackMachine(target.track), target.a))
				return name;
		return _parameter.getName(32).fromFirstOccurrenceOf(" ", false, false).toUpperCase();
	}

	bool Editor::sectionOff(const juce::String& _section) const
	{
		if(_section == "d1" || _section == "d2")
			return choiceOf(trackParameter(_section == "d1" ? "drv1_type" : "drv2_type")->getParameterID()) == 0;
		if(_section == "l1" || _section == "l2")
			return choiceOf(trackParameter(_section == "l1" ? "lfo1_target" : "lfo2_target")->getParameterID()) == 0;
		// The Amp block's envelope, while AMP is off
		if(_section == "ae")
			return trackParameter("amp_on")->getValue() < 0.5f;
		return false;
	}

	// ---- the sequencer: the pattern playing, edited on the message thread's copy ----

	const Pattern& Editor::pattern() const
	{
		return m_processor.sequencer().pattern(m_processor.currentPattern());
	}

	void Editor::editPattern(const std::function<void(Pattern&)>& _edit)
	{
		const int current = m_processor.currentPattern();
		m_processor.editSequencer([&](Sequencer& _s) { _edit(_s.pattern(current)); });
		// Detail draws its lanes as the pattern has them at once, a drag's edits too, before the release lays it out
		if(m_page == Page::Seq)
			m_lines = lockLineList();
		repaint();
	}

	void Editor::toggleTrig(const int _track, const int _step)
	{
		// A trig set has the highest velocity (t-ui-spec, direct trig editing)
		editPattern([&](Pattern& _p) { _p.setTrig(_track, _step, !_p.step(_track, _step).trig, 127); });
	}

	int Editor::trackLength(const int _track) const
	{
		return pattern().trackLength[static_cast<size_t>(_track)];
	}

	void Editor::openSelector(const float _x, const float _y, const juce::StringArray& _items,
		std::function<bool(int)> _current, std::function<void(int)> _choose, std::function<bool(int)> _disabled,
		const bool _stays)
	{
		m_selector = {true, _items, std::move(_current), std::move(_choose), std::move(_disabled), _x, _y, _stays};
		layout();
		repaint();
	}

	// ---- samples ----

	bool Editor::isInterestedInFileDrag(const juce::StringArray& _files)
	{
		for(const auto& f : _files)
			if(isSampleFile(f) || (m_page == Page::Browse && juce::File(f).isDirectory()))
				return true;
		return false;
	}

	void Editor::filesDropped(const juce::StringArray& _files, const int _x, const int _y)
	{
		// A folder on Browse is ADD FOLDER (ticket 20): it joins the Locker linked, a flash image in it gives FACTORY
		if(m_page == Page::Browse)
		{
			bool folders = false;
			for(const auto& f : _files)
				if(juce::File(f).isDirectory())
				{
					startLocker(juce::File(f), false);
					folders = true;
					break;
				}
			if(folders)
				return;
		}
		juce::String file;
		for(const auto& f : _files)
			if(isSampleFile(f))
			{
				file = f;
				break;
			}
		if(file.isEmpty())
			return;
		const auto mm = toMm({static_cast<float>(_x), static_cast<float>(_y)});
		for(int k = 0; k < 16; ++k)
			if(juce::Rectangle<float>(10 + KeyPitch * static_cast<float>(k), 249.33f, KeyPitch, 27.5f).contains(mm))
			{
				loadSample(k, juce::File(file));
				return;
			}
		if(m_page == Page::Sound && juce::Rectangle<float>(cx(0) - 2, 23, 16 * Col + 2, 70).contains(mm))
			loadSample(m_track, juce::File(file));
	}

	void Editor::loadSample(const int _track, const juce::File& _file)
	{
		juce::String error;
		if(!m_processor.loadSample(_track, _file, error))
		{
			toast(error);
			return;
		}
		sampleLoaded(_track);
	}

	// A sample of a source by its file, a factory one or one of the set by its fingerprint
	void Editor::loadSample(const int _track, const SampleLibrary::Entry& _entry)
	{
		if(_entry.folder >= 0)
		{
			loadSample(_track, _entry.file);
			return;
		}
		juce::String error;
		if(!m_processor.loadLibrarySample(_track, _entry.fingerprint, error))
		{
			toast(error);
			return;
		}
		sampleLoaded(_track);
	}

	void Editor::sampleLoaded(const int _track)
	{
		// A sample loaded puts the Track on the Sampler
		auto* machine = trackParameter("machine", _track);
		const float sampler = machine->convertTo0to1(static_cast<float>(m_processor.samplerChoice()));
		if(machine->getValue() != sampler)
			setNormalised(*machine, sampler);
		toast(m_processor.sampleName(_track) + " on Track " + juce::String(_track + 1));
		layout();
		repaint();
	}

	void Editor::chooseSample(const int _track)
	{
		m_chooser = std::make_unique<juce::FileChooser>("Load a sample", juce::File(), "*.wav;*.aif;*.aiff;*.flac");
		juce::Component::SafePointer<Editor> self(this);
		m_chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
			[self, _track](const juce::FileChooser& _c)
			{
				if(self && _c.getResult().existsAsFile())
					self->loadSample(_track, _c.getResult());
			});
	}

	void Editor::audition(const juce::File& _file)
	{
		juce::String error;
		if(!m_processor.auditionSample(_file, error))
			toast(error);
		m_auditionFile = _file;
		m_auditionRef.clear();
		repaint();
	}

	void Editor::audition(const SampleLibrary::Entry& _entry)
	{
		if(_entry.folder >= 0)
		{
			audition(_entry.file);
			return;
		}
		juce::String error;
		const bool played = _entry.folder == SampleLibrary::SetFolder
			? m_processor.auditionSetSample(_entry.fingerprint, error)
			: m_processor.auditionFactorySample(_entry.fingerprint.fromFirstOccurrenceOf(":", false, false).getIntValue(),
				error);
		if(!played)
			toast(error);
		m_auditionFile = juce::File();
		m_auditionRef = _entry.fingerprint;
		repaint();
	}

	void Editor::chooseFolder(const bool _copy)
	{
		m_chooser = std::make_unique<juce::FileChooser>(_copy ? "Import a pack into the Locker: its samples are copied"
			: "Add a folder to the Locker: linked where it lies, nothing copied");
		juce::Component::SafePointer<Editor> self(this);
		m_chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
			[self, _copy](const juce::FileChooser& _c)
			{
				if(self && _c.getResult().isDirectory())
					self->startLocker(_c.getResult(), _copy);
			});
	}

	void Editor::startLocker(const juce::File& _folder, const bool _copy)
	{
		if(!m_processor.addToLocker(_folder, _copy))
		{
			toast(m_processor.importing() ? "The Locker reads a folder: wait for its end"
				: _folder.getFileName() + " cannot be read");
			return;
		}
		m_browseTree = 2;
		m_browseFilter = 0;
		m_browseItem = m_browseScroll = 0;
		toast((_copy ? "Importing " : "Reading ") + _folder.getFileName());
		layout();
		repaint();
	}

	// What a walk found: its samples, those the Locker had, those copied; a factory bank; the files not read
	juce::String Editor::lockerSummary() const
	{
		const auto& r = m_processor.lastImport();
		if(r.error.isNotEmpty())
			return r.error;
		juce::StringArray parts;
		parts.add(r.audio == 0 ? juce::String("no sample") : juce::String(r.audio) + (r.audio == 1 ? " sample" : " samples")
			+ (r.known > 0 ? ", " + juce::String(r.known) + " already in the Locker" : juce::String())
			+ (r.copy != juce::File() ? ", " + juce::String(r.copied) + " copied" : juce::String())
			+ (r.left > 0 ? ", " + juce::String(r.left) + " left out (too large or too many)" : juce::String()));
		if(r.image.isNotEmpty())
			parts.add("factory bank from " + r.image + ", in the Locker at next load");
		else if(r.refused > 0)
			parts.add("no factory bank among " + juce::String(r.refused) + " image-sized files");
		if(r.unread > 0)
			parts.add(juce::String(r.unread) + " files not read");
		return r.folder.getFileName() + ": " + parts.joinIntoString("; ");
	}

	void Editor::startRename(const juce::String& _text, std::function<void(const juce::String&)> _done)
	{
		m_renameDone = std::move(_done);
		m_rename.setText(_text, false);
		m_rename.setVisible(true);
		m_rename.grabKeyboardFocus();
		m_rename.selectAll();
	}

	void Editor::endRename(const bool _keep)
	{
		// Taken first: hiding the field takes its focus, which calls this again
		auto done = std::move(m_renameDone);
		m_renameDone = nullptr;
		if(!done)
			return;
		const auto text = m_rename.getText().trim();
		m_rename.setVisible(false);
		if(_keep && text.isNotEmpty())
			done(text);
		repaint();
	}

	void Editor::toast(const juce::String& _text)
	{
		m_toast = _text;
		m_toastUntil = juce::Time::getMillisecondCounterHiRes() + 2200;
		repaint(toPx({ScreenX, 195, ScreenW, 20}));
	}

	void Editor::saveTrackSound()
	{
		const int track = m_track;
		// Deleted by the modal manager after its callback, which reads the name first
		auto* window = new juce::AlertWindow("Save Track " + juce::String(track + 1) + "'s sound", {},
			juce::MessageBoxIconType::NoIcon, this);
		window->addTextEditor("name", machineName(track));
		window->getTextEditor("name")->setInputRestrictions(24);
		window->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
		window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
		juce::Component::SafePointer<Editor> self(this);
		window->enterModalState(true, juce::ModalCallbackFunction::create([self, window, track](const int _result)
		{
			const auto name = window->getTextEditorContents("name").trim();
			if(!self || _result != 1 || name.isEmpty())
				return;
			const auto file = self->m_processor.saveTrackSound(track, name);
			self->toast(file.existsAsFile() ? "Saved the sound " + name : "The sound could not be written");
			self->layout();
			self->repaint();
		}), true);
	}

	void Editor::loadTrackSound(const int _track, const juce::File& _file)
	{
		if(!m_processor.loadTrackSound(_track, _file))
		{
			toast(_file.getFileNameWithoutExtension() + " could not be read");
			return;
		}
		toast(_file.getFileNameWithoutExtension() + " on Track " + juce::String(_track + 1));
		layout();
		repaint();
	}

	void Editor::auditionSound(const juce::File& _file)
	{
		if(!m_processor.auditionTrackSound(_file))
			toast(_file.getFileNameWithoutExtension() + " could not be read");
		m_auditionFile = _file;
		m_auditionRef.clear();
		repaint();
	}

	void Editor::chooseFile(const juce::String& _title, const juce::String& _pattern, const bool _save,
		std::function<void(const juce::File&)> _then)
	{
		if(chooseFileForTests)
		{
			if(const auto file = chooseFileForTests(_title); file != juce::File())
				_then(file);
			return;
		}
		m_chooser = std::make_unique<juce::FileChooser>(_title, juce::File(), _pattern);
		juce::Component::SafePointer<Editor> self(this);
		const int flags = juce::FileBrowserComponent::canSelectFiles | (_save
			? juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting
			: juce::FileBrowserComponent::openMode);
		m_chooser->launchAsync(flags, [self, then = std::move(_then)](const juce::FileChooser& _c)
		{
			if(self && _c.getResult() != juce::File())
			{
				then(_c.getResult());
				self->repaint();
			}
		});
	}
}
