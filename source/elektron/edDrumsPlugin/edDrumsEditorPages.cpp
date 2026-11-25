#include "edDrumsEditorPages.h"

#include <cmath>
#include <vector>

#include "LockstepAssets.h"
#include "edDrumsParameters.h"

namespace elektron::drums::ui
{
	namespace
	{
		Hue makeHue(const uint32_t _a, const uint32_t _b, const uint32_t _c, const uint32_t _d, const uint32_t _e)
		{
			return {{juce::Colour(_a), juce::Colour(_b), juce::Colour(_c), juce::Colour(_d), juce::Colour(_e)}};
		}

		// The face, Barlow Semi Condensed from the plug-in's data, its two weights, and how high its capitals stand in
		// a JUCE font height
		struct Face
		{
			juce::Typeface::Ptr regular, bold;
			float capRatio = 0.7f;
		};

		const Face& face()
		{
			static const Face f = []
			{
				using namespace LockstepAssets;
				Face r;
				r.regular = juce::Typeface::createSystemTypefaceFor(BarlowSemiCondensedMedium_ttf,
					BarlowSemiCondensedMedium_ttfSize);
				r.bold = juce::Typeface::createSystemTypefaceFor(BarlowSemiCondensedSemiBold_ttf,
					BarlowSemiCondensedSemiBold_ttfSize);
				// The capital's ink, from its outline (a glyph's box is the font's whole height)
				juce::GlyphArrangement ga;
				ga.addLineOfText(juce::Font(r.bold).withHeight(100.0f), "H", 0, 0);
				juce::Path outline;
				ga.createPath(outline);
				const float cap = outline.getBounds().getHeight();
				if(cap > 10)
					r.capRatio = cap / 100.0f;
				return r;
			}();
			return f;
		}

		// The 0's advance in a font, laid out once for each of the few the editor draws in
		float zeroAdvance(const juce::Font& _font)
		{
			struct Known { juce::Typeface* face; float height, kerning, advance; };
			thread_local std::vector<Known> known;
			auto* const face = _font.getTypefacePtr().get();
			for(const auto& k : known)
				if(k.face == face && k.height == _font.getHeight() && k.kerning == _font.getExtraKerningFactor())
					return k.advance;
			juce::GlyphArrangement zero;
			zero.addLineOfText(_font, "0", 0, 0);
			const float advance = zero.getGlyph(0).getRight() - zero.getGlyph(0).getLeft();
			known.push_back({face, _font.getHeight(), _font.getExtraKerningFactor(), advance});
			return advance;
		}

		// A line of text with every digit in the 0's advance, its glyph centred there: Barlow's digits are
		// proportional, and a value that changes must not move its neighbours
		juce::GlyphArrangement arrange(const juce::Font& _font, const juce::String& _text, const float _x,
			const float _baseline)
		{
			const float advance = zeroAdvance(_font);
			juce::GlyphArrangement ga;
			ga.addLineOfText(_font, _text, _x, _baseline);
			float shift = 0;
			for(int i = 0; i < ga.getNumGlyphs(); ++i)
			{
				auto& g = ga.getGlyph(i);
				g.moveBy(shift, 0);
				if(!juce::CharacterFunctions::isDigit(g.getCharacter()))
					continue;
				const float w = g.getRight() - g.getLeft();
				g.moveBy((advance - w) / 2, 0);
				shift += advance - w;
			}
			return ga;
		}

		float widthOf(juce::GlyphArrangement& _ga, const float _x)
		{
			const int n = _ga.getNumGlyphs();
			return n == 0 ? 0 : _ga.getGlyph(n - 1).getRight() - _x;
		}

		// The style guide's three sizes: 4.0, 3.2, 2.8 mm
		float snapCap(const float _cap)
		{
			return _cap <= 2.8f ? 2.8f : _cap <= 3.4f ? 3.2f : _cap <= 4.4f ? 4.0f : _cap;
		}
	}

	namespace hue
	{
		const Hue g = makeHue(0xff8cc152, 0xff324027, 0xff2f3b29, 0xff46553d, 0xffaad17f);
		const Hue k = makeHue(0xff3a9cc8, 0xff1e3744, 0xff21343e, 0xff324c5a, 0xff86c2dd);
		const Hue a = makeHue(0xffe8903c, 0xff483422, 0xff403225, 0xff5c4938, 0xfff0b47c);
		const Hue b = makeHue(0xff5b8de0, 0xff26334a, 0xff273143, 0xff3a4960, 0xff9bbaec);
		const Hue s = makeHue(0xffe57b64, 0xff472f2c, 0xff3f2e2c, 0xff5b4442, 0xffefab9d);
		const Hue p = makeHue(0xff9282dc, 0xff333149, 0xff302f42, 0xff48465f, 0xffbcb2e9);
		const Hue y = makeHue(0xffe2b44a, 0xff463d26, 0xff3f3828, 0xff5b523c, 0xffe8c472);
		const Hue m = makeHue(0xffd77fc4, 0xff443043, 0xff3d2f3e, 0xff584559, 0xffe6aed9);
		const Hue v = makeHue(0xffb4c450, 0xff3b4127, 0xff373b29, 0xff50563d, 0xffc4d176);
		const Hue w = makeHue(0xffc3c8ce, 0xff3f4145, 0xff393c3f, 0xff53575b, 0xffcbd0d5);
		const Hue t = makeHue(0xffE9DCC2, 0xff3f3b33, 0xff38352e, 0xff59544a, 0xffF3EADA);
	}

	const juce::StringArray& trackColourNames()
	{
		static const juce::StringArray names{"IVORY", "CORAL", "ORANGE", "YELLOW", "GREEN", "CYAN", "BLUE", "VIOLET",
			"PINK", "GREY"};
		return names;
	}

	const Hue& trackHue(const int _colour)
	{
		static const std::array<const Hue*, 10> hues{&hue::t, &hue::s, &hue::a, &hue::y, &hue::g, &hue::k, &hue::b, &hue::p,
			&hue::m, &hue::w};
		return *hues[static_cast<size_t>(juce::jlimit(0, 9, _colour))];
	}

	Painter::Painter(juce::Graphics& _g, const float _scale) : g(_g), scale(_scale) {}

	juce::String Painter::faceName()
	{
		const auto& f = face();
		return f.regular->getName() + " / " + f.bold->getName();
	}

	juce::Font Painter::font(const float _cap, const int _weight)
	{
		const auto& f = face();
		const float cap = snapCap(_cap);
		juce::Font font(_weight >= 600 ? f.bold : f.regular);
		font.setHeight(cap / f.capRatio);
		font.setExtraKerningFactor(cap >= 4 ? 0.0f : 0.02f);
		return font;
	}

	float Painter::textWidth(const juce::String& _text, const float _cap, const int _weight)
	{
		auto ga = arrange(font(_cap, _weight), _text, 0, 0);
		return widthOf(ga, 0);
	}

	void Painter::text(float _x, const float _baseline, const juce::String& _text, const float _cap,
		const juce::Colour _colour, const int _weight, const Align _align) const
	{
		if(_text.isEmpty())
			return;
		if(drawnTexts)
			drawnTexts->add(_text);
		auto ga = arrange(font(_cap, _weight), _text, _x, _baseline);
		if(_align != Align::Left)
		{
			const float w = widthOf(ga, _x);
			ga.moveRangeOfGlyphs(0, -1, _align == Align::Right ? -w : -w / 2, 0);
		}
		g.setColour(_colour);
		ga.draw(g);
	}

	void Painter::rect(const float _x, const float _y, const float _w, const float _h, const juce::Colour _colour,
		const float _radius) const
	{
		if(_w <= 0 || _h <= 0)
			return;
		g.setColour(_colour);
		if(_radius < 0.2f)
			g.fillRect(juce::Rectangle<float>(_x, _y, _w, _h));
		else
			g.fillRoundedRectangle(_x, _y, _w, _h, std::min(_radius, std::min(_w, _h) / 2));
	}

	void Painter::frame(const float _x, const float _y, const float _w, const float _h, const juce::Colour _colour,
		const float _radius, const float _line) const
	{
		g.setColour(_colour);
		g.drawRoundedRectangle(_x, _y, _w, _h, _radius, _line);
	}

	void Painter::line(const float _x0, const float _y0, const float _x1, const float _y1, const juce::Colour _colour,
		const float _width) const
	{
		g.setColour(_colour);
		g.drawLine(_x0, _y0, _x1, _y1, _width);
	}

	void Painter::poly(const std::vector<juce::Point<float>>& _points, const juce::Colour _colour, const float _width) const
	{
		if(_points.size() < 2)
			return;
		juce::Path path;
		path.startNewSubPath(_points[0]);
		for(size_t i = 1; i < _points.size(); ++i)
			path.lineTo(_points[i]);
		g.setColour(_colour);
		g.strokePath(path, juce::PathStrokeType(_width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
	}

	void Painter::dot(const float _x, const float _y, const float _r, const juce::Colour _fill, const juce::Colour _ring,
		const float _line) const
	{
		g.setColour(_fill);
		g.fillEllipse(_x - _r, _y - _r, 2 * _r, 2 * _r);
		g.setColour(_ring);
		g.drawEllipse(_x - _r, _y - _r, 2 * _r, 2 * _r, _line);
	}

	bool Cell::isList() const
	{
		return !labels.isEmpty() || dynamic_cast<const juce::AudioParameterChoice*>(parameter)
			|| dynamic_cast<const juce::AudioParameterBool*>(parameter);
	}

	int Cell::count() const
	{
		if(!labels.isEmpty())
			return labels.size();
		if(const auto* c = dynamic_cast<const juce::AudioParameterChoice*>(parameter))
			return c->choices.size();
		if(dynamic_cast<const juce::AudioParameterBool*>(parameter))
			return 2;
		if(!parameter)
			return 1;
		const auto& r = parameter->getNormalisableRange();
		return juce::roundToInt(r.end - r.start) + 1;
	}

	int Cell::index() const
	{
		return parameter ? indexOf(parameter->getValue()) : 0;
	}

	float Cell::normalisedOf(const int _index) const
	{
		if(!parameter)
			return 0;
		return parameter->convertTo0to1(parameter->getNormalisableRange().start + static_cast<float>(_index));
	}

	int Cell::indexOf(const float _normalised) const
	{
		if(!parameter)
			return 0;
		return juce::roundToInt(parameter->convertFrom0to1(_normalised) - parameter->getNormalisableRange().start);
	}

	juce::String Cell::valueText(const float _normalised) const
	{
		if(!parameter)
			return juce::String::fromUTF8("\xe2\x80\x94");	// an em dash: nothing behind it
		const auto& r = parameter->getNormalisableRange();
		const float plain = parameter->convertFrom0to1(_normalised);
		const int n = juce::roundToInt(plain);
		switch(format)
		{
		case Format::Pan:
			return n == 64 ? juce::String("C") : n < 64 ? "L" + juce::String(64 - n) : "R" + juce::String(n - 64);
		case Format::Pattern:
			return juce::String::charToString(static_cast<juce::juce_wchar>('A' + (n - 1) / 16))
				+ juce::String((n - 1) % 16 + 1).paddedLeft('0', 2);
		case Format::Chokes:
		{
			juce::StringArray tracks;
			for(int t = 0; t < 16; ++t)
				if(n & (1 << t))
					tracks.add(juce::String(t + 1));
			return tracks.isEmpty() ? juce::String("NONE") : tracks.joinIntoString(",");
		}
		case Format::Bpm:
			return juce::String(plain, 1) + " BPM";
		case Format::Pitch:
			return parameters::noteName(juce::jlimit(0, 127, n + shift));
		case Format::Plain:
			break;
		}
		if(isList())
		{
			const int i = juce::roundToInt(plain - r.start);
			// A value past the labels (a Cycles machine's VEL TARGET on another machine) as the host names it
			if(i < labels.size())
				return labels[i];
			if(const auto* c = dynamic_cast<const juce::AudioParameterChoice*>(parameter))
				return c->choices[i].toUpperCase();
			return i ? "ON" : "OFF";
		}
		// The free rates, as the host shows them (parameters::rateText)
		if(dynamic_cast<const juce::AudioParameterFloat*>(parameter))
			return parameter->getText(_normalised, 0);
		// In its unit where the parameter has one (parameters::valueText), else the number, signed where it can be
		const auto unit = parameter->getText(_normalised, 0);
		if(unit != juce::String(n))
			return unit;
		juce::String text = (r.start < 0 && n > 0 ? "+" : "") + juce::String(n);
		if(n < 0)
			text = juce::String::fromUTF8("\xe2\x88\x92") + juce::String(-n);	// U+2212
		return text;
	}
}
