#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

// What the Lockstep editor draws with (.scratch/elektron-drums/concepts/t.html, t-ui-spec.md, t-style-guide.md): the
// unit's millimetres, the palette, a painter that writes in millimetres, and the screen's model: value cells bound to
// host parameters, the areas the mouse acts on, and the areas whose state the timer watches to repaint them alone.
namespace elektron::drums::ui
{
	// The unit, in mm: 533.33 x 333.33, 3 px/mm at 1600 x 1000
	constexpr float UnitWidth = 533.33f, UnitHeight = 333.33f;
	constexpr float ScreenX = 12, ScreenY = 12, ScreenW = 466, ScreenH = 230.5f;
	constexpr float Col = 19.25f;	// the screen's 24 columns, from x 16
	constexpr float cx(const float _k) { return 16 + _k * Col; }
	constexpr float KeyPitch = (509.33f - 28) / 15, KeyWidth = 28;	// the 16 Tracks under the screen

	// A section's hue: the ring, three darks (rule, fill, cell), the light
	struct Hue
	{
		juce::Colour c[5];
	};

	namespace colour
	{
		const juce::Colour ink{0xffECEBE4}, mid{0xffA6A6A0}, dim{0xff8C8C87}, faint{0xff2C2D2F}, grey{0xff9C9690};
		const juce::Colour lock{0xffF2483C}, lockLight{0xffE8948A}, base{0xff8E8C88}, cellBack{0xff141517};
		const juce::Colour meterGreen{0xff7CC58A}, meterAmber{0xffD3AC4A}, meterRed{0xffD9553F}, unlit{0xff222325};
		const juce::Colour button{0xff1F2022}, panel{0xff0B0C0D}, panelLine{0xff3A3B3D}, rule{0xff4A4B4D};
	}

	namespace hue
	{
		extern const Hue g, k, a, b, s, p, y, m, v, w, t;
	}

	// Writes in mm on a Graphics scaled to the window. Text at the three sizes of the style guide (cap height 4.0, 3.2
	// and 2.8 mm), at a baseline
	class Painter
	{
	public:
		Painter(juce::Graphics& _g, float _scale);

		enum class Align { Left, Centre, Right };
		void text(float _x, float _baseline, const juce::String& _text, float _cap, juce::Colour _colour,
			int _weight = 500, Align _align = Align::Left) const;
		float textWidth(const juce::String& _text, float _cap, int _weight = 500) const;
		void rect(float _x, float _y, float _w, float _h, juce::Colour _colour, float _radius = 0) const;
		void frame(float _x, float _y, float _w, float _h, juce::Colour _colour, float _radius, float _line) const;
		void line(float _x0, float _y0, float _x1, float _y1, juce::Colour _colour, float _width) const;
		void poly(const std::vector<juce::Point<float>>& _points, juce::Colour _colour, float _width) const;
		void dot(float _x, float _y, float _r, juce::Colour _fill, juce::Colour _ring, float _line) const;

		juce::Graphics& g;
		const float scale;

		// The face the style guide names is Barlow Semi Condensed; until its file is in the repository, the nearest
		// one Windows has (Bahnschrift SemiCondensed), or the default sans narrowed. Its name, for the test's report
		static juce::String faceName();
		// The face at a cap height in mm, for a mm-scaled Graphics: a component in pixels scales its height
		static juce::Font font(float _cap, int _weight);
	};

	// A value cell bound to a host parameter: its name, its value, and for a continuous one a thin cursor. Dragged
	// horizontally; a list of more than two values opens the selector on a click, a list of two toggles
	struct Cell
	{
		// Cell: name, value, cursor. Plain: the same without a cursor. Field: name and value on a title line. Title:
		// the value alone, right-aligned on a block's title line. Button: a toggle drawn as a button. Segments: a
		// toggle drawn as two joined segments. Fader: a Master strip's vertical fader. Small: a strip's pan or output
		enum class Look { Cell, Plain, Field, Title, Button, Segments, Fader, Small };
		enum class Format { Plain, Pan, Pattern, Chokes };

		juce::RangedAudioParameter* parameter = nullptr;	// nullptr: a greyed cell with no engine behind it
		juce::String name;
		float x = 0, y = 0, w = 0, h = 18;					// mm: the name's top left, the width with its gutter
		const Hue* hue = &hue::t;
		juce::StringArray labels;	// what a list shows for each value from its range's start, else the parameter's
		Look look = Look::Cell;
		Format format = Format::Plain;
		bool visible = true;
		bool off = false;			// greyed: no engine behind it
		bool velocity = false;		// its name in the velocity hue
		bool reversed = false;		// Segments: the second label first
		juce::String section;		// the block: its focus, its off state, a lock line's subtitle

		bool isList() const;
		int index() const;			// a list's value, from 0
		int count() const;			// a list's values
		float normalised() const { return parameter ? parameter->getValue() : 0.0f; }
		juce::String valueText(float _normalised) const;
		// Where it takes the mouse and what it paints. Cell and Plain: x, y the name's top left. Field: y the baseline.
		// Title: x the right edge, y the block's top. The others: their box
		juce::Rectangle<float> area() const
		{
			switch(look)
			{
			case Look::Cell:
			case Look::Plain: return {x - 1, y - 0.5f, w, h - 0.5f};
			case Look::Field: return {x - 1, y - 4.5f, w, 6.5f};
			case Look::Title: return {x - w, y, w, 8};
			default: return {x, y, w, h};
			}
		}
	};

	// Where the mouse acts, in mm. The last one added is on top. A click is a press and release without a drag
	struct Hit
	{
		juce::Rectangle<float> r;
		int cell = -1;								// a Cell's gestures
		std::function<void()> press;				// at once on the press
		std::function<void()> click;				// on a release without a drag
		std::function<void()> doubleClick;
		float stepMm = 0;							// a horizontal drag in steps of this many mm
		std::function<void(int)> steps;
		std::function<void(float, float)> drag;		// a free drag, its last move in mm
		std::function<void()> end;					// after a drag
		juce::MouseCursor::StandardCursorType cursor = juce::MouseCursor::PointingHandCursor;
		bool keepsLocks = false;					// a press here leaves the lock edit as it is
	};

	// An area whose look follows a state the timer reads: when the state changes, the area alone is repainted
	struct Region
	{
		juce::Rectangle<float> r;
		std::function<uint64_t()> state;
		uint64_t last = 0;
	};

	inline uint64_t mix(uint64_t _h, const uint64_t _v)
	{
		return (_h ^ (_v + 0x9e3779b97f4a7c15ull + (_h << 6) + (_h >> 2))) * 0x100000001b3ull;
	}
	inline uint64_t mixFloat(const uint64_t _h, const float _v)
	{
		uint32_t u;
		static_assert(sizeof(u) == sizeof(_v));
		std::memcpy(&u, &_v, sizeof(u));
		return mix(_h, u);
	}
}
