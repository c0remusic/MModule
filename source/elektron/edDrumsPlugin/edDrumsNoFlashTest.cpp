// The instrument with no flash image anywhere: nothing of a firmware is read, every machine that needs none of its data
// sounds as it does with it, and the others are reported and play silence, Hits at HitDelay, jitter 0, at any position.
//
// Needs nothing: the variables that lead to an image are cleared here, whatever the gate set, and GEARMULATOR_DATA_ROOT
// is a folder of its own.

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "edDrumsProcessor.h"
#include "edDrumsTestSupport.h"
#include "mdVoiceDsp.h"
#include "mdVoiceInternal.h"

namespace
{
	using namespace elektron::drums;
	namespace hooks = md::voice::testHooks;

	constexpr int Block = 128, Samples = 22050;
	constexpr int Note = 36;					// Track 1
	constexpr double SoundsAbove = 0.02;		// -34 dB
	constexpr int HitDelay = 81;				// stated here: no change of its may pass unseen
	constexpr int Earliest = HitDelay - 33;		// TRX-S2's first block, a ramp, reaches 1 % of its peak at its defaults

	void require(const bool _condition, const std::string& _message)
	{
		if(!_condition)
			throw std::runtime_error(_message);
	}

	std::unique_ptr<Processor> processor(const size_t _choice)
	{
		auto p = std::make_unique<Processor>();
		auto layout = p->getBusesLayout();
		for(auto& bus : layout.outputBuses)
			bus = juce::AudioChannelSet::stereo();
		require(p->setBusesLayout(layout), "all the outputs");
		dynamic_cast<juce::AudioParameterChoice&>(*p->getParameters()[0]) = static_cast<int>(_choice);
		p->prepareToPlay(SampleRate, Block);
		return p;
	}

	float peakOf(const std::vector<float>& _x)
	{
		float peak = 0;
		for(const float x : _x)
			peak = std::max(peak, std::abs(x));
		return peak;
	}

	// The waves made here sound, in the range a word holds, and start and end at zero: no step into a sine or out of a fade
	void houseWavesJoin()
	{
		const Library library;
		const auto& t = library.trx;
		const auto check = [](const char* const _name, const auto& _words, const double _least)
		{
			const auto value = [](const uint32_t _word) { return md::voice::dsp::sx(_word) / 8388608.0; };
			double peak = 0;
			double before = 0;
			for(const uint32_t word : _words)
			{
				peak = std::max(peak, std::abs(value(word)));
				// A wave past full scale wraps: nothing it means to play jumps this far
				require(std::abs(value(word) - before) < 1.5, std::string(_name) + ": a word wrapped");
				before = value(word);
			}
			require(peak >= _least, std::string(_name) + ": a wave too quiet");
			require(peak < 0.999, std::string(_name) + ": a wave at the end of its range");
			require(value(_words.front()) == 0, std::string(_name) + ": a step in");
			require(std::abs(value(_words.back())) < 0.001, std::string(_name) + ": a step out");
			// What follows a wave starts from there: a last slope this steep clicks
			require(std::abs(value(_words[_words.size() - 2])) < 0.2, std::string(_name) + ": a click at its end");
		};
		check("TRX-BD A", t.bdWaveA, 0.9);
		check("TRX-BD B", t.bdWaveB, 0.05);
		check("TRX-XT", t.xtWave, 0.5);
		check("TRX-XC", t.xcWave, 0.5);
		check("TRX-SD", t.sdWave, 0.3);
		check("TRX-RS", t.rsWave, 0.3);
	}

	// What reports None plays silence however it is played: Hits and each machine's own retriggers, knobs at the ends
	// of their range and at random, with another Track's voice leaving its words in the scratch all share. An E12
	// machine's directory is zeros, so that a retrigger's position stays in its window of zeros; a ROM one's slot is
	// empty, so that its render ends before any window, forward or reversed
	void noneIsSilent()
	{
		const Library library;
		std::mt19937 random(1);
		int played = 0;
		std::array<int64_t, hooks::ProbeCount> probes{};
		hooks::hooks().probes = &probes;
		std::string sounding;
		for(const Machine machine : test::machines())
		{
			if(dataOf(library, machine) != MachineData::None)
				continue;
			const auto play = [&](const std::array<uint8_t, 8>& _knobs)
			{
				auto e = std::make_unique<Engine>(library);
				e->setMachine(0, machine);
				e->setLevel(0, 127);
				e->setChain(0, ChainSettings{});
				e->setMachine(1, {Family::Machinedrum, 33});
				e->setOutput(1, 15);
				for(int i = 0; i < 8; ++i)
					e->setParameter(0, i, _knobs[static_cast<size_t>(i)]);
				test::Take take(6000);
				for(const int at : {0, 32, 2000})
				{
					take.render(*e, at == 0 ? 0 : at == 32 ? 0 : 32, at == 0 ? 0 : at == 32 ? 32 : 1968);
					e->noteOn(1, 127);
					e->noteOn(0, 127);
				}
				take.render(*e, 2000, 4000);
				++played;
				return peakOf(take.channel(0));
			};
			float peak = 0;
			for(const uint8_t value : {0, 127})
				for(size_t k = 0; k < 8; ++k)
				{
					auto knobs = defaults(machine);
					knobs[k] = value;
					peak = std::max(peak, play(knobs));
				}
			for(int n = 0; n < 40; ++n)
			{
				std::array<uint8_t, 8> knobs{};
				for(auto& k : knobs)
					k = static_cast<uint8_t>(random() % 128);
				peak = std::max(peak, play(knobs));
			}
			if(peak != 0)
				sounding += " " + test::nameOf(machine) + " " + std::to_string(20 * std::log10(peak)) + ",";
		}
		hooks::hooks().probes = nullptr;
		std::printf("  %d renders of the machines reported None, E12's retriggers %lld:%s\n", played,
			static_cast<long long>(probes[hooks::E12Retrigger]), sounding.empty() ? " silence" : sounding.c_str());
		require(sounding.empty(), "a machine reported None sounds");
		require(probes[hooks::E12Retrigger] > 0, "no retrigger played");
	}

	// Track 1's Main, left, with a note _at samples in
	std::vector<float> play(Processor& _p, const int _at)
	{
		std::vector<float> out;
		juce::AudioBuffer<float> buffer(2 * Outputs, Block);
		for(int from = 0; from < Samples; from += Block)
		{
			buffer.clear();
			juce::MidiBuffer midi;
			if(_at >= from && _at < from + Block)
				midi.addEvent(juce::MidiMessage::noteOn(1, Note, static_cast<juce::uint8>(127)), _at - from);
			_p.processBlock(buffer, midi);
			out.insert(out.end(), buffer.getReadPointer(0), buffer.getReadPointer(0) + Block);
		}
		return out;
	}
}

int main()
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	// As the CRT's copy of the environment, which getenv reads
	for(const char* name : {"GEARMULATOR_MD_FIRMWARE_BIN", "LOCALAPPDATA", "USERPROFILE", "HOME"})
		_putenv_s(name, "");
	const auto root = juce::File::createTempFile("edDrumsNoFlash");
	root.createDirectory();
	_putenv_s("GEARMULATOR_DATA_ROOT", root.getFullPathName().toRawUTF8());

	const juce::ScopedJuceInitialiser_GUI juce;
	int result = 0;
	try
	{
		const auto roms = Processor::dataFolder().getChildFile("roms").getFullPathName().toStdString();
		require(Library::findFlashImage(roms).empty(), "an image found");
		houseWavesJoin();
		noneIsSilent();
		Processor first;
		const auto list = first.machines();
		// Every Track starts on a machine that sounds: a Track's parameters are its machine and 13 others
		for(int t = 0; t < Tracks; ++t)
		{
			const auto& machine = dynamic_cast<juce::AudioParameterChoice&>(*first.getParameters()[14 * t]);
			require(first.machineData(static_cast<size_t>(machine.getIndex())) != MachineData::None,
				"Track " + std::to_string(t + 1) + " starts silent");
		}
		int whole = 0, house = 0, silent = 0;
		double quietest = 1;
		int earliest = Samples;
		std::string quietestName, silentNames;
		for(const Machine machine : test::machines())
		{
			// ROM-33 to -48 are the Sampler's entries, not machines of the list; ROM-01 to -32 left it (ticket 27), their
			// samples the Sampler's factory ones
			if(!playable(machine) || (machine.family == Family::Machinedrum && machine.id >= 128 && machine.id < 160))
				continue;
			const auto name = test::nameOf(machine);
			const auto choice = static_cast<size_t>(std::find(list.begin(), list.end(), machine) - list.begin());
			require(choice < list.size(), name + " is not in the list");
			auto p = processor(choice);
			require(p->getLatencySamples() == HitDelay, "the latency reported is not HitDelay");
			const auto data = p->machineData(choice);
			const auto first = play(*p, 0);
			const float peak = peakOf(first);
			if(data == MachineData::None)
			{
				require(peak == 0, name + ": reported silent, it sounds");
				++silent;
				silentNames += " " + name + ",";
				continue;
			}
			require(peak > SoundsAbove, name + ": a machine that sounds does not, peak " + std::to_string(peak));
			if(peak < quietest)
			{
				quietest = peak;
				quietestName = name;
			}
			(data == MachineData::Whole ? whole : house)++;

			// 1 % of its peak, as edDrumsEngineTest reads a Hit
			const auto reached = [&](const float _x) { return std::abs(_x) >= 0.01f * peak; };
			const auto onset = static_cast<int>(std::find_if(first.begin(), first.end(), reached) - first.begin());
			earliest = std::min(earliest, onset);
			require(onset >= Earliest, name + ": before HitDelay, at " + std::to_string(onset));
			for(const int at : {7, 61, 200})
			{
				auto y = first;
				y.insert(y.begin(), static_cast<size_t>(at), 0.0f);
				y.resize(first.size());
				auto q = processor(choice);
				if(play(*q, at) != y)
					throw std::runtime_error(name + ": a Hit moved with its note, at " + std::to_string(at));
			}
		}
		std::printf("  no image: %d machines whole, %d on words made here, quietest %s at %.1f dB, HitDelay %d "
			"reported, the earliest 1 %% of a peak %d, jitter 0 at 4 positions; %d silent:%s\n", whole, house, quietestName.c_str(),
			20 * std::log10(quietest), HitDelay, earliest, silent, silentNames.c_str());
		require(whole == 32 && house == 10 && silent == 17, "another count of machines");

		// Ticket 27: a Track on factory slot 5 without the image keeps its reference, says why it is silent, and a state
		// saved again keeps the slot; Browse has no factory sample to list
		{
			juce::XmlElement xml("ElektronDrums");
			xml.setAttribute("version", 5);
			xml.setAttribute("t03_machine", first.samplerChoice());
			auto* element = xml.createNewChildElement("sample");
			element->setAttribute("track", 2);
			element->setAttribute("factory", 5);
			juce::MemoryBlock state;
			juce::AudioProcessor::copyXmlToBinary(xml, state);
			Processor p;
			p.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
			require(p.sampleFingerprint(2) == "factory:5" && p.sampleName(2).contains("no flash image")
				&& p.factorySamples().empty(), "factory slot 5 without the image: " + p.sampleName(2).toStdString());
			juce::MemoryBlock saved;
			p.getStateInformation(saved);
			Processor q;
			q.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
			require(q.sampleFingerprint(2) == "factory:5", "the reference not kept without the image");
			std::printf("  factory slot 5 without the image: \"%s\", kept in the state\n", p.sampleName(2).toRawUTF8());
		}
		std::printf("PASS: the instrument plays with no image\n");
	}
	catch(const std::exception& _e)
	{
		std::printf("FAIL: %s\n", _e.what());
		result = 1;
	}
	root.deleteRecursively();
	return result;
}
