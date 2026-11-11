// The Lockstep editor, headless at 1600 x 1000: every host parameter of a Track's Sound page, of FX and of Master has
// exactly one control; a horizontal drag on a value cell changes its parameter inside one gesture; a click on a step
// key toggles its trig through the sequencer; a strip's step takes a Lock from a drag instead of the parameter; each
// page is rendered to a PNG (first argument: the folder, else the temporary folder's edDrumsEditor).

#include <cstdio>
#include <map>

#include "edDrumsEditor.h"

namespace
{
	using namespace elektron::drums;

	int g_failures = 0;

	void check(const bool _ok, const juce::String& _what)
	{
		std::printf("%s %s\n", _ok ? "PASS" : "FAIL", _what.toRawUTF8());
		if(!_ok)
			++g_failures;
	}

	struct Gestures : juce::AudioProcessorListener
	{
		int begun = 0, ended = 0, changes = 0;
		void audioProcessorParameterChanged(juce::AudioProcessor*, int, float) override { ++changes; }
		void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails&) override {}
		void audioProcessorParameterChangeGestureBegin(juce::AudioProcessor*, int) override { ++begun; }
		void audioProcessorParameterChangeGestureEnd(juce::AudioProcessor*, int) override { ++ended; }
	};

	juce::MouseEvent mouse(juce::Component& _c, const juce::Point<int> _down, const juce::Point<int> _now)
	{
		const auto now = juce::Time::getCurrentTime();
		return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), _now.toFloat(),
			juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &_c, &_c, now,
			_down.toFloat(), now, 1, _now != _down);
	}

	void click(Editor& _e, const juce::Point<int> _at)
	{
		_e.mouseDown(mouse(_e, _at, _at));
		_e.mouseUp(mouse(_e, _at, _at));
	}

	void drag(Editor& _e, const juce::Point<int> _from, const juce::Point<int> _by)
	{
		_e.mouseDown(mouse(_e, _from, _from));
		for(int i = 1; i <= 10; ++i)
			_e.mouseDrag(mouse(_e, _from, _from + _by * i / 10));
		_e.mouseUp(mouse(_e, _from, _from + _by));
	}

	// Each expected ID bound by exactly one of the page's cells
	void coverage(const Editor& _editor, const juce::StringArray& _ids, const juce::String& _page)
	{
		std::map<juce::String, int> count;
		for(const auto& c : _editor.cells())
			if(c.parameter)
				++count[c.parameter->getParameterID()];
		int wrong = 0;
		for(const auto& id : _ids)
			if(count[id] != 1)
			{
				std::printf("  %s: %d controls\n", id.toRawUTF8(), count[id]);
				++wrong;
			}
		check(wrong == 0 && !_ids.isEmpty(), _page + ": each of " + juce::String(_ids.size())
			+ " parameters has exactly one control");
	}

	void snapshot(Editor& _editor, const juce::File& _file)
	{
		_editor.tick();
		const auto image = _editor.createComponentSnapshot(_editor.getLocalBounds(), true, 1.0f);
		_file.deleteFile();
		juce::FileOutputStream out(_file);
		const bool ok = out.openedOk() && juce::PNGImageFormat().writeImageToStream(image, out);
		check(ok, "rendered " + _file.getFullPathName());
	}

	void run(const juce::File& _folder)
	{
		_folder.createDirectory();
		Processor processor;
		Editor editor(processor);
		std::printf("face: %s\n", ui::Painter::faceName().toRawUTF8());
		check(editor.getWidth() == Editor::Width && editor.getHeight() == Editor::Height, "the editor at 1600 x 1000");

		using S = Processor::Section;
		coverage(editor, processor.trackIds(0), "Sound, Track 1");
		editor.showTrack(9);
		coverage(editor, processor.trackIds(9), "Sound, Track 10");
		editor.showTrack(0);
		editor.showPage(Editor::Page::Fx);
		coverage(editor, processor.ids(S::Effects), "FX");
		editor.showPage(Editor::Page::Master);
		coverage(editor, processor.ids(S::Master), "Master");
		editor.showPage(Editor::Page::Sound);

		// A drag to the right on FREQ: its value rises, inside one gesture
		{
			Gestures gestures;
			processor.addListener(&gestures);
			auto* p = processor.parameter("t01_flt_freq");
			const auto area = editor.cellArea("t01_flt_freq");
			check(!area.isEmpty(), "Track 1's FREQ has a cell");
			// Right from below half, left from above
			const float before = p->getValue();
			const int sign = before < 0.5f ? 1 : -1;
			drag(editor, area.getCentre(), {sign * 60, 0});
			const float after = p->getValue();
			check((after - before) * static_cast<float>(sign) > 0.2f, "a drag of 60 px moves FREQ from " + juce::String(before, 3) + " to "
				+ juce::String(after, 3));
			check(gestures.begun == 1 && gestures.ended == 1 && gestures.changes > 0, "in one gesture: "
				+ juce::String(gestures.begun) + " begun, " + juce::String(gestures.ended) + " ended, "
				+ juce::String(gestures.changes) + " changes");

			// A list of two: a click toggles it, one more gesture
			auto* place = processor.parameter("t01_drv1_post");
			const float was = place->getValue();
			click(editor, editor.cellArea("t01_drv1_post").getCentre());
			check(place->getValue() != was && gestures.begun == 2 && gestures.ended == 2,
				"a click on PLACE toggles it in one gesture");
			processor.removeListener(&gestures);
		}

		// A click on step key 3 toggles Track 1's step 3, a second takes it off
		{
			const auto trig = [&] { return processor.sequencer().pattern(processor.currentPattern()).step(0, 2).trig; };
			const bool before = trig();
			click(editor, editor.stepKeyArea(2).getCentre());
			check(trig() != before, "a click on step key 3 toggles the trig");
			click(editor, editor.stepKeyArea(2).getCentre());
			check(trig() == before, "a second click takes it back");
			for(const int k : {0, 4, 8, 12, 14})
				click(editor, editor.stepKeyArea(k).getCentre());
		}

		// A strip's step: a drag on RESO writes a Lock on step 1, not the parameter
		{
			auto* reso = processor.parameter("t01_flt_res");
			const float base = reso->getValue();
			const auto strip = editor.stepKeyArea(0).getCentre() + juce::Point<int>(0, 21 * 3);	// 21 mm lower
			click(editor, strip);
			drag(editor, editor.cellArea("t01_flt_res").getCentre(), {50, 0});
			const auto& step = processor.sequencer().pattern(processor.currentPattern()).step(0, 0);
			check(step.lockCount == 1 && reso->getValue() == base, "a strip, then a drag on RESO: a Lock on step 1 ("
				+ juce::String(step.lockCount) + "), RESO left as it was");
			snapshot(editor, _folder.getChildFile("sound-lock.png"));
			click(editor, {10, 10});	// outside: leaves the lock edit
		}

		editor.showPage(Editor::Page::Sound);
		snapshot(editor, _folder.getChildFile("sound.png"));
		editor.showLfo(1);
		snapshot(editor, _folder.getChildFile("sound-lfo2.png"));
		editor.showLfo(0);
		editor.showMenu(true);
		snapshot(editor, _folder.getChildFile("menu.png"));
		editor.showMenu(false);
		editor.showPage(Editor::Page::Seq);
		snapshot(editor, _folder.getChildFile("seq.png"));
		editor.showPage(Editor::Page::Master);
		snapshot(editor, _folder.getChildFile("master.png"));
		editor.showPage(Editor::Page::Fx);
		snapshot(editor, _folder.getChildFile("fx.png"));
		editor.showPage(Editor::Page::Kits);
		snapshot(editor, _folder.getChildFile("kits.png"));
		editor.showPage(Editor::Page::Browse);
		snapshot(editor, _folder.getChildFile("browse.png"));
		editor.showPage(Editor::Page::Sound);
		editor.setSize(Editor::SmallWidth, Editor::SmallHeight);
		snapshot(editor, _folder.getChildFile("sound-1280.png"));
	}
}

int main(const int _argc, char** _argv)
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	const juce::ScopedJuceInitialiser_GUI juce;
	const auto folder = _argc > 1 ? juce::File(juce::String::fromUTF8(_argv[1]))
		: juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("edDrumsEditor");
	run(folder);
	std::printf("%s: %d failure(s)\n", g_failures == 0 ? "PASSED" : "FAILED", g_failures);
	return g_failures == 0 ? 0 : 1;
}
