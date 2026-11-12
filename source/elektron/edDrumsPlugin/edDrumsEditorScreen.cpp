#include "edDrumsEditor.h"

#include <cmath>

#include "edDrumsNames.h"
#include "sophieMachine.h"

// The screen's pages: Sound, Seq (Detail), Master, FX, Kits, Browse, and the Menu panel and the selector over them.
// Positions in mm from t_assets/t.js (sound(), detail(), mix(), fx(), kits(), browse(), menuPanel(), outMenu())
namespace elektron::drums
{
	using namespace ui;
	using Look = Cell::Look;
	using Format = Cell::Format;
	using Align = Painter::Align;

	namespace
	{
		// Sound, after u.html's variant 2 (u-html-synthese-v19.md): three groups, SOURCE, TIMBRE, AMP & MOD, on 9, 8 and 7
		// of the 24 columns, their names above; the blocks from y 30, 5 mm apart down a column, the bottom row's at 185.
		// TIMBRE in halves of 4 columns: the drives, EQ and SENDS
		constexpr float SoundY = 30, LowY = 185, FltY = SoundY + 27 + 5, LfoY = SoundY + 92 + 5;
		constexpr float SrcX = cx(0) - 2, SrcW = 9 * Col + 2, TimX = cx(9) - 2, TimW = 8 * Col + 2, ModX = cx(17) - 2,
			ModW = 7 * Col + 2, HalfW = 4 * Col + 2, SndX = TimX + 4 * Col;
		// The pictures: VOICE's, the filter's response and envelope, the amp's envelope, the LFO's wave and its two cards
		const juce::Rectangle<float> voicePicture{SrcX + 2, SoundY + 9, SrcW - 6, 84};
		const juce::Rectangle<float> responsePicture{TimX + 66, FltY + 9, TimW - 70, 40};
		const juce::Rectangle<float> envelopePicture{TimX + 35, FltY + 63, TimW - 39, 36};
		const juce::Rectangle<float> ampPicture{ModX + 2, SoundY + 43, ModW - 6, 30};
		const juce::Rectangle<float> lfoPicture{ModX + 2, LfoY + 47, ModW - 6, 33};
		juce::Rectangle<float> lfoCard(const int _i)
		{
			const float w = (ModW - 8) / 2;
			return {ModX + 2 + static_cast<float>(_i) * (w + 2), LfoY + 83, w, 28};
		}
		// EQ's curve: under its row of cells, lower and shorter under 3 BAND's two rows
		juce::Rectangle<float> eqPicture(const int _type)
		{
			return _type == 3 ? juce::Rectangle<float>{TimX + 2, LowY + 38.5f, HalfW - 6, 13}
				: juce::Rectangle<float>{TimX + 2, LowY + 27, HalfW - 6, 23};
		}
		// SENDS: a bar each send, its name above, its value's cell under it
		juce::Rectangle<float> sendBar(const int _i)
		{
			const float gap = 3, w = (HalfW - 6 - 3 * gap) / 4;
			return {SndX + 2 + static_cast<float>(_i) * (w + gap), LowY + 14.5f, w, 31};
		}

		constexpr float SG = cx(2), SC = (22 * Col - 4 + 1.2f) / 16;	// Seq's 16 columns
		const juce::Colour beatA{0xff191B1D}, beatB{0xff111315};
		juce::Colour beat(const int _i) { return (_i / 4) % 2 == 0 ? beatA : beatB; }

		// Detail's lock lanes, five a page, and what the + LOCK menu lays out: ten columns of rows of 8.6 mm over the page
		constexpr float LaneY = 86, LanePitch = 25, LaneH = 23.4f, MenuY = 86, MenuRow = 8.6f;
		constexpr int Lanes = 5, MenuRows = 13;
		juce::Rectangle<float> laneBox(const int _row)
		{
			return {SG, LaneY + static_cast<float>(_row) * LanePitch, 16 * SC, LaneH};
		}
		juce::Rectangle<float> laneDeleteBox(const int _row)
		{
			return {SG - 8.5f, LaneY + static_cast<float>(_row) * LanePitch + 0.5f, 7, 7.6f};
		}
		const juce::Rectangle<float> addLockBox{SG + 15 * SC, 78, SC - 1, 7}, lanesBox{SG + 13 * SC, 78, 2 * SC - 1, 7};

		// A Track parameter's block, as Sound names it: its hue, its name, its column in the + LOCK menu (VOICE takes two)
		struct LaneSection
		{
			const Hue* hue;
			const char* name;
			int column;
		};
		LaneSection laneSection(const juce::String& _id)
		{
			// The last that matches: "_amp_mode" holds "_m" too
			static const std::pair<const char*, LaneSection> sections[]{{"_m", {&hue::k, "VOICE", 0}},
				{"_note", {&hue::k, "VOICE", 0}}, {"_drv1", {&hue::a, "DRIVE 1", 2}}, {"_drv2", {&hue::a, "DRIVE 2", 3}},
				{"_flt", {&hue::s, "FILTER", 4}}, {"_fenv", {&hue::s, "FILTER", 4}}, {"_band", {&hue::b, "EQ", 5}},
				{"_amp", {&hue::p, "AMP", 6}}, {"_vel", {&hue::p, "AMP", 6}}, {"_level", {&hue::p, "AMP", 6}},
				{"_pan", {&hue::p, "AMP", 6}}, {"_accent", {&hue::p, "AMP", 6}}, {"_lfo1", {&hue::y, "LFO 1", 7}},
				{"_lfo2", {&hue::m, "LFO 2", 8}}, {"_send", {&hue::w, "SENDS", 9}}};
			LaneSection found{&hue::t, "", 0};
			for(const auto& s : sections)
				if(_id.contains(s.first))
					found = s.second;
			return found;
		}

		// _text cut to _width mm
		juce::String fitted(const Painter& _p, juce::String _text, const float _cap, const float _width,
			const int _weight = 500)
		{
			while(_text.isNotEmpty() && _p.textWidth(_text, _cap, _weight) > _width)
				_text = _text.dropLastCharacters(1);
			return _text;
		}

		// A level line dashed in steps of 1.2 mm, its dashes a list of rectangles: a dashed stroke or a path of them costs
		// some 0.6 ms a line, the list 0.02
		void dashed(const Painter& _p, const float _x0, const float _x1, const float _y, const juce::Colour _colour,
			const float _width)
		{
			juce::RectangleList<float> d;
			for(float x = _x0; x < _x1; x += 2.4f)
				d.addWithoutMerging({x, _y - _width / 2, std::min(1.2f, _x1 - x), _width});
			_p.g.setColour(_colour);
			_p.g.fillRectList(d);
		}

		// VOICE's page selector: three segments of 7 mm in its head, left of the machine's name
		juce::Rectangle<float> machinePageBox(const int _page)
		{
			return {SrcX + SrcW - 61 + 7 * static_cast<float>(_page), SoundY + 1.8f, 7, 6};
		}

		// The Sampler's GRAIN in VOICE's head: two segments from 46 mm in, its name at 34 (t.js); 22 mm, not the mockup's
		// 14, which hold MD and DT2 but not 12-BIT and VINTAGE, and still end before the page selector
		juce::Rectangle<float> grainBox(const int _i)
		{
			return {SrcX + 46 + 22 * static_cast<float>(_i), SoundY + 1.8f, 22, 6};
		}

		// Machine 1-8 of _page 0, 9-16 of 1, 17-24 of 2, as _machine names them (edDrumsNames.h): empty for one it does
		// not use, and past Machine 8 for a machine of 8 (a Syntakt machine's Machine 9, HOLD, is on no page)
		std::array<juce::String, 8> machineLabels(const Machine& _machine, const int _page)
		{
			std::array<juce::String, 8> labels;
			for(size_t i = 0; i < 8; ++i)
			{
				const int index = _page * 8 + static_cast<int>(i);
				if(index >= machineParameters(_machine))
					break;
				if(const char* name = names::control(_machine, index))
					labels[i] = name;
			}
			return labels;
		}

		// What an LFO's TARGET lists: Machine 1-24 as _machine names them, "MACHINE n" where it uses none, the rest as the
		// host names them
		juce::StringArray targetLabels(const juce::AudioParameterChoice& _target, const Machine& _machine)
		{
			juce::StringArray labels;
			for(int i = 0; i < _target.choices.size(); ++i)
			{
				const int slot = machineOf(static_cast<LfoTarget>(i));
				const char* name = slot >= 0 ? names::control(_machine, slot) : nullptr;
				labels.add(name ? juce::String(name) : _target.choices[i].toUpperCase());
			}
			return labels;
		}

		juce::StringArray outputs()
		{
			juce::StringArray o{"MAIN"};
			for(int i = 1; i < 16; ++i)
				o.add("OUT " + juce::String(i));
			return o;
		}

		const juce::StringArray scaleNames{"1/8X", "1/4X", "1/2X", "3/4X", "1X", "3/2X", "2X"};
		// The pattern's length (t-ui-spec, common length)
		const juce::StringArray lengthChoices{"4", "8", "12", "16", "24", "32", "48", "64", "96", "128", "256", "512"};

		juce::String patternName(const int _index)
		{
			return juce::String::charToString(static_cast<juce::juce_wchar>('A' + _index / 16))
				+ juce::String(_index % 16 + 1).paddedLeft('0', 2);
		}

		// A step's lists: a value each, its name in the same place
		struct StepList
		{
			std::vector<int> values;
			juce::StringArray names;
			int indexOf(const int _v) const
			{
				for(size_t i = 0; i < values.size(); ++i)
					if(values[i] == _v)
						return static_cast<int>(i);
				return 0;
			}
			int stepped(const int _v, const int _n) const
			{
				return values[static_cast<size_t>(juce::jlimit(0, static_cast<int>(values.size()) - 1, indexOf(_v) + _n))];
			}
		};
		// NOTE: the Track's own, then C1 to C7 (MIDI 24 to 96, C4 the Note's default of 60)
		const StepList& noteList()
		{
			static const StepList list = []
			{
				StepList l{{NoNote}, {"TRACK"}};
				const char* names[]{"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
				for(int n = 24; n <= 96; ++n)
				{
					l.values.push_back(n);
					l.names.add(juce::String(names[n % 12]) + juce::String(n / 12 - 1));
				}
				return l;
			}();
			return list;
		}
		// DURATION in 8ths of a step, Elektron's lengths that fall on them, INF for none
		const StepList& gateList()
		{
			static const StepList list = []
			{
				StepList l{{0}, {"INF"}};
				for(const int g : {1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64, 96, 128, 192, 256, 384, 512})
				{
					l.values.push_back(g);
					const juce::String steps = g % GateUnits == 0 ? juce::String(g / GateUnits)
						: g == 12 ? juce::String("1.5") : juce::String(g) + "/" + juce::String(GateUnits);
					l.names.add(g == 2 ? "1/4 STEP" : g == 4 ? "1/2 STEP" : g == 6 ? "3/4 STEP"
						: steps + (g == GateUnits ? " STEP" : g < GateUnits ? " STEP" : " STEPS"));
				}
				return l;
			}();
			return list;
		}
		const StepList& microList()
		{
			static const StepList list = []
			{
				StepList l;
				for(int m = -(TimingUnits - 1); m < TimingUnits; ++m)
				{
					l.values.push_back(m);
					l.names.add(m == 0 ? juce::String("0") : (m > 0 ? "+" : juce::String::fromUTF8("\xe2\x88\x92"))
						+ juce::String(std::abs(m)) + " / 24");
				}
				return l;
			}();
			return list;
		}
		const StepList& ratchetList()
		{
			static const StepList list{{1, 2, 3, 4, 5, 6, 7, 8}, {"1", "2", "3", "4", "5", "6", "7", "8"}};
			return list;
		}
		// SPEED: the ratchet's Hits spread over the step, or so many a step
		const StepList& speedList()
		{
			static const StepList list = []
			{
				StepList l{{0}, {"SPREAD"}};
				for(int r = 2; r <= 16; ++r)
				{
					l.values.push_back(r);
					l.names.add(juce::String(r) + " / STEP");
				}
				return l;
			}();
			return list;
		}

		// The 65 conditions of spec 7 as the sequencer has them: Always, 22 probabilities, the 8 states, the A:B cycles
		struct ConditionList
		{
			std::vector<Condition> conditions;
			juce::StringArray names;
			ConditionList()
			{
				conditions.push_back({});
				names.add("ALWAYS");
				for(const int p : {99, 98, 96, 94, 91, 87, 81, 75, 67, 59, 50, 41, 33, 25, 19, 13, 9, 6, 4, 3, 1})
				{
					conditions.push_back({ConditionKind::Probability, static_cast<uint8_t>(p), 0});
					names.add(juce::String(p) + " %");
				}
				const std::pair<ConditionKind, const char*> states[]{{ConditionKind::Fill, "FILL"},
					{ConditionKind::NotFill, "NOT FILL"}, {ConditionKind::Pre, "PRE"}, {ConditionKind::NotPre, "NOT PRE"},
					{ConditionKind::Nei, "NEI"}, {ConditionKind::NotNei, "NOT NEI"}, {ConditionKind::First, "1ST"},
					{ConditionKind::NotFirst, "NOT 1ST"}};
				for(const auto& s : states)
				{
					conditions.push_back({s.first, 0, 0});
					names.add(s.second);
				}
				for(int b = 2; b <= 8; ++b)
					for(int a = 1; a <= b; ++a)
					{
						conditions.push_back({ConditionKind::Ratio, static_cast<uint8_t>(a), static_cast<uint8_t>(b)});
						names.add(juce::String(a) + ":" + juce::String(b));
					}
			}
			int indexOf(const Condition& _c) const
			{
				for(size_t i = 0; i < conditions.size(); ++i)
					if(conditions[i].kind == _c.kind && conditions[i].a == _c.a && conditions[i].b == _c.b)
						return static_cast<int>(i);
				return 0;
			}
		};
		const ConditionList& conditionList()
		{
			static const ConditionList list;
			return list;
		}

		// The filter's response, drawn: a biquad's, an EQ's bell, or the Machinedrum's (t.js biq). _mode: Filter Mode's
		// choice, LP, HP, EQ1-EQ5, OFF
		std::function<float(float)> response(const float _fc, const float _q, const FilterType _type, const int _mode)
		{
			constexpr int Hp = 1, Eq1 = 2, Eq5 = 6, ModeOff = 7;
			if(_type == FilterType::Off || (_type != FilterType::Machinedrum && _mode == ModeOff))
				return [](float) { return 0.0f; };
			if(_type != FilterType::Machinedrum && _mode >= Eq1 && _mode <= Eq5)
			{
				const float widths[]{0.7f, 1.1f, 1.6f, 2.4f, 3.4f};
				const float wd = widths[_mode - Eq1];
				return [=](const float _f)
				{
					const float l = std::log2(_f / _fc) * wd;
					return 12 * std::exp(-l * l) * std::min(1.5f, _q / 3);
				};
			}
			if(_type == FilterType::Machinedrum)
				return [=](const float _f)
				{
					const float l = std::log2(_f / _fc);
					return -24 * std::pow(l / 2.5f, 2.0f) * (1 - std::exp(-std::abs(l))) + _q;
				};
			const float w0 = 2 * juce::MathConstants<float>::pi * _fc / 44100, al = std::sin(w0) / (2 * _q),
				co = std::cos(w0), a0 = 1 + al, a1 = -2 * co, a2 = 1 - al;
			const bool hp = _mode == Hp;
			const float b0 = hp ? (1 + co) / 2 : (1 - co) / 2, b1 = hp ? -(1 + co) : 1 - co, b2 = b0;
			return [=](const float _f)
			{
				const float o = 2 * juce::MathConstants<float>::pi * _f / 44100, cr = std::cos(o), ci = std::sin(o),
					c2 = std::cos(2 * o), s2 = std::sin(2 * o);
				const float nr = b0 + b1 * cr + b2 * c2, ni = b1 * ci + b2 * s2, dr = a0 + a1 * cr + a2 * c2,
					di = a1 * ci + a2 * s2;
				return 10 * std::log10((nr * nr + ni * ni) / (dr * dr + di * di) + 1e-9f);
			};
		}

		float wave(const LfoShape _shape, const float _u)
		{
			const float f = _u - std::floor(_u);
			switch(_shape)
			{
			case LfoShape::Sine:		return std::sin(f * 6.2832f);
			case LfoShape::Square:		return f < 0.5f ? 1.0f : -1.0f;
			case LfoShape::Saw:			return 1 - 2 * f;
			case LfoShape::Random:
			{
				const float q = std::sin(std::floor(_u * 16) * 12.9898f) * 43758.5f;
				return (q - std::floor(q)) * 2 - 1;
			}
			case LfoShape::Exponential:	return 1 - f * 2;
			case LfoShape::Ramp:		return f < 0.5f ? 2 * f : 0.0f;
			case LfoShape::Triangle:	break;
			}
			return f < 0.25f ? 4 * f : f < 0.75f ? 2 - 4 * f : 4 * f - 4;
		}

		void picture(const Painter& _p, const float _x, const float _y, const float _w, const float _h, const Hue& _hue,
			const std::vector<juce::Point<float>>& _guide, const std::vector<juce::Point<float>>& _curve, const bool _off)
		{
			_p.frame(_x, _y, _w, _h, _hue.c[1], 1.2f, 0.4f);
			if(!_guide.empty())
				_p.poly(_guide, colour::rule, 0.3f);
			_p.poly(_curve, _off ? colour::dim : _hue.c[0], 0.55f);
		}

		void ladder(const Painter& _p, const float _x, const float _y, const float _w, const float _h, const float _f)
		{
			const int n = juce::roundToInt(_h / 2.3f);
			const float pitch = _h / static_cast<float>(n), seg = pitch - 0.4f;
			const int lit = juce::roundToInt(_f * static_cast<float>(n));
			for(int i = 0; i < n; ++i)
			{
				const float u = (static_cast<float>(i) + 0.5f) / static_cast<float>(n);
				const auto c = i >= lit ? colour::unlit : u < 0.68f ? colour::meterGreen : u < 0.9f ? colour::meterAmber
					: colour::meterRed;
				_p.rect(_x, _y + _h - static_cast<float>(i + 1) * pitch + 0.2f, _w, seg, c);
			}
		}

		Cell cellOf(juce::RangedAudioParameter* _p, const juce::String& _name, const float _x, const float _y,
			const float _w, const Hue& _hue, const juce::String& _section)
		{
			Cell c;
			c.parameter = _p;
			c.name = _name;
			c.x = _x;
			c.y = _y;
			c.w = _w;
			c.hue = &_hue;
			c.section = _section;
			return c;
		}

		uint64_t hashOf(const std::vector<juce::RangedAudioParameter*>& _ps)
		{
			uint64_t h = 0;
			for(const auto* p : _ps)
				if(p)
					h = mixFloat(h, p->getValue());
			return h;
		}

		// The Lock of parameter _parameter on _track's step _step, nullptr for none (Pattern::setLock keeps one a parameter)
		const Lock* lockAt(const Pattern& _p, const int _track, const int _step, const int _parameter)
		{
			const auto* l = _p.locks(_track, _step);
			for(int i = 0; l && i < _p.step(_track, _step).lockCount; ++i)
				if(l[i].parameter == _parameter)
					return &l[i];
			return nullptr;
		}

		// The steps of _track's 64 that hold a Lock of _parameter
		int lockCount(const Pattern& _p, const int _track, const int _parameter)
		{
			int n = 0;
			for(int s = 0; s < SeqSteps; ++s)
				n += lockAt(_p, _track, s, _parameter) ? 1 : 0;
			return n;
		}
	}

	// ---- shared pieces ----

	void Editor::block(const float _x, const float _y, const float _w, const juce::String& _section,
		const juce::String& _title, const Hue& _hue, const float _h, const int _rank)
	{
		m_blocks.push_back({_x, _y, _w, _title, _section, &_hue, _rank});
		Hit focus;
		focus.r = {_x, _y, _w - 1, _h};
		focus.press = [this, _section] { m_focus = _section; };
		focus.cursor = juce::MouseCursor::NormalCursor;
		addHit(std::move(focus));
	}

	void Editor::drawBlockTitle(Painter& _p, const Block& _b)
	{
		const bool focus = _b.section == m_focus;
		const bool off = sectionOff(_b.section) || _b.section == "x";
		// Rank 1, the page's first block: a full rule, its name at 4.0 mm; rank 2, a block that matters less: a thin rule,
		// its name at 2.8 in grey (u.js blockTitle)
		const float rule = focus ? 1.0f : _b.rank == 1 ? 0.8f : _b.rank == 2 ? 0.25f : 0.4f;
		_p.rect(_b.x + 2, _b.y, _b.w - 6, rule, focus || _b.rank == 1 ? _b.hue->c[0] : off ? colour::faint : _b.hue->c[1]);
		_p.text(_b.x + 2, _b.y + 6.2f, _b.title, _b.rank == 1 ? 4.0f : _b.rank == 2 ? 2.8f : 3.2f, off ? colour::dim
			: _b.rank == 2 ? colour::mid : _b.hue->c[4], 600);
	}

	void Editor::drawHeader(Painter& _p, const juce::String& _title, const juce::String& _sub, const juce::String& _right)
	{
		const float x = ScreenX + 4, y = ScreenY + 7.4f;
		_p.text(x, y, _title, 4, colour::ink, 600);
		if(_sub.isNotEmpty())
			_p.text(x + static_cast<float>(_title.length()) * 2.6f + 6, y, _sub, 2.8f, colour::mid);
		_p.text(ScreenX + ScreenW - 36, y, _right.isNotEmpty() ? _right : patternName(m_processor.currentPattern()), 2.8f,
			colour::mid, 500, Align::Right);
		bool playing = false;
		for(int t = 0; t < 16; ++t)
			playing = playing || m_processor.playingStep(t) >= 0;
		_p.text(ScreenX + ScreenW - 31, y, "HOST", 2.8f, playing ? hue::t.c[4] : colour::mid);
		_p.text(ScreenX + ScreenW - 4, y, playing ? "PLAYING" : "STOPPED", 2.8f, playing ? hue::t.c[4] : colour::mid,
			500, Align::Right);
	}

	void Editor::drawButton(Painter& _p, const float _x, const float _y, const float _w, const float _h,
		const juce::String& _text, const bool _on, const bool _enabled)
	{
		_p.rect(_x, _y, _w, _h, _on ? hue::t.c[3] : colour::button, 0.8f);
		_p.text(_x + _w / 2, _y + _h / 2 + 1.5f, _text, 3, !_enabled ? juce::Colour(0xff4E4E4C) : _on ? hue::t.c[4]
			: colour::mid, _on ? 600 : 500, Align::Centre);
	}

	void Editor::drawSegments(Painter& _p, const float _x, const float _y, const float _w,
		const juce::StringArray& _items, const int _on, const Hue* _hue, const int _enabled)
	{
		const float h = 6;
		_p.rect(_x, _y, _w * static_cast<float>(_items.size()), h, colour::button, 0.8f);
		if(_on >= 0)
			_p.rect(_x + static_cast<float>(_on) * _w, _y, _w, h, _hue ? _hue->c[3] : hue::t.c[3], 0.8f);
		for(int i = 0; i < _items.size(); ++i)
			_p.text(_x + (static_cast<float>(i) + 0.5f) * _w, _y + h / 2 + 1.5f, _items[i], 3.2f,
				i == _on ? (_hue ? _hue->c[4] : hue::t.c[4]) : i >= _enabled ? colour::dim : colour::mid, i == _on ? 600 : 500,
				Align::Centre);
	}

	void Editor::drawCell(Painter& _p, const Cell& _c)
	{
		if(!_c.visible)
			return;
		const bool off = _c.off || !_c.parameter || sectionOff(_c.section);
		float value = _c.normalised();
		const bool locked = lockOf(_c, value);
		const auto& h = *_c.hue;
		const auto text = _c.valueText(value);
		switch(_c.look)
		{
		case Look::Field:
			_p.text(_c.x, _c.y, _c.name, 2.8f, colour::mid);
			_p.text(_c.x + _p.textWidth(_c.name, 2.8f) + 2, _c.y, text, 2.8f, off ? colour::dim : colour::ink, 600);
			return;
		case Look::Title:
			_p.text(_c.x, _c.y + 6.2f, text, 2.8f, h.c[4], 500, Align::Right);
			return;
		case Look::Button:
			drawButton(_p, _c.x, _c.y, _c.w, _c.h, _c.name, _c.index() != 0);
			return;
		case Look::Segments:
		{
			juce::StringArray items = _c.labels;
			int on = _c.index();
			if(_c.reversed)
			{
				items = {_c.labels[1], _c.labels[0]};
				on = 1 - on;
			}
			drawSegments(_p, _c.x, _c.y, _c.w / 2, items, on);
			return;
		}
		case Look::Fader:
		{
			const float y = _c.y + _c.h * (1 - value * 0.95f);
			_p.rect(_c.x + 3, y - 0.4f, _c.w - 6, 0.8f, h.c[0], 0.2f);
			return;
		}
		case Look::Small:
			_p.text(_c.x + _c.w / 2, _c.y + 4.4f, text, 2.6f, _c.isList() ? colour::dim : colour::mid, 500,
				Align::Centre);
			if(_c.isList())
				_p.rect(_c.x + 1.5f, _c.y + 5.5f, _c.w - 3, 0.15f, colour::rule);
			return;
		case Look::Cell:
		case Look::Plain:
			break;
		}
		const bool edit = lockEdit() && lockable(_c) && !off;
		_p.text(_c.x, _c.y + 3.3f, _c.name, 3.2f, locked ? colour::lock : edit ? colour::lockLight : off ? colour::dim
			: colour::mid);
		if(m_page == Page::Sound && _c.parameter && _c.look == Look::Cell)
			_p.rect(_c.x, _c.y + 4, static_cast<float>(_c.name.length()) * 1.72f + 0.2f, 0.15f, colour::rule);
		_p.text(_c.x, _c.y + 10, text, 4, locked ? colour::lock : off ? colour::dim : colour::ink, 600);
		if(_c.isList())
		{
			if(locked)
				_p.text(_c.x + _c.w - 2.5f, _c.y + 3.3f, _c.valueText(_c.normalised()), 2.6f, colour::base, 500,
					Align::Right);
			return;
		}
		if(_c.look == Look::Plain)
			return;
		const float w = _c.w - 4;
		_p.rect(_c.x, _c.y + 12, w, 0.55f, off ? colour::unlit : h.c[1]);
		_p.rect(_c.x, _c.y + 12, w * value, 0.55f, off ? colour::dim : locked ? juce::Colour(0xff8E2A22) : h.c[0]);
		if(locked)
		{
			_p.rect(_c.x + w * _c.normalised() - 0.2f, _c.y + 11.2f, 0.4f, 2.2f, colour::base);
			_p.text(_c.x + _c.w - 4, _c.y + 10, _c.valueText(_c.normalised()), 3.2f, colour::base, 500, Align::Right);
			_p.rect(_c.x + w * value - 0.45f, _c.y + 10.9f, 0.9f, 2.8f, colour::lock);
		}
		else if(_c.parameter)
			_p.rect(_c.x + w * value - 0.25f, _c.y + 11.3f, 0.5f, 2, off ? colour::dim : h.c[4]);
	}

	void Editor::drawPage(Painter& _p)
	{
		for(const auto& b : m_blocks)
			drawBlockTitle(_p, b);
		switch(m_page)
		{
		case Page::Sound:
			drawHeader(_p, "SOUND", juce::String(m_track + 1) + "  " + machineName(m_track));
			drawSoundPictures(_p);
			break;
		case Page::Seq: drawSeq(_p); break;
		case Page::Master: drawMaster(_p); break;
		case Page::Fx: drawFx(_p); break;
		case Page::Kits: drawKits(_p); break;
		case Page::Browse: drawBrowse(_p); break;
		}
		// The Menu panel's own cells after the panel, which covers the page's
		for(const auto& c : m_cells)
			if(c.section != "menu")
				drawCell(_p, c);
	}

	// ---- Sound: one page per Track, a block per section in the signal's order ----

	juce::Rectangle<int> Editor::machinePageArea(const int _page) const
	{
		return toPx(machinePageBox(_page));
	}

	juce::Rectangle<int> Editor::grainArea(const int _i) const
	{
		return toPx(grainBox(_i));
	}

	juce::Rectangle<int> Editor::lfoCardArea(const int _lfo) const
	{
		return toPx(lfoCard(_lfo));
	}

	void Editor::buildSound()
	{
		const auto t = [this](const char* _suffix) { return trackParameter(_suffix); };
		// A block's cells: _columns across its width, rows of 18 mm from _top. A cell hidden is made so, taking no mouse
		const auto put = [this](const float _x, const float _top, const float _w, const int _columns, const int _slot,
			juce::RangedAudioParameter* _p, const juce::String& _name, const Hue& _hue, const juce::String& _section,
			const bool _visible = true)
		{
			const float cw = (_w - 2) / static_cast<float>(_columns);
			Cell c = cellOf(_p, _name, _x + 2 + static_cast<float>(_slot % _columns) * cw,
				_top + static_cast<float>(_slot / _columns) * 18, cw, _hue, _section);
			c.visible = _visible;
			return addCell(c);
		};
		// A list on a block's title line, right-aligned at _right; two segments there
		const auto title = [this](juce::RangedAudioParameter* _p, const float _right, const float _y, const float _w,
			const Hue& _hue, const juce::String& _section)
		{
			Cell c = cellOf(_p, "", _right, _y, _w, _hue, _section);
			c.look = Look::Title;
			return addCell(c);
		};
		const auto segments = [this](juce::RangedAudioParameter* _p, const juce::String& _name, const float _x,
			const float _y, const float _w, const juce::StringArray& _labels, const Hue& _hue, const juce::String& _section)
		{
			Cell c = cellOf(_p, _name, _x, _y + 1.8f, _w, _hue, _section);
			c.look = Look::Segments;
			c.labels = _labels;
			c.h = 6;
			return addCell(c);
		};

		// VOICE: the machine, its name on the title line (a click opens Browse), its picture, NOTE and CHOKES, its eight
		// values named by the machine
		{
			const float x = SrcX, w = SrcW;
			block(x, SoundY, w, "m", "VOICE", hue::k, 150, 1);
			title(t("machine"), x + w - 4, SoundY, 34, hue::k, "m");
			const float cw = (w - 2) / 4;
			Cell note = cellOf(t("note"), "NOTE", x + 2, SoundY + 96, cw, hue::k, "");
			note.look = Look::Plain;
			addCell(note);
			Cell chokes = cellOf(t("chokes"), "CHOKES", x + 2 + cw, SoundY + 96, 3 * cw, hue::k, "");
			chokes.look = Look::Plain;
			chokes.format = Format::Chokes;
			addCell(chokes);
			const int choice = choiceOf(*t("machine"));
			m_machinePage = std::min(m_machinePage, machinePages() - 1);
			const auto labels = machineLabels(trackMachine(m_track), m_machinePage);
			for(int i = 0; i < 8; ++i)
			{
				const auto& l = labels[static_cast<size_t>(i)];
				const int n = m_machinePage * 8 + i + 1;
				const int c = put(x, SoundY + 113, w, 4, i, t(("m" + juce::String(n)).toRawUTF8()),
					l.isEmpty() ? "M" + juce::String(n) : l, hue::k, "m");
				m_cells[static_cast<size_t>(c)].off = l.isEmpty();
			}
			// The page selector in the head, left of the machine's name: a page the machine does not use stays shut
			for(int i = 0; i < 3; ++i)
			{
				Hit page;
				page.r = machinePageBox(i);
				page.click = [this, i] { showMachinePage(i); };
				addHit(std::move(page));
			}
			// The Sampler: GRAIN's two segments in the head (t.js, sound()), Machine 9 at 0 or 127
			if(choice == m_processor.samplerChoice())
			{
				for(int i = 0; i < 2; ++i)
				{
					Hit grain;
					grain.r = grainBox(i);
					grain.click = [this, i]
					{
						setNormalised(*trackParameter("m9"), i ? 1.0f : 0.0f);
						// A Sampler saved before the grains holds 0 there: 6 bits, linear, 100 Hz. VINTAGE takes the new
						// Sampler's values instead, in the same click
						const char* own[]{"m10", "m11", "m12"};
						bool unset = true;
						for(const char* id : own)
							unset = unset && trackParameter(id)->getValue() == 0.0f;
						if(i && unset)
						{
							const auto d = allDefaults(Sampler);
							for(int k = 0; k < 3; ++k)
							{
								auto& p = *trackParameter(own[k]);
								setNormalised(p, p.convertTo0to1(static_cast<float>(d[static_cast<size_t>(vintage::BitsSlot + k)])));
							}
						}
						repaint();
					};
					addHit(std::move(grain));
				}
				watch(grainBox(0).getUnion(grainBox(1)), [this] { return hashOf({trackParameter("m9")}); });
			}
			// SOPHIE: its 16 factory sounds in the block's picture, a click lists them (Machine 1-20 as each sets them)
			if(choice == m_processor.sophieChoice())
			{
				Hit sound;
				sound.r = voicePicture;
				sound.click = [this]
				{
					juce::StringArray names;
					for(const char* n : sophie::SoundNames)
						names.add(n);
					openSelector(SrcX + 1, voicePicture.getY() + 4, names,
						[this](const int _i) { return m_processor.sophieSound(m_track) == _i; },
						[this](const int _i) { m_processor.setSophieSound(m_track, _i); repaint(); });
				};
				addHit(std::move(sound));
			}
			watch(voicePicture, [this]
			{
				return mix(mix(static_cast<uint64_t>(choiceOf(trackParameter("machine")->getParameterID())),
					static_cast<uint64_t>(m_processor.sampleName(m_track).hashCode64())),
					static_cast<uint64_t>(m_processor.sophieSound(m_track) + 1));
			});
		}

		// SAMPLE LAYER: LOAD, the Sampler's file and CLEAR on the title line; the layer's values arrive with J2, greyed,
		// all on one page, MIX M/S a cell of the second row
		{
			const float x = SrcX, w = SrcW;
			block(x, LowY, w, "x", "SAMPLE LAYER", hue::g, 53.5f);
			Hit load;
			load.r = {x + 30, LowY + 1.4f, 19, 6};
			load.click = [this] { chooseSample(m_track); };
			addHit(std::move(load));
			const char* first[]{"START", "END", "LOOP", "TUNE", "LEVEL"};
			for(int i = 0; i < 5; ++i)
				m_cells[static_cast<size_t>(put(x, LowY + 9.2f, w, 5, i, nullptr, first[i], hue::g, "x"))].off = true;
			const char* second[]{"PLAY", "NOTE", "MIX M/S"};
			for(int i = 0; i < 3; ++i)
				m_cells[static_cast<size_t>(put(x, LowY + 31, w, 3, i, nullptr, second[i], hue::g, "x"))].off = true;
			if(m_processor.sampleName(m_track).isNotEmpty())
			{
				Hit clear;
				clear.r = {x + w - 24, LowY + 1.4f, 20, 6};
				clear.click = [this] { m_processor.clearSample(m_track); layout(); repaint(); };
				addHit(std::move(clear));
			}
		}

		// Drive 1 and 2, side by side: TYPE and PLACE on the title line, AMOUNT and MIX; no picture, a sine through the
		// curve said nothing of the sound. TYPE stays live while the drive is off, being what turns it on
		for(int d = 0; d < 2; ++d)
		{
			const float x = TimX + static_cast<float>(d) * 4 * Col, w = HalfW;
			const juce::String s = d ? "d2" : "d1", pre = d ? "drv2_" : "drv1_";
			block(x, SoundY, w, s, d ? "DRIVE 2" : "DRIVE 1", hue::a, 27);
			title(t((pre + "type").toRawUTF8()), x + w - 30, SoundY, 30, hue::a, "");
			segments(t((pre + "post").toRawUTF8()), "PLACE", x + w - 26, SoundY, 22, {"PRE", "POST"}, hue::a, s);
			put(x, SoundY + 9, w, 2, 0, t((pre + "amount").toRawUTF8()), "AMOUNT", hue::a, s);
			put(x, SoundY + 9, w, 2, 1, t((pre + "mix").toRawUTF8()), "MIX", hue::a, s);
		}

		// Filter: TYPE on the title line; CUTOFF, RESO, KEY TRACK and MODE (WIDTH with the window filter, which has no
		// mode) beside the response; ENVELOPE under it, ENV AMT beside its picture, its five times in a row
		{
			const float x = TimX, w = TimW, y = FltY, cw = 31;
			block(x, y, w, "f", "FILTER", hue::s, 118);
			const bool md = choiceOf(t("flt_type")->getParameterID()) == 2;
			title(t("flt_type"), x + w - 4, y, 40, hue::s, "f");
			const auto at = [&](const float _x, const float _y, const char* _id, const char* _name, const bool _visible)
			{
				Cell c = cellOf(t(_id), _name, _x, _y, cw, hue::s, "f");
				c.visible = _visible;
				addCell(c);
			};
			at(x + 2, y + 9, "flt_freq", "CUTOFF", true);
			at(x + 2 + cw, y + 9, "flt_res", "RESO", true);
			at(x + 2, y + 27, "flt_key", "KEY TRACK", true);
			at(x + 2 + cw, y + 27, "flt_mode", "MODE", !md);
			at(x + 2 + cw, y + 27, "flt_width", "WIDTH", md);
			const float ey = y + 54;
			Cell reset = cellOf(t("fenv_reset"), "ENV RETRIG", x + w - 34, ey + 1.2f, 30, hue::s, "f");
			reset.look = Look::Button;
			reset.h = 6;
			addCell(reset);
			at(x + 2, ey + 9, "fenv_depth", "ENV AMT", true);
			const char* ids[]{"fenv_delay", "fenv_attack", "fenv_decay", "fenv_sustain", "fenv_release"};
			const char* names[]{"DELAY", "ATTACK", "DECAY", "SUSTAIN", "RELEASE"};
			for(int i = 0; i < 5; ++i)
				put(x, ey + 48, w, 5, i, t(ids[i]), names[i], hue::s, "f");
			watch(responsePicture.getUnion(envelopePicture), [this, ids]
			{
				std::vector<juce::RangedAudioParameter*> ps{trackParameter("flt_type"), trackParameter("flt_mode"),
					trackParameter("flt_freq"), trackParameter("flt_res")};
				for(const char* id : ids)
					ps.push_back(trackParameter(id));
				return hashOf(ps);
			});
		}

		// Amp: MODE on the title line; LEVEL, PAN, ACCENT; VEL TARGET, VEL AMT, AMP ENV; the envelope's picture, then the
		// times its mode has (AHD or ADSR), the others hidden. The envelope's cells, "ae", greyed while AMP ENV is off: a
		// Machinedrum voice keeps its own
		{
			const float x = ModX, w = ModW, y = SoundY;
			block(x, y, w, "a", "AMP", hue::p, 92);
			segments(t("amp_mode"), "MODE", x + w - 34, y, 30, {"AHD", "ADSR"}, hue::p, "ae");
			put(x, y + 9, w, 3, 0, t("level"), "LEVEL", hue::p, "a");
			const int pan = put(x, y + 9, w, 3, 1, t("pan"), "PAN", hue::p, "a");
			m_cells[static_cast<size_t>(pan)].format = Format::Pan;
			put(x, y + 9, w, 3, 2, t("accent"), "ACCENT", hue::p, "a");
			put(x, y + 27, w, 3, 0, t("vel_target"), "VEL TARGET", hue::p, "a");
			put(x, y + 27, w, 3, 1, t("vel_depth"), "VEL AMT", hue::p, "a");
			put(x, y + 27, w, 3, 2, t("amp_on"), "AMP ENV", hue::p, "a");
			const bool adsr = choiceOf(t("amp_mode")->getParameterID()) == 1;
			const char* ids[]{"amp_attack", "amp_hold", "amp_decay", "amp_sustain", "amp_release"};
			const char* names[]{"ATTACK", "HOLD", "DECAY", "SUSTAIN", "RELEASE"};
			const int ahdSlot[]{0, 1, 2, -1, -1}, adsrSlot[]{0, -1, 1, 2, 3};
			for(int i = 0; i < 5; ++i)
			{
				const int slot = adsr ? adsrSlot[i] : ahdSlot[i];
				put(x, y + 75.5f, w, adsr ? 4 : 3, std::max(0, slot), t(ids[i]), names[i], hue::p, "ae", slot >= 0);
			}
			watch(ampPicture, [this, ids]
			{
				std::vector<juce::RangedAudioParameter*> ps{trackParameter("amp_mode")};
				for(const char* id : ids)
					ps.push_back(trackParameter(id));
				ps.push_back(trackParameter("amp_on"));
				return hashOf(ps);
			});
		}

		// LFO: the one shown named on the title line, its ten values, its wave; two cards under it choose it, a dot on one
		// whose LFO aims somewhere with a depth. RATE or HZ as SYNC says; MULT, FADE, PHASE have no engine. TARGET stays
		// live while the LFO aims nowhere, being what turns it on
		{
			const float x = ModX, w = ModW, cw = (w - 2) / 5;
			const juce::String s = m_lfo ? "l2" : "l1";
			block(x, LfoY, w, s, m_lfo ? "LFO 2" : "LFO 1", m_lfo ? hue::m : hue::y, 111.5f);
			for(int i = 0; i < 2; ++i)
			{
				Hit card;
				card.r = lfoCard(i);
				card.click = [this, i] { showLfo(i); };
				addHit(std::move(card));
			}
			const juce::StringArray shapes{"TRI", "SIN", "SQR", "SAW", "EXP", "RMP", "RND"};
			juce::StringArray tracks{"OWN"};
			for(int i = 1; i <= 16; ++i)
				tracks.add(juce::String(i));
			for(int l = 0; l < 2; ++l)
			{
				const juce::String pre = "lfo" + juce::String(l + 1) + "_", sec = "l" + juce::String(l + 1);
				const Hue& hl = l ? hue::m : hue::y;
				const bool shown = l == m_lfo;
				const bool sync = choiceOf(trackParameter(pre + "sync")->getParameterID()) != 0;
				const auto at = [&](const int _n, const char* _suffix, const juce::String& _name)
				{
					Cell c = cellOf(_suffix ? trackParameter(pre + _suffix) : nullptr, _name,
						x + 2 + static_cast<float>(_n % 5) * cw, LfoY + 9 + static_cast<float>(_n / 5) * 18, cw, hl, sec);
					c.visible = shown;
					c.off = _suffix == nullptr;
					return c;
				};
				Cell speed = at(0, "speed", "RATE");
				speed.visible = shown && sync;
				addCell(speed);
				Cell hz = at(0, "hz", "RATE");
				hz.visible = shown && !sync;
				addCell(hz);
				addCell(at(1, "feel", "FEEL"));
				Cell shape = at(2, "shape", "WAVE");
				shape.labels = shapes;
				addCell(shape);
				addCell(at(3, "mode", "TRIG MODE"));
				addCell(at(4, "depth", "DEPTH"));
				Cell track = at(5, "track", "ON TRACK");
				track.labels = tracks;
				addCell(track);
				addCell(at(6, nullptr, "MULT"));
				addCell(at(7, nullptr, "FADE"));
				addCell(at(8, nullptr, "PHASE"));
				// Machine 1-24 named by the machine of the Track the LFO aims at; last in its row, its names the longest
				Cell target = at(9, "target", "TARGET");
				target.section = "";
				const int aimed = choiceOf(trackParameter(pre + "track")->getParameterID()) - 1;
				target.labels = targetLabels(*dynamic_cast<const juce::AudioParameterChoice*>(target.parameter),
					trackMachine(aimed < 0 ? m_track : aimed));
				addCell(target);
				Cell syncCell = cellOf(trackParameter(pre + "sync"), "SYNC", x + w - 32, LfoY + 1.8f, 28, hl, "");
				syncCell.look = Look::Segments;
				syncCell.labels = {"HZ", "SYNC"};
				syncCell.reversed = true;
				syncCell.h = 6;
				syncCell.visible = shown;
				addCell(syncCell);
			}
			watch(lfoPicture.getUnion(lfoCard(0)).getUnion(lfoCard(1)), [this]
			{
				std::vector<juce::RangedAudioParameter*> ps;
				for(const char* p : {"lfo1_", "lfo2_"})
					for(const char* v : {"shape", "depth", "target", "mode", "sync", "speed", "hz"})
						ps.push_back(trackParameter(juce::String(p) + v));
				return hashOf(ps);
			});
		}

		// EQ: TYPE and PLACE on the title line, the values its type has named by it, its curve under them
		{
			const float x = TimX, w = HalfW, y = LowY;
			block(x, y, w, "b", "EQ", hue::b, 53.5f, 2);
			const int type = choiceOf(t("band_type")->getParameterID());
			const int t2 = title(t("band_type"), x + w - 28, y, 30, hue::b, "b");
			m_cells[static_cast<size_t>(t2)].labels = {"OFF", "CUTS", "PEAK", "3 BAND"};
			segments(t("band_after"), "PLACE", x + w - 26, y, 22, {"PRE", "POST"}, hue::b, "b");
			const std::array<juce::StringArray, 4> names{{{}, {"HPF", "LPF"}, {"PEAK FREQ", "PEAK GAIN"},
				{"LOW F", "LOW G", "MID F", "MID G", "HIGH F", "HIGH G"}}};
			const auto& n = names[static_cast<size_t>(juce::jlimit(0, 3, type))];
			const float cw3 = (w - 2) / 3;
			for(int i = 0; i < 6; ++i)
			{
				auto* p = t(("band_" + juce::String(i + 1)).toRawUTF8());
				const juce::String name = i < n.size() ? n[i] : "EQ " + juce::String(i + 1);
				if(type == 3)
				{
					Cell c = cellOf(p, name, x + 2 + static_cast<float>(i % 3) * cw3, y + 8.4f + static_cast<float>(i / 3) * 15.5f,
						cw3, hue::b, "b");
					c.h = 15.5f;
					addCell(c);
				}
				else
					put(x, y + 9, w, 2, i, p, name, hue::b, "b", i < n.size());
			}
			watch(eqPicture(type), [this]
			{
				std::vector<juce::RangedAudioParameter*> ps{trackParameter("band_type")};
				for(int i = 1; i <= 6; ++i)
					ps.push_back(trackParameter("band_" + juce::String(i)));
				return hashOf(ps);
			});
		}

		// Sends: OUT on the title line; a bar each send, dragged vertically, its value's cell under it
		{
			const float x = SndX, w = HalfW, y = LowY;
			block(x, y, w, "s", "SENDS", hue::w, 53.5f, 2);
			const int out = title(t("output"), x + w - 4, y, 24, hue::w, "s");
			m_cells[static_cast<size_t>(out)].labels = outputs();
			const char* ids[]{"send_delay", "send_reverb", "send_chorus", "send_phaser"};
			const char* names[]{"DELAY", "REVERB", "CHORUS", "PHASER"};
			for(int i = 0; i < 4; ++i)
			{
				const auto r = sendBar(i);
				Cell value = cellOf(t(ids[i]), names[i], r.getX(), r.getBottom() + 0.3f, r.getWidth(), hue::w, "s");
				value.look = Look::Small;
				value.h = 6;
				addCell(value);
				Hit bar;
				bar.r = r;
				bar.cursor = juce::MouseCursor::UpDownResizeCursor;
				bar.keepsLocks = true;
				const juce::String id = ids[i];
				const float bh = r.getHeight();
				bar.press = [this, id]
				{
					m_press.value = trackParameter(id)->getValue();
					trackParameter(id)->beginChangeGesture();
				};
				bar.drag = [this, id, bh](float, const float _dy)
				{
					m_press.value = juce::jlimit(0.0f, 1.0f, m_press.value - _dy / bh);
					trackParameter(id)->setValueNotifyingHost(m_press.value);
				};
				bar.end = [this, id] { trackParameter(id)->endChangeGesture(); };
				bar.click = bar.end;
				addHit(std::move(bar));
			}
			watch(sendBar(0).getUnion(sendBar(3)), [this]
			{
				return hashOf({trackParameter("send_delay"), trackParameter("send_reverb"), trackParameter("send_chorus"),
					trackParameter("send_phaser")});
			});
		}
	}

	void Editor::drawSoundPictures(Painter& _p)
	{
		const auto v = [this](const char* _s) { return trackParameter(_s)->getValue(); };
		const auto frame = [&](const juce::Rectangle<float>& _r, const juce::Colour _c)
		{
			_p.frame(_r.getX(), _r.getY(), _r.getWidth(), _r.getHeight(), _c, 1.2f, 0.4f);
		};

		// The groups' names above their columns; the oversampling at TIMBRE's right
		_p.text(SrcX + 2, 27.6f, "SOURCE", 2.8f, colour::mid, 600);
		_p.text(TimX + 2, 27.6f, "TIMBRE", 2.8f, colour::mid, 600);
		_p.text(ModX + 2, 27.6f, "AMP & MOD", 2.8f, colour::mid, 600);
		{
			const juce::String os[]{"OFF", "2X", "4X"};
			_p.text(TimX + TimW - 4, 27.6f, "OS " + os[juce::jlimit(0, 2, choiceOf("oversampling"))], 2.8f, hue::a.c[4],
				500, Align::Right);
		}

		// VOICE: the page selector; the Sampler's GRAIN and file, SOPHIE's factory sound; the hit's preview has no
		// engine yet
		{
			const auto box = machinePageBox(0);
			drawSegments(_p, box.getX(), box.getY(), box.getWidth(), {"1", "2", "3"}, m_machinePage, &hue::k,
				machinePages());
			const auto r = voicePicture;
			frame(r, hue::k.c[1]);
			if(choiceOf(trackParameter("machine")->getParameterID()) == m_processor.samplerChoice())
			{
				const auto grain = grainBox(0);
				_p.text(SrcX + 34, SoundY + 5.2f, "GRAIN", 2.8f, colour::mid);
				const bool vintage = vintage::isVintage(juce::roundToInt(trackParameter("m9")->convertFrom0to1(v("m9"))));
				drawSegments(_p, grain.getX(), grain.getY(), grain.getWidth(), {"12-BIT", "VINTAGE"}, vintage ? 1 : 0, &hue::k);
				const auto name = m_processor.sampleName(m_track);
				_p.text(r.getCentreX(), r.getCentreY() + 1.6f, name.isEmpty() ? "NO SAMPLE" : name.toUpperCase(), 3.2f,
					name.isEmpty() ? colour::mid : hue::k.c[4], 500, Align::Centre);
			}
			else if(choiceOf(trackParameter("machine")->getParameterID()) == m_processor.sophieChoice())
			{
				const int sound = m_processor.sophieSound(m_track);
				_p.text(r.getX() + 3, r.getY() + 6.4f, "FACTORY SOUND", 2.8f, colour::mid);
				_p.text(r.getCentreX(), r.getCentreY() + 2, sound < 0 ? "EDITED" : sophie::SoundNames[static_cast<size_t>(sound)],
					4, sound < 0 ? colour::mid : hue::k.c[4], 600, Align::Centre);
			}
		}
		// SAMPLE LAYER's title line: LOAD, the file, CLEAR
		{
			const float x = SrcX, w = SrcW;
			drawButton(_p, x + 30, LowY + 1.4f, 19, 6, "LOAD", false);
			_p.text(x + w - 26, LowY + 6.2f, "GRAIN J2", 2.8f, colour::dim, 500, Align::Right);
			const auto name = fitted(_p, m_processor.sampleName(m_track).toUpperCase(), 2.8f,
				w - 26 - _p.textWidth("GRAIN J2", 2.8f) - 4 - 52, 600);
			_p.text(x + 52, LowY + 6.2f, name.isEmpty() ? "NO SAMPLE" : name, 2.8f, name.isEmpty() ? colour::dim
				: hue::g.c[4], 600);
			if(m_processor.sampleName(m_track).isNotEmpty())
				drawButton(_p, x + w - 24, LowY + 1.4f, 20, 6, "CLEAR", false);
		}
		// Filter: its response from FREQ and RESO; ENVELOPE's rule and name, its picture
		{
			const auto r = responsePicture;
			const float x = r.getX(), y = r.getY(), w = r.getWidth(), h = r.getHeight();
			const auto type = static_cast<FilterType>(choiceOf(trackParameter("flt_type")->getParameterID()));
			const int mode = choiceOf(trackParameter("flt_mode")->getParameterID());
			const float fc = 20 * std::pow(1000.0f, v("flt_freq")), q = 0.6f + v("flt_res") * 9;
			const auto f = response(fc, q, type, mode);
			std::vector<juce::Point<float>> curve;
			for(int i = 0; i <= 80; ++i)
			{
				const float hz = 20 * std::pow(1000.0f, static_cast<float>(i) / 80);
				curve.push_back({x + w * static_cast<float>(i) / 80, juce::jlimit(y + 1, y + h - 1,
					y + h * 0.35f - f(hz) / 36 * h * 0.6f)});
			}
			picture(_p, x, y, w, h, hue::s, {}, curve, false);
			const float gx = x + w * v("flt_freq"), gy = juce::jlimit(y + 1, y + h - 1, y + h * 0.35f - f(fc) / 36 * h * 0.6f);
			_p.dot(gx, gy, 1.5f, juce::Colours::black, hue::s.c[4], 0.5f);

			_p.rect(TimX + 2, FltY + 54, TimW - 6, 0.25f, hue::s.c[1]);
			_p.text(TimX + 2, FltY + 59, "ENVELOPE", 2.8f, hue::s.c[4], 600);
			const auto e = envelopePicture;
			const float ex = e.getX(), ey = e.getY(), ew = e.getWidth(), eh = e.getHeight();
			const float d0 = v("fenv_delay") * 0.2f, at = v("fenv_attack") * 0.25f, dc = v("fenv_decay") * 0.25f,
				su = v("fenv_sustain"), rl = v("fenv_release") * 0.25f, hd = 0.14f;
			const float total = std::max(d0 + at + dc + hd + rl, 1.0f);
			const auto X = [&](const float _u) { return ex + 1 + (ew - 2) * _u / total; };
			const auto Y = [&](const float _l) { return ey + eh - 1.5f - _l * (eh - 4); };
			const std::vector<juce::Point<float>> env{{X(0), Y(0)}, {X(d0), Y(0)}, {X(d0 + at), Y(1)},
				{X(d0 + at + dc), Y(su)}, {X(d0 + at + dc + hd), Y(su)}, {X(d0 + at + dc + hd + rl), Y(0)}};
			picture(_p, ex, ey, ew, eh, hue::s, {}, env, false);
			for(const size_t i : {size_t(1), size_t(2), size_t(3), size_t(5)})
				_p.dot(env[i].x, env[i].y, 1.5f, juce::Colours::black, hue::s.c[4], 0.5f);
		}
		// Amp: AHD or ADSR
		{
			const auto r = ampPicture;
			const float x = r.getX(), y = r.getY(), w = r.getWidth(), h = r.getHeight();
			const auto X = [&](const float _u) { return x + 1 + (w - 2) * _u; };
			const auto Y = [&](const float _l) { return y + h - 1.5f - _l * (h - 4); };
			const float at = v("amp_attack") * 0.25f;
			std::vector<juce::Point<float>> env;
			if(choiceOf(trackParameter("amp_mode")->getParameterID()) == 1)
			{
				const float dc = at + v("amp_decay") * 0.25f, su = v("amp_sustain");
				env = {{X(0), Y(0)}, {X(at), Y(1)}, {X(dc), Y(su)}, {X(dc + 0.15f), Y(su)},
					{X(dc + 0.15f + v("amp_release") * 0.25f), Y(0)}, {X(1), Y(0)}};
			}
			else
			{
				const float hd = at + v("amp_hold") * 0.25f;
				env = {{X(0), Y(0)}, {X(at), Y(1)}, {X(hd), Y(1)}, {X(hd + v("amp_decay") * 0.5f), Y(0)}, {X(1), Y(0)}};
			}
			const bool ampOff = sectionOff("ae");
			picture(_p, x, y, w, h, hue::p, {}, env, ampOff);
			for(size_t i = 1; !ampOff && i + 1 < env.size(); ++i)
				_p.dot(env[i].x, env[i].y, 1.5f, juce::Colours::black, hue::p.c[4], 0.5f);
		}
		// LFO: the wave of the one shown, scaled by its depth; the two cards
		{
			const juce::String pre = "lfo" + juce::String(m_lfo + 1) + "_";
			const Hue& h = m_lfo ? hue::m : hue::y;
			const auto r = lfoPicture;
			const float x = r.getX(), w = r.getWidth(), m = r.getCentreY(), a = r.getHeight() * 0.44f;
			const auto* p = trackParameter(pre + "depth");
			const float depth = p->convertFrom0to1(p->getValue()) / 127;
			const auto shape = static_cast<LfoShape>(juce::jlimit(0, static_cast<int>(LfoShape::Random),
				choiceOf(trackParameter(pre + "shape")->getParameterID())));
			std::vector<juce::Point<float>> guide, curve;
			for(int i = 0; i <= 160; ++i)
			{
				const float u = static_cast<float>(i) / 160, wv = wave(shape, u * 4);
				guide.push_back({x + 1 + (w - 2) * u, m - wv * a});
				curve.push_back({x + 1 + (w - 2) * u, m - wv * a * depth});
			}
			picture(_p, x, r.getY(), w, r.getHeight(), h, guide, curve, sectionOff(m_lfo ? "l2" : "l1"));
			// A card: its LFO's wave and mode, where it aims, its depth and rate, as its cells write them
			for(int i = 0; i < 2; ++i)
			{
				const juce::String pi = "lfo" + juce::String(i + 1) + "_";
				const Hue& hi = i ? hue::m : hue::y;
				const auto c = lfoCard(i);
				const bool shown = i == m_lfo;
				const auto text = [&](const char* _suffix)
				{
					Cell cell;
					cell.parameter = trackParameter(pi + _suffix);
					return cell.valueText(cell.parameter->getValue());
				};
				auto* target = dynamic_cast<const juce::AudioParameterChoice*>(trackParameter(pi + "target"));
				const bool active = target->getIndex() != 0;
				const int aimed = choiceOf(trackParameter(pi + "track")->getParameterID()) - 1;
				const auto aim = targetLabels(*target, trackMachine(aimed < 0 ? m_track : aimed))[target->getIndex()];
				const auto* d = trackParameter(pi + "depth");
				_p.rect(c.getX(), c.getY(), c.getWidth(), c.getHeight(), shown ? hi.c[2] : colour::cellBack, 1);
				_p.frame(c.getX(), c.getY(), c.getWidth(), c.getHeight(), shown ? hi.c[0] : hi.c[1], 1, shown ? 0.5f : 0.3f);
				_p.text(c.getX() + 3, c.getY() + 5.5f, "LFO " + juce::String(i + 1), 2.8f, hi.c[4], 600);
				if(active && std::abs(d->convertFrom0to1(d->getValue())) > 1)
					_p.rect(c.getX() + 14, c.getY() + 3.2f, 1.6f, 1.6f, hi.c[0], 0.8f);
				_p.text(c.getRight() - 3, c.getY() + 5.5f, text("shape") + "  " + text("mode"), 2.8f, active ? hi.c[0]
					: colour::dim, 500, Align::Right);
				_p.text(c.getX() + 3, c.getY() + 14, active ? aim : juce::String("NONE"), 3.2f, active ? colour::ink
					: colour::dim, 600);
				const bool sync = choiceOf(trackParameter(pi + "sync")->getParameterID()) != 0;
				_p.text(c.getX() + 3, c.getY() + 22, "DEPTH " + text("depth") + "  " + text(sync ? "speed" : "hz"), 2.8f,
					colour::mid);
			}
		}
		// EQ: its shape by type
		{
			const int type = choiceOf(trackParameter("band_type")->getParameterID());
			const auto r = eqPicture(type);
			const float x = r.getX(), w = r.getWidth(), m = r.getCentreY(), h = r.getHeight();
			const auto b = [&](const int _i) { return trackParameter("band_" + juce::String(_i))->getValue(); };
			const auto bell = [](const float _u, const float _f, const float _g)
			{
				return (_g - 0.5f) * 2 * std::exp(-std::pow((_u - _f) / 0.12f, 2.0f));
			};
			const auto sig = [](const float _z) { return 1 / (1 + std::exp(-_z / 0.016f)); };
			std::vector<juce::Point<float>> curve;
			for(int i = 0; i <= 240; ++i)
			{
				const float u = static_cast<float>(i) / 240;
				float l = 0;
				if(type == 1)
				{
					const float lo = b(1), hi = std::min(1.0f, b(1) + b(2));
					l = 2 * sig(u - lo) * (1 - sig(u - hi)) - 1;
				}
				else if(type == 2)
					l = bell(u, b(1), b(2));
				else if(type == 3)
					l = bell(u, b(1), b(2)) + bell(u, b(3), b(4)) + bell(u, b(5), b(6));
				curve.push_back({x + 1 + (w - 2) * u, m - juce::jlimit(-1.0f, 1.0f, l) * h * 0.42f});
			}
			picture(_p, x, r.getY(), w, h, hue::b, {}, curve, type == 0);
		}
		// Sends: a bar each, its name above
		{
			const char* names[]{"DLY", "REV", "CHO", "PHA"};
			const char* ids[]{"send_delay", "send_reverb", "send_chorus", "send_phaser"};
			for(int i = 0; i < 4; ++i)
			{
				const auto r = sendBar(i);
				const float fh = r.getHeight() * v(ids[i]);
				_p.text(r.getCentreX(), LowY + 12.6f, names[i], 2.8f, colour::mid, 600, Align::Centre);
				_p.rect(r.getX(), r.getY(), r.getWidth(), r.getHeight(), juce::Colour(0xff17181A), 0.8f);
				if(fh > 0.05f)
				{
					_p.rect(r.getX(), r.getBottom() - fh, r.getWidth(), fh, hue::w.c[3], 0.8f);
					_p.rect(r.getX(), r.getBottom() - fh, r.getWidth(), 0.6f, hue::w.c[0]);
				}
			}
		}
	}

	// ---- Seq, Detail ----

	std::vector<int> Editor::lockLineList() const
	{
		std::vector<int> lines;
		const auto& p = pattern();
		for(int s = 0; s < SeqSteps; ++s)
		{
			const auto* locks = p.locks(m_track, s);
			for(int i = 0; locks && i < p.step(m_track, s).lockCount; ++i)
				if(std::find(lines.begin(), lines.end(), locks[i].parameter) == lines.end())
					lines.push_back(locks[i].parameter);
		}
		for(const int l : m_lockLines[static_cast<size_t>(m_track)])
			if(std::find(lines.begin(), lines.end(), l) == lines.end())
				lines.push_back(l);
		return lines;
	}

	// As the sequencer plays the Track's steps (edDrumsSequencer.cpp) and Processor::applyEvent applies them: a step with
	// a trig or a Lock is an event, and an event releases what the Track's last one locked and locks its own; so a Lock
	// holds to the next event, then the base comes back, across the loop's end too. A trig's Lock of Machine 1-24 without
	// SLIDE is the Hit's (Engine::lock), which only the Track's next trig releases, or a trigless Lock of it. A trig with
	// SLIDE glides what it locks towards the next trig's Lock, or the base, until the next event. On a trig, a step's note
	// plays over a Lock of Note. A step whose condition is not Always may not play (Sequencer::emit): it neither releases
	// nor locks here, and its own Lock is drawn as one that may play
	std::vector<Editor::LaneStep> Editor::laneSteps(const int _line, int* _locks) const
	{
		const auto& p = pattern();
		auto* rp = dynamic_cast<juce::RangedAudioParameter*>(m_processor.getParameters()[_line]);
		const float base = rp ? rp->getValue() : 0.0f;
		std::vector<LaneStep> steps(static_cast<size_t>(SeqSteps), {base, base, false});
		const int length = std::min(SeqSteps, p.trackLength[static_cast<size_t>(m_track)]);
		if(_locks)
			*_locks = 0;
		if(!rp)
			return steps;
		const bool note = _line == m_keyChoices[static_cast<size_t>(m_track)].note->getParameterIndex();
		const bool machine = m_processor.lockTarget(_line).kind == Processor::LockKind::Machine;
		// Each step's own Lock, the step's note over it on a trig; whether a Lock in its list slides; whether the step is an
		// event, sure to play; whether its Lock is the Hit's; the value it locks
		std::array<bool, SeqSteps> own{}, events{}, hits{};
		std::array<float, SeqSteps> value;
		value.fill(base);
		// Past the Track's length, a step's Lock is counted only
		for(int s = 0; s < SeqSteps; ++s)
		{
			const auto* lock = lockAt(p, m_track, s, _line);
			if(_locks && lock)
				++*_locks;
			if(s >= length)
				continue;
			const auto i = static_cast<size_t>(s);
			auto& st = steps[i];
			const auto& step = p.step(m_track, s);
			const bool locks = lock != nullptr;
			bool has = locks;
			if(lock)
				value[i] = rp->convertTo0to1(lock->value);
			if(note && step.trig && step.note != NoNote)
			{
				value[i] = rp->convertTo0to1(static_cast<float>(step.note));
				has = true;
			}
			if(step.condition.kind != ConditionKind::Always)
			{
				st.may = has;
				st.would = value[i];
				continue;
			}
			st.locked = has;
			own[i] = locks;
			events[i] = step.trig || step.lockCount > 0;
			hits[i] = machine && step.trig && locks && !step.slide;
		}
		// Two laps, the second drawn: what the first left held is what every later lap starts with
		float held = base;
		bool hitHeld = false;
		for(int n = 0; n < 2 * length; ++n)
		{
			const auto i = static_cast<size_t>(n % length);
			if(events[i] && (p.step(m_track, n % length).trig || steps[i].locked || !hitHeld))
			{
				held = steps[i].locked ? value[i] : base;
				hitHeld = hits[i];
			}
			if(n >= length)
				steps[i].from = steps[i].to = held;
		}
		for(int s = 0; s < length; ++s)
		{
			const auto& step = p.step(m_track, s);
			if(!step.trig || !step.slide || !own[static_cast<size_t>(s)])
				continue;
			for(int d = 1; d <= length; ++d)
			{
				const int next = (s + d) % length;
				if(!p.step(m_track, next).trig)
					continue;
				const float v0 = steps[static_cast<size_t>(s)].from;
				const auto* lock = lockAt(p, m_track, next, _line);
				const float vt = lock ? rp->convertTo0to1(lock->value) : base;
				for(int k = 0; k < d && (k == 0 || !events[static_cast<size_t>((s + k) % length)]); ++k)
				{
					auto& st = steps[static_cast<size_t>((s + k) % length)];
					st.from = v0 + (vt - v0) * static_cast<float>(k) / static_cast<float>(d);
					st.to = v0 + (vt - v0) * static_cast<float>(k + 1) / static_cast<float>(d);
				}
				break;
			}
		}
		return steps;
	}

	void Editor::buildSeq()
	{
		for(int i = 0; i < 3; ++i)
		{
			Hit tab;
			tab.r = {SG + static_cast<float>(i) * SC, ScreenY + 2, SC, 6};
			tab.click = [this, i] { m_seqTab = i; };
			addHit(std::move(tab));
		}
		if(m_seqTab == 1)
		{
			buildOverview();
			return;
		}
		if(m_seqTab == 2)
		{
			buildPatterns();
			return;
		}
		const int o0 = m_stepPage * 16;
		// The pattern's fields: PATTERN is the host's parameter, the others the pattern's own
		{
			Cell pat = cellOf(m_processor.parameter("pattern"), "PATTERN", 16, 23, 31, hue::t, "");
			pat.look = Look::Plain;
			pat.format = Format::Pattern;
			pat.h = 13;
			addCell(pat);
			const auto field = [this](const float _x, std::function<void(int)> _steps, std::function<void()> _click)
			{
				Hit h;
				h.r = {_x - 1, 23, SC - 1, 12};
				h.stepMm = 1.2f;
				h.steps = std::move(_steps);
				h.click = std::move(_click);
				h.cursor = juce::MouseCursor::LeftRightResizeCursor;
				addHit(std::move(h));
			};
			field(SG, [this](const int _n)
			{
				editPattern([&](Pattern& _p) { _p.trackLength[static_cast<size_t>(m_track)] =
					juce::jlimit(1, SeqSteps, _p.trackLength[static_cast<size_t>(m_track)] + _n); });
			}, {});
			field(SG + SC, [this](const int _n)
			{
				editPattern([&](Pattern& _p)
				{
					auto& s = _p.trackScale[static_cast<size_t>(m_track)];
					s = static_cast<StepScale>(juce::jlimit(0, static_cast<int>(StepScale::Count) - 1, static_cast<int>(s) + _n));
				});
			}, [this]
			{
				openSelector(SG + SC - 1, 19, scaleNames,
					[this](const int _i) { return static_cast<int>(pattern().trackScale[static_cast<size_t>(m_track)]) == _i; },
					[this](const int _i) { editPattern([&](Pattern& _p) {
						_p.trackScale[static_cast<size_t>(m_track)] = static_cast<StepScale>(_i); }); });
			});
			field(SG + 2 * SC, [this](const int _n)
			{
				editPattern([&](Pattern& _p) { _p.swing = juce::jlimit(50, 80, _p.swing + _n); });
			}, {});
			field(SG + 5 * SC, [this](const int _n)
			{
				editPattern([&](Pattern& _p) { _p.masterLength = juce::jlimit(1, SeqMaxLength, _p.masterLength + _n); });
			}, [this]
			{
				openSelector(SG + 5 * SC - 1, 19, lengthChoices,
					[this](const int _i) { return pattern().masterLength == lengthChoices[_i].getIntValue(); },
					[this](const int _i) { editPattern([&](Pattern& _p) { _p.masterLength = lengthChoices[_i].getIntValue(); }); });
			});
		}
		// COPY, PASTE, CLEAR, SHIFT: the Track shown in the pattern playing
		{
			const std::array<std::function<void()>, 4> actions{
				[this] { m_trackClipboard = std::make_unique<Pattern>(pattern()); toast("Track " + juce::String(m_track + 1) + " copied"); },
				[this]
				{
					if(!m_trackClipboard)
						return;
					const Pattern from = *m_trackClipboard;
					editPattern([&](Pattern& _p) { _p.copyTrackFrom(from, m_track, m_track); });
				},
				[this] { editPattern([&](Pattern& _p) { _p.clearTrack(m_track); }); },
				[this]
				{
					// Each step one later, the last first, inside the Track's length, Locks with their step
					const Pattern from = pattern();
					const int len = from.trackLength[static_cast<size_t>(m_track)];
					editPattern([&](Pattern& _p)
					{
						for(int s = 0; s < len; ++s)
						{
							const int src = (s + len - 1) % len;
							auto& d = _p.step(m_track, s);
							const auto& o = from.step(m_track, src);
							const auto* locks = from.locks(m_track, src);
							std::vector<Lock> copy(locks, locks + (locks ? o.lockCount : 0));
							d = o;
							d.lockCount = 0;
							for(const auto& l : copy)
								_p.setLock(m_track, s, l.parameter, l.value);
							d.trig = o.trig;
						}
					});
				}};
			for(int i = 0; i < 4; ++i)
			{
				Hit b;
				b.r = {SG + static_cast<float>(12 + i) * SC, 24, SC - 1, 9.6f};
				b.click = actions[static_cast<size_t>(i)];
				addHit(std::move(b));
			}
			// LIVE (spec 7): the host's notes and the gestures recorded on the steps while the transport plays
			Hit live;
			live.r = {SG + 11 * SC, 24, SC - 1, 9.6f};
			live.click = [this] { m_processor.setLive(!m_processor.live()); };
			addHit(std::move(live));
		}
		for(int i = 0; i < 3; ++i)
		{
			Hit b;
			b.r = {SG + static_cast<float>(13 + i) * SC, 39, SC - 1, 7};
			b.click = [this, i] { m_seqMode = i; };
			addHit(std::move(b));
		}

		// The trig row and the lock lines: a click toggles, a drag paints or sets the value
		const auto stepAt = [o0](const float _x) { return o0 + juce::jlimit(0, 15, static_cast<int>((_x - SG) / SC)); };
		const auto grid = [this, stepAt](const juce::Rectangle<float> _r, const int _line)
		{
			// _line -1: the trigs, in the mode chosen; else a parameter's Locks
			const auto on = [this, _line](const int _n)
			{
				const auto& s = pattern().step(m_track, _n);
				if(_line < 0)
					return m_seqMode == 0 ? s.trig : m_seqMode == 1 ? s.accent : s.slide;
				return lockAt(pattern(), m_track, _n, _line) != nullptr;
			};
			const auto set = [this, _line](const int _n, const bool _to)
			{
				auto* p = _line >= 0 ? m_processor.getParameters()[_line] : nullptr;
				auto* rp = dynamic_cast<juce::RangedAudioParameter*>(p);
				editPattern([&](Pattern& _p)
				{
					if(_line < 0)
					{
						auto& s = _p.step(m_track, _n);
						if(m_seqMode == 1)
							s.accent = _to;
						else if(m_seqMode == 2)
							s.slide = _to;
						else
							_p.setTrig(m_track, _n, _to, _to ? 127 : 100);
					}
					else if(!_to)
						_p.removeLock(m_track, _n, static_cast<uint16_t>(_line));
					else if(rp)
						_p.setLock(m_track, _n, static_cast<uint16_t>(_line), rp->convertFrom0to1(rp->getValue()));
				});
			};
			Hit h;
			h.r = _r;
			h.cursor = juce::MouseCursor::CrosshairCursor;
			h.press = [this, stepAt, on, _line]
			{
				m_grid = {};
				m_grid.line = _line;
				m_grid.step = m_grid.last = stepAt(m_press.down.x);
				m_grid.was = on(m_grid.step);
			};
			h.drag = [this, stepAt, set, _line](const float _dx, const float _dy)
			{
				m_grid.travel += {_dx, _dy};
				if(!m_grid.direction)
				{
					if(m_grid.travel.getDistanceFromOrigin() < 2)
						return;
					const bool vertical = std::abs(m_grid.travel.y) > std::abs(m_grid.travel.x);
					const bool hasValue = _line >= 0 ? m_grid.was : m_seqMode == 0 && m_grid.was;
					m_grid.direction = !vertical ? 'h' : hasValue ? 'v' : 'x';
					if(m_grid.direction == 'h')
					{
						m_grid.to = !m_grid.was;
						set(m_grid.step, m_grid.to);
					}
					if(m_grid.direction == 'v')
					{
						if(_line < 0)
							m_grid.start = static_cast<float>(pattern().step(m_track, m_grid.step).velocity) / 127;
						else
						{
							auto* rp = dynamic_cast<juce::RangedAudioParameter*>(m_processor.getParameters()[_line]);
							if(const auto* lock = lockAt(pattern(), m_track, m_grid.step, _line); lock && rp)
								m_grid.start = rp->convertTo0to1(lock->value);
						}
					}
				}
				if(m_grid.direction == 'h')
				{
					const int q = stepAt(m_press.last.x);
					for(int j = std::min(q, m_grid.last); j <= std::max(q, m_grid.last); ++j)
						set(j, m_grid.to);
					m_grid.last = q;
				}
				else if(m_grid.direction == 'v')
				{
					// 1/32 of the range per mm
					const float v = juce::jlimit(0.0f, 1.0f, m_grid.start - m_grid.travel.y / 32);
					const int n = m_grid.step;
					if(_line < 0)
						editPattern([&](Pattern& _p) { _p.step(m_track, n).velocity =
							static_cast<uint8_t>(std::max(1, juce::roundToInt(v * 127))); });
					else if(auto* rp = dynamic_cast<juce::RangedAudioParameter*>(m_processor.getParameters()[_line]))
					{
						const float plain = rp->convertFrom0to1(v);
						editPattern([&](Pattern& _p) { _p.setLock(m_track, n, static_cast<uint16_t>(_line), plain); });
					}
				}
			};
			h.click = [this, set, _line]
			{
				m_seqStep = m_grid.step;
				if(_line < 0 && m_seqMode == 0)
					toggleTrig(m_track, m_grid.step);
				else if(_line < 0)
					set(m_grid.step, !m_grid.was);
				else if(!m_grid.was)
					set(m_grid.step, true);
			};
			// A double-click toggles once: the trig row leaves the first click's; a lock cell takes the Lock off
			h.doubleClick = [this, stepAt, set, _line]
			{
				if(_line >= 0)
					set(stepAt(m_press.down.x), false);
			};
			addHit(std::move(h));
		};
		grid({SG, 47, 16 * SC, 22}, -1);
		for(int i = 0; i < 16; ++i)
		{
			Hit n;
			n.r = {SG + static_cast<float>(i) * SC, 70, SC - 0.4f, 6};
			n.click = [this, o0, i] { m_seqStep = o0 + i; };
			addHit(std::move(n));
		}

		// LOCKS (u.html, variant 2): + LOCK opens the menu of the Track's parameters over the page; five lanes a page,
		// LANES turns it past five; with none, three empty lanes open the menu too. A lane's X takes it off, asking again
		// first where it holds Locks
		{
			Hit add;
			add.r = addLockBox;
			add.click = [this] { m_lockMenu = !m_lockMenu; };
			addHit(std::move(add));
			const auto& lines = m_lines;
			const int count = static_cast<int>(lines.size());
			m_lockScroll = count > Lanes ? juce::jlimit(0, (count - 1) / Lanes * Lanes, m_lockScroll / Lanes * Lanes) : 0;
			if(count > Lanes)
			{
				Hit page;
				page.r = lanesBox;
				page.click = [this, count] { m_lockScroll = m_lockScroll + Lanes < count ? m_lockScroll + Lanes : 0; };
				addHit(std::move(page));
			}
			for(int r = 0; lines.empty() && r < 3; ++r)
			{
				Hit empty;
				empty.r = laneBox(r);
				empty.click = [this] { m_lockMenu = true; };
				addHit(std::move(empty));
			}
			for(int r = 0; r < Lanes && m_lockScroll + r < count; ++r)
			{
				const int line = lines[static_cast<size_t>(m_lockScroll + r)];
				grid(laneBox(r), line);
				Hit x;
				x.r = laneDeleteBox(r);
				// An X's press keeps the line armed (another X's click then arms its own)
				x.press = [this] { m_armedLine = m_press.armed; };
				x.click = [this, line]
				{
					const bool has = lockCount(pattern(), m_track, line) > 0;
					if(has && m_armedLine != line)
					{
						m_armedLine = line;
						return;
					}
					editPattern([&](Pattern& _p)
					{
						for(int s = 0; s < SeqSteps; ++s)
							_p.removeLock(m_track, s, static_cast<uint16_t>(line));
					});
					auto& added = m_lockLines[static_cast<size_t>(m_track)];
					added.erase(std::remove(added.begin(), added.end(), line), added.end());
					m_armedLine = -1;
				};
				addHit(std::move(x));
			}
		}

		// STEP: the step chosen's fields
		{
			const auto stepField = [this](const int _i, const float _mm, std::function<void(Step&, int)> _steps,
				std::function<void()> _click)
			{
				Hit h;
				h.r = {SG + static_cast<float>(_i) * 2 * SC - 1, 221.5f, 2 * SC - 2, 13};
				h.stepMm = _mm;
				h.steps = [this, _steps](const int _n)
				{
					editPattern([&](Pattern& _p) { _steps(_p.step(m_track, m_seqStep), _n); });
				};
				h.click = std::move(_click);
				h.cursor = juce::MouseCursor::LeftRightResizeCursor;
				addHit(std::move(h));
			};
			// A field of a list: a drag steps it, a click opens the list (t-ui-spec, the step's fields)
			const auto listField = [this, stepField](const int _i, const StepList& _list,
				std::function<int(const Step&)> _get, std::function<void(Step&, int)> _put)
			{
				stepField(_i, 4, [&_list, _get, _put](Step& _s, const int _n) { _put(_s, _list.stepped(_get(_s), _n)); },
					[this, _i, &_list, _get, _put]
					{
						openSelector(SG + static_cast<float>(_i) * 2 * SC - 1, 205, _list.names,
							[this, &_list, _get](const int _k) { return _list.indexOf(_get(pattern().step(m_track, m_seqStep))) == _k; },
							[this, &_list, _put](const int _k) { editPattern([&](Pattern& _p) {
								_put(_p.step(m_track, m_seqStep), _list.values[static_cast<size_t>(_k)]); }); });
					});
			};
			listField(0, noteList(), [](const Step& _s) { return static_cast<int>(_s.note); },
				[](Step& _s, const int _v) { _s.note = static_cast<uint8_t>(_v); });
			listField(1, gateList(), [](const Step& _s) { return static_cast<int>(_s.gate); },
				[](Step& _s, const int _v) { _s.gate = static_cast<uint16_t>(_v); });
			stepField(2, 4, [](Step& _s, const int _n)
			{
				const auto& list = conditionList();
				_s.condition = list.conditions[static_cast<size_t>(juce::jlimit(0,
					static_cast<int>(list.conditions.size()) - 1, list.indexOf(_s.condition) + _n))];
			}, [this]
			{
				openSelector(SG + 4 * SC - 1, 205, conditionList().names,
					[this](const int _i) { return conditionList().indexOf(pattern().step(m_track, m_seqStep).condition) == _i; },
					[this](const int _i) { editPattern([&](Pattern& _p) {
						_p.step(m_track, m_seqStep).condition = conditionList().conditions[static_cast<size_t>(_i)]; }); });
			});
			listField(3, microList(), [](const Step& _s) { return static_cast<int>(_s.timing); },
				[](Step& _s, const int _v) { _s.timing = static_cast<int8_t>(_v); });
			listField(4, ratchetList(), [](const Step& _s) { return static_cast<int>(_s.ratchets); },
				[](Step& _s, const int _v) { _s.ratchets = static_cast<uint8_t>(_v); });
			listField(5, speedList(), [](const Step& _s) { return static_cast<int>(_s.ratchetRate); },
				[](Step& _s, const int _v) { _s.ratchetRate = static_cast<uint8_t>(_v); });
			stepField(6, 1.2f, [](Step& _s, const int _n)
			{
				_s.ratchetVelocity = static_cast<int8_t>(juce::jlimit(-64, 64, _s.ratchetVelocity + _n));
			}, {});

			// COPY and PASTE the step chosen, its Locks with it
			Hit copy;
			copy.r = {SG + 14 * SC, 222.5f, SC - 1, 9.6f};
			copy.click = [this]
			{
				const auto& p = pattern();
				m_stepClipboard = p.step(m_track, m_seqStep);
				const auto* l = p.locks(m_track, m_seqStep);
				m_stepLocksClipboard.assign(l, l + (l ? m_stepClipboard.lockCount : 0));
				m_stepCopied = true;
				toast("Step " + juce::String(m_seqStep + 1) + " copied");
			};
			addHit(std::move(copy));
			Hit paste;
			paste.r = {SG + 15 * SC, 222.5f, SC - 1, 9.6f};
			paste.click = [this]
			{
				if(!m_stepCopied)
					return;
				editPattern([&](Pattern& _p)
				{
					_p.setTrig(m_track, m_seqStep, false);
					Step s = m_stepClipboard;
					s.lockCount = 0;
					s.lockFirst = 0;
					_p.step(m_track, m_seqStep) = s;
					for(const auto& l : m_stepLocksClipboard)
						_p.setLock(m_track, m_seqStep, l.parameter, l.value);
				});
			};
			addHit(std::move(paste));
		}
		watch({ScreenX, 22, ScreenW, ScreenH - 10}, [this]
		{
			const auto& p = pattern();
			uint64_t h = mix(static_cast<uint64_t>(p.masterLength), static_cast<uint64_t>(p.swing));
			h = mix(h, static_cast<uint64_t>(p.trackLength[static_cast<size_t>(m_track)]));
			h = mix(h, static_cast<uint64_t>(p.trackScale[static_cast<size_t>(m_track)]));
			for(int s = 0; s < SeqSteps; ++s)
			{
				const auto& st = p.step(m_track, s);
				h = mix(h, static_cast<uint64_t>((st.trig ? 1 : 0) | (st.accent ? 2 : 0) | (st.slide ? 4 : 0)) | st.velocity << 3
					| static_cast<uint64_t>(st.lockCount) << 11);
				// A step's condition decides what its lanes draw
				h = mix(h, static_cast<uint64_t>(st.condition.kind) | static_cast<uint64_t>(st.condition.a) << 8
					| static_cast<uint64_t>(st.condition.b) << 16);
				const auto* l = p.locks(m_track, s);
				for(int i = 0; l && i < st.lockCount; ++i)
					h = mixFloat(mix(h, l[i].parameter), l[i].value);
				if(s == m_seqStep)
					h = mix(mix(mix(mix(mix(mix(h, static_cast<uint64_t>(st.timing + 64)), st.ratchets), st.ratchetRate),
						static_cast<uint64_t>(st.ratchetVelocity + 128)), st.note), st.gate)
						^ conditionList().indexOf(st.condition);
			}
			h = mix(h, (m_processor.live() ? 1u : 0u) | (m_stepCopied ? 2u : 0u));
			return mix(h, static_cast<uint64_t>(m_processor.playingStep(m_track) + 1));
		});

		// + LOCK's menu, over all the rest: an entry without a lane adds one, at its page; a press beside the entries
		// shuts it
		if(m_lockMenu)
		{
			Hit shut;
			shut.r = {ScreenX, ScreenY, ScreenW, ScreenH};
			shut.click = [this] { m_lockMenu = false; };
			shut.cursor = juce::MouseCursor::NormalCursor;
			addHit(std::move(shut));
			m_menuEntries = lockMenuEntries();
			for(const auto& e : m_menuEntries)
			{
				if(e.taken)
					continue;
				Hit h;
				h.r = e.r;
				h.click = [this, parameter = e.parameter]
				{
					m_lockLines[static_cast<size_t>(m_track)].push_back(parameter);
					m_lockMenu = false;
					m_lockScroll = (static_cast<int>(lockLineList().size()) - 1) / Lanes * Lanes;
				};
				addHit(std::move(h));
			}
		}
	}

	std::vector<Editor::MenuEntry> Editor::lockMenuEntries() const
	{
		const int used = machineParameters(trackMachine(m_track));
		const auto& lines = m_lines;
		std::vector<MenuEntry> entries;
		std::array<int, 10> rows{};
		const float cw = (ScreenW - 8) / 10;
		for(const auto& id : m_processor.trackIds(m_track))
		{
			if(id.endsWith("_machine") || id.endsWith("_output") || id.endsWith("_chokes"))
				continue;
			auto* p = m_processor.parameter(id);
			const int index = p->getParameterIndex();
			if(const auto& target = m_processor.lockTarget(index); target.kind == Processor::LockKind::Machine
				&& target.a >= used)
				continue;
			const auto section = laneSection(id);
			// VOICE runs on into the column after its own
			const int column = section.column == 0 && rows[0] >= MenuRows ? 1 : section.column;
			const int row = rows[static_cast<size_t>(column)]++;
			entries.push_back({index, {ScreenX + 4 + static_cast<float>(column) * cw, MenuY + 10 + static_cast<float>(row)
				* MenuRow, cw - 2, MenuRow - 1}, section.hue, column == 1 ? juce::String() : juce::String(section.name),
				lockName(*p), std::find(lines.begin(), lines.end(), index) != lines.end()});
		}
		return entries;
	}

	juce::Rectangle<int> Editor::seqButtonArea(const SeqButton _button) const
	{
		return toPx(_button == SeqButton::AddLock ? addLockBox : lanesBox);
	}

	juce::Rectangle<int> Editor::laneArea(const int _row) const
	{
		return toPx(laneBox(_row));
	}

	juce::Rectangle<int> Editor::laneDeleteArea(const int _row) const
	{
		return toPx(laneDeleteBox(_row));
	}

	juce::Rectangle<int> Editor::lockMenuArea(const int _parameter) const
	{
		if(!m_lockMenu)
			return {};
		for(const auto& e : m_menuEntries)
			if(e.parameter == _parameter && !e.taken)
				return toPx(e.r);
		return {};
	}

	void Editor::drawSeq(Painter& _p)
	{
		const auto& p = pattern();
		const int o0 = m_stepPage * 16;
		const int length = p.trackLength[static_cast<size_t>(m_track)];
		const int playing = m_processor.playingStep(m_track);
		// The pattern's name in the title; at the right, CHAIN n/m while a chain plays, else nothing
		{
			bool running = false;
			for(int t = 0; t < 16; ++t)
				running = running || m_processor.playingStep(t) >= 0;
			const int chain = m_processor.sequencer().chainLength(), slot = m_processor.chainSlot();
			drawHeader(_p, "SEQ " + patternName(m_processor.currentPattern()), {}, running && chain > 1 && slot >= 0
				? "CHAIN " + juce::String(slot + 1) + "/" + juce::String(chain) : juce::String(" "));
		}
		drawSegments(_p, SG, ScreenY + 2, SC, {"DETAIL", "OVERVIEW", "PATTERNS"}, m_seqTab);
		if(m_seqTab == 1)
		{
			drawOverview(_p);
			return;
		}
		if(m_seqTab == 2)
		{
			drawPatterns(_p);
			return;
		}
		_p.text(SG + 3 * SC, ScreenY + 7.4f, juce::String(m_track + 1) + "  " + machineName(m_track), 2.8f, colour::mid);

		const auto field = [&](const float _x, const juce::String& _name, const juce::String& _value)
		{
			_p.text(_x, 26.3f, _name, 3.2f, colour::mid);
			_p.text(_x, 33, _value, 4, colour::ink, 600);
		};
		field(SG, "TRACK LEN", juce::String(length));
		field(SG + SC, "SCALE", scaleNames[static_cast<int>(p.trackScale[static_cast<size_t>(m_track)])]);
		field(SG + 2 * SC, "SWING", juce::String(p.swing) + " %");
		field(SG + 5 * SC, "PAT LEN", juce::String(p.masterLength));
		const char* actions[]{"COPY", "PASTE", "CLEAR", "SHIFT"};
		for(int i = 0; i < 4; ++i)
			drawButton(_p, SG + static_cast<float>(12 + i) * SC, 24, SC - 1, 9.6f, actions[i], false,
				i != 1 || m_trackClipboard != nullptr);
		drawButton(_p, SG + 11 * SC, 24, SC - 1, 9.6f, "LIVE", m_processor.live());

		const auto rule = [&](const float _y, const juce::String& _name, const bool _focus)
		{
			_p.rect(16, _y, 458, _focus ? 0.8f : 0.4f, _focus ? hue::t.c[0] : hue::t.c[1]);
			_p.text(16, _y + 6.2f, _name, 3.2f, hue::t.c[4], 600);
		};
		// TRIGS, the steps shown of the Track's length beside the title; the mode's name, and under it what the marks
		// drawn in the steps say
		rule(38, "TRIGS", false);
		_p.text(31, 44.2f, juce::String(o0 + 1) + juce::String::fromUTF8("\xe2\x80\x93") + juce::String(o0 + 16) + " / "
			+ juce::String(length), 2.8f, colour::mid);
		const char* modes[]{"VEL", "ACCENT", "SLIDE"};
		for(int i = 0; i < 3; ++i)
			drawButton(_p, SG + static_cast<float>(13 + i) * SC, 39, SC - 1, 7, modes[i], m_seqMode == i);
		_p.text(16, 54, modes[m_seqMode], 3.2f, hue::t.c[4], 600);
		const float yb = 47, bh = 22;
		{
			const float l1 = yb + 14.5f, l2 = yb + 19.5f;
			_p.text(16, l1, "1:2", 2.8f, hue::y.c[4], 600);
			_p.text(22, l1, "COND", 2.8f, colour::mid);
			for(int k = 0; k < 3; ++k)
				_p.rect(34 + static_cast<float>(k) * 2.1f, l1 - 2.2f, 1.3f, 1.8f, hue::t.c[4], 0.3f);
			_p.text(41, l1, "RAT", 2.8f, colour::mid);
			_p.rect(16.6f, l2 - 2.2f, 1.2f, 1.8f, hue::k.c[4], 0.3f);
			_p.text(19, l2, "MICRO", 2.8f, colour::mid);
			_p.line(34, l2 - 1.2f, 39, l2 - 1.2f, hue::t.c[4], 0.5f);
			_p.text(41, l2, "SLIDE", 2.8f, colour::mid);
		}
		for(int i = 0; i < 16; ++i)
		{
			const int n = o0 + i;
			const auto& st = p.step(m_track, n);
			const float x = SG + static_cast<float>(i) * SC, w = SC - 1.2f;
			const float val = m_seqMode == 0 ? (st.trig ? static_cast<float>(st.velocity) / 127 : 0)
				: m_seqMode == 1 ? (st.accent ? 1.0f : 0.0f) : (st.slide ? 1.0f : 0.0f);
			_p.rect(x, yb, w, bh, beat(i), 0.5f);
			if(val > 0.01f)
			{
				_p.rect(x, yb + bh * (1 - val), w, bh * val, hue::t.c[3], 0.5f);
				_p.rect(x, yb + bh * (1 - val), w, 0.6f, hue::t.c[0]);
			}
			// The trig's marks: its condition, its ratchet's Hits, its slide to the next step, its micro timing
			if(st.trig)
			{
				if(const int c = conditionList().indexOf(st.condition))
					_p.text(x + 1.4f, yb + 3.6f, fitted(_p, conditionList().names[c], 2.4f, w - 8, 600), 2.4f, hue::y.c[4], 600);
				for(int k = 0; st.ratchets > 1 && k < std::min<int>(st.ratchets, 4); ++k)
					_p.rect(x + w - 2.2f - static_cast<float>(k) * 2.1f, yb + 1.5f, 1.3f, 1.8f, hue::t.c[4], 0.3f);
				if(st.slide && i < 15)
					_p.line(x + w - 1, yb + bh * 0.55f, x + SC + 1, yb + bh * 0.55f, hue::t.c[4], 0.5f);
				if(st.timing != 0)
				{
					const float mx = juce::jlimit(x + 1.5f, x + w - 1.5f, x + w / 2 + static_cast<float>(st.timing)
						/ static_cast<float>(TimingUnits) * w * 0.4f);
					_p.rect(mx - 0.6f, yb + bh - 2.2f, 1.2f, 1.8f, hue::k.c[4], 0.3f);
				}
			}
			if(n == playing)
				_p.rect(x, yb, w, bh, juce::Colour(0x33E9DCC2), 0.5f);
			const bool sel = n == m_seqStep;
			_p.text(x + w / 2, yb + bh + 4.4f, juce::String(n + 1), 2.8f, sel ? colour::ink : i % 4 == 0
				? colour::mid : juce::Colour(0xff5E5E5A), sel || i % 4 == 0 ? 600 : 500, Align::Centre);
		}

		// LOCKS: + LOCK, LANES past five; a lane each parameter locked or added, five a page
		rule(77, "LOCKS", false);
		drawButton(_p, addLockBox.getX(), addLockBox.getY(), addLockBox.getWidth(), addLockBox.getHeight(), "+ LOCK",
			m_lockMenu);
		const auto& lines = m_lines;
		const int count = static_cast<int>(lines.size());
		if(count > Lanes)
			drawButton(_p, lanesBox.getX(), lanesBox.getY(), lanesBox.getWidth(), lanesBox.getHeight(), "LANES "
				+ juce::String(m_lockScroll + 1) + juce::String::fromUTF8("\xe2\x80\x93")
				+ juce::String(std::min(count, m_lockScroll + Lanes)) + " / " + juce::String(count), false);
		const float right = SG + 16 * SC - 1.2f;
		// A lane's frame: the beats in bands of four steps, a fine line between steps, its outline
		const auto laneFrame = [&](const juce::Rectangle<float>& _r)
		{
			for(int g = 0; g < 4; ++g)
				_p.rect(SG + static_cast<float>(g) * 4 * SC, _r.getY(), 4 * SC - (g < 3 ? 0 : 1.2f), _r.getHeight(),
					g % 2 ? juce::Colour(0xff131517) : beatA);
			for(int q = 1; q < 16; ++q)
				if(q % 4)
					_p.rect(SG + static_cast<float>(q) * SC - 0.6f, _r.getY() + 0.6f, 0.15f, _r.getHeight() - 1.2f,
						juce::Colour(0x12ECEBE4));
			_p.frame(SG, _r.getY(), right - SG, _r.getHeight(), juce::Colour(0xff34363A), 0.6f, 0.35f);
		};
		// None: three empty lanes, each opening the menu
		for(int r = 0; lines.empty() && r < 3; ++r)
		{
			const auto b = laneBox(r);
			_p.text(16, b.getY() + 6.2f, "EMPTY LANE", 3.2f, colour::dim, 600);
			dashed(_p, SG, right, b.getY(), juce::Colour(0xff3A3B3D), 0.35f);
			dashed(_p, SG, right, b.getBottom(), juce::Colour(0xff3A3B3D), 0.35f);
			_p.rect(SG, b.getY(), 0.35f, b.getHeight(), juce::Colour(0xff3A3B3D));
			_p.rect(right - 0.35f, b.getY(), 0.35f, b.getHeight(), juce::Colour(0xff3A3B3D));
			_p.text((SG + right) / 2, b.getCentreY() + 1.2f, "+ LOCK", 2.8f, colour::mid, 600, Align::Centre);
		}
		const auto& params = m_processor.getParameters();
		for(int r = 0; r < Lanes && m_lockScroll + r < count; ++r)
		{
			const int line = lines[static_cast<size_t>(m_lockScroll + r)];
			const auto b = laneBox(r);
			const float yy = b.getY(), hh = b.getHeight();
			auto* rp = dynamic_cast<juce::RangedAudioParameter*>(params[line]);
			if(!rp)
				continue;
			const auto section = laneSection(rp->getParameterID());
			const Hue& h = *section.hue;
			const bool list = dynamic_cast<juce::AudioParameterChoice*>(rp) || dynamic_cast<juce::AudioParameterBool*>(rp);
			const float base = rp->getValue();
			const auto valueText = [&](const float _v)
			{
				Cell c;
				c.parameter = rp;
				return c.valueText(_v);
			};
			// What it plays on each step (laneSteps), and how many Locks it holds
			int locks = 0;
			const auto steps = laneSteps(line, &locks);
			// The names' column: the name, its block or the X's question, its base; the X, framed
			const bool armed = m_armedLine == line;
			const float nameW = SG - 16 - 9;
			_p.text(16, yy + 6.2f, fitted(_p, lockName(*rp), 3.2f, nameW, 600), 3.2f, h.c[4], 600);
			_p.text(16, yy + 11.4f, armed ? fitted(_p, "DELETE " + juce::String(locks) + " LOCKS?", 2.8f, SG - 18, 600)
				: juce::String(section.name), 2.8f, armed ? colour::lock : colour::dim, armed ? 600 : 500);
			_p.text(16, yy + 16.8f, fitted(_p, "BASE " + valueText(base), 2.8f, SG - 18), 2.8f, colour::base);
			const auto x = laneDeleteBox(r);
			if(armed)
				_p.rect(x.getX() + 0.5f, x.getY() + 0.5f, 6, 6.6f, juce::Colour(0xff5A2622), 0.8f);
			_p.frame(x.getX() + 0.5f, x.getY() + 0.5f, 6, 6.6f, armed ? colour::lock : colour::panelLine, 0.8f, 0.3f);
			_p.text(x.getX() + 3.5f, x.getY() + 5.3f, juce::String::fromUTF8("\xc3\x97"), 3.2f, armed ? colour::ink
				: colour::mid, 600, Align::Centre);

			laneFrame(b);
			const auto at = [&](const int _q) { return steps[static_cast<size_t>(o0 + _q)]; };
			if(list)
			{
				// A list's Lock: a pill with its value, black ringed in the hue where the step's condition may not play it;
				// where a run of steps without one starts, what they play, the base in grey, a Lock held in the lane's hue
				const auto pill = [&](const int _q) { return at(_q).locked || at(_q).may; };
				for(int q = 0; q < 16; ++q)
				{
					const float sx = SG + static_cast<float>(q) * SC, w = SC - 1.2f;
					if(pill(q))
					{
						const bool may = !at(q).locked;
						_p.rect(sx + 1.2f, yy + 4, w - 2.4f, hh - 8, may ? juce::Colours::black : h.c[3], 1.2f);
						_p.frame(sx + 1.2f, yy + 4, w - 2.4f, hh - 8, h.c[0], 1.2f, 0.4f);
						_p.text(sx + w / 2, yy + hh / 2 + 1.4f, fitted(_p, valueText(may ? at(q).would : at(q).from), 2.8f,
							w - 3, 600), 2.8f, h.c[4], 600, Align::Centre);
					}
					else if(q == 0 || pill(q - 1) || at(q - 1).from != at(q).from)
					{
						int run = 1;
						while(q + run < 16 && !pill(q + run) && at(q + run).from == at(q).from)
							++run;
						const bool held = at(q).from != base;
						_p.text(sx + 1.6f, yy + hh / 2 + 1.2f, fitted(_p, valueText(at(q).from), 2.8f,
							static_cast<float>(run) * SC - 3), 2.8f, held ? h.c[4] : colour::base, held ? 600 : 500);
					}
				}
				continue;
			}
			// A value: held steps, ramps where a slide glides, filled from the base line
			const auto Y = [&](const float _v) { return yy + 0.6f + (hh - 1.2f) * (1 - _v); };
			const float baseY = Y(base);
			std::vector<juce::Point<float>> curve;
			for(int q = 0; q < 16; ++q)
			{
				const float x0 = SG + static_cast<float>(q) * SC, x1 = q < 15 ? x0 + SC : right;
				curve.push_back({x0, Y(at(q).from)});
				curve.push_back({x1, Y(at(q).to)});
			}
			juce::Path fill;
			fill.startNewSubPath(SG, baseY);
			for(const auto& pt : curve)
				fill.lineTo(pt);
			fill.lineTo(right, baseY);
			fill.closeSubPath();
			_p.g.setColour(h.c[2]);
			_p.g.fillPath(fill);
			// Its line with plain joints: Painter::poly's curved ones cost 0.4 ms a lane
			juce::Path stroke;
			stroke.startNewSubPath(curve.front());
			for(size_t i = 1; i < curve.size(); ++i)
				stroke.lineTo(curve[i]);
			_p.g.setColour(h.c[0]);
			_p.g.strokePath(stroke, juce::PathStrokeType(0.65f));
			dashed(_p, SG, right, baseY, juce::Colour(0x8c8E8C88), 0.35f);
			// A Lock's dot; black ringed in the hue where the step's condition may not play it, at what it would lock
			for(int q = 0; q < 16; ++q)
				if(at(q).locked)
					_p.dot(SG + static_cast<float>(q) * SC + (SC - 1.2f) / 2, Y(at(q).from), 1.4f, h.c[4], h.c[4], 0.1f);
				else if(at(q).may)
					_p.dot(SG + static_cast<float>(q) * SC + (SC - 1.2f) / 2, Y(at(q).would), 1.4f, juce::Colours::black,
						h.c[4], 0.5f);
		}

		// The step chosen: a frame on its trig, a column through the lanes; what lies beyond the Track's length
		const int shown = std::min(Lanes, count - m_lockScroll);
		if(m_seqStep >= o0 && m_seqStep < o0 + 16)
		{
			const float sx = SG + static_cast<float>(m_seqStep - o0) * SC;
			_p.rect(sx, yb, SC - 1.2f, bh, juce::Colour(0x12E9DCC2), 0.5f);
			_p.frame(sx + 0.15f, yb + 0.15f, SC - 1.5f, bh - 0.3f, hue::t.c[0], 0.5f, 0.3f);
			if(shown > 0)
			{
				const float hh = static_cast<float>(shown - 1) * LanePitch + LaneH;
				_p.rect(sx, LaneY, SC - 1.2f, hh, juce::Colour(0x12E9DCC2), 0.5f);
				_p.frame(sx + 0.1f, LaneY + 0.1f, SC - 1.4f, hh - 0.2f, juce::Colour(0x52E9DCC2), 0.5f, 0.25f);
			}
		}
		if(length < o0 + 16)
		{
			const float ox = SG + static_cast<float>(std::max(0, length - o0)) * SC, ow = SG + 16 * SC - 1.2f - ox;
			_p.rect(ox, yb, ow, bh, juce::Colour(0x9e0B0C0D));
			if(shown > 0)
				_p.rect(ox, LaneY, ow, static_cast<float>(shown) * LanePitch - 1.6f, juce::Colour(0x9e0B0C0D));
		}

		// STEP
		rule(213, "STEP " + juce::String(m_seqStep + 1), true);
		const auto& st = p.step(m_track, m_seqStep);
		const juce::String values[]{noteList().names[noteList().indexOf(st.note)],
			gateList().names[gateList().indexOf(st.gate)], conditionList().names[conditionList().indexOf(st.condition)],
			microList().names[microList().indexOf(st.timing)], juce::String(st.ratchets),
			speedList().names[speedList().indexOf(st.ratchetRate)],
			st.ratchetVelocity == 0 ? juce::String("0") : (st.ratchetVelocity > 0 ? "+" : juce::String::fromUTF8("\xe2\x88\x92"))
				+ juce::String(std::abs(st.ratchetVelocity))};
		const char* names[]{"NOTE", "DURATION", "CONDITION", "MICRO", "RATCHET", "SPEED", "VEL CURVE"};
		for(int i = 0; i < 7; ++i)
		{
			const float xx = SG + static_cast<float>(i) * 2 * SC;
			_p.text(xx, 225.3f, names[i], 3.2f, colour::mid);
			_p.text(xx, 232, values[i], 4, colour::ink, 600);
		}
		drawButton(_p, SG + 14 * SC, 222.5f, SC - 1, 9.6f, "COPY", false);
		drawButton(_p, SG + 15 * SC, 222.5f, SC - 1, 9.6f, "PASTE", false, m_stepCopied);

		// + LOCK's menu, last, over the page: a column each block, every parameter that takes a Lock; one with a lane
		// already greyed
		if(m_lockMenu)
		{
			const auto& entries = m_menuEntries;
			float bottom = MenuY;
			for(const auto& e : entries)
				bottom = std::max(bottom, e.r.getBottom());
			_p.rect(ScreenX + 3, MenuY, ScreenW - 6, bottom + 2 - MenuY, colour::panel, 1);
			_p.frame(ScreenX + 3, MenuY, ScreenW - 6, bottom + 2 - MenuY, colour::panelLine, 1, 0.3f);
			for(const auto& e : entries)
			{
				if(e.r.getY() < MenuY + 10.5f && e.section.isNotEmpty())
					_p.text(e.r.getX(), MenuY + 6.5f, e.section, 2.8f, e.hue->c[4], 600);
				_p.rect(e.r.getX(), e.r.getY(), e.r.getWidth(), e.r.getHeight(), e.taken ? juce::Colour(0xff1D1F21)
					: colour::cellBack, 0.6f);
				_p.text(e.r.getX() + 2, e.r.getY() + 5.2f, fitted(_p, e.name, 2.8f, e.r.getWidth() - 4), 2.8f, e.taken
					? colour::dim : e.hue->c[4]);
			}
		}
	}

	// ---- Seq, Overview: every Track, 32 square steps a page, each row on its own steps ----

	namespace
	{
		constexpr float OvRow = 12.65f, OvW = 32 * OvRow, OvX = 474.7f - OvW, OvY = 36;

		// Steps of _track in the pattern's length, counted at its own scale
		int rowSteps(const Pattern& _p, const int _track)
		{
			return static_cast<int>(std::ceil(_p.masterLength * 0.25 / stepQuarters(_p.trackScale[static_cast<size_t>(_track)])
				- 1e-9));
		}

		int overviewPages(const Pattern& _p)
		{
			int most = 1;
			for(int t = 0; t < SeqTracks; ++t)
				most = std::max(most, rowSteps(_p, t));
			return (most + 31) / 32;
		}

		bool patternUsed(const Pattern& _p)
		{
			for(int t = 0; t < SeqTracks; ++t)
				for(int s = 0; s < SeqSteps; ++s)
					if(_p.step(t, s).trig || _p.step(t, s).lockCount)
						return true;
			return false;
		}

		std::vector<ChainEntry> chainOf(const Sequencer& _s)
		{
			std::vector<ChainEntry> c;
			for(int i = 0; i < _s.chainLength(); ++i)
				c.push_back(_s.chainEntry(i));
			return c;
		}
	}

	void Editor::buildOverview()
	{
		const auto& p = pattern();
		const int pages = overviewPages(p);
		m_overviewPage = juce::jlimit(0, pages - 1, m_overviewPage);
		const int w0 = m_overviewPage * 32;
		// LENGTH: a drag by the step, a click opens the lengths
		{
			Hit len;
			len.r = {SG + 13 * SC - 1, 23.4f, 2 * SC - 2, 8.4f};
			len.stepMm = 1.2f;
			len.steps = [this](const int _n)
			{
				editPattern([&](Pattern& _q) { _q.masterLength = juce::jlimit(1, SeqMaxLength, _q.masterLength + _n); });
			};
			len.click = [this]
			{
				openSelector(SG + 13 * SC - 1, 15, lengthChoices,
					[this](const int _i) { return pattern().masterLength == lengthChoices[_i].getIntValue(); },
					[this](const int _i) { editPattern([&](Pattern& _q) { _q.masterLength = lengthChoices[_i].getIntValue(); }); });
			};
			len.cursor = juce::MouseCursor::LeftRightResizeCursor;
			addHit(std::move(len));
		}
		Hit fill;
		fill.r = {SG + 15 * SC, 24, SC - 1, 7};
		fill.click = [this]
		{
			const bool on = !m_processor.sequencer().fill();
			m_processor.editSequencer([on](Sequencer& _s) { _s.setFill(on); });
		};
		addHit(std::move(fill));
		if(pages > 1)
		{
			const int w1 = juce::jlimit(0, std::max(0, pages - 13), m_overviewPage - 6);
			for(int w = w1; w < std::min(pages, w1 + 13); ++w)
			{
				Hit page;
				page.r = {SG + static_cast<float>(w - w1) * SC, 24, SC - 1, 7};
				page.click = [this, w] { m_overviewPage = w; m_follow = false; };
				addHit(std::move(page));
			}
		}

		for(int i = 0; i < SeqTracks; ++i)
		{
			const float y = OvY + static_cast<float>(i) * OvRow;
			Hit name;
			name.r = {ScreenX + 2, y, 38, OvRow - 0.4f};
			name.click = [this, i] { showTrack(i); };
			addHit(std::move(name));
			Hit mute;
			mute.r = {ScreenX + 41, y, 6, OvRow - 0.4f};
			mute.click = [this, i]
			{
				const bool on = !m_processor.sequencer().mute(i);
				m_processor.editSequencer([i, on](Sequencer& _s) { _s.setMute(i, on); });
			};
			addHit(std::move(mute));
			Hit solo;
			solo.r = {ScreenX + 48, y, 6, OvRow - 0.4f};
			solo.click = [this, i]
			{
				const bool on = !m_processor.sequencer().solo(i);
				m_processor.editSequencer([i, on](Sequencer& _s) { _s.setSolo(i, on); });
			};
			addHit(std::move(solo));

			// The row: the same gestures as Detail's trigs, on the step its copy repeats
			const auto cellAt = [w0](const float _x) { return w0 + juce::jlimit(0, 31, static_cast<int>((_x - OvX) / OvRow)); };
			const auto aim = [this, i, w0, cellAt](const float _x)
			{
				const auto& q = pattern();
				const int len = q.trackLength[static_cast<size_t>(i)];
				const int c = cellAt(_x);
				if(q.step(i, c % len).trig)
					return c;
				// The trig nearest within 3 mm
				int best = c;
				float dist = 3;
				for(int a = w0; a < w0 + 32; ++a)
					if(q.step(i, a % len).trig)
						if(const float d = std::abs(OvX + (static_cast<float>(a - w0) + 0.5f) * OvRow - _x); d < dist)
						{
							dist = d;
							best = a;
						}
				return best;
			};
			Hit row;
			row.r = {OvX, y, OvW, OvRow - 0.4f};
			row.cursor = juce::MouseCursor::CrosshairCursor;
			row.press = [this, i, aim]
			{
				m_grid = {};
				m_grid.line = i;
				m_grid.step = m_grid.last = aim(m_press.down.x);
				const auto& q = pattern();
				const int n = m_grid.step % q.trackLength[static_cast<size_t>(i)];
				m_grid.was = q.step(i, n).trig;
				m_grid.start = static_cast<float>(q.step(i, n).velocity) / 127;
			};
			row.drag = [this, i, cellAt](const float _dx, const float _dy)
			{
				m_grid.travel += {_dx, _dy};
				const int len = pattern().trackLength[static_cast<size_t>(i)];
				const auto set = [this, i, len](const int _a, const bool _on)
				{
					editPattern([&](Pattern& _q)
					{
						if(_q.step(i, _a % len).trig != _on)
							_q.setTrig(i, _a % len, _on, 127);
					});
				};
				if(!m_grid.direction)
				{
					if(m_grid.travel.getDistanceFromOrigin() < 2)
						return;
					const bool vertical = std::abs(m_grid.travel.y) > std::abs(m_grid.travel.x);
					m_grid.direction = !vertical ? 'h' : m_grid.was ? 'v' : 'x';
					if(m_grid.direction == 'h')
					{
						m_grid.to = !m_grid.was;
						set(m_grid.step, m_grid.to);
					}
				}
				if(m_grid.direction == 'h')
				{
					const int q = cellAt(m_press.last.x);
					for(int a = std::min(q, m_grid.last); a <= std::max(q, m_grid.last); ++a)
						set(a, m_grid.to);
					m_grid.last = q;
				}
				else if(m_grid.direction == 'v')
				{
					const float v = juce::jlimit(0.0f, 1.0f, m_grid.start - m_grid.travel.y / 32);
					const int n = m_grid.step % len;
					editPattern([&](Pattern& _q) { _q.step(i, n).velocity = static_cast<uint8_t>(std::max(1, juce::roundToInt(v * 127))); });
				}
			};
			row.click = [this, i]
			{
				const auto& q = pattern();
				const int n = m_grid.step % q.trackLength[static_cast<size_t>(i)];
				// What a double-click puts back
				m_undoTrack = i;
				m_undoStep = n;
				m_undo = q.step(i, n);
				const auto* l = q.locks(i, n);
				m_undoLocks.assign(l, l + (l ? m_undo.lockCount : 0));
				toggleTrig(i, n);
			};
			// The second click does not toggle again: a trig the first took off comes back with its values and Locks,
			// and the step opens in Detail
			row.doubleClick = [this, i, aim]
			{
				const auto& q = pattern();
				const int n = aim(m_press.down.x) % q.trackLength[static_cast<size_t>(i)];
				if(m_undoTrack == i && m_undoStep == n && m_undo.trig && !q.step(i, n).trig)
					editPattern([&](Pattern& _q)
					{
						Step s = m_undo;
						s.lockCount = 0;
						s.lockFirst = 0;
						_q.step(i, n) = s;
						for(const auto& l : m_undoLocks)
							_q.setLock(i, n, l.parameter, l.value);
					});
				m_undoTrack = -1;
				m_track = i;
				m_processor.setShownTrack(i);
				m_seqStep = n;
				m_stepPage = n / 16;
				m_seqTab = 0;
			};
			addHit(std::move(row));
		}

		watch({ScreenX, 22, ScreenW, ScreenH - 10}, [this]
		{
			const auto& q = pattern();
			uint64_t h = mix(static_cast<uint64_t>(q.masterLength), m_processor.fillPlaying() ? 1 : 0);
			for(int t = 0; t < SeqTracks; ++t)
			{
				h = mix(mix(h, static_cast<uint64_t>(q.trackLength[static_cast<size_t>(t)])),
					static_cast<uint64_t>(q.trackScale[static_cast<size_t>(t)]));
				h = mix(h, static_cast<uint64_t>(m_processor.playingRowStep(t) + 1));
				h = mix(h, (m_processor.sequencer().mute(t) ? 1u : 0u) | (m_processor.sequencer().solo(t) ? 2u : 0u));
				for(int s = 0; s < SeqSteps; ++s)
				{
					const auto& st = q.step(t, s);
					h = mix(h, static_cast<uint64_t>((st.trig ? 1 : 0) | (st.accent ? 2 : 0)) | st.velocity << 2
						| static_cast<uint64_t>(st.lockCount ? 1 : 0) << 9);
				}
			}
			return h;
		});
	}

	void Editor::drawOverview(Painter& _p)
	{
		const auto& p = pattern();
		const auto& seq = m_processor.sequencer();
		const int pages = overviewPages(p);
		const int w0 = juce::jlimit(0, pages - 1, m_overviewPage) * 32;
		bool anySolo = false;
		for(int t = 0; t < SeqTracks; ++t)
			anySolo = anySolo || seq.solo(t);

		_p.rect(16, 23, 458, 0.4f, hue::t.c[1]);
		_p.text(16, 29.2f, "PATTERN " + patternName(m_processor.currentPattern()), 3.2f, hue::t.c[4], 600);
		const float lx = SG + 13 * SC;
		_p.text(lx, 28.8f, "LENGTH", 3.2f, colour::mid);
		_p.text(lx + 16, 28.8f, juce::String(p.masterLength), 4, colour::ink, 600);
		drawButton(_p, SG + 15 * SC, 24, SC - 1, 7, "FILL", m_processor.fillPlaying());
		if(pages > 1)
		{
			const int w1 = juce::jlimit(0, std::max(0, pages - 13), m_overviewPage - 6);
			for(int w = w1; w < std::min(pages, w1 + 13); ++w)
				drawButton(_p, SG + static_cast<float>(w - w1) * SC, 24, SC - 1, 7,
					juce::String(w * 32 + 1) + "-" + juce::String(w * 32 + 32), w * 32 == w0);
		}
		// The ruler: a number every 4 steps, brighter every 16
		for(int k = 0; k < 32; k += 4)
		{
			const int n = w0 + k;
			const bool bar = n % 16 == 0;
			_p.text(OvX + static_cast<float>(k) * OvRow + 0.2f, 35, juce::String(n + 1), 2.8f,
				bar ? colour::mid : juce::Colour(0xff7A7A75), bar ? 600 : 500);
			_p.rect(OvX + static_cast<float>(k) * OvRow, 35, bar ? 0.4f : 0.2f, 1, bar ? juce::Colour(0xff6E6E6A)
				: juce::Colour(0xff37383A));
		}

		for(int i = 0; i < SeqTracks; ++i)
		{
			const float y = OvY + static_cast<float>(i) * OvRow;
			const bool sel = i == m_track;
			const bool silent = seq.mute(i) || (anySolo && !seq.solo(i));
			if(sel)
			{
				_p.rect(ScreenX + 2, y, OvX - ScreenX - 2.7f, OvRow - 0.7f, juce::Colour(0xff1D1F21), 0.5f);
				_p.rect(ScreenX + 2, y + 0.6f, 0.7f, OvRow - 1.9f, hue::t.c[0], 0.3f);
			}
			_p.text(ScreenX + 6.6f, y + 7.2f, juce::String(i + 1), 2.8f, sel ? colour::mid : juce::Colour(0xff7A7A75), 500,
				Align::Right);
			_p.text(ScreenX + 8.4f, y + 7.2f, machineName(i).substring(0, 10), 3.2f,
				sel ? colour::ink : silent ? juce::Colour(0xff57564F) : colour::mid, sel ? 600 : 500);
			_p.text(ScreenX + 44, y + 7.2f, "M", 3, seq.mute(i) ? colour::lock : juce::Colour(0xff7A7A75), 600, Align::Centre);
			_p.text(ScreenX + 51, y + 7.2f, "S", 3, seq.solo(i) ? hue::t.c[4] : juce::Colour(0xff7A7A75), 600, Align::Centre);

			const int len = p.trackLength[static_cast<size_t>(i)];
			const int playing = m_processor.playingRowStep(i);
			const float alpha = silent ? 0.4f : 1.0f;
			for(int a = w0; a < w0 + 32; ++a)
			{
				const float x = OvX + static_cast<float>(a - w0) * OvRow;
				const auto& st = p.step(i, a % len);
				const bool group = (a / 4) % 2 == 0;
				// A copy of a shorter Track's steps, at 55 %
				const float k = alpha * (a >= len ? 0.55f : 1.0f);
				_p.rect(x, y, OvRow - 0.7f, OvRow - 0.7f, (sel ? (group ? juce::Colour(0xff2A2C2E) : juce::Colour(0xff222426))
					: group ? juce::Colour(0xff1B1D1F) : juce::Colour(0xff131517)).withMultipliedAlpha(k), 0.5f);
				if(st.trig)
					_p.rect(x, y, OvRow - 0.7f, OvRow - 0.7f, (st.accent ? juce::Colour(0xffF3EADA)
						: juce::Colour(0xffE9DCC2).withAlpha(0.2f + 0.55f * static_cast<float>(st.velocity) / 127))
						.withMultipliedAlpha(k), 0.5f);
				if(a == playing)
					_p.rect(x, y, OvRow - 0.7f, OvRow - 0.7f, st.trig ? juce::Colour(0x80FFFFFF) : juce::Colour(0x33E9DCC2), 0.5f);
				if(st.lockCount)
					_p.rect(x + 1, y + OvRow - 2.6f, OvRow - 2.7f, 0.7f, colour::lock.withMultipliedAlpha(k));
			}
			// The pattern's end on this row's steps, the grid darker past it
			const float ex = OvX + static_cast<float>(rowSteps(p, i) - w0) * OvRow;
			if(ex < OvX + OvW - 0.1f)
			{
				const float xe = std::max(ex, OvX);
				_p.rect(xe - 0.7f, y, OvX + OvW - xe + 0.7f, OvRow, juce::Colour(0xcc0B0C0D));
				if(ex >= OvX)
					_p.rect(xe - 0.52f, y, 0.35f, OvRow, hue::t.c[0]);
			}
		}
		_p.rect(OvX + 16 * OvRow - 0.45f, 35, 0.2f, 16 * OvRow, juce::Colour(0xff4A4B4D));
		// Detail's step on the Track shown
		if(m_seqStep >= w0 && m_seqStep < w0 + 32)
			_p.frame(OvX + static_cast<float>(m_seqStep - w0) * OvRow + 0.15f, OvY + static_cast<float>(m_track) * OvRow + 0.15f,
				OvRow - 1, OvRow - 1, juce::Colour(0xffD8D6D0), 0, 0.3f);
	}

	// ---- Seq, Patterns: the 128 patterns, the one asked for, the chain ----

	void Editor::buildPatterns()
	{
		constexpr float Rh = 20.4f;
		const float chainY = 198, sy = chainY + 13;
		const auto inChain = [sy](const juce::Point<float> _mm) { return _mm.y >= sy - 2 && _mm.y < sy + 29; };
		// Where a drop falls in the chain: before the entry under it
		const auto dropIndex = [this](const float _x)
		{
			return juce::jlimit(0, m_processor.sequencer().chainLength(),
				m_chainPage * 6 + juce::roundToInt((_x - 16) / (4 * Col)));
		};
		const auto setChain = [this](std::vector<ChainEntry> _c, const bool _play)
		{
			m_processor.editSequencer([&](Sequencer& _s) { _s.setChain(_c); });
			// A chain made, or added to while none plays, takes over at the end of the pattern playing
			if(_play && !_c.empty())
				m_processor.requestChain();
		};

		const std::array<std::function<void()>, 5> actions{
			[this]
			{
				if(auto* p = m_processor.parameter("pattern"))
					setNormalised(*p, p->convertTo0to1(static_cast<float>(m_patternSel + 1)));
			},
			[this]
			{
				m_patternClipboard = std::make_unique<Pattern>(m_processor.sequencer().pattern(m_patternSel));
				toast("Pattern " + patternName(m_patternSel) + " copied");
			},
			[this]
			{
				if(!m_patternClipboard)
					return;
				const Pattern from = *m_patternClipboard;
				m_processor.editSequencer([&](Sequencer& _s) { _s.pattern(m_patternSel) = from; });
			},
			[this] { m_processor.editSequencer([this](Sequencer& _s) { _s.clearPattern(m_patternSel); }); },
			[this]
			{
				const auto name = "Pattern " + patternName(m_patternSel) + ".mid";
				m_chooser = std::make_unique<juce::FileChooser>("Export the pattern as MIDI",
					juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile(name), "*.mid");
				const int index = m_patternSel;
				m_chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
					| juce::FileBrowserComponent::warnAboutOverwriting, [this, index](const juce::FileChooser& _c)
					{
						const auto file = _c.getResult();
						if(file == juce::File())
							return;
						const auto mid = file.withFileExtension("mid");
						toast(m_processor.exportPatternMidi(index, mid) ? "Exported " + mid.getFileName()
							: "Could not write " + mid.getFileName());
					});
			}};
		for(int i = 0; i < 5; ++i)
		{
			Hit b;
			b.r = {SG + static_cast<float>(11 + i) * SC, 24, SC - 1, 7};
			b.click = actions[static_cast<size_t>(i)];
			addHit(std::move(b));
		}

		// The grid: a click chooses (and with APPEND ON adds to the chain), a drag into the chain inserts there
		for(int r = 0; r < 8; ++r)
			for(int i = 0; i < 16; ++i)
			{
				const int index = r * 16 + i;
				Hit cell;
				cell.r = {SG + static_cast<float>(i) * SC, 32 + static_cast<float>(r) * Rh, SC - 0.2f, Rh - 0.4f};
				cell.click = [this, index, setChain]
				{
					m_patternSel = index;
					if(!m_chainAppend)
						return;
					auto c = chainOf(m_processor.sequencer());
					if(static_cast<int>(c.size()) < MaxChain)
						c.push_back({static_cast<uint8_t>(index), 1, false});
					setChain(c, m_processor.chainSlot() < 0);
				};
				cell.drag = [this, index](float, float) { m_patternDrag = index; };
				cell.end = [this, index, inChain, dropIndex, setChain]
				{
					m_patternDrag = -1000;
					if(!inChain(m_press.last))
						return;
					auto c = chainOf(m_processor.sequencer());
					if(static_cast<int>(c.size()) < MaxChain)
						c.insert(c.begin() + dropIndex(m_press.last.x), {static_cast<uint8_t>(index), 1, false});
					setChain(c, m_processor.chainSlot() < 0);
				};
				addHit(std::move(cell));
			}

		// CHAIN: APPEND ON, EMPTY, PLAY; its pages; six entries a page
		{
			Hit append;
			append.r = {ScreenX + 26, chainY + 1.6f, 31, 7};
			append.click = [this] { m_chainAppend = !m_chainAppend; };
			addHit(std::move(append));
			Hit empty;
			empty.r = {ScreenX + 59, chainY + 1.6f, 31, 7};
			empty.click = [setChain] { setChain({}, false); };
			addHit(std::move(empty));
			Hit play;
			play.r = {ScreenX + 92, chainY + 1.6f, 22, 7};
			play.click = [this] { m_processor.requestChain(); };
			addHit(std::move(play));
		}
		const auto& seq = m_processor.sequencer();
		const int pages = (seq.chainLength() + 5) / 6;
		m_chainPage = juce::jlimit(0, std::max(0, pages - 1), m_follow && m_processor.chainSlot() >= 0
			? m_processor.chainSlot() / 6 : m_chainPage);
		if(pages > 1)
		{
			const int w1 = juce::jlimit(0, std::max(0, pages - 5), m_chainPage - 2);
			for(int w = w1; w < std::min(pages, w1 + 5); ++w)
			{
				Hit page;
				page.r = {SG + static_cast<float>(11 + w - w1) * SC, chainY + 1.6f, SC - 1, 7};
				page.click = [this, w] { m_chainPage = w; m_follow = false; };
				addHit(std::move(page));
			}
		}
		for(int j = 0; j < 6; ++j)
		{
			const int e = m_chainPage * 6 + j;
			if(e >= seq.chainLength())
				break;
			const float x = cx(static_cast<float>(j * 4));
			// The entry: dragged, it moves where it is dropped in the chain, or leaves it dropped outside
			Hit entry;
			entry.r = {x, sy, 4 * Col - 4, 27};
			entry.drag = [this, e](float, float) { m_patternDrag = -1 - e; };
			entry.end = [this, e, inChain, dropIndex, setChain]
			{
				m_patternDrag = -1000;
				auto c = chainOf(m_processor.sequencer());
				const ChainEntry moved = c[static_cast<size_t>(e)];
				int to = inChain(m_press.last) ? dropIndex(m_press.last.x) : -1;
				c.erase(c.begin() + e);
				if(to >= 0)
					c.insert(c.begin() + (to > e ? to - 1 : to), moved);
				setChain(c, false);
			};
			entry.cursor = juce::MouseCursor::DraggingHandCursor;
			addHit(std::move(entry));
			const auto change = [this, e, setChain](const std::function<void(ChainEntry&)>& _f)
			{
				auto c = chainOf(m_processor.sequencer());
				_f(c[static_cast<size_t>(e)]);
				setChain(c, false);
			};
			Hit fill;
			fill.r = {x + 52, sy + 2, 18, 7};
			fill.click = [change] { change([](ChainEntry& _c) { _c.fill = !_c.fill; }); };
			addHit(std::move(fill));
			Hit minus;
			minus.r = {x + 24, sy + 13, 13, 9.6f};
			minus.click = [change] { change([](ChainEntry& _c) { _c.repeats = static_cast<uint8_t>(std::max(1, _c.repeats - 1)); }); };
			addHit(std::move(minus));
			Hit plus;
			plus.r = {x + 39, sy + 13, 13, 9.6f};
			plus.click = [change] { change([](ChainEntry& _c) { _c.repeats = static_cast<uint8_t>(std::min(16, _c.repeats + 1)); }); };
			addHit(std::move(plus));
		}

		watch({ScreenX, 22, ScreenW, ScreenH - 10}, [this]
		{
			const auto& s = m_processor.sequencer();
			uint64_t h = mix(static_cast<uint64_t>(m_processor.currentPattern()), static_cast<uint64_t>(m_processor.chainSlot() + 1));
			h = mix(mix(h, static_cast<uint64_t>(m_processor.chainPass())), static_cast<uint64_t>(choiceOf("pattern")));
			for(int i = 0; i < s.chainLength(); ++i)
				h = mix(h, static_cast<uint64_t>(s.chainEntry(i).pattern | s.chainEntry(i).repeats << 8 | (s.chainEntry(i).fill ? 1 << 16 : 0)));
			for(int i = 0; i < SeqPatterns; ++i)
				h = mix(h, patternUsed(s.pattern(i)) ? 1 : 0);
			return mix(mix(h, m_chainAppend ? 1 : 0), m_patternClipboard ? 1 : 0);
		});
	}

	void Editor::drawPatterns(Painter& _p)
	{
		constexpr float Rh = 20.4f;
		const float chainY = 198, sy = chainY + 13;
		const auto& seq = m_processor.sequencer();
		const int playing = m_processor.currentPattern();
		const auto* asked = m_processor.parameter("pattern");
		const int want = asked ? juce::roundToInt(asked->convertFrom0to1(asked->getValue())) - 1 : playing;

		_p.rect(16, 23, 458, 0.4f, hue::t.c[1]);
		_p.text(16, 29.2f, "PATTERN " + patternName(m_patternSel), 3.2f, hue::t.c[4], 600);
		const char* actions[]{"REQUEST", "COPY", "PASTE", "CLEAR", "MIDI EXPORT"};
		for(int i = 0; i < 5; ++i)
			drawButton(_p, SG + static_cast<float>(11 + i) * SC, 24, SC - 1, 7, actions[i], false,
				i != 2 || m_patternClipboard != nullptr);

		for(int r = 0; r < 8; ++r)
		{
			const float y = 32 + static_cast<float>(r) * Rh;
			_p.text(ScreenX + 4, y + 11.6f, juce::String::charToString(static_cast<juce::juce_wchar>('A' + r)), 3.8f,
				colour::mid, 600);
			for(int i = 0; i < 16; ++i)
			{
				const int index = r * 16 + i;
				const float x = SG + static_cast<float>(i) * SC;
				const bool has = patternUsed(seq.pattern(index)), pl = index == playing;
				_p.rect(x, y + 0.2f, SC - 1.2f, Rh - 1, pl ? hue::t.c[0] : has ? hue::t.c[2] : beat(i), 0.5f);
				if(has && !pl)
					_p.rect(x, y + 0.2f, SC - 1.2f, 0.6f, hue::t.c[3]);
				if(index == want && want != playing)
					_p.frame(x + 0.2f, y + 0.4f, SC - 1.6f, Rh - 1.4f, hue::t.c[0], 0, 0.35f);
				else if(index == m_patternSel)
					_p.frame(x + 0.2f, y + 0.4f, SC - 1.6f, Rh - 1.4f, juce::Colour(0xff6E6E6A), 0, 0.35f);
				_p.text(x + 2.6f, y + 11.6f, patternName(index), 3.2f, pl ? juce::Colour(0xff101113)
					: has ? colour::ink : juce::Colour(0xff7A7A75), pl || has ? 600 : 500);
				int order = 0;
				for(int e = 0; e < seq.chainLength() && !order; ++e)
					order = seq.chainEntry(e).pattern == index ? e + 1 : 0;
				if(order)
					_p.text(x + SC - 3.2f, y + 11.6f, juce::String(order), 3.4f, pl ? juce::Colour(0xff101113) : hue::t.c[4], 700,
						Align::Right);
			}
		}

		// CHAIN
		_p.rect(16, chainY, 458, 0.8f, hue::t.c[0]);
		_p.text(16, chainY + 6.2f, "CHAIN", 3.2f, hue::t.c[4], 600);
		drawButton(_p, ScreenX + 26, chainY + 1.6f, 31, 7, "APPEND ON", m_chainAppend);
		drawButton(_p, ScreenX + 59, chainY + 1.6f, 31, 7, "EMPTY", false, seq.chainLength() > 0);
		const int slot = m_processor.chainSlot();
		drawButton(_p, ScreenX + 92, chainY + 1.6f, 22, 7, "PLAY", slot >= 0, seq.chainLength() > 0);
		const int pages = (seq.chainLength() + 5) / 6;
		const int page = juce::jlimit(0, std::max(0, pages - 1), m_chainPage);
		if(pages > 1)
		{
			const int w1 = juce::jlimit(0, std::max(0, pages - 5), page - 2);
			for(int w = w1; w < std::min(pages, w1 + 5); ++w)
				drawButton(_p, SG + static_cast<float>(11 + w - w1) * SC, chainY + 1.6f, SC - 1, 7,
					juce::String(w * 6 + 1) + "-" + juce::String(w * 6 + 6), w == page);
		}
		if(!seq.chainLength())
			_p.text(ScreenX + 4, sy + 14, "DRAG PATTERNS HERE", 3, colour::dim);
		else
			_p.text(ScreenX + 117, chainY + 6.6f, slot < 0 ? juce::String("PLAY STARTS IT AT THE PATTERN'S END")
				: "SLOT " + juce::String(slot + 1) + " OF " + juce::String(seq.chainLength()) + ", PASS "
					+ juce::String(m_processor.chainPass() + 1) + " OF " + juce::String(seq.chainEntry(slot).repeats),
				2.8f, hue::t.c[4], 600);
		for(int j = 0; j < 6; ++j)
		{
			const int e = page * 6 + j;
			if(e >= seq.chainLength())
				break;
			const auto& c = seq.chainEntry(e);
			const float x = cx(static_cast<float>(j * 4)), w = 4 * Col - 4;
			const bool pl = e == slot;
			_p.rect(x, sy, w, 27, pl ? juce::Colour(0xff1d1c19) : juce::Colour(0xff141517), 0.8f);
			_p.rect(x, sy, w, 0.7f, pl ? hue::t.c[0] : juce::Colour(0xff3A3B3D));
			_p.text(x + 3, sy + 7, juce::String(e + 1), 3.2f, colour::mid, 500);
			_p.text(x + 11, sy + 9, patternName(c.pattern), 5, pl ? hue::t.c[4] : colour::ink, 600);
			_p.text(x + 3, sy + 20, "x " + juce::String(c.repeats), 4, colour::ink, 600);
			drawButton(_p, x + 52, sy + 2, 18, 7, "FILL", c.fill);
			drawButton(_p, x + 24, sy + 13, 13, 9.6f, "-", false, c.repeats > 1);
			drawButton(_p, x + 39, sy + 13, 13, 9.6f, "+", false, c.repeats < 16);
			for(int k = 0; k < c.repeats && k < 8; ++k)
				_p.rect(x + 3 + static_cast<float>(k) * 5, sy + 23.4f, 4, 1.2f,
					pl && k <= m_processor.chainPass() ? hue::t.c[0] : juce::Colour(0xff3A3B3D));
		}
		// What a drag carries, under the mouse, and where it falls in the chain
		if(m_patternDrag != -1000 && m_press.hit >= 0)
		{
			const auto at = m_press.last;
			if(at.y >= sy - 2 && at.y < sy + 29)
			{
				const int i = juce::jlimit(0, seq.chainLength(), page * 6 + juce::roundToInt((at.x - 16) / (4 * Col))) - page * 6;
				_p.rect(cx(static_cast<float>(i * 4)) - 2.4f, sy, 0.8f, 27, hue::t.c[0]);
			}
			const int shown = m_patternDrag >= 0 ? m_patternDrag : seq.chainEntry(-1 - m_patternDrag).pattern;
			_p.rect(at.x - 9, at.y - 5, 18, 10, juce::Colour(0xff2b2a26), 0.8f);
			_p.frame(at.x - 9, at.y - 5, 18, 10, hue::t.c[0], 0.8f, 0.3f);
			_p.text(at.x, at.y + 1.4f, patternName(shown), 3.2f, colour::ink, 600, Align::Centre);
		}
	}

	// ---- Master: the mix and the master section ----

	namespace
	{
		// A return meter's value: whole dB, -inf under -60 (t-ui-spec)
		juce::String returnDb(const float _peak)
		{
			if(_peak < 0.001f)
				return juce::String::fromUTF8("\xe2\x88\x92") + "inf";
			const int db = juce::roundToInt(20 * std::log10(_peak));
			return db < 0 ? juce::String::fromUTF8("\xe2\x88\x92") + juce::String(-db) : juce::String(db);
		}
	}

	void Editor::buildMaster()
	{
		const auto outs = outputs();
		for(int i = 0; i < 16; ++i)
		{
			const float xc = 22 + static_cast<float>(i) * 13.5f;
			Hit name;
			name.r = {xc - 6.5f, 25, 13, 7};
			name.click = [this, i] { showTrack(i); };
			addHit(std::move(name));
			Cell fader = cellOf(trackParameter("level", i), "", xc - 8, 34, 16, hue::w, "");
			fader.look = Look::Fader;
			fader.h = 166.5f;
			addCell(fader);
			Cell pan = cellOf(trackParameter("pan", i), "", xc - 6.5f, 202.5f, 13, hue::w, "");
			pan.look = Look::Small;
			pan.format = Format::Pan;
			pan.h = 11;
			addCell(pan);
			Cell out = cellOf(trackParameter("output", i), "", xc - 6.5f, 234.1f, 13, hue::w, "");
			out.look = Look::Small;
			out.labels = outs;
			out.h = 6;
			addCell(out);
		}
		const char* returns[]{"delay_output", "reverb_output", "chorus_output", "phaser_output"};
		for(int i = 0; i < 4; ++i)
		{
			Cell out = cellOf(m_processor.parameter(returns[i]), "", 237 + static_cast<float>(i) * 9.5f - 5, 234.1f, 10,
				hue::w, "");
			out.look = Look::Small;
			out.labels = outs;
			out.h = 6;
			addCell(out);
		}
		Cell main = cellOf(m_processor.parameter("master_volume"), "", 279 - 8, 34, 16, hue::w, "");
		main.look = Look::Fader;
		main.h = 166.5f;
		addCell(main);

		const float x1 = cx(15) - 2, w = 9 * Col + 2, cw = (w - 2) / 3;
		block(x1, 23, w, "k", "COMPRESSOR", hue::k);
		const char* ids[]{"master_threshold", "master_ratio", "master_attack", "master_release", "master_makeup",
			"master_mix"};
		const char* names[]{"THRESHOLD", "RATIO", "ATTACK", "RELEASE", "MAKEUP", "MIX"};
		for(int i = 0; i < 6; ++i)
			addCell(cellOf(m_processor.parameter(ids[i]), names[i], x1 + 2 + static_cast<float>(i % 3) * cw,
				32 + static_cast<float>(i / 3) * 18, cw, hue::k, "k"));
		block(x1, 72, w, "ms", "MASTER", hue::s);
		const char* ids2[]{"master_on", "master_drive", "master_filter"};
		const char* names2[]{"MASTER", "DRIVE", "SC FILTER"};
		for(int i = 0; i < 3; ++i)
			addCell(cellOf(m_processor.parameter(ids2[i]), names2[i], x1 + 2 + static_cast<float>(i) * cw, 81, cw,
				hue::s, "ms"));
		watch({ScreenX, 25, 280, 182}, [this]
		{
			uint64_t h = static_cast<uint64_t>(m_processor.outputPeak(0) * 40);
			for(size_t t = 0; t < 16; ++t)
				h = mix(h, static_cast<uint64_t>(m_activity[t] * 10));
			for(int t = 0; t < 16; ++t)
				h = mixFloat(h, trackParameter("level", t)->getValue());
			for(int s = 0; s < SendCount; ++s)
				h = mix(h, static_cast<uint64_t>(m_processor.returnPeak(s) * 400));
			return mixFloat(h, valueOf("master_volume"));
		});
		// The gain reduction's bar and its trace, a frame each, which scrolls while the master is on
		watch({cx(15) + 100, 134, 9 * Col - 100, 108}, [this]
		{
			const float gr = m_processor.masterReduction();
			m_grTrace[m_grAt] = gr;
			m_grAt = (m_grAt + 1) % m_grTrace.size();
			return choiceOf("master_on") ? mix(static_cast<uint64_t>(m_grAt), static_cast<uint64_t>(-gr * 10)) : 0;
		});
		watch({x1, 130, w, 108}, [this]
		{
			return hashOf({m_processor.parameter("master_threshold"), m_processor.parameter("master_ratio"),
				m_processor.parameter("master_makeup"), m_processor.parameter("master_mix")});
		});
	}

	void Editor::drawMaster(Painter& _p)
	{
		drawHeader(_p, "MASTER", {});
		const float h = 166.5f;
		for(int i = 0; i < 16; ++i)
		{
			const float xc = 22 + static_cast<float>(i) * 13.5f;
			const bool sel = i == m_track;
			// The machine's name, cut to the strip's 12.5 mm
			auto name = machineName(i);
			while(name.length() > 1 && _p.textWidth(name, 2.6f, sel ? 600 : 500) > 12.5f)
				name = name.dropLastCharacters(1);
			_p.text(xc, 30, name, 2.6f, sel ? colour::ink : colour::mid, sel ? 600 : 500, Align::Centre);
			const float level = trackParameter("level", i)->getValue();
			ladder(_p, xc - 2.5f, 34, 5, h, m_activity[static_cast<size_t>(i)] * level);
			_p.text(xc, 206.5f, juce::String(juce::roundToInt(level * 127)), 3.2f, colour::ink, 600, Align::Centre);
			// Mute and Solo have no engine yet: greyed
			_p.rect(xc - 6, 214.5f, 12, 8.6f, colour::button, 0.8f);
			_p.text(xc, 220.3f, "M", 3, juce::Colour(0xff4E4E4C), 500, Align::Centre);
			_p.rect(xc - 6, 223.9f, 12, 8.6f, colour::button, 0.8f);
			_p.text(xc, 229.7f, "S", 3, juce::Colour(0xff4E4E4C), 500, Align::Centre);
		}
		const char* returns[]{"DLY", "REV", "CHO", "PHA"};
		for(int i = 0; i < 4; ++i)
		{
			const float c = 237 + static_cast<float>(i) * 9.5f;
			_p.text(c, 30, returns[i], 2.6f, colour::mid, 500, Align::Centre);
			const float peak = m_processor.returnPeak(i);
			ladder(_p, c - 1.6f, 34, 3.2f, h, juce::jlimit(0.0f, 1.0f, 1.07f * std::sqrt(peak)));
			_p.text(c, 206.5f, returnDb(peak), 2.6f, colour::ink, 600, Align::Centre);
		}
		const float xm = 279;
		_p.text(xm, 30, "MAIN", 2.8f, colour::ink, 600, Align::Centre);
		const float peak = m_processor.outputPeak(0);
		const float f = juce::jlimit(0.0f, 1.0f, 1.07f * std::sqrt(peak));
		ladder(_p, xm - 5, 34, 4.4f, h, f);
		ladder(_p, xm + 0.6f, 34, 4.4f, h, f);
		for(const int d : {0, -6, -12, -24, -48})
		{
			const float yy = 34 + h * (1 - std::pow(10.0f, static_cast<float>(d) / 20));
			_p.rect(xm + 5.4f, yy - 0.1f, 1.2f, 0.2f, juce::Colour(0xff6E6E6A));
			_p.text(xm + 7, yy + 1, d == 0 ? juce::String("0") : juce::String::fromUTF8("\xe2\x88\x92") + juce::String(-d),
				2.4f, colour::dim);
		}
		_p.text(xm, 206.5f, juce::String(choiceOf("master_volume")), 3.2f, colour::ink, 600, Align::Centre);

		// The compressor's curve from its values: 0-127 drawn as -60 to 0 dB, ratio 1 to 8.5, makeup 0 to 7 dB
		const float x1 = cx(15) - 2, w = 9 * Col + 2, yv = 134;
		_p.rect(x1 + 2, yv, w - 6, 0.6f, hue::k.c[0]);
		const float sz = 96, a = x1 + 8, b = yv + 4;
		const float th = valueOf("master_threshold") * 60 - 60, ra = 1 + valueOf("master_ratio") * 7.5f,
			mk = valueOf("master_makeup") * 7, mx = valueOf("master_mix");
		const auto X = [&](const float _d) { return a + (_d + 60) / 60 * sz; };
		const auto Y = [&](const float _d) { return b + sz - (_d + 60) / 60 * sz; };
		const auto comp = [&](const float _d) { const float c = _d < th ? _d : th + (_d - th) / ra; return mx * (c + mk) + (1 - mx) * _d; };
		_p.frame(a, b, sz, sz, colour::faint, 0.3f, 0.3f);
		for(const int d : {-48, -36, -24, -12})
		{
			_p.line(X(static_cast<float>(d)), b, X(static_cast<float>(d)), b + sz, juce::Colour(0xff242527), 0.25f);
			_p.line(a, Y(static_cast<float>(d)), a + sz, Y(static_cast<float>(d)), juce::Colour(0xff242527), 0.25f);
			const auto label = juce::String::fromUTF8("\xe2\x88\x92") + juce::String(-d);
			_p.text(X(static_cast<float>(d)), b + sz + 3.2f, label, 2.4f, colour::dim, 500, Align::Centre);
			_p.text(a - 1, Y(static_cast<float>(d)) + 0.9f, label, 2.4f, colour::dim, 500, Align::Right);
		}
		_p.g.saveState();
		_p.g.reduceClipRegion(juce::Rectangle<float>(a, b, sz, sz).getSmallestIntegerContainer());
		_p.poly({{X(-60), Y(-60)}, {X(0), Y(0)}}, colour::rule, 0.3f);
		std::vector<juce::Point<float>> curve;
		for(float d = -60; d <= 0; d += 0.5f)
			curve.push_back({X(d), Y(comp(d))});
		_p.poly(curve, hue::k.c[0], 0.7f);
		_p.g.restoreState();
		const float tx = X(th);
		const float dash[]{1, 1};
		_p.g.setColour(hue::k.c[4]);
		_p.g.drawDashedLine({tx, b, tx, b + sz}, dash, 2, 0.3f);
		_p.text(juce::jlimit(a + 1, a + sz - 20, tx + 1), b + sz - 1.6f, "THR", 2.8f, hue::k.c[4], 600);
		_p.dot(tx, juce::jlimit(b, b + sz, Y(comp(th))), 1.5f, juce::Colours::black, hue::k.c[4], 0.5f);
		// Gain reduction, the audio thread's at the frame: a bar down from 0 to -12 dB, its number under it, and its
		// trace of the last 240 frames, the newest on the right
		const float gx = a + sz + 7;
		_p.rect(gx, b, 5, sz, juce::Colour(0xff17181A), 0.4f);
		const float trx = gx + 7, trw = x1 + w - 4 - trx;
		_p.rect(trx, b, trw, sz, juce::Colour(0xff121315), 0.4f);
		_p.text(trx + 1, b + 3, "GR", 2.4f, colour::mid);
		const auto down = [&](const float _db) { return sz * juce::jlimit(0.0f, 1.0f, -_db / 12); };
		const float gr = m_processor.masterReduction();
		if(gr < 0)
			_p.rect(gx, b, 5, down(gr), hue::k.c[0]);
		_p.text(gx + 2.5f, b + sz + 3.2f, gr < -0.05f ? juce::String::fromUTF8("\xe2\x88\x92") + juce::String(-gr, 1) :
			juce::String("0.0"), 2.4f, colour::ink, 600, Align::Centre);
		std::vector<juce::Point<float>> trace;
		const size_t n = m_grTrace.size();
		for(size_t i = 0; i < n; ++i)
			trace.push_back({trx + trw * static_cast<float>(i) / static_cast<float>(n - 1),
				b + down(m_grTrace[(m_grAt + i) % n])});
		_p.poly(trace, hue::k.c[0], 0.4f);
	}

	// ---- FX: the four shared effects ----

	void Editor::buildFx()
	{
		const auto outs = outputs();
		const auto blockOf = [&](const int _i, const juce::String& _s, const juce::String& _title, const Hue& _hue,
			const std::vector<std::pair<const char*, const char*>>& _cells, const std::vector<bool>& _visible)
		{
			const float x = cx(2 + static_cast<float>(_i) * 5.5f) - 2, w = 5.5f * Col + 2, cw = (w - 2) / 3;
			block(x, 23, w, _s, _title, _hue);
			int slot = 0;
			for(size_t k = 0; k < _cells.size(); ++k)
			{
				const bool shown = _visible.empty() || _visible[k];
				Cell c = cellOf(m_processor.parameter(_cells[k].first), _cells[k].second,
					x + 2 + static_cast<float>(slot % 3) * cw, 32 + static_cast<float>(slot / 3) * 18, cw, _hue, _s);
				c.visible = shown;
				if(juce::String(_cells[k].first).endsWith("_output"))
					c.labels = outs;
				addCell(c);
				if(shown)
					++slot;
			}
		};
		// SYNC or HZ at the block's top right, as the LFO's: the delay's DURATION in notes or its HZ, the chorus's and
		// the phaser's SPEED in notes or their own rate
		const auto syncOf = [&](const int _i, const char* _id, const Hue& _hue)
		{
			const float x = cx(2 + static_cast<float>(_i) * 5.5f) - 2, w = 5.5f * Col + 2;
			Cell c = cellOf(m_processor.parameter(_id), "SYNC", x + w - 30, 24.8f, 28, _hue, "");
			c.look = Look::Segments;
			c.labels = {"HZ", "SYNC"};
			c.reversed = true;
			c.h = 6;
			addCell(c);
		};
		const bool echo = choiceOf("delay_source") == 0, gate = choiceOf("reverb_source") == 0;
		const bool sync = choiceOf("delay_sync") != 0, chorusSync = choiceOf("chorus_sync") != 0,
			phaserSync = choiceOf("phaser_sync") != 0;
		blockOf(0, "fd", "DELAY", hue::k, {{"delay_source", "SOURCE"}, {"delay_output", "OUT"},
			{"delay_note", "TIME"}, {"delay_free", "RATE"},
			{"echo_mod", "MOD DEPTH"}, {"echo_mfrq", "MOD RATE"}, {"echo_fb", "FEEDBACK"},
			{"echo_fltf", "HPF"}, {"echo_fltw", "BANDWIDTH"}, {"echo_mono", "MONO"}, {"echo_lev", "LEVEL"},
			{"delay_cross", "PING-PONG"}, {"delay_width", "WIDTH"}, {"delay_fdbk", "FEEDBACK"},
			{"delay_hp", "HPF"}, {"delay_lp", "LPF"}, {"delay_rev", "TO REVERB"},
			// The machines' TIME, in 128ths, which plays with Sync off at 0 Hz: a host's, not shown
			{"echo_time", "TIME"}, {"delay_time", "TIME"}},
			{true, true, sync, !sync, echo, echo, echo, echo, echo, echo, echo, !echo, !echo, !echo, !echo, !echo, !echo,
				false, false});
		syncOf(0, "delay_sync", hue::k);
		blockOf(1, "fr", "REVERB", hue::a, {{"reverb_source", "SOURCE"}, {"reverb_output", "OUT"},
			{"gate_dvol", "DRY LEVEL"}, {"gate_pred", "PRE-DELAY"}, {"gate_dec", "DECAY"}, {"gate_damp", "DAMPING"},
			{"gate_hp", "HPF"}, {"gate_lp", "LPF"}, {"gate_gate", "GATE"}, {"gate_lev", "LEVEL"},
			{"reverb_pre", "PRE-DELAY"}, {"reverb_dec", "DECAY"}, {"reverb_freq", "SHELF FREQ"}, {"reverb_gain", "SHELF GAIN"},
			{"reverb_hp", "HPF"}, {"reverb_lp", "LPF"}},
			{true, true, gate, gate, gate, gate, gate, gate, gate, gate, !gate, !gate, !gate, !gate, !gate, !gate});
		blockOf(2, "fc", "CHORUS", hue::g, {{"chorus_output", "OUT"}, {"chorus_depth", "DEPTH"},
			{"chorus_rate", "RATE"}, {"chorus_speed", "RATE"}, {"chorus_delay", "DELAY"}, {"chorus_fdbk", "FEEDBACK"},
			{"chorus_level", "LEVEL"}, {"chorus_shape", "WAVE"}}, {true, true, !chorusSync, chorusSync, true, true, true, true});
		syncOf(2, "chorus_sync", hue::g);
		blockOf(3, "fp", "PHASER / FLANGER", hue::p, {{"phaser_mode", "TYPE"}, {"phaser_output", "OUT"},
			{"phaser_stages", "STAGES"}, {"phaser_depth", "DEPTH"}, {"phaser_rate", "RATE"}, {"phaser_speed", "RATE"},
			{"phaser_freq", "FREQ"}, {"phaser_spread", "SPREAD"}, {"phaser_fdbk", "FEEDBACK"}, {"phaser_level", "LEVEL"},
			{"phaser_dp4_phase", "WET PHASE"}, {"phaser_dp4_left", "DELAY L"}, {"phaser_dp4_right", "DELAY R"},
			{"phaser_dp4_fdbk", "DELAY FDBK"}, {"phaser_dp4_sh", "S&H RATE"}},
			{true, true, true, true, !phaserSync, phaserSync, true, true, true, true, true, true, true, true, true});
		syncOf(3, "phaser_sync", hue::p);
		// The returns' meters, at the frame's rate
		watch({12, 25, 40, 210}, [this]
		{
			uint64_t h = 0;
			for(int s = 0; s < SendCount; ++s)
				h = mix(h, static_cast<uint64_t>(m_processor.returnPeak(s) * 400));
			return h;
		});
		watch({cx(2), 124, 22 * Col, 116}, [this]
		{
			uint64_t h = 0;
			for(const auto& id : m_processor.ids(Processor::Section::Effects))
				h = mixFloat(h, valueOf(id));
			return h;
		});
	}

	void Editor::drawFx(Painter& _p)
	{
		drawHeader(_p, "FX", {});
		const char* returns[]{"DLY", "REV", "CHO", "PHA"};
		for(int i = 0; i < 4; ++i)
		{
			const float c = 18 + static_cast<float>(i) * 10;
			_p.text(c, 30, returns[i], 2.6f, colour::mid, 500, Align::Centre);
			const float peak = m_processor.returnPeak(i);
			ladder(_p, c - 1.6f, 34, 3.2f, 190, juce::jlimit(0.0f, 1.0f, 1.07f * std::sqrt(peak)));
			_p.text(c, 230, returnDb(peak), 2.6f, colour::ink, 600, Align::Centre);
		}
		// Each effect's view, from its values (y 128 to 238)
		const float vy = 130, vh = 106;
		for(int i = 0; i < 4; ++i)
		{
			const Hue& h = i == 0 ? hue::k : i == 1 ? hue::a : i == 2 ? hue::g : hue::p;
			const float a = cx(2 + static_cast<float>(i) * 5.5f), w = 5.5f * Col - 4, by = vy + vh;
			_p.rect(a, 124, w, 0.6f, h.c[0]);
			_p.line(a, by, a + w, by, colour::faint, 0.2f);
			if(i == 0)
			{
				const bool echo = choiceOf("delay_source") == 0;
				const float time = valueOf(echo ? "echo_time" : "delay_time"), fb = valueOf(echo ? "echo_fb" : "delay_fdbk");
				const float t = juce::jlimit(0.02f, 0.2f, 0.02f + time * 0.18f);
				for(int k = 1; k < 40; ++k)
				{
					const float xx = a + static_cast<float>(k) * t * w;
					if(xx > a + w)
						break;
					const float hh = (vh - 2) * std::pow(std::min(fb * 1.6f, 0.95f), static_cast<float>(k - 1));
					_p.rect(xx, by - hh, 0.6f, hh, h.c[0]);
				}
			}
			else if(i == 1)
			{
				const bool gate = choiceOf("reverb_source") == 0;
				const float dc = 2 + (1 - valueOf(gate ? "gate_dec" : "reverb_dec")) * 9,
					pre = valueOf(gate ? "gate_pred" : "reverb_pre") * 0.2f;
				std::vector<juce::Point<float>> curve;
				for(int k = 0; k <= 300; ++k)
				{
					const float u = static_cast<float>(k) / 300;
					const float yr = u < pre ? by : by - (vh - 2) * std::exp(-(u - pre) * dc) * std::min(1.0f, (u - pre) / 0.02f);
					curve.push_back({a + u * w, yr});
				}
				_p.poly(curve, h.c[0], 0.55f);
			}
			else if(i == 2)
			{
				const float n = vy + vh / 2, depth = valueOf("chorus_depth") * 0.9f + 0.1f,
					cycles = 1 + valueOf("chorus_rate") * 8;
				std::vector<juce::Point<float>> curve;
				for(int k = 0; k <= 400; ++k)
				{
					const float u = static_cast<float>(k) / 400;
					curve.push_back({a + u * w, n - std::sin(u * 6.283f * cycles) * vh * 0.46f * depth});
				}
				_p.line(a, n, a + w, n, colour::faint, 0.2f);
				_p.poly(curve, h.c[0], 0.55f);
			}
			else
			{
				const float dep = valueOf("phaser_depth"), pw = 1.2f + valueOf("phaser_rate");
				const float notches = static_cast<float>(choiceOf("phaser_stages"));
				std::vector<juce::Point<float>> curve;
				for(int k = 0; k <= 500; ++k)
				{
					const float u = static_cast<float>(k) / 500;
					const float e = std::abs(std::cos(std::pow(u, pw) * juce::MathConstants<float>::pi * std::max(1.0f, notches / 2)));
					curve.push_back({a + u * w, vy + vh - 3 - (1 - dep * (1 - std::pow(e, 0.6f))) * (vh - 8)});
				}
				_p.poly(curve, h.c[0], 0.55f);
			}
		}
	}

	// ---- Kits and Browse ----

	// The Bank's 64 Slots in four columns of 16 rows, a click selects one; the Slot selected's panel: LOAD, SAVE HERE,
	// RENAME, DELETE, a second click on DELETE empties it. IMPORT and EXPORT have no engine yet: greyed
	namespace
	{
		constexpr float KitRowH = 12.9f, KitPanelX = cx(16), KitButtonW = (8 * Col - 4 - 2) / 2;

		juce::Rectangle<float> kitRow(const int _slot)
		{
			return {cx(static_cast<float>((_slot / 16) * 4)), 32 + static_cast<float>(_slot % 16) * KitRowH, 4 * Col - 4,
				KitRowH - 0.7f};
		}

		// LOAD, SAVE HERE; RENAME, DELETE; the FILE's IMPORT, EXPORT; then ticket 19's: the pattern's LOAD AT START, KEEP
		// THE KIT; its IMPORT, EXPORT; the Track's SAVE SOUND, LOAD SOUND
		constexpr int KitButtons = 12;
		juce::Rectangle<float> kitButton(const int _i)
		{
			constexpr float rows[]{47, 61.5f, 222, 97, 111.5f, 146};
			return {KitPanelX + static_cast<float>(_i % 2) * (KitButtonW + 2), rows[_i / 2], KitButtonW, 12.5f};
		}

		juce::String slotNumber(const int _slot)
		{
			return juce::String(_slot + 1).paddedLeft('0', 2);
		}
	}

	juce::Rectangle<int> Editor::kitSlotArea(const int _slot) const
	{
		return toPx(kitRow(_slot));
	}

	juce::Rectangle<int> Editor::kitActionArea(const int _action) const
	{
		return toPx(kitButton(_action));
	}

	void Editor::buildKits()
	{
		for(int i = 0; i < Processor::KitSlots; ++i)
		{
			Hit row;
			row.r = kitRow(i).withHeight(KitRowH);
			row.click = [this, i]
			{
				m_kitSel = i;
				m_kitDelete = -1;
			};
			addHit(std::move(row));
		}
		const std::array<std::function<void()>, KitButtons> actions{
			[this]
			{
				if(m_processor.loadKit(m_kitSel))
					toast("Loaded " + m_processor.kitSlot(m_kitSel).name);
			},
			[this]
			{
				toast(m_processor.saveKit(m_kitSel) ? "Saved in slot " + slotNumber(m_kitSel)
					: "Slot " + slotNumber(m_kitSel) + " could not be written");
			},
			[this]
			{
				if(!m_processor.kitSlot(m_kitSel).empty())
					renameKit(m_kitSel);
			},
			[this]
			{
				if(m_processor.kitSlot(m_kitSel).empty())
					return;
				// What loses a Kit takes a second click
				if(m_kitDelete != m_kitSel)
				{
					m_kitDelete = m_kitSel;
					toast("Click DELETE again to empty slot " + slotNumber(m_kitSel));
					return;
				}
				m_kitDelete = -1;
				if(m_processor.clearKit(m_kitSel))
					toast("Slot " + slotNumber(m_kitSel) + " emptied");
			},
			[this]
			{
				const int slot = m_kitSel;
				chooseFile("Import a kit into slot " + slotNumber(slot), "*.edkit", false, [this, slot](const juce::File& _f)
				{
					toast(m_processor.importKit(slot, _f) ? "Imported " + m_processor.kitSlot(slot).name + " in slot "
						+ slotNumber(slot) : _f.getFileName() + " is not a kit");
				});
			},
			[this]
			{
				const int slot = m_kitSel;
				if(m_processor.kitSlot(slot).empty())
					return;
				chooseFile("Export kit " + slotNumber(slot), "*.edkit", true, [this, slot](const juce::File& _f)
				{
					const auto file = _f.withFileExtension(".edkit");
					toast(m_processor.exportKit(slot, file) ? "Exported to " + file.getFileName()
						: file.getFileName() + " could not be written");
				});
			},
			[this]
			{
				if(m_processor.kitSlot(m_kitSel).empty())
					return;
				const int pattern = m_processor.currentPattern();
				m_processor.setPatternKit(pattern, m_kitSel);
				toast("Pattern " + juce::String(pattern + 1) + " loads slot " + slotNumber(m_kitSel) + " at its start");
			},
			[this]
			{
				const int pattern = m_processor.currentPattern();
				m_processor.setPatternKit(pattern, -1);
				toast("Pattern " + juce::String(pattern + 1) + " keeps the kit playing");
			},
			[this]
			{
				const int pattern = m_processor.currentPattern();
				chooseFile("Import into pattern " + juce::String(pattern + 1), "*.edpattern", false,
					[this, pattern](const juce::File& _f)
					{
						toast(m_processor.importPattern(pattern, _f) ? "Imported " + _f.getFileNameWithoutExtension()
							+ " in pattern " + juce::String(pattern + 1) : _f.getFileName() + " is not a pattern");
					});
			},
			[this]
			{
				const int pattern = m_processor.currentPattern();
				chooseFile("Export pattern " + juce::String(pattern + 1), "*.edpattern", true,
					[this, pattern](const juce::File& _f)
					{
						const auto file = _f.withFileExtension(".edpattern");
						toast(m_processor.exportPattern(pattern, file) ? "Exported to " + file.getFileName()
							: file.getFileName() + " could not be written");
					});
			},
			[this] { saveTrackSound(); },
			[this]
			{
				// The first 32 of the Library, eight a column; Browse lists them all
				juce::StringArray names;
				std::vector<juce::File> files;
				for(const auto& e : m_processor.trackSounds().entries())
				{
					if(names.size() == 32)
						break;
					names.add(e.name);
					files.push_back(e.file);
				}
				if(names.isEmpty())
				{
					toast("No track sound saved yet");
					return;
				}
				const auto b = kitButton(11);
				openSelector(b.getX(), b.getY(), names, {}, [this, files](const int _i)
				{
					loadTrackSound(m_track, files[static_cast<size_t>(_i)]);
				});
			}};
		for(size_t i = 0; i < actions.size(); ++i)
		{
			Hit button;
			button.r = kitButton(static_cast<int>(i));
			button.click = actions[i];
			addHit(std::move(button));
		}
		// Another instance's write, the sound changed, a Slot selected: the page again
		watch({ScreenX, 20, ScreenW, 220}, [this]
		{
			uint64_t h = mix(0, static_cast<uint64_t>(m_processor.currentKit() + 1));
			h = mix(h, m_processor.kitDiffers() ? 1 : 0);
			h = mix(mix(h, static_cast<uint64_t>(m_kitSel)), static_cast<uint64_t>(m_kitDelete + 1));
			const int pattern = m_processor.currentPattern();
			h = mix(mix(h, static_cast<uint64_t>(pattern)), static_cast<uint64_t>(m_processor.patternKit(pattern) + 1));
			h = mix(mix(h, static_cast<uint64_t>(m_track)), m_processor.trackSounds().generation());
			for(int i = 0; i < Processor::KitSlots; ++i)
			{
				const auto& s = m_processor.kitSlot(i);
				h = mix(mix(h, static_cast<uint64_t>(s.size)), static_cast<uint64_t>(s.written.toMilliseconds()));
			}
			return h;
		});
	}

	void Editor::renameKit(const int _slot)
	{
		// Deleted by the modal manager after its callback, which reads the name first
		auto* window = new juce::AlertWindow("Rename kit " + slotNumber(_slot), {}, juce::MessageBoxIconType::NoIcon,
			this);
		window->addTextEditor("name", m_processor.kitSlot(_slot).name);
		window->getTextEditor("name")->setInputRestrictions(16);
		window->addButton("Rename", 1, juce::KeyPress(juce::KeyPress::returnKey));
		window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
		juce::Component::SafePointer<Editor> self(this);
		window->enterModalState(true, juce::ModalCallbackFunction::create([self, window, _slot](const int _result)
		{
			const auto name = window->getTextEditorContents("name").trim();
			if(!self || _result != 1 || name.isEmpty())
				return;
			self->m_processor.renameKit(_slot, name);
			self->repaint();
		}), true);
	}

	void Editor::drawKits(Painter& _p)
	{
		drawHeader(_p, "KITS", {});
		int used = 0;
		for(int i = 0; i < Processor::KitSlots; ++i)
			used += m_processor.kitSlot(i).empty() ? 0 : 1;
		const int played = m_processor.currentKit();
		const Block bank{cx(0) - 2, 23, 16 * Col + 2, "BANK", "kb", &hue::t};
		drawBlockTitle(_p, bank);
		_p.text(cx(16) - 6, 29.2f, juce::String(used) + " OF 64", 2.8f, hue::t.c[4], 500, Align::Right);
		for(int i = 0; i < Processor::KitSlots; ++i)
		{
			const auto r = kitRow(i);
			const bool on = i == played, selected = i == m_kitSel;
			_p.rect(r.getX(), r.getY(), r.getWidth(), r.getHeight(), selected ? colour::faint : beat(i % 16), 0.5f);
			if(on)
				_p.rect(r.getX(), r.getY() + 0.6f, 0.7f, KitRowH - 1.9f, hue::t.c[0], 0.3f);
			_p.text(r.getX() + 2.4f, r.getY() + 8, slotNumber(i), 2.8f, juce::Colour(0xff5E5E5A));
			const auto& slot = m_processor.kitSlot(i);
			if(!slot.empty())
				_p.text(r.getX() + 10, r.getY() + 8, slot.name, 3.2f, on ? hue::t.c[4] : selected ? colour::ink
					: colour::mid, on || selected ? 600 : 500);
		}

		// The Slot selected
		const float x = KitPanelX;
		const auto& slot = m_processor.kitSlot(m_kitSel);
		const bool playing = m_kitSel == played, edited = playing && m_processor.kitDiffers();
		drawBlockTitle(_p, {x - 2, 23, 8 * Col + 2, "KIT " + slotNumber(m_kitSel), "kk", &hue::t});
		if(playing)
			_p.text(x + 8 * Col - 4, 29.2f, edited ? "PLAYING, EDITED" : "PLAYING", 2.8f, hue::t.c[4], 500, Align::Right);
		_p.text(x, 40, slot.empty() ? juce::String("Empty") : slot.name, 5, slot.empty() ? colour::dim : colour::ink, 600);
		drawBlockTitle(_p, {x - 2, 213, 8 * Col + 2, "FILE", "kf", &hue::t});

		// The pattern playing's Kit at its start, and its file; the Track shown's sound
		const int pattern = m_processor.currentPattern(), loads = m_processor.patternKit(pattern);
		drawBlockTitle(_p, {x - 2, 79, 8 * Col + 2, "PATTERN " + juce::String(pattern + 1), "kp", &hue::t});
		_p.text(x, 92, loads < 0 ? juce::String("AT ITS START: KEEPS THE KIT PLAYING")
			: "AT ITS START: LOADS SLOT " + slotNumber(loads) + ", " + m_processor.kitSlot(loads).name.toUpperCase(), 2.8f,
			loads < 0 ? colour::dim : hue::t.c[4], 500);
		drawBlockTitle(_p, {x - 2, 128, 8 * Col + 2, "TRACK " + juce::String(m_track + 1) + " SOUND", "ks", &hue::t});
		_p.text(x, 141, machineName(m_track).toUpperCase() + ", " + juce::String(static_cast<int>(
			m_processor.trackSounds().entries().size())) + " SOUNDS IN THE LIBRARY", 2.8f, colour::mid, 500);

		const juce::String names[]{"LOAD", "SAVE HERE", "RENAME", m_kitDelete == m_kitSel ? "DELETE: AGAIN" : "DELETE",
			"IMPORT", "EXPORT", "LOAD AT ITS START", "KEEP THE KIT", "IMPORT PATTERN", "EXPORT PATTERN", "SAVE SOUND",
			"LOAD SOUND"};
		for(int i = 0; i < KitButtons; ++i)
		{
			const auto b = kitButton(i);
			// LOAD lit where it changes the sound, SAVE HERE where the Kit played was changed since; the pattern's choice
			const bool enabled = i == 1 || i == 4 || i >= 7 || !slot.empty();
			const bool lit = (i == 0 && !slot.empty() && (!playing || edited)) || (i == 1 && edited)
				|| (i == 3 && m_kitDelete == m_kitSel) || (i == 6 && loads == m_kitSel) || (i == 7 && loads < 0);
			drawButton(_p, b.getX(), b.getY(), b.getWidth(), b.getHeight(), names[i], lit, enabled);
		}
	}

	// LIBRARY's entries, each in its hue (t.js browse()); MACHINES and the two lists of samples have an engine behind
	// them. The list: 16 rows, or 15 under the segments of a list of samples (ALL, then the LOCKER's sources or the
	// types)
	namespace
	{
		constexpr float BrowseRowH = 12.9f, BrowseListW = 10 * Col - 4, BrowseInfoW = 8 * Col - 4;
		constexpr int BrowseFolderSegments = 7;
		const char* const browseTreeNames[]{"KITS", "TRACK SOUNDS", "LOCKER", "MACHINES", "SAMPLES, BY TYPE"};
		// The LIBRARY column's buttons, the card's two side by side
		constexpr float AddFolderY = 170, ImportPackY = 184, CardButtonY = 201, ButtonH = 12.5f;

		const Hue& browseHue(const int _tree)
		{
			const Hue* hues[]{&hue::y, &hue::a, &hue::g, &hue::k, &hue::p};
			return *hues[_tree];
		}

		juce::Rectangle<float> browseTreeRow(const int _tree)
		{
			return {cx(0), 32 + static_cast<float>(_tree) * BrowseRowH, 6 * Col - 4, BrowseRowH - 0.7f};
		}

		juce::Rectangle<float> browseListRow(const int _row, const bool _samples)
		{
			return {cx(6), (_samples ? 41 : 32) + static_cast<float>(_row) * BrowseRowH, BrowseListW, BrowseRowH - 0.7f};
		}

		// The card's RENAME (0) and REMOVE SOURCE or KEEP IN LOCKER (1)
		juce::Rectangle<float> cardButton(const int _i)
		{
			const float w = (BrowseInfoW - 6) / 2;
			return {cx(16) + 2 + static_cast<float>(_i) * (w + 2), CardButtonY, w, ButtonH};
		}

		// The mockup's 22 mm, up to 30 for a few sources' names (IN THIS SET whole), fewer where many share the list
		float browseSegment(const int _count)
		{
			return std::min(_count > 6 ? 22.0f : 30.0f, BrowseListW / static_cast<float>(_count));
		}

		juce::String lengthText(const int64_t _frames, const double _rate)
		{
			const double s = static_cast<double>(_frames) / _rate;
			return s < 1 ? juce::String(juce::roundToInt(s * 1000)) + " MS" : juce::String(s, 2) + " S";
		}

		juce::String bytesText(const int64_t _bytes)
		{
			const double kb = static_cast<double>(_bytes) / 1024;
			return kb < 1000 ? juce::String(juce::roundToInt(kb)) + " KB" : juce::String(kb / 1024, 1) + " MB";
		}

		// What an audition played, _w mm wide about _mid, _h high: min and max a column, 0.5 mm wide
		void drawWave(Painter& _p, const float _x, const float _mid, const float _w, const float _h,
			const std::vector<int16_t>& _pcm, const juce::Colour _colour)
		{
			const int columns = static_cast<int>(_w / 0.5f);
			for(int col = 0; col < columns; ++col)
			{
				const size_t a = _pcm.size() * static_cast<size_t>(col) / static_cast<size_t>(columns);
				const size_t b = std::max(a + 1, _pcm.size() * static_cast<size_t>(col + 1) / static_cast<size_t>(columns));
				int lo = 0, hi = 0;
				for(size_t n = a; n < b && n < _pcm.size(); ++n)
				{
					lo = std::min(lo, static_cast<int>(_pcm[n]));
					hi = std::max(hi, static_cast<int>(_pcm[n]));
				}
				const float px = _x + static_cast<float>(col) * 0.5f + 0.25f;
				_p.line(px, _mid - static_cast<float>(hi) / 32768.0f * _h / 2, px, _mid - static_cast<float>(lo) / 32768.0f
					* _h / 2 + 0.2f, _colour, 0.4f);
			}
		}
	}

	juce::Rectangle<int> Editor::browseTreeArea(const int _tree) const
	{
		return toPx(browseTreeRow(_tree));
	}

	juce::Rectangle<int> Editor::browseRowArea(const int _row) const
	{
		return toPx(browseListRow(_row, browseSamples()));
	}

	juce::Rectangle<int> Editor::browseButtonArea(const BrowseButton _button) const
	{
		switch(_button)
		{
		case BrowseButton::AddFolder: return toPx({cx(0), AddFolderY, 6 * Col - 4, ButtonH});
		case BrowseButton::ImportPack: return toPx({cx(0), ImportPackY, 6 * Col - 4, ButtonH});
		case BrowseButton::Type: return toPx({cx(16) + 1, 118, BrowseInfoW - 2, 9});
		case BrowseButton::Rename: return toPx(cardButton(0));
		case BrowseButton::Remove: return toPx(cardButton(1));
		case BrowseButton::Load: return toPx({cx(16), 222, BrowseInfoW, ButtonH});
		}
		return {};
	}

	juce::Rectangle<int> Editor::browseSegmentArea(const int _segment) const
	{
		const float segment = browseSegment(browseFilters().size());
		return toPx({cx(6) + static_cast<float>(_segment) * segment, 33, segment, 6});
	}

	juce::Rectangle<int> Editor::selectorItemArea(const int _item) const
	{
		float cw = 0;
		const auto box = selectorBox(cw);
		return toPx({box.getX() + static_cast<float>(_item / 8) * cw, box.getY() + static_cast<float>(_item % 8) * 7.6f, cw,
			7.6f});
	}

	std::vector<int> Editor::browseList() const
	{
		std::vector<int> list;
		if(m_browseTree == 0)
		{
			for(int i = 0; i < Processor::KitSlots; ++i)
				if(!m_processor.kitSlot(i).empty())
					list.push_back(i);
			return list;
		}
		if(m_browseTree == 1)
		{
			const auto& sounds = m_processor.trackSounds().entries();
			for(size_t i = 0; i < sounds.size(); ++i)
				if(m_browseSearch.isEmpty() || sounds[i].name.containsIgnoreCase(m_browseSearch)
					|| m_processor.trackSoundMachine(sounds[i]).containsIgnoreCase(m_browseSearch))
					list.push_back(static_cast<int>(i));
			return list;
		}
		// The sources' entries, then FACTORY's, then IN THIS SET's (browseEntry): ALL and the types hold the first two
		const auto& entries = m_processor.sampleLibrary().entries();
		const auto& factory = m_processor.factorySamples();
		const int segment = lockerSegment();
		const auto add = [&](const SampleLibrary::Entry& _e, const size_t _index)
		{
			if(m_browseSearch.isEmpty() || _e.name.containsIgnoreCase(m_browseSearch))
				list.push_back(static_cast<int>(_index));
		};
		if(segment == SetSegment)
		{
			const auto& set = m_processor.setSamples();
			for(size_t i = 0; i < set.size(); ++i)
				add(set[i], entries.size() + factory.size() + i);
			return list;
		}
		for(size_t i = 0; i < entries.size() + factory.size(); ++i)
		{
			const auto& e = i < entries.size() ? entries[i] : factory[i - entries.size()];
			if(m_browseTree == 4 ? m_browseFilter > 0 && static_cast<int>(e.type) != m_browseFilter - 1
				: segment != AllSegment && e.folder != (segment == FactorySegment ? SampleLibrary::FactoryFolder : segment))
				continue;
			add(e, i);
		}
		return list;
	}

	std::vector<int> Editor::lockerSegments() const
	{
		std::vector<int> segments{AllSegment};
		const int sources = static_cast<int>(m_processor.sampleLibrary().sources().size());
		for(int s = 0; s < std::min(sources, BrowseFolderSegments); ++s)
			segments.push_back(s);
		if(!m_processor.factorySamples().empty())
			segments.push_back(FactorySegment);
		if(!m_processor.setSamples().empty())
			segments.push_back(SetSegment);
		segments.push_back(AddSegment);
		return segments;
	}

	int Editor::lockerSegment() const
	{
		if(m_browseTree != 2)
			return AllSegment;
		const auto segments = lockerSegments();
		return m_browseFilter >= 0 && m_browseFilter < static_cast<int>(segments.size())
			? segments[static_cast<size_t>(m_browseFilter)] : AllSegment;
	}

	const SampleLibrary::Entry& Editor::browseEntry(const int _index) const
	{
		const auto& entries = m_processor.sampleLibrary().entries();
		const auto& factory = m_processor.factorySamples();
		const auto i = static_cast<size_t>(_index);
		return i < entries.size() ? entries[i] : i < entries.size() + factory.size() ? factory[i - entries.size()]
			: m_processor.setSamples()[i - entries.size() - factory.size()];
	}

	juce::StringArray Editor::browseFilters() const
	{
		juce::StringArray filters;
		if(m_browseTree == 4)
		{
			filters.add("ALL");
			for(int t = 0; t < SampleLibrary::Types; ++t)
				filters.add(SampleLibrary::typeName(static_cast<SampleLibrary::Type>(t)));
			return filters;
		}
		const auto& sources = m_processor.sampleLibrary().sources();
		for(const int s : lockerSegments())
			filters.add(s == AllSegment ? juce::String("ALL") : s == FactorySegment ? juce::String("FACTORY")
				: s == SetSegment ? juce::String("IN THIS SET") : s == AddSegment ? juce::String("+")
				: sources[static_cast<size_t>(s)].name.toUpperCase());
		return filters;
	}

	void Editor::buildBrowse()
	{
		for(int tree = 0; tree < 5; ++tree)
		{
			Hit row;
			row.r = browseTreeRow(tree).withHeight(BrowseRowH);
			row.click = [this, tree]
			{
				m_browseTree = tree;
				m_browseItem = m_browseScroll = m_browseFilter = 0;
				m_removeArmed = AllSegment;
			};
			addHit(std::move(row));
		}
		if(m_browseTree <= 1)
		{
			// KITS: a click chooses, a double-click or LOAD KIT loads. TRACK SOUNDS: a click auditions, a double-click
			// loads on the Track shown, a drag onto a Track loads there
			const bool kits = m_browseTree == 0;
			const auto list = browseList();
			const auto& sounds = m_processor.trackSounds().entries();
			const auto load = [this, kits](const int _item, const int _track)
			{
				const auto list = browseList();
				if(_item >= static_cast<int>(list.size()))
					return;
				const int entry = list[static_cast<size_t>(_item)];
				if(kits)
				{
					if(m_processor.loadKit(entry))
						toast("Loaded " + m_processor.kitSlot(entry).name);
					return;
				}
				loadTrackSound(_track, m_processor.trackSounds().entries()[static_cast<size_t>(entry)].file);
			};
			for(int i = 0; i < 16 && m_browseScroll + i < static_cast<int>(list.size()); ++i)
			{
				const int item = m_browseScroll + i, entry = list[static_cast<size_t>(item)];
				Hit row;
				row.r = browseListRow(i, false).withHeight(BrowseRowH);
				const juce::File file = kits ? juce::File() : sounds[static_cast<size_t>(entry)].file;
				row.click = [this, item, kits, file]
				{
					m_browseItem = item;
					if(!kits)
						auditionSound(file);
				};
				row.doubleClick = [this, item, load]
				{
					m_browseItem = item;
					load(item, m_track);
				};
				if(!kits)
				{
					row.drag = [this, entry](float, float) { m_browseDrag = entry; };
					row.end = [this, file]
					{
						m_browseDrag = -1;
						const int k = static_cast<int>((m_press.last.x - 10) / KeyPitch);
						if(m_press.last.y >= 244 && k >= 0 && k < 16)
							loadTrackSound(k, file);
					};
				}
				addHit(std::move(row));
			}
			if(m_browseItem < static_cast<int>(list.size()))
			{
				if(!kits)
				{
					const auto file = sounds[static_cast<size_t>(list[static_cast<size_t>(m_browseItem)])].file;
					Hit replay;
					replay.r = {cx(16), 32, BrowseInfoW, 185};
					replay.click = [this, file] { auditionSound(file); };
					addHit(std::move(replay));
				}
				Hit button;
				button.r = {cx(16), 222, BrowseInfoW, 12.5f};
				button.click = [this, load] { load(m_browseItem, m_track); };
				addHit(std::move(button));
			}
			watch({cx(6), 23, 18 * Col, 216}, [this]
			{
				uint64_t h = mix(0, static_cast<uint64_t>(m_processor.currentKit() + 1));
				h = mix(h, reinterpret_cast<uintptr_t>(m_processor.auditioned()));
				for(int i = 0; i < Processor::KitSlots; ++i)
					h = mix(h, static_cast<uint64_t>(m_processor.kitSlot(i).written.toMilliseconds()));
				return h;
			});
			return;
		}
		if(!browseSamples())
		{
			// MACHINES: its list, scrolled by the wheel, and PUT ON TRACK
			const int n = static_cast<int>(m_processor.machines().size());
			for(int i = 0; i < 16 && m_browseScroll + i < n; ++i)
			{
				Hit row;
				row.r = browseListRow(i, false).withHeight(BrowseRowH);
				const int item = m_browseScroll + i;
				row.click = [this, item] { m_browseItem = item; };
				row.doubleClick = [this, item]
				{
					m_browseItem = item;
					auto* p = trackParameter("machine");
					setNormalised(*p, p->convertTo0to1(static_cast<float>(item)));
				};
				addHit(std::move(row));
			}
			Hit put;
			put.r = {cx(16), 222, BrowseInfoW, 12.5f};
			put.click = [this]
			{
				auto* p = trackParameter("machine");
				setNormalised(*p, p->convertTo0to1(static_cast<float>(m_browseItem)));
				toast(machineName(m_track) + " on Track " + juce::String(m_track + 1));
			};
			addHit(std::move(put));
			watch({cx(6), 23, 18 * Col, 216}, [this]
			{
				return static_cast<uint64_t>(choiceOf(trackParameter("machine")->getParameterID()));
			});
			return;
		}

		// The samples: the Locker scanned once Browse shows them. ADD FOLDER links a folder, IMPORT PACK copies one
		auto& library = m_processor.sampleLibrary();
		library.scanOnce();
		Hit add;
		add.r = {cx(0), AddFolderY, 6 * Col - 4, ButtonH};
		add.click = [this] { chooseFolder(false); };
		addHit(std::move(add));
		Hit pack;
		pack.r = {cx(0), ImportPackY, 6 * Col - 4, ButtonH};
		pack.click = [this] { chooseFolder(true); };
		addHit(std::move(pack));
		watch({cx(0), 150, 6 * Col - 4, 48}, [this]
		{
			return mix(static_cast<uint64_t>(m_processor.importing() ? 1 : 0), m_processor.sampleLibrary().generation());
		});
		const auto filters = browseFilters();
		const auto segments = lockerSegments();
		const float segment = browseSegment(filters.size());
		for(int i = 0; i < filters.size(); ++i)
		{
			Hit s;
			s.r = {cx(6) + static_cast<float>(i) * segment, 33, segment, 6};
			const bool plus = m_browseTree == 2 && segments[static_cast<size_t>(i)] == AddSegment;
			s.click = [this, i, plus]
			{
				if(plus)
				{
					chooseFolder(false);
					return;
				}
				m_browseFilter = i;
				// LOCKER: a source chosen shows its card until a sample is
				m_browseItem = m_browseTree == 2 ? -1 : 0;
				m_browseScroll = 0;
				m_removeArmed = AllSegment;
			};
			addHit(std::move(s));
		}
		// A click auditions, a double-click loads on the Track shown, a drag onto a Track loads there
		const auto list = browseList();
		for(int i = 0; i < 15 && m_browseScroll + i < static_cast<int>(list.size()); ++i)
		{
			const int item = m_browseScroll + i, entry = list[static_cast<size_t>(item)];
			const auto e = browseEntry(entry);
			Hit row;
			row.r = browseListRow(i, true).withHeight(BrowseRowH);
			row.click = [this, item, e]
			{
				m_browseItem = item;
				audition(e);
			};
			row.doubleClick = [this, item, e]
			{
				m_browseItem = item;
				loadSample(m_track, e);
			};
			row.drag = [this, entry](float, float) { m_browseDrag = entry; };
			row.end = [this, e]
			{
				m_browseDrag = -1;
				const int k = static_cast<int>((m_press.last.x - 10) / KeyPitch);
				if(m_press.last.y >= 244 && k >= 0 && k < 16)
					loadSample(k, e);
			};
			addHit(std::move(row));
		}
		if(m_browseItem >= 0 && m_browseItem < static_cast<int>(list.size()))
		{
			const auto e = browseEntry(list[static_cast<size_t>(m_browseItem)]);
			Hit replay;
			replay.r = {cx(16), 32, BrowseInfoW, CardButtonY - 33};
			replay.click = [this, e] { audition(e); };
			addHit(std::move(replay));
			Hit load;
			load.r = {cx(16), 222, BrowseInfoW, 12.5f};
			load.click = [this, e] { loadSample(m_track, e); };
			addHit(std::move(load));
			// TYPE, its row in the panel (drawBrowseSample): a click opens the types, the one chosen kept as the user's,
			// by fingerprint, a factory sample's too; one of the set has no file to keep it by
			const auto fingerprint = e.fingerprint;
			Hit type;
			type.r = {cx(16) + 1, 118, BrowseInfoW - 2, 9};
			type.click = [this, fingerprint]
			{
				juce::StringArray types;
				for(int t = 0; t < SampleLibrary::Types; ++t)
					types.add(SampleLibrary::typeName(static_cast<SampleLibrary::Type>(t)));
				openSelector(cx(16) + 29, 120, types, [this, fingerprint](const int _t)
				{
					const auto* e = m_processor.sampleLibrary().find(fingerprint);
					for(const auto& f : m_processor.factorySamples())
						if(f.fingerprint == fingerprint)
							e = &f;
					return e && static_cast<int>(e->type) == _t;
				}, [this, fingerprint](const int _t)
				{
					if(!m_processor.sampleLibrary().setType(fingerprint, static_cast<SampleLibrary::Type>(_t)))
						toast("The type could not be kept");
				});
			};
			if(e.folder != SampleLibrary::SetFolder)
				addHit(std::move(type));
			// RENAME the sample, by fingerprint; KEEP IN LOCKER one of the set
			Hit rename;
			rename.r = cardButton(0);
			rename.click = [this, fingerprint, name = e.name]
			{
				startRename(name, [this, fingerprint](const juce::String& _name)
				{
					if(!m_processor.sampleLibrary().renameSample(fingerprint, _name))
						toast("The name could not be kept");
				});
			};
			if(e.folder != SampleLibrary::SetFolder)
				addHit(std::move(rename));
			Hit keep;
			keep.r = cardButton(1);
			keep.click = [this, fingerprint, name = e.name]
			{
				juce::String error;
				if(!m_processor.keepInLocker(fingerprint, error))
				{
					toast(error);
					return;
				}
				m_browseFilter = m_browseItem = m_browseScroll = 0;
				toast(name + " kept in the Locker, in FROM SETS");
			};
			if(e.folder == SampleLibrary::SetFolder)
				addHit(std::move(keep));
		}
		else if(const int s = lockerSegment(); m_browseItem < 0 && s != AllSegment && s != SetSegment)
		{
			// The source's card: RENAME, and REMOVE SOURCE, a second click where it erases the Locker's copy
			if(s >= 0)
			{
				Hit rename;
				rename.r = cardButton(0);
				rename.click = [this, s]
				{
					startRename(m_processor.sampleLibrary().sources()[static_cast<size_t>(s)].name, [this, s](const juce::String& _n)
					{
						if(!m_processor.sampleLibrary().renameSource(s, _n))
							toast("The name could not be kept");
					});
				};
				addHit(std::move(rename));
			}
			Hit remove;
			remove.r = cardButton(1);
			remove.click = [this, s]
			{
				auto& library = m_processor.sampleLibrary();
				const bool erases = s == FactorySegment
					|| library.sources()[static_cast<size_t>(s)].kind == SampleLibrary::Kind::Imported;
				if(erases && m_removeArmed != s)
				{
					m_removeArmed = s;
					return;
				}
				m_removeArmed = AllSegment;
				const auto name = s == FactorySegment ? juce::String("FACTORY") : library.sources()[static_cast<size_t>(s)].name;
				const bool removed = s == FactorySegment ? m_processor.removeFactory() : library.removeSource(s);
				if(!removed)
				{
					toast(name + " could not be removed");
					return;
				}
				m_browseFilter = m_browseItem = m_browseScroll = 0;
				toast(erases ? name + " left the Locker, its copy erased" : name + " left the Locker, its files stay on the disk");
			};
			if(s >= 0 || m_processor.libraryFromWords())
				addHit(std::move(remove));
		}
		// The Track's sample and what the audition holds
		watch({cx(6), 23, 18 * Col, 216}, [this]
		{
			uint64_t h = mix(0, static_cast<uint64_t>(m_processor.sampleFingerprint(m_track).hashCode64()));
			h = mix(h, reinterpret_cast<uintptr_t>(m_processor.auditioned()));
			return mix(h, m_processor.sampleLibrary().scanning() ? 1 : 0);
		});
	}

	void Editor::drawBrowse(Painter& _p)
	{
		drawHeader(_p, "BROWSE", {});
		const bool samples = browseSamples();
		const auto& c = browseHue(m_browseTree);
		float x = cx(0);
		drawBlockTitle(_p, {x - 2, 23, 6 * Col + 2, "LIBRARY", "bl", &hue::t});
		for(int i = 0; i < 5; ++i)
		{
			const auto r = browseTreeRow(i);
			const auto& h = browseHue(i);
			const bool on = i == m_browseTree;
			_p.rect(r.getX(), r.getY(), r.getWidth(), r.getHeight(), on ? h.c[1] : beat(i), 0.5f);
			_p.rect(r.getX(), r.getY() + 0.6f, 0.7f, BrowseRowH - 1.9f, h.c[0], 0.3f);
			_p.text(r.getX() + 2.4f, r.getY() + 8, browseTreeNames[i], 3.2f, on ? h.c[4] : colour::mid, on ? 600 : 500);
		}
		if(samples)
		{
			// The Locker: what it copied into the data folder, its sources; then its two gestures
			const auto& library = m_processor.sampleLibrary();
			int64_t copied = m_processor.factoryBytes();
			for(size_t s = 0; s < library.sources().size(); ++s)
				if(library.sources()[s].kind == SampleLibrary::Kind::Imported)
					copied += library.sourceBytes(static_cast<int>(s));
			const int sources = static_cast<int>(library.sources().size()) + (m_processor.factorySamples().empty() ? 0 : 1);
			_p.text(x, 159, "LOCKER  " + bytesText(copied) + " IMPORTED,", 2.8f, colour::mid, 600);
			_p.text(x, 164.6f, juce::String(sources) + (sources == 1 ? " SOURCE" : " SOURCES"), 2.8f, colour::mid, 600);
			const bool reading = m_processor.importing();
			drawButton(_p, x, AddFolderY, 6 * Col - 4, ButtonH, reading ? "READING THE FOLDER" : "ADD FOLDER", false,
				!reading);
			drawButton(_p, x, ImportPackY, 6 * Col - 4, ButtonH, reading ? "READING THE FOLDER" : "IMPORT PACK", false,
				!reading);
		}
		if(samples || m_browseTree == 1)
		{
			const juce::String hints[]{"CLICK: LISTEN", "DOUBLE CLICK: LOAD ON TRACK " + juce::String(m_track + 1),
				"DRAG: DROP ON ANY TRACK"};
			for(int i = 0; i < 3; ++i)
				_p.text(x, 202 + static_cast<float>(i) * 5.6f, hints[i], 2.6f, colour::dim);
		}

		x = cx(6);
		if(m_browseTree <= 1)
		{
			// The Bank's Kits, or the Track sounds' Library: a row each, the Kit played or the sound heard marked
			const bool kits = m_browseTree == 0;
			const auto list = browseList();
			const auto& sounds = m_processor.trackSounds().entries();
			drawBlockTitle(_p, {x - 2, 23, 10 * Col + 2, browseTreeNames[m_browseTree], "bs", &c});
			_p.text(x + BrowseListW, 29.2f, juce::String(static_cast<int>(list.size())), 2.8f, c.c[4], 500, Align::Right);
			for(int i = 0; i < 16 && m_browseScroll + i < static_cast<int>(list.size()); ++i)
			{
				const int item = m_browseScroll + i, entry = list[static_cast<size_t>(item)];
				const auto r = browseListRow(i, false);
				const bool on = item == m_browseItem;
				_p.rect(x, r.getY(), BrowseListW, r.getHeight(), on ? c.c[1] : beat(i), 0.5f);
				if(on)
					_p.rect(x, r.getY() + 0.6f, 0.7f, BrowseRowH - 1.9f, c.c[0], 0.3f);
				const auto name = kits ? slotNumber(entry) + "  " + m_processor.kitSlot(entry).name
					: sounds[static_cast<size_t>(entry)].name;
				_p.text(x + 2.4f, r.getY() + 8, name, 3.2f, on ? colour::ink : colour::mid, on ? 600 : 500);
				const auto tag = kits ? (entry == m_processor.currentKit() ? juce::String("PLAYING") : juce::String())
					: m_processor.trackSoundMachine(sounds[static_cast<size_t>(entry)]).toUpperCase();
				_p.text(x + BrowseListW - 3, r.getY() + 8, tag, 2.8f, kits ? c.c[4] : colour::dim, 500, Align::Right);
			}
			if(list.empty())
				_p.text(x + BrowseListW / 2, 126, kits ? "NO KIT SAVED: SAVE ONE ON THE KITS PAGE"
					: m_browseSearch.isNotEmpty() ? "NOTHING MATCHES" : "NO SOUND SAVED: SAVE SOUND ON THE KITS PAGE", 2.8f,
					colour::dim, 500, Align::Centre);
			x = cx(16);
			const bool chosen = m_browseItem < static_cast<int>(list.size());
			const int entry = chosen ? list[static_cast<size_t>(m_browseItem)] : -1;
			const auto title = !chosen ? juce::String() : kits ? m_processor.kitSlot(entry).name.toUpperCase()
				: sounds[static_cast<size_t>(entry)].name.toUpperCase();
			drawBlockTitle(_p, {x - 2, 23, 8 * Col + 2, title, "bi", &c});
			_p.frame(x, 32, BrowseInfoW, 185, c.c[1], 1.2f, 0.4f);
			if(chosen && kits)
				drawBrowseKit(_p, x, c);
			else if(chosen)
				drawBrowseSound(_p, x, c);
			drawButton(_p, x, 222, BrowseInfoW, 12.5f, kits ? juce::String("LOAD KIT")
				: "LOAD ON TRACK " + juce::String(m_track + 1), chosen, chosen);
			return;
		}
		if(!samples)
		{
			const auto& machines = m_processor.machines();
			const int n = static_cast<int>(machines.size());
			const int current = choiceOf(trackParameter("machine")->getParameterID());
			drawBlockTitle(_p, {x - 2, 23, 10 * Col + 2, "MACHINES", "bm", &hue::k});
			_p.text(x + BrowseListW, 29.2f, juce::String(n), 2.8f, hue::k.c[4], 500, Align::Right);
			const auto* choice = dynamic_cast<const juce::AudioParameterChoice*>(trackParameter("machine"));
			for(int i = 0; i < 16 && m_browseScroll + i < n; ++i)
			{
				const int item = m_browseScroll + i;
				const auto r = browseListRow(i, false);
				const bool on = item == m_browseItem;
				_p.rect(x, r.getY(), BrowseListW, r.getHeight(), on ? hue::k.c[1] : beat(i), 0.5f);
				if(on)
					_p.rect(x, r.getY() + 0.6f, 0.7f, BrowseRowH - 1.9f, hue::k.c[0], 0.3f);
				_p.text(x + 2.4f, r.getY() + 8, choice->choices[item], 3.2f, on ? colour::ink : colour::mid, on ? 600 : 500);
				if(item == current)
					_p.text(x + BrowseListW - 3, r.getY() + 8, "ON TRACK", 2.8f, hue::k.c[4], 500, Align::Right);
			}
			x = cx(16);
			const auto name = m_browseItem < n ? choice->choices[m_browseItem].toUpperCase() : juce::String();
			drawBlockTitle(_p, {x - 2, 23, 8 * Col + 2, name, "bi", &hue::k});
			_p.frame(x, 32, BrowseInfoW, 185, hue::k.c[1], 1.2f, 0.4f);
			_p.text(x + BrowseInfoW / 2, 126, "NO PREVIEW YET", 2.8f, colour::dim, 500, Align::Centre);
			// Its family, what says how the machine makes its sound (ticket 27)
			if(const char* family = m_browseItem < n ? names::family(machines[static_cast<size_t>(m_browseItem)]) : nullptr)
			{
				_p.text(x + 3, 208, "FAMILY", 2.8f, colour::mid);
				_p.text(x + 30, 208, family, 3.2f, colour::ink, 600);
			}
			drawButton(_p, x, 222, BrowseInfoW, 12.5f, "PUT ON TRACK " + juce::String(m_track + 1), true);
			return;
		}

		// The list of samples: its title, SEARCH (a TextEditor over the title line), the segments, the rows
		const auto& library = m_processor.sampleLibrary();
		const auto list = browseList();
		drawBlockTitle(_p, {x - 2, 23, 10 * Col + 2, browseTreeNames[m_browseTree], "bs", &c});
		_p.text(x + BrowseListW, 29.2f, juce::String(static_cast<int>(list.size())), 2.8f, c.c[4], 500, Align::Right);
		// A source's name cut to its segment
		auto filters = browseFilters();
		const float segment = browseSegment(filters.size());
		for(auto& f : filters)
			while(f.length() > 1 && _p.textWidth(f, 3.2f, 600) > segment - 2)
				f = f.dropLastCharacters(1);
		drawSegments(_p, x, 33, segment, filters, m_browseFilter, &c);
		const auto played = m_processor.sampleFingerprint(m_track);
		const auto sourceName = [&](const SampleLibrary::Entry& _e)
		{
			return _e.folder == SampleLibrary::FactoryFolder ? juce::String("FACTORY")
				: _e.folder == SampleLibrary::SetFolder ? juce::String("IN THIS SET")
				: library.sources()[static_cast<size_t>(_e.folder)].name.toUpperCase();
		};
		for(int i = 0; i < 15 && m_browseScroll + i < static_cast<int>(list.size()); ++i)
		{
			const int item = m_browseScroll + i;
			const auto& e = browseEntry(list[static_cast<size_t>(item)]);
			const auto r = browseListRow(i, true);
			const bool on = item == m_browseItem;
			_p.rect(x, r.getY(), BrowseListW, r.getHeight(), on ? c.c[1] : beat(i), 0.5f);
			if(on)
				_p.rect(x, r.getY() + 0.6f, 0.7f, BrowseRowH - 1.9f, c.c[0], 0.3f);
			_p.text(x + 2.4f, r.getY() + 8, e.name, 3.2f, on ? colour::ink : colour::mid, on ? 600 : 500);
			const bool here = e.fingerprint == played;
			const auto tag = here ? juce::String("ON TRACK") : m_browseTree == 2 ? juce::String(SampleLibrary::typeName(e.type))
				: sourceName(e);
			_p.text(x + BrowseListW - 3, r.getY() + 8, tag, 2.8f, here ? c.c[4] : colour::dim, 500, Align::Right);
		}
		if(list.empty())
		{
			const auto why = library.sources().empty() && m_processor.factorySamples().empty()
				? "DROP A FOLDER OF SAMPLES HERE, OR ADD FOLDER" : library.scanning() ? "SCANNING THE LOCKER"
				: m_browseSearch.isEmpty() ? "NO WAV, AIFF OR FLAC FILE HERE" : "NOTHING MATCHES";
			_p.text(x + BrowseListW / 2, 126, why, 2.8f, colour::dim, 500, Align::Centre);
		}

		x = cx(16);
		const bool chosen = m_browseItem >= 0 && m_browseItem < static_cast<int>(list.size());
		const auto* e = chosen ? &browseEntry(list[static_cast<size_t>(m_browseItem)]) : nullptr;
		if(e)
		{
			drawBlockTitle(_p, {x - 2, 23, 8 * Col + 2, m_rename.isVisible() ? juce::String() : e->name.toUpperCase(), "bi", &c});
			_p.frame(x, 32, BrowseInfoW, 185, c.c[1], 1.2f, 0.4f);
			drawBrowseSample(_p, x, c);
		}
		else
			drawLockerSource(_p, x, c);
		drawButton(_p, x, 222, BrowseInfoW, 12.5f, "LOAD ON TRACK " + juce::String(m_track + 1), e != nullptr, e != nullptr);
	}

	// The card of the segment chosen while no sample is: ALL the Locker's, a source's, FACTORY's or IN THIS SET's
	void Editor::drawLockerSource(Painter& _p, const float _x, const Hue& _hue)
	{
		const auto& library = m_processor.sampleLibrary();
		const int s = lockerSegment();
		std::vector<const SampleLibrary::Entry*> held;
		for(const int i : browseList())
			held.push_back(&browseEntry(i));
		if(m_browseTree != 2 || held.empty() && s == AllSegment)
		{
			drawBlockTitle(_p, {_x - 2, 23, 8 * Col + 2, {}, "bi", &_hue});
			_p.frame(_x, 32, BrowseInfoW, 185, _hue.c[1], 1.2f, 0.4f);
			return;
		}
		const auto* source = s >= 0 ? &library.sources()[static_cast<size_t>(s)] : nullptr;
		const juce::String name = s == AllSegment ? "LOCKER" : s == FactorySegment ? "FACTORY" : s == SetSegment
			? "IN THIS SET" : source->name.toUpperCase();
		const juce::String kind = s == AllSegment ? "" : s == FactorySegment ? "FACTORY" : s == SetSegment ? "IN THIS SET"
			: source->kind == SampleLibrary::Kind::Imported ? "IMPORTED" : "LINKED";
		drawBlockTitle(_p, {_x - 2, 23, 8 * Col + 2, m_rename.isVisible() ? juce::String() : name, "bi", &_hue});
		if(!m_rename.isVisible())
			_p.text(_x + BrowseInfoW, 29.2f, kind, 2.8f, _hue.c[4], 500, Align::Right);
		_p.frame(_x, 32, BrowseInfoW, 185, _hue.c[1], 1.2f, 0.4f);

		// Its samples by type, the most first
		std::array<int, SampleLibrary::Types> types{};
		int64_t bytes = 0;
		for(const auto* e : held)
		{
			++types[static_cast<size_t>(e->type)];
			bytes += e->bytes;
		}
		juce::StringArray typeText;
		for(int n = 0; n < 3; ++n)
		{
			const auto most = std::max_element(types.begin(), types.end());
			if(*most == 0)
				break;
			typeText.add(juce::String(SampleLibrary::typeName(static_cast<SampleLibrary::Type>(most - types.begin())))
				+ " " + juce::String(*most));
			*most = 0;
		}
		const bool erases = s == FactorySegment || (source && source->kind == SampleLibrary::Kind::Imported);
		const int64_t space = s == FactorySegment ? m_processor.factoryBytes() : bytes;
		const juce::String path = s == AllSegment ? juce::String(library.sources().size()) + " FOLDERS"
			+ (m_processor.factorySamples().empty() ? "" : ", THE FACTORY BANK")
			: s == FactorySegment ? (m_processor.libraryFromWords() ? "THE FACTORY BANK, KEPT BY THE LOCKER"
				: "THE FACTORY BANK, READ FROM YOUR FLASH IMAGE")
			: s == SetSegment ? juce::String("THE SAMPLES THIS SET HOLDS") : source->folder.getFullPathName();
		const juce::String spaceText = s == SetSegment ? "IN THE SET, " + bytesText(bytes)
			: s == FactorySegment && !m_processor.libraryFromWords() ? juce::String("READ FROM THE IMAGE")
			: s == AllSegment || erases ? "COPIED, " + bytesText(space) : "LINKED, " + bytesText(space);
		std::vector<std::pair<juce::String, juce::String>> rows{{"PATH", path},
			{"SAMPLES", juce::String(static_cast<int>(held.size()))}, {"SPACE", spaceText},
			{"TYPES", typeText.joinIntoString("  ")}};
		if(s == FactorySegment)
			rows.emplace_back("ALSO", "GIVES SOME MACHINES THEIR WAVES");
		if(s == SetSegment)
			rows.emplace_back("KEEP", "CHOOSE A SAMPLE, THEN KEEP IN LOCKER");
		if(m_removeArmed == s && erases)
			rows.emplace_back("REMOVE", "ERASES " + bytesText(space) + " FROM THE LOCKER");
		float y = 48;
		for(const auto& [label, value] : rows)
		{
			auto v = value;
			while(v.length() > 1 && _p.textWidth(v, 3.2f, 600) > BrowseInfoW - 33)
				v = "..." + v.substring(4);
			_p.text(_x + 3, y, label, 2.8f, colour::mid);
			_p.text(_x + 30, y, v, 3.2f, label == "REMOVE" ? colour::lock : colour::ink, 600);
			y += 9;
		}
		if(s == AllSegment || s == SetSegment)
			return;
		const auto b0 = cardButton(0), b1 = cardButton(1);
		drawButton(_p, b0.getX(), b0.getY(), b0.getWidth(), b0.getHeight(), "RENAME", false, s >= 0);
		drawButton(_p, b1.getX(), b1.getY(), b1.getWidth(), b1.getHeight(), m_removeArmed == s ? "REMOVE: CLICK AGAIN"
			: "REMOVE SOURCE", m_removeArmed == s, s >= 0 || m_processor.libraryFromWords());
	}

	// The sample chosen: its sound as the audition plays it, once heard, then what the Locker knows of it
	void Editor::drawBrowseSample(Painter& _p, const float _x, const Hue& _hue)
	{
		const auto& library = m_processor.sampleLibrary();
		const auto list = browseList();
		const auto& e = browseEntry(list[static_cast<size_t>(m_browseItem)]);
		const float w = BrowseInfoW - 4, top = 36, h = 76, mid = top + h / 2;
		const auto* heard = m_processor.auditioned();
		if(heard && (e.folder < 0 ? m_auditionRef == e.fingerprint : m_auditionFile == e.file) && !heard->pcm.empty())
			drawWave(_p, _x + 2, mid, w, h, heard->pcm, _hue.c[4]);
		else
			_p.text(_x + BrowseInfoW / 2, mid + 1, "CLICK TO LISTEN", 2.8f, colour::dim, 500, Align::Centre);

		const juce::String channels = e.channels == 1 ? "MONO" : e.channels == 2 ? "STEREO" : juce::String(e.channels);
		const bool set = e.folder == SampleLibrary::SetFolder, factory = e.folder == SampleLibrary::FactoryFolder;
		const juce::String rows[][2]{
			// Guessed by its name or its sound until the user sets it, a click on the row
			{"TYPE", juce::String(SampleLibrary::typeName(e.type)) + (e.typeFrom == SampleLibrary::TypeFrom::User
				? "" : ", GUESSED")},
			{"LENGTH", lengthText(e.length, e.rate) + (static_cast<double>(e.length) / e.rate
				> Sample::MaxLength / 44100.0 ? ", 12-BIT GRAIN PLAYS 1.29 S" : "")},
			{"RATE", juce::String(e.rate / 1000, 1) + " KHZ"},
			{"CHANNELS", channels},
			{"SOURCE", factory ? juce::String("FACTORY") : set ? juce::String("IN THIS SET")
				: library.sources()[static_cast<size_t>(e.folder)].name},
			// A factory sample: its slot of the bank; one of the set: no file
			{"FILE", factory ? "FACTORY BANK, SLOT " + e.fingerprint.fromFirstOccurrenceOf(":", false, false)
				: set ? juce::String("NONE: THE SET HOLDS IT") : e.file.getRelativePathFrom(
					library.sources()[static_cast<size_t>(e.folder)].folder)},
			{"FINGERPRINT", e.fingerprint.toUpperCase()},
		};
		float y = top + h + 12;
		for(const auto& row : rows)
		{
			_p.text(_x + 3, y, row[0], 2.8f, colour::mid);
			_p.text(_x + 30, y, row[1], 3.2f, colour::ink, 600);
			y += 9;
		}
		if(e.fingerprint == m_processor.sampleFingerprint(m_track))
			_p.text(_x + 3, y + 3, "ON TRACK " + juce::String(m_track + 1), 2.8f, _hue.c[4], 600);
		const auto b0 = cardButton(0), b1 = cardButton(1);
		drawButton(_p, b0.getX(), b0.getY(), b0.getWidth(), b0.getHeight(), "RENAME", false, !set);
		if(set)
			drawButton(_p, b1.getX(), b1.getY(), b1.getWidth(), b1.getHeight(), "KEEP IN LOCKER", false, true);
	}

	// The Track sound chosen: its Hit as the audition rendered it, once heard, then its machine and file
	void Editor::drawBrowseSound(Painter& _p, const float _x, const Hue& _hue)
	{
		const auto& e = m_processor.trackSounds().entries()[static_cast<size_t>(browseList()[static_cast<size_t>(
			m_browseItem)])];
		const float w = BrowseInfoW - 4, top = 36, h = 76, mid = top + h / 2;
		const auto* heard = m_processor.auditioned();
		if(heard && m_auditionFile == e.file && !heard->pcm.empty())
			drawWave(_p, _x + 2, mid, w, h, heard->pcm, _hue.c[4]);
		else
			_p.text(_x + BrowseInfoW / 2, mid + 1, "CLICK TO LISTEN", 2.8f, colour::dim, 500, Align::Centre);
		const juce::String rows[][2]{{"MACHINE", m_processor.trackSoundMachine(e)}, {"FILE", e.file.getFileName()},
			{"SAVED", e.written.toString(true, true, false)}};
		float y = top + h + 12;
		for(const auto& row : rows)
		{
			_p.text(_x + 3, y, row[0], 2.8f, colour::mid);
			_p.text(_x + 30, y, row[1], 3.2f, colour::ink, 600);
			y += 9;
		}
	}

	// The Kit chosen: its Slot and whether it plays
	void Editor::drawBrowseKit(Painter& _p, const float _x, const Hue& _hue)
	{
		const int slot = browseList()[static_cast<size_t>(m_browseItem)];
		const auto& s = m_processor.kitSlot(slot);
		const juce::String rows[][2]{{"SLOT", slotNumber(slot)}, {"SAVED", s.written.toString(true, true, false)},
			{"STATE", slot == m_processor.currentKit() ? (m_processor.kitDiffers() ? "PLAYING, EDITED" : "PLAYING")
				: "DOUBLE CLICK OR LOAD KIT"}};
		float y = 48;
		for(const auto& row : rows)
		{
			_p.text(_x + 3, y, row[0], 2.8f, colour::mid);
			_p.text(_x + 30, y, row[1], 3.2f, slot == m_processor.currentKit() ? _hue.c[4] : colour::ink, 600);
			y += 9;
		}
	}

	// ---- the Menu panel, over the third row: WINDOW and AUDIO; the samples are Browse's LOCKER ----

	void Editor::buildMenu()
	{
		const float px = 204.5f, py = 165.2f;
		Hit outside;
		outside.r = {ScreenX, ScreenY, ScreenW, ScreenH};
		outside.click = [this] { showMenu(false); };
		outside.cursor = juce::MouseCursor::NormalCursor;
		addHit(std::move(outside));
		Hit panel;
		panel.r = {px, py, ScreenX + ScreenW - px, ScreenY + ScreenH - py};
		panel.cursor = juce::MouseCursor::NormalCursor;
		addHit(std::move(panel));
		const float r1 = 178.2f;
		Hit size;
		size.r = {cx(10) - 1, r1 - 0.5f, 2 * Col - 1, 13};
		size.click = [this, r1]
		{
			openSelector(cx(10) - 1, r1 - 4, {"1280 X 800", "1600 X 1000"},
				[this](const int _i) { return (getWidth() == Width) == (_i == 1); },
				[this](const int _i)
				{
					if(_i)
						setSize(Width, Height);
					else
						setSize(SmallWidth, SmallHeight);
				});
		};
		addHit(std::move(size));
		Cell os = cellOf(m_processor.parameter("oversampling"), "OVERSAMPLING", cx(13), r1, 2 * Col, hue::k, "menu");
		os.labels = {"OFF", "2X", "4X"};
		addCell(os);
	}

	void Editor::drawMenu(Painter& _p)
	{
		const float px = 204.5f, py = 165.2f, pw = ScreenX + ScreenW - px, ph = ScreenY + ScreenH - py, y = 169.2f,
			r1 = y + 9;
		_p.rect(px, py, pw, ph, colour::panel);
		_p.rect(px, py, pw, 0.3f, colour::panelLine);
		_p.rect(px, py, 0.3f, ph, colour::panelLine);
		const auto head = [&](const float _c, const float _span, const juce::String& _n, const Hue& _h)
		{
			_p.rect(cx(_c), y, _span * Col - 4, 0.4f, _h.c[1]);
			_p.text(cx(_c), y + 6.2f, _n, 3.2f, _h.c[4], 600);
		};
		const auto cell = [&](const float _c, const juce::String& _n, const juce::String& _v, const juce::Colour _col)
		{
			_p.text(cx(_c), r1 + 3.3f, _n, 3.2f, colour::mid);
			_p.text(cx(_c), r1 + 10, _v, 4, _col, 600);
		};
		head(10, 3, "WINDOW", hue::w);
		cell(10, "SIZE", getWidth() == Width ? "1600 X 1000" : "1280 X 800", colour::ink);
		head(13, 5, "AUDIO", hue::k);
		cell(15, "96 KHZ NATIVE", "OFF", colour::dim);
		for(const auto& c : m_cells)
			if(c.section == "menu")
				drawCell(_p, c);
	}

	// ---- the selector: eight entries a column, under the cell or above it ----

	juce::Rectangle<float> Editor::selectorBox(float& _columnWidth) const
	{
		const int n = m_selector.items.size();
		const int rows = std::min(8, n), cols = (n + 7) / 8;
		int longest = 0;
		for(const auto& s : m_selector.items)
			longest = std::max(longest, s.length());
		_columnWidth = std::max(17.0f, static_cast<float>(longest) * 2 + 4);
		const float h = 7.6f, wd = static_cast<float>(cols) * _columnWidth, ht = static_cast<float>(rows) * h;
		const float a = juce::jlimit(ScreenX + 4, ScreenX + ScreenW - wd - 4, m_selector.x);
		const float b = std::max(ScreenY + 4, m_selector.y + 16 + ht <= ScreenY + ScreenH - 4 ? m_selector.y + 16
			: m_selector.y - ht - 1);
		return {a, b, wd, ht};
	}

	void Editor::buildSelector()
	{
		Hit outside;
		outside.r = {ScreenX, ScreenY, ScreenW, ScreenH};
		outside.click = [this] { m_selector.open = false; };
		outside.cursor = juce::MouseCursor::NormalCursor;
		outside.keepsLocks = true;
		addHit(std::move(outside));
		float cw = 0;
		const auto box = selectorBox(cw);
		for(int i = 0; i < m_selector.items.size(); ++i)
		{
			Hit item;
			item.r = {box.getX() + static_cast<float>(i / 8) * cw, box.getY() + static_cast<float>(i % 8) * 7.6f, cw, 7.6f};
			item.keepsLocks = true;
			item.click = [this, i]
			{
				if(m_selector.disabled && m_selector.disabled(i))
					return;
				m_selector.choose(i);
				if(!m_selector.stays)
					m_selector.open = false;
				repaint();
			};
			addHit(std::move(item));
		}
	}

	void Editor::drawSelector(Painter& _p)
	{
		float cw = 0;
		const auto box = selectorBox(cw);
		_p.rect(box.getX() - 1, box.getY() - 1, box.getWidth() + 2, box.getHeight() + 2, colour::panel, 1);
		_p.frame(box.getX() - 1, box.getY() - 1, box.getWidth() + 2, box.getHeight() + 2, colour::panelLine, 1, 0.3f);
		for(int i = 0; i < m_selector.items.size(); ++i)
		{
			const float x = box.getX() + static_cast<float>(i / 8) * cw, y = box.getY() + static_cast<float>(i % 8) * 7.6f;
			const bool on = m_selector.current && m_selector.current(i);
			const bool dis = m_selector.disabled && m_selector.disabled(i);
			_p.rect(x + 0.3f, y + 0.3f, cw - 0.6f, 7, on ? hue::t.c[3] : colour::cellBack, 0.6f);
			_p.text(x + cw / 2, y + 3.8f + 1.2f, m_selector.items[i], 2.8f, dis ? juce::Colour(0xff4E4E4C) : on
				? hue::t.c[4] : colour::mid, on ? 600 : 500, Align::Centre);
		}
	}
}
