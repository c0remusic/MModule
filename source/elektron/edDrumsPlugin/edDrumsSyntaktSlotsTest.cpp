// A Syntakt machine's Machine 1-8 in its SYN page's order (ticket 27 of .scratch/elektron-drums-v1, 2026-11-11):
//
//   - a slot plays the parameter its label names: CY ALLOY's RADIO, SD VINTAGE's COMPLEX and BD MODERN's OVERDRIVE move
//     a Hit as syntakt::Syntakt's internal ids 19, 20 and 26 move the original's, by how far the Hit moves from its value 0
//     (the waveform's difference and the third octaves', within 1 dB at 64 and 127);
//   - each machine's defaults play the sound they played before the page order;
//   - a state and a Kit from before the page order (state version 5, Kit version 3), whose Syntakt Tracks' values, a
//     Lock on old Machine 4 and an LFO on old Machine 2 meant the machine's index, play what the same sound saved in page
//     order plays, sample for sample.
//
// GEARMULATOR_MD_FIRMWARE_BIN names the flash image (exit 77 without it); without SYNTAKT_SECTION3 and SYNTAKT_SECTION7
// the comparison with the original is skipped. With --dump <folder>, it also writes what an old state, an old Kit, the Kit
// ED_DRUMS_FACTORY_KIT names and each machine's defaults play, to compare the renders of two builds by hand.

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "edDrumsProcessor.h"
#include "stFastMetrics.h"
#include "stMainMemory.h"
#include "stSection7.h"
#include "stSyntakt.h"

namespace
{
	using namespace elektron::drums;

	constexpr int Block = 128;
	constexpr int Channels = 2 * Outputs;
	using Take = std::vector<std::vector<float>>;

	void require(const bool _condition, const std::string& _message)
	{
		if(!_condition)
			throw std::runtime_error(_message);
	}

	// The SYN page, Machine 1-8 as the editor labels it (TUNE, SWEEP or SHIMMER, PUNCH, DECAY, the machine's two, MOD ENV,
	// OVERDRIVE), each the internal id it is on the Syntakt: syntakt::Syntakt's TrackParameter, Tune 18, its Synth 1-5
	// 19-23, Decay 25, Overdrive 26, in the order of section 3's descriptors, the same for every machine. 24, the voice's
	// HOLD, is on no page. Before the page order, Machine k played internal id 17 + k
	constexpr std::array<int, 8> PageIds{18, 21, 23, 25, 19, 20, 22, 26};
	constexpr int HoldId = 24;

	// Where internal id _id is in page order: 0-7 Machine 1-8, 8 Machine 9 for HOLD
	int slotOf(const int _id)
	{
		for(size_t s = 0; s < PageIds.size(); ++s)
			if(PageIds[s] == _id)
				return static_cast<int>(s);
		require(_id == HoldId, "an id on no slot");
		return 8;
	}

	// ---- against the original ----

	std::string dumpFolder;

	void dump(const std::string& _name, const std::vector<float>& _x)
	{
		if(dumpFolder.empty())
			return;
		std::ofstream f(dumpFolder + "/" + _name + ".f32", std::ios::binary);
		f.write(reinterpret_cast<const char*>(_x.data()), static_cast<std::streamsize>(_x.size() * sizeof(float)));
	}

	// A Hit of _machine on Track 1 at velocity 127, its slot _slot at _value and the others at the machine's defaults, no
	// chain, level 100, after 0.5 s of nothing: Main's left, 2 s
	std::vector<double> ours(const Library& _library, const Machine& _machine, const int _slot, const int _value)
	{
		constexpr int Pre = 22050, Samples = 88200;
		auto e = std::make_unique<Engine>(_library);
		e->setMachine(0, _machine);
		e->setLevel(0, 100);
		e->setChain(0, ChainSettings{});
		e->setParameter(0, _slot, static_cast<uint8_t>(_value));
		std::vector<std::vector<float>> out(Channels, std::vector<float>(Pre + Samples));
		std::array<float*, Channels> p{};
		for(size_t c = 0; c < p.size(); ++c)
			p[c] = out[c].data();
		e->render(p.data(), Pre);
		e->noteOn(0, 127);
		for(auto*& q : p)
			q += Pre;
		e->render(p.data(), Samples);
		return {out[0].begin() + Pre, out[0].end()};
	}

	// The original's: its track 1 on _machine, ids 18-23 and 25 at 64, PUNCH and OVERDRIVE at 0 as the defaults have them,
	// id _id at _value; 0.5 s, then note 60 at velocity 127; its stem, 2 s at 48 kHz
	std::vector<double> original(const syntakt::main::Section3& _s3, const std::vector<int32_t>& _s7, const int _machine,
		const int _id, const int _value)
	{
		constexpr int Pre = 24000, Samples = 96000;
		auto st = std::make_unique<syntakt::Syntakt>(_s3, _s7);
		require(st->setMachine(0, _machine), "the original's machine");
		for(const int id : {18, 19, 20, 21, 22, 25})
			st->setParameter(0, id, 64 << 8);
		st->setParameter(0, 23, 0);
		st->setParameter(0, 26, 0);
		st->setParameter(0, _id, static_cast<uint16_t>(_value << 8));
		std::vector<int32_t> buffer(static_cast<size_t>(syntakt::Syntakt::Outputs) * 32);
		std::vector<int32_t*> outs(syntakt::Syntakt::Outputs);
		for(int o = 0; o < syntakt::Syntakt::Outputs; ++o)
			outs[static_cast<size_t>(o)] = buffer.data() + 32 * o;
		for(int n = 0; n < Pre; n += 32)
			st->render(outs.data(), 32);
		st->noteOn(0, 60, 127);
		std::vector<double> y(Samples);
		for(int n = 0; n < Samples; n += 32)
		{
			st->render(outs.data(), 32);
			for(int k = 0; k < 32; ++k)
				y[static_cast<size_t>(n + k)] = outs[0][k] / 2147483648.0;
		}
		return y;
	}

	double rms(const std::vector<double>& _x)
	{
		double s = 0;
		for(const double v : _x)
			s += v * v;
		return std::sqrt(s / static_cast<double>(_x.size()));
	}

	// How far _x is from _x0: the difference's level against _x0's, and the RMS of the third octaves' change (50 Hz to
	// 16 kHz, where either is within 60 dB of the loudest), both in dB
	struct Change
	{
		double wave, bands;
	};

	Change changeOf(const std::vector<double>& _x, const std::vector<double>& _x0, const double _fs)
	{
		std::vector<double> d(_x.size());
		for(size_t n = 0; n < d.size(); ++n)
			d[n] = _x[n] - _x0[n];
		using syntakt::fast::bench::bands;
		using syntakt::fast::bench::db;
		const auto b = bands(_x, _fs, -13, 12), b0 = bands(_x0, _fs, -13, 12);
		double top = -1e300;
		for(size_t k = 0; k < b.size(); ++k)
			top = std::max({top, db(b[k]), db(b0[k])});
		double sum = 0;
		int kept = 0;
		for(size_t k = 0; k < b.size(); ++k)
			if(std::max(db(b[k]), db(b0[k])) > top - 60)
			{
				sum += (db(b[k]) - db(b0[k])) * (db(b[k]) - db(b0[k]));
				++kept;
			}
		return {20 * std::log10(std::max(rms(d), 1e-300) / rms(_x0)), std::sqrt(sum / kept)};
	}

	// What the label says: the slot moves our Hit as the id moves the original's
	bool labels(const Library& _library)
	{
		const char* s3 = std::getenv("SYNTAKT_SECTION3");
		const char* s7 = std::getenv("SYNTAKT_SECTION7");
		if(!s3 || !s7)
		{
			std::printf("  SKIP: SYNTAKT_SECTION3 and SYNTAKT_SECTION7 for the original\n");
			return false;
		}
		const auto section3 = syntakt::main::loadSection3(s3);
		const auto section7 = syntakt::loadSection7(s7);
		struct Case
		{
			const char* label;
			uint8_t machine;	// ours
			int original;		// the original's machine
			int id;
		};
		// CY ALLOY's RADIO, SD VINTAGE's COMPLEX, BD MODERN's OVERDRIVE
		constexpr Case Cases[] = {{"CY ALLOY RADIO", 2, 2, 19}, {"SD VINTAGE COMPLEX", 4, 6, 20}, {"BD MODERN OVERDRIVE", 0, 0, 26}};
		std::string failed;
		for(const auto& c : Cases)
		{
			const Machine machine{Family::Syntakt, c.machine};
			const int slot = slotOf(c.id);
			const auto u0 = ours(_library, machine, slot, 0), o0 = original(section3, section7, c.original, c.id, 0);
			std::printf("  %-20s Machine %d:", c.label, slot + 1);
			for(const int v : {64, 127})
			{
				const auto u = changeOf(ours(_library, machine, slot, v), u0, SampleRate);
				const auto o = changeOf(original(section3, section7, c.original, c.id, v), o0, syntakt::fast::MachineRate);
				std::printf(" at %d ours %.1f / %.1f dB, the original's %.1f / %.1f dB;", v, u.wave, u.bands, o.wave, o.bands);
				if(std::abs(u.wave - o.wave) > 1 || std::abs(u.bands - o.bands) > 1)
					failed += std::string(c.label) + " at " + std::to_string(v) + "; ";
			}
			std::printf("\n");
		}
		require(failed.empty(), "a slot does not play what its label names: " + failed);
		return true;
	}

	// Each machine's defaults play what the defaults of before played: each value where it plays now
	void defaults(const Library& _library)
	{
		// Before, by index: the knobs in the middle, PUNCH and HOLD off
		constexpr std::array<uint8_t, 8> Before{64, 64, 64, 64, 64, 0, 0, 64};
		std::string failed;
		for(int id = 0; id < SyntaktMachines; ++id)
		{
			const Machine machine{Family::Syntakt, static_cast<uint8_t>(id)};
			if(dataOf(_library, machine) == MachineData::None)
				continue;
			const auto play = [&](const bool _set)
			{
				auto e = std::make_unique<Engine>(_library);
				e->setMachine(0, machine);
				if(_set)
				{
					for(int i = 0; i < 8; ++i)
						e->setParameter(0, slotOf(18 + i), Before[static_cast<size_t>(i)]);
					e->setParameter(0, 7, 0);
				}
				std::vector<std::vector<float>> out(Channels, std::vector<float>(44100));
				std::array<float*, Channels> p{};
				for(size_t c = 0; c < p.size(); ++c)
					p[c] = out[c].data();
				e->noteOn(0, 100);
				e->render(p.data(), 44100);
				return out[0];
			};
			const auto fresh = play(false);
			dump("default_" + std::to_string(id), fresh);
			if(fresh != play(true))
				failed += std::string(syntaktMachineNames[static_cast<size_t>(id)]) + " ";
		}
		require(failed.empty(), "defaults that are not the sound of before: " + failed);
		std::printf("  every machine's defaults: the sound of before, sample for sample\n");
	}

	// An LFO on Machine 9-24: a Syntakt machine's HOLD in Machine 9, OPEN DRUM's Machine 12, nothing on a machine of 8
	void lfoMachines(const Library& _library)
	{
		const auto run = [&](const Machine& _machine, const LfoTarget _target, const int _depth)
		{
			auto e = std::make_unique<Engine>(_library);
			e->setMachine(0, _machine);
			e->setLevel(0, 100);
			e->setChain(0, ChainSettings{});
			LfoSettings s;
			s.rate.division = LfoDivisions - 1;		// 16 bars: held at the square's first half
			s.shape = LfoShape::Square;
			s.mode = LfoMode::Trig;
			s.depth = _depth;
			s.target = _target;
			e->setLfo(0, 0, s);
			std::vector<std::vector<float>> out(Channels, std::vector<float>(88200));
			std::array<float*, Channels> p{};
			for(size_t c = 0; c < p.size(); ++c)
				p[c] = out[c].data();
			e->noteOn(0, 127);
			e->render(p.data(), 88200);
			return out[0];
		};
		const auto energy = [](const std::vector<float>& _x, const size_t _from)
		{
			double e = 0;
			for(size_t n = _from; n < _x.size(); ++n)
				e += static_cast<double>(_x[n]) * _x[n];
			return e;
		};
		// BD MODERN's voice held by HOLD: its second second louder than its own decay leaves it
		const Machine bd{Family::Syntakt, 0};
		const double held = energy(run(bd, LfoTarget::Machine9, 127), 44100), free = energy(run(bd, LfoTarget::Machine9, 0), 44100);
		std::printf("  an LFO on Machine 9 of BD MODERN: its second second %.1f dB above the voice let go\n",
			10 * std::log10(std::max(held, 1e-30) / std::max(free, 1e-30)));
		require(held > 10 * free, "an LFO on Machine 9 does not hold a Syntakt voice");

		// OD SNARE's Machine 12, DEC SLOPE (OD DRUM's, FM/MIX, says nothing while its FM AMT is 0)
		const Machine snare{Family::OpenDrum, 1};
		require(std::string(machineParameterName(snare, 11)) == "DEC SLOPE", "OD SNARE's Machine 12");
		require(run(snare, LfoTarget::Machine12, 127) != run(snare, LfoTarget::Machine12, 0), "an LFO on Machine 12 of OD SNARE moves nothing");
		std::printf("  an LFO on Machine 12 of OD SNARE moves its DEC SLOPE\n");

		// A machine of 8: nothing
		require(run(bd, LfoTarget::Machine12, 127) == run(bd, LfoTarget::Machine12, 0), "an LFO on Machine 12 of BD MODERN moves it");
	}

	// ---- a state and a Kit of before ----

	class Transport final : public juce::AudioPlayHead
	{
	public:
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

	// _samples of the processor, its transport playing at 120 BPM from 0, a note on each Track of _notes at 9000 samples
	// apart
	Take play(Processor& _processor, int _samples, const std::vector<int>& _notes)
	{
		_samples = (_samples + Block - 1) / Block * Block;
		Transport transport;
		_processor.setPlayHead(&transport);
		_processor.prepareToPlay(SampleRate, Block);
		Take take(Channels, std::vector<float>(static_cast<size_t>(_samples)));
		juce::AudioBuffer<float> buffer(Channels, Block);
		juce::MidiBuffer midi;
		for(int at = 0; at < _samples; at += Block)
		{
			transport.quarters = at / (SampleRate / 2);
			buffer.clear();
			midi.clear();
			for(size_t k = 0; k < _notes.size(); ++k)
				if(const int when = 1000 + 9000 * static_cast<int>(k); when >= at && when < at + Block)
					midi.addEvent(juce::MidiMessage::noteOn(1, 36 + _notes[k], static_cast<juce::uint8>(110)), when - at);
			_processor.processBlock(buffer, midi);
			for(int c = 0; c < Channels; ++c)
				std::copy_n(buffer.getReadPointer(c), Block, take[static_cast<size_t>(c)].begin() + at);
		}
		_processor.setPlayHead(nullptr);
		return take;
	}

	void allOutputs(Processor& _processor)
	{
		auto layout = _processor.getBusesLayout();
		for(auto& bus : layout.outputBuses)
			bus = juce::AudioChannelSet::stereo();
		require(_processor.setBusesLayout(layout), "all the outputs");
	}

	void dump(const std::string& _name, const Take& _take)
	{
		std::vector<float> all;
		for(const auto& c : _take)
			all.insert(all.end(), c.begin(), c.end());
		dump(_name, all);
	}

	int choiceOf(const Processor& _processor, const Machine& _machine)
	{
		const auto& m = _processor.machines();
		return static_cast<int>(std::find(m.begin(), m.end(), _machine) - m.begin());
	}

	// Three Syntakt Tracks, each Machine value by internal id: CY ALLOY with an LFO on RADIO and a Lock on SHIMMER at step 4,
	// SD VINTAGE, and BD MODERN with its voice's HOLD on. _pageOrder: as this version writes them; else as before
	struct Sound
	{
		Machine machine;
		std::array<int, 9> byId;	// ids 18-26
	};
	const std::array<Sound, 3> Sounds{{
		{{Family::Syntakt, 2}, {70, 30, 100, 10, 50, 1, 0, 90, 0}},
		{{Family::Syntakt, 4}, {64, 80, 20, 40, 64, 0, 0, 64, 0}},
		{{Family::Syntakt, 0}, {60, 64, 90, 20, 64, 127, 127, 50, 0}},
	}};

	void write(juce::XmlElement& _xml, const Processor& _processor, const bool _pageOrder)
	{
		for(size_t t = 0; t < Sounds.size(); ++t)
		{
			const auto id = juce::String::formatted("t%02d_", static_cast<int>(t) + 1);
			_xml.setAttribute(id + "machine", choiceOf(_processor, Sounds[t].machine));
			for(int i = 0; i < 9; ++i)
			{
				const int internal = 18 + i;
				const int slot = _pageOrder ? slotOf(internal) : (internal == 26 ? -1 : i);
				if(slot >= 0)
					_xml.setAttribute(id + "m" + juce::String(slot + 1), Sounds[t].byId[static_cast<size_t>(i)]);
			}
			// Machine 9 kept nothing for a Syntakt machine before
			if(!_pageOrder)
				_xml.setAttribute(id + "m9", 33);
		}
		// The other Tracks off: their defaults are no sound of before
		for(int t = static_cast<int>(Sounds.size()); t < Tracks; ++t)
			_xml.setAttribute(juce::String::formatted("t%02d_machine", t + 1), 0);
		// Track 1's LFO 1 on RADIO, id 19
		_xml.setAttribute("t01_lfo1_target", 1 + (_pageOrder ? slotOf(19) : 1));
		_xml.setAttribute("t01_lfo1_depth", 60);
		_xml.setAttribute("t01_lfo1_speed", 3);
		// Track 3's LFO 1 on HOLD, id 24, old Machine 7 "MOD ENV": Machine 9 now, a square that lets go of it each half
		_xml.setAttribute("t03_lfo1_target", _pageOrder ? static_cast<int>(LfoTarget::Machine9) : 7);
		_xml.setAttribute("t03_lfo1_depth", -127);
		_xml.setAttribute("t03_lfo1_shape", static_cast<int>(LfoShape::Square));
		_xml.setAttribute("t03_lfo1_speed", 4);
	}

	std::unique_ptr<juce::XmlElement> stateOf(Processor& _processor)
	{
		juce::MemoryBlock data;
		_processor.getStateInformation(data);
		auto xml = juce::AudioProcessor::getXmlFromBinary(data.getData(), static_cast<int>(data.getSize()));
		require(xml != nullptr, "the state's XML");
		return xml;
	}

	void stateAndKit()
	{
		constexpr int Length = 4 * 88200;
		const std::vector<int> notes{0, 1, 2, 0};
		std::string failed;
		Processor reference;
		allOutputs(reference);

		// The state: Track 1's steps 0 and 4, a Lock on SHIMMER (id 21) at step 4
		const auto stateWith = [&](const bool _pageOrder)
		{
			auto xml = stateOf(reference);
			xml->setAttribute("version", _pageOrder ? xml->getIntAttribute("version") : 5);
			write(*xml, reference, _pageOrder);
			const int lock = reference.parameter("t01_m" + juce::String((_pageOrder ? slotOf(21) : 21 - 18) + 1))->getParameterIndex();
			auto* pattern = xml->createNewChildElement("pattern");
			pattern->setAttribute("index", 0);
			pattern->setAttribute("length", 16);
			for(const int step : {0, 4})
			{
				auto* s = pattern->createNewChildElement("step");
				s->setAttribute("track", 0);
				s->setAttribute("index", step);
				s->setAttribute("trig", 1);
				s->setAttribute("velocity", 100);
				if(step == 4)
					s->setAttribute("locks", juce::String(lock) + ":10");
			}
			// Track 3's HOLD (id 24), old Machine 7 "MOD ENV", locked off at step 6: Machine 9 now. An LFO cannot aim at
			// Machine 9 (LfoTarget stops at Machine 8): one on old Machine 7 aims at nothing
			const int hold = reference.parameter(_pageOrder ? "t03_m9" : "t03_m7")->getParameterIndex();
			for(const int step : {2, 6})
			{
				auto* s = pattern->createNewChildElement("step");
				s->setAttribute("track", 2);
				s->setAttribute("index", step);
				s->setAttribute("trig", 1);
				s->setAttribute("velocity", 100);
				if(step == 6)
					s->setAttribute("locks", juce::String(hold) + ":0");
			}
			juce::MemoryBlock data;
			juce::AudioProcessor::copyXmlToBinary(*xml, data);
			auto p = std::make_unique<Processor>();
			allOutputs(*p);
			p->setStateInformation(data.getData(), static_cast<int>(data.getSize()));
			return play(*p, Length, notes);
		};
		const auto before = stateWith(false), now = stateWith(true);
		dump("state_v5", before);
		if(before != now)
			failed += "a state of before does not play what it played; ";
		else
			std::printf("  a state of version 5, three Syntakt Tracks, a Lock on old Machine 4, an LFO on old Machine 2: the same"
			" samples as in page order, and a Lock on old Machine 7 (HOLD) as one on Machine 9\n");

		// The Kit: saved by this version, then rewritten as before
		require(reference.saveKit(0), "a Kit saved");
		const auto kitFile = Processor::dataFolder().getChildFile("Kits").getChildFile("01.edkit");
		const auto kitWith = [&](const bool _pageOrder)
		{
			auto kit = KitBank::readFile(kitFile, KitBank::Tag);
			require(kit != nullptr, "the Kit's file");
			if(!_pageOrder)
				kit->setAttribute("version", 3);
			write(*kit, reference, _pageOrder);
			const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("edDrumsSlotsKit.edkit");
			require(KitBank::writeFile(file, *kit), "a Kit file");
			Processor p;
			allOutputs(p);
			require(p.importKit(5, file) && p.loadKit(5), "a Kit imported and loaded");
			file.deleteFile();
			return play(p, Length, notes);
		};
		const auto kitBefore = kitWith(false), kitNow = kitWith(true);
		dump("kit_v3", kitBefore);
		if(kitBefore != kitNow)
			failed += "a Kit of before does not play what it played; ";
		else
			std::printf("  a Kit of version 3: the same samples as in page order\n");

		// The Kit ED_DRUMS_FACTORY_KIT names, by hand: its Tracks one after the other
		if(const char* factory = std::getenv("ED_DRUMS_FACTORY_KIT"))
		{
			Processor p;
			allOutputs(p);
			require(p.importKit(6, juce::File(factory)) && p.loadKit(6), "the factory Kit");
			std::vector<int> all;
			for(int t = 0; t < Tracks; ++t)
				all.push_back(t);
			dump("factory", play(p, 9000 * Tracks + 44100, all));
			std::printf("  the factory Kit %s rendered\n", factory);
		}
		require(failed.empty(), failed);
	}
}

int main(const int _argc, char** _argv)
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	const juce::ScopedJuceInitialiser_GUI juce;
	if(_argc > 2 && std::string(_argv[1]) == "--dump")
		dumpFolder = _argv[2];
	if(Library::findFlashImage({}).empty())
	{
		std::printf("SKIP: set GEARMULATOR_MD_FIRMWARE_BIN to the Machinedrum UW flash image\n");
		return 77;
	}
	const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("edDrumsSyntaktSlotsTestData");
	root.deleteRecursively();
	_putenv_s("GEARMULATOR_DATA_ROOT", root.getFullPathName().toRawUTF8());
	// Each part on its own, so that a dump holds all of them
	const Library library(Library::findFlashImage({}));
	std::string failed;
	bool compared = false;
	const auto part = [&](const std::function<void()>& _part)
	{
		try
		{
			_part();
		}
		catch(const std::exception& e)
		{
			std::printf("  FAILED: %s\n", e.what());
			failed += std::string(e.what()) + "; ";
		}
	};
	part([&] { defaults(library); });
	part([&] { lfoMachines(library); });
	part([&] { stateAndKit(); });
	part([&] { compared = labels(library); });
	if(!failed.empty())
	{
		std::printf("FAIL: %s\n", failed.c_str());
		return 1;
	}
	std::printf(compared ? "PASS: each Syntakt slot plays what its label names; old states and Kits play as before\n"
		: "PASS: old states and Kits play as before (the original not compared)\n");
	return 0;
}
