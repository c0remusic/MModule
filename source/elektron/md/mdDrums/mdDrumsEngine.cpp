#include "mdDrumsEngine.h"

#include "Dsp56.h"
#include "MdEngine.h"
#include "Firmware.h"
#include "MdsPackage.h"

#include "mdProtocol/mdkit.h"
#include "mdProtocol/mdmachines.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <stdexcept>

namespace mdDrums
{
	namespace
	{
		constexpr size_t g_flashSize = 8 * 1024 * 1024;

		std::vector<uint8_t> readFlash(const std::filesystem::path& _path)
		{
			std::error_code error;
			if(!std::filesystem::is_regular_file(_path, error) || std::filesystem::file_size(_path, error) != g_flashSize)
				return {};
			std::ifstream file(_path, std::ios::binary);
			std::vector<uint8_t> flash(g_flashSize);
			if(!file.read(reinterpret_cast<char*>(flash.data()), static_cast<std::streamsize>(flash.size())))
				return {};
			return flash;
		}

		std::filesystem::path fromEnvironment(const char* _name)
		{
			const auto* value = std::getenv(_name);
			return value && *value ? std::filesystem::path(value) : std::filesystem::path();
		}
	}

	// Sample-accurate Hits (ticket 19 of the editor map). The voice DSP starts a Hit at its next 32-sample block, 0 to 31
	// samples after the note. Each Track's contributions to the mix (main left and right, its REV and DEL sends, its
	// own output) go through a delay of 31 less that wait, set by its Hit, so that every Hit sounds 31 samples after its
	// note, plus what its machine's voice is quicker than the latest (ticket 36: voiceStart, AlignedStart), so that
	// every voice sounds as late as TRX-BD's. The contributions are the products the mixer DSP sums (Mixer::gains): the
	// level a Hit sets, PAN and the sends move with the Hit, and the sum wraps as the DSP's does, so equal delays give
	// the engine's own mix, shifted. A Track's own output waits MasterDelay more, as Main does after the master
	// effects.
	struct Engine::State
	{
		State(const md::fw::FlashOs& _os, const md::fw::RomBank& _bank, const bool _master)
			: engine(_os.firmware, _os.osImage), samples(_bank)
		{
			engine.mixOn = false;
			if(_master)
				engine.enableMaster(_os.firmware);
			pending.fill(-1);
		}

		// A block, plus the longest delay: the rest of a block, a voice held back to AlignedStart, ANTI-CLICK's wait,
		// MasterDelay on a Track's own output
		// The same whatever the setting, so that a Hit on its way when it changes still fits, and no tail is cut
		static constexpr int Ring = 256;
		static_assert(BlockSize + SampleAccurateDelay + AlignedStart + MaxFade + MasterDelay <= Ring);
		static constexpr int mask = Ring - 1;
		static constexpr int Channels = 6;	// main L R, REV L R, DEL L R

		struct Lane
		{
			std::array<std::array<md::engine::d56::Acc, Channels>, Ring> mix{};
			std::array<md::engine::d56::Acc, Ring> solo{};	// the Track's own output, before the limiter
		};

		md::engine::Engine engine;
		std::array<std::shared_ptr<const md::mds::Package>, 16> userMachines{};	// as installed, for their defaults
		Samples samples;
		md::engine::Engine::Output out;
		size_t used = BlockSize;	// samples of the output block already handed out

		bool sampleAccurate = true;
		int64_t blockStart = 0;	// the engine's time at the next block it renders
		std::array<int, TrackCount> delay{};
		std::array<int, TrackCount> pending{};		// the delay a Hit set, from the block it lands in; -1 none
		std::array<int, TrackCount> heldDelay{};	// a Hit waiting for ANTI-CLICK's fade: its delay when it fires
		int fade = 0;								// ANTI-CLICK's wait, in samples
		std::array<Lane, TrackCount> lanes{};
		std::array<std::array<uint32_t, 5>, TrackCount> lastWords{};	// each Track's mixer words in the last block

		// The output block: the master effects' inputs (the dry main mix and the two sends), Main after them, each Track's
		// own output
		md::engine::Mixer::Stereo dry{};
		md::engine::Mixer::Stereo rev{};
		md::engine::Mixer::Stereo del{};
		md::engine::Mixer::Stereo main{};
		std::array<std::array<int32_t, BlockSize>, TrackCount> solo{};

		void renderBlock();
	};

	void Engine::State::renderBlock()
	{
		using namespace md::engine::d56;
		samples.write(engine.voices());
		if(!engine.render(out))
			throw std::runtime_error("Machinedrum engine fault: " + engine.fault());
		const auto start = blockStart;
		blockStart += BlockSize;
		auto& host = engine.host();

		// A Hit that waited for ANTI-CLICK's fade fires in this block with its own delay: it lands as late after its note as
		// every other Hit
		for(int t = 0; t < TrackCount; ++t)
			if(host.firedHeld(t))
				pending[t] = heldDelay[t];
		std::array<bool, TrackCount> struck{};
		for(int t = 0; t < TrackCount; ++t)
		{
			struck[t] = pending[t] >= 0;
			if(!struck[t])
				continue;
			delay[t] = pending[t];
			pending[t] = -1;
		}
		// A NEIGHBOUR User Machine plays on the delay of its first source, by default the Track before it (tickets 20
		// and 31 of the User Machines map): its sound, made from that Track's raw block, then lands where that Track's
		// does, exactly when it reads that Track alone (7 of the 13 in X.20 B's zip), and a block later for a source
		// rendered after it; its own Hit's wait does not count
		for(int t = 0; t < TrackCount && sampleAccurate; ++t)
		{
			const auto id = host.machine(t);
			// X.20's NFX built-ins read their sources as NEIGHBOUR User Machines do (the map of X.20's built-in Machines)
			if(host.playsX20(id) && md::engine::x20::hearsTracks(id))
			{
				delay[t] = delay[engine.voices().sources(t)[0]];
				continue;
			}
			if(!md::engine::isUserMachine(id))
				continue;
			const auto& package = userMachines[static_cast<size_t>(id - md::engine::kFirstUserMachine)];
			if(package && (package->flags & md::mds::FlagNeighbour))
				delay[t] = delay[engine.voices().sources(t)[0]];
		}

		// A Choke that silenced a Track in this block (ticket 38): the Track stops exactly where the Hit that cut it
		// sounds. What it still had waiting past that point goes; when it waits less than that Hit, it plays on with
		// its last block's words up to it, unless the Hit struck it too. A lower Track, which the engine silences at its
		// next tick, keeps sounding until then at its own delay.
		std::array<int, TrackCount> extend{};
		for(int t = 0; t < TrackCount && sampleAccurate; ++t)
		{
			const auto by = host.cutBy(t);
			if(by >= TrackCount)
				continue;
			// Up to the furthest a block written before this one reaches (its own output MasterDelay further)
			for(auto time = start + delay[by]; time < start + SampleAccurateDelay + AlignedStart; ++time)
			{
				lanes[t].mix[time & mask] = {};
				lanes[t].solo[(time + MasterDelay) & mask] = 0;
			}
			if(!struck[t])
				extend[t] = std::max(0, delay[by] - delay[t]);
		}

		for(int t = 0; t < TrackCount; ++t)
		{
			const auto words = host.mixerInput(t).mix;
			const auto last = lastWords[t];
			lastWords[t] = words;
			const auto& samples = out.tracks[t];
			if(std::all_of(samples.begin(), samples.end(), [](const int32_t _s) { return _s == 0; }))
				continue;
			auto& lane = lanes[t];
			// Samples _from to _to of the block with the mixer's words _words
			const auto mix = [&](const std::array<uint32_t, 5>& _words, const int _from, const int _to)
			{
				std::array<int32_t, Channels> gains{};
				const bool main = engine.mixer().gains(_words, (engine.dryMute >> t) & 1, gains);
				const auto vol = sx24(_words[1]);
				for(int i = _from; i < _to; ++i)
				{
					const auto slot = (start + i + delay[t]) & mask;
					if(main)
						for(int c = 0; c < Channels; ++c)
							lane.mix[slot][c] = add(lane.mix[slot][c], mpy(gains[c], samples[i]));
					// Mixer::solo, before its limiter, as late as Main
					auto& solo = lane.solo[(slot + MasterDelay) & mask];
					solo = add(solo, asl(mpy(samples[i], vol), 4));
				}
			};
			mix(last, 0, extend[t]);
			mix(words, extend[t], BlockSize);
		}

		for(int i = 0; i < BlockSize; ++i)
		{
			const auto slot = (start + i) & mask;
			std::array<Acc, Channels> sum{};
			for(int t = 0; t < TrackCount; ++t)
			{
				auto& lane = lanes[t];
				for(int c = 0; c < Channels; ++c)
					sum[c] = add(sum[c], lane.mix[slot][c]);
				lane.mix[slot] = {};
				solo[t][i] = lim(lane.solo[slot]);
				lane.solo[slot] = 0;
			}
			dry[i] = {md::engine::Mixer::mainSample(sum[0]), md::engine::Mixer::mainSample(sum[1])};
			rev[i] = {md::engine::Mixer::mainSample(sum[2]), md::engine::Mixer::mainSample(sum[3])};
			del[i] = {md::engine::Mixer::mainSample(sum[4]), md::engine::Mixer::mainSample(sum[5])};
		}
		if(!engine.masterOn())
			main = dry;
		else if(!engine.processMaster(dry, rev, del, main))
			throw std::runtime_error("Machinedrum master effects fault: " + engine.fault());
	}

	const std::array<uint8_t, Engine::MasterCount>& Engine::masterDefaults()
	{
		// Gate Box DVOL PRED DEC DAMP HP LP GATE LEV, Rhythm Echo TIME MOD MFRQ FB FLTF FLTW MONO LEV, EQ LF LG HF HG PF PG
		// PQ GAIN, Dynamix ATCK REL TRHD RTIO KNEE HP OUTG MIX
		static const std::array<uint8_t, MasterCount> defaults{
			0, 0, 68, 50, 1, 82, 71, 109,
			16, 0, 32, 27, 0, 44, 0, 71,
			64, 64, 64, 64, 64, 64, 64, 127,
			127, 127, 127, 127, 127, 127, 0, 0};
		return defaults;
	}

	void Engine::setMaster(const int _index, const int _value)
	{
		m_state->engine.host().setMaster(_index, _value);
	}

	std::string_view Engine::parameterName(const uint16_t _machine, const size_t _index)
	{
		if(_index < 8)
			if(const auto* names = md::machines::parameterNames(md::MachineModel::Machinedrum, _machine))
				return (*names)[_index];
		return paramNames()[_index];
	}

	const std::array<const char*, Engine::ParamCount>& Engine::paramNames()
	{
		static const std::array<const char*, ParamCount> names{
			"SYN1", "SYN2", "SYN3", "SYN4", "SYN5", "SYN6", "SYN7", "SYN8",
			"AMD", "AMF", "EQF", "EQG", "FLTF", "FLTW", "FLTQ", "SRR",
			"DIST", "VOL", "PAN", "DEL", "REV", "LFOS", "LFOD", "LFOM"};
		return names;
	}

	const std::array<uint8_t, Engine::ParamCount>& Engine::paramDefaults()
	{
		static const std::array<uint8_t, ParamCount> defaults{
			64, 64, 64, 64, 64, 64, 64, 64,
			0, 0, 64, 64, 0, 127, 0, 0,
			0, 100, 64, 0, 0, 64, 0, 0};
		return defaults;
	}

	uint8_t Engine::defaultMachine(const int _track)
	{
		// TRX's ids run from 16 in this order (mdLib/mdmachines.cpp); EFM-BD and -SD are 32 and 33.
		return static_cast<uint8_t>(_track < 14 ? 16 + _track : 32 + (_track - 14));
	}

	std::vector<uint8_t> Engine::findFlashImage(const std::vector<std::filesystem::path>& _folders)
	{
		if(const auto path = fromEnvironment("GEARMULATOR_MD_FIRMWARE_BIN"); !path.empty())
		{
			if(auto flash = readFlash(path); !flash.empty())
				return flash;
		}
		auto folders = _folders;
		if(const auto local = fromEnvironment("LOCALAPPDATA"); !local.empty())
			folders.push_back(local / "Programs" / "Gearmulator-Elektron");
		if(const auto home = fromEnvironment("USERPROFILE"); !home.empty())
			folders.push_back(home / "Documents" / "Gearmulator Preview" / "Machinedrum");
		if(const auto home = fromEnvironment("HOME"); !home.empty())
			folders.push_back(home / "Documents" / "Gearmulator Preview" / "Machinedrum");
		for(const auto& folder : folders)
		{
			if(auto flash = readFlash(folder / FlashImageName); !flash.empty())
				return flash;
		}
		return {};
	}

	std::vector<uint8_t> Engine::readFlashImage(const std::filesystem::path& _file, std::string& _error)
	{
		auto flash = readFlash(_file);
		if(flash.empty())
		{
			_error = "That file is not an 8 MB flash image";
			return {};
		}
		try
		{
			(void)md::fw::loadFirmwareFromFlash(flash);
		}
		catch(const std::exception&)
		{
			_error = "No Machinedrum OS in that file";
			return {};
		}
		return flash;
	}

	Engine::Engine(const std::vector<uint8_t>& _flashImage)
		: Engine(md::fw::loadFirmwareFromFlash(_flashImage), md::fw::loadRomBankFromFlash(_flashImage), true)
	{
		// The machines' voice code and the master's compiled before the audio thread plays them (EngineT::warmUpVoices,
		// MasterEngine::warmUp)
		if(m_state->engine.warmUpVoices() < 0)
			throw std::runtime_error("Machinedrum voice DSP fault while compiling the machines");
		if(auto* master = m_state->engine.masterEngine(); master && !master->warmUp())
			throw std::runtime_error("Machinedrum mixer DSP fault while compiling the master effects");
	}

	Engine::Engine(const md::fw::FlashOs& _os, const md::fw::RomBank& _bank, const bool _master)
		: m_state(std::make_unique<State>(_os, _bank, _master))
	{
		m_state->engine.loadRomBank(_bank);
		applyEngineDefaults(m_state->engine.host());
	}

	Engine::~Engine() = default;

	bool Engine::setSample(const int _rom, std::shared_ptr<const Sample> _sample)
	{
		return m_state->samples.set(_rom, std::move(_sample));
	}

	std::shared_ptr<const Sample> Engine::sample(const int _rom) const
	{
		return _rom >= 0 && _rom < Samples::RomCount ? m_state->samples.get(_rom) : nullptr;
	}

	bool Engine::samplesWritten() const
	{
		return m_state->samples.written();
	}

	size_t Engine::sampleRoom() const
	{
		return m_state->samples.room();
	}

	const std::vector<uint16_t>& Engine::sampleCodes() const
	{
		return m_state->samples.codes();
	}

	void Engine::writeSamples()
	{
		m_state->samples.write(m_state->engine.voices(), Samples::End - Samples::Base);
	}

	static_assert(Engine::MasterDelay == md::engine::MasterEngine::MainDelay);
	static_assert(Engine::MasterCount == md::engine::HostModel<>::kMasterParams);

	const std::vector<md::engine::MachineInfo>& Engine::machines() const
	{
		return m_state->engine.os().machines();
	}

	std::shared_ptr<const md::engine::TrackFx::Tables> Engine::fxTables() const
	{
		return m_state->engine.tables();
	}

	void Engine::setMachine(const int _track, const uint8_t _machineId)
	{
		auto& host = m_state->engine.host();
		host.setMachine(_track, _machineId);
		// A NEIGHBOUR User Machine, an effect, works from now on without waiting for a Hit (ticket 32 of the User Machines
		// map)
		if(isNeighbour(_machineId))
			host.start(_track);
	}

	bool Engine::isNeighbour(const uint8_t _machineId) const
	{
		if(m_state->engine.host().playsX20(_machineId))
			return md::engine::x20::hearsTracks(_machineId);
		if(!md::engine::isUserMachine(_machineId))
			return false;
		const auto& package = m_state->userMachines[static_cast<size_t>(_machineId - FirstUserMachine)];
		return package && (package->flags & md::mds::FlagNeighbour);
	}

	void Engine::installUserMachine(const int _index, std::shared_ptr<const md::mds::Package> _package)
	{
		m_state->engine.installUserMachine(_index, _package);
		m_state->userMachines[static_cast<size_t>(_index)] = std::move(_package);
		// The Tracks that played this number went silent until their next Hit (VoiceEngine::forgetUserMachine); a NEIGHBOUR
		// starts again at once
		const auto id = static_cast<uint8_t>(FirstUserMachine + _index);
		auto& host = m_state->engine.host();
		for(int t = 0; t < TrackCount && isNeighbour(id); ++t)
			if(host.machine(t) == id)
				host.start(t);
	}

	void Engine::setSources(const int _track, const Sources& _sources)
	{
		m_state->engine.voices().setSources(_track, _sources);
	}

	const Engine::Sources& Engine::sources(const int _track) const
	{
		return m_state->engine.voices().sources(_track);
	}

	void Engine::removeUserMachine(const int _index)
	{
		m_state->engine.removeUserMachine(_index);
		if(_index >= 0 && _index < static_cast<int>(m_state->userMachines.size()))
			m_state->userMachines[static_cast<size_t>(_index)].reset();
	}

	std::shared_ptr<const md::mds::Package> Engine::userMachine(const int _index) const
	{
		return _index >= 0 && _index < static_cast<int>(m_state->userMachines.size())
			? m_state->userMachines[static_cast<size_t>(_index)] : nullptr;
	}

	void Engine::installX20(std::shared_ptr<const md::fw::DspImage> _voice)
	{
		m_state->engine.installX20Builtins(std::move(_voice), md::engine::x20::kAll);
		// An NFX a Track already plays starts at once, as a NEIGHBOUR User Machine does
		auto& host = m_state->engine.host();
		for(int t = 0; t < TrackCount; ++t)
			if(isNeighbour(host.machine(t)))
				host.start(t);
	}

	bool Engine::playsX20(const uint8_t _id) const
	{
		return m_state->engine.host().playsX20(_id);
	}

	std::optional<std::array<uint8_t, 8>> Engine::machineDefaults(const uint8_t _id) const
	{
		if(playsX20(_id))
			return md::engine::x20::info(_id).defaults;
		if(md::engine::isUserMachine(_id))
		{
			std::array<uint8_t, 8> defaults{};
			if(const auto& package = m_state->userMachines[_id - FirstUserMachine])
				for(size_t p = 0; p < package->parameters.size() && p < defaults.size(); ++p)
					defaults[p] = package->parameters[p].defaultValue;
			return defaults;
		}
		if(const auto* m = machine(_id))
			return m->defaults;
		return std::nullopt;
	}

	const md::engine::MachineInfo* Engine::machine(const uint8_t _id) const
	{
		return m_state->engine.os().machine(_id);
	}

	const md::engine::MachineInfo* Engine::findMachine(const std::vector<md::engine::MachineInfo>& _machines,
		const uint8_t _id)
	{
		const auto it = std::find_if(_machines.begin(), _machines.end(),
			[&](const md::engine::MachineInfo& _m) { return _m.id == _id; });
		return it == _machines.end() ? nullptr : &*it;
	}

	std::array<uint16_t, Engine::ParamCount> Engine::played(const int _track) const
	{
		std::array<uint16_t, ParamCount> words{};
		if(_track < 0 || _track >= TrackCount)
			return words;
		const auto* voice = m_state->engine.host().voiceParams(_track);
		std::copy_n(voice, ParamCount, words.begin());
		return words;
	}

	void Engine::setParam(const int _track, const int _param, const int _value)
	{
		m_state->engine.host().setParam(_track, _param, _value);
	}

	void Engine::knob(const int _track, const int _param, const int _value)
	{
		m_state->engine.host().knob(_track, _param, _value);
	}

	void Engine::lock(const int _track, const int _param, const int _value)
	{
		m_state->engine.host().lock(_track, _param, _value);
	}

	void Engine::accent(const int _track, const int _amount)
	{
		m_state->engine.host().accent(_track, _amount);
	}

	void Engine::slide(const int _track, const int _clocks)
	{
		m_state->engine.host().slide(_track, _clocks);
	}

	void Engine::extendSlide(const int _track, const int _times127)
	{
		m_state->engine.host().extendSlide(_track, _times127);
	}

	void Engine::slideTarget(const int _track, const int _param, const int _value)
	{
		m_state->engine.host().slideTarget(_track, _param, _value);
	}

	void Engine::clock()
	{
		m_state->engine.host().clock();
	}

	void Engine::setLevel(const int _track, const int _level)
	{
		m_state->engine.host().setLevel(_track, _level);
	}

	void Engine::setMute(const int _track, const bool _mute)
	{
		m_state->engine.host().setMute(_track, _mute);
	}

	void Engine::setLink(const int _track, const int _target)
	{
		if(_track >= 0 && _track < TrackCount)
			m_state->engine.host().setLink(_track, _target);
	}

	void Engine::setChoke(const int _track, const int _target)
	{
		if(_track >= 0 && _track < TrackCount)
			m_state->engine.host().setChoke(_track, _target);
	}

	void Engine::setLfo(const int _track, const md::LfoSettings& _lfo)
	{
		if(_track >= 0 && _track < TrackCount)
			m_state->engine.host().setLfo(_track, _lfo.track, _lfo.parameter, _lfo.shape1, _lfo.shape2, _lfo.update);
	}

	void Engine::loadLfo(const int _track, const uint8_t* _block)
	{
		static_assert(LfoBytes == md::engine::HostModel<>::kLfoBytes);
		if(_track >= 0 && _track < TrackCount)
			m_state->engine.host().loadLfo(_track, _block);
	}

	void Engine::setTempo(const double _bpm)
	{
		m_state->engine.host().setTempo(_bpm);
	}

	uint32_t Engine::trigger(const int _track, const int _velocity)
	{
		if(_track < 0 || _track >= TrackCount)
			return 0;
		auto& state = *m_state;
		const auto struck = state.engine.host().trigger(_track, std::clamp(_velocity, 1, 127));
		if(!state.sampleAccurate)
			return struck;

		// The Hit lands BlockSize - used samples from now: each Track it struck, its own and its Link's, waits the rest
		// of the 31, then what its machine's voice is quicker than the latest
		// With ANTI-CLICK, a Hit that waits for the fade keeps that delay for the block it fires in, and every other one
		// waits as long as the fade
		const auto delay = static_cast<int>(state.used) - 1;
		auto& host = state.engine.host();
		for(int t = 0; t < TrackCount; ++t)
		{
			if(!(struck & (1u << t)))
				continue;
			if(host.holds(t))
			{
				const auto id = host.pendingOrMachine(t);
				state.heldDelay[t] = delay + std::max(0, AlignedStart - voiceStart(id));
				continue;
			}
			state.pending[t] = delay + std::max(0, AlignedStart - voiceStart(host.machine(t))) + state.fade;
		}
		return struck;
	}

	void Engine::setFade(const int _samples)
	{
		const int fade = _samples >= 128 ? 128 : _samples >= 64 ? 64 : 0;
		auto& state = *m_state;
		state.fade = fade;
		state.engine.host().setFadeBlocks(fade / BlockSize);
	}

	int Engine::fade() const
	{
		return m_state->fade;
	}

	int Engine::voiceStart(const uint8_t _machine)
	{
		// Measured on each machine's own output, a Hit again at velocity 127 with the Kit's defaults (mdDrumsEngineTest's
		// "voice starts"). The voices that start a block late are held back the least: TRX-BD and TRX-S2, and the ROM
		// machines; most voices start 4 to 8 samples after their block, which their own sound sets (a noise, an
		// attack), and count as 4. TRX-XT (40), TRX-XC (68) and TRX-CB (102) start later than TRX-BD, which sets
		// AlignedStart: they are not held back and sound that much later, as on the Machinedrum. A machine's first Hit
		// after it is chosen can start later still (P-I ML 260 samples, TRX-MA 27), its sound again.
		switch(_machine)
		{
		case 16:	// TRX-BD
		case 29:	// TRX-S2
			return AlignedStart;
		case 18:	// TRX-XT
			return 40;
		case 21:	// TRX-CB
			return 102;
		case 27:	// TRX-XC
			return 68;
		default:
			// ROM-01 to -32, then ROM-33 to -48
			return (_machine >= 128 && _machine < 160) || (_machine >= 176 && _machine < 192) ? 12 : CommonVoiceStart;
		}
	}

	void Engine::setSampleAccurate(const bool _on)
	{
		auto& state = *m_state;
		state.sampleAccurate = _on;
		if(_on)
			return;
		state.delay.fill(0);
		state.pending.fill(-1);
	}

	bool Engine::isSampleAccurate() const
	{
		return m_state->sampleAccurate;
	}

	void Engine::setSeparateOutputs(const uint32_t _tracks)
	{
		m_state->engine.dryMute = _tracks & ((1u << TrackCount) - 1);
	}

	void Engine::render(float* _left, float* _right, const size_t _count)
	{
		std::array<float*, OutputCount> outputs{};
		outputs[0] = _left;
		outputs[1] = _right;
		render(outputs.data(), _count);
	}

	void Engine::render(float* const* _outputs, const size_t _count)
	{
		auto& state = *m_state;
		constexpr float scale = 1.0f / 8388608.0f;
		size_t done = 0;
		while(done < _count)
		{
			if(state.used == BlockSize)
			{
				state.renderBlock();
				state.used = 0;
			}
			const auto count = std::min(_count - done, BlockSize - state.used);
			for(int channel = 0; channel < 2; ++channel)
			{
				if(auto* output = _outputs[channel])
					for(size_t i = 0; i < count; ++i)
						output[done + i] = static_cast<float>(state.main[state.used + i][channel]) * scale;
			}
			for(int track = 0; track < TrackCount; ++track)
			{
				auto* output = _outputs[2 + track];
				if(!output)
					continue;
				const auto& samples = state.solo[track];
				for(size_t i = 0; i < count; ++i)
					output[done + i] = static_cast<float>(samples[state.used + i]) * scale;
			}
			state.used += count;
			done += count;
		}
	}

	void applyKit(Engine& _engine, const md::automation::sysex::MdKit& _kit)
	{
		using md::automation::sysex::MdKit;
		static_assert(MdKit::ParameterCount == Engine::ParamCount && MdKit::LfoSize == Engine::LfoBytes
			&& MdKit::MasterEffectsSize == Engine::MasterCount);
		for(uint8_t track = 0; track < Engine::TrackCount; ++track)
		{
			_engine.setMachine(track, _kit.machine(track));
			for(uint8_t parameter = 0; parameter < MdKit::ParameterCount; ++parameter)
				_engine.setParam(track, parameter, _kit.parameters[track][parameter]);
			_engine.setLevel(track, _kit.levels[track]);
			_engine.loadLfo(track, _kit.lfos[track].data());
			// MdKit::Off is past the Tracks, which switches a Link or a Choke off
			_engine.setLink(track, _kit.links[track]);
			_engine.setChoke(track, _kit.chokes[track]);
		}
		// The dump keeps the master effects in the engine's order
		for(int index = 0; index < Engine::MasterCount; ++index)
			_engine.setMaster(index, _kit.masterEffects[static_cast<size_t>(index)]);
	}
}
