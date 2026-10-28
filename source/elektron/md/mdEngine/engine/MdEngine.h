// Machinedrum One's engine: the OS's own control code (HostModel + MachineRunner), the voice DSP (VoiceEngine,
// emulated) and the mixer DSP's per-track effects and mix as native C++ (TrackFx, Mixer). One 32-sample block
// at a time: all 16 tracks, the dry main mix, the reverb/delay sends and each track's own output.
#pragma once
#include <algorithm>
#include <chrono>
#include <array>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "HostModel.h"
#include "MachineRunner.h"
#include "MasterEngine.h"
#include "Mixer.h"
#include "TrackFx.h"
#include "VoiceEngine.h"

namespace md::engine
{
	// TVoices: VoiceEngine, the one DSP2 instance that renders the 16 voices. A template as upstream has it, defined
	// inline here (MdEngine.h, not Engine.h: macOS file systems ignore case and that would be the MPC wrapper's
	// engine.h). md-drums: upstream's other TVoices, ParallelVoiceEngine (the 16 voices split across threads), is
	// left out: measured and not worth it (ticket 25 of MD Drums' editor map).
	template<class TVoices = VoiceEngine>
	class EngineT
	{
	public:
		static constexpr int kTracks = 16;
		static constexpr int kBlock = 32;

		// _osImage: the OS file's section 0 (the ColdFire OS), decompressed.
		EngineT(const fw::Firmware& _fw, std::vector<uint8_t> _osImage)
			: m_tables(std::make_shared<const TrackFx::Tables>(_fw)), m_mixer(*m_tables)
		{
			m_os = std::make_unique<MachineRunner>(std::move(_osImage));
			m_voices = std::make_unique<TVoices>(_fw);
			init();
		}

		~EngineT() = default;

		HostModel<TVoices>& host() { return *m_host; }
		const MachineRunner& os() const { return *m_os; }
		// md-drums: the OS's tables that TrackFx and the Mixer read, immutable, for whoever runs a TrackFx of its own
		const std::shared_ptr<const TrackFx::Tables>& tables() const { return m_tables; }
		TVoices& voices() { return *m_voices; }

		// md-drums: the UW sample bank the ROM machines play (fw::loadRomBankFromFlash). The OS's boot copies it from
		// the flash into the voice DSP; this voice DSP starts without it, and every ROM machine is silent until then.
		void loadRomBank(const fw::RomBank& _bank)
		{
			for(const auto& entry : _bank.directory)
				m_voices->writeP(entry.addr, entry.words, std::size(entry.words));
			m_voices->writeP(fw::RomBank::DataAddr, _bank.data.data(), _bank.data.size());
		}

		// md-drums: every audio machine's voice code compiled now rather than at its first Hit. The voice DSP's JIT compiles
		// a machine's code the first time a voice runs it: 1.5 to 8 ms in one 32-sample block (mdDrumsFirstHitTest), more
		// than an audio thread at 128 samples has. Each audio machine but RAM-R and RAM-P plays a Hit at its default
		// SYN1-8 for _blocks blocks on the voice DSP alone, its words computed by a copy of the OS; the DSP then gets its
		// memory and registers back (VoiceEngine::saveState), so that the engine sounds as if nothing had run. Call it
		// after loadRomBank, before the first render. Returns the machines run, or -1 if the voice DSP faulted.
		int warmUpVoices(const int _blocks = 4)
		{
			const auto state = m_voices->saveState();
			const auto os = m_os->clone();
			std::vector<const MachineInfo*> machines;
			for(const auto& m : os->machines())
				if(isAudioMachine(m.id) && m.name.rfind("RAM", 0) != 0)
					machines.push_back(&m);
			typename TVoices::Block block;
			bool ok = true;
			for(size_t first = 0; first < machines.size() && ok; first += kTracks)
			{
				for(int b = 0; b < _blocks && ok; ++b)
				{
					for(size_t v = 0; v < static_cast<size_t>(kTracks) && first + v < machines.size(); ++v)
					{
						const auto& machine = *machines[first + v];
						// SYN1-8: the machine reads no other
						std::array<uint16_t, 8> params{};
						for(size_t p = 0; p < 8; ++p)
							params[p] = static_cast<uint16_t>(machine.defaults[p] << 7);
						uint32_t out[32];
						const int n = os->compute(machine.id, params.data(), out, 32, b == 0);
						if(n < 0)
							continue;
						out[0] = b == 0 ? static_cast<uint32_t>(machine.id) + 1 : 0;
						m_voices->setSlot(static_cast<int>(v), out, std::clamp(n, 1, static_cast<int>(TVoices::kSlotWords)));
					}
					ok = m_voices->renderBlock(block);
				}
			}
			m_voices->restoreState(*state);
			return ok ? static_cast<int>(machines.size()) : -1;
		}

		// md-drums: a User Machine at USR-01 to USR-16 (_index 0-15): its code into the voice DSP, its package to the host
		// model, then compiled as warmUpVoices compiles the built-in Machines (a Hit at its defaults on the voice DSP alone,
		// which then gets its memory and registers back). Never while render runs. Throws on a package the voice DSP cannot
		// play (VoiceEngine::installUserMachine).
		void installUserMachine(const int _index, std::shared_ptr<const md::mds::Package> _package, const int _blocks = 4)
		{
			m_voices->installUserMachine(_index, _package);
			m_host->setUserMachine(_index, _package);
			const auto state = m_voices->saveState();
			uint32_t words[9] = {};
			for(size_t p = 0; p < _package->parameters.size() && p < 8; ++p)
				words[1 + p] = md::mds::convert(_package->parameters[p], _package->parameters[p].defaultValue,
					m_host->decayTable());
			typename TVoices::Block block;
			bool ok = true;
			for(int b = 0; b < _blocks && ok; ++b)
			{
				words[0] = b == 0 ? VoiceEngine::kFirstUserType + static_cast<uint32_t>(_index) : 0;
				m_voices->setSlot(0, words, 9);
				ok = m_voices->renderBlock(block);
			}
			m_voices->restoreState(*state);
			if(!ok)
				throw std::runtime_error("voice DSP fault while compiling " + _package->displayName());
		}
		void removeUserMachine(const int _index)
		{
			m_voices->removeUserMachine(_index);
			m_host->setUserMachine(_index, nullptr);
		}

		// md-drums: MDX X.20's built-in Machines in _ids (a bit per x20::Machine id) from the user's X.20 OS, _voice its
		// voice DSP program (fw::parseX20Os): their code into the voice DSP, the host model computing their words, then
		// compiled as warmUpVoices compiles the others (a Hit at its defaults on the voice DSP alone, which then gets its
		// memory and registers back). Never while render runs. None, or no id, takes them back.
		void installX20Builtins(std::shared_ptr<const fw::DspImage> _voice, const uint32_t _ids, const int _blocks = 4)
		{
			const uint32_t ids = _voice ? _ids : 0;
			m_voices->installBuiltins(std::move(_voice), ids);
			m_host->setX20Builtins(ids);
			const auto state = m_voices->saveState();
			typename TVoices::Block block;
			bool ok = true;
			for(uint8_t id = 0; id < 32 && ok; ++id)
			{
				if(!((ids >> id) & 1) || !x20::isBuiltin(id))
					continue;
				std::array<uint16_t, 8> params{};
				for(size_t p = 0; p < 8; ++p)
					params[p] = static_cast<uint16_t>(x20::info(id).defaults[p] << 7);
				for(int b = 0; b < _blocks && ok; ++b)
				{
					uint32_t words[11] = {};
					const int n = x20::compute(id, params.data(), 0, m_host->decayTable(), HostModel<TVoices>::kX20Tempo24,
						words + 1);
					words[0] = b == 0 ? static_cast<uint32_t>(id) + 1 : 0;
					m_voices->setSlot(0, words, n + 1);
					ok = m_voices->renderBlock(block);
				}
			}
			m_voices->restoreState(*state);
			if(!ok)
				throw std::runtime_error("voice DSP fault while compiling X.20's built-in Machines");
		}

		// md-drums, not bit-exact: a voice that sounded and went quiet stops rendering until its next Hit or a change of its
		// words (VoiceEngine::setQuietSkip), on every audio machine of the OS but those that retrigger themselves (RTRG:
		// E12, ROM) and RAM-R and -P. Off, as the Machinedrum, until this is called.
		void setQuietSkip(const bool _on)
		{
			std::bitset<256> types;
			for(const auto& m : m_os->machines())
			{
				const bool retriggers = std::find(m.params.begin(), m.params.end(), "RTRG") != m.params.end();
				if(_on && isAudioMachine(m.id) && m.name.rfind("RAM", 0) != 0 && !retriggers)
					types.set(m.id + 1u);
			}
			m_voices->setQuietSkip(types);
		}

		// md-drums: each voice's 32 samples of the last block, before its Track's effects (for the tests)
		const typename TVoices::Block& voiceBlock() const { return m_voiceOut; }

		struct Output
		{
			Mixer::Output mix;													// main, sends, individual outputs
			std::array<std::array<int32_t, kBlock>, kTracks> tracks{};			// each track after its effects
			Mixer::Stereo master{};		// md-drums: Main after the master effects (enableMaster), MasterEngine::MainDelay late
		};

		// md-drums: the master effects (MasterEngine), off until this is called (it allocates the mixer DSP, ~9 MB, and
		// runs its init). With the mixer on, render() then runs them on its buses into Output::master; without it, the
		// caller runs them on its own buses (processMaster). Either way the OS's parameter words of each tick reach them.
		void enableMaster(const fw::Firmware& _fw)
		{
			if(!m_master)
				m_master = std::make_unique<MasterEngine>(_fw);
		}
		bool masterOn() const { return m_master != nullptr; }
		MasterEngine* masterEngine() { return m_master.get(); }
		bool processMaster(const Mixer::Stereo& _main, const Mixer::Stereo& _rev, const Mixer::Stereo& _del, Mixer::Stereo& _out)
		{
			if(!m_master)
				return false;
			const auto tm0 = timingOn ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
			const bool ok = m_master->process(_main, _rev, _del, _out);
			if(timingOn)
				masterUs += std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - tm0).count();
			return ok;
		}

		uint32_t dryMute = 0;	// tracks kept out of the dry main mix (see Mixer::process); set by the caller between renders
		// md-drums: false leaves Output::mix untouched, for a caller that mixes Output::tracks itself (mixer())
		bool mixOn = true;
		const Mixer& mixer() const { return m_mixer; }
		// Optional stage timing (microseconds, accumulated): the master effects. The OS tick and the voice DSP are
		// HostModel's (host().tickUs / dspUs).
		bool timingOn = false;
		double masterUs = 0;

		// One track's effects: a settled silent track's chain gives the same again while its input stays silent and
		// its state (which holds its params) stays the same, so it is skipped. Exact - see HANDOFF.md, "fixed
		// per-block cost".
		void fxTrack(const int _t, const int32_t* _src, int32_t* _dst)
		{
			TrackFx::setParams(m_state[_t], m_host->mixerInput(_t).fx.data());
			const bool silentIn = std::all_of(_src, _src + kBlock, [](int32_t s) { return s == 0; });
			if(silentIn && m_settled[_t] && m_state[_t].y == m_settledState[_t].y)
			{
				std::fill(_dst, _dst + kBlock, 0);
				return;
			}
			const auto before = m_state[_t];
			m_fx->process(m_state[_t], _src, _dst);
			m_settled[_t] = silentIn && m_state[_t].y == before.y && std::all_of(_dst, _dst + kBlock, [](int32_t s) { return s == 0; });
			if(m_settled[_t])
				m_settledState[_t] = m_state[_t];
		}

		bool render(Output& _out)
		{
			m_host->timingOn = timingOn;
			if(!m_host->renderBlock(m_voiceOut))
				return false;
			// md-drums: the master words the tick computed, before the master runs on this block (the OS sends them while
			// the voice DSP renders, so they land on the mixer DSP between two of its blocks here)
			if(m_master)
			{
				typename HostModel<TVoices>::MasterWords words;
				if(m_host->takeMasterWords(words))
					m_master->setWords(words.y, words.words.data(), words.count);
			}
			for(int t = 0; t < kTracks; ++t)
				fxTrack(t, m_voiceOut[t].data(), _out.tracks[t].data());
			// md-drums: without the mixer (mixOn), each track's output is all there is
			if(!mixOn)
				return true;
			std::array<const int32_t*, kTracks> in{};
			std::array<std::array<uint32_t, 5>, kTracks> mix{};
			for(int t = 0; t < kTracks; ++t)
			{
				in[t] = _out.tracks[t].data();
				mix[t] = m_host->mixerInput(t).mix;
			}
			m_mixer.process(in.data(), mix.data(), _out.mix, dryMute);
			return !m_master || processMaster(_out.mix.main, _out.mix.rev, _out.mix.del, _out.master);
		}

		const std::string& fault() const
		{
			return m_master && !m_master->faultReason().empty() ? m_master->faultReason() : m_voices->faultReason();
		}

	private:
		void init()
		{
			m_host = std::make_unique<HostModel<TVoices>>(*m_os, *m_voices);
			for(auto& s : m_state)
				TrackFx::init(s);
			m_fx = std::make_unique<TrackFx>(*m_tables);
		}

		std::unique_ptr<MachineRunner> m_os;
		std::unique_ptr<TVoices> m_voices;
		std::unique_ptr<HostModel<TVoices>> m_host;
		std::shared_ptr<const TrackFx::Tables> m_tables;	// md-drums: shared, so that the editor measures with them (tables())
		std::unique_ptr<TrackFx> m_fx;
		Mixer m_mixer;
		std::unique_ptr<MasterEngine> m_master;	// md-drums: the master effects, once enabled
		std::array<TrackFx::State, kTracks> m_state{};
		std::array<TrackFx::State, kTracks> m_settledState{};
		std::array<bool, kTracks> m_settled{};
		typename TVoices::Block m_voiceOut{};
	};

	using Engine = EngineT<VoiceEngine>;
}
