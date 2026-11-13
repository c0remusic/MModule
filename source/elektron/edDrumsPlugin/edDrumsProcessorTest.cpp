// The processor in process (tickets 13 and 18 of .scratch/elektron-drums-v1):
//
//   - what processBlock tells the engine of the host's transport: playing, the position and the tempo; a host's jump;
//     stopped, the LFOs run on; no playhead; an LFO set by its host parameters follows the host;
//   - the parameters' IDs, frozen in edDrumsParameterIds.txt: the same list, in the same order;
//   - at its defaults the processor plays what the engine plays with no setting given;
//   - every parameter of ticket 18, set as a host sets it, on every Track at once: the processor plays what an engine
//     given the same values plays, sample for sample (the engine's calls written here, by ID, apart from the
//     processor's), and the change is heard, on one of two sounds that each make some settings heard;
//   - a state saved and restored by another instance plays the same; a state without ticket 18's parameters, and with
//     one that is not ours, loads with the defaults, a Track's chain its machine's family's; nothing told to the host
//     as an edit as a state loads;
//   - Machine's choices: the Sampler where ROM-33 stood, OPEN DRUM's after it, no INP, CTR, MID or RAM machine; an old
//     state on ROM-33 to -48 playing the Sampler, an old state's and Kit's choices read in their own list; a WAV
//     loaded on a Track plays, the state keeps it, cleared it is silence (ticket 20); the samples' Library scans a folder
//     for its sound files alone, another Library sees the folder, a sample loaded by fingerprint plays as its file, the
//     state and a Kit keep the fingerprint, the audition plays once on Main;
//   - a Kit saved, then loaded by another instance, plays the same, its sample included, no edit told; two instances
//     on one Bank each write a Slot and keep the other's; a Kit without an ID loads that parameter's default (ticket 19);
//   - a SysEx file's Kit dump, made by mdProtocol's encoder, imported into an empty Slot, its values on the Tracks once
//     loaded, and what Lockstep lacks of the file said a line each (ticket 20);
//   - the sequencer from a playhead at 120 BPM: Hits at their steps' samples, Locks for their step only, Pattern
//     switching at the end of the pattern playing, patterns and samples in the state play the same (ticket 17);
//   - a slide: Filter Freq and Machine 1 glide from the step's Locks to the next trig's, the engine's, in any host
//     blocks, nothing allocated; no glide without SLIDE; a host note ends it;
//   - the relays for Push follow the Track shown, set its parameter, choose it, and stay out of the state; the state
//     compressed, its sizes on the default and a full set, restores the same set and samples, as a version 6 state
//     uncompressed does.
//
// GEARMULATOR_MD_FIRMWARE_BIN names the flash image; exit 77 without it. With --print-ids, it prints the IDs, one a line.

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "../md/mdDrums/mdDrumsAllocationCounter.h"
#include "edDrumsMaster.h"
#include "edDrumsNames.h"
#include "edDrumsAnalogPort.h"
#include "edDrumsAnalogVoice.h"
#include "edDrumsProcessor.h"
#include "edDrumsVintage.h"
#include "MidiNoteNumbers.h"
#include "mdProtocol/mdkit.h"
#include "mdProtocol/mdmachines.h"
#include "sophieMachine.h"

namespace
{
	using namespace elektron::drums;

	constexpr int Block = 128;
	constexpr int Channels = 2 * Outputs;

	void require(const bool _condition, const std::string& _message)
	{
		if(!_condition)
			throw std::runtime_error(_message);
	}

	class Transport final : public juce::AudioPlayHead
	{
	public:
		juce::Optional<PositionInfo> getPosition() const override
		{
			PositionInfo info;
			info.setBpm(bpm);
			info.setIsPlaying(playing);
			if(hasQuarters)
				info.setPpqPosition(quarters);
			return info;
		}

		double bpm = 120, quarters = 0;
		bool playing = true, hasQuarters = true;
	};

	void block(Processor& _processor)
	{
		juce::AudioBuffer<float> buffer(Channels, Block);
		juce::MidiBuffer midi;
		_processor.processBlock(buffer, midi);
	}

	void allOutputs(Processor& _processor)
	{
		auto layout = _processor.getBusesLayout();
		for(auto& bus : layout.outputBuses)
			bus = juce::AudioChannelSet::stereo();
		require(_processor.setBusesLayout(layout), "all the outputs");
	}

	// A parameter's value as its host shows it, and set as a host sets it
	double plain(const juce::RangedAudioParameter& _p)
	{
		return _p.convertFrom0to1(_p.getValue());
	}

	void set(juce::RangedAudioParameter& _p, const double _value)
	{
		_p.setValueNotifyingHost(_p.convertTo0to1(static_cast<float>(_value)));
	}

	juce::RangedAudioParameter& find(Processor& _processor, const juce::String& _id)
	{
		auto* p = _processor.parameter(_id);
		require(p != nullptr, "no parameter " + _id.toStdString());
		return *p;
	}

	// ---- the host's transport ----

	void transport()
	{
		Processor processor;
		Transport transport;
		processor.setPlayHead(&transport);
		allOutputs(processor);
		processor.prepareToPlay(SampleRate, Block);
		auto& engine = *processor.engine();

		// Playing: where the host says, at each block's start, and on at its tempo
		const double perBlock = 120.0 / 60 / SampleRate * Block;
		transport.quarters = 7.5;
		block(processor);
		require(std::abs(engine.position() - (7.5 + perBlock)) < 1e-9, "the transport's position");
		std::printf("  playing: position %.6f after a block from 7.5\n", engine.position());

		// A jump
		transport.quarters = 2;
		block(processor);
		require(std::abs(engine.position() - (2 + perBlock)) < 1e-9, "a host's jump");

		// Its tempo
		transport.bpm = 60;
		transport.quarters = engine.position();
		const double from = engine.position();
		block(processor);
		require(std::abs(engine.position() - from - perBlock / 2) < 1e-9, "the host's tempo");

		// Stopped: the position it gives stands still, the LFOs do not
		transport.playing = false;
		transport.quarters = 100;
		const double stopped = engine.position();
		block(processor);
		require(std::abs(engine.position() - stopped - perBlock / 2) < 1e-9, "a stopped transport runs on");

		// Started again: the host's
		transport.playing = true;
		block(processor);
		require(std::abs(engine.position() - (100 + perBlock / 2)) < 1e-9, "play takes the host's position");

		// Playing with no position to give: it runs on
		transport.hasQuarters = false;
		const double told = engine.position();
		block(processor);
		require(std::abs(engine.position() - told - perBlock / 2) < 1e-9, "a host with no position");
		transport.hasQuarters = true;

		// What a host must not send stays out
		for(const double wrong : {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()})
		{
			const double before = engine.position();
			transport.bpm = 60;
			transport.quarters = wrong;
			block(processor);
			require(std::abs(engine.position() - before - perBlock / 2) < 1e-9, "a position that is no number");
			transport.bpm = wrong;
			transport.playing = false;
			const double held = engine.position();
			block(processor);
			require(std::abs(engine.position() - held - perBlock) < 1e-9, "a tempo that is no number: the parameter's, 120");
			transport.bpm = 60;
			transport.quarters = engine.position();
			transport.playing = true;
			block(processor);
		}

		// An LFO's phase is the host's: a cycle of half a quarter note, wherever the host goes, set by its parameters
		set(find(processor, "t01_lfo1_speed"), 3);
		set(find(processor, "t01_lfo1_depth"), 10);
		set(find(processor, "t01_lfo1_target"), static_cast<int>(LfoTarget::Level));
		transport.bpm = 120;
		for(const double quarters : {1.3, 7.21, 2.05})
		{
			transport.quarters = quarters;
			block(processor);
			const double phase = engine.lfoPhase(0, 0);
			const double expected = (quarters + perBlock) / 0.5 - std::floor((quarters + perBlock) / 0.5);
			std::printf("  the host at %.2f: phase %.4f, %.4f at the block's end\n", quarters, phase, expected);
			require(std::abs(phase - expected) <= 2 * 32 * perBlock / Block / 0.5, "an LFO's phase is not the host's");
		}

		// No playhead: the Tempo parameter, from where it stands
		processor.setPlayHead(nullptr);
		const double alone = engine.position();
		block(processor);
		require(std::abs(engine.position() - alone - perBlock) < 1e-9, "no playhead");
		std::printf("PASS: the host's transport reaches the engine\n");
	}

	// ---- the IDs ----

	juce::StringArray idsOf(Processor& _processor)
	{
		juce::StringArray ids;
		for(auto* parameter : _processor.getParameters())
			if(auto* p = dynamic_cast<juce::RangedAudioParameter*>(parameter))
				ids.add(p->getParameterID());
		return ids;
	}

	void frozenIds(Processor& _processor)
	{
		std::ifstream file(ED_DRUMS_PARAMETER_IDS);
		require(file.good(), std::string("no ") + ED_DRUMS_PARAMETER_IDS);
		juce::StringArray frozen;
		for(std::string line; std::getline(file, line);)
			if(!line.empty())
				frozen.add(juce::String(line).trim());
		const auto ids = idsOf(_processor);
		for(int i = 0; i < std::min(ids.size(), frozen.size()); ++i)
			require(ids[i] == frozen[i], "parameter " + std::to_string(i) + " is " + ids[i].toStdString() + ", frozen as "
				+ frozen[i].toStdString() + ": an ID is appended, never renamed or renumbered");
		require(ids.size() == frozen.size(), "the processor has " + std::to_string(ids.size()) + " parameters, the list "
			+ std::to_string(frozen.size()) + ": append a new one to edDrumsParameterIds.txt");
		auto unique = ids;
		unique.removeDuplicates(false);
		require(unique.size() == ids.size(), "an ID twice");

		// The editor's finding: every ID, each Track's in its sections and nowhere else
		int inSections = 0;
		for(const auto& id : ids)
			require(_processor.parameter(id) && _processor.parameter(id)->getParameterID() == id, "parameter(" + id.toStdString() + ")");
		require(_processor.parameter("nothing") == nullptr, "an ID that is none");
		for(int t = 0; t < Tracks; ++t)
		{
			int sum = 0;
			for(int s = 0; s < Processor::TrackSections; ++s)
				for(const auto& id : _processor.ids(static_cast<Processor::Section>(s), t))
				{
					require(id.startsWith(juce::String::formatted("t%02d_", t + 1)), id.toStdString() + " in another Track");
					++sum;
				}
			require(sum == _processor.trackIds(t).size(), "a Track's sections hold its parameters");
			inSections += sum;
		}
		for(const auto s : {Processor::Section::Effects, Processor::Section::Master, Processor::Section::Global,
			Processor::Section::Relays})
			inSections += _processor.ids(s).size();
		require(inSections == ids.size(), "every parameter in one section");

		// The relays for Push: 64, last but the parameters appended after them, each named in Push's ten characters, as
		// its target on Track 1 in range and kind
		const juce::StringArray afterRelays{"chorus_type"};
		const auto& relays = _processor.ids(Processor::Section::Relays);
		require(relays.size() == parameters::RelayCount
			&& ids.indexOf(relays[0]) == ids.size() - parameters::RelayCount - afterRelays.size(),
			"the 64 relays last but the parameters appended after them");
		for(int i = 0; i < afterRelays.size(); ++i)
			require(ids[ids.size() - afterRelays.size() + i] == afterRelays[i], "not appended after the relays");
		for(int r = 0; r < relays.size(); ++r)
		{
			const auto& spec = parameters::relaySpecs[static_cast<size_t>(r)];
			auto& relay = find(_processor, relays[r]);
			require(relay.getName(64).length() <= 10, relays[r].toStdString() + ": a name over ten characters");
			if(!spec.target)
			{
				require(relays[r] == "push_track" && relay.getNumSteps() == Tracks, "the Track relay");
				continue;
			}
			const auto& target = find(_processor, juce::String("t01_") + spec.target);
			require(relays[r] == juce::String("push_") + spec.target && relay.getNumSteps() == target.getNumSteps()
				&& relay.getNormalisableRange().start == target.getNormalisableRange().start
				&& relay.getNormalisableRange().end == target.getNormalisableRange().end
				&& std::abs(relay.getValue() - target.getValue()) <= 1e-6f, relays[r].toStdString() + ": not its target's kind");
		}
		std::printf("  %d parameters, %d per Track, %d shared; the IDs are the frozen list's\n", ids.size(),
			_processor.trackIds(0).size(), ids.size() - Tracks * _processor.trackIds(0).size());
		require(_processor.trackIds(0).size() == 75 + 16, "91 a Track: 75 and Machine 9-24");
		std::printf("PASS: the IDs are frozen\n");
	}

	// ---- the engine's calls, by ID, apart from the processor's ----

	using Values = std::map<juce::String, double>;	// plain values by ID

	struct Expected
	{
		std::array<ChainSettings, Tracks> chain{};
		std::array<std::array<LfoSettings, 2>, Tracks> lfo{};
		bool masterOn = false;
	};

	using TrackSetter = std::function<void(Engine&, Expected&, int, double)>;
	using SharedSetter = std::function<void(Engine&, double)>;

	int whole(const double _v)
	{
		return static_cast<int>(std::lround(_v));
	}

	uint8_t byteOf(const double _v)
	{
		return static_cast<uint8_t>(whole(_v));
	}

	const std::map<std::string, TrackSetter>& trackSetters()
	{
		static const std::map<std::string, TrackSetter> setters = []
		{
			std::map<std::string, TrackSetter> m;
			const auto chain = [&m](const char* _id, std::function<void(ChainSettings&, int)> _f)
			{
				m[_id] = [_f](Engine&, Expected& _x, const int _t, const double _v) { _f(_x.chain[static_cast<size_t>(_t)], whole(_v)); };
			};
			for(int d = 0; d < 2; ++d)
			{
				const std::string at = "drv" + std::to_string(d + 1) + "_";
				chain((at + "type").c_str(), [d](ChainSettings& _s, const int _v) { _s.drive[static_cast<size_t>(d)].type = static_cast<DriveType>(_v); });
				chain((at + "post").c_str(), [d](ChainSettings& _s, const int _v) { _s.drive[static_cast<size_t>(d)].post = _v != 0; });
				chain((at + "amount").c_str(), [d](ChainSettings& _s, const int _v) { _s.drive[static_cast<size_t>(d)].amount = _v; });
				chain((at + "mix").c_str(), [d](ChainSettings& _s, const int _v) { _s.drive[static_cast<size_t>(d)].mix = _v; });
			}
			chain("band_type", [](ChainSettings& _s, const int _v) { _s.band.type = static_cast<BandType>(_v); });
			chain("band_after", [](ChainSettings& _s, const int _v) { _s.band.after = _v != 0; });
			for(int b = 0; b < 6; ++b)
				chain(("band_" + std::to_string(b + 1)).c_str(), [b](ChainSettings& _s, const int _v) { _s.band.values[static_cast<size_t>(b)] = _v; });
			chain("flt_type", [](ChainSettings& _s, const int _v) { _s.filter.type = static_cast<FilterType>(_v); });
			chain("flt_mode", [](ChainSettings& _s, const int _v) { _s.filter.mode = _v; });
			chain("flt_freq", [](ChainSettings& _s, const int _v) { _s.filter.frequency = _v; });
			chain("flt_res", [](ChainSettings& _s, const int _v) { _s.filter.resonance = _v; });
			chain("flt_width", [](ChainSettings& _s, const int _v) { _s.filter.width = _v; });
			chain("fenv_delay", [](ChainSettings& _s, const int _v) { _s.filter.envelope.delay = _v; });
			chain("fenv_attack", [](ChainSettings& _s, const int _v) { _s.filter.envelope.attack = _v; });
			chain("fenv_decay", [](ChainSettings& _s, const int _v) { _s.filter.envelope.decay = _v; });
			chain("fenv_sustain", [](ChainSettings& _s, const int _v) { _s.filter.envelope.sustain = _v; });
			chain("fenv_release", [](ChainSettings& _s, const int _v) { _s.filter.envelope.release = _v; });
			chain("fenv_reset", [](ChainSettings& _s, const int _v) { _s.filter.envelope.reset = _v != 0; });
			chain("fenv_depth", [](ChainSettings& _s, const int _v) { _s.filter.depth = _v; });
			chain("flt_key", [](ChainSettings& _s, const int _v) { _s.filter.keyTrack = _v; });
			chain("amp_on", [](ChainSettings& _s, const int _v) { _s.amp.on = _v != 0; });
			chain("amp_attack", [](ChainSettings& _s, const int _v) { _s.amp.attack = _v; });
			chain("amp_hold", [](ChainSettings& _s, const int _v) { _s.amp.hold = _v; });
			chain("amp_decay", [](ChainSettings& _s, const int _v) { _s.amp.decay = _v; });
			chain("amp_sustain", [](ChainSettings& _s, const int _v) { _s.amp.sustain = _v; });
			chain("amp_release", [](ChainSettings& _s, const int _v) { _s.amp.release = _v; });
			chain("amp_mode", [](ChainSettings& _s, const int _v) { _s.amp.adsr = _v != 0; });
			chain("vel_target", [](ChainSettings& _s, const int _v) { _s.velocityTarget = static_cast<VelocityTarget>(_v); });
			chain("vel_depth", [](ChainSettings& _s, const int _v) { _s.velocityDepth = _v; });
			for(int l = 0; l < 2; ++l)
			{
				const std::string at = "lfo" + std::to_string(l + 1) + "_";
				const auto lfo = [&m, l, at](const char* _id, std::function<void(LfoSettings&, double)> _f)
				{
					m[at + _id] = [_f, l](Engine&, Expected& _x, const int _t, const double _v) { _f(_x.lfo[static_cast<size_t>(_t)][static_cast<size_t>(l)], _v); };
				};
				lfo("sync", [](LfoSettings& _s, const double _v) { _s.rate.sync = _v != 0; });
				lfo("speed", [](LfoSettings& _s, const double _v) { _s.rate.division = whole(_v); });
				lfo("feel", [](LfoSettings& _s, const double _v) { _s.rate.feel = static_cast<LfoFeel>(whole(_v)); });
				lfo("hz", [](LfoSettings& _s, const double _v) { _s.rate.hz = _v; });
				lfo("shape", [](LfoSettings& _s, const double _v) { _s.shape = static_cast<LfoShape>(whole(_v)); });
				lfo("mode", [](LfoSettings& _s, const double _v) { _s.mode = static_cast<LfoMode>(whole(_v)); });
				lfo("depth", [](LfoSettings& _s, const double _v) { _s.depth = whole(_v); });
				lfo("track", [](LfoSettings& _s, const double _v) { _s.track = whole(_v) - 1; });
				lfo("target", [](LfoSettings& _s, const double _v) { _s.target = static_cast<LfoTarget>(whole(_v)); });
			}
			const auto send = [&m](const char* _id, const Send _send)
			{
				m[_id] = [_send](Engine& _e, Expected&, const int _t, const double _v) { _e.setSend(_t, _send, whole(_v)); };
			};
			send("send_delay", Send::Delay);
			send("send_reverb", Send::Reverb);
			send("send_chorus", Send::Chorus);
			send("send_phaser", Send::Phaser);
			m["accent"] = [](Engine& _e, Expected&, const int _t, const double _v) { _e.setAccent(_t, whole(_v)); };
			for(int i = 8; i < MachineParameters; ++i)
				m["m" + std::to_string(i + 1)] = [i](Engine& _e, Expected&, const int _t, const double _v)
				{
					_e.setParameter(_t, i, static_cast<uint8_t>(whole(_v)));
				};
			return m;
		}();
		return setters;
	}

	const std::map<std::string, SharedSetter>& sharedSetters()
	{
		static const std::map<std::string, SharedSetter> setters = []
		{
			using namespace md::fast;
			std::map<std::string, SharedSetter> m;
			m["delay_source"] = [](Engine& _e, const double _v) { _e.sends().setSource(Send::Delay, static_cast<SendSource>(whole(_v))); };
			m["reverb_source"] = [](Engine& _e, const double _v) { _e.sends().setSource(Send::Reverb, static_cast<SendSource>(whole(_v))); };
			m["delay_output"] = [](Engine& _e, const double _v) { _e.sends().setOutput(Send::Delay, whole(_v)); };
			m["reverb_output"] = [](Engine& _e, const double _v) { _e.sends().setOutput(Send::Reverb, whole(_v)); };
			m["chorus_output"] = [](Engine& _e, const double _v) { _e.sends().setOutput(Send::Chorus, whole(_v)); };
			m["phaser_output"] = [](Engine& _e, const double _v) { _e.sends().setOutput(Send::Phaser, whole(_v)); };
			const auto echo = [&m](const char* _id, const int _byte)
			{
				m[_id] = [_byte](Engine& _e, const double _v) { _e.sends().setEcho(_byte, byteOf(_v)); };
			};
			echo("echo_time", EchoTime); echo("echo_mod", EchoMod); echo("echo_mfrq", EchoMfrq); echo("echo_fb", EchoFb);
			echo("echo_fltf", EchoFltf); echo("echo_fltw", EchoFltw); echo("echo_mono", EchoMono); echo("echo_lev", EchoLev);
			const auto gate = [&m](const char* _id, const int _byte)
			{
				m[_id] = [_byte](Engine& _e, const double _v) { _e.sends().setGateBox(_byte, byteOf(_v)); };
			};
			gate("gate_dvol", GateDvol); gate("gate_pred", GatePred); gate("gate_dec", GateDec); gate("gate_damp", GateDamp);
			gate("gate_hp", GateHp); gate("gate_lp", GateLp); gate("gate_gate", GateGate); gate("gate_lev", GateLev);
			const auto delay = [&m](const char* _id, const int _byte)
			{
				m[_id] = [_byte](Engine& _e, const double _v) { _e.sends().setSyntaktDelay(_byte, byteOf(_v)); };
			};
			delay("delay_time", StDelayTime); delay("delay_cross", StDelayCross); delay("delay_width", StDelayWidth);
			delay("delay_fdbk", StDelayFdbk); delay("delay_hp", StDelayHp); delay("delay_lp", StDelayLp);
			const auto reverb = [&m](const char* _id, const int _byte)
			{
				m[_id] = [_byte](Engine& _e, const double _v) { _e.sends().setSyntaktReverb(_byte, byteOf(_v)); };
			};
			reverb("reverb_pre", StReverbPre); reverb("reverb_dec", StReverbDec); reverb("reverb_freq", StReverbFreq);
			reverb("reverb_gain", StReverbGain); reverb("reverb_hp", StReverbHp); reverb("reverb_lp", StReverbLp);
			m["delay_rev"] = [](Engine& _e, const double _v) { _e.sends().setSyntaktDelaySend(byteOf(_v)); };
			m["delay_free"] = [](Engine& _e, const double _v) { _e.sends().setDelayFree(_v); };
			const auto chorus = [&m](const char* _id, const int _byte)
			{
				m[_id] = [_byte](Engine& _e, const double _v) { _e.sends().setChorus(_byte, byteOf(_v)); };
			};
			chorus("chorus_rate", ChorusRate); chorus("chorus_depth", ChorusDepth); chorus("chorus_delay", ChorusDelay);
			chorus("chorus_fdbk", ChorusFeedback); chorus("chorus_level", ChorusLevel); chorus("chorus_shape", ChorusShape);
			chorus("chorus_type", ChorusType);
			const auto phaser = [&m](const char* _id, const int _byte)
			{
				m[_id] = [_byte](Engine& _e, const double _v) { _e.sends().setPhaser(_byte, byteOf(_v)); };
			};
			phaser("phaser_mode", PhaserMode); phaser("phaser_stages", PhaserStages); phaser("phaser_rate", PhaserRate);
			phaser("phaser_depth", PhaserDepth); phaser("phaser_freq", PhaserFrequency); phaser("phaser_spread", PhaserSpread);
			phaser("phaser_fdbk", PhaserFeedback); phaser("phaser_level", PhaserLevel); phaser("phaser_dp4_phase", PhaserDp4Phase);
			phaser("phaser_dp4_left", PhaserDp4DelayLeft); phaser("phaser_dp4_right", PhaserDp4DelayRight);
			phaser("phaser_dp4_fdbk", PhaserDp4DelayFeedback);
			const auto master = [&m](const char* _id, const int _byte)
			{
				m[_id] = [_byte](Engine& _e, const double _v) { _e.master().set(_byte, byteOf(_v)); };
			};
			master("master_threshold", MasterThreshold); master("master_ratio", MasterRatio);
			master("master_attack", MasterAttack); master("master_release", MasterRelease);
			master("master_makeup", MasterMakeup); master("master_mix", MasterMix); master("master_filter", MasterFilter);
			master("master_drive", MasterDrive); master("master_volume", MasterVolume);
			m["oversampling"] = [](Engine& _e, const double _v) { _e.setOversampling(1 << whole(_v)); };
			m["delay_sync"] = [](Engine& _e, const double _v) { _e.sends().setDelaySync(whole(_v) != 0); };
			m["delay_note"] = [](Engine& _e, const double _v) { _e.sends().setDelayNote(parameters::delayNoteQuarters(whole(_v))); };
			m["chorus_sync"] = [](Engine& _e, const double _v) { _e.sends().setLfoSync(Send::Chorus, whole(_v) != 0); };
			m["phaser_sync"] = [](Engine& _e, const double _v) { _e.sends().setLfoSync(Send::Phaser, whole(_v) != 0); };
			m["chorus_speed"] = [](Engine& _e, const double _v)
			{
				_e.sends().setLfoNote(Send::Chorus, parameters::lfoNoteQuarters(whole(_v)));
			};
			m["phaser_speed"] = [](Engine& _e, const double _v)
			{
				_e.sends().setLfoNote(Send::Phaser, parameters::lfoNoteQuarters(whole(_v)));
			};
			m["phaser_dp4_sh"] = [](Engine& _e, const double _v) { _e.sends().setPhaser(PhaserDp4SampleHold, byteOf(_v)); };
			return m;
		}();
		return setters;
	}

	// An engine told _values as the processor tells its engine at its first block: the Tracks' first parameters, then
	// ticket 18's by the setters above, the master's On after its bytes
	std::unique_ptr<Engine> engineOf(const Library& _library, const Processor& _processor, const Values& _values)
	{
		auto e = std::make_unique<Engine>(_library);
		Expected x;
		const auto value = [&](const juce::String& _id) { return _values.at(_id); };
		for(int t = 0; t < Tracks; ++t)
		{
			const auto id = juce::String::formatted("t%02d_", t + 1);
			const auto& machine = _processor.machines()[static_cast<size_t>(whole(value(id + "machine")))];
			e->setMachine(t, machine);
			for(int i = 0; i < MachineParameters; ++i)
			{
				const int v = whole(value(id + "m" + juce::String(i + 1)));
				if(v != e->parameter(t, i))
					e->setParameter(t, i, static_cast<uint8_t>(v));
			}
			e->setNote(t, whole(value(id + "note")));
			e->setLevel(t, whole(value(id + "level")));
			e->setPan(t, whole(value(id + "pan")));
			e->setOutput(t, whole(value(id + "output")));
			e->setChokes(t, static_cast<uint32_t>(whole(value(id + "chokes"))));
		}
		e->setTempo(value("tempo"));
		for(const auto& [id, v] : _values)
		{
			if(id.startsWith("t") && id[3] == '_')
			{
				const auto setter = trackSetters().find(id.substring(4).toStdString());
				if(setter != trackSetters().end())
					setter->second(*e, x, id.substring(1, 3).getIntValue() - 1, v);
				continue;
			}
			if(id == "master_on")
				x.masterOn = v != 0;
			else if(const auto setter = sharedSetters().find(id.toStdString()); setter != sharedSetters().end())
				setter->second(*e, v);
		}
		for(int t = 0; t < Tracks; ++t)
		{
			e->setChain(t, x.chain[static_cast<size_t>(t)]);
			for(int l = 0; l < 2; ++l)
				e->setLfo(t, l, x.lfo[static_cast<size_t>(t)][static_cast<size_t>(l)]);
		}
		e->setMasterOn(x.masterOn);
		return e;
	}

	// ---- the takes ----

	// The effects' bytes slew from 0 at a new engine, the Machinedrum's at the OS's step: they have Warm samples to get there
	constexpr int Warm = 4096, Samples = Warm + 32768;
	// The Echo's feedback and the Gate Box's gate are heard past the Echo's glide to its time, and the gate's hold
	constexpr int Long = 1378 * Block;

	bool slow(const juce::String& _step)
	{
		return _step == "echo_fb" || _step == "gate_gate";
	}

	// A note's on and off, by Block
	int eventAt(const int _at)
	{
		return _at == 0 || _at == Warm || _at == Warm + 2048 ? 1 : _at == Warm + 1024 || _at == Warm + 3072 ? -1 : 0;
	}

	using Take = std::vector<std::vector<float>>;

	// Every Track hits after Warm and 2048 later, at velocity 100, each Hit held 1024
	Take play(Processor& _processor, const int _samples = Samples)
	{
		_processor.prepareToPlay(SampleRate, Block);
		Take take(Channels, std::vector<float>(static_cast<size_t>(_samples)));
		juce::AudioBuffer<float> buffer(Channels, Block);
		juce::MidiBuffer midi;
		for(int at = 0; at < _samples; at += Block)
		{
			buffer.clear();
			midi.clear();
			for(int t = 0; t < Tracks && eventAt(at); ++t)
				midi.addEvent(eventAt(at) > 0 ? juce::MidiMessage::noteOn(1, 36 + t, static_cast<juce::uint8>(100))
					: juce::MidiMessage::noteOff(1, 36 + t), 0);
			_processor.processBlock(buffer, midi);
			for(int c = 0; c < Channels; ++c)
				std::copy_n(buffer.getReadPointer(c), Block, take[static_cast<size_t>(c)].begin() + at);
		}
		return take;
	}

	Take play(Engine& _engine, const int _samples = Samples)
	{
		Take take(Channels, std::vector<float>(static_cast<size_t>(_samples)));
		std::array<float*, Channels> out{};
		for(int at = 0; at < _samples; at += Block)
		{
			for(int t = 0; t < Tracks && eventAt(at); ++t)
				if(eventAt(at) > 0)
					_engine.noteOn(t, 100);
				else
					_engine.noteOff(t);
			for(size_t c = 0; c < out.size(); ++c)
				out[c] = take[c].data() + at;
			_engine.setTempo(120);
			_engine.render(out.data(), Block);
		}
		return take;
	}

	// The VST3 host test's pattern: a Hit every 1 to 1500 samples, on a Track and at a velocity drawn
	struct Hit
	{
		int at, track, velocity;
	};

	std::vector<Hit> pattern(const int _samples)
	{
		std::mt19937 random(1);
		std::vector<Hit> hits;
		for(int at = 100; at < _samples - 3000; at += 1 + static_cast<int>(random() % 1500))
			hits.push_back({at, static_cast<int>(random() % Tracks), 1 + static_cast<int>(random() % 127)});
		return hits;
	}

	Take play(Processor& _processor, const std::vector<Hit>& _hits, const int _samples)
	{
		_processor.prepareToPlay(SampleRate, Block);
		Take take(Channels, std::vector<float>(static_cast<size_t>(_samples)));
		juce::AudioBuffer<float> buffer(Channels, Block);
		juce::MidiBuffer midi;
		size_t next = 0;
		for(int at = 0; at < _samples; at += Block)
		{
			buffer.clear();
			midi.clear();
			for(; next < _hits.size() && _hits[next].at < at + Block; ++next)
				midi.addEvent(juce::MidiMessage::noteOn(1, 36 + _hits[next].track, static_cast<juce::uint8>(_hits[next].velocity)),
					_hits[next].at - at);
			_processor.processBlock(buffer, midi);
			for(int c = 0; c < Channels; ++c)
				std::copy_n(buffer.getReadPointer(c), Block, take[static_cast<size_t>(c)].begin() + at);
		}
		return take;
	}

	Take play(Engine& _engine, const std::vector<Hit>& _hits, const int _samples)
	{
		Take take(Channels, std::vector<float>(static_cast<size_t>(_samples)));
		std::array<float*, Channels> out{};
		for(size_t c = 0; c < out.size(); ++c)
			out[c] = take[c].data();
		int done = 0;
		const auto upTo = [&](const int _at)
		{
			_engine.render(out.data(), _at - done);
			for(auto*& o : out)
				o += _at - done;
			done = _at;
		};
		for(const auto& hit : _hits)
		{
			upTo(hit.at);
			_engine.noteOn(hit.track, hit.velocity);
		}
		upTo(_samples);
		return take;
	}

	bool same(const Take& _a, const Take& _b, std::string* _where = nullptr)
	{
		for(size_t c = 0; c < _a.size(); ++c)
			for(size_t n = 0; n < _a[c].size(); ++n)
				if(_a[c][n] != _b[c][n])
				{
					if(_where)
						*_where = "channel " + std::to_string(c) + " at sample " + std::to_string(n);
					return false;
				}
		return true;
	}

	// Takes the same sample for sample, or which sample first differs
	void requireSame(const Take& _a, const Take& _b, const std::string& _what)
	{
		std::string where;
		const bool equal = same(_a, _b, &where);
		require(equal, _what + where);
	}

	// The relays for Push, which follow the Track shown and are not of the sound
	bool isRelay(const juce::String& _id)
	{
		return _id.startsWith("push_");
	}

	Values valuesOf(Processor& _processor)
	{
		Values v;
		for(auto* parameter : _processor.getParameters())
			if(auto* p = dynamic_cast<juce::RangedAudioParameter*>(parameter); p && !isRelay(p->getParameterID()))
				v[p->getParameterID()] = plain(*p);
		return v;
	}

	// ---- every parameter of ticket 18 ----

	// Two sounds over the defaults: each Track on its own output, the sends fed, an LFO on the level and one on the
	// frequency, the drives up and the filter envelope deep. A sound each for the settings the other leaves unheard:
	// A the Machinedrum's effects, the first LFO synced, the phaser; B the Syntakt's effects, the second LFO synced,
	// the DP/4, the three bands, a second drive on every Track, the master on and pressing
	Values sound(const bool _b)
	{
		Values v;
		for(int t = 0; t < Tracks; ++t)
		{
			const auto id = juce::String::formatted("t%02d_", t + 1);
			v[id + "output"] = t;
			v[id + "note"] = 40 + 2 * t;
			v[id + "pan"] = 8 * t;
			v[id + "amp_decay"] = 40;
			for(const char* send : {"send_delay", "send_reverb", "send_chorus", "send_phaser"})
				v[id + send] = 90;
			v[id + "lfo1_depth"] = 60;
			v[id + "lfo1_target"] = static_cast<int>(LfoTarget::Level);
			v[id + "lfo1_speed"] = 2;
			v[id + "lfo1_sync"] = _b ? 0 : 1;
			v[id + "lfo1_hz"] = 8;
			v[id + "lfo2_depth"] = 40;
			v[id + "lfo2_target"] = static_cast<int>(LfoTarget::Frequency);
			v[id + "lfo2_speed"] = 2;
			v[id + "lfo2_sync"] = _b ? 1 : 0;
			v[id + "lfo2_hz"] = 8;
			v[id + "drv1_amount"] = 64;
			v[id + "drv2_amount"] = 64;
			v[id + "fenv_depth"] = 100;
			v[id + "vel_target"] = static_cast<int>(VelocityTarget::Frequency);
			v[id + "vel_depth"] = 50;
			if(_b)
			{
				v[id + "amp_mode"] = 1;
				v[id + "flt_freq"] = 64;
				v[id + "fenv_attack"] = 100;
				v[id + "band_type"] = static_cast<int>(BandType::ThreeBand);
				v[id + "drv2_type"] = static_cast<int>(DriveType::Syntakt);
			}
		}
		v["echo_time"] = 4;
		v["delay_time"] = 4;
		// TIME heard on both, the Echo's on A, the Syntakt's on B; the chorus's and the phaser's notes on B
		v["delay_sync"] = 0;
		v["chorus_sync"] = _b ? 1 : 0;
		v["phaser_sync"] = _b ? 1 : 0;
		v["gate_gate"] = 0;
		v["echo_mod"] = 40;
		v["echo_fb"] = 40;
		if(_b)
		{
			v["delay_source"] = 1;
			v["reverb_source"] = 1;
			v["phaser_mode"] = 2;
			v["master_on"] = 1;
			v["master_threshold"] = 40;
			v["master_ratio"] = 100;
			v["master_drive"] = 64;
			v["delay_rev"] = 64;
			v["delay_fdbk"] = 40;
			v["phaser_dp4_left"] = 8;
			v["phaser_dp4_right"] = 8;
			v["phaser_dp4_fdbk"] = 100;
		}
		return v;
	}

	// A value away from _value: half the range on, round
	double changed(const juce::RangedAudioParameter& _p, const double _value)
	{
		if(auto* f = dynamic_cast<const juce::AudioParameterFloat*>(&_p))
			return _value == 0 ? 50 : std::min(_value * 4, static_cast<double>(f->range.end));
		const double lo = _p.convertFrom0to1(0), hi = _p.convertFrom0to1(1);
		const int count = whole(hi - lo) + 1;
		const int step = count <= 8 ? 1 : count / 2;
		return lo + (whole(_value - lo) + step) % count;
	}

	void applyValues(Processor& _processor, const Values& _values)
	{
		for(const auto& [id, v] : _values)
			set(find(_processor, id), v);
	}

	void everyParameter(const Library& _library)
	{
		Processor processor;
		allOutputs(processor);
		const auto ids = idsOf(processor);
		const int first = ids.indexOf("tempo") + 1;

		// At its defaults, the engine's own sound
		const auto defaults = valuesOf(processor);
		{
			auto bare = std::make_unique<Engine>(_library);
			for(int t = 0; t < Tracks; ++t)
			{
				const auto id = juce::String::formatted("t%02d_", t + 1);
				bare->setMachine(t, processor.machines()[static_cast<size_t>(whole(defaults.at(id + "machine")))]);
				bare->setLevel(t, whole(defaults.at(id + "level")));
				bare->setPan(t, whole(defaults.at(id + "pan")));
				bare->setNote(t, whole(defaults.at(id + "note")));
			}
			std::string where;
			require(same(play(processor), play(*bare), &where), "the defaults change the engine's sound: " + where);
			// And each Track on its own output, on the VST3 host test's pattern
			bare = std::make_unique<Engine>(_library);
			for(int t = 0; t < Tracks; ++t)
			{
				const auto id = juce::String::formatted("t%02d_", t + 1);
				bare->setMachine(t, processor.machines()[static_cast<size_t>(whole(defaults.at(id + "machine")))]);
				bare->setLevel(t, whole(defaults.at(id + "level")));
				bare->setOutput(t, t);
				set(find(processor, id + "output"), t);
			}
			constexpr int Pattern = 1172 * Block;		// the VST3 host test's 150000, in whole blocks
			const auto hits = pattern(Pattern);
			require(same(play(processor, hits, Pattern), play(*bare, hits, Pattern), &where),
				"the defaults change the engine's sound, each Track on its output: " + where);
			for(int t = 0; t < Tracks; ++t)
				set(find(processor, juce::String::formatted("t%02d_output", t + 1)), 0);
			std::printf("  at its defaults: the engine's sound\n");
		}

		juce::MemoryBlock clean;
		processor.getStateInformation(clean);
		std::array<juce::MemoryBlock, 2> states;
		std::array<Values, 2> soundValues;
		std::array<Take, 2> takes, longTakes;
		for(int b = 0; b < 2; ++b)
		{
			processor.setStateInformation(clean.getData(), static_cast<int>(clean.getSize()));
			applyValues(processor, sound(b == 1));
			processor.getStateInformation(states[static_cast<size_t>(b)]);
			soundValues[static_cast<size_t>(b)] = valuesOf(processor);
			takes[static_cast<size_t>(b)] = play(processor);
			longTakes[static_cast<size_t>(b)] = play(processor, Long);
			std::string where;
			require(same(takes[static_cast<size_t>(b)], play(*engineOf(_library, processor, soundValues[static_cast<size_t>(b)])), &where),
				std::string("sound ") + (b ? "B" : "A") + " is not the engine's: " + where);
		}

		// A Track's parameter on all 16 at once, a shared one alone
		std::vector<juce::String> steps;
		for(int i = first; i < ids.size(); ++i)
			if(!isRelay(ids[i]) && (!ids[i].startsWith("t") || ids[i].startsWith("t01_")))
				steps.push_back(ids[i].startsWith("t01_") ? ids[i].substring(4) : ids[i]);
		const auto start = std::chrono::steady_clock::now();
		std::vector<std::string> unheard;
		for(const auto& step : steps)
		{
			const bool perTrack = trackSetters().count(step.toStdString()) != 0;
			require(perTrack || sharedSetters().count(step.toStdString()) || step == "master_on" || step == "pattern",
				"no engine call for " + step.toStdString());
			bool heard = false;
			for(int b = 0; b < 2; ++b)
			{
				processor.setStateInformation(states[static_cast<size_t>(b)].getData(), static_cast<int>(states[static_cast<size_t>(b)].getSize()));
				for(int t = 0; t < (perTrack ? Tracks : 1); ++t)
				{
					const auto id = perTrack ? juce::String::formatted("t%02d_", t + 1) + step : step;
					auto& p = find(processor, id);
					set(p, changed(p, plain(p)));
				}
				const int length = slow(step) ? Long : Samples;
				const auto take = play(processor, length);
				std::string where;
				require(same(take, play(*engineOf(_library, processor, valuesOf(processor)), length), &where),
					step.toStdString() + (b ? " on B" : " on A") + ": the processor is not the engine told the same, " + where);
				heard |= !same(take, (slow(step) ? longTakes : takes)[static_cast<size_t>(b)]);
			}
			if(!heard)
				unheard.push_back(step.toStdString());
		}
		const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
		std::string list;
		for(const auto& u : unheard)
			list += " " + u;
		std::printf("  %zu settings (%d parameters) each set on two sounds, %.1f s: the engine told the same plays the same;"
			" unheard on both:%s\n", steps.size(), ids.size() - first, seconds, list.empty() ? " none" : list.c_str());
		// The accent plays on a Hit with an accent, which no host's note gives, Pattern asks for a pattern, all empty here,
		// and the two sounds' machines use Machine 1-8 alone, Track 1's Syntakt machine also Machine 9, its voice's HOLD
		std::vector<std::string> expected{"accent", "pattern"};
		for(int i = 10; i <= MachineParameters; ++i)
			expected.push_back("m" + std::to_string(i));
		// Delay Duration plays while Sync is on, which both sounds leave off for TIME's sake: delay_sync's step, which
		// turns it on, plays the processor's against the engine's, and edDrumsEngineTest's sendTimes the notes
		expected.push_back("delay_note");
		require(unheard == expected, "a setting the processor gives is not heard");
		std::printf("PASS: every parameter reaches the engine\n");
	}

	// ---- a machine of another family brings its chain, which the parameters take ----

	void familyChange()
	{
		Processor processor;
		allOutputs(processor);
		processor.prepareToPlay(SampleRate, Block);
		block(processor);
		set(find(processor, "t01_drv1_amount"), 100);
		block(processor);
		const auto chainOf = [&]
		{
			elektron::drums::parameters::ChainArray v{};
			for(size_t i = 0; i < v.size(); ++i)
				v[i] = whole(plain(find(processor, juce::String("t01_") + elektron::drums::parameters::chainSpecs[i].id)));
			return v;
		};
		const auto choiceOf = [&](const Machine& _machine)
		{
			const auto& list = processor.machines();
			return static_cast<int>(std::find(list.begin(), list.end(), _machine) - list.begin());
		};
		set(find(processor, "t01_machine"), choiceOf({Family::Machinedrum, 16}));
		block(processor);
		require(chainOf() == elektron::drums::parameters::valuesOf(machinedrumChain()), "TRX-BD: not the Machinedrum's chain");
		set(find(processor, "t01_drv1_amount"), 90);
		set(find(processor, "t01_machine"), choiceOf({Family::Machinedrum, 17}));
		block(processor);
		require(whole(plain(find(processor, "t01_drv1_amount"))) == 90, "a machine of the same family changes the chain");
		set(find(processor, "t01_machine"), choiceOf({Family::Syntakt, 7}));
		block(processor);
		require(chainOf() == elektron::drums::parameters::valuesOf(syntaktChain(true)), "SY BITS: not the Syntakt's chain");
		std::printf("PASS: a machine of another family brings its chain to the parameters, one of the same family keeps it\n");
	}

	// ---- the state ----

	struct Edits final : juce::AudioProcessorListener
	{
		int edits = 0;
		void audioProcessorParameterChanged(juce::AudioProcessor*, int, float) override { ++edits; }
		void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails&) override {}
		void audioProcessorParameterChangeGestureBegin(juce::AudioProcessor*, int) override { ++edits; }
		void audioProcessorParameterChangeGestureEnd(juce::AudioProcessor*, int) override { ++edits; }
	};

	void state()
	{
		// Another instance, given the state, plays the same
		Processor one;
		allOutputs(one);
		applyValues(one, sound(true));
		set(find(one, "t03_flt_freq"), 30);
		set(find(one, "t05_lfo2_shape"), static_cast<int>(LfoShape::Square));
		juce::MemoryBlock saved;
		one.getStateInformation(saved);
		Processor other;
		allOutputs(other);
		Edits edits;
		other.addListener(&edits);
		other.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
		other.removeListener(&edits);
		require(edits.edits == 0, "a state restored tells the host of edits");
		require(valuesOf(other) == valuesOf(one), "a parameter of the state");
		require(same(play(one), play(other)), "a state restored does not play the same");
		std::printf("  a state restored: %d parameters, the same samples, no edit told\n", static_cast<int>(valuesOf(other).size()));

		// A state of ticket 13: ticket 18's parameters missing, Track 1 on a Machinedrum machine, an ID not ours
		const auto defaults = valuesOf(other);
		{
			Processor fresh;
			// TRX-BD, and what fresh has as defaults
			int trxBd = -1;
			for(size_t m = 0; m < fresh.machines().size(); ++m)
				if(fresh.machines()[m] == Machine{Family::Machinedrum, 16})
					trxBd = static_cast<int>(m);
			require(trxBd >= 0, "TRX-BD");
			juce::XmlElement xml("ElektronDrums");
			xml.setAttribute("version", 1);
			for(const auto& id : idsOf(fresh))
			{
				if(id == "tempo")
					break;
				// Machine 1-8 as fresh has them are in the Syntakt's page order, which a state of version 1 is not
				if(id.substring(3).startsWith("_m") && juce::CharacterFunctions::isDigit(id[5]))
					continue;
				xml.setAttribute(id, plain(find(fresh, id)));
			}
			xml.setAttribute("t01_machine", trxBd);
			xml.setAttribute("not_ours", 3);
			juce::MemoryBlock old;
			juce::AudioProcessor::copyXmlToBinary(xml, old);
			const auto expected = valuesOf(fresh);

			other.setStateInformation(old.getData(), static_cast<int>(old.getSize()));
			const auto loaded = valuesOf(other);
			const auto mdChain = elektron::drums::parameters::valuesOf(machinedrumChain());
			int checked = 0;
			for(const auto& [id, v] : loaded)
			{
				if(id == "t01_machine")
				{
					require(whole(v) == trxBd, "the old state's machine");
					continue;
				}
				const int chainIndex = static_cast<int>(elektron::drums::parameters::chainSpecs.size());
				int k = chainIndex;
				for(size_t i = 0; i < elektron::drums::parameters::chainSpecs.size(); ++i)
					if(id == juce::String("t01_") + elektron::drums::parameters::chainSpecs[i].id)
						k = static_cast<int>(i);
				if(k < chainIndex)
					require(whole(v) == mdChain[static_cast<size_t>(k)], id.toStdString() + ": not the Machinedrum's chain");
				else if(id == "delay_sync")
					require(whole(v) == 0, "a state from before Delay Sync does not keep its TIME");
				else
					require(std::abs(v - expected.at(id)) <= 1e-5 * std::max(1.0, std::abs(v)),
						id.toStdString() + ": not its default");	// a float default comes back through its 0-1 value
				++checked;
			}
			require(other.parameter("not_ours") == nullptr, "an ID not ours");
			std::printf("  a state without ticket 18's parameters: %d at their defaults, Track 1's chain the Machinedrum's\n",
				checked);
		}

		// The Syntakt family's amp of before (HOLD 64, DECAY 127) in a state still plays: its values are the state's, a
		// fresh Track's are the new defaults (HOLD 0, DECAY 126)
		{
			Processor fresh;
			allOutputs(fresh);
			require(whole(plain(find(fresh, "t01_amp_hold"))) == 0 && whole(plain(find(fresh, "t01_amp_decay"))) == 126,
				"a fresh Syntakt Track's amp");
			juce::MemoryBlock saved;
			fresh.getStateInformation(saved);
			const auto xml = Processor::readState(saved.getData(), static_cast<int>(saved.getSize()));
			require(xml != nullptr, "the state's XML");
			xml->setAttribute("t01_amp_hold", 64);
			xml->setAttribute("t01_amp_decay", 127);
			juce::MemoryBlock old;
			juce::AudioProcessor::copyXmlToBinary(*xml, old);
			Processor loaded, edited;
			allOutputs(loaded);
			allOutputs(edited);
			loaded.setStateInformation(old.getData(), static_cast<int>(old.getSize()));
			require(whole(plain(find(loaded, "t01_amp_hold"))) == 64 && whole(plain(find(loaded, "t01_amp_decay"))) == 127,
				"a state's HOLD 64 and DECAY 127 not loaded");
			set(find(edited, "t01_amp_hold"), 64);
			set(find(edited, "t01_amp_decay"), 127);
			const auto played = play(loaded);
			require(same(played, play(edited)), "a state's HOLD 64 does not play HOLD 64");
			require(!same(played, play(fresh)), "HOLD 64 plays as the new defaults");
			std::printf("  a state of the amp of before: HOLD 64, DECAY 127 loaded and played; a fresh Track HOLD 0, DECAY 126\n");
		}
		std::printf("PASS: the state keeps every parameter by ID\n");
	}

	// ---- the Sampler in the Machine list ----

	int choiceOf(const Processor& _processor, const Machine& _machine)
	{
		const auto& list = _processor.machines();
		const auto found = std::find(list.begin(), list.end(), _machine);
		require(found != list.end(), "a machine not in the list");
		return static_cast<int>(found - list.begin());
	}

	bool removedFamily(const Machine& _machine)
	{
		if(_machine.family != Family::Machinedrum || _machine == Sampler)
			return false;
		const auto* family = md::machines::familyOf(md::MachineModel::Machinedrum, _machine.id);
		return family && (family->name == "INP" || family->name == "CTR" || family->name == "MID" || family->name == "RAM");
	}

	void samplerChoice()
	{
		Processor processor;
		// The list of version 2: Off, the Syntakt's, the Machinedrum's that played, ROM-33 to -48 last
		std::vector<Machine> v2{Machine{}};
		for(int id = 0; id < SyntaktMachines; ++id)
			v2.push_back({Family::Syntakt, static_cast<uint8_t>(id)});
		for(const auto& m : md::machines::machines(md::MachineModel::Machinedrum))
			if(m.id < 256 && md::voice::playable(static_cast<uint8_t>(m.id)))
				v2.push_back({Family::Machinedrum, static_cast<uint8_t>(m.id)});
		require(v2.size() >= 16, "the old list's size");
		for(size_t i = 0; i < 16; ++i)
			require(v2[v2.size() - 16 + i] == Machine{Family::Machinedrum, static_cast<uint8_t>(176 + i)},
				"ROM-33 to -48 were not the old list's last 16");
		// ... of version 3, the Sampler in ROM-33's place
		std::vector<Machine> v3(v2.begin(), v2.end() - 16);
		v3.push_back(Sampler);
		// ... of version 4: without INP, CTR, MID and RAM, OPEN DRUM's after the Sampler
		std::vector<Machine> v4;
		for(const auto& m : v3)
			if(!removedFamily(m))
				v4.push_back(m);
		for(int id = 0; id < OpenDrumMachines; ++id)
			v4.push_back({Family::OpenDrum, static_cast<uint8_t>(id)});
		// ... and SOPHIE last, appended within version 4: no choice before it moved
		v4.push_back({Family::Sophie, 0});
		// ... of version 5: without ROM-01 to -32, the factory bank's samples on the Sampler (ticket 27)
		std::vector<Machine> v5;
		for(const auto& m : v4)
			if(!(m.family == Family::Machinedrum && m.id >= 128 && m.id < 160))
				v5.push_back(m);
		require(v4.size() - v5.size() == 32, "ROM-01 to -32 not in version 4's list");
		// ... and the analog machines appended within version 5 (ticket 28): no choice before them moved
		for(int id = 0; id < SyntaktAnalogMachines; ++id)
			v5.push_back({Family::SyntaktAnalog, static_cast<uint8_t>(id)});
		// ... then the Rytm's own (ticket 28, stage 2)
		for(int id = 0; id < RytmAnalogMachines; ++id)
			v5.push_back({Family::RytmAnalog, static_cast<uint8_t>(id)});
		const auto& list = processor.machines();
		require(list == v5, "the list");
		require(list[static_cast<size_t>(processor.rytmChoice())] == Machine{Family::RytmAnalog, 0}, "the Rytm family's choice");
		require(list[static_cast<size_t>(processor.sophieChoice())] == Machine{Family::Sophie, 0}, "SOPHIE's choice");
		{
			// A Track put on SOPHIE starts on its Kick; a factory sound sets Machine 1-20, which the engine plays
			Processor p;
			allOutputs(p);
			p.prepareToPlay(SampleRate, Block);
			block(p);
			set(find(p, "t01_machine"), static_cast<float>(p.sophieChoice()));
			block(p);
			require(p.sophieSound(0) == 0, "a Track put on SOPHIE is not on its Kick");
			p.setSophieSound(0, 3);
			block(p);
			const auto clap = sophie::soundValues(3);
			require(p.sophieSound(0) == 3, "the Clap not set");
			for(int i = 0; i < sophie::MachineSlots; ++i)
				require(p.engine()->parameter(0, i) == clap[static_cast<size_t>(i)], "the engine does not play the Clap");
			set(find(p, "t01_m2"), 100);
			require(p.sophieSound(0) == -1, "an edited sound is still a factory one");
		}
		require(list[static_cast<size_t>(processor.samplerChoice())] == Sampler, "the Sampler's choice");
		const auto& machine = dynamic_cast<juce::AudioParameterChoice&>(find(processor, "t01_machine"));
		require(machine.choices[processor.samplerChoice()] == "SAMPLER", "the Sampler's name");
		for(int id = 0; id < OpenDrumMachines; ++id)
			require(machine.choices[processor.samplerChoice() + 1 + id]
				== names::machine({Family::OpenDrum, static_cast<uint8_t>(id)}), "OPEN DRUM's names");

		const auto at = [](const std::vector<Machine>& _list, const Machine& _m)
		{
			const auto found = std::find(_list.begin(), _list.end(), _m);
			require(found != _list.end(), "a machine not in an old list");
			return static_cast<int>(found - _list.begin());
		};
		const Machine trxBd{Family::Machinedrum, 16}, ctrAl{Family::Machinedrum, 112}, mid{Family::Machinedrum, 96};
		const auto machineAt = [&](Processor& _p, const char* _id)
		{
			return list[static_cast<size_t>(whole(plain(find(_p, _id))))];
		};

		// A state of version 2 on ROM-33 or ROM-40 plays the Sampler; one on TRX-BD stays there
		{
			const int rom33 = static_cast<int>(v2.size()) - 16;
			juce::XmlElement xml("ElektronDrums");
			xml.setAttribute("version", 2);
			xml.setAttribute("t01_machine", rom33);
			xml.setAttribute("t02_machine", rom33 + 7);
			xml.setAttribute("t03_machine", at(v2, trxBd));
			juce::MemoryBlock state;
			juce::AudioProcessor::copyXmlToBinary(xml, state);
			processor.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
			require(machineAt(processor, "t01_machine") == Sampler && machineAt(processor, "t02_machine") == Sampler,
				"an old ROM-33 to -48");
			require(machineAt(processor, "t03_machine") == trxBd, "an old TRX-BD");
		}
		// A state of version 3 and a Kit of version 1 on TRX-BD, the Sampler, CTR-AL, MID-01: TRX-BD, the Sampler, Off
		const auto old = [&](juce::XmlElement& _xml)
		{
			_xml.setAttribute("t01_machine", at(v3, trxBd));
			_xml.setAttribute("t02_machine", at(v3, Sampler));
			_xml.setAttribute("t03_machine", at(v3, ctrAl));
			_xml.setAttribute("t04_machine", at(v3, mid));
			_xml.setAttribute("t05_machine", static_cast<int>(v3.size()) - 2);	// ROM-32, before the Sampler
		};
		const auto check = [&](Processor& _p, const char* _what)
		{
			require(machineAt(_p, "t01_machine") == trxBd && machineAt(_p, "t02_machine") == Sampler
				&& machineAt(_p, "t03_machine") == Machine{} && machineAt(_p, "t04_machine") == Machine{},
				std::string(_what) + ": a machine moved");
			// ROM-32 plays the Sampler on factory slot 32
			require(machineAt(_p, "t05_machine") == Sampler && _p.sampleFingerprint(4) == "factory:32",
				std::string(_what) + ": ROM-32 is not the Sampler on factory slot 32");
		};
		{
			juce::XmlElement xml("ElektronDrums");
			xml.setAttribute("version", 3);
			old(xml);
			juce::MemoryBlock state;
			juce::AudioProcessor::copyXmlToBinary(xml, state);
			Processor p;
			p.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
			check(p, "a state of version 3");
		}
		{
			// A Kit as saved, its Machine choices as version 1 wrote them
			Processor saver;
			require(saver.saveKit(40), "Slot 41 saved");
			KitBank bank(Processor::dataFolder().getChildFile("Kits"));
			auto kit = bank.read(40);
			require(kit != nullptr, "Slot 41 read");
			kit->setAttribute("version", 1);
			old(*kit);
			require(bank.write(40, *kit), "Slot 41 written");
			Processor p;
			require(p.loadKit(40), "the old Kit not loaded");
			check(p, "a Kit of version 1");
			require(!p.kitDiffers(), "an old Kit loaded differs from its Slot");
			require(bank.clear(40), "Slot 41 cleared");
		}
		// What the new list stores, it reads back: OPEN DRUM's and the Sampler
		{
			Processor a;
			set(find(a, "t01_machine"), static_cast<float>(a.samplerChoice() + 1));
			set(find(a, "t02_machine"), static_cast<float>(a.samplerChoice()));
			juce::MemoryBlock state;
			a.getStateInformation(state);
			Processor b;
			b.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
			require(machineAt(b, "t01_machine") == Machine{Family::OpenDrum, 0} && machineAt(b, "t02_machine") == Sampler,
				"a state of version 5");
		}
		// Ticket 27: a state of version 4 and a Kit of version 2 on ROM-05 play the Sampler on factory slot 5, Machine 1-8
		// carried, the 12-bit grain; the state saved again holds the slot, not its audio
		const Machine rom05{Family::Machinedrum, 132};
		const std::array<int, 8> knobs{70, 100, 20, 3, 5, 120, 0, 64};
		const auto onRom = [&](juce::XmlElement& _xml)
		{
			_xml.setAttribute("t03_machine", at(v4, rom05));
			_xml.setAttribute("t04_machine", at(v4, trxBd));
			for(int i = 0; i < 8; ++i)
				_xml.setAttribute("t03_m" + juce::String(i + 1), knobs[static_cast<size_t>(i)]);
			_xml.setAttribute("t03_m9", 100);		// what ROM-05 had there: nothing it played
		};
		size_t factory = 0;
		const auto played = [&](Processor& _p, const char* _what)
		{
			require(machineAt(_p, "t03_machine") == Sampler && machineAt(_p, "t04_machine") == trxBd,
				std::string(_what) + ": ROM-05 not the Sampler");
			require(_p.sampleFingerprint(2) == "factory:5", std::string(_what) + ": not on factory slot 5");
			for(int i = 0; i < 8; ++i)
				require(whole(plain(find(_p, ("t03_m" + std::to_string(i + 1)).c_str()))) == knobs[static_cast<size_t>(i)],
					std::string(_what) + ": Machine " + std::to_string(i + 1) + " not carried");
			require(whole(plain(find(_p, "t03_m9"))) < 64, std::string(_what) + ": not the 12-bit grain");
			factory = _p.factorySamples().size();
			require(factory > 0 ? _p.sampleName(2) == "FACTORY 05" : _p.sampleName(2).contains("not in the Locker"),
				std::string(_what) + ": the sample's name " + _p.sampleName(2).toStdString());
		};
		{
			juce::XmlElement xml("ElektronDrums");
			xml.setAttribute("version", 4);
			onRom(xml);
			juce::MemoryBlock state;
			juce::AudioProcessor::copyXmlToBinary(xml, state);
			Processor p;
			p.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
			played(p, "a state of version 4");
			juce::MemoryBlock saved;
			p.getStateInformation(saved);
			const auto back = Processor::readState(saved.getData(), static_cast<int>(saved.getSize()));
			int samples = 0;
			for(const auto* element : back->getChildWithTagNameIterator("sample"))
			{
				++samples;
				require(element->getIntAttribute("track") == 2 && element->getIntAttribute("factory") == 5
					&& element->getAllSubText().isEmpty(), "the state holds factory audio, or another sample");
			}
			require(back->getIntAttribute("version") == 6 && samples == 1, "the state saved again: no factory slot");
			Processor q;
			q.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
			played(q, "a state of version 5");
		}
		// ... where ROM-05 held a Library sample: one notice names it, once, however often the state is read
		{
			juce::XmlElement xml("ElektronDrums");
			xml.setAttribute("version", 4);
			onRom(xml);
			auto* element = xml.createNewChildElement("sample");
			element->setAttribute("track", 2);
			element->setAttribute("name", "kick.wav");
			element->setAttribute("fingerprint", "f1ng3r");
			juce::MemoryBlock state;
			juce::AudioProcessor::copyXmlToBinary(xml, state);
			Processor p;
			p.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
			p.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
			const auto notices = p.takeNotices();
			require(notices.size() == 1 && notices[0] == "Track 3: sample kick.wav replaced by Factory 05",
				"the replaced sample's notice: " + notices.joinIntoString(" | ").toStdString());
			require(p.takeNotices().isEmpty(), "a notice given twice");
		}
		{
			Processor saver;
			require(saver.saveKit(41), "Slot 42 saved");
			KitBank bank(Processor::dataFolder().getChildFile("Kits"));
			auto kit = bank.read(41);
			require(kit != nullptr, "Slot 42 read");
			kit->setAttribute("version", 2);
			onRom(*kit);
			require(bank.write(41, *kit), "Slot 42 written");
			Processor p;
			require(p.loadKit(41), "the Kit of version 2 not loaded");
			played(p, "a Kit of version 2");
			require(!p.kitDiffers(), "a Kit of version 2 loaded differs from its Slot");
			// Saved again, the Kit holds the slot alone, no factory audio (stockage-proposition.md)
			require(p.saveKit(41), "Slot 42 saved again");
			const auto again = bank.read(41);
			int samples = 0;
			for(const auto* element : again->getChildWithTagNameIterator("sample"))
			{
				++samples;
				require(element->getIntAttribute("track") == 2 && element->getIntAttribute("factory") == 5
					&& element->getAllSubText().isEmpty() && !element->hasAttribute("fingerprint"),
					"the Kit holds factory audio, or another sample");
			}
			require(samples == 1, "the Kit saved again: no factory slot");
			require(bank.clear(41), "Slot 42 cleared");
		}
		std::printf("PASS: Machine's %zu choices (%zu before, %zu with the ROM machines), the Sampler where ROM-33 stood, OPEN"
			" DRUM's 4 after it, no INP, CTR, MID, RAM or ROM; a state of version 2 or 3 and a Kit of version 1 load TRX-BD,"
			" the Sampler as theirs, ROM-32 as the Sampler on factory slot 32, CTR-AL and MID-01 as Off; a state of version 4"
			" and a Kit of version 2 on ROM-05 the Sampler on factory slot 5, its knobs carried, saved as the slot alone;"
			" %zu factory samples\n", list.size(), v3.size(), v4.size(), factory);
	}
	// ---- Machine 9-24 (ticket 26) ----

	// Defined with the sequencer's helpers further down
	Take playSequenced(Processor& _processor, int _samples, const std::function<void(int)>& _atBlock);
	int stepSample(double _quarters);
	int firstDifference(const Take& _a, const Take& _b);
	uint16_t indexOf(Processor& _processor, const juce::String& _id);

	void machinePages()
	{
		const Machine drum{Family::OpenDrum, 0};
		const auto all = allDefaults(drum);
		const auto onDrum = [&](Processor& _p)
		{
			_p.prepareToPlay(SampleRate, Block);
			set(find(_p, "t01_machine"), static_cast<float>(choiceOf(_p, drum)));
			block(_p);
		};

		// A Lock of Machine 17 (FILTER) on step 4 of an OD DRUM Track: its Hit sounds otherwise, from there
		constexpr int Length = 200 * Block;
		Processor unlocked, locked;
		for(auto* p : {&unlocked, &locked})
		{
			allOutputs(*p);
			onDrum(*p);
			p->editSequencer([&](Sequencer& _s)
			{
				_s.pattern(0).setTrig(0, 0, true);
				_s.pattern(0).setTrig(0, 4, true);
				if(p == &locked)
					_s.pattern(0).setLock(0, 4, indexOf(*p, "t01_m17"), 5);
			});
		}
		require(whole(plain(find(unlocked, "t01_m17"))) == all[16], "Machine 17 not at the voice's default");
		const auto a = playSequenced(unlocked, Length, {}), b = playSequenced(locked, Length, {});
		const int step4 = stepSample(1.0), from = firstDifference(a, b);
		require(from >= step4 && from < step4 + 200, "a Lock of Machine 17 changed the sound from " + std::to_string(from));
		std::printf("  a Lock of Machine 17 on an OD DRUM Track changes its Hit from sample %d (step 4 at %d)\n", from, step4);

		// A Kit from before Machine 9-24: they take the machine's defaults, and the Kit loaded is its Slot's
		{
			Processor saver;
			onDrum(saver);
			require(saver.saveKit(42), "Slot 43 saved");
			KitBank bank(Processor::dataFolder().getChildFile("Kits"));
			auto kit = bank.read(42);
			require(kit != nullptr, "Slot 43 read");
			for(int t = 0; t < Tracks; ++t)
				for(int i = 9; i <= MachineParameters; ++i)
					kit->removeAttribute(juce::String::formatted("t%02d_m%d", t + 1, i));
			require(bank.write(42, *kit), "Slot 43 written");
			Processor p;
			set(find(p, "t01_m17"), 99);
			set(find(p, "t02_m9"), 99);
			require(p.loadKit(42), "the old Kit not loaded");
			for(int i = 9; i <= MachineParameters; ++i)
				require(whole(plain(find(p, juce::String::formatted("t01_m%d", i)))) == all[static_cast<size_t>(i - 1)],
					"an old Kit's Machine " + std::to_string(i) + " not the voice's default");
			require(whole(plain(find(p, "t02_m9"))) == 0, "an old Kit's Machine 9 of a machine of 8 not 0");
			require(!p.kitDiffers(), "an old Kit loaded differs from its Slot");
			require(bank.clear(42), "Slot 43 cleared");
		}
		// A state from before them, the same
		{
			Processor saver;
			onDrum(saver);
			juce::MemoryBlock state;
			saver.getStateInformation(state);
			auto xml = Processor::readState(state.getData(), static_cast<int>(state.getSize()));
			for(int i = 9; i <= MachineParameters; ++i)
				xml->removeAttribute(juce::String::formatted("t01_m%d", i));
			juce::AudioProcessor::copyXmlToBinary(*xml, state);
			Processor p;
			set(find(p, "t01_m17"), 99);
			p.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
			require(whole(plain(find(p, "t01_m17"))) == all[16], "an old state's Machine 17 not the voice's default");
		}
		std::printf("PASS: Machine 9-24: a Lock on Machine 17 is heard; an old Kit and an old state load the machine's"
			" defaults there\n");
	}
	// ---- a Track's sample ----

	juce::File writeWav(const juce::File& _file)
	{
		_file.deleteFile();
		juce::AudioBuffer<float> wave(1, 13230);	// 0.3 s, a decaying 440 Hz
		for(int n = 0; n < wave.getNumSamples(); ++n)
			wave.setSample(0, n, 0.8f * std::exp(-n / 3000.0f) * std::sin(2 * 3.14159265f * 440 * n / 44100.0f));
		juce::WavAudioFormat wav;
		std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(new juce::FileOutputStream(_file), 44100, 1, 16,
			{}, 0));
		require(writer != nullptr && writer->writeFromAudioSampleBuffer(wave, 0, wave.getNumSamples()), "the WAV written");
		return _file;
	}

	float peakOf(const Take& _take, const int _channel)
	{
		float peak = 0;
		for(const float x : _take[static_cast<size_t>(_channel)])
			peak = std::max(peak, std::abs(x));
		return peak;
	}

	void samples(const juce::File& _wav)
	{
		Processor processor;
		allOutputs(processor);
		set(find(processor, "t03_machine"), processor.samplerChoice());
		const std::vector<Hit> hits{{1000, 2, 100}, {30000, 2, 60}};
		constexpr int Length = 469 * Block;	// the takes are whole blocks
		require(peakOf(play(processor, hits, Length), 0) < Engine::Silence, "the Sampler with no sample sounds");

		juce::String error;
		const auto text = _wav.getSiblingFile("notAudio.txt");
		text.replaceWithText("no audio");
		require(!processor.loadSample(2, text, error) && error.isNotEmpty(), "a text file loaded");
		require(processor.loadSample(2, _wav, error), "the WAV refused: " + error.toStdString());
		require(processor.sampleName(2) == _wav.getFileName(), "the sample's name");
		const auto loaded = play(processor, hits, Length);
		require(peakOf(loaded, 0) > 0.1f, "the sample does not sound");

		// The same samples in another instance given the state, and silence again cleared
		juce::MemoryBlock state;
		processor.getStateInformation(state);
		Processor other;
		allOutputs(other);
		other.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
		require(other.sampleName(2) == _wav.getFileName(), "the sample's name in the state");
		require(same(play(other, hits, Length), loaded), "the sample restored does not play the same");
		processor.clearSample(2);
		require(processor.sampleName(2).isEmpty() && peakOf(play(processor, hits, Length), 0) < Engine::Silence,
			"a sample cleared sounds");
		text.deleteFile();
		std::printf("PASS: a WAV loaded plays on the Sampler, peak %.2f; the state keeps it; cleared, silence\n",
			peakOf(loaded, 0));
	}

	// ---- the samples' Library ----

	// A sound of _frames at _rate on _channels, in _format's file
	void writeAudio(juce::AudioFormat& _format, const juce::File& _file, const double _rate, const int _channels,
		const int _frames, const float _hz)
	{
		_file.deleteFile();
		juce::AudioBuffer<float> wave(_channels, _frames);
		for(int c = 0; c < _channels; ++c)
			for(int n = 0; n < _frames; ++n)
				wave.setSample(c, n, 0.7f * std::exp(-n / 4000.0f) * std::sin(2 * 3.14159265f * _hz * n / static_cast<float>(_rate)));
		std::unique_ptr<juce::AudioFormatWriter> writer(_format.createWriterFor(new juce::FileOutputStream(_file), _rate,
			static_cast<unsigned>(_channels), 16, {}, 0));
		require(writer != nullptr && writer->writeFromAudioSampleBuffer(wave, 0, _frames), "the sound written");
	}

	// Until the Library's scan is taken, at most 20 s
	void waitForScan(SampleLibrary& _library)
	{
		const auto until = juce::Time::getMillisecondCounter() + 20000;
		while(!_library.update())
		{
			require(juce::Time::getMillisecondCounter() < until, "the scan never ended");
			juce::Thread::sleep(5);
		}
	}

	// A folder of three sounds, a corrupt WAV and a text file: the scan finds the three as they are, by name and type;
	// a second Library on the data folder lists the folder; a sample loaded by fingerprint plays as its file loaded
	void sampleLibrary()
	{
		const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("edDrumsLibrarySamples");
		folder.deleteRecursively();
		require(folder.createDirectory().wasOk(), "the samples' folder");
		juce::WavAudioFormat wav;
		juce::AiffAudioFormat aiff;
		writeAudio(wav, folder.getChildFile("kick_01.wav"), 44100, 1, 13230, 60);
		writeAudio(aiff, folder.getChildFile("SD-snare.aif"), 48000, 2, 9600, 200);
		require(folder.getChildFile("sub").createDirectory().wasOk(), "a sub-folder");
		writeAudio(wav, folder.getChildFile("sub").getChildFile("hh closed.wav"), 22050, 1, 2205, 3000);
		folder.getChildFile("broken.wav").replaceWithText("RIFF\x10\x00\x00\x00WAVEjunk, not a header");
		folder.getChildFile("notes.txt").replaceWithText("no audio");

		struct Expected
		{
			const char* name;
			int64_t length;
			double rate;
			int channels;
			SampleLibrary::Type type;
		};
		const Expected expected[]{{"kick_01.wav", 13230, 44100, 1, SampleLibrary::Type::Kick},
			{"SD-snare.aif", 9600, 48000, 2, SampleLibrary::Type::Snare},
			{"hh closed.wav", 2205, 22050, 1, SampleLibrary::Type::Hat}};

		Processor processor;
		auto& library = processor.sampleLibrary();
		require(library.sources().empty() && library.entries().empty(), "a new data folder's Library is empty");
		require(library.addSource(SampleLibrary::Kind::Linked, folder), "the folder not added");
		waitForScan(library);
		const auto& entries = library.entries();
		require(entries.size() == 3, "the scan found " + std::to_string(entries.size()) + " samples, not 3");
		for(const auto& x : expected)
		{
			const auto it = std::find_if(entries.begin(), entries.end(), [&](const auto& _e) { return _e.name == x.name; });
			require(it != entries.end(), std::string(x.name) + " not found");
			require(it->length == x.length && it->rate == x.rate && it->channels == x.channels && it->type == x.type
				&& it->fingerprint.length() == 16, std::string(x.name) + ": " + std::to_string(it->length) + " frames at "
				+ std::to_string(it->rate) + " Hz, " + std::to_string(it->channels) + " channels, "
				+ SampleLibrary::typeName(it->type));
		}

		// Another instance's Library, the same data folder: the same folders, and the same samples once scanned
		{
			SampleLibrary other(Processor::dataFolder());
			require(other.sources().size() == 1 && other.sources()[0].folder == library.sources()[0].folder,
				"a second Library does not see the folder");
			other.scanOnce();
			waitForScan(other);
			require(other.entries().size() == 3 && other.entries()[0].fingerprint == entries[0].fingerprint,
				"a second Library does not find the same samples");
		}

		// By fingerprint on Track 3, as the file loaded on another Processor's Track 3
		const auto* kick = library.find(entries[0].fingerprint);
		require(kick && kick->name == "kick_01.wav", "find by fingerprint");
		allOutputs(processor);
		set(find(processor, "t03_machine"), processor.samplerChoice());
		juce::String error;
		require(!processor.loadLibrarySample(2, "0123456789abcdef", error) && error.isNotEmpty(),
			"an unknown fingerprint loaded");
		require(processor.loadLibrarySample(2, kick->fingerprint, error), "by fingerprint: " + error.toStdString());
		require(processor.sampleFingerprint(2) == kick->fingerprint, "the Track's fingerprint");
		Processor byFile;
		allOutputs(byFile);
		set(find(byFile, "t03_machine"), byFile.samplerChoice());
		require(byFile.loadSample(2, kick->file, error), "the file refused: " + error.toStdString());
		require(byFile.sampleFingerprint(2) == kick->fingerprint, "a file loaded has its fingerprint");
		const std::vector<Hit> hits{{1000, 2, 100}};
		constexpr int Length = 100 * Block;
		const auto played = play(processor, hits, Length);
		require(peakOf(played, 0) > 0.1f && same(played, play(byFile, hits, Length)),
			"by fingerprint does not play as the file");

		// The state and a Kit keep the fingerprint and the name beside the copy
		juce::MemoryBlock state;
		processor.getStateInformation(state);
		Processor restored;
		restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
		require(restored.sampleFingerprint(2) == kick->fingerprint, "the state's fingerprint");
		require(processor.saveKit(30), "the Kit not saved");
		const auto kit = juce::File(Processor::dataFolder().getChildFile("Kits").getChildFile("31.edkit"));
		processor.clearSample(2);
		require(processor.sampleFingerprint(2).isEmpty(), "a cleared sample keeps its fingerprint");
		require(processor.loadKit(30) && processor.sampleFingerprint(2) == kick->fingerprint
			&& processor.sampleName(2) == "kick_01.wav", "the Kit's fingerprint");
		require(kit.existsAsFile(), "the Kit's file");

		// The audition plays once on Main, the Tracks silent
		Processor audition;
		allOutputs(audition);
		require(audition.auditionSample(kick->file, error), "the audition: " + error.toStdString());
		const auto heard = play(audition, {}, 2 * Length);
		require(peakOf(heard, 0) > 0.1f && peakOf(heard, 1) > 0.1f, "the audition is silent");
		require(peakOf(play(audition, {}, Length), 0) < Engine::Silence, "the audition plays more than once");

		// IN THIS SET: the state opened where no source of the Locker holds its sample shows it there; KEEP IN LOCKER
		// writes it into the IMPORTED source FROM SETS, whose file the Track then names, and it leaves IN THIS SET
		const auto kickPrint = kick->fingerprint;
		require(library.removeSource(library.indexOf(folder)), "the folder not removed");
		{
			Processor elsewhere;
			elsewhere.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
			auto& lib = elsewhere.sampleLibrary();
			lib.scanOnce();
			waitForScan(lib);
			const auto set = elsewhere.setSamples();
			require(set.size() == 1 && set[0].fingerprint == kickPrint && set[0].name == "kick_01.wav"
				&& set[0].folder == SampleLibrary::SetFolder, "IN THIS SET holds " + std::to_string(set.size()));
			require(elsewhere.loadLibrarySample(5, kickPrint, error) && elsewhere.sampleFingerprint(5) == kickPrint,
				"a sample of the set loaded on another Track: " + error.toStdString());
			// What KEEP tells the host: the set changed outside its parameters, once it is kept and not before
			struct Dirty final : juce::AudioProcessorListener
			{
				int marked = 0;
				void audioProcessorParameterChanged(juce::AudioProcessor*, int, float) override {}
				void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails& _d) override
				{
					marked += _d.nonParameterStateChanged ? 1 : 0;
				}
			} dirty;
			elsewhere.addListener(&dirty);
			// A file where FROM SETS' folder goes: nothing written, the Tracks keep their sample, the set unmarked
			const auto fromSets = SampleLibrary::importedFolder(Processor::dataFolder()).getChildFile("From sets");
			require(fromSets.getParentDirectory().createDirectory().wasOk() && fromSets.replaceWithText("in the way"),
				"the file in FROM SETS' way");
			require(!elsewhere.keepInLocker(kickPrint, error) && error.isNotEmpty()
				&& elsewhere.sampleFingerprint(2) == kickPrint && elsewhere.sampleFingerprint(5) == kickPrint
				&& dirty.marked == 0, "KEEP with its folder blocked");
			require(fromSets.deleteFile(), "the file in the way not removed");
			require(elsewhere.keepInLocker(kickPrint, error), "KEEP: " + error.toStdString());
			elsewhere.removeListener(&dirty);
			require(dirty.marked == 1, "KEEP marked the set modified " + std::to_string(dirty.marked) + " times");
			waitForScan(lib);
			const auto ref = elsewhere.sampleFingerprint(2);
			const auto* kept = lib.find(ref);
			require(kept && ref != kickPrint && elsewhere.sampleFingerprint(5) == ref
				&& lib.sources()[static_cast<size_t>(kept->folder)].kind == SampleLibrary::Kind::Imported
				&& kept->file.isAChildOf(SampleLibrary::importedFolder(Processor::dataFolder()))
				&& kept->length == 13230 && elsewhere.setSamples().empty(), "KEEP did not make it an IMPORTED source's");
		}
		std::printf("PASS: the Library finds 3 of 5 files (lengths, rates, channels, types), a second Library the same "
			"folder; by fingerprint plays as the file; state and Kit keep it; the audition peaks %.2f once; a set's "
			"sample IN THIS SET, kept in the Locker\n", peakOf(heard, 0));
		folder.deleteRecursively();
	}

	// ---- the Kits ----

	// What a Kit's load tells the host: edits, none expected, and the re-reads
	struct KitListener final : juce::AudioProcessorListener
	{
		int edits = 0, rereads = 0;
		void audioProcessorParameterChanged(juce::AudioProcessor*, int, float) override { ++edits; }
		void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails& _d) override
		{
			rereads += _d.programChanged ? 1 : 0;
		}
		void audioProcessorParameterChangeGestureBegin(juce::AudioProcessor*, int) override { ++edits; }
		void audioProcessorParameterChangeGestureEnd(juce::AudioProcessor*, int) override { ++edits; }
	};

	// GEARMULATOR_DATA_ROOT names a folder of the test's own, empty at its start
	void kits(const juce::File& _wav)
	{
		// A sound, a sample on Track 3 and a tempo, saved in Slot 5
		Processor one;
		allOutputs(one);
		require(one.kitSlot(4).empty() && one.currentKit() == -1 && !one.kitDiffers(), "a new Bank's Slot 5");
		applyValues(one, sound(true));
		set(find(one, "t03_machine"), one.samplerChoice());
		set(find(one, "tempo"), 100);
		juce::String error;
		require(one.loadSample(2, _wav, error), "the WAV refused: " + error.toStdString());
		// A block first: the Sampler's Machine 1-8 take its defaults on the audio thread, as a host's change of machine
		one.prepareToPlay(SampleRate, Block);
		block(one);
		require(one.saveKit(4), "the Kit not saved");
		require(one.currentKit() == 4 && !one.kitDiffers() && one.kitSlot(4).name == "Kit 05", "the Kit saved in Slot 5");
		set(find(one, "t01_level"), 3);
		require(one.kitDiffers(), "a change after the save is not seen");
		set(find(one, "t01_level"), 100);
		require(!one.kitDiffers(), "the value saved given back, the sound still differs");
		const auto take = play(one);

		// A fresh instance loads it: every value but the Global ones, the sample, the same samples; no edit told
		Processor other;
		allOutputs(other);
		KitListener heard;
		other.addListener(&heard);
		require(other.loadKit(4), "the Kit not loaded");
		other.removeListener(&heard);
		require(heard.edits == 0 && heard.rereads > 0, "a Kit loaded: " + std::to_string(heard.edits) + " edits told, "
			+ std::to_string(heard.rereads) + " re-reads");
		auto expected = valuesOf(one);
		expected["tempo"] = plain(find(other, "tempo"));
		require(plain(find(other, "tempo")) == 120, "the Kit carried the tempo");
		for(const auto& [id, v] : valuesOf(other))
			require(v == expected.at(id), "the Kit's " + id.toStdString() + ": " + std::to_string(v) + " for "
				+ std::to_string(expected.at(id)));
		require(other.sampleName(2) == _wav.getFileName() && other.currentKit() == 4 && !other.kitDiffers(),
			"the Kit's sample");
		// The synced LFOs follow the tempo, which the Kit does not keep
		set(find(other, "tempo"), 100);
		requireSame(take, play(other), "a Kit loaded does not play the same: ");
		std::printf("  a Kit saved and loaded in another instance: the same samples, its sample, no edit told\n");

		// Two instances on the same Bank each write a Slot: both stay
		Processor a, b;
		set(find(a, "t01_level"), 11);
		set(find(b, "t01_level"), 22);
		require(a.saveKit(9) && b.saveKit(10), "two Slots written");
		a.followKits();
		require(!a.kitSlot(10).empty() && !b.kitSlot(9).empty(), "an instance does not see the other's Slot");
		require(a.renameKit(9, "Dusty"), "a rename");
		require(a.clearKit(4) && a.kitSlot(4).empty(), "a Slot cleared");
		{
			Processor c;
			require(c.kitSlot(9).name == "Dusty" && c.kitSlot(10).name == "Kit 11" && c.kitSlot(4).empty(), "the Bank read");
			require(c.loadKit(9) && whole(plain(find(c, "t01_level"))) == 11, "Slot 10's Kit");
			require(c.loadKit(10) && whole(plain(find(c, "t01_level"))) == 22, "Slot 11's Kit");
		}
		// The Kit played rewritten by another instance: the sound differs from its Slot
		juce::Thread::sleep(20);
		require(b.saveKit(9), "Slot 10 rewritten");
		a.followKits();
		require(a.currentKit() == 9 && a.kitDiffers(), "a Slot rewritten by another instance");
		std::printf("  two instances each write a Slot of the Bank and keep the other's\n");

		// A Kit without an ID, and with one not ours: the parameter it lacks takes its default
		{
			KitBank bank(Processor::dataFolder().getChildFile("Kits"));
			auto kit = bank.read(10);
			require(kit != nullptr && kit->hasAttribute("t01_level"), "Slot 11's file");
			kit->removeAttribute("t01_level");
			kit->setAttribute("not_ours", 5);
			require(bank.write(12, *kit), "Slot 13 written");
			Processor d;
			set(find(d, "t01_level"), 5);
			require(d.loadKit(12), "the Kit without t01_level");
			auto& level = find(d, "t01_level");
			require(level.getValue() == level.getDefaultValue() && !d.kitDiffers(), "a missing ID: not its default");
		}
		std::printf("PASS: Kits: saved and loaded play the same, two instances keep each other's Slots, a missing ID "
			"takes its default\n");
	}

	// ---- the sequencer ----

	constexpr double SamplesPerQuarter = SampleRate / 2;	// 120 BPM

	// A step's sample at 120 BPM, as the sequencer places it: the first at or after its time
	int stepSample(const double _quarters)
	{
		return static_cast<int>(std::ceil(_quarters * SamplesPerQuarter - 1e-3));
	}

	// The processor in blocks, its playhead playing at 120 BPM from 0; _atBlock, if given, runs before each block
	Take playSequenced(Processor& _processor, const int _samples, const std::function<void(int)>& _atBlock = {})
	{
		Transport transport;
		_processor.setPlayHead(&transport);
		_processor.prepareToPlay(SampleRate, Block);
		Take take(Channels, std::vector<float>(static_cast<size_t>(_samples)));
		juce::AudioBuffer<float> buffer(Channels, Block);
		juce::MidiBuffer midi;
		for(int at = 0; at < _samples; at += Block)
		{
			if(_atBlock)
				_atBlock(at);
			transport.quarters = at / SamplesPerQuarter;
			buffer.clear();
			_processor.processBlock(buffer, midi);
			for(int c = 0; c < Channels; ++c)
				std::copy_n(buffer.getReadPointer(c), Block, take[static_cast<size_t>(c)].begin() + at);
		}
		_processor.setPlayHead(nullptr);
		return take;
	}

	// An engine told what the processor tells its engine, the calls of _events at their samples
	using Calls = std::multimap<int, std::function<void(Engine&)>>;

	Take playEngine(const Library& _library, Processor& _processor, const Calls& _events, const int _samples)
	{
		auto e = engineOf(_library, _processor, valuesOf(_processor));
		Take take(Channels, std::vector<float>(static_cast<size_t>(_samples)));
		std::array<float*, Channels> out{};
		auto next = _events.begin();
		for(int at = 0; at < _samples; at += Block)
		{
			e->setTempo(120);
			e->setPosition(at / SamplesPerQuarter);
			for(size_t c = 0; c < out.size(); ++c)
				out[c] = take[c].data() + at;
			int done = at;
			for(; next != _events.end() && next->first < at + Block; ++next)
			{
				e->render(out.data(), next->first - done);
				for(auto*& o : out)
					o += next->first - done;
				done = next->first;
				next->second(*e);
			}
			e->render(out.data(), at + Block - done);
		}
		return take;
	}

	std::function<void(Engine&)> hitOf(const int _track, const int _velocity = 100)
	{
		return [_track, _velocity](Engine& _e) { _e.noteOn(_track, _velocity); };
	}

	// The first sample from which two takes differ, on any channel; their length if none
	int firstDifference(const Take& _a, const Take& _b)
	{
		int first = static_cast<int>(_a[0].size());
		for(size_t c = 0; c < _a.size(); ++c)
			for(int n = 0; n < first; ++n)
				if(_a[c][static_cast<size_t>(n)] != _b[c][static_cast<size_t>(n)])
				{
					first = n;
					break;
				}
		return first;
	}

	uint16_t indexOf(Processor& _processor, const juce::String& _id)
	{
		return static_cast<uint16_t>(find(_processor, _id).getParameterIndex());
	}

	// Pattern 1: Track 1 on steps 0, 4 and 9, Track 2 on step 6 with an accent-less velocity of 60
	void basicPattern(Sequencer& _s)
	{
		auto& p = _s.pattern(0);
		p.setTrig(0, 0, true);
		p.setTrig(0, 4, true);
		p.setTrig(0, 9, true);
		p.setTrig(1, 6, true, 60);
	}

	// Tracks 1-3 on analog machines (ticket 28), Track 4 on the Rytm's tom, a parameter set and a Lock of Machine 2 on step 4: the state saved and
	// restored in another instance plays the same samples, the Lock's Hit apart from what it plays without the Lock
	void analogState()
	{
		constexpr int Length = 846 * Block;
		const auto prepare = [](Processor& _p, const bool _lock)
		{
			allOutputs(_p);
			const int first = _p.analogChoice();
			set(find(_p, "t01_machine"), first + static_cast<int>(analog::Machine::KickHard));
			set(find(_p, "t02_machine"), first + static_cast<int>(analog::Machine::SnareClassic));
			set(find(_p, "t03_machine"), first + static_cast<int>(analog::Machine::ClosedClassic));
			set(find(_p, "t04_machine"), _p.rytmChoice() + static_cast<int>(rytm::Machine::Tom));
			_p.prepareToPlay(SampleRate, Block);
			block(_p);	// a block takes the machines, whose defaults Machine 1-8 take before what follows
			set(find(_p, "t02_m2"), 30);
			_p.editSequencer([&](Sequencer& _s)
			{
				basicPattern(_s);
				_s.pattern(0).setTrig(2, 2, true);
				_s.pattern(0).setTrig(3, 6, true);
				if(_lock)
					_s.pattern(0).setLock(0, 4, indexOf(_p, "t01_m2"), 110);
			});
		};
		auto one = std::make_unique<Processor>();
		prepare(*one, true);
		require(one->machines()[static_cast<size_t>(plain(find(*one, "t01_machine")))].family == Family::SyntaktAnalog,
			"Track 1 not on an analog machine");
		juce::MemoryBlock saved;
		one->getStateInformation(saved);
		const auto values = valuesOf(*one);
		const auto take = playSequenced(*one, Length);
		auto other = std::make_unique<Processor>();
		allOutputs(*other);
		other->setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
		other->prepareToPlay(SampleRate, Block);
		block(*other);	// a block in, as the first took its machines
		const auto restored = valuesOf(*other);
		for(const auto& [id, v] : values)
			// to a float's rounding: a value of a wide range (an LFO's rate) comes back within its normalised float
			require(restored.count(id) && std::abs(restored.at(id) - v) <= 1e-5 * std::max(1.0, std::abs(v)),
				"an analog Track's parameter in the state: " + id.toStdString());
		require(other->sequencer().pattern(0).step(0, 4).lockCount == 1, "an analog Track's Lock in the state");
		requireSame(take, playSequenced(*other, Length), "an analog kit restored: ");
		require(peakOf(take, 0) > 0.05f, "the analog kit is silent");
		auto unlocked = std::make_unique<Processor>();
		prepare(*unlocked, false);
		const int differs = firstDifference(take, playSequenced(*unlocked, Length));
		require(differs >= stepSample(1.0) && differs < stepSample(9 * 0.25), "the Lock does not play on its step");
		std::printf("  analog Tracks and a Lock: the state restored plays the same; the Lock from sample %d (step 4 at %d)\n",
			differs, stepSample(1.0));
	}

	void sequencer(const Library& _library)
	{
		constexpr int Length = 846 * Block;	// a pattern of 16 steps, 88200 samples, and its start again
		Processor processor;
		allOutputs(processor);
		require(processor.playingStep(0) == -1, "a step before the transport plays");
		processor.editSequencer(basicPattern);
		require(processor.sequencer().pattern(0).step(0, 4).trig, "the edit in the message thread's copy");
		int stepSeen = -1;
		const auto take = playSequenced(processor, Length, [&](const int _at)
		{
			if(_at == 128 * 200)	// 25600: step 4 holds 22050 to 27562
				stepSeen = processor.playingStep(0);
		});
		Calls calls;
		for(const int n : {0, 4, 9, 16})
			calls.emplace(stepSample(n * 0.25), hitOf(0));
		calls.emplace(stepSample(6 * 0.25), hitOf(1, 60));	// the second pass's steps 4 and 6 fall past the take
		requireSame(take, playEngine(_library, processor, calls, Length), "the sequencer's Hits: ");
		require(peakOf(take, 0) > 0.05f, "the sequenced take is silent");
		require(stepSeen == 4, "the step playing, " + std::to_string(stepSeen));
		processor.prepareToPlay(SampleRate, Block);
		block(processor);
		require(processor.playingStep(0) == -1, "a step while the transport stands still");
		std::printf("  Hits at steps 0, 4, 9 and 6 at their samples, at 120 BPM: the engine's, sample for sample\n");

		// Locks on step 4: Machine 1 for the Hit, Level until the next Hit, which takes the host's 100 again
		Processor locked;
		allOutputs(locked);
		locked.editSequencer(basicPattern);
		const auto m1 = indexOf(locked, "t01_m1"), level = indexOf(locked, "t01_level");
		locked.editSequencer([&](Sequencer& _s)
		{
			_s.pattern(0).setLock(0, 4, m1, 10);
			_s.pattern(0).setLock(0, 4, level, 40);
			_s.pattern(0).setLock(0, 4, indexOf(locked, "t02_level"), 0);		// another Track's: nothing
		});
		const auto lockTake = playSequenced(locked, Length);
		Calls lockCalls = calls;
		const int step4 = stepSample(1.0), step9 = stepSample(9 * 0.25);
		lockCalls.erase(step4);
		lockCalls.emplace(step4, [](Engine& _e) { _e.lock(0, 0, 10); _e.setLevel(0, 40); _e.noteOn(0, 100); });
		lockCalls.erase(step9);
		lockCalls.emplace(step9, [](Engine& _e) { _e.setLevel(0, 100); _e.noteOn(0, 100); });
		requireSame(lockTake, playEngine(_library, locked, lockCalls, Length), "the Locks: ");
		const int from = firstDifference(lockTake, take);
		require(from >= step4 && from < step4 + 200, "a Lock changed the sound from " + std::to_string(from));
		// From the second pass on, the sound is the unlocked one's again: its first Hit's tail has gone
		const int second = stepSample(4.0) + 5000;
		bool sameAfter = true;
		for(size_t c = 0; c < Channels; ++c)
			sameAfter &= std::equal(lockTake[c].begin() + second, lockTake[c].end(), take[c].begin() + second);
		require(sameAfter, "a Lock outlives its step");
		std::printf("  Locks of step 4, Machine 1 and Level: the sound changes from sample %d (step 4 at %d), the engine's\n",
			from, step4);

		// A ratchet of 4 on step 0 with Machine 1 and Level locked: each of its Hits plays them, step 4 the host's
		{
			Processor ratchet;
			allOutputs(ratchet);
			ratchet.editSequencer([&](Sequencer& _s)
			{
				auto& p = _s.pattern(0);
				p.setTrig(0, 0, true);
				p.step(0, 0).ratchets = 4;
				p.setLock(0, 0, m1, 10);
				p.setLock(0, 0, level, 40);
				p.setTrig(0, 4, true);
			});
			constexpr int Short = 300 * Block;
			const auto ratchetTake = playSequenced(ratchet, Short);
			Calls ratchetCalls;
			ratchetCalls.emplace(0, [](Engine& _e) { _e.lock(0, 0, 10); _e.setLevel(0, 40); _e.noteOn(0, 100); });
			for(int k = 1; k < 4; ++k)
				ratchetCalls.emplace(stepSample(k * 0.0625), [](Engine& _e) { _e.lock(0, 0, 10); _e.noteOn(0, 100); });
			ratchetCalls.emplace(step4, [](Engine& _e) { _e.setLevel(0, 100); _e.noteOn(0, 100); });
			requireSame(ratchetTake, playEngine(_library, ratchet, ratchetCalls, Short), "a ratchet's Locks: ");
			std::printf("  a ratchet of 4 plays the step's Locks on each of its Hits, the engine's\n");
		}

		// A Lock held when a state loads does not outlive it: the host's value again
		{
			Processor held;
			allOutputs(held);
			held.editSequencer([&](Sequencer& _s) { _s.pattern(0).setLock(0, 0, m1, 10); });
			playSequenced(held, 4 * Block);
			const auto& m1Parameter = find(held, "t01_m1");
			const int host = juce::roundToInt(m1Parameter.convertFrom0to1(m1Parameter.getValue()));
			require(host != 10, "Machine 1's default is the Lock's value");
			require(held.engine()->parameter(0, 0) == 10, "the lock without note holds Machine 1");
			juce::MemoryBlock state;
			held.getStateInformation(state);
			held.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
			block(held);
			require(held.engine()->parameter(0, 0) == host, "a Lock survived the state: Machine 1 at "
				+ std::to_string(held.engine()->parameter(0, 0)) + ", the host's " + std::to_string(host));
			std::printf("  a Lock held when a state loads gives the host's value back\n");
		}

		// Pattern 2 asked for at step 5: pattern 1 ends first
		Processor switched;
		allOutputs(switched);
		switched.editSequencer([](Sequencer& _s)
		{
			_s.pattern(0).setTrig(0, 0, true);
			_s.pattern(1).setTrig(1, 0, true);
			_s.pattern(1).setTrig(1, 8, true);
		});
		constexpr int Two = 1457 * Block;	// two patterns and the start of a third
		std::vector<int> patterns;
		const auto switchTake = playSequenced(switched, Two, [&](const int _at)
		{
			if(_at == 128 * 230)
				set(find(switched, "pattern"), 2);
			if(_at > 0 && (_at / Block) % 100 == 0)
				patterns.push_back(switched.currentPattern());
		});
		Calls switchCalls;
		switchCalls.emplace(0, hitOf(0));
		switchCalls.emplace(stepSample(4.0), hitOf(1));
		switchCalls.emplace(stepSample(6.0), hitOf(1));
		switchCalls.emplace(stepSample(8.0), hitOf(1));
		requireSame(switchTake, playEngine(_library, switched, switchCalls, Two), "Pattern 2: ");
		// Blocks of 100: 12800 to 76800 still pattern 1, 89600 on pattern 2
		require(patterns.size() > 7 && patterns[5] == 0 && patterns[6] == 1, "the pattern playing at the blocks");
		std::printf("  Pattern 2 asked for at step 5: pattern 1 to its end, then pattern 2's Hits, the engine's\n");

		// The state: patterns with their Locks and a sample, in another instance, play the same
		set(find(locked, "t03_machine"), locked.samplerChoice());
		block(locked);	// a block takes the machine, whose defaults Machine 1-8 take before the state is saved
		juce::String error;
		const auto wav = writeWav(juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("edDrumsSeq.wav"));
		require(locked.loadSample(2, wav, error), "the WAV");
		locked.editSequencer([](Sequencer& _s)
		{
			_s.pattern(0).setTrig(2, 2, true);
			_s.pattern(0).step(2, 2).timing = 5;
			_s.pattern(0).trackLength[2] = 7;
			_s.pattern(9).setTrig(5, 3, true);
			_s.pattern(9).swing = 60;
		});
		juce::MemoryBlock state;
		locked.getStateInformation(state);
		Processor restored;
		allOutputs(restored);
		restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
		const auto& a = locked.sequencer().pattern(9);
		const auto& b = restored.sequencer().pattern(9);
		require(b.swing == 60 && b.step(5, 3).trig && !b.step(5, 4).trig && a.trackLength == b.trackLength, "pattern 10");
		require(restored.sequencer().pattern(0).locks(0, 4)[1].value == 40.0f
			&& restored.sequencer().pattern(0).step(0, 4).lockCount == 3, "the Locks of the state");
		const auto before = playSequenced(locked, Length);
		require(peakOf(before, 0) > 0.05f, "the state's take is silent");
		requireSame(before, playSequenced(restored, Length), "patterns and sample restored: ");
		wav.deleteFile();
		std::printf("  the state: %zu bytes; patterns, Locks and the sample restored play the same\n", state.getSize());
		std::printf("PASS: the sequencer plays the host's transport\n");
	}

	// The processor in host blocks of _block, its playhead at 120 BPM from 0, with _midi's notes at their samples
	Take playWithNotes(Processor& _processor, const int _samples, const int _block,
		const std::multimap<int, juce::MidiMessage>& _midi)
	{
		Transport transport;
		_processor.setPlayHead(&transport);
		_processor.prepareToPlay(SampleRate, _block);
		Take take(Channels, std::vector<float>(static_cast<size_t>(_samples)));
		juce::AudioBuffer<float> buffer(Channels, _block);
		juce::MidiBuffer midi;
		for(int at = 0; at < _samples; at += _block)
		{
			const int n = std::min(_block, _samples - at);
			transport.quarters = at / SamplesPerQuarter;
			buffer.setSize(Channels, n, false, false, true);
			buffer.clear();
			midi.clear();
			for(auto it = _midi.lower_bound(at); it != _midi.end() && it->first < at + n; ++it)
				midi.addEvent(it->second, it->first - at);
			_processor.processBlock(buffer, midi);
			for(int c = 0; c < Channels; ++c)
				std::copy_n(buffer.getReadPointer(c), n, take[static_cast<size_t>(c)].begin() + at);
		}
		_processor.setPlayHead(nullptr);
		return take;
	}

	// Ticket 17's last functions, and spec criterion 6 through the processor: a step's note and gate as the engine's
	// setNote and noteOff; the criterion's pattern rendered twice and in other host blocks the same; LIVE recording a
	// note and a gesture; mutes, solos, fill, seed and the chain kept by the state; a pattern's MIDI file read back
	void sequencerFunctions(const Library& _library)
	{
		// A note and a gate on step 0: the Track's Note for that Hit, its end half a step later; step 4 the host's Note
		{
			Processor noted;
			allOutputs(noted);
			const int host = juce::roundToInt(plain(find(noted, "t01_note")));
			noted.editSequencer([](Sequencer& _s)
			{
				auto& p = _s.pattern(0);
				p.setTrig(0, 0, true);
				p.step(0, 0).note = 72;
				p.step(0, 0).gate = 4;
				p.setTrig(0, 4, true);
			});
			constexpr int Short = 300 * Block;
			const auto take = playSequenced(noted, Short);
			Calls calls;
			calls.emplace(0, [](Engine& _e) { _e.setNote(0, 72); _e.noteOn(0, 100); });
			calls.emplace(stepSample(0.125), [](Engine& _e) { _e.noteOff(0); });
			calls.emplace(stepSample(1.0), [host](Engine& _e) { _e.setNote(0, host); _e.noteOn(0, 100); });
			requireSame(take, playEngine(_library, noted, calls, Short), "a step's note and gate: ");
			std::printf("  a step's note 72 and gate of half a step: setNote and noteOff at their samples, the engine's\n");
		}

		// Criterion 6: Track lengths and scales, conditions on a seed, micro-timing, ratchets, slide, Locks and a lock
		// without note, a pattern asked for during the first pass
		const auto criterion = [](Processor& _p)
		{
			const auto m1 = indexOf(_p, "t01_m1"), level = indexOf(_p, "t02_level"), freq = indexOf(_p, "t03_flt_freq"),
				m4 = indexOf(_p, "t04_m1");
			_p.editSequencer([&](Sequencer& _s)
			{
				auto& p = _s.pattern(0);
				p.swing = 60;
				for(int i = 0; i < 16; i += 4)
					p.setTrig(0, i, true);
				p.step(0, 4).timing = 6;
				p.setLock(0, 4, m1, 20);
				p.step(0, 8).ratchets = 3;
				p.step(0, 12).condition = {ConditionKind::Probability, 50, 0};
				p.trackLength[1] = 12;
				p.trackScale[1] = StepScale::Two;
				for(const int i : {0, 3, 7})
					p.setTrig(1, i, true, 90);
				p.step(1, 3).condition = {ConditionKind::Ratio, 1, 2};
				p.step(1, 7).timing = -5;
				p.setLock(1, 7, level, 50);
				p.trackLength[2] = 8;
				p.trackScale[2] = StepScale::Half;
				p.setTrig(2, 0, true, 80);
				p.setTrig(2, 2, true, 80);
				p.step(2, 0).slide = true;
				p.setLock(2, 0, freq, 20);
				p.setLock(2, 2, freq, 110);
				p.setLock(3, 6, m4, 5);		// a lock without note
				p.setTrig(3, 8, true);
				_s.pattern(1).setTrig(4, 0, true, 60);
				_s.setSeed(0x5eed);
			});
		};
		constexpr int Long = 1800 * Block;		// pattern 1 twice, asked for pattern 2 at 5.3 quarters, which starts at 8
		const auto play = [&](const int _block)
		{
			Processor p;
			allOutputs(p);
			criterion(p);
			Transport transport;
			p.setPlayHead(&transport);
			p.prepareToPlay(SampleRate, _block);
			Take take(Channels, std::vector<float>(static_cast<size_t>(Long)));
			juce::AudioBuffer<float> buffer(Channels, _block);
			juce::MidiBuffer midi;
			for(int at = 0; at < Long; at += _block)
			{
				const int n = std::min(_block, Long - at);
				if(at <= stepSample(5.3) && stepSample(5.3) < at + n)
					set(find(p, "pattern"), 2);
				transport.quarters = at / SamplesPerQuarter;
				buffer.setSize(Channels, n, false, false, true);
				buffer.clear();
				p.processBlock(buffer, midi);
				for(int c = 0; c < Channels; ++c)
					std::copy_n(buffer.getReadPointer(c), n, take[static_cast<size_t>(c)].begin() + at);
			}
			p.setPlayHead(nullptr);
			return take;
		};
		const auto first = play(Block);
		require(peakOf(first, 0) > 0.05f, "criterion 6's take is silent");
		requireSame(first, play(Block), "criterion 6 rendered twice: ");
		for(const int b : {37, 512})
			requireSame(first, play(b), "criterion 6 in host blocks of " + std::to_string(b) + ": ");
		std::printf("  criterion 6's pattern rendered twice, and in host blocks of 37 and 512: the same samples\n");

		// LIVE: a note at 1.02 quarters on Track 3 is step 4 with micro +1; Level of Track 2 moved in a gesture is a Lock
		// of the step playing; the host's automation, without a gesture, records nothing
		{
			Processor live;
			allOutputs(live);
			live.setLive(true);
			std::multimap<int, juce::MidiMessage> notes;
			notes.emplace(stepSample(1.02), juce::MidiMessage::noteOn(1, 36 + 2, static_cast<juce::uint8>(77)));
			playWithNotes(live, 200 * Block, Block, notes);
			auto& level = find(live, "t02_level");
			const int step = live.playingStep(1);
			level.beginChangeGesture();
			set(level, 33);
			level.endChangeGesture();
			set(find(live, "t03_level"), 44);
			// A host's controller: VST3 sets the parameter on the audio thread, here a thread of the test's, no gesture
			const int hostStep = live.playingStep(3);
			auto& pan = find(live, "t04_pan");
			std::thread([&] { set(pan, 20); }).join();
			live.takeLive();
			const auto& p = live.sequencer().pattern(0);
			require(p.step(2, 4).trig && p.step(2, 4).velocity == 77 && p.step(2, 4).timing == 1,
				"LIVE: the note at step 4, micro " + std::to_string(p.step(2, 4).timing));
			const auto* l = p.locks(1, step);
			require(step >= 0 && p.step(1, step).lockCount == 1 && l[0].parameter == indexOf(live, "t02_level")
				&& l[0].value == 33.0f, "LIVE: Level's Lock on step " + std::to_string(step));
			int locks = 0;
			for(int s = 0; s < SeqSteps; ++s)
				locks += p.step(2, s).lockCount;
			require(locks == 0, "LIVE: a change on the message thread without a gesture recorded a Lock");
			const auto* hl = hostStep >= 0 ? p.locks(3, hostStep) : nullptr;
			require(hl && p.step(3, hostStep).lockCount == 1 && hl[0].parameter == indexOf(live, "t04_pan")
				&& std::abs(hl[0].value - plain(pan)) < 1e-4, "LIVE: the host's Pan of Track 4 a Lock of step "
				+ std::to_string(hostStep));
			std::printf("  LIVE: a host's move without a gesture, on the audio thread, a Lock of step %d\n", hostStep);
			live.setLive(false);
			std::printf("  LIVE: a note is step 4 with micro +1 and velocity 77, a gesture a Lock of step %d\n", step);
		}

		// The state: mutes, solos, fill, seed and the chain playing
		{
			Processor kept;
			allOutputs(kept);
			kept.editSequencer([](Sequencer& _s)
			{
				_s.setMute(3, true);
				_s.setSolo(5, true);
				_s.setFill(true);
				_s.setSeed(77);
				_s.setChain({{2, 3, true}, {0, 1, false}});
				_s.pattern(2).setTrig(5, 0, true);
			});
			kept.requestChain();
			playSequenced(kept, 4 * Block);
			require(kept.chainSlot() == 0 && kept.fillPlaying(), "the chain plays from its first entry, in fill");
			juce::MemoryBlock state;
			kept.getStateInformation(state);
			Processor restored;
			allOutputs(restored);
			restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
			const auto& s = restored.sequencer();
			require(s.mute(3) && !s.mute(2) && s.solo(5) && s.fill() && s.seed() == 77 && s.chainLength() == 2
				&& s.chainEntry(0).pattern == 2 && s.chainEntry(0).repeats == 3 && s.chainEntry(0).fill
				&& s.chainEntry(1).pattern == 0 && !s.chainEntry(1).fill, "the sequencer's settings in the state");
			playSequenced(restored, 4 * Block);
			require(restored.chainSlot() == 0 && restored.currentPattern() == 2, "the restored chain plays");
			std::printf("  the state keeps mutes, solos, fill, seed and the chain playing\n");
		}

		// A pattern's MIDI file, read back by JUCE
		{
			Processor exported;
			exported.editSequencer([](Sequencer& _s)
			{
				auto& p = _s.pattern(3);
				p.setTrig(0, 0, true, 100);
				p.setTrig(0, 8, true, 90);
				p.step(0, 8).gate = 16;
				p.setTrig(15, 2, true, 70);
				p.step(15, 2).timing = 6;
				p.setLock(0, 0, 5, 1.0f);
			});
			const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("edDrumsPattern.mid");
			require(exported.exportPatternMidi(3, file), "the MIDI file written");
			juce::MidiFile midi;
			juce::FileInputStream in(file);
			require(in.openedOk() && midi.readFrom(in) && midi.getNumTracks() == 1 && midi.getTimeFormat() == 960,
				"the MIDI file read");
			std::vector<std::array<int, 3>> ons, offs;
			const auto& track = *midi.getTrack(0);
			for(int i = 0; i < track.getNumEvents(); ++i)
			{
				const auto& m = track.getEventPointer(i)->message;
				const int tick = juce::roundToInt(m.getTimeStamp());
				if(m.isNoteOn())
					ons.push_back({tick, m.getNoteNumber(), m.getVelocity()});
				else if(m.isNoteOff())
					offs.push_back({tick, m.getNoteNumber(), 0});
			}
			const std::vector<std::array<int, 3>> wantOn{{0, 36, 100}, {540, 51, 70}, {1920, 36, 90}};
			const std::vector<std::array<int, 3>> wantOff{{240, 36, 0}, {780, 51, 0}, {2400, 36, 0}};
			require(ons == wantOn && offs == wantOff, "the MIDI file's notes: " + std::to_string(ons.size()) + " on, "
				+ std::to_string(offs.size()) + " off");
			// The drag to the host: the same file, in the instrument's own temporary folder
			const auto dragged = exported.patternMidiForDrag(3, "Pattern A04");
			juce::MemoryBlock want, got;
			require(dragged.existsAsFile() && dragged.getFileName() == "Pattern A04.mid"
				&& dragged.getParentDirectory().getFileName() == Processor::MidiDragFolder
				&& file.loadFileAsData(want) && dragged.loadFileAsData(got) && want == got,
				"the MIDI file for a drag: " + dragged.getFullPathName().toStdString());
			dragged.deleteFile();
			file.deleteFile();
			std::printf("  a pattern's MIDI file read back by JUCE: notes 36 and 51 at their ticks, a gate of 2 steps; the"
				" drag's file the same\n");
		}
		std::printf("PASS: notes, gates, criterion 6, LIVE, the sequencer's state, MIDI export\n");
	}

	// A take in host blocks of _block samples, the last one shorter, with Machine 1 of Track 1 in the engine at each
	// block's end and what processBlock allocated
	struct SlideTake
	{
		Take take;
		std::vector<std::pair<int, int>> machine1;
		size_t allocations = 0;
	};

	SlideTake playBlocks(Processor& _processor, const int _samples, const int _block)
	{
		Transport transport;
		_processor.setPlayHead(&transport);
		_processor.prepareToPlay(SampleRate, _block);
		SlideTake r;
		r.take.assign(Channels, std::vector<float>(static_cast<size_t>(_samples)));
		r.machine1.reserve(static_cast<size_t>(_samples / _block + 1));
		juce::AudioBuffer<float> buffer(Channels, _block);
		juce::MidiBuffer midi;
		for(int at = 0; at < _samples; at += _block)
		{
			const int n = std::min(_block, _samples - at);
			buffer.setSize(Channels, n, false, true, true);
			transport.quarters = at / SamplesPerQuarter;
			r.allocations += countAllocations([&] { _processor.processBlock(buffer, midi); });
			for(int c = 0; c < Channels; ++c)
				std::copy_n(buffer.getReadPointer(c), n, r.take[static_cast<size_t>(c)].begin() + at);
			r.machine1.emplace_back(at + n, _processor.engine()->parameter(0, 0));
		}
		_processor.setPlayHead(nullptr);
		return r;
	}

	// Track 1 on steps 0 and 4, Filter Freq and Machine 1 locked to 20 on step 0 and 100 on step 4; step 0 slides or not.
	// A slide moves its step's Locks in a line to the next trig's, a step every 32 samples from its Hit
	// One instance at 44.1 kHz, then 96, then 44.1 again, prepareToPlay each time as a host that changes its rate does:
	// each sounds, reports its engine's hitDelay as its latency, and 44.1 kHz plays the same samples after as before
	void rateSwitch()
	{
		Processor p;
		allOutputs(p);
		const auto take = [&](const double _rate)
		{
			p.prepareToPlay(_rate, Block);
			std::vector<float> out;
			for(int b = 0; b < 60; ++b)
			{
				juce::AudioBuffer<float> buffer(Channels, Block);
				buffer.clear();
				juce::MidiBuffer midi;
				if(b == 1)
					for(int t = 0; t < 4; ++t)
						midi.addEvent(juce::MidiMessage::noteOn(1, 36 + t, static_cast<juce::uint8>(100 + t)), 17 + 23 * t);
				p.processBlock(buffer, midi);
				for(int c = 0; c < Channels; ++c)
					out.insert(out.end(), buffer.getReadPointer(c), buffer.getReadPointer(c) + Block);
			}
			double peak = 0;
			for(const float v : out)
				peak = std::max(peak, static_cast<double>(std::abs(v)));
			require(peak > 0.01, "the instance at " + std::to_string(_rate) + " Hz is silent");
			require(p.getLatencySamples() == Engine::hitDelay(_rate), "the latency at " + std::to_string(_rate) + " Hz");
			return out;
		};
		const auto before = take(44100);
		take(96000);
		const auto after = take(44100);
		require(before == after, "44.1 kHz after 96 kHz plays other samples");
		std::printf("  44.1, 96, 44.1 kHz in one instance: latency %d, %d, %d; 44.1 kHz the same samples after (%zu)\n",
			Engine::HitDelay, Engine::hitDelay(96000), Engine::HitDelay, before.size());
	}

	void slides(const Library& _library)
	{
		constexpr int Length = 210 * Block;			// 26880: steps 0 to 4 and some
		constexpr int SlideLength = 22050;			// 4 steps of a 16th at 120 BPM
		const int step4 = stepSample(1.0);
		require(step4 == SlideLength, "step 4's sample");

		const auto make = [](const bool _slide)
		{
			auto p = std::make_unique<Processor>();
			allOutputs(*p);
			const auto freq = indexOf(*p, "t01_flt_freq"), m1 = indexOf(*p, "t01_m1");
			p->editSequencer([&](Sequencer& _s)
			{
				auto& pattern = _s.pattern(0);
				pattern.setTrig(0, 0, true);
				pattern.step(0, 0).slide = _slide;
				pattern.setLock(0, 0, freq, 20);
				pattern.setLock(0, 0, m1, 20);
				pattern.setTrig(0, 4, true);
				pattern.setLock(0, 4, freq, 100);
				pattern.setLock(0, 4, m1, 100);
			});
			return p;
		};

		// What the processor tells its engine, as an engine driven directly is told it
		const auto callsOf = [&](Processor& _p, const bool _slide)
		{
			parameters::ChainArray host{};
			size_t f = 0;
			for(size_t i = 0; i < host.size(); ++i)
			{
				host[i] = whole(plain(find(_p, juce::String("t01_") + parameters::chainSpecs[i].id)));
				if(juce::String(parameters::chainSpecs[i].id) == "flt_freq")
					f = i;
			}
			const int m1Host = whole(plain(find(_p, "t01_m1")));
			require(m1Host != 20 && m1Host != 100 && host[f] != 20 && host[f] != 100, "a host value is a Lock's");
			const auto withFreq = [host, f](const int _v) { auto v = host; v[f] = _v; return parameters::chainOf(v); };
			Calls c;
			c.emplace(0, [=](Engine& _e) { _e.setChain(0, withFreq(20)); _e.lock(0, 0, 20); _e.noteOn(0, 100); });
			int freq = 20, m1 = m1Host;		// what the engine holds: the Hit's lock is not Machine 1's value
			std::vector<int> ramp;
			for(int at = 32; _slide && at <= step4; at += 32)
			{
				const int v = juce::roundToInt(20.0f + (100.0f - 20.0f) * (static_cast<float>(at) / static_cast<float>(SlideLength)));
				ramp.push_back(v);
				if(v != freq)
					c.emplace(at, [=](Engine& _e) { _e.setChain(0, withFreq(v)); });
				if(v != m1)
					c.emplace(at, [=](Engine& _e) { _e.setParameter(0, 0, static_cast<uint8_t>(v)); });
				freq = m1 = v;
			}
			require(!_slide || (std::is_sorted(ramp.begin(), ramp.end()) && ramp.back() == 100), "the ramp expected");
			// The next Hit: the host's values back, then its Locks
			c.emplace(step4, [=](Engine& _e)
			{
				_e.setChain(0, withFreq(host[f]));
				if(m1 != m1Host)
					_e.setParameter(0, 0, static_cast<uint8_t>(m1Host));
				_e.setChain(0, withFreq(100));
				_e.lock(0, 0, 100);
				_e.noteOn(0, 100);
			});
			return std::make_pair(c, m1Host);
		};

		auto sliding = make(true);
		const auto a = playBlocks(*sliding, Length, 37);
		const auto b = playBlocks(*sliding, Length, 512);
		requireSame(a.take, b.take, "a slide in blocks of 37 and of 512: ");
		const auto [slideCalls, m1Host] = callsOf(*sliding, true);
		requireSame(b.take, playEngine(_library, *sliding, slideCalls, Length), "the slide: ");
		require(a.allocations == 0 && b.allocations == 0, "processBlock allocated " + std::to_string(a.allocations) + " and "
			+ std::to_string(b.allocations) + " times");

		// Machine 1 read back from the engine at each block's end: rising from 20 to 100 before step 4, the host's after
		for(const auto* take : {&a, &b})
		{
			int last = 20, distinct = 0;
			for(const auto& [end, v] : take->machine1)
			{
				if(end >= step4)
				{
					require(v == m1Host, "Machine 1 after the slide's end: " + std::to_string(v));
					continue;
				}
				require(v >= last && v <= 100, "Machine 1 falls back to " + std::to_string(v) + " at " + std::to_string(end));
				distinct += v != last;
				last = v;
			}
			require(last >= 99 && distinct >= 40,"Machine 1 reached " + std::to_string(last) + " in " + std::to_string(distinct)
				+ " steps");
		}
		std::printf("  a slide from step 0 to step 4: Filter Freq and Machine 1 from 20 to 100 in %d steps of 32 samples, the "
			"engine's; the same in blocks of 37 and 512; nothing allocated\n", SlideLength / 32);

		// Without SLIDE the Locks hold until step 4
		auto held = make(false);
		const auto c = playBlocks(*held, Length, 512);
		requireSame(c.take, playEngine(_library, *held, callsOf(*held, false).first, Length), "the Locks without a slide: ");
		for(const auto& [end, v] : c.machine1)
			require(v == m1Host, "Machine 1 glides without a slide: " + std::to_string(v) + " at " + std::to_string(end));
		const int from = firstDifference(b.take, c.take);
		require(from > 0 && from < step4, "the slide is not heard before step 4");
		std::printf("  without SLIDE nothing glides; the slide is heard from sample %d\n", from);

		// A host note on the Track ends the slide: the host's values from it on
		{
			auto noted = make(true);
			Transport transport;
			noted->setPlayHead(&transport);
			noted->prepareToPlay(SampleRate, Block);
			juce::AudioBuffer<float> buffer(Channels, Block);
			for(int at = 0; at < 100 * Block; at += Block)
			{
				juce::MidiBuffer midi;
				if(at == 50 * Block)
					midi.addEvent(juce::MidiMessage::noteOn(1, 36, static_cast<juce::uint8>(100)), 0);
				transport.quarters = at / SamplesPerQuarter;
				noted->processBlock(buffer, midi);
				if(at >= 50 * Block)
					require(noted->engine()->parameter(0, 0) == m1Host, "a host note does not end the slide");
			}
			noted->setPlayHead(nullptr);
		}
		std::printf("PASS: slides\n");
	}

	// ---- the values' units ----

	// What a value's text says in its base unit: Hz, s, dB, % or a bare number, after a "Q ", "LP " or "HP "
	double measure(const juce::String& _text)
	{
		const auto t = _text.trim();
		if(t.startsWith("-inf"))
			return -std::numeric_limits<double>::infinity();
		if(t == "INF")
			return std::numeric_limits<double>::infinity();
		int at = 0;
		while(at < t.length() && !(juce::CharacterFunctions::isDigit(t[at]) || t[at] == '-' || t[at] == '+'))
			++at;
		const auto rest = t.substring(at);
		if(rest.containsChar('/'))		// a note's length, in wholes
			return rest.upToFirstOccurrenceOf("/", false, false).getDoubleValue()
				/ rest.fromFirstOccurrenceOf("/", false, false).getDoubleValue();
		const double x = rest.getDoubleValue();
		const auto unit = rest.fromFirstOccurrenceOf(" ", false, false);
		if(unit.startsWith("k"))
			return x * 1000;
		if(unit == "ms")
			return x / 1000;
		return x;
	}

	// The text of _id at _value, as the host asks for it
	juce::String textAt(Processor& _processor, const juce::String& _id, const int _value)
	{
		auto& p = find(_processor, _id);
		return p.getText(p.convertTo0to1(static_cast<float>(_value)), 0);
	}

	// The text at _value within three significant figures of _expected (dB within 0.05)
	void requireValue(Processor& _processor, const juce::String& _id, const int _value, const double _expected)
	{
		const auto text = textAt(_processor, _id, _value);
		const double shown = measure(text);
		const bool same = std::isinf(_expected) ? shown == _expected
			: std::abs(shown - _expected) <= std::max(0.0051 * std::abs(_expected), text.endsWith("dB") ? 0.051 : 1e-9);
		require(same, (_id + " at " + juce::String(_value) + " shows " + text + ", the engine's " + juce::String(_expected))
			.toStdString());
	}

	void requireValues(Processor& _processor, const juce::String& _id, const std::function<double(int)>& _engine)
	{
		for(const int v : {0, 64, 127})
			requireValue(_processor, _id, v, _engine(v));
	}

	// Every value of _p shown and read back as the host does: the same step; a float, the same text
	void roundTrip(juce::RangedAudioParameter& _p)
	{
		const auto before = _p.getValue();
		if(dynamic_cast<juce::AudioParameterFloat*>(&_p))
		{
			for(const float v : {0.0f, 0.25f, 0.5f, 1.0f})
			{
				const auto text = _p.getText(v, 0);
				require(_p.getText(_p.getValueForText(text), 0) == text, (_p.getParameterID() + ": " + text).toStdString());
			}
			return;
		}
		const auto& r = _p.getNormalisableRange();
		for(int v = juce::roundToInt(r.start); v <= juce::roundToInt(r.end); ++v)
		{
			const float normalised = _p.convertTo0to1(static_cast<float>(v));
			_p.setValue(normalised);
			const auto text = _p.getText(normalised, 0);
			const int back = juce::roundToInt(_p.convertFrom0to1(_p.getValueForText(text)));
			require(back == v, (_p.getParameterID() + " " + juce::String(v) + " shows " + text + ", read back as "
				+ juce::String(back)).toStdString());
		}
		_p.setValue(before);
	}

	void units()
	{
		Processor processor;
		namespace st = syntakt::fast;
		const double inf = std::numeric_limits<double>::infinity();
		const auto db = [](const double _gain) { return _gain > 0 ? 20 * std::log10(_gain) : -std::numeric_limits<double>::infinity(); };
		const auto choose = [&](const juce::String& _id, const int _value) { set(find(processor, _id), _value); };

		// Every parameter of Track 1 and the shared ones at their defaults (the other Tracks' are Track 1's; a relay shows
		// the Track shown's, its text read back near that parameter's value, not its own)
		int checked = 0;
		for(auto* parameter : processor.getParameters())
		{
			auto* p = dynamic_cast<juce::RangedAudioParameter*>(parameter);
			if(p && !isRelay(p->getParameterID()) && (!p->getParameterID().startsWith("t") || p->getParameterID().startsWith("t01_")
				|| p->getParameterID() == "tempo"))
			{
				roundTrip(*p);
				++checked;
			}
		}

		// Level, sends, pan: Engine::gainOf and setSend take the square of the fraction, panOf 64 as the centre
		const auto squared = [&](const int _v) { return db(_v / 127.0 * (_v / 127.0)); };
		requireValues(processor, "t01_level", squared);
		requireValues(processor, "t01_send_reverb", squared);
		require(textAt(processor, "t01_pan", 0) == "L64" && textAt(processor, "t01_pan", 64) == "C"
			&& textAt(processor, "t01_pan", 127) == "R63", "pan");

		// The Syntakt's filter: the cutoff word Chain::setFilter computes, on Multimode's scale; RESO's Q
		choose("t01_flt_type", static_cast<int>(FilterType::Syntakt));
		requireValues(processor, "t01_flt_freq", [](const int _v)
		{
			return 5 * std::pow(4400.0, (st::filterCutoff(wordOf(_v, 0) << 1, 64 << 8, 0, 60, 0) >> 22) / 512.0);
		});
		requireValues(processor, "t01_flt_res", [](const int _v) { return 0.501 * std::pow(std::sqrt(500.0) / 0.501, _v / 127.0); });
		roundTrip(find(processor, "t01_flt_freq"));
		roundTrip(find(processor, "t01_flt_res"));

		// The Machinedrum's: the frequency of the pole radius the DSP's table holds at FLTF's word >> 3
		const auto& md = md::fast::tables::tables(44100);
		const auto fromRadius = [](const int32_t _word, const double _rate)
		{
			return -std::log(_word / 8388608.0) * _rate / (2 * 3.14159265358979323846);
		};
		choose("t01_flt_type", static_cast<int>(FilterType::Machinedrum));
		requireValues(processor, "t01_flt_freq", [&](const int _v)
		{
			return fromRadius(md.cutoffExp[static_cast<size_t>((wordOf(_v, 0) >> 3) + md::fast::tables::CutoffExpBase)], 44100);
		});
		requireValues(processor, "t01_flt_width", [](const int _v) { return (wordOf(_v, 0) >> 3) / 2048.0 * std::log2(4000.0); });
		roundTrip(find(processor, "t01_flt_freq"));
		roundTrip(find(processor, "t01_flt_width"));

		// The envelopes, at 48 kHz in seconds; the amp's decay measured on a TrackAmp: the blocks it takes to fall to 1/10
		requireValues(processor, "t01_amp_hold", [&](const int _v) { return _v == 127 ? inf : st::ampHoldSamples(_v) / 48000; });
		requireValues(processor, "t01_amp_attack", [](const int _v) { return _v == 0 ? 0 : st::ampAttackSamples(_v) / 48000; });
		requireValues(processor, "t01_fenv_attack", [](const int _v) { return st::envelopeAttackBlocks(_v) * 32 / 48000; });
		requireValues(processor, "t01_fenv_decay", [&](const int _v)
		{
			return _v == 0 ? 0 : _v == 127 ? inf : st::envelopeDecayBlocks(_v) * 32 / 48000;
		});
		requireValues(processor, "t01_fenv_delay", [](const int _v) { return st::envelopeDelayBlocks(_v) * 32 / 48000; });
		requireValues(processor, "t01_amp_sustain", [&](const int _v) { return db(_v / 128.0); });
		{
			st::TrackAmp amp(st::MachineRate);
			amp.set(0, 0, 0, 64, 0, 0, false);
			amp.trig(0);
			st::Block block{};
			int samples = 0;
			do
			{
				for(auto& s : block.s)
					s[0] = 1;
				amp.process(block, 1);
				samples += st::BlockSize;
			} while(amp.level(0) > 0.1f && samples < 48000 * 60);
			const double shown = measure(textAt(processor, "t01_amp_decay", 64));
			require(std::abs(shown * 48000 - samples) <= 3 * st::BlockSize + st::ampHoldSamples(0),
				"amp decay 64: " + std::to_string(samples) + " samples to 1/10, shown " + std::to_string(shown));
		}
		{
			st::FilterEnvelope envelope(st::MachineRate);
			st::FilterEnvelope::Setting s;
			s.attack = 64;
			envelope.step(0, s, true, false);
			int steps = 0;
			while(envelope.step(0, s, false, false) < 1 && steps < 100000)
				++steps;
			const double shown = measure(textAt(processor, "t01_fenv_attack", 64)) * 48000 / 32;
			require(std::abs(shown - (steps + 1)) <= 1.5, "filter attack 64: " + std::to_string(steps + 1) + " blocks");
		}

		// The drives: TrackDrive's gain into its clip, Dist's table after its shift, Srr's held rate
		choose("t01_drv1_type", static_cast<int>(DriveType::Syntakt));
		requireValues(processor, "t01_drv1_amount", [&](const int _v)
		{
			const double u = (wordOf(_v, 0) << 1) / 32768.0;
			return db(0.1535 + 0.8465 * u * u);
		});
		roundTrip(find(processor, "t01_drv1_amount"));
		choose("t01_drv1_type", static_cast<int>(DriveType::MdDist));
		requireValues(processor, "t01_drv1_amount", [&](const int _v)
		{
			return db(md.distGain[static_cast<size_t>(wordOf(_v, 0) >> 6)] / 8388608.0 * 512);
		});
		roundTrip(find(processor, "t01_drv1_amount"));
		choose("t01_drv1_type", static_cast<int>(DriveType::MdSrr));
		requireValues(processor, "t01_drv1_amount", [](const int _v)
		{
			const double x = wordOf(_v, 0) + 0x4000;
			return 44100.0 * 16 / ((x * x / 8388608 - 32) * 4 + 16);
		});
		roundTrip(find(processor, "t01_drv1_amount"));
		requireValues(processor, "t01_drv1_mix", [](const int _v) { return 100.0 * _v / 127; });

		// The bands: ThreeBand's laws, the EQ's pole radius table at EQF's word >> 3
		choose("t01_band_type", static_cast<int>(BandType::ThreeBand));
		requireValues(processor, "t01_band_3", [](const int _v) { return ThreeBand::hz(_v); });
		requireValues(processor, "t01_band_4", [](const int _v) { return ThreeBand::db(_v); });
		for(int b = 1; b <= 6; ++b)
			roundTrip(find(processor, "t01_band_" + juce::String(b)));
		choose("t01_band_type", static_cast<int>(BandType::MdEq));
		requireValues(processor, "t01_band_1", [&](const int _v)
		{
			return fromRadius(md.eqExp[static_cast<size_t>((wordOf(_v, 0) >> 3) + md::fast::tables::EqExpBase)], 44100);
		});
		roundTrip(find(processor, "t01_band_1"));

		// The delays' times in 128ths of a whole note, from their frames at 120 BPM (2 s a whole); the Gate Box's
		const auto wholes = [](const double _samples, const double _rate) { return std::round(_samples / _rate / 2 * 128); };
		requireValues(processor, "delay_time", [&](const int _v)
		{
			return wholes(static_cast<double>((static_cast<uint32_t>((_v << 8) + 0x100) * 48000u >> 10) * 900 / 14400), 48000)
				/ 128;
		});
		requireValues(processor, "echo_time", [&](const int _v)
		{
			uint32_t product = static_cast<uint32_t>(_v << 7) * 44100u;
			if((_v << 7) > 0x3f3d)
				product += 128 * 44100u;
			return wholes(static_cast<double>((product >> 10) * 360 / 2880), 44100) / 128;
		});
		require(textAt(processor, "delay_time", 31) == "1/4" && textAt(processor, "echo_time", 32) == "1/4", "a quarter");
		requireValues(processor, "gate_dec", [](const int _v) { return 0.4 * std::pow(10.0, (_v >> 1) / 63.0); });
		requireValues(processor, "gate_pred", [](const int _v) { return _v * 64 / 44100.0; });
		requireValues(processor, "reverb_pre", [](const int _v) { return (_v * _v + 37) / 48000.0; });
		requireValues(processor, "delay_fdbk", [](const int _v) { return 100 * 66053.0 * (_v << 8) / 2147483648.0; });

		// Our chorus and phaser, their laws in edDrumsSendFx
		requireValues(processor, "chorus_rate", [](const int _v) { return sendFxRateHz(static_cast<uint8_t>(_v)); });
		requireValues(processor, "chorus_delay", [](const int _v) { return (8.0 * _v + 1) / 44100; });
		requireValues(processor, "chorus_level", [&](const int _v) { return db(sendFxLevel(static_cast<uint8_t>(_v))); });
		requireValues(processor, "phaser_freq", [](const int _v) { return phaserHz(_v); });
		choose("phaser_mode", 1);
		requireValues(processor, "phaser_freq", [](const int _v) { return (32.0 / 127 * _v + 1) / 44100; });
		roundTrip(find(processor, "phaser_freq"));
		choose("phaser_mode", 2);
		requireValues(processor, "phaser_rate", [](const int _v) { return dp4RateHz(static_cast<uint8_t>(_v)); });
		requireValues(processor, "phaser_dp4_left", [](const int _v) { return dp4DelayMs(static_cast<uint8_t>(_v)) / 1000; });
		require(textAt(processor, "phaser_freq", 0) == "-99" && textAt(processor, "phaser_freq", 127) == "+99", "DP/4");
		for(const char* id : {"phaser_rate", "phaser_freq", "phaser_spread", "phaser_fdbk"})
			roundTrip(find(processor, id));

		// The master
		requireValues(processor, "master_threshold", [](const int _v) { return masterThresholdDb(static_cast<uint8_t>(_v)); });
		requireValues(processor, "master_attack", [](const int _v) { return masterAttackMs(static_cast<uint8_t>(_v)) / 1000; });
		requireValues(processor, "master_release", [](const int _v) { return masterReleaseMs(static_cast<uint8_t>(_v)) / 1000; });
		requireValues(processor, "master_makeup", [](const int _v) { return 24.0823996531 * _v / 127; });
		requireValues(processor, "master_volume", squared);
		require(textAt(processor, "master_ratio", 0) == "1.50:1" && textAt(processor, "master_ratio", 127) == "20.0:1",
			"ratio " + textAt(processor, "master_ratio", 0).toStdString());
		require(textAt(processor, "master_filter", 0) == "LP 20.0 Hz" && textAt(processor, "master_filter", 127) == "HP 20.0 kHz",
			"filter " + textAt(processor, "master_filter", 0).toStdString());

		// The note, the accent, the depths, the free rates
		require(textAt(processor, "t01_note", 60) == "C4" && textAt(processor, "t01_note", 69) == "A4"
			&& textAt(processor, "t01_note", 61) == "C#4", "note " + textAt(processor, "t01_note", 60).toStdString());
		requireValues(processor, "t01_accent", [&](const int _v) { return db((128 + 2.0 * _v) / 127); });
		requireValue(processor, "t01_lfo1_depth", -127, -100);
		requireValue(processor, "t01_vel_depth", 64, 100.0 * 64 / 127);
		{
			auto& hz = find(processor, "t01_lfo1_hz");
			set(hz, 2);
			require(hz.getCurrentValueAsText() == "2.00 Hz", "LFO 1 Hz " + hz.getCurrentValueAsText().toStdString());
			auto& free = find(processor, "delay_free");
			set(free, 0);
			require(free.getCurrentValueAsText() == "OFF", "delay free at 0");
			set(free, 1200);
			require(std::abs(free.convertFrom0to1(free.getValueForText(free.getCurrentValueAsText())) - 1200) < 6,
				"delay free 1.20 kHz read back");
		}

		// Machine 1-24 in their machine's units (parameters::machineText), each against the code that plays it
		const auto playOn = [&](const Machine& _m)
		{
			const auto& list = processor.machines();
			const auto at = std::find(list.begin(), list.end(), _m);
			require(at != list.end(), "a machine of the list");
			choose("t01_machine", static_cast<int>(at - list.begin()));
		};
		const auto number = [](const juce::String& _text) { return std::strtod(_text.toRawUTF8(), nullptr); };
		const auto machineValues = [&](const juce::String& _id, const std::function<double(int)>& _engine)
		{
			for(const int v : {16, 40, 64, 100, 112})
			{
				const auto text = textAt(processor, _id, v);
				require(std::abs(number(text) - _engine(v)) <= std::max(0.0051 * std::abs(_engine(v)), 0.051),
					(_id + " at " + juce::String(v) + " shows " + text + ", the engine's " + juce::String(_engine(v))).toStdString());
			}
			roundTrip(find(processor, _id));
		};
		// MODERN DIGI's TUNE: BD MODERN's pitch (its offset 88) over its pitch at TUNE 64, in semitones, on a note high
		// enough that pitchQ16 does not clamp it at note 0 (at note 60 TUNE under 28 plays as 28)
		playOn({Family::Syntakt, 0});
		machineValues("t01_m1", [](const int _v)
		{
			return (st::pitchQ16(96, _v << 8, 88) - st::pitchQ16(96, 64 << 8, 88)) / 65536.0;
		});
		// ANALOG's kicks: the pitch word the circuit plays from TUNE, as the note's semitones
		playOn({Family::SyntaktAnalog, static_cast<uint8_t>(analog::Machine::KickHard)});
		machineValues("t01_m1", [](const int _v)
		{
			std::array<uint8_t, 8> controls{static_cast<uint8_t>(_v), 64, 64, 64, 64, 64, 64, 64};
			std::array<uint8_t, 20> words{};
			analog::paramsOf(analog::Machine::KickHard, controls.data(), words);
			const auto tune = static_cast<int16_t>(words[2] << 8 | words[3]);
			analog::port::Emac e(false);
			const double at = analog::port::pitchWord(e, tune, 60 << 16), base = analog::port::pitchWord(e, 64 << 8, 60 << 16);
			return (at - base) / (analog::port::pitchWord(e, 64 << 8, 61 << 16) - base);
		});
		// VA DRUMS: TUNE the note whose frequency the oscillator plays, FINE the detune's cents
		playOn({Family::OpenDrum, 0});
		for(const int v : {24, 60, 69, 100})
		{
			const auto name = textAt(processor, "t01_m1", v);
			const auto names = juce::StringArray{"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
			const int named = names.indexOf(name.dropLastCharacters(1)) + 12 * (name.getTrailingIntValue() + 1);
			require(std::abs(440 * std::pow(2.0, (named - 69) / 12.0) / MidiNoteFrequencies[v] - 1) < 1e-6,
				("VA DRUM TUNE " + juce::String(v) + " shows " + name).toStdString());
		}
		roundTrip(find(processor, "t01_m1"));
		machineValues("t01_m9", [](const int _v)
		{
			return 1200 * std::log2(1 + (_v / 127.0 - 0.5) * (1.0594630943592952645618252949463 - 1));
		});
		// HYPER FM: sophie::machineValue's units, the cutoff the voice computes from its percent
		playOn({Family::Sophie, 0});
		machineValues("t01_m2", [](const int _v) { return sophie::machineValue(SOPHIE_TUNE, static_cast<uint8_t>(_v)); });
		machineValues("t01_m3", [](const int _v) { return sophie::machineValue(SOPHIE_COLOR, static_cast<uint8_t>(_v)); });
		machineValues("t01_m10", [](const int _v)
		{
			const double hz = 40 * std::pow(400.0, sophie::machineValue(SOPHIE_CUTOFF, static_cast<uint8_t>(_v)) / 100);
			return hz >= 1000 ? hz / 1000 : hz;
		});
		require(textAt(processor, "t01_m1", 0) == "FUSE" && textAt(processor, "t01_m12", 127) == "BPF", "HYPER FM's lists");
		std::printf("  e.g. HYPER FM TUNE 54 %s, DECAY 58 %s, CUTOFF 64 %s\n", textAt(processor, "t01_m2", 54).toRawUTF8(),
			textAt(processor, "t01_m7", 58).toRawUTF8(), textAt(processor, "t01_m10", 64).toRawUTF8());
		for(int i = 1; i <= sophie::MachineSlots; ++i)
			roundTrip(find(processor, "t01_m" + juce::String(i)));
		// The Sampler, of the Machinedrum's family: BITS as vintage::bitsOf gives them
		playOn(Sampler);
		for(const int v : {0, 64, 127})
			require(textAt(processor, "t01_m10", v) == juce::String(vintage::bitsOf(v)) + " BIT", "Sampler BITS");

		std::printf("  e.g. FLTF 64 %s, Hold 64 %s, Echo TIME 32 %s, threshold 64 %s\n",
			textAt(processor, "t01_flt_freq", 64).toRawUTF8(), textAt(processor, "t01_amp_hold", 64).toRawUTF8(),
			textAt(processor, "echo_time", 32).toRawUTF8(), textAt(processor, "master_threshold", 64).toRawUTF8());
		std::printf("PASS: the values' units: %d parameters read back, every family at 0, 64 and 127 as the engine has it; "
			"the note, the accent, the depths, the free rates; Machine 1-24 of MODERN DIGI, ANALOG, VA DRUMS, HYPER FM and "
			"the Sampler as their code plays them\n",
			checked);
	}

	// ---- Track sounds, a pattern's Kit, Kit and pattern files (ticket 19) ----

	// One Hit of _track at velocity 100 at the start, held, in blocks
	Take playHit(Processor& _processor, const int _track, const int _samples)
	{
		_processor.prepareToPlay(SampleRate, Block);
		Take take(Channels, std::vector<float>(static_cast<size_t>(_samples)));
		juce::AudioBuffer<float> buffer(Channels, Block);
		juce::MidiBuffer midi;
		for(int at = 0; at < _samples; at += Block)
		{
			buffer.clear();
			midi.clear();
			if(at == 0)
				midi.addEvent(juce::MidiMessage::noteOn(1, 36 + _track, static_cast<juce::uint8>(100)), 0);
			_processor.processBlock(buffer, midi);
			for(int c = 0; c < Channels; ++c)
				std::copy_n(buffer.getReadPointer(c), Block, take[static_cast<size_t>(c)].begin() + at);
		}
		return take;
	}

	// A SysEx file of our own, made by mdProtocol's encoder, imported as a folder is: its Kit in an empty Slot of the
	// Bank, loaded, every value Lockstep has on its Tracks; and what Lockstep lacks of it, a line each (ticket 20)
	void machineKits()
	{
		using md::automation::sysex::MdKit;
		MdKit kit;
		kit.setName("IMPORTED");
		for(auto& v : kit.parameters)
			v[17] = 127;		// VOL at ASSIGN MACHINE's, which Lockstep's level alone stands for
		for(int t = 0; t < Tracks; ++t)
			kit.machines[static_cast<size_t>(t)] = 17;	// TRX-SD
		auto& one = kit.parameters[0];
		kit.machines[0] = 16;	// TRX-BD
		for(int i = 0; i < 8; ++i)
			one[static_cast<size_t>(i)] = static_cast<uint8_t>(10 + i);
		one[10] = 70; one[11] = 50; one[12] = 20; one[13] = 100; one[14] = 30; one[15] = 5; one[16] = 40;
		one[18] = 30; one[19] = 60; one[20] = 70;
		kit.levels[0] = 90;
		kit.chokes[0] = 2;		// T01's Hit silences T03
		kit.machines[1] = 128 + 4;	// ROM-05: the Sampler on factory slot 5
		kit.parameters[1][0] = 33;
		kit.parameters[1][8] = 50;	// AMD
		kit.parameters[1][22] = 20;	// LFOD
		kit.machines[2] = 80;	// INP-GA
		kit.machines[3] = 178;	// ROM-35, a sample of the user's own
		kit.parameters[4][17] = 100;
		kit.links[4] = 5;
		// Pans across the range, for the mix's gains below
		kit.levels[4] = 100;
		kit.levels[5] = 110;
		kit.parameters[5][18] = 127;
		kit.parameters[6][18] = 64;
		kit.levels[6] = 127;
		kit.parameters[7][18] = 100;
		kit.levels[7] = 60;
		kit.parameters[7][17] = 80;
		const uint8_t echo = md::automation::sysex::mdKitMasterIndex(0, 0), gate = md::automation::sysex::mdKitMasterIndex(1, 0);
		for(uint8_t i = 0; i < 8; ++i)
		{
			kit.masterEffects[static_cast<size_t>(echo + i)] = static_cast<uint8_t>(40 + i);
			kit.masterEffects[static_cast<size_t>(gate + i)] = static_cast<uint8_t>(80 + i);
		}

		const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("edDrumsMachineKits");
		folder.deleteRecursively();
		require(folder.createDirectory().wasOk(), "the folder");
		auto bytes = md::automation::sysex::mdKitDump(kit, 3);
		// A sample dump's header after it, which Lockstep does not read
		for(const uint8_t b : {0xf0, 0x7e, 0x00, 0x01, 0x00, 0x00, 0xf7})
			bytes.push_back(b);
		require(folder.getChildFile("backup.syx").replaceWithData(bytes.data(), bytes.size()), "the SysEx file");
		require(folder.getChildFile("song.stprj").replaceWithText("a project"), "the project file");

		Processor p;
		require(p.addToLocker(folder, false), "the import started");
		const auto until = juce::Time::getMillisecondCounter() + 20000;
		while(!p.takeLockerImport() && juce::Time::getMillisecondCounter() < until)
			juce::Thread::sleep(5);
		const auto& r = p.lastImport();
		require(r.kitSlots.size() == 1 && r.unread == 1, "the import: " + std::to_string(r.kitSlots.size()) + " kits, "
			+ std::to_string(r.unread) + " files not read");
		const int slot = r.kitSlots[0];
		require(p.kitSlot(slot).name == "IMPORTED", "the Slot's name: " + p.kitSlot(slot).name.toStdString());
		const auto notTaken = r.notTaken;
		for(const char* expected : {"IMPORTED: T03 plays INP-GA, which Lockstep does not have",
			"IMPORTED: T04 plays ROM-35, which Lockstep does not have", "IMPORTED: AM on T02 not taken",
			"IMPORTED: the LFO of T02 not taken", "IMPORTED: T05's Hit also hits T06",
			"IMPORTED: the master EQ and DYNAMIX not taken", "backup.syx: 1 x sample dumps", "song.stprj: not read"})
		{
			bool found = false;
			for(const auto& line : notTaken)
				found = found || line.startsWith(expected);
			require(found, std::string("the report lacks \"") + expected + "\": " + notTaken.joinIntoString(" | ").toStdString());
		}
		require(notTaken.size() == 8, "the report says more: " + notTaken.joinIntoString(" | ").toStdString());

		require(p.loadKit(slot), "the imported Kit loaded");
		const auto value = [&](const char* _id) { return whole(plain(find(p, _id))); };
		require(value("t01_machine") == choiceOf(p, Machine{Family::Machinedrum, 16}), "T01's machine");
		for(int i = 0; i < 8; ++i)
			require(value(("t01_m" + std::to_string(i + 1)).c_str()) == 10 + i, "T01's SYN" + std::to_string(i + 1));
		const std::map<std::string, int> one1{{"t01_send_delay", 60},
			{"t01_send_reverb", 70}, {"t01_band_1", 70}, {"t01_band_2", 50}, {"t01_flt_freq", 20}, {"t01_flt_width", 100},
			{"t01_flt_res", 30}, {"t01_drv1_amount", 5}, {"t01_drv2_amount", 40}, {"t01_chokes", 4},
			{"t01_flt_type", static_cast<int>(FilterType::Machinedrum)}, {"t01_band_type", static_cast<int>(BandType::MdEq)},
			{"t01_drv1_type", static_cast<int>(DriveType::MdSrr)}, {"t01_drv2_type", static_cast<int>(DriveType::MdDist)},
			{"echo_time", 40}, {"echo_lev", 47}, {"gate_dvol", 80}, {"gate_lev", 87}, {"delay_source", 0},
			{"reverb_source", 0}, {"t02_m1", 33}, {"t03_machine", 0}, {"t04_machine", 0}};
		for(const auto& [id, expected] : one1)
			require(value(id.c_str()) == expected, id + ": " + std::to_string(value(id.c_str())) + " for "
				+ std::to_string(expected));
		require(value("t02_machine") == p.samplerChoice() && p.sampleFingerprint(1) == "factory:5", "T02 on factory slot 5");
		require(value("t05_machine") == choiceOf(p, Machine{Family::Machinedrum, 17}), "T05's machine");

		// Each side's gain as the machine's mix gives it (HostModel::mixer: LEV^2 x VOL^2; Mixer::gains: the sine's first
		// quarter at PAN x 64, its last word at 127) and as Lockstep's gives it from the values imported (Engine's gainOf
		// and panOf): the louder side within 0.2 dB, the other within 1 % of full scale. The sends come after these gains
		// in both, as the squares of their own values, which the import copies
		for(const int t : {0, 4, 5, 6, 7})
		{
			const auto& v = kit.parameters[static_cast<size_t>(t)];
			const double lev = kit.levels[static_cast<size_t>(t)] / 127.0, vol = v[17] / 127.0;
			const double angle = juce::MathConstants<double>::halfPi * (v[18] >= 127 ? 8191 : v[18] * 64) / 8192;
			const double g = lev * lev * vol * vol;
			const double md[2]{g * std::cos(angle), g * std::sin(angle)};
			const auto prefix = "t" + juce::String(t + 1).paddedLeft('0', 2).toStdString() + "_";
			const double level = value((prefix + "level").c_str()) / 127.0, pan = value((prefix + "pan").c_str());
			const double ours[2]{level * level * std::min(1.0, (127 - pan) / 63), level * level * std::min(1.0, pan / 64)};
			const int loud = md[1] > md[0] ? 1 : 0;
			const double db = 20 * std::log10(ours[loud] / md[loud]);
			require(std::abs(db) < 0.2 && std::abs(ours[1 - loud] - md[1 - loud]) < 0.01, "T" + std::to_string(t + 1)
				+ "'s mix: left " + std::to_string(ours[0]) + " for " + std::to_string(md[0]) + ", right "
				+ std::to_string(ours[1]) + " for " + std::to_string(md[1]));
		}
		folder.deleteRecursively();
		std::printf("PASS: a Kit dump in a SysEx file into Slot %d, its values on the Tracks; %d things not taken said\n",
			slot + 1, notTaken.size());
	}

	void kitFiles(const Library& _library, const juce::File& _wav)
	{
		// A Track sound: Track 3 on the Sampler with a sample and its chain, LFO and Machine 9-24 changed, saved; loaded on
		// Track 7 of another instance it plays what Track 3 played, and every value of it is Track 3's
		{
			Processor from;
			allOutputs(from);
			set(find(from, "t03_machine"), from.samplerChoice());
			// A block first: the Sampler's Machine 1-8 take its defaults on the audio thread
			from.prepareToPlay(SampleRate, Block);
			block(from);
			juce::String error;
			require(from.loadSample(2, _wav, error), "the WAV refused: " + error.toStdString());
			const std::pair<const char*, double> values[]{{"t03_m1", 20}, {"t03_level", 90}, {"t03_pan", 30},
				{"t03_flt_freq", 70}, {"t03_flt_res", 40}, {"t03_drv1_amount", 50}, {"t03_amp_decay", 60},
				{"t03_lfo1_depth", 40}, {"t03_lfo1_target", 2}, {"t03_m12", 77}, {"t03_output", 3}};
			for(const auto& [id, v] : values)
				set(find(from, id), v);
			const auto file = from.saveTrackSound(2, "Dusty sampler");
			require(file.existsAsFile(), "the Track sound not written");
			const auto take = playHit(from, 2, 64 * Block);
			require(peakOf(take, 6) + peakOf(take, 7) > 0.01f, "Track 3's Hit is silent on Out 03");

			Processor to;
			allOutputs(to);
			const auto& entries = to.trackSounds().entries();
			require(entries.size() == 1 && entries[0].name == "Dusty sampler" && entries[0].file == file,
				"the Library lists " + std::to_string(entries.size()) + " sounds, the first "
				+ (entries.empty() ? std::string() : entries[0].name.toStdString() + " in "
				+ entries[0].file.getFullPathName().toStdString()) + ", for " + file.getFullPathName().toStdString());
			set(find(to, "t07_output"), 3);
			KitListener heard;
			to.addListener(&heard);
			require(to.loadTrackSound(6, file), "the Track sound not loaded");
			to.removeListener(&heard);
			require(heard.edits == 0 && heard.rereads > 0, "a Track sound loaded told edits");
			for(const auto& id : from.trackIds(2))
			{
				const auto suffix = id.substring(4);
				if(suffix == "output" || suffix == "chokes")
					continue;
				// A float's plain value comes back within a rounding, as a Kit's does (Processor::kitDiffers)
				const double a = plain(find(to, "t07_" + suffix)), b = plain(find(from, id));
				require(std::abs(a - b) <= 1e-5 * std::max(1.0, std::abs(b)), "Track 7's " + suffix.toStdString() + ": "
					+ std::to_string(plain(find(to, "t07_" + suffix))) + " for " + std::to_string(plain(find(from, id))));
			}
			require(to.sampleName(6) == _wav.getFileName(), "the Track sound's sample");
			requireSame(take, playHit(to, 6, 64 * Block), "a Track sound on Track 7 does not play as on Track 3: ");
			require(to.auditionTrackSound(file) && to.auditioned() && !to.auditioned()->pcm.empty(), "the audition");
			std::printf("  a Track sound saved from Track 3 plays the same on Track 7 of another instance\n");
		}

		// Ticket 27: a machine's name is no key. A Track sound saved with the name its machine had before (BD MODERN, as
		// the builds before the house names wrote it) loads and plays as the one saved now, and the Library shows the
		// machine's name now
		{
			Processor from;
			set(find(from, "t01_m2"), 90);
			const auto file = from.saveTrackSound(0, "Named kick");
			auto xml = from.trackSounds().read(file);
			require(xml && xml->getStringAttribute("machineName") == "PUNCH KICK", "a Track sound saved with its machine's "
				"name: " + (xml ? xml->getStringAttribute("machineName").toStdString() : std::string()));
			xml->setAttribute("machineName", "BD MODERN");
			const auto old = file.getSiblingFile("Old named kick" + juce::String(TrackSounds::Extension));
			require(KitBank::writeFile(old, *xml), "the old Track sound not written");
			from.trackSounds().follow();
			const auto& entries = from.trackSounds().entries();
			const auto entry = std::find_if(entries.begin(), entries.end(), [&](const auto& _e) { return _e.file == old; });
			require(entry != entries.end() && entry->machine == "BD MODERN" && from.trackSoundMachine(*entry) == "PUNCH KICK",
				"the old Track sound shown by its machine's name now");
			Processor now, before;
			allOutputs(now);
			allOutputs(before);
			require(now.loadTrackSound(5, file) && before.loadTrackSound(5, old), "the Track sounds not loaded");
			for(const auto& id : now.trackIds(5))
				require(plain(find(now, id)) == plain(find(before, id)), "the old Track sound's " + id.toStdString());
			requireSame(playHit(now, 5, 64 * Block), playHit(before, 5, 64 * Block), "an old Track sound plays apart: ");
			file.deleteFile();
			old.deleteFile();
			std::printf("  a Track sound saved under its machine's old name loads and plays as one saved now\n");
		}

		// A Track sound naming a machine no list holds: of an older version it is Off, of this one the name it was saved
		// with, never a name looked up by its id
		{
			Processor p;
			TrackSounds::Entry e;
			e.machineChoice = 9999;
			e.version = 1;
			require(p.trackSoundMachine(e) == "OFF", "an old Track sound's unknown machine: "
				+ p.trackSoundMachine(e).toStdString());
			e.version = KitBank::Version;
			e.machine = "PUNCH KICK";
			require(p.trackSoundMachine(e) == "PUNCH KICK", "a Track sound's unknown machine: "
				+ p.trackSoundMachine(e).toStdString());
			std::printf("  a Track sound naming a machine no list holds: OFF, or the name it was saved with\n");
		}

		// A Track sound of version 3, before page order, whose LFO aims at T05's Machine 2: the sound holds Track 1 alone,
		// so T05's machine is the set's. Where T05 plays a Machinedrum machine its Machine 2 stays; where it plays the
		// Syntakt's, the LFO follows Machine 2's value to where it plays now
		{
			juce::XmlElement old(TrackSounds::Tag);
			old.setAttribute("version", 3);
			old.setAttribute("name", "Old LFO");
			old.setAttribute("lfo1_track", 5);
			old.setAttribute("lfo1_target", static_cast<int>(LfoTarget::Machine2));
			const auto file = Processor::dataFolder().getChildFile("old-lfo.edsound");
			require(KitBank::writeFile(file, old), "the old Track sound not written");
			Processor machinedrum;
			set(find(machinedrum, "t05_machine"), plain(find(machinedrum, "t10_machine")));
			require(machinedrum.loadTrackSound(0, file), "the old Track sound not loaded");
			require(whole(plain(find(machinedrum, "t01_lfo1_target"))) == static_cast<int>(LfoTarget::Machine2),
				"an LFO aimed at T05's Machine 2, a Machinedrum machine, moved to "
				+ std::to_string(plain(find(machinedrum, "t01_lfo1_target"))));
			Processor syntakt;
			require(syntakt.loadTrackSound(0, file), "the old Track sound not loaded on the Syntakt");
			require(whole(plain(find(syntakt, "t01_lfo1_target"))) != static_cast<int>(LfoTarget::Machine2),
				"an LFO aimed at T05's Machine 2, a Syntakt machine, did not follow its value");
			file.deleteFile();
			std::printf("  an old Track sound's LFO aimed at another Track follows the machine the set plays there\n");
		}

		// A pattern's Kit: pattern 2 loads Slot 21 (Track 2's level 30) where it starts, at step 1 of its first pass
		Processor switched;
		allOutputs(switched);
		set(find(switched, "t02_level"), 30);
		require(switched.saveKit(20), "Slot 21 saved");
		set(find(switched, "t02_level"), 100);
		switched.editSequencer([](Sequencer& _s)
		{
			_s.pattern(0).setTrig(1, 0, true);
			_s.pattern(1).setTrig(1, 0, true);
		});
		switched.setPatternKit(1, 20);
		require(switched.patternKit(1) == 20 && switched.patternKit(0) == -1, "the patterns' Kits");
		constexpr int Two = 1457 * Block;
		const auto take = playSequenced(switched, Two, [&](const int _at)
		{
			if(_at == 128 * 230)
				set(find(switched, "pattern"), 2);
		});
		Processor reference;
		Calls calls;
		calls.emplace(0, hitOf(1));
		calls.emplace(stepSample(4.0), [](Engine& _e) { _e.setLevel(1, 30); _e.noteOn(1, 100); });
		calls.emplace(stepSample(8.0), hitOf(1));
		requireSame(take, playEngine(_library, reference, calls, Two), "pattern 2's Kit: ");
		switched.followKits();
		require(plain(find(switched, "t02_level")) == 30 && switched.currentKit() == 20 && !switched.kitDiffers(),
			"the Kit loaded by pattern 2 is not the Kit played");
		// ... with nothing allocated on the audio thread: pattern 1, given Slot 21 too, asked for from pattern 2
		{
			switched.setPatternKit(0, 20);
			set(find(switched, "t02_level"), 100);
			set(find(switched, "pattern"), 1);
			Transport transport;
			switched.setPlayHead(&transport);
			switched.prepareToPlay(SampleRate, Block);
			juce::AudioBuffer<float> buffer(Channels, Block);
			juce::MidiBuffer midi;
			size_t allocations = 0;
			for(int at = 0; at < 800 * Block; at += Block)
			{
				transport.quarters = at / SamplesPerQuarter;
				allocations += countAllocations([&] { switched.processBlock(buffer, midi); });
			}
			switched.setPlayHead(nullptr);
			require(switched.currentPattern() == 0 && plain(find(switched, "t02_level")) == 30, "pattern 1's Kit not loaded");
			require(allocations == 0, "a pattern's Kit loaded allocated " + std::to_string(allocations) + " times");
			switched.setPatternKit(0, -1);
		}
		std::printf("  pattern 2 loads Slot 21 at its first step, sample %d, the engine's, nothing allocated\n",
			stepSample(4.0));

		// Files: a Kit exported, then imported into another Slot, is the same Kit; a pattern the same pattern
		const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory);
		const auto kitFile = dir.getChildFile("edDrumsKitExport.edkit");
		require(switched.exportKit(20, kitFile) && switched.importKit(33, kitFile), "Kit export and import");
		{
			KitBank bank(Processor::dataFolder().getChildFile("Kits"));
			require(bank.read(33)->isEquivalentTo(bank.read(20).get(), false), "the Kit imported differs");
			Processor a, b;
			require(a.loadKit(20) && b.loadKit(33), "the Kits load");
			require(valuesOf(a) == valuesOf(b), "the Kit imported loads other values");
		}
		const auto patternFile = dir.getChildFile("edDrumsPatternExport.edpattern");
		require(switched.exportPattern(1, patternFile) && switched.importPattern(5, patternFile), "pattern files");
		const auto& p = switched.sequencer().pattern(5);
		require(p.kit == 20 && p.step(1, 0).trig && !p.step(1, 1).trig, "the pattern imported");
		kitFile.deleteFile();
		patternFile.deleteFile();
		std::printf("PASS: Track sounds play the same on another Track; a pattern's Kit loads at its start's sample; Kit "
			"and pattern files read back the same\n");
	}
}

namespace
{
	// ---- the relays for Push, in process (edDrumsVst3HostTest hosts them) ----

	void requireRelaysOn(Processor& _p, const int _track, const std::string& _what)
	{
		for(const auto& spec : parameters::relaySpecs)
		{
			const auto relay = juce::String("push_") + (spec.target ? spec.target : "track");
			const double expected = spec.target
				? plain(find(_p, juce::String::formatted("t%02d_", _track + 1) + spec.target)) : _track + 1;
			require(std::abs(plain(find(_p, relay)) - expected) <= 1e-5 * std::max(1.0, std::abs(expected)), _what + ": " + relay.toStdString() + " is not Track "
				+ std::to_string(_track + 1) + "'s");
		}
	}

	void relays()
	{
		Processor p;
		allOutputs(p);
		p.prepareToPlay(SampleRate, Block);
		Edits edits;
		p.addListener(&edits);
		block(p);
		p.syncRelays(false);
		requireRelaysOn(p, 0, "loaded");
		require(edits.edits == 0, "the relays told to the host as the instrument loads");

		// Another Track shown: the relays take its values, the host told of each that changed
		p.setShownTrack(4);
		p.syncRelays(false);
		requireRelaysOn(p, 4, "Track 5 shown");
		const int shownEdits = edits.edits;
		require(shownEdits > 0, "Track 5 shown, the host is told nothing");

		// A relay turned by the host: Track 5's parameter at the next block, Track 1's not, nothing more told
		const double before = plain(find(p, "t01_flt_freq"));
		set(find(p, "push_flt_freq"), 33);
		edits.edits = 0;
		block(p);
		p.syncRelays(false);
		require(plain(find(p, "t05_flt_freq")) == 33 && plain(find(p, "t01_flt_freq")) == before
			&& plain(find(p, "push_flt_freq")) == 33, "a relay does not reach the Track shown's parameter");
		require(edits.edits == 0, "a relay's target told to the host");

		// The Track relay: the Track shown, the relays on it
		set(find(p, "push_track"), 2);
		block(p);
		p.syncRelays(false);
		require(p.shownTrack() == 1, "the Track relay does not show Track 2");
		requireRelaysOn(p, 1, "the Track relay on Track 2");
		p.removeListener(&edits);

		// Neither the state nor a Kit holds a relay
		juce::MemoryBlock saved;
		p.getStateInformation(saved);
		const auto xml = Processor::readState(saved.getData(), static_cast<int>(saved.getSize()));
		require(xml && !xml->hasAttribute("push_level") && xml->hasAttribute("t05_flt_freq"), "a relay in the state");
		std::printf("PASS: the relays follow the Track shown (%d edits told when Track 5 is shown), a relay turned sets the"
			" Track shown's parameter silently, the Track relay shows a Track, the state holds none\n", shownEdits);
	}

	// ---- the state compressed ----

	juce::MemoryBlock measuredState(Processor& _p, const char* _what)
	{
		juce::MemoryBlock saved;
		_p.getStateInformation(saved);
		const auto xml = Processor::readState(saved.getData(), static_cast<int>(saved.getSize()));
		require(xml != nullptr && xml->getIntAttribute("version") == 6, std::string(_what) + ": the state's XML");
		juce::MemoryBlock old;
		juce::AudioProcessor::copyXmlToBinary(*xml, old);
		std::printf("  %s: %zu bytes as version 6 wrote it, %zu compressed (%.1f %%)\n", _what, old.getSize(),
			saved.getSize(), 100.0 * static_cast<double>(saved.getSize()) / static_cast<double>(old.getSize()));
		return saved;
	}

	void compressedState()
	{
		{
			Processor fresh;
			measuredState(fresh, "the default set");
		}

		// A full set: 128 patterns, 16 Tracks of 64 steps, a trig on each, of its own velocity, two Locks on every fourth
		Processor full;
		allOutputs(full);
		std::array<uint16_t, Tracks> cutoff{}, machine1{};
		for(int t = 0; t < Tracks; ++t)
		{
			cutoff[static_cast<size_t>(t)] = indexOf(full, juce::String::formatted("t%02d_flt_freq", t + 1));
			machine1[static_cast<size_t>(t)] = indexOf(full, juce::String::formatted("t%02d_m1", t + 1));
		}
		full.editSequencer([&](Sequencer& _s)
		{
			for(int i = 0; i < SeqPatterns; ++i)
			{
				auto& q = _s.pattern(i);
				for(int t = 0; t < Tracks; ++t)
				{
					q.trackLength[static_cast<size_t>(t)] = SeqSteps;
					for(int n = 0; n < SeqSteps; ++n)
					{
						q.setTrig(t, n, true, 1 + (7 * i + 5 * t + 3 * n) % 127);
						if(n % 4)
							continue;
						q.setLock(t, n, cutoff[static_cast<size_t>(t)], static_cast<float>((i + n) % 128));
						q.setLock(t, n, machine1[static_cast<size_t>(t)], static_cast<float>((t + n) % 128));
					}
				}
			}
		});
		const auto saved = measuredState(full, "a full set (128 patterns of 16 Tracks x 64 trigs, 4096 Locks each)");
		const auto xml = Processor::readState(saved.getData(), static_cast<int>(saved.getSize()));
		juce::MemoryBlock old;
		juce::AudioProcessor::copyXmlToBinary(*xml, old);

		// The compressed state and the same uncompressed, as version 6 wrote it, each give the set back and play it
		Processor restored, fromOld;
		allOutputs(restored);
		allOutputs(fromOld);
		restored.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
		fromOld.setStateInformation(old.getData(), static_cast<int>(old.getSize()));
		// The patterns, the sequencer and the samples to the character; the parameters to a float's rounding (an LFO's
		// rate comes back within its normalised float, as it always has)
		const auto requireSet = [&xml](Processor& _p, const std::string& _what)
		{
			juce::MemoryBlock back;
			_p.getStateInformation(back);
			const auto b = Processor::readState(back.getData(), static_cast<int>(back.getSize()));
			require(b && b->getNumAttributes() == xml->getNumAttributes() && b->getNumChildElements() == xml->getNumChildElements(),
				_what + ": not the set's elements");
			for(int i = 0; i < xml->getNumAttributes(); ++i)
			{
				const auto name = xml->getAttributeName(i);
				const double v = xml->getDoubleAttribute(name), w = b->getDoubleAttribute(name, -1e9);
				require(std::abs(v - w) <= 1e-5 * std::max(1.0, std::abs(v)), _what + ": " + name.toStdString());
			}
			for(int i = 0; i < xml->getNumChildElements(); ++i)
				require(b->getChildElement(i)->toString() == xml->getChildElement(i)->toString(),
					_what + ": its element " + std::to_string(i));
		};
		requireSet(restored, "the compressed state restored");
		requireSet(fromOld, "the version 6 state");
		constexpr int Length = 846 * Block;
		const auto take = playSequenced(full, Length);
		require(peakOf(take, 0) > 0.05f, "the full set is silent");
		requireSame(take, playSequenced(restored, Length), "the compressed state restored: ");
		requireSame(take, playSequenced(fromOld, Length), "the version 6 state: ");

		// A container of a format to come, or cut short, loads nothing
		juce::MemoryBlock later(saved);
		static_cast<int*>(later.getData())[1] = 2;
		require(Processor::readState(later.getData(), static_cast<int>(later.getSize())) == nullptr, "a format to come");
		require(Processor::readState(saved.getData(), static_cast<int>(saved.getSize()) / 2) == nullptr, "a state cut short");
		std::printf("PASS: the state compressed restores the same set and samples; a version 6 state loads the same\n");
	}
}

int main(const int _argc, char** _argv)
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	const juce::ScopedJuceInitialiser_GUI juce;
	if(_argc > 1 && std::string(_argv[1]) == "--print-ids")
	{
		Processor processor;
		for(const auto& id : idsOf(processor))
			std::printf("%s\n", id.toRawUTF8());
		return 0;
	}
	if(Library::findFlashImage({}).empty())
	{
		std::printf("SKIP: set GEARMULATOR_MD_FIRMWARE_BIN to the Machinedrum UW flash image\n");
		return 77;
	}
	// A data folder of the test's own, empty: the Kits' Bank is written there, never in the user's
	const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("edDrumsProcessorTestData");
	root.deleteRecursively();
	_putenv_s("GEARMULATOR_DATA_ROOT", root.getFullPathName().toRawUTF8());
	try
	{
		units();
		transport();
		{
			Processor processor;
			frozenIds(processor);
		}
		const Library library(Library::findFlashImage({}));
		everyParameter(library);
		sequencer(library);
		analogState();
		sequencerFunctions(library);
		familyChange();
		state();
		relays();
		compressedState();
		samplerChoice();
		machinePages();
		const auto wav = writeWav(juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("edDrumsSample.wav"));
		samples(wav);
		kits(wav);
		kitFiles(library, wav);
		machineKits();
		sampleLibrary();
		wav.deleteFile();
		slides(library);
		rateSwitch();
		return 0;
	}
	catch(const std::exception& e)
	{
		std::printf("FAIL: %s\n", e.what());
		return 1;
	}
}
