// The Lockstep editor, headless at 1600 x 1000: every host parameter of a Track's Sound page, of FX and of Master has
// exactly one control; a horizontal drag on a value cell changes its parameter inside one gesture; a click on a step
// key toggles its trig through the sequencer; a strip's step takes a Lock from a drag instead of the parameter; Browse
// lists a folder of samples, filters by SEARCH, loads one by a double-click and by a drag onto a Track; each
// page is rendered to a PNG (first argument: the folder, else the temporary folder's edDrumsEditor).

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

#include <juce_audio_formats/juce_audio_formats.h>

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

	juce::MouseEvent mouse(juce::Component& _c, const juce::Point<int> _down, const juce::Point<int> _now,
		const int _clicks = 1)
	{
		const auto now = juce::Time::getCurrentTime();
		return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), _now.toFloat(),
			juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &_c, &_c, now,
			_down.toFloat(), now, _clicks, _now != _down);
	}

	// A decaying tone of _frames at _rate in _format's file
	void writeSound(juce::AudioFormat& _format, const juce::File& _file, const double _rate, const int _frames)
	{
		_file.deleteFile();
		juce::AudioBuffer<float> wave(1, _frames);
		for(int n = 0; n < _frames; ++n)
			wave.setSample(0, n, 0.7f * std::exp(-n / 3000.0f) * std::sin(0.05f * static_cast<float>(n)));
		std::unique_ptr<juce::AudioFormatWriter> writer(_format.createWriterFor(new juce::FileOutputStream(_file), _rate, 1,
			16, {}, 0));
		check(writer != nullptr && writer->writeFromAudioSampleBuffer(wave, 0, _frames), "wrote " + _file.getFileName());
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

	void doubleClick(Editor& _e, const juce::Point<int> _at)
	{
		click(_e, _at);
		_e.mouseDown(mouse(_e, _at, _at, 2));
		_e.mouseUp(mouse(_e, _at, _at, 2));
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

	// Sound: each of the Track's parameters on one of the Machine block's pages, Machine 9-24 its machine does not use
	// none
	void soundCoverage(Editor& _editor, Processor& _processor, const int _track, const juce::String& _page)
	{
		const auto isMachine = [](const juce::String& _id)
		{
			return _id.length() > 5 && _id.substring(3, 5) == "_m" && _id.substring(5).containsOnly("0123456789");
		};
		std::map<juce::String, int> count;
		for(int page = 0; page < _editor.machinePages(); ++page)
		{
			_editor.showMachinePage(page);
			for(const auto& c : _editor.cells())
				if(c.parameter && (page == 0 || isMachine(c.parameter->getParameterID())))
					++count[c.parameter->getParameterID()];
		}
		_editor.showMachinePage(0);
		const auto* choice = dynamic_cast<juce::AudioParameterChoice*>(
			_processor.parameter(juce::String::formatted("t%02d_machine", _track + 1)));
		const int used = std::max(8, (machineParameters(_processor.machines()[static_cast<size_t>(choice->getIndex())]) + 7)
			/ 8 * 8);
		int wrong = 0, expected = 0;
		for(const auto& id : _processor.trackIds(_track))
		{
			const bool shown = !isMachine(id) || id.substring(5).getIntValue() <= used;
			expected += shown;
			if(count[id] != (shown ? 1 : 0))
			{
				std::printf("  %s: %d controls\n", id.toRawUTF8(), count[id]);
				++wrong;
			}
		}
		check(wrong == 0, _page + ": each of " + juce::String(expected) + " parameters has exactly one control");
	}

	// The Machine block's pages: OD DRUM's three, TRX-BD's one, the others shut
	void machinePages(Editor& _editor, Processor& _processor)
	{
		auto* machine = _processor.parameter("t01_machine");
		const auto put = [&](const int _choice)
		{
			machine->setValueNotifyingHost(machine->convertTo0to1(static_cast<float>(_choice)));
			_editor.tick();
		};
		const auto nameOf = [&](const juce::String& _id)
		{
			for(const auto& c : _editor.cells())
				if(c.parameter && c.parameter->getParameterID() == _id)
					return c.off ? juce::String("off") : c.name;
			return juce::String();
		};
		const auto& list = _processor.machines();
		const auto choiceOf = [&](const Machine& _m)
		{
			return static_cast<int>(std::find(list.begin(), list.end(), _m) - list.begin());
		};
		put(choiceOf({Family::OpenDrum, 0}));
		check(_editor.machinePages() == 3 && _editor.machinePage() == 0 && nameOf("t01_m1") == "TUNE",
			"OD DRUM: three pages, page 1 shown, Machine 1 TUNE");
		click(_editor, _editor.machinePageArea(1).getCentre());
		check(_editor.machinePage() == 1 && nameOf("t01_m9") == "FINE" && nameOf("t01_m1").isEmpty(),
			"a click on 2: Machine 9-16, Machine 9 FINE, Machine 1 gone");
		click(_editor, _editor.machinePageArea(2).getCentre());
		check(_editor.machinePage() == 2 && nameOf("t01_m17") == "FILTER" && nameOf("t01_m23") == "off"
			&& nameOf("t01_m24") == "off", "a click on 3: Machine 17 FILTER, 23 and 24 greyed");
		put(choiceOf({Family::Machinedrum, 16}));
		check(_editor.machinePages() == 1 && _editor.machinePage() == 0 && nameOf("t01_m1").isNotEmpty(),
			"TRX-BD: one page, page 1 shown again");
		click(_editor, _editor.machinePageArea(1).getCentre());
		check(_editor.machinePage() == 0 && nameOf("t01_m9").isEmpty(), "TRX-BD: a click on the greyed 2 opens nothing");
		put(choiceOf({Family::Sophie, 0}));
		check(_editor.machinePages() == 3 && nameOf("t01_m1") == "MODEL", "SOPHIE: three pages, Machine 1 MODEL");
		click(_editor, _editor.machinePageArea(2).getCentre());
		check(nameOf("t01_m20") == "BODY" && nameOf("t01_m21") == "off", "SOPHIE: Machine 20 BODY, 21 greyed");
		put(choiceOf({Family::OpenDrum, 0}));
		click(_editor, _editor.machinePageArea(1).getCentre());
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
		soundCoverage(editor, processor, 0, "Sound, Track 1");
		editor.showTrack(9);
		soundCoverage(editor, processor, 9, "Sound, Track 10");
		editor.showTrack(0);
		machinePages(editor, processor);
		soundCoverage(editor, processor, 0, "Sound, Track 1 on OD DRUM");
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

		// Kits: a Kit saved in Slot 3, the sound changed, a click on Slot 3 and one on LOAD give it back; a click on
		// Slot 6 and one on SAVE HERE fill it
		{
			auto* freq = processor.parameter("t02_flt_freq");
			freq->setValueNotifyingHost(0.25f);
			const float saved = freq->getValue();
			check(processor.saveKit(2), "a Kit saved in Slot 3");
			freq->setValueNotifyingHost(0.75f);
			editor.showPage(Editor::Page::Kits);
			click(editor, editor.kitSlotArea(2).getCentre());
			check(editor.kitSelected() == 2, "a click on Slot 3 selects it");
			click(editor, editor.kitActionArea(0).getCentre());
			check(freq->getValue() == saved && processor.currentKit() == 2 && !processor.kitDiffers(),
				"LOAD gives Slot 3's Kit back");
			click(editor, editor.kitSlotArea(5).getCentre());
			click(editor, editor.kitActionArea(1).getCentre());
			check(!processor.kitSlot(5).empty() && processor.currentKit() == 5, "SAVE HERE fills Slot 6");
			processor.renameKit(5, "Dusty");
			click(editor, editor.kitActionArea(3).getCentre());
			check(!processor.kitSlot(5).empty(), "a first click on DELETE keeps the Kit");
			click(editor, editor.kitSlotArea(2).getCentre());
			freq->setValueNotifyingHost(0.5f);
		}

		// Browse: a folder of three sounds and a text file in the Library, listed by source; SEARCH filters; a
		// double-click loads a sample on the Track shown, a drag onto Track 6 there
		{
			const auto samples = juce::File::getSpecialLocation(juce::File::tempDirectory)
				.getChildFile("edDrumsEditorSamples");
			samples.deleteRecursively();
			samples.createDirectory();
			juce::WavAudioFormat wav;
			juce::AiffAudioFormat aiff;
			writeSound(wav, samples.getChildFile("kick_01.wav"), 44100, 13230);
			writeSound(aiff, samples.getChildFile("SD-snare.aif"), 48000, 9600);
			writeSound(wav, samples.getChildFile("hh closed.wav"), 44100, 4410);
			samples.getChildFile("notes.txt").replaceWithText("no audio");
			auto& library = processor.sampleLibrary();
			check(library.addFolder(samples), "the folder added to the Library");
			const auto until = juce::Time::getMillisecondCounter() + 20000;
			while(!library.update() && juce::Time::getMillisecondCounter() < until)
				juce::Thread::sleep(5);
			check(library.entries().size() == 3, "the Library holds " + juce::String(library.entries().size())
				+ " samples");

			editor.showPage(Editor::Page::Browse);
			click(editor, editor.browseTreeArea(2).getCentre());
			editor.tick();
			check(editor.browseCount() == 3 && editor.searchField().isVisible(), "SAMPLES, BY SOURCE lists "
				+ juce::String(editor.browseCount()) + " samples, SEARCH shown");
			auto& search = editor.searchField();
			search.setText("snare", false);
			search.onTextChange();	// what the message loop calls
			check(editor.browseCount() == 1, "SEARCH \"snare\" leaves " + juce::String(editor.browseCount()));
			doubleClick(editor, editor.browseRowArea(0).getCentre());
			const auto* snare = library.find(processor.sampleFingerprint(0));
			check(processor.sampleName(0) == "SD-snare.aif" && snare && snare->name == "SD-snare.aif"
				&& juce::roundToInt(processor.parameter("t01_machine")->convertFrom0to1(
					processor.parameter("t01_machine")->getValue())) == processor.samplerChoice(),
				"a double-click loads SD-snare.aif on Track 1, on the Sampler");
			search.setText({}, false);
			search.onTextChange();
			click(editor, editor.browseTreeArea(4).getCentre());
			check(editor.browseCount() == 3, "SAMPLES, BY TYPE lists them all");
			drag(editor, editor.browseRowArea(0).getCentre(), editor.stepKeyArea(5).getCentre()
				- editor.browseRowArea(0).getCentre());
			check(processor.sampleName(5).isNotEmpty(), "a drag onto Track 6 loads " + processor.sampleName(5));
			click(editor, editor.browseTreeArea(2).getCentre());
			click(editor, editor.browseRowArea(0).getCentre());	// auditioned: its sound drawn
			check(processor.auditioned() != nullptr, "a click auditions the sample");

			// Ticket 20: a click on TYPE opens the types, CLAP chosen is the user's; REMOVE FOLDER on the folder's
			// segment takes its samples out of the Library and leaves its files
			click(editor, editor.browseButtonArea(1).getCentre());
			editor.tick();
			click(editor, editor.selectorItemArea(static_cast<int>(SampleLibrary::Type::Clap)).getCentre());
			editor.tick();
			const auto clap = std::count_if(library.entries().begin(), library.entries().end(), [](const auto& _e)
			{
				return _e.type == SampleLibrary::Type::Clap && _e.typeFrom == SampleLibrary::TypeFrom::User;
			});
			check(clap == 1, "TYPE set to CLAP on " + juce::String(static_cast<int>(clap)) + " samples");
			click(editor, editor.browseButtonArea(2).getCentre());
			editor.tick();
			snapshot(editor, _folder.getChildFile("browse-samples.png"));
			click(editor, editor.browseButtonArea(0).getCentre());
			editor.tick();
			check(library.folders().isEmpty() && library.entries().empty() && editor.browseCount() == 0
				&& samples.getChildFile("kick_01.wav").existsAsFile(), "REMOVE FOLDER leaves "
				+ juce::String(static_cast<int>(library.entries().size())) + " samples, the files on the disk");
		}

		// Ticket 19: Kits' LOAD AT ITS START gives the pattern playing the Slot selected; Browse's KITS lists the Bank, a
		// double-click loads; TRACK SOUNDS lists the Library, a double-click loads on the Track shown, a drag onto Track 4
		{
			editor.showPage(Editor::Page::Kits);
			click(editor, editor.kitSlotArea(2).getCentre());
			click(editor, editor.kitActionArea(6).getCentre());
			check(processor.patternKit(processor.currentPattern()) == 2, "LOAD AT ITS START: pattern's Kit Slot 3");
			snapshot(editor, _folder.getChildFile("kits-pattern.png"));
			click(editor, editor.kitActionArea(7).getCentre());
			check(processor.patternKit(processor.currentPattern()) == -1, "KEEP THE KIT");

			auto* level = processor.parameter("t01_level");
			level->setValueNotifyingHost(level->convertTo0to1(33));
			check(processor.saveTrackSound(0, "Low kick").existsAsFile(), "a Track sound saved");
			level->setValueNotifyingHost(level->convertTo0to1(100));
			editor.showPage(Editor::Page::Browse);
			click(editor, editor.browseTreeArea(0).getCentre());
			editor.tick();
			check(editor.browseCount() >= 2, "KITS lists " + juce::String(editor.browseCount()) + " Kits");
			doubleClick(editor, editor.browseRowArea(0).getCentre());
			check(processor.currentKit() == 2, "a double-click on KITS' first row loads Slot 3");
			click(editor, editor.browseTreeArea(1).getCentre());
			editor.tick();
			check(editor.browseCount() == 1 && editor.searchField().isVisible(), "TRACK SOUNDS lists the sound saved");
			click(editor, editor.browseRowArea(0).getCentre());
			check(processor.auditioned() != nullptr, "a click auditions the Track sound");
			snapshot(editor, _folder.getChildFile("browse-sounds.png"));
			doubleClick(editor, editor.browseRowArea(0).getCentre());
			check(juce::roundToInt(level->convertFrom0to1(level->getValue())) == 33, "a double-click loads it on Track 1");
			drag(editor, editor.browseRowArea(0).getCentre(), editor.stepKeyArea(3).getCentre()
				- editor.browseRowArea(0).getCentre());
			auto* level4 = processor.parameter("t04_level");
			check(juce::roundToInt(level4->convertFrom0to1(level4->getValue())) == 33, "a drag loads it on Track 4");
		}

		editor.showPage(Editor::Page::Sound);
		snapshot(editor, _folder.getChildFile("sound.png"));
		{
			// SOPHIE: its factory sound in the Machine block; page 3 with AMP off, the amp's envelope greyed
			auto* machine = processor.parameter("t01_machine");
			auto* ampOn = processor.parameter("t01_amp_on");
			const float ampWas = ampOn->getValue();
			const float was = machine->getValue();
			machine->setValueNotifyingHost(machine->convertTo0to1(static_cast<float>(processor.sophieChoice())));
			processor.setSophieSound(0, 13);
			editor.showMachinePage(0);
			snapshot(editor, _folder.getChildFile("sound-sophie.png"));
			editor.showMachinePage(2);
			ampOn->setValueNotifyingHost(0.0f);
			snapshot(editor, _folder.getChildFile("sound-sophie-p3.png"));
			ampOn->setValueNotifyingHost(ampWas);
			editor.showMachinePage(0);
			machine->setValueNotifyingHost(was);
			editor.tick();
		}
		editor.showLfo(1);
		snapshot(editor, _folder.getChildFile("sound-lfo2.png"));
		editor.showLfo(0);
		editor.showMenu(true);
		snapshot(editor, _folder.getChildFile("menu.png"));
		editor.showMenu(false);
		editor.showPage(Editor::Page::Seq);
		snapshot(editor, _folder.getChildFile("seq.png"));
		// Ticket 17: Overview's rows take a click as a trig; Patterns shows the chain
		{
			const auto px = [&](const float _x, const float _y)
			{
				const float s = static_cast<float>(editor.getWidth()) / ui::UnitWidth;
				return juce::Point<int>(juce::roundToInt(_x * s), juce::roundToInt(_y * s));
			};
			processor.editSequencer([](Sequencer& _s)
			{
				for(int t = 0; t < 6; ++t)
					for(int i = t % 3; i < 16; i += 3 + t)
						_s.pattern(_s.currentPattern()).setTrig(t, i, true, 60 + 10 * t);
				_s.pattern(_s.currentPattern()).trackLength[2] = 12;
				_s.pattern(5).setTrig(0, 0, true);
				_s.setChain({{0, 2, false}, {5, 1, true}});
				_s.setMute(4, true);
			});
			editor.showSeqTab(1);
			const int pattern = processor.currentPattern();
			const bool before = processor.sequencer().pattern(pattern).step(7, 3).trig;
			click(editor, px(474.7f - 32 * 12.65f + 3.5f * 12.65f, 36 + 7.5f * 12.65f));
			check(processor.sequencer().pattern(pattern).step(7, 3).trig != before, "a click on Overview's row 8 toggles step 4");
			snapshot(editor, _folder.getChildFile("seq-overview.png"));
			editor.showSeqTab(2);
			snapshot(editor, _folder.getChildFile("seq-patterns.png"));
			editor.showSeqTab(0);
		}
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
	// A data folder of the test's own, empty: the Kits' Bank is written there, never in the user's
	const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("edDrumsEditorTestData");
	root.deleteRecursively();
	_putenv_s("GEARMULATOR_DATA_ROOT", root.getFullPathName().toRawUTF8());
	const juce::ScopedJuceInitialiser_GUI juce;
	const auto folder = _argc > 1 ? juce::File(juce::String::fromUTF8(_argv[1]))
		: juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("edDrumsEditor");
	run(folder);
	std::printf("%s: %d failure(s)\n", g_failures == 0 ? "PASSED" : "FAILED", g_failures);
	return g_failures == 0 ? 0 : 1;
}
