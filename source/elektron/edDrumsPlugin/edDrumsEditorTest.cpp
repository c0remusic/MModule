// The Lockstep editor, headless at 1600 x 1000: every host parameter of a Track's Sound page, of FX and of Master has
// exactly one control; a horizontal drag on a value cell changes its parameter inside one gesture; a click on a step
// key toggles its trig through the sequencer; a strip's step takes a Lock from a drag instead of the parameter; Browse's
// LOCKER takes a folder dropped, filters by SEARCH, loads a sample by a double-click, a drag onto a Track and LOAD ON
// TRACK, renames and removes its sources, keeps a set's sample; nothing drawn on Browse and the Menu names a brand; the
// names of ticket 27: every machine and control named, nothing the host sees or the pages draw names a brand or an old
// machine, every cell's name fits; Sound's LFO cards choose the LFO shown and a drive's TYPE stays live while it is off;
// Seq's lanes: empty ones open + LOCK's menu, the menu lists every parameter that takes a Lock and adds a lane, LANES
// turns their page, a lane's X asks again where it holds Locks; every gesture of the step box (ticket 23: VEL, ACCENT,
// SLIDE, a lane's cells, the strips' lock edit, LIVE) read back from the pattern; what a full repaint of Sound and Seq costs is printed;
// each page is rendered to a PNG (first argument: the folder, else the temporary folder's edDrumsEditor).

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>

#include <juce_audio_formats/juce_audio_formats.h>

#include "edDrumsEditor.h"
#include "edDrumsNames.h"
#include "mdProtocol/mdmachines.h"

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
			"VA DRUM: three pages, page 1 shown, Machine 1 TUNE");
		click(_editor, _editor.machinePageArea(1).getCentre());
		check(_editor.machinePage() == 1 && nameOf("t01_m9") == "FINE TUNE" && nameOf("t01_m1").isEmpty(),
			"a click on 2: Machine 9-16, Machine 9 FINE TUNE, Machine 1 gone");
		click(_editor, _editor.machinePageArea(2).getCentre());
		check(_editor.machinePage() == 2 && nameOf("t01_m17") == "CUTOFF" && nameOf("t01_m23") == "off"
			&& nameOf("t01_m24") == "off", "a click on 3: Machine 17 CUTOFF, 23 and 24 greyed");
		put(choiceOf({Family::Machinedrum, 16}));
		check(_editor.machinePages() == 1 && _editor.machinePage() == 0 && nameOf("t01_m1").isNotEmpty(),
			"ROUND KICK: one page, page 1 shown again");
		click(_editor, _editor.machinePageArea(1).getCentre());
		check(_editor.machinePage() == 0 && nameOf("t01_m9").isEmpty(), "ROUND KICK: a click on the greyed 2 opens nothing");
		put(choiceOf({Family::Sophie, 0}));
		check(_editor.machinePages() == 3 && nameOf("t01_m1") == "MODEL", "HYPER FM: three pages, Machine 1 MODEL");
		click(_editor, _editor.machinePageArea(2).getCentre());
		check(nameOf("t01_m20") == "KICK BODY" && nameOf("t01_m21") == "off", "HYPER FM: Machine 20 KICK BODY, 21 greyed");
		// The Sampler: two pages, its grain's on the second; GRAIN's segments in the head set Machine 9
		put(_processor.samplerChoice());
		click(_editor, _editor.machinePageArea(1).getCentre());
		check(_editor.machinePages() == 2 && nameOf("t01_m9") == "GRAIN" && nameOf("t01_m12") == "OUTPUT LPF"
			&& nameOf("t01_m13") == "off", "the Sampler: Machine 9 GRAIN, 12 OUTPUT LPF, 13 greyed");
		auto* grain = _processor.parameter("t01_m9");
		click(_editor, _editor.grainArea(1).getCentre());
		check(juce::roundToInt(grain->convertFrom0to1(grain->getValue())) == 127 && grain->getCurrentValueAsText() == "VINTAGE",
			"a click on VINTAGE sets Machine 9 to 127");
		click(_editor, _editor.grainArea(0).getCentre());
		check(juce::roundToInt(grain->convertFrom0to1(grain->getValue())) == 0 && grain->getCurrentValueAsText() == "12-BIT",
			"a click on 12-BIT sets it back to 0");
		put(choiceOf({Family::OpenDrum, 0}));
		click(_editor, _editor.machinePageArea(1).getCentre());
	}
	// A host's transport playing at 120 BPM, at the quarter set
	struct Transport final : juce::AudioPlayHead
	{
		juce::Optional<PositionInfo> getPosition() const override
		{
			PositionInfo info;
			info.setBpm(120);
			info.setIsPlaying(true);
			info.setPpqPosition(quarters);
			return info;
		}
		double quarters = 0;
	};

	void snapshot(Editor& _editor, const juce::File& _file);

	// Seq's lanes drawn as the Processor plays them (Editor::laneSteps): a Processor of its own plays Track 1 for two laps
	// at 120 BPM, what its engine plays of Machine 1 (Engine::played, a Hit's lock too) read at each step's middle on the
	// second lap and compared with what the lane draws there. A step of a condition that fails draws as one that may.
	// Seq's Detail with that lane rendered to _folder
	void lanesAsPlayed(const juce::File& _folder)
	{
		Processor processor;
		Editor editor(processor);
		auto* m1 = processor.parameter("t01_m1");
		const auto lockOf = [](juce::RangedAudioParameter* _p) { return static_cast<uint16_t>(_p->getParameterIndex()); };
		m1->setValueNotifyingHost(m1->convertTo0to1(40));
		const auto lap = [&](const std::function<void(Pattern&)>& _set, const juce::String& _what)
			-> std::vector<Editor::LaneStep>
		{
			processor.editSequencer([&](Sequencer& _s)
			{
				auto& p = _s.pattern(_s.currentPattern());
				p.clearTrack(0);
				_set(p);
			});
			editor.tick();
			const auto drawn = editor.laneSteps(m1->getParameterIndex());
			Transport transport;
			processor.setPlayHead(&transport);
			constexpr int Block = 256;
			constexpr double Rate = 44100, Step = Rate / 2 / 4;	// a 16th at 120 BPM
			processor.prepareToPlay(Rate, Block);
			juce::AudioBuffer<float> buffer(processor.getTotalNumOutputChannels(), Block);
			juce::MidiBuffer midi;
			std::array<int, 16> played{};
			for(int at = 0, next = 16; next < 32; at += Block)
			{
				transport.quarters = at / (Rate / 2);
				processor.processBlock(buffer, midi);
				while(next < 32 && (next + 0.5) * Step <= at + Block)
					played[static_cast<size_t>(next++ - 16)] = processor.engine()->played(0, 0);
			}
			processor.setPlayHead(nullptr);
			juce::StringArray wrong;
			for(int s = 0; s < 16; ++s)
			{
				const auto& d = drawn[static_cast<size_t>(s)];
				const float mid = m1->convertFrom0to1((d.from + d.to) / 2);
				if(std::abs(mid - static_cast<float>(played[static_cast<size_t>(s)])) > 1.5f)
					wrong.add(juce::String(s + 1) + ": drawn " + juce::String(mid, 1) + ", played "
						+ juce::String(played[static_cast<size_t>(s)]));
			}
			check(wrong.isEmpty(), "a lane drawn as played, " + _what + (wrong.isEmpty() ? juce::String()
				: ": " + wrong.joinIntoString(" | ")));
			return drawn;
		};
		// 1: a trig on 5, a trigless Lock on 9: from the second lap on, steps 1-5 play the Lock
		lap([&](Pattern& _p)
		{
			_p.setTrig(0, 4, true);
			_p.setLock(0, 8, lockOf(m1), 100);
		}, "a Lock held across the loop's end");
		// 2: a trigless Lock on 4, the next trig on 9: steps 4-8 play it, then the base
		lap([&](Pattern& _p)
		{
			_p.setLock(0, 3, lockOf(m1), 100);
			_p.setTrig(0, 8, true);
		}, "a Lock held to the next trig");
		// 3: a trig on 1 sliding Machine 1 from 100 to the base of the trig on 9; a trigless Lock of Machine 2 on 5 ends
		// the glide there
		lap([&](Pattern& _p)
		{
			_p.setTrig(0, 0, true);
			_p.step(0, 0).slide = true;
			_p.setLock(0, 0, lockOf(m1), 100);
			_p.setLock(0, 4, lockOf(processor.parameter("t01_m2")), 90);
			_p.setTrig(0, 8, true);
		}, "a slide ended by the next event");
		// 4: a trig on 1 locking Machine 1, the Hit's Lock; a trigless Lock of Machine 2 on 5 does not release it, the
		// next trig on 9 does
		lap([&](Pattern& _p)
		{
			_p.setTrig(0, 0, true);
			_p.setLock(0, 0, lockOf(m1), 100);
			_p.setLock(0, 4, lockOf(processor.parameter("t01_m2")), 90);
			_p.setTrig(0, 8, true);
		}, "a trig's Lock of Machine 1 held to the next trig");
		// 5: a trigless Lock on 4, a Lock of 20 on 7 that plays in a fill alone, the next trig on 9: steps 4-8 play the
		// first, the second drawn as one that may, of 20, holding nothing
		const auto may = lap([&](Pattern& _p)
		{
			_p.setLock(0, 3, lockOf(m1), 100);
			_p.setLock(0, 6, lockOf(m1), 20);
			_p.step(0, 6).condition = {ConditionKind::Fill, 0, 0};
			_p.setTrig(0, 8, true);
		}, "a Lock of a condition that fails");
		const auto& six = may[6];
		check(six.may && !six.locked && juce::roundToInt(m1->convertFrom0to1(six.would)) == 20
			&& std::count_if(may.begin(), may.end(), [](const Editor::LaneStep& _s) { return _s.may; }) == 1,
			"a Lock that plays in a fill alone: drawn as one that may, of 20, the others not");
		editor.showPage(Editor::Page::Seq);
		snapshot(editor, _folder.getChildFile("seq-lane-may.png"));
		editor.showPage(Editor::Page::Sound);

		// A list holds its Lock the same way: the filter's TYPE locked on 4, the next trig on 9
		auto* type = processor.parameter("t01_flt_type");
		processor.editSequencer([&](Sequencer& _s)
		{
			auto& p = _s.pattern(_s.currentPattern());
			p.clearTrack(0);
			p.setLock(0, 3, lockOf(type), 2);
			p.setTrig(0, 8, true);
		});
		const auto list = editor.laneSteps(type->getParameterIndex());
		bool held = true;
		for(int s = 0; s < 16; ++s)
			held = held && juce::roundToInt(type->convertFrom0to1(list[static_cast<size_t>(s)].from)) == (s >= 3 && s < 8 ? 2
				: juce::roundToInt(type->convertFrom0to1(type->getValue())));
		check(held, "the filter's TYPE locked on 4: steps 4-8 play WINDOW, the others the base");

		// NOTE: on a trig the step's note plays over a Lock of Note (Processor::applyEvent), and holds to the next event
		auto* note = processor.parameter("t01_note");
		processor.editSequencer([&](Sequencer& _s)
		{
			auto& p = _s.pattern(_s.currentPattern());
			p.clearTrack(0);
			p.setTrig(0, 2, true);
			p.step(0, 2).note = 72;
			p.setLock(0, 2, lockOf(note), 50);
			p.setTrig(0, 6, true);
		});
		const auto notes = editor.laneSteps(note->getParameterIndex());
		const auto plain = [&](const int _s) { return juce::roundToInt(note->convertFrom0to1(notes[static_cast<size_t>(_s)].from)); };
		check(plain(2) == 72 && plain(5) == 72 && plain(6) == juce::roundToInt(note->convertFrom0to1(note->getValue())),
			"NOTE: the trig's note 72 over its Lock of 50, held to the next trig");
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

	// ---- ticket 27: the names ----

	// What a text shown names of a brand, a machine of origin, or an old machine's code or name: empty for none
	juce::String brandIn(const juce::String& _text)
	{
		static const char* const Words[]{"ELEKTRON", "MACHINEDRUM", "SYNTAKT", "RYTM", "DIGITAKT", "DIGITONE", "DP/4",
			"DP4", "EVENTIDE", "VIRUS", "ACCESS", "OBERHEIM", "LINN", "ROLAND", "TR-", "ERICA", "LXR", "SOPHIE",
			"OPEN DRUM", "DT2", "TRX-", "EFM-", "E12-", "P-I-", "GND-", "ROM-", "BD MODERN", "SD BASIC", "CY ALLOY",
			"PC CARBON", "SD VINTAGE", "CP VINTAGE", "SY TOY", "SY BITS", "SP TWINSHOT", "OD DRUM", "OD SNARE",
			"OD CYMBAL", "OD HIHAT", "GATE BOX", "MD EQ", "MD DIST", "MD SRR", "BASE-WIDTH"};
		const auto upper = _text.toUpperCase();
		for(const char* w : Words)
			if(upper.contains(w))
				return w;
		const auto words = juce::StringArray::fromTokens(upper, " ,.:;()/", "");
		for(const char* w : {"MD", "UW", "OD"})
			if(words.contains(w))
				return w;
		return {};
	}

	// Every machine of the list named in 10 characters, none twice, with its family; each control it uses named in 12 at
	// most where the machine of origin names one (its OS's table, OPEN DRUM's and SOPHIE's names), and none it does not use
	void machineNames(const Processor& _processor)
	{
		const auto& list = _processor.machines();
		juce::StringArray seen;
		int wrong = 0, controls = 0;
		const auto fail = [&](const juce::String& _what) { std::printf("  %s\n", _what.toRawUTF8()); ++wrong; };
		for(const auto& m : list)
		{
			const char* name = names::machine(m);
			const juce::String n = name ? juce::String(name) : juce::String("?");
			if(!name || n.length() > 10 || seen.contains(n) || brandIn(n).isNotEmpty()
				|| (m.family != Family::Off && !names::family(m)))
				fail("machine " + n);
			seen.add(n);
			for(int i = 0; i < MachineParameters; ++i)
			{
				const char* c = names::control(m, i);
				const bool used = i < machineParameters(m);
				bool origin = used;
				if(m.family == Family::OpenDrum || m.family == Family::Sophie)
					origin = machineParameterName(m, i) != nullptr;
				else if(m.family == Family::SyntaktAnalog)
					origin = used && analog::usesControl(static_cast<analog::Machine>(m.id), i);
				else if(m.family == Family::RytmAnalog)
					origin = used && rytm::Voice::usesControl(static_cast<rytm::Machine>(m.id), i);
				else if(m.family == Family::Machinedrum && !(m == Sampler))
					if(const auto* os = md::machines::parameterNames(md::MachineModel::Machinedrum, m.id))
						origin = used && !(*os)[static_cast<size_t>(i)].empty();
				const bool hold = m.family == Family::Syntakt && i == 8;	// HOLD, on no page
				if((used || !hold) && (c != nullptr) != origin)
					fail(n + " Machine " + juce::String(i + 1) + (c ? juce::String(" ") + c : juce::String(" unnamed")));
				if(c && (std::strlen(c) > 12 || brandIn(c).isNotEmpty()))
					fail(n + " " + c);
				controls += c && used;
			}
		}
		check(wrong == 0, juce::String(static_cast<int>(list.size())) + " machines named in 10 characters at most, none "
			"twice, each in its family; " + juce::String(controls) + " controls named in 12 at most where the machine uses them");
	}

	// Every name and choice the host sees and every value's text of a parameter of 256 values at most: no brand; a Track
	// parameter's first 10 characters after "T01 " (a shared one's first 10) its own
	void hostNames(Processor& _processor)
	{
		std::map<juce::String, juce::String> heads;
		juce::StringArray branded, clashes;
		int texts = 0;
		for(auto* p : _processor.getParameters())
		{
			const auto name = p->getName(64);
			juce::StringArray shown{name};
			if(const auto* c = dynamic_cast<juce::AudioParameterChoice*>(p))
				shown.addArray(c->choices);
			else if(const auto* r = dynamic_cast<juce::RangedAudioParameter*>(p);
				r && !dynamic_cast<juce::AudioParameterFloat*>(p) && r->getNormalisableRange().getRange().getLength() < 256)
				for(int v = juce::roundToInt(r->getNormalisableRange().start); v <= juce::roundToInt(r->getNormalisableRange().end); ++v)
					shown.add(r->getText(r->convertTo0to1(static_cast<float>(v)), 0));
			for(const auto& s : shown)
				if(brandIn(s).isNotEmpty())
					branded.addIfNotAlreadyThere(name + ": " + s);
			texts += shown.size();
			const bool track = name.length() > 4 && name[0] == 'T' && name.substring(1, 3).containsOnly("0123456789")
				&& name[3] == ' ';
			const auto head = track ? name.substring(0, 4) + name.substring(4, 14) : name.substring(0, 10);
			if(const auto found = heads.find(head); found != heads.end())
				clashes.add(found->second + " / " + name);
			heads[head] = name;
		}
		check(branded.isEmpty(), "the host: " + juce::String(_processor.getParameters().size()) + " parameters, "
			+ juce::String(texts) + " names, choices and values, none names a brand or an old machine"
			+ (branded.isEmpty() ? juce::String() : ": " + branded.joinIntoString(" | ")));
		check(clashes.isEmpty(), "the host: each name's first 10 characters after \"T01 \" its own"
			+ (clashes.isEmpty() ? juce::String() : ": " + clashes.joinIntoString(" | ")));
	}

	// On Track 1, every machine of the list: each page of its Machine block, its cells named as names::control says,
	// every name fitting its cell; LFO 1's TARGET on Machine 1 by the machine's name; its Browse card with its family.
	// Then Seq's lock lines on Machine 1-3, every FX source and mode, Master, Kits. Nothing drawn names a brand
	void editorNames(Editor& _editor, Processor& _processor)
	{
		juce::Image image(juce::Image::ARGB, 4, 4, true);
		juce::Graphics g(image);
		const ui::Painter painter(g, 1.0f);
		juce::StringArray drawn, unfit, misnamed;
		ui::Painter::drawnTexts = &drawn;
		const auto draw = [&]
		{
			_editor.tick();
			_editor.createComponentSnapshot(_editor.getLocalBounds(), true, 1.0f);
		};
		// Every visible cell's name inside its cell, a gutter of 2 mm left
		const auto fits = [&]
		{
			for(const auto& c : _editor.cells())
				if(c.visible && (c.look == ui::Cell::Look::Cell || c.look == ui::Cell::Look::Plain) && c.name.isNotEmpty()
					&& painter.textWidth(c.name, 3.2f) > c.w - 2)
					unfit.addIfNotAlreadyThere(c.name + " (" + juce::String(painter.textWidth(c.name, 3.2f), 1) + " of "
						+ juce::String(c.w - 2, 1) + " mm)");
		};
		auto* machine = _processor.parameter("t01_machine");
		auto* target = _processor.parameter("t01_lfo1_target");
		const float machineWas = machine->getValue(), targetWas = target->getValue();
		target->setValueNotifyingHost(target->convertTo0to1(static_cast<float>(LfoTarget::Machine1)));
		const auto& list = _processor.machines();
		_editor.showPage(Editor::Page::Sound);
		_editor.showLfo(0);
		for(size_t k = 0; k < list.size(); ++k)
		{
			const auto& m = list[k];
			machine->setValueNotifyingHost(machine->convertTo0to1(static_cast<float>(k)));
			_editor.tick();
			for(int page = 0; page < _editor.machinePages(); ++page)
			{
				_editor.showMachinePage(page);
				for(const auto& c : _editor.cells())
				{
					const auto id = c.parameter ? c.parameter->getParameterID() : juce::String();
					if(id == "t01_lfo1_target")
					{
						const char* first = names::control(m, 0);
						if(c.valueText(c.normalised()) != (first ? juce::String(first) : juce::String("MACHINE 1")))
							misnamed.add(juce::String(names::machine(m)) + " TARGET " + c.valueText(c.normalised()));
					}
					if(!id.startsWith("t01_m") || !id.substring(5).containsOnly("0123456789"))
						continue;
					const int i = id.substring(5).getIntValue() - 1;
					const char* expected = names::control(m, i);
					if(c.name != (expected ? juce::String(expected) : "M" + juce::String(i + 1)))
						misnamed.add(juce::String(names::machine(m)) + " " + id + " " + c.name);
				}
				fits();
				draw();
			}
			_editor.showMachinePage(0);
			// The machine's name opens Browse on it: its card and its family
			click(_editor, _editor.cellArea("t01_machine").getCentre());
			draw();
			if(const char* family = names::family(m); family && !drawn.contains(family))
				misnamed.add(juce::String(names::machine(m)) + ": no FAMILY " + family);
			_editor.showPage(Editor::Page::Sound);
		}
		check(misnamed.isEmpty(), "Sound: every machine's cells and LFO TARGET named as the machine names them"
			+ (misnamed.isEmpty() ? juce::String() : ": " + misnamed.joinIntoString(" | ")));

		// Seq: lock lines on Machine 1-3 of ROUND KICK, named as it names them
		const Machine kick{Family::Machinedrum, 16};
		machine->setValueNotifyingHost(machine->convertTo0to1(static_cast<float>(
			std::find(list.begin(), list.end(), kick) - list.begin())));
		_processor.editSequencer([&](Sequencer& _s)
		{
			for(int i = 0; i < 3; ++i)
				_s.pattern(_s.currentPattern()).setLock(0, 4 + i, static_cast<uint16_t>(
					_processor.parameter("t01_m" + juce::String(i + 1))->getParameterIndex()), 30.0f);
		});
		_editor.showPage(Editor::Page::Seq);
		const int before = drawn.size();
		draw();
		juce::StringArray seq;
		for(int i = before; i < drawn.size(); ++i)
			seq.add(drawn[i]);
		check(seq.contains("TUNE") && seq.contains("DECAY") && seq.contains("SWEEP"), "Seq: ROUND KICK's lock lines read "
			"TUNE, DECAY, SWEEP");

		// FX: each delay and reverb, each phaser type, each rate synced and free
		_editor.showPage(Editor::Page::Fx);
		for(const char* id : {"delay_source", "reverb_source", "phaser_mode", "delay_sync", "chorus_sync", "phaser_sync"})
		{
			auto* p = _processor.parameter(id);
			const float was = p->getValue();
			for(const float v : {0.0f, 0.5f, 1.0f})
			{
				p->setValueNotifyingHost(v);
				_editor.tick();
				fits();
				draw();
			}
			p->setValueNotifyingHost(was);
		}
		for(const auto page : {Editor::Page::Master, Editor::Page::Kits, Editor::Page::Sound})
		{
			_editor.showPage(page);
			_editor.tick();
			fits();
			draw();
		}
		_editor.showLfo(1);
		fits();
		draw();
		_editor.showLfo(0);
		ui::Painter::drawnTexts = nullptr;
		machine->setValueNotifyingHost(machineWas);
		target->setValueNotifyingHost(targetWas);
		_editor.tick();

		juce::StringArray branded;
		for(const auto& t : drawn)
			if(brandIn(t).isNotEmpty())
				branded.addIfNotAlreadyThere(t);
		check(branded.isEmpty(), "Sound, Browse's cards, Seq, FX, Master, Kits: " + juce::String(drawn.size())
			+ " texts drawn, none names a brand or an old machine" + (branded.isEmpty() ? juce::String() : ": "
			+ branded.joinIntoString(" | ")));
		check(unfit.isEmpty(), "every cell's name fits its cell" + (unfit.isEmpty() ? juce::String() : ": "
			+ unfit.joinIntoString(" | ")));
	}

	// Ticket 23: every gesture of the step box (ticket 09's list as spec 10 and t-ui-spec's direct trig editing and lock
	// model have it: no modifier, the strips in place of Ctrl and Shift, a Lock on a step without a trig the Lock without
	// a note in place of Alt), played on Track 1 in Seq's Detail and on its strips, each read back from the pattern the
	// sequencer plays
	void stepGestures(Editor& _editor, Processor& _processor)
	{
		using SB = Editor::SeqButton;
		const auto pattern = [&]() -> const Pattern& { return _processor.sequencer().pattern(_processor.currentPattern()); };
		const auto st = [&](const int _s) -> const Step& { return pattern().step(0, _s); };
		const auto index = [&](const juce::String& _id) { return _processor.parameter(_id)->getParameterIndex(); };
		const auto lockOn = [&](const int _s, const juce::String& _id) -> const Lock*
		{
			const auto* locks = pattern().locks(0, _s);
			for(int i = 0; locks && i < st(_s).lockCount; ++i)
				if(locks[i].parameter == index(_id))
					return &locks[i];
			return nullptr;
		};
		const auto all = [](const int _from, const int _to, const std::function<bool(int)>& _ok)
		{
			for(int s = _from; s <= _to; ++s)
				if(!_ok(s))
					return false;
			return true;
		};
		_processor.editSequencer([](Sequencer& _s) { _s.pattern(_s.currentPattern()).clearTrack(0); });
		_editor.showTrack(0);
		_editor.showPage(Editor::Page::Seq);
		_editor.showSeqTab(0);
		_editor.tick();
		const float mm = static_cast<float>(_editor.getWidth()) / ui::UnitWidth;
		const auto row = _editor.trigRowArea();
		const float pitch = static_cast<float>(row.getWidth()) / 16;
		const auto at = [&](const juce::Rectangle<int> _r, const int _s)
		{
			return juce::Point<int>(_r.getX() + juce::roundToInt((static_cast<float>(_s) + 0.5f) * pitch), _r.getCentreY());
		};
		const auto steps = [&](const int _n) { return juce::Point<int>(juce::roundToInt(static_cast<float>(_n) * pitch), 0); };
		const auto down = [&](const float _mm) { return juce::Point<int>(0, juce::roundToInt(_mm * mm)); };

		// VEL: a click sets a trig at the highest velocity, a second takes it off
		click(_editor, _editor.seqButtonArea(SB::Vel).getCentre());
		click(_editor, at(row, 2));
		check(st(2).trig && st(2).velocity == 127, "VEL: a click on step 3 sets a trig at 127");
		click(_editor, at(row, 2));
		check(!st(2).trig, "VEL: a second click takes it off");
		// A horizontal drag from an empty step paints trigs, one from a trig erases
		drag(_editor, at(row, 1), steps(4));
		check(all(1, 5, [&](int s) { return st(s).trig && st(s).velocity == 127; }) && !st(0).trig && !st(6).trig,
			"VEL: a drag from step 2 to 6 paints five trigs at 127");
		drag(_editor, at(row, 4), steps(-2));
		check(st(1).trig && all(2, 4, [&](int s) { return !st(s).trig; }) && st(5).trig,
			"VEL: a drag from the trig on 5 back to 3 erases 3-5");
		// A vertical drag on a trig sets its velocity, 1/32 of the range per mm, the trig kept; on an empty step nothing
		drag(_editor, at(row, 1), down(16));
		check(st(1).trig && std::abs(st(1).velocity - 64) <= 1, "VEL: 16 mm down on step 2's trig: velocity "
			+ juce::String(st(1).velocity));
		drag(_editor, at(row, 7), down(-10));
		check(!st(7).trig, "VEL: a vertical drag on an empty step sets no trig");

		// ACCENT and SLIDE: a click toggles the step's, a drag paints or erases, the trigs left as they are
		click(_editor, _editor.seqButtonArea(SB::Accent).getCentre());
		click(_editor, at(row, 1));
		check(st(1).accent && st(1).trig, "ACCENT: a click accents step 2, its trig kept");
		click(_editor, at(row, 1));
		check(!st(1).accent && st(1).trig, "ACCENT: a second click takes the accent off");
		drag(_editor, at(row, 0), steps(5));
		check(all(0, 5, [&](int s) { return st(s).accent; }) && !st(0).trig && !st(6).accent,
			"ACCENT: a drag from step 1 to 6 paints accents, no trig set");
		drag(_editor, at(row, 3), steps(2));
		check(all(0, 2, [&](int s) { return st(s).accent; }) && all(3, 5, [&](int s) { return !st(s).accent; }),
			"ACCENT: a drag from the accent on 4 erases 4-6");
		click(_editor, _editor.seqButtonArea(SB::Slide).getCentre());
		click(_editor, at(row, 5));
		check(st(5).slide && st(5).trig && !st(5).accent, "SLIDE: a click slides step 6");
		drag(_editor, at(row, 8), steps(2));
		check(all(8, 10, [&](int s) { return st(s).slide; }) && !st(11).slide, "SLIDE: a drag paints slides on 9-11");
		click(_editor, _editor.seqButtonArea(SB::Vel).getCentre());
		click(_editor, at(row, 9));
		check(st(9).trig && st(9).slide, "VEL again: a click sets a trig, the step's slide kept");

		// A lane (RESO): a click on an empty step locks it at the base, trig or not; a click on a Lock changes nothing; a
		// vertical drag sets the Lock's value; a double-click takes it off; a horizontal drag paints at the base or erases
		click(_editor, _editor.seqButtonArea(SB::AddLock).getCentre());
		click(_editor, _editor.lockMenuArea(index("t01_flt_res")).getCentre());
		check(_editor.lockLanes() == 1, "+ LOCK: RESO's lane");
		const auto lane = _editor.laneArea(0);
		auto* reso = _processor.parameter("t01_flt_res");
		const float base = reso->convertFrom0to1(reso->getValue());
		click(_editor, at(lane, 1));
		click(_editor, at(lane, 7));
		check(lockOn(1, "t01_flt_res") && lockOn(1, "t01_flt_res")->value == base && lockOn(7, "t01_flt_res")
			&& !st(7).trig, "a lane: a click on step 2 (a trig) and on step 8 (none, a Lock without a note) lock at the base");
		drag(_editor, at(lane, 1), down(-8));
		// The first 0.8 mm only make the press a drag (Editor::mouseDrag): 7.2 mm count, 0.225 of the range
		const float raised = std::min(1.0f, reso->getValue() + 0.225f);
		const float got = reso->convertTo0to1(lockOn(1, "t01_flt_res")->value);
		check(std::abs(got - raised) < 0.01f && reso->convertFrom0to1(reso->getValue()) == base,
			"a lane: 8 mm up on step 2's Lock raises it a quarter, to " + juce::String(got, 3) + ", RESO left as it was");
		click(_editor, at(lane, 1));
		check(std::abs(reso->convertTo0to1(lockOn(1, "t01_flt_res")->value) - got) < 1e-6f,
			"a lane: a click on a Lock changes nothing");
		doubleClick(_editor, at(lane, 10));
		check(!lockOn(10, "t01_flt_res"), "a lane: a double-click on an empty step sets and takes off, one toggle");
		doubleClick(_editor, at(lane, 7));
		check(!lockOn(7, "t01_flt_res"), "a lane: a double-click on a Lock takes it off");
		drag(_editor, at(lane, 11), steps(3));
		check(all(11, 14, [&](int s) { return lockOn(s, "t01_flt_res") && lockOn(s, "t01_flt_res")->value == base; }),
			"a lane: a drag from an empty step paints Locks at the base on 12-15");
		drag(_editor, at(lane, 12), steps(2));
		check(lockOn(11, "t01_flt_res") && all(12, 14, [&](int s) { return !lockOn(s, "t01_flt_res"); }),
			"a lane: a drag from a Lock erases 13-15");
		// A trig taken off takes its Locks with it (spec 7)
		click(_editor, at(row, 1));
		check(!st(1).trig && st(1).lockCount == 0, "VEL: the trig on step 2 taken off, its Lock goes too");

		// The strips: a press on step 3's and a drag to step 6's put 3-6 in edit and open Sound; a drag on FREQ locks
		// each of them, a Lock without a note where there is no trig, FREQ itself unchanged
		_processor.editSequencer([](Sequencer& _s)
		{
			auto& p = _s.pattern(_s.currentPattern());
			p.clearTrack(0);
			p.setTrig(0, 2, true);
		});
		_editor.tick();
		const auto strip = [&](const int _k)
		{
			return _editor.stepKeyArea(_k).getCentre() + juce::Point<int>(0, juce::roundToInt(21 * mm));
		};
		drag(_editor, strip(2), strip(5) - strip(2));
		check(_editor.page() == Editor::Page::Sound, "a strip's press opens Sound");
		auto* freq = _processor.parameter("t01_flt_freq");
		const float freqWas = freq->getValue();
		drag(_editor, _editor.cellArea("t01_flt_freq").getCentre(), {freqWas < 0.5f ? 50 : -50, 0});
		const auto* f2 = lockOn(2, "t01_flt_freq");
		check(f2 && all(3, 5, [&](int s) { return lockOn(s, "t01_flt_freq") && lockOn(s, "t01_flt_freq")->value == f2->value
			&& !st(s).trig; }) && st(2).trig && !lockOn(6, "t01_flt_freq") && freq->getValue() == freqWas,
			"strips 3-6 dragged, then FREQ: a Lock on each, without a note on 4-6, FREQ left as it was");
		// A press on a strip in edit takes its step out; a double-click on a cell takes the Locks of the steps in edit off
		click(_editor, strip(3));
		drag(_editor, _editor.cellArea("t01_flt_res").getCentre(), {40, 0});
		check(lockOn(2, "t01_flt_res") && !lockOn(3, "t01_flt_res") && lockOn(4, "t01_flt_res") && lockOn(5, "t01_flt_res"),
			"a press on strip 4 takes step 4 out of the edit: RESO locks 3, 5 and 6");
		doubleClick(_editor, _editor.cellArea("t01_flt_res").getCentre());
		check(all(2, 5, [&](int s) { return !lockOn(s, "t01_flt_res"); }) && lockOn(3, "t01_flt_freq"),
			"a double-click on RESO takes its Locks off the steps in edit, FREQ's kept");
		// A click outside leaves the edit: a drag then moves the parameter
		click(_editor, {10, 10});
		const int locksBefore = st(2).lockCount;
		drag(_editor, _editor.cellArea("t01_flt_freq").getCentre(), {freqWas < 0.5f ? 50 : -50, 0});
		check(freq->getValue() != freqWas && st(2).lockCount == locksBefore, "out of the edit, a drag on FREQ moves FREQ");
		freq->setValueNotifyingHost(freqWas);
		_processor.editSequencer([](Sequencer& _s) { _s.pattern(_s.currentPattern()).clearTrack(0); });
		_editor.tick();
	}

	// Ticket 23, LIVE: a click on Seq's LIVE turns it on; with the host's transport playing, a cell dragged on Sound is a
	// Lock of the step playing, the parameter moving too; a second click turns it off
	void liveGestures()
	{
		Processor processor;
		Editor editor(processor);
		editor.showPage(Editor::Page::Seq);
		editor.tick();
		click(editor, editor.seqButtonArea(Editor::SeqButton::Live).getCentre());
		check(processor.live(), "LIVE: a click turns it on");
		Transport transport;
		processor.setPlayHead(&transport);
		constexpr int Block = 256;
		constexpr double Rate = 44100;
		processor.prepareToPlay(Rate, Block);
		juce::AudioBuffer<float> buffer(processor.getTotalNumOutputChannels(), Block);
		juce::MidiBuffer midi;
		for(int at = 0; at < 40 * Block; at += Block)
		{
			transport.quarters = at / (Rate / 2);
			processor.processBlock(buffer, midi);
		}
		const int step = processor.playingStep(0);
		editor.showPage(Editor::Page::Sound);
		editor.tick();
		auto* freq = processor.parameter("t01_flt_freq");
		const float was = freq->getValue();
		drag(editor, editor.cellArea("t01_flt_freq").getCentre(), {was < 0.5f ? 50 : -50, 0});
		processor.takeLive();
		processor.setPlayHead(nullptr);
		const auto& p = processor.sequencer().pattern(processor.currentPattern());
		const auto* locks = step >= 0 ? p.locks(0, step) : nullptr;
		const bool locked = locks && p.step(0, step).lockCount == 1
			&& locks[0].parameter == freq->getParameterIndex()
			&& std::abs(locks[0].value - freq->convertFrom0to1(freq->getValue())) < 1e-3f;
		check(locked && freq->getValue() != was && !p.step(0, step).trig, "LIVE: FREQ dragged while step "
			+ juce::String(step + 1) + " plays is its Lock, FREQ moved too");
		editor.showPage(Editor::Page::Seq);
		editor.tick();
		click(editor, editor.seqButtonArea(Editor::SeqButton::Live).getCentre());
		check(!processor.live(), "LIVE: a second click turns it off");
	}

	// PAT LEN back to zero: the automatic length, whose value a drag starts from
	void patternLengthAuto()
	{
		Processor processor;
		Editor editor(processor);
		const int pattern = processor.currentPattern();
		processor.editSequencer([&](Sequencer& _s)
		{
			_s.pattern(pattern).masterLength = 48;
			_s.pattern(pattern).trackLength[3] = 20;
		});
		editor.showPage(Editor::Page::Seq);
		editor.tick();
		const auto at = editor.seqButtonArea(Editor::SeqButton::PatLength).getCentre();
		doubleClick(editor, at);
		editor.tick();
		const auto& p = processor.sequencer().pattern(pattern);
		check(p.masterLength == Pattern::AutoLength && p.length() == 32, "PAT LEN double-clicked: automatic, 32 for a Track"
			" of 20, got " + juce::String(p.masterLength) + " / " + juce::String(p.length()));
		drag(editor, at, {40, 0});
		editor.tick();
		check(p.masterLength > 32, "PAT LEN dragged from automatic starts at 32: " + juce::String(p.masterLength));
	}

	// Ticket 24, each read back from the Processor: Browse's MACHINES (a double-click, PUT ON TRACK, a drag onto Track 7
	// put the machine on), Master's M and S (the sequencer's mute and solo) and THR on the curve (the threshold, in one
	// gesture), FX's cells (a drag in one gesture, SYNC showing TIME or RATE), the Menu's TEMPO
	void machinesMasterFx(Editor& _editor, Processor& _processor)
	{
		const auto px = [&](const float _x, const float _y)
		{
			const float s = static_cast<float>(_editor.getWidth()) / ui::UnitWidth;
			return juce::Point<int>(juce::roundToInt(_x * s), juce::roundToInt(_y * s));
		};
		const auto plain = [&](const char* _id)
		{
			const auto* p = _processor.parameter(_id);
			return juce::roundToInt(p->convertFrom0to1(p->getValue()));
		};
		{
			auto* m1 = _processor.parameter("t01_machine");
			auto* m7 = _processor.parameter("t07_machine");
			const float was1 = m1->getValue(), was7 = m7->getValue();
			_editor.showTrack(0);
			_editor.showPage(Editor::Page::Browse);
			click(_editor, _editor.browseTreeArea(3).getCentre());
			_editor.tick();
			doubleClick(_editor, _editor.browseRowArea(3).getCentre());
			check(plain("t01_machine") == 3, "MACHINES: a double-click on row 4 puts machine 3 on Track 1");
			click(_editor, _editor.browseRowArea(4).getCentre());
			click(_editor, _editor.browseButtonArea(Editor::BrowseButton::Load).getCentre());
			check(plain("t01_machine") == 4, "MACHINES: row 5, then PUT ON TRACK");
			drag(_editor, _editor.browseRowArea(5).getCentre(), _editor.stepKeyArea(6).getCentre()
				- _editor.browseRowArea(5).getCentre());
			check(plain("t07_machine") == 5, "MACHINES: row 6 dragged onto Track 7 puts machine 5 there, "
				+ juce::String(plain("t07_machine")));
			m1->setValueNotifyingHost(was1);
			m7->setValueNotifyingHost(was7);
			_editor.tick();
		}
		{
			_editor.showPage(Editor::Page::Master);
			_editor.tick();
			const auto& seq = _processor.sequencer();
			const float xc = 22 + 2 * 13.5f;	// Track 3's strip
			click(_editor, px(xc, 218.8f));
			_editor.tick();
			check(seq.mute(2) && _editor.repainted() > 0, "Master: a click on Track 3's M mutes it, drawn");
			click(_editor, px(xc, 218.8f));
			check(!seq.mute(2), "Master: a second click unmutes it");
			click(_editor, px(xc, 228.2f));
			check(seq.solo(2), "Master: a click on Track 3's S solos it");
			click(_editor, px(xc, 228.2f));
			check(!seq.solo(2), "Master: a second click takes the solo off");
			_editor.tick();

			Gestures gestures;
			_processor.addListener(&gestures);
			auto* thr = _processor.parameter("master_threshold");
			const float was = thr->getValue();
			thr->setValueNotifyingHost(0.5f);
			const float before = thr->getValue();
			const float a = ui::cx(15) - 2 + 8;
			drag(_editor, px(a + 48, 186), px(20, 0));
			check(thr->getValue() - before > 0.15f && gestures.begun == 1 && gestures.ended == 1,
				"Master: THR dragged 20 mm right on the curve raises the threshold from " + juce::String(before, 3) + " to "
				+ juce::String(thr->getValue(), 3) + ", in one gesture");
			_processor.removeListener(&gestures);
			thr->setValueNotifyingHost(was);
		}
		{
			_editor.showPage(Editor::Page::Fx);
			_editor.tick();
			Gestures gestures;
			_processor.addListener(&gestures);
			auto* depth = _processor.parameter("chorus_depth");
			const float before = depth->getValue();
			const int sign = before < 0.5f ? 1 : -1;
			drag(_editor, _editor.cellArea("chorus_depth").getCentre(), {sign * 60, 0});
			check((depth->getValue() - before) * static_cast<float>(sign) > 0.2f && gestures.begun == 1
				&& gestures.ended == 1, "FX: a drag on the chorus's DEPTH moves it in one gesture");
			_processor.removeListener(&gestures);
			depth->setValueNotifyingHost(before);
			const int sync = plain("delay_sync");
			click(_editor, _editor.cellArea("delay_sync").getCentre());
			_editor.tick();
			const bool synced = plain("delay_sync") != 0;
			const bool echo = plain("delay_source") == 0;
			check(plain("delay_sync") != sync && _editor.cellArea("delay_note").isEmpty() != synced
				&& _editor.cellArea("delay_free").isEmpty() == synced
				&& _editor.cellArea(echo ? "echo_time" : "delay_time").isEmpty() == synced
				&& _editor.cellArea(echo ? "delay_time" : "echo_time").isEmpty(),
				"FX: SYNC toggles, DURATION or RATE and the source's own TIME shown as it says");
			// With SYNC off, the source's TIME dragged in one gesture, in note lengths as the engine plays it
			const auto timeId = echo ? "echo_time" : "delay_time";
			if(synced)
			{
				click(_editor, _editor.cellArea("delay_sync").getCentre());
				_editor.tick();
			}
			auto* time = _processor.parameter(timeId);
			const float timeWas = time->getValue();
			Gestures timeGestures;
			_processor.addListener(&timeGestures);
			const int timeSign = timeWas < 0.5f ? 1 : -1;
			drag(_editor, _editor.cellArea(timeId).getCentre(), {timeSign * 60, 0});
			check(plain("delay_sync") == 0 && (time->getValue() - timeWas) * static_cast<float>(timeSign) > 0.2f
				&& timeGestures.begun == 1 && timeGestures.ended == 1, juce::String("FX: ") + timeId
				+ " dragged in one gesture, reads " + time->getCurrentValueAsText());
			_processor.removeListener(&timeGestures);
			time->setValueNotifyingHost(timeWas);
			if(plain("delay_sync") != sync)
			{
				click(_editor, _editor.cellArea("delay_sync").getCentre());
				_editor.tick();
			}
		}
		{
			_editor.showMenu(true);
			_editor.tick();
			auto* tempo = _processor.parameter("tempo");
			const float before = tempo->getValue();
			drag(_editor, _editor.cellArea("tempo").getCentre(), {60, 0});
			check(tempo->getValue() > before, "the Menu's TEMPO dragged: " + tempo->getCurrentValueAsText());
			tempo->setValueNotifyingHost(before);
			_editor.showMenu(false);
			_editor.showPage(Editor::Page::Sound);
			_editor.tick();
		}
	}

	// Ticket 24: every host parameter has a control the mouse reaches, on an editor of its own. Sound shows each
	// Track's, the Track put on the machine that uses the most of Machine 1-24, its pages turned (a cell an LFO card, a
	// SYNC or a MODE hides counts: those choose it); FX's in each of the 32 states of its SOURCE and SYNC choices, a
	// cell counting where it shows; Master's, the Menu's, Seq Detail's PATTERN. None for the relays for Push, which aim
	// at the Track shown
	void reachable()
	{
		Processor processor;
		Editor editor(processor);
		std::set<juce::String> bound;
		const auto collect = [&](const bool _shownOnly)
		{
			editor.tick();
			for(const auto& c : editor.cells())
				if(c.parameter && (c.visible || !_shownOnly))
					bound.insert(c.parameter->getParameterID());
		};
		const auto& machines = processor.machines();
		size_t widest = 0;
		for(size_t m = 0; m < machines.size(); ++m)
			if(machineParameters(machines[m]) > machineParameters(machines[widest]))
				widest = m;
		editor.showPage(Editor::Page::Sound);
		for(int t = 0; t < 16; ++t)
		{
			auto* machine = processor.parameter(juce::String::formatted("t%02d_machine", t + 1));
			machine->setValueNotifyingHost(machine->convertTo0to1(static_cast<float>(widest)));
			editor.showTrack(t);
			editor.tick();
			for(int page = 0; page < editor.machinePages(); ++page)
			{
				editor.showMachinePage(page);
				collect(false);
			}
			editor.showMachinePage(0);
		}
		editor.showPage(Editor::Page::Fx);
		const char* toggles[]{"delay_source", "reverb_source", "delay_sync", "chorus_sync", "phaser_sync"};
		for(int s = 0; s < 32; ++s)
		{
			for(int b = 0; b < 5; ++b)
			{
				auto* p = processor.parameter(toggles[b]);
				p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>((s >> b) & 1)));
			}
			collect(true);
		}
		editor.showPage(Editor::Page::Master);
		collect(true);
		editor.showPage(Editor::Page::Seq);
		editor.showSeqTab(0);
		collect(true);
		editor.showMenu(true);
		collect(true);
		editor.showMenu(false);

		const auto& relays = processor.ids(Processor::Section::Relays);
		juce::StringArray missing;
		int counted = 0;
		for(auto* parameter : processor.getParameters())
		{
			const auto* p = dynamic_cast<const juce::RangedAudioParameter*>(parameter);
			if(!p || relays.contains(p->getParameterID()))
				continue;
			++counted;
			if(bound.count(p->getParameterID()) == 0)
				missing.add(p->getParameterID());
		}
		check(missing.isEmpty() && counted > 0, "every host parameter has a control: " + juce::String(counted) + " of "
			+ juce::String(processor.getParameters().size()) + " (less " + juce::String(relays.size())
			+ " relays)" + (missing.isEmpty() ? juce::String() : ", none for " + missing.joinIntoString(" ")));
	}

	// Ticket 24: Master's GR, on an editor of its own: loud Hits through the master on, its reduction below 0 dB and
	// its bar and trace repainted
	void masterReduction()
	{
		Processor processor;
		Editor editor(processor);
		editor.showPage(Editor::Page::Master);
		const auto set = [&](const char* _id, const float _plain)
		{
			auto* p = processor.parameter(_id);
			p->setValueNotifyingHost(p->convertTo0to1(_plain));
		};
		set("master_on", 1);
		set("master_threshold", 0);
		set("master_ratio", 127);
		editor.tick();
		processor.setRateAndBufferSizeDetails(44100, 256);
		processor.prepareToPlay(44100, 256);
		juce::AudioBuffer<float> buffer(processor.getTotalNumOutputChannels(), 256);
		float least = 0;
		for(int i = 0; i < 200; ++i)
		{
			juce::MidiBuffer midi;
			if(i % 20 == 0)
				for(int t = 0; t < 4; ++t)
					midi.addEvent(juce::MidiMessage::noteOn(1, 36 + t, static_cast<juce::uint8>(127)), 0);
			processor.processBlock(buffer, midi);
			least = std::min(least, processor.masterReduction());
		}
		editor.tick();
		editor.tick();
		check(least < -0.5f && editor.repainted() > 0, "Master: GR reads " + juce::String(least, 1)
			+ " dB at its deepest, the bar and trace repainted");
	}

	void run(const juce::File& _folder)
	{
		_folder.createDirectory();
		Processor processor;
		Editor editor(processor);
		std::printf("face: %s\n", ui::Painter::faceName().toRawUTF8());
		check(editor.getWidth() == Editor::Width && editor.getHeight() == Editor::Height, "the editor at 1600 x 1000");
		lanesAsPlayed(_folder);
		liveGestures();
		patternLengthAuto();

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

		// Sound after u.html's variant 2: the LFO cards choose the LFO shown, no tabs; a drive's TYPE stays live while the
		// drive is off, its list opening on a click
		{
			click(editor, editor.lfoCardArea(1).getCentre());
			check(editor.lfo() == 1 && !editor.cellArea("t01_lfo2_depth").isEmpty() && editor.cellArea("t01_lfo1_depth").isEmpty(),
				"a click on LFO 2's card shows LFO 2's cells");
			click(editor, editor.lfoCardArea(0).getCentre());
			check(editor.lfo() == 0, "a click on LFO 1's card shows LFO 1 again");
			auto* type = processor.parameter("t01_drv2_type");
			const float was = type->getValue();
			type->setValueNotifyingHost(0.0f);
			editor.tick();
			click(editor, editor.cellArea("t01_drv2_type").getCentre());
			click(editor, editor.selectorItemArea(1).getCentre());
			check(juce::roundToInt(type->convertFrom0to1(type->getValue())) == 1, "Drive 2 off: a click on its TYPE and one on "
				"the list's second entry turn it on");
			type->setValueNotifyingHost(was);
			editor.tick();
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

			// Ticket 24: RENAME in place over the Slot's title; EXPORT, then IMPORT into Slot 10 (the chooser given by the
			// test); a second click on DELETE in a row empties the Slot
			click(editor, editor.kitActionArea(2).getCentre());
			check(editor.renameField().isVisible() && editor.renameField().getText() == "Dusty",
				"RENAME opens its field on the Kit's name");
			editor.renameField().setText("Dusty Room", false);
			editor.renameField().onReturnKey();
			check(!editor.renameField().isVisible() && processor.kitSlot(5).name == "Dusty Room",
				"RENAME names Slot 6 " + processor.kitSlot(5).name);
			const auto kitFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
				.getChildFile("edDrumsEditorKit.edkit");
			kitFile.deleteFile();
			Editor::chooseFileForTests = [kitFile](const juce::String&) { return kitFile; };
			click(editor, editor.kitActionArea(5).getCentre());
			check(kitFile.existsAsFile(), "EXPORT writes Slot 6 to " + kitFile.getFileName());
			click(editor, editor.kitSlotArea(9).getCentre());
			click(editor, editor.kitActionArea(4).getCentre());
			check(processor.kitSlot(9).name == "Dusty Room", "IMPORT reads it into Slot 10: " + processor.kitSlot(9).name);
			Editor::chooseFileForTests = nullptr;
			click(editor, editor.kitActionArea(3).getCentre());
			check(!processor.kitSlot(9).empty(), "DELETE on Slot 10 asks again");
			click(editor, editor.kitActionArea(3).getCentre());
			check(processor.kitSlot(9).empty(), "the second click empties Slot 10");
			kitFile.deleteFile();
			click(editor, editor.kitSlotArea(2).getCentre());
			freq->setValueNotifyingHost(0.5f);
		}

		// Browse, the LOCKER (ticket 20, stockage-proposition.md): a folder of three sounds and a text file dropped on
		// Browse joins it linked; SEARCH filters; a double-click loads a sample on the Track shown, a drag onto Track 6
		// there; a source chosen by its segment shows its card, RENAME names it, REMOVE SOURCE takes it out, a second
		// click where it erases a copy; IN THIS SET's sample kept in the Locker
		{
			using B = Editor::BrowseButton;
			const auto temp = juce::File::getSpecialLocation(juce::File::tempDirectory);
			const auto samples = temp.getChildFile("edDrumsEditorSamples"), pack = temp.getChildFile("edDrumsEditorPack");
			for(const auto& f : {samples, pack})
			{
				f.deleteRecursively();
				f.createDirectory();
			}
			juce::WavAudioFormat wav;
			juce::AiffAudioFormat aiff;
			writeSound(wav, samples.getChildFile("kick_01.wav"), 44100, 13230);
			writeSound(aiff, samples.getChildFile("SD-snare.aif"), 48000, 9600);
			writeSound(wav, samples.getChildFile("hh closed.wav"), 44100, 4410);
			samples.getChildFile("notes.txt").replaceWithText("no audio");
			writeSound(wav, pack.getChildFile("clap 909.wav"), 44100, 6000);
			writeSound(wav, temp.getChildFile("edDrumsEditorElsewhere.wav"), 44100, 8000);
			auto& library = processor.sampleLibrary();
			const auto settle = [&]
			{
				const auto until = juce::Time::getMillisecondCounter() + 20000;
				while(!processor.takeLockerImport() && juce::Time::getMillisecondCounter() < until)
					juce::Thread::sleep(5);
				while((!library.update() || library.scanning()) && juce::Time::getMillisecondCounter() < until)
					juce::Thread::sleep(5);
				editor.tick();
				editor.tick();
			};
			// A source's segment: ALL first, then the sources in their order
			const auto segmentOf = [&](const juce::String& _name)
			{
				juce::StringArray names;
				for(const auto& s : library.sources())
					names.add(s.name.toUpperCase());
				return names.indexOf(_name.toUpperCase()) + 1;
			};

			editor.showPage(Editor::Page::Browse);
			click(editor, editor.browseTreeArea(2).getCentre());
			editor.tick();
			editor.filesDropped({samples.getFullPathName()}, editor.browseRowArea(3).getCentre().x,
				editor.browseRowArea(3).getCentre().y);
			settle();
			check(library.sources().size() == 1 && library.sources()[0].kind == SampleLibrary::Kind::Linked
				&& library.entries().size() == 3, "a folder dropped on Browse joins the LOCKER linked, "
				+ juce::String(library.entries().size()) + " samples");
			const int factoryCount = static_cast<int>(processor.factorySamples().size());
			check(editor.browseCount() == 3 + factoryCount && editor.searchField().isVisible(), "LOCKER's ALL lists "
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
			check(editor.browseCount() == 3 + factoryCount, "SAMPLES, BY TYPE lists them all, FACTORY's included");
			drag(editor, editor.browseRowArea(0).getCentre(), editor.stepKeyArea(5).getCentre()
				- editor.browseRowArea(0).getCentre());
			check(processor.sampleName(5).isNotEmpty(), "a drag onto Track 6 loads " + processor.sampleName(5));
			click(editor, editor.browseTreeArea(2).getCentre());
			click(editor, editor.browseRowArea(0).getCentre());	// auditioned: its sound drawn
			check(processor.auditioned() != nullptr, "a click auditions the sample");

			// TYPE: a click opens the types, CLAP chosen is the user's
			click(editor, editor.browseButtonArea(B::Type).getCentre());
			editor.tick();
			click(editor, editor.selectorItemArea(static_cast<int>(SampleLibrary::Type::Clap)).getCentre());
			editor.tick();
			const auto clap = std::count_if(library.entries().begin(), library.entries().end(), [](const auto& _e)
			{
				return _e.type == SampleLibrary::Type::Clap && _e.typeFrom == SampleLibrary::TypeFrom::User;
			});
			check(clap == 1, "TYPE set to CLAP on " + juce::String(static_cast<int>(clap)) + " samples");
			// RENAME the sample: its field over the card's title, Return keeps the name
			click(editor, editor.browseButtonArea(B::Rename).getCentre());
			check(editor.renameField().isVisible(), "RENAME opens its field");
			editor.renameField().setText("Closed hat", false);
			editor.renameField().onReturnKey();
			editor.tick();
			const auto renamed = std::find_if(library.entries().begin(), library.entries().end(),
				[](const auto& _e) { return _e.name == "Closed hat"; });
			check(!editor.renameField().isVisible() && renamed != library.entries().end()
				&& renamed->file.getFileName() == "hh closed.wav" && renamed->file.existsAsFile(),
				"RENAME names the first row's sample, no file renamed");
			// LOAD ON TRACK, at the card's foot
			click(editor, editor.browseButtonArea(B::Load).getCentre());
			check(processor.sampleName(0) == "hh closed.wav", "LOAD ON TRACK loads " + processor.sampleName(0));
			snapshot(editor, _folder.getChildFile("browse-samples.png"));

			// The source, by its segment: its card; RENAME
			click(editor, editor.browseSegmentArea(segmentOf(samples.getFileName())).getCentre());
			editor.tick();
			check(editor.browseCount() == 3, "the source's segment lists its 3 samples");
			click(editor, editor.browseButtonArea(B::Rename).getCentre());
			editor.renameField().setText("Drums 2024", false);
			editor.renameField().onReturnKey();
			editor.tick();
			check(library.sources()[0].name == "Drums 2024", "RENAME names the source " + library.sources()[0].name);
			snapshot(editor, _folder.getChildFile("browse-locker-source.png"));

			// FACTORY, a source after the folders: the factory bank's samples; a double-click loads one, the Track's
			// sample a reference to its slot
			const auto& factory = processor.factorySamples();
			if(!factory.empty())
			{
				click(editor, editor.browseSegmentArea(2).getCentre());
				editor.tick();
				check(editor.browseCount() == static_cast<int>(factory.size()), "FACTORY lists "
					+ juce::String(editor.browseCount()) + " samples, not " + juce::String(static_cast<int>(factory.size())));
				snapshot(editor, _folder.getChildFile("browse-factory-source.png"));
				click(editor, editor.browseRowArea(0).getCentre());
				check(processor.auditioned() && processor.auditioned()->factory > 0, "a click auditions the factory sample");
				doubleClick(editor, editor.browseRowArea(0).getCentre());
				editor.tick();
				check(processor.sampleFingerprint(0) == factory[0].fingerprint && processor.sampleName(0) == factory[0].name
					&& juce::roundToInt(processor.parameter("t01_machine")->convertFrom0to1(
						processor.parameter("t01_machine")->getValue())) == processor.samplerChoice(),
					"a double-click loads " + factory[0].name + " on Track 1, on the Sampler");
				snapshot(editor, _folder.getChildFile("browse-factory.png"));
			}
			else
				std::printf("  (no flash image: FACTORY not shown)\n");

			// IN THIS SET: a sample no source holds, on Track 8; KEEP IN LOCKER makes it FROM SETS'
			juce::String error;
			check(processor.loadSample(7, temp.getChildFile("edDrumsEditorElsewhere.wav"), error), "a sample on Track 8");
			editor.tick();
			const int setSegment = editor.browseSegments() - 2;
			click(editor, editor.browseSegmentArea(setSegment).getCentre());
			editor.tick();
			check(editor.browseCount() == 1, "IN THIS SET lists " + juce::String(editor.browseCount()));
			click(editor, editor.browseRowArea(0).getCentre());
			snapshot(editor, _folder.getChildFile("browse-set.png"));
			click(editor, editor.browseButtonArea(B::Remove).getCentre());	// KEEP IN LOCKER
			settle();
			const auto* kept = library.find(processor.sampleFingerprint(7));
			check(kept && library.sources()[static_cast<size_t>(kept->folder)].name == "FROM SETS"
				&& library.sources()[static_cast<size_t>(kept->folder)].kind == SampleLibrary::Kind::Imported
				&& processor.setSamples().empty(), "KEEP IN LOCKER: the sample in FROM SETS, IN THIS SET empty");

			// IMPORT PACK: copied into the Locker; REMOVE SOURCE asks again, then erases the copy alone
			check(processor.addToLocker(pack, true), "IMPORT PACK started");
			settle();
			const int packSource = segmentOf(pack.getFileName()) - 1;
			check(packSource >= 0 && library.sources()[static_cast<size_t>(packSource)].kind
				== SampleLibrary::Kind::Imported, "the pack is an IMPORTED source");
			const auto copy = packSource >= 0 ? library.sources()[static_cast<size_t>(packSource)].folder : juce::File();
			click(editor, editor.browseSegmentArea(packSource + 1).getCentre());
			editor.tick();
			click(editor, editor.browseButtonArea(B::Remove).getCentre());
			editor.tick();
			check(library.sources().size() == 3 && copy.isDirectory(), "a first click on REMOVE SOURCE keeps the pack");
			snapshot(editor, _folder.getChildFile("browse-remove-again.png"));
			click(editor, editor.browseButtonArea(B::Remove).getCentre());
			editor.tick();
			check(library.sources().size() == 2 && !copy.exists() && pack.getChildFile("clap 909.wav").existsAsFile(),
				"the second click erases the pack's copy, the pack stays");
			// The linked folder: one click, its files stay
			click(editor, editor.browseSegmentArea(segmentOf("Drums 2024")).getCentre());
			editor.tick();
			click(editor, editor.browseButtonArea(B::Remove).getCentre());
			settle();
			check(library.sources().size() == 1 && samples.getChildFile("kick_01.wav").existsAsFile(),
				"REMOVE SOURCE on the linked folder: it leaves, its files on the disk");
			snapshot(editor, _folder.getChildFile("browse-locker.png"));
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

		// Ticket 24: MACHINES' gestures, Master's M, S and THR, FX's cells and SYNC, the Menu's TEMPO
		machinesMasterFx(editor, processor);

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
		{
			// The Sampler on VINTAGE, its grain's page; Drive 1 a DAC at 64
			auto* machine = processor.parameter("t01_machine");
			auto* grain = processor.parameter("t01_m9");
			auto* drive = processor.parameter("t01_drv1_type");
			auto* amount = processor.parameter("t01_drv1_amount");
			const float was = machine->getValue(), grainWas = grain->getValue(), driveWas = drive->getValue(),
				amountWas = amount->getValue();
			machine->setValueNotifyingHost(machine->convertTo0to1(static_cast<float>(processor.samplerChoice())));
			editor.tick();
			grain->setValueNotifyingHost(1.0f);
			drive->setValueNotifyingHost(drive->convertTo0to1(static_cast<float>(DriveType::Dac)));
			amount->setValueNotifyingHost(amount->convertTo0to1(64.0f));
			editor.showMachinePage(1);
			snapshot(editor, _folder.getChildFile("sound-vintage.png"));
			editor.showMachinePage(0);
			amount->setValueNotifyingHost(amountWas);
			drive->setValueNotifyingHost(driveWas);
			grain->setValueNotifyingHost(grainWas);
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
		// Seq's lanes after u.html's variant 2: empty lanes open + LOCK's menu; the menu lists every parameter that takes a
		// Lock, one with a lane greyed and inert, and a press beside it shuts it; past five lanes LANES turns the page; a
		// lane's X asks again where it holds Locks, any other press disarms it
		{
			using SB = Editor::SeqButton;
			const auto index = [&](const juce::String& _id) { return processor.parameter(_id)->getParameterIndex(); };
			const auto locksOf = [&](const juce::String& _id)
			{
				int n = 0;
				const auto& p = processor.sequencer().pattern(processor.currentPattern());
				for(int s = 0; s < SeqSteps; ++s)
					for(int i = 0; p.locks(0, s) && i < p.step(0, s).lockCount; ++i)
						n += p.locks(0, s)[i].parameter == index(_id) ? 1 : 0;
				return n;
			};
			processor.editSequencer([](Sequencer& _s)
			{
				auto& p = _s.pattern(_s.currentPattern());
				for(int s = 0; s < SeqSteps; ++s)
					while(p.step(0, s).lockCount)
						p.removeLock(0, s, p.locks(0, s)[0].parameter);
			});
			editor.tick();
			check(editor.lockLanes() == 0 && !editor.lockMenuOpen(), "Track 1 without a Lock: no lane");
			snapshot(editor, _folder.getChildFile("seq-empty-lanes.png"));
			click(editor, editor.laneArea(1).getCentre());
			check(editor.lockMenuOpen(), "a click on an empty lane opens + LOCK's menu");
			int listed = 0, expected = 0;
			const auto* machine = dynamic_cast<juce::AudioParameterChoice*>(processor.parameter("t01_machine"));
			const int used = machineParameters(processor.machines()[static_cast<size_t>(machine->getIndex())]);
			for(const auto& id : processor.trackIds(0))
			{
				if(id.endsWith("_machine") || id.endsWith("_output") || id.endsWith("_chokes"))
					continue;
				const auto suffix = id.substring(4);
				if(suffix.startsWithChar('m') && suffix.substring(1).containsOnly("0123456789")
					&& suffix.substring(1).getIntValue() > used)
					continue;
				++expected;
				listed += editor.lockMenuArea(index(id)).isEmpty() ? 0 : 1;
			}
			check(listed == expected && expected > 50, "the menu lists " + juce::String(listed) + " of the Track's "
				+ juce::String(expected) + " parameters that take a Lock");
			snapshot(editor, _folder.getChildFile("seq-lock-menu.png"));
			click(editor, editor.lockMenuArea(index("t01_flt_res")).getCentre());
			check(editor.lockLanes() == 1 && !editor.lockMenuOpen(), "an entry adds its lane and shuts the menu");
			click(editor, editor.seqButtonArea(SB::AddLock).getCentre());
			check(editor.lockMenuOpen() && editor.lockMenuArea(index("t01_flt_res")).isEmpty(), "+ LOCK opens it again, "
				"RESO's entry greyed and inert");
			const float s = static_cast<float>(editor.getWidth()) / ui::UnitWidth;
			click(editor, {juce::roundToInt(470 * s), juce::roundToInt(236 * s)});
			check(!editor.lockMenuOpen(), "a press beside the entries shuts it");
			for(const char* id : {"t01_flt_type", "t01_flt_freq", "t01_amp_decay", "t01_lfo1_depth", "t01_send_delay",
				"t01_drv1_amount"})
			{
				click(editor, editor.seqButtonArea(SB::AddLock).getCentre());
				click(editor, editor.lockMenuArea(index(id)).getCentre());
			}
			check(editor.lockLanes() == 7 && editor.lockScroll() == 5, "seven lanes, the page of the last added shown");
			click(editor, editor.seqButtonArea(SB::Lanes).getCentre());
			check(editor.lockScroll() == 0, "LANES turns back to lanes 1-5");

			// Locks to draw: RESO on trigs 5 and 9, 5 sliding to 9; the filter's TYPE on 9
			processor.editSequencer([&](Sequencer& _s)
			{
				auto& p = _s.pattern(_s.currentPattern());
				for(const int t : {0, 4, 8, 12})
					p.setTrig(0, t, true);
				p.step(0, 4).slide = true;
				auto* res = processor.parameter("t01_flt_res");
				p.setLock(0, 4, static_cast<uint16_t>(index("t01_flt_res")), res->convertFrom0to1(0.85f));
				p.setLock(0, 8, static_cast<uint16_t>(index("t01_flt_res")), res->convertFrom0to1(0.2f));
				p.setLock(0, 8, static_cast<uint16_t>(index("t01_flt_type")), 2);
			});
			editor.tick();
			snapshot(editor, _folder.getChildFile("seq-lanes.png"));
			click(editor, editor.seqButtonArea(SB::Lanes).getCentre());
			snapshot(editor, _folder.getChildFile("seq-lanes-p2.png"));
			click(editor, editor.seqButtonArea(SB::Lanes).getCentre());

			// RESO's lane, first: its X asks again; another press disarms; the X twice takes its two Locks off
			check(locksOf("t01_flt_res") == 2, "RESO holds 2 Locks");
			click(editor, editor.laneDeleteArea(0).getCentre());
			snapshot(editor, _folder.getChildFile("seq-delete-armed.png"));
			check(locksOf("t01_flt_res") == 2 && editor.lockLanes() == 7, "a first click on RESO's X keeps its Locks");
			click(editor, {juce::roundToInt(20 * s), juce::roundToInt(75 * s)});	// on the screen, nothing there
			click(editor, editor.laneDeleteArea(0).getCentre());
			check(locksOf("t01_flt_res") == 2, "a press elsewhere disarms it: the next click asks again");
			click(editor, editor.laneDeleteArea(0).getCentre());
			check(locksOf("t01_flt_res") == 0 && editor.lockLanes() == 6, "the second click in a row takes RESO's Locks "
				"and its lane off");
			// The rest: a lane without a Lock goes at one click
			for(int guard = 0; editor.lockLanes() > 0 && guard < 20; ++guard)
				click(editor, editor.laneDeleteArea(0).getCentre());
			check(editor.lockLanes() == 0, "every lane taken off");
		}
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
		stepGestures(editor, processor);
		editor.showPage(Editor::Page::Master);
		snapshot(editor, _folder.getChildFile("master.png"));
		editor.showPage(Editor::Page::Fx);
		snapshot(editor, _folder.getChildFile("fx.png"));
		editor.showPage(Editor::Page::Kits);
		snapshot(editor, _folder.getChildFile("kits.png"));
		editor.showPage(Editor::Page::Browse);
		snapshot(editor, _folder.getChildFile("browse.png"));
		// Ticket 27's names: nothing drawn on Browse's five lists, the LOCKER's segments and the Menu names a brand, and
		// the Menu holds WINDOW and AUDIO alone
		{
			juce::StringArray drawn;
			ui::Painter::drawnTexts = &drawn;
			const auto draw = [&]
			{
				editor.tick();
				editor.createComponentSnapshot(editor.getLocalBounds(), true, 1.0f);
			};
			for(int tree = 0; tree < 5; ++tree)
			{
				click(editor, editor.browseTreeArea(tree).getCentre());
				draw();
				for(int s = 1; tree == 2 && s < editor.browseSegments() - 1; ++s)
				{
					click(editor, editor.browseSegmentArea(s).getCentre());
					draw();
				}
			}
			const int browseTexts = drawn.size();
			editor.showMenu(true);
			draw();
			editor.showMenu(false);
			ui::Painter::drawnTexts = nullptr;
			juce::StringArray menu;
			for(int i = browseTexts; i < drawn.size(); ++i)
				menu.add(drawn[i]);
			juce::StringArray branded;
			for(const auto& t : drawn)
			{
				const auto words = juce::StringArray::fromTokens(t.toUpperCase(), " ,.:;()/-", "");
				if(t.containsIgnoreCase("ELEKTRON") || t.containsIgnoreCase("MACHINEDRUM") || words.contains("UW"))
					branded.addIfNotAlreadyThere(t);
			}
			check(branded.isEmpty() && drawn.contains("LOCKER") && drawn.contains("ADD FOLDER")
				&& drawn.contains("IMPORT PACK"), "Browse and the Menu: " + juce::String(drawn.size())
				+ " texts drawn, none names a brand" + (branded.isEmpty() ? juce::String() : ": " + branded.joinIntoString(" | ")));
			check(menu.contains("WINDOW") && menu.contains("AUDIO") && !menu.contains("ELEKTRON CONTENT")
				&& !menu.contains("IMPORT MY CONTENT"), "the Menu: WINDOW and AUDIO, no ELEKTRON CONTENT block");
		}
		// Ticket 27: the names the list, the host and the editor show
		machineNames(processor);
		hostNames(processor);
		editorNames(editor, processor);
		editor.showPage(Editor::Page::Seq);
		snapshot(editor, _folder.getChildFile("seq-lock-names.png"));
		{
			// The names to look at: a sample player's, a long-named Track's third page, LFO 1 on Machine 1; a machine's card
			auto* machine = processor.parameter("t01_machine");
			auto* target = processor.parameter("t01_lfo1_target");
			const float was = machine->getValue(), targetWas = target->getValue();
			const auto& list = processor.machines();
			const auto put = [&](const Machine& _m)
			{
				const auto at = std::find(list.begin(), list.end(), _m) - list.begin();
				machine->setValueNotifyingHost(machine->convertTo0to1(static_cast<float>(at)));
				editor.tick();
			};
			target->setValueNotifyingHost(target->convertTo0to1(static_cast<float>(LfoTarget::Machine1)));
			editor.showPage(Editor::Page::Sound);
			put({Family::Machinedrum, 48});
			snapshot(editor, _folder.getChildFile("sound-names-lofi-kick.png"));
			put({Family::OpenDrum, 3});
			editor.showMachinePage(2);
			snapshot(editor, _folder.getChildFile("sound-names-va-hihat-p3.png"));
			editor.showMachinePage(0);
			put({Family::Syntakt, 6});
			snapshot(editor, _folder.getChildFile("sound-names-toy-synth.png"));
			click(editor, editor.cellArea("t01_machine").getCentre());
			snapshot(editor, _folder.getChildFile("browse-machine.png"));
			machine->setValueNotifyingHost(was);
			target->setValueNotifyingHost(targetWas);
		}
		// A frame with nothing new repaints nothing, on every page; the CPU's readout is what processBlock measured
		for(const auto page : {Editor::Page::Sound, Editor::Page::Seq, Editor::Page::Master, Editor::Page::Fx,
			Editor::Page::Kits, Editor::Page::Browse})
		{
			editor.showPage(page);
			editor.tick();
			editor.tick();
			check(editor.repainted() == 0, "page " + juce::String(static_cast<int>(page)) + ": a frame with nothing new "
				"repaints nothing (" + juce::String(editor.repainted()) + " areas)");
		}
		{
			editor.showPage(Editor::Page::Sound);
			// As a host does: the rate first, which getSampleRate gives the measure
			processor.setRateAndBufferSizeDetails(44100, 256);
			processor.prepareToPlay(44100, 256);
			juce::AudioBuffer<float> buffer(processor.getTotalNumOutputChannels(), 256);
			juce::MidiBuffer midi;
			for(int i = 0; i < 400; ++i)
				processor.processBlock(buffer, midi);
			const float load = processor.cpuLoad();
			check(load > 0 && load < 1, "the CPU readout: " + juce::String(100 * load, 2) + " % of the block's time");
			editor.tick();
			editor.tick();
			check(editor.repainted() == 0, "a frame with the load unchanged in whole percent repaints nothing");
		}
		// What a full repaint of Sound and of Seq costs at 1600: printed, to compare before and after a layout's change
		for(const auto page : {Editor::Page::Sound, Editor::Page::Seq})
		{
			editor.showPage(page);
			editor.tick();
			const int frames = 40;
			const double start = juce::Time::getMillisecondCounterHiRes();
			for(int i = 0; i < frames; ++i)
				editor.createComponentSnapshot(editor.getLocalBounds(), true, 1.0f);
			std::printf("frame cost, %s: %.2f ms a full repaint\n", page == Editor::Page::Sound ? "Sound" : "Seq",
				(juce::Time::getMillisecondCounterHiRes() - start) / frames);
		}
		editor.showPage(Editor::Page::Sound);
		editor.setSize(Editor::SmallWidth, Editor::SmallHeight);
		snapshot(editor, _folder.getChildFile("sound-1280.png"));
		editor.showPage(Editor::Page::Seq);
		snapshot(editor, _folder.getChildFile("seq-1280.png"));
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
	reachable();
	masterReduction();
	std::printf("%s: %d failure(s)\n", g_failures == 0 ? "PASSED" : "FAILED", g_failures);
	return g_failures == 0 ? 0 : 1;
}
