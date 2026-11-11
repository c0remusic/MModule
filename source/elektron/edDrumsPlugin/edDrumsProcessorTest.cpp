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
//   - the Sampler last of Machine's choices, none before it moved, an old state on ROM-33 to -48 playing it; a WAV
//     loaded on a Track plays, the state keeps it, cleared it is silence (ticket 20);
//   - a Kit saved, then loaded by another instance, plays the same, its sample included, no edit told; two instances
//     on one Bank each write a Slot and keep the other's; a Kit without an ID loads that parameter's default (ticket 19);
//   - the sequencer from a playhead at 120 BPM: Hits at their steps' samples, Locks for their step only, Pattern
//     switching at the end of the pattern playing, patterns and samples in the state play the same (ticket 17).
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
#include <vector>

#include "edDrumsMaster.h"
#include "edDrumsProcessor.h"
#include "mdProtocol/mdmachines.h"

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
		for(const auto s : {Processor::Section::Effects, Processor::Section::Master, Processor::Section::Global})
			inSections += _processor.ids(s).size();
		require(inSections == ids.size(), "every parameter in one section");
		std::printf("  %d parameters, %d per Track, %d shared; the IDs are the frozen list's\n", ids.size(),
			_processor.trackIds(0).size(), ids.size() - Tracks * _processor.trackIds(0).size());
		require(_processor.trackIds(0).size() == 75, "75 a Track");
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
			for(int i = 0; i < 8; ++i)
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

	Values valuesOf(Processor& _processor)
	{
		Values v;
		for(auto* parameter : _processor.getParameters())
			if(auto* p = dynamic_cast<juce::RangedAudioParameter*>(parameter))
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
			if(!ids[i].startsWith("t") || ids[i].startsWith("t01_"))
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
		// The accent plays on a Hit with an accent, which no host's note gives, and Pattern asks for a pattern, all empty here
		require(unheard == std::vector<std::string>{"accent", "pattern"}, "a setting the processor gives is not heard");
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
				else
					require(std::abs(v - expected.at(id)) <= 1e-5 * std::max(1.0, std::abs(v)),
						id.toStdString() + ": not its default");	// a float default comes back through its 0-1 value
				++checked;
			}
			require(other.parameter("not_ours") == nullptr, "an ID not ours");
			std::printf("  a state without ticket 18's parameters: %d at their defaults, Track 1's chain the Machinedrum's\n",
				checked);
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

	void samplerChoice()
	{
		Processor processor;
		// The list before the Sampler: Off, the Syntakt's, the Machinedrum's that played, ROM-33 to -48 among them
		std::vector<Machine> old{Machine{}};
		for(int id = 0; id < SyntaktMachines; ++id)
			old.push_back({Family::Syntakt, static_cast<uint8_t>(id)});
		for(const auto& m : md::machines::machines(md::MachineModel::Machinedrum))
			if(m.id < 256 && md::voice::playable(static_cast<uint8_t>(m.id)))
				old.push_back({Family::Machinedrum, static_cast<uint8_t>(m.id)});
		const auto& list = processor.machines();
		require(old.size() >= 16 && list.size() == old.size() - 15, "the list's size");
		for(size_t i = 0; i < 16; ++i)
			require(old[old.size() - 16 + i] == Machine{Family::Machinedrum, static_cast<uint8_t>(176 + i)},
				"ROM-33 to -48 were not the old list's last 16");
		require(std::equal(list.begin(), list.end() - 1, old.begin()), "a choice before the Sampler moved");
		require(list.back() == Sampler && processor.samplerChoice() == static_cast<int>(list.size()) - 1, "the Sampler last");
		const auto& machine = dynamic_cast<juce::AudioParameterChoice&>(find(processor, "t01_machine"));
		require(machine.choices[processor.samplerChoice()] == "SAMPLER", "the Sampler's name");

		// A state of version 2 on ROM-33 or ROM-40 plays the Sampler; one on TRX-BD stays there
		const int rom33 = static_cast<int>(old.size()) - 16;
		juce::XmlElement xml("ElektronDrums");
		xml.setAttribute("version", 2);
		xml.setAttribute("t01_machine", rom33);
		xml.setAttribute("t02_machine", rom33 + 7);
		xml.setAttribute("t03_machine", choiceOf(processor, {Family::Machinedrum, 16}));
		juce::MemoryBlock state;
		juce::AudioProcessor::copyXmlToBinary(xml, state);
		processor.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
		require(whole(plain(find(processor, "t01_machine"))) == processor.samplerChoice()
			&& whole(plain(find(processor, "t02_machine"))) == processor.samplerChoice(), "an old ROM-33 to -48");
		require(list[static_cast<size_t>(whole(plain(find(processor, "t03_machine"))))] == Machine{Family::Machinedrum, 16},
			"an old TRX-BD");
		std::printf("PASS: the Sampler takes ROM-33's place, last of %zu choices; an old state on ROM-33 to -48 plays it\n",
			list.size());
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
	constexpr int MidiChannelOne = 1;

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

		// Every parameter of Track 1 and the shared ones at their defaults (the other Tracks' are Track 1's)
		int checked = 0;
		for(auto* parameter : processor.getParameters())
		{
			auto* p = dynamic_cast<juce::RangedAudioParameter*>(parameter);
			if(p && (!p->getParameterID().startsWith("t") || p->getParameterID().startsWith("t01_")
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

		std::printf("  e.g. FLTF 64 %s, Hold 64 %s, Echo TIME 32 %s, threshold 64 %s\n",
			textAt(processor, "t01_flt_freq", 64).toRawUTF8(), textAt(processor, "t01_amp_hold", 64).toRawUTF8(),
			textAt(processor, "echo_time", 32).toRawUTF8(), textAt(processor, "master_threshold", 64).toRawUTF8());
		std::printf("PASS: the values' units: %d parameters read back, every family at 0, 64 and 127 as the engine has it\n",
			checked);
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
		familyChange();
		state();
		samplerChoice();
		const auto wav = writeWav(juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("edDrumsSample.wav"));
		samples(wav);
		kits(wav);
		wav.deleteFile();
		sequencer(library);
		return 0;
	}
	catch(const std::exception& e)
	{
		std::printf("FAIL: %s\n", e.what());
		return 1;
	}
}
