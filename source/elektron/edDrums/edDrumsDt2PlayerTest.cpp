// The Sampler's DT2 player (tickets 04, 05, 11 and 13 of .scratch/elektron-drums-v2) against the Digitakt II's voice
// run in its OS (DT2_VECTORS, the folder dt2_voice_oracle.py and its siblings wrote: per case the sample, the voice's
// 48 kHz output and its settings), on every case:
//   - the player's output, scaled by the oracle's DC gain (STRETCH's voice has no kernel, no gain), nulled against the
//     voice column: SNR at least the bound each case states (the Hits 45 dB, the sines 45, the broadband cases 27: our
//     own kernel, fitted, not the firmware's; STRETCH 60, its grains read linearly as the DSP's);
//   - Q4 on top: the 1 ms envelope within 0.1 dB where the oracle is above its peak - 40 dB, a sine's pitch within 1 cent;
//   - 16 lanes on the same case give lane 0's samples, and 4 lanes rendered alone give those of 16;
//   - Q15: the same Hits at 44.1 kHz through Host against 48 kHz: 10 ms envelopes within 0.2 dB, third octaves to
//     16 kHz within 0.5 dB;
//   - the cost of 16 lanes playing, in ONESHOT and in STRETCH, printed.
// Exit 77 without the vectors, once the rest has run.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "edDrumsDt2Player.h"
#include "stFastMetrics.h"
#include "stFastResample.h"

namespace
{
	using namespace elektron::drums;
	using elektron::Block;
	using elektron::BlockSize;
	using elektron::Lanes;
	namespace bench = syntakt::fast::bench;

	void require(const bool _condition, const std::string& _message)
	{
		if(!_condition)
			throw std::runtime_error(_message);
	}

	std::string readText(const std::string& _path)
	{
		std::ifstream f(_path, std::ios::binary);
		return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
	}

	template<typename T>
	std::vector<T> readRaw(const std::string& _path)
	{
		std::ifstream f(_path, std::ios::binary);
		require(f.good(), "cannot read " + _path);
		std::vector<char> bytes{std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
		std::vector<T> v(bytes.size() / sizeof(T));
		std::memcpy(v.data(), bytes.data(), v.size() * sizeof(T));
		return v;
	}

	// A number after "key": in the oracle's JSON (flat enough for that)
	double number(const std::string& _json, const char* _key)
	{
		const auto at = _json.find("\"" + std::string(_key) + "\":");
		require(at != std::string::npos, std::string("no ") + _key + " in the case");
		return std::atof(_json.c_str() + at + std::strlen(_key) + 3);
	}

	struct Case
	{
		const char* name;
		double snrBound;		// dB against the oracle's voice column
		bool sine;				// its pitch checked
		// A reversed read from a start inside the sample: its last block, faded, reads below that start something the
		// oracle's probes have not identified (under the fade a near constant ~0.11, neither the sample, nor zeros, nor
		// the slice's end): judged up to that block, its length checked
		bool openLastBlock = false;
		// WERP looped (FWD.L, REV.L): after a step, while the read lies outside the window it moved, the oracle adds
		// something not identified (+0.12 dB on a sine, -18 dB of the signal on noise, ramped in and out): judged up to
		// the first step, which falls in that block
		int beforeBlock = 0;
	};

	// The ONESHOT cases: what each exercises is in vectors-dt2's README. The lev and vel cases play the unison Hit
	constexpr Case Cases[]{
		{"os_hit_unison", 45, false}, {"os_hit_up12", 45, false}, {"os_hit_down12", 45, false}, {"os_hit_note72", 45, false},
		{"os_hit_strt30_len60", 45, false}, {"os_hit_reverse", 45, false}, {"os_hit_loop_len30", 45, false},
		{"os_hit_lev127", 45, false}, {"os_hit_vel64_lin", 45, false},
		{"os_sine1k_unison", 45, true}, {"os_sine1k_up12", 45, true}, {"os_sine1k_down12", 45, true},
		{"os_noise_tune_frac", 27, false}, {"os_impulse_unison", 27, false},
		// REPITCH (ticket 05): the portion over 2^BARS bars at the tempo, TUNE and the note not read
		{"rp_hit_ratio1", 45, false}, {"rp_hit_ratio05", 45, false}, {"rp_hit_bars1", 45, false},
		{"rp_hit_strt30_len60", 45, false}, {"rp_hit_tune76", 45, false}, {"rp_hit_note72", 45, false}, {"rp_hit_reverse_len30", 45, false},
		// GRID (ticket 05): 4 << G slices, the slice played and the ones after it
		{"gr_hit_g4_i1_l0", 45, false}, {"gr_hit_g4_i2_l0", 45, false}, {"gr_hit_g4_i4_l0", 45, false}, {"gr_hit_g4_i2_l1", 45, false},
		{"gr_hit_g4_i1_l3", 45, false}, {"gr_hit_g16_i5_up12", 45, false}, {"gr_hit_g16_i5_l0", 45, false},
		{"gr_hit_g4_i2_rev", 45, false, true}, {"gr_hit_g4_i3_rev", 45, false, true}, {"gr_hit_g16_i9_rev", 45, false, true},
		{"gr_hit_g4_i2_rev_up12", 45, false, true}, {"os_hit_strt30_rev", 45, false, true},
		// WERP (ticket 11): the ColdFire's sequencer and the SHARC's voice composed at the oracle, a window a segment
		// long stepping at the tempo; the voice stops where its read passes the window's end
		{"wp_fwd_match", 45, false}, {"wp_fwd_short", 45, false}, {"wp_fwd_up12", 45, false},
		{"wp_fwd_down12", 45, false}, {"wp_fwd_seg2_bars1", 45, false}, {"wp_fwd_seg1_bars2", 45, false},
		{"wp_rev_sine", 45, true, true}, {"wp_fwd_noise_match", 27, false},
		{"wp_fwdl_sine", 45, true, false, 38}, {"wp_fwdl_short", 45, false, false, 38},
		// A reversed loop's wrap: the block that goes round differs from the oracle's (6 dB on the sine, 22 dB on the
		// noise, against 30 around it) whatever the lag (ReverseLoopLag, the best of 0 to 7): not identified; the blocks
		// between agree to the forward cases'
		{"wp_revl_sine", 20, true, false, 38},
		// STRETCH (ticket 13): grains at the tempo, at TUNE's pitch, at the output rate without the kernel. The oracle's
		// products are floored (a sample 2 LSB low where positive), which a quiet Hit shows (its reversed tail, ~20 LSB,
		// or its middle, ~570 LSB, at STRT 60): those cases play noise
		{"st_hit_ratio1", 60, false}, {"st_hit_ratio05", 60, false}, {"st_hit_ratio1_up12", 60, false},
		{"st_hit_down12", 60, false}, {"st_hit_note72_bars2", 60, false}, {"st_hit_ratio25", 60, false},
		{"st_hit_ratio_clamp4", 60, false}, {"st_noise_frac", 60, false}, {"st_noise_rev", 60, false},
		{"st_hit_short_end", 60, false}, {"st_hit_short_rev", 60, false}, {"st_noise_strt60_len6", 60, false},
		{"st_noise_strt60_len6_rev", 60, false}, {"st_hit_short_loop", 60, false}, {"st_hit_short_revloop", 60, false},
		{"st_noise_short_loop_frac", 60, false}, {"st_noise_short_revloop_frac", 60, false},
		{"st_noise_strt60_len6_loop", 60, false},
		// SLICE (ticket 12): the portion the ColdFire hands the SHARC in frames, which the Track's slices give
		{"sl_hit_0_1200", 45, false}, {"sl_hit_1200_2400", 45, false}, {"sl_hit_2400_4800", 45, false},
		{"sl_hit_1037_3011", 45, false}, {"sl_hit_1200_2400_up12", 45, false}, {"sl_hit_1200_2400_note48", 45, false},
		{"sl_hit_1200_2400_rev", 45, false, true}, {"sl_hit_1200_2400_loop", 45, false},
		// Its REV LOOP wraps as WERP's REV.L, ReverseLoopLag below its start (5, the best of 0 to 12 here too); the
		// blocks that go round differ as WERP's do, not identified: 28.5 dB over its three wraps
		{"sl_hit_1200_2400_revloop", 25, false}, {"sl_hit_4700_4800", 45, false}, {"sl_noise_1037_3011", 27, false},
	};

	struct Settings
	{
		double tune = 64, note = 60, strt = 0, len = 120, loopPos = 0, bpm = 120;
		bool reverse = false, loop = false;
		// selector 0 ONESHOT, 1 WERP, 2 STRETCH, 3 REPITCH, 4 GRID, 5 SLICE; bars BARS, or GRID's G
		int blocks = 0, selector = 0, bars = 0;
		// WERP: PLAY as the Digitakt II numbers it (0 REV, 1 REV.L, 2 FWD.L, 3 FWD), SEG and BARS
		int werpPlay = 3, werpSegment = 0, werpBars = 0;
		// SLICE: the portion the ColdFire hands the SHARC, in frames; its loop point is its start in every case
		uint32_t sliceFrom = 0, sliceTo = 0;
		int slice = 1, slices = 1;		// the Track's slices that give it (load), and the one played
	};

	Settings settingsOf(const std::string& _json)
	{
		Settings s;
		s.tune = number(_json, "tune");
		s.note = number(_json, "note");
		s.strt = number(_json, "strt");
		s.len = number(_json, "len");
		s.loopPos = number(_json, "loop_pos");
		s.reverse = number(_json, "reverse") != 0;
		s.loop = number(_json, "loop") != 0;
		s.blocks = static_cast<int>(number(_json, "blocks"));
		s.selector = static_cast<int>(number(_json, "selector"));
		s.bars = static_cast<int>(number(_json, "bars"));
		s.bpm = number(_json, "bpm");
		require(s.selector >= 0 && s.selector <= 5, "not an ONESHOT, WERP, STRETCH, REPITCH, GRID or SLICE case");
		if(s.selector == 5)
		{
			const auto at = _json.find("\"slice\":");
			require(at != std::string::npos, "a SLICE case without its portion");
			const char* c = _json.c_str() + _json.find('[', at) + 1;
			char* next = nullptr;
			const double from = std::strtod(c, &next), to = std::strtod(next + 1, &next);
			const double loopAt = std::strtod(next + 1, &next);
			require(loopAt == from, "a SLICE case whose loop point is not its start");
			s.sliceFrom = static_cast<uint32_t>(from);
			s.sliceTo = static_cast<uint32_t>(to);
		}
		if(s.selector == 1)
		{
			const auto werp = _json.substr(_json.find("\"werp\":"));
			s.werpPlay = static_cast<int>(number(werp, "play"));
			s.werpSegment = static_cast<int>(number(werp, "seg"));
			s.werpBars = static_cast<int>(number(werp, "bars"));
		}
		return s;
	}

	// A knob of the oracle's 0..120 as the player's word: the fraction over the 15 bits of value and fine
	void setFraction(Dt2Player& _p, const int _lane, const int _index, const double _knob)
	{
		const auto word = static_cast<int>(std::lround(_knob / 120 * (127 << 8)));
		_p.setParameter(_lane, _index, static_cast<uint8_t>(word >> 8), static_cast<uint8_t>(word & 255));
	}

	void apply(Dt2Player& _p, const int _lane, const Settings& _s)
	{
		const double tune = _s.tune;
		const auto value = static_cast<int>(std::floor(tune));
		_p.setParameter(_lane, Dt2Player::Tune, static_cast<uint8_t>(value), static_cast<uint8_t>(std::lround((tune - value) * 256)));
		_p.setNote(_lane, static_cast<int>(_s.note));
		_p.setTempo(_s.bpm);
		int play = _s.reverse ? (_s.loop ? Dt2Player::ReverseLoop : Dt2Player::Reverse)
			: (_s.loop ? Dt2Player::ForwardLoop : Dt2Player::Forward);
		if(_s.selector == 1)
		{
			static constexpr int Plays[]{Dt2Player::Reverse, Dt2Player::ReverseLoop, Dt2Player::ForwardLoop, Dt2Player::Forward};
			play = Plays[_s.werpPlay];
		}
		_p.setParameter(_lane, Dt2Player::Play, static_cast<uint8_t>(play * 32), 0);
		// MODE a sixth of the range each (Dt2Player::modeOf): ONESHOT 0, GRID 1, REPITCH 2, WERP 3, STRETCH 5
		const int mode = _s.selector == 1 ? Dt2Player::Werp : _s.selector == 2 ? Dt2Player::Stretch
			: _s.selector == 3 ? Dt2Player::Repitch : _s.selector == 4 ? Dt2Player::Grid
			: _s.selector == 5 ? Dt2Player::SliceMode : Dt2Player::OneShot;
		_p.setParameter(_lane, Dt2Player::Mode, static_cast<uint8_t>(mode * 128 / 6 + 10), 0);
		if(_s.selector == 1)
		{
			// SEGMENT a third of the range each, BARS a quarter (Dt2Player::werpSegmentOf, werpBarsOf)
			const auto segment = static_cast<uint8_t>(_s.werpSegment * 43 + 1);
			const auto bars = static_cast<uint8_t>(_s.werpBars * 32 + 1);
			require(Dt2Player::werpSegmentOf(segment) == _s.werpSegment && Dt2Player::werpBarsOf(bars) == _s.werpBars,
				"the test's SEGMENT or BARS value does not give the case's");
			_p.setParameter(_lane, Dt2Player::Start, segment, 0);
			_p.setParameter(_lane, Dt2Player::Loop, bars, 0);
			return;
		}
		if(_s.selector == 5)
		{
			// The slice that is the case's portion among the marks load() cut, alone (LEN 0)
			_p.setParameter(_lane, Dt2Player::Start, static_cast<uint8_t>((_s.slice - 1) * 128 / _s.slices + 1), 0);
			_p.setParameter(_lane, Dt2Player::Length, 0, 0);
			require(Dt2Player::sliceOf((_s.slice - 1) * 128 / _s.slices + 1, _s.slices) == _s.slice,
				"the test's SLICE value does not give the case's slice");
			return;
		}
		if(_s.selector == 4)
		{
			// The oracle's strt is the slice (1-based), len the slices after it, bars G: 4 << G slices
			const int slices = 4 << _s.bars;
			require(Dt2Player::gridOf(_s.bars * 26) == slices, "the test's GRID value does not give the case's slices");
			_p.setParameter(_lane, Dt2Player::Loop, static_cast<uint8_t>(_s.bars * 26), 0);
			_p.setParameter(_lane, Dt2Player::Start, static_cast<uint8_t>((static_cast<int>(_s.strt) - 1) * 128 / slices + 1), 0);
			_p.setParameter(_lane, Dt2Player::Length, static_cast<uint8_t>(static_cast<int>(_s.len) * 128 / slices + 1), 0);
			return;
		}
		setFraction(_p, _lane, Dt2Player::Start, _s.strt);
		setFraction(_p, _lane, Dt2Player::Length, _s.len);
		if(_s.selector == 3 || _s.selector == 2)
		{
			// BARS: Dt2Player::barsOf, a fifth of the range each (REPITCH's and STRETCH's)
			const auto value = static_cast<uint8_t>(_s.bars * 26);
			require(Dt2Player::barsOf(value) == _s.bars, "the test's BARS value does not give the case's bars");
			_p.setParameter(_lane, Dt2Player::Loop, value, 0);
			return;
		}
		setFraction(_p, _lane, Dt2Player::Loop, _s.loopPos);
	}

	// _blocks of lane _lane after the trig's block, at the oracle's gain (STRETCH's voice has none: no kernel)
	std::vector<double> render(Dt2Player& _p, const int _lane, const int _blocks, const double _gain = Dt2Player::DcGain)
	{
		std::vector<double> y;
		y.reserve(static_cast<size_t>(_blocks) * BlockSize);
		_p.renderBlock();	// the block that sees the trig
		for(int b = 0; b < _blocks; ++b)
		{
			const Block& out = _p.renderBlock();
			for(int n = 0; n < BlockSize; ++n)
				y.push_back(out.s[n][_lane] * _gain);
		}
		return y;
	}

	double snrOf(const std::vector<double>& _ref, const std::vector<double>& _test)
	{
		double s = 0, r = 0;
		for(size_t i = 0; i < std::min(_ref.size(), _test.size()); ++i)
		{
			s += _ref[i] * _ref[i];
			r += (_test[i] - _ref[i]) * (_test[i] - _ref[i]);
		}
		return 10 * std::log10(s / std::max(r, 1e-30));
	}

	// The sample of a case as the player takes it, held by its vector
	struct Loaded
	{
		std::vector<int16_t> pcm;
		Dt2Samples samples;
		Settings settings;
		std::vector<double> voice;
		std::unique_ptr<Slices> slices = std::make_unique<Slices>();	// a SLICE case's, where the slots point
	};

	Loaded load(const std::string& _folder, const char* _case)
	{
		Loaded l;
		const std::string base = _folder + "/" + _case;
		l.pcm = readRaw<int16_t>(base + ".s16");
		l.settings = settingsOf(readText(base + ".json"));
		const auto f32 = readRaw<float>(base + ".f32");
		require(f32.size() % 3 == 0, "a case's .f32 holds 3 columns");
		l.voice.resize(f32.size() / 3);
		for(size_t n = 0; n < l.voice.size(); ++n)
			l.voice[n] = f32[3 * n];
		// SLICE: marks where the case's portion starts and ends, inside the sample; it is the second slice if it does
		// not start at 0
		auto& s = l.settings;
		const auto frames = static_cast<uint32_t>(l.pcm.size());
		for(const uint32_t mark : {s.sliceFrom, s.sliceTo})
			if(s.selector == 5 && mark > 0 && mark < frames)
				l.slices->marks[static_cast<size_t>(l.slices->count++)] = mark;
		s.slices = l.slices->count + 1;
		s.slice = s.sliceFrom > 0 ? 2 : 1;
		for(auto& slot : l.samples.slots)
			slot = {l.pcm.data(), frames, 48000.0, l.slices.get()};
		return l;
	}

	// ---- against the oracle ----
	void oracleCases(const std::string& _folder)
	{
		std::printf("against the oracle: case, SNR dB (bound), envelope dB, pitch cents\n");
		for(const auto& c : Cases)
		{
			const Loaded l = load(_folder, c.name);
			auto p = std::make_unique<Dt2Player>();
			p->setSamples(&l.samples);
			apply(*p, 0, l.settings);
			p->trig(0);
			auto y = render(*p, 0, l.settings.blocks, l.settings.selector == 2 ? 1.0 : Dt2Player::DcGain);
			auto voice = l.voice;
			if(c.openLastBlock)
			{
				// Both as long, then both cut before the oracle's last sounding block
				int last = -1;
				for(size_t i = 0; i < voice.size(); ++i)
					if(std::abs(voice[i]) > 1e-6)
						last = static_cast<int>(i);
				int lastY = -1;
				for(size_t i = 0; i < y.size(); ++i)
					if(std::abs(y[i]) > 1e-6)
						lastY = static_cast<int>(i);
				require(last / BlockSize == lastY / BlockSize, std::string(c.name) + ": the reversed read stops elsewhere");
				const auto keep = static_cast<size_t>(last / BlockSize * BlockSize);
				voice.resize(keep);
				y.resize(keep);
			}
			if(c.beforeBlock)
			{
				require(voice.size() >= static_cast<size_t>(c.beforeBlock * BlockSize), std::string(c.name) + ": shorter than judged");
				voice.resize(static_cast<size_t>(c.beforeBlock * BlockSize));
				y.resize(voice.size());
			}
			const double snr = snrOf(voice, y);
			const double env = bench::envelopeDiff(bench::envelope(voice, 48000), bench::envelope(y, 48000));
			double cents = 0;
			if(c.sine)
				cents = 1200 * std::log2(bench::peakHz(y, 48000) / bench::peakHz(voice, 48000));
			std::printf("  %-22s %6.1f (%2.0f)  %6.3f  %6.2f\n", c.name, snr, c.snrBound, env, cents);
			if(snr < c.snrBound)
			{
				const auto lastOf = [](const std::vector<double>& _v)
				{
					int last = -1;
					for(size_t i = 0; i < _v.size(); ++i)
						if(std::abs(_v[i]) > 1e-6)
							last = static_cast<int>(i);
					return last;
				};
				std::printf("    last output above -120 dB: oracle %d, player %d\n", lastOf(voice), lastOf(y));
			}
			require(snr >= c.snrBound, std::string(c.name) + ": SNR against the oracle under its bound");
			require(env <= 0.1, std::string(c.name) + ": envelope off by more than 0.1 dB");
			require(std::abs(cents) <= 1, std::string(c.name) + ": pitch off by more than a cent");
		}
	}

	// ---- 16 lanes, 4 lanes ----
	void lanes(const std::string& _folder)
	{
		const Loaded l = load(_folder, "os_hit_unison");
		auto one = std::make_unique<Dt2Player>(), all = std::make_unique<Dt2Player>(), four = std::make_unique<Dt2Player>();
		for(auto* p : {one.get(), all.get(), four.get()})
			p->setSamples(&l.samples);
		apply(*one, 0, l.settings);
		one->trig(0);
		for(int lane = 0; lane < Lanes; ++lane)
		{
			apply(*all, lane, l.settings);
			all->trig(lane);
			if(lane < 4)
			{
				apply(*four, lane, l.settings);
				four->trig(lane);
			}
		}
		four->setLanes(4);
		const int blocks = l.settings.blocks;
		one->renderBlock();
		all->renderBlock();
		four->renderBlock();
		for(int b = 0; b < blocks; ++b)
		{
			const Block& a = one->renderBlock();
			const Block& m = all->renderBlock();
			const Block& f = four->renderBlock();
			for(int n = 0; n < BlockSize; ++n)
			{
				for(int lane = 0; lane < Lanes; ++lane)
					require(m.s[n][lane] == a.s[n][0], "a lane of 16 differs from the lane alone");
				for(int lane = 0; lane < 4; ++lane)
					require(f.s[n][lane] == a.s[n][0], "a lane of 4 differs from the lane alone");
				for(int lane = 4; lane < Lanes; ++lane)
					require(f.s[n][lane] == 0, "a lane left out is not silent");
			}
		}
		std::printf("PASS: 16 lanes and 4 lanes play the samples of 1\n");
	}

	// ---- Q15: 44.1 kHz through Host against 48 kHz ----
	// The case's 16 lanes a semitone apart, from _lowest semitones about its TUNE
	void q15(const std::string& _folder, const char* _case, const int _lowest)
	{
		const Loaded l = load(_folder, _case);
		auto at48 = std::make_unique<Dt2Player>();
		auto at44 = std::make_unique<syntakt::fast::Host<Dt2Player>>(44100.0f);
		at48->setSamples(&l.samples);
		at44->setSamples(&l.samples);
		for(int lane = 0; lane < Lanes; ++lane)
			for(auto* p : {static_cast<Dt2Player*>(at48.get()), static_cast<Dt2Player*>(at44.get())})
			{
				Settings s = l.settings;
				s.tune = l.settings.tune + lane + _lowest;
				apply(*p, lane, s);
				p->trig(lane);
			}
		constexpr double Seconds = 0.08;
		const int blocks48 = static_cast<int>(Seconds * 48000 / BlockSize) + 1, blocks44 = static_cast<int>(Seconds * 44100 / BlockSize) + 1;
		std::vector<std::vector<double>> a(Lanes), b(Lanes);
		for(int bl = 0; bl < blocks48; ++bl)
		{
			const Block& out = at48->renderBlock();
			for(int n = 0; n < BlockSize; ++n)
				for(int lane = 0; lane < Lanes; ++lane)
					a[static_cast<size_t>(lane)].push_back(out.s[n][lane]);
		}
		std::vector<std::vector<float>> buffers(Lanes, std::vector<float>(BlockSize));
		std::vector<float*> outputs(Lanes);
		for(int lane = 0; lane < Lanes; ++lane)
			outputs[static_cast<size_t>(lane)] = buffers[static_cast<size_t>(lane)].data();
		for(int bl = 0; bl < blocks44; ++bl)
		{
			at44->render(outputs.data());
			for(int n = 0; n < BlockSize; ++n)
				for(int lane = 0; lane < Lanes; ++lane)
					b[static_cast<size_t>(lane)].push_back(buffers[static_cast<size_t>(lane)][static_cast<size_t>(n)]);
		}
		// The trig sounds a block later at 48 kHz, Host's resampler Latency later at 44.1: both from their first sample
		// above -60 dB of their peak (bench::onsetOf), cut to Seconds
		double worstEnv = 0, worstBand = 0, worstAt = 0;
		int worstLane = 0;
		for(size_t lane = 0; lane < Lanes; ++lane)
		{
			auto& x = a[lane];
			auto& y = b[lane];
			x.erase(x.begin(), x.begin() + static_cast<std::ptrdiff_t>(bench::onsetOf(x)));
			y.erase(y.begin(), y.begin() + static_cast<std::ptrdiff_t>(bench::onsetOf(y)));
			x.resize(static_cast<size_t>(Seconds * 48000));
			y.resize(static_cast<size_t>(Seconds * 44100));
			// The cut's end tapered over 10 ms: a Hit still sounding there would leak its low notes into every band
			const auto taper = [](std::vector<double>& _v, const double _fs)
			{
				const auto n = static_cast<size_t>(0.01 * _fs);
				for(size_t i = 0; i < n; ++i)
					_v[_v.size() - 1 - i] *= 0.5 - 0.5 * std::cos(bench::Pi * static_cast<double>(i) / static_cast<double>(n));
			};
			taper(x, 48000);
			taper(y, 44100);
			worstEnv = std::max(worstEnv, bench::envelopeDiff(bench::envelope(x, 48000, 10), bench::envelope(y, 44100, 10)));
			// third octaves from 20 Hz to 16 kHz where the 48 kHz render is above its loudest - 60 dB
			const auto ra = bench::bands(x, 48000, -17, 12), rb = bench::bands(y, 44100, -17, 12);
			const double loud = *std::max_element(ra.begin(), ra.end());
			double laneWorst = 0, at = 0;
			for(size_t k = 0; k < ra.size(); ++k)
				if(ra[k] >= loud * 1e-6 && std::abs(bench::db(rb[k] / ra[k])) > laneWorst)
				{
					laneWorst = std::abs(bench::db(rb[k] / ra[k]));
					at = 1000 * std::exp2((static_cast<int>(k) - 17) / 3.0);
				}
			if(laneWorst > worstBand)
			{
				worstBand = laneWorst;
				worstAt = at;
				worstLane = static_cast<int>(lane);
			}
		}
		std::printf("Q15 (%s): 44.1 against 48 kHz, 16 lanes: 10 ms envelope within %.3f dB, third octaves to 16 kHz within"
			" %.3f dB (lane %d, %.0f Hz)\n", _case, worstEnv, worstBand, worstLane, worstAt);
		require(worstEnv <= 0.2, "Q15: the envelope at 44.1 kHz is off by more than 0.2 dB");
		require(worstBand <= 0.5, "Q15: a third octave at 44.1 kHz is off by more than 0.5 dB");
	}

	// ---- the cost: 16 lanes looping a sample, in ONESHOT (the 96 kHz kernel) or STRETCH ----
	void cost(const Dt2Player::PlayerMode _mode)
	{
		std::vector<int16_t> pcm(48000);
		for(size_t i = 0; i < pcm.size(); ++i)
			pcm[i] = static_cast<int16_t>(20000 * std::sin(i * 0.05) * std::sin(i * 0.0011));
		Dt2Samples samples;
		for(auto& slot : samples.slots)
			slot = {pcm.data(), static_cast<uint32_t>(pcm.size()), 48000.0};
		auto p = std::make_unique<Dt2Player>();
		p->setSamples(&samples);
		for(int lane = 0; lane < Lanes; ++lane)
		{
			p->setParameter(lane, Dt2Player::Play, Dt2Player::ForwardLoop * 32, 0);
			p->setParameter(lane, Dt2Player::Length, 127, 0);
			p->setParameter(lane, Dt2Player::Tune, static_cast<uint8_t>(60 + lane), 0);
			p->setParameter(lane, Dt2Player::Mode, static_cast<uint8_t>(_mode * 128 / 6 + 10), 0);
			p->trig(lane);
		}
		p->renderBlock();
		constexpr int Blocks = 3000;
		double best = 1e9;
		for(int run = 0; run < 5; ++run)
		{
			const auto t0 = std::chrono::steady_clock::now();
			for(int b = 0; b < Blocks; ++b)
				p->renderBlock();
			best = std::min(best, std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
		}
		std::printf("cost: 16 lanes playing %s, %.3f %% of a core at 48 kHz (%.2f us a block)\n",
			_mode == Dt2Player::Stretch ? "STRETCH" : "ONESHOT", 100 * best / (Blocks * BlockSize / 48000.0), 1e6 * best / Blocks);
	}

	// ---- STRETCH without vectors: the tempo taken at each grain; a looped portion shorter than a grain's move ----
	void stretchLaws()
	{
		std::vector<int16_t> pcm(48000);
		for(size_t i = 0; i < pcm.size(); ++i)
			pcm[i] = static_cast<int16_t>(20000 * std::sin(i * 0.05) * std::sin(i * 0.0011));
		Dt2Samples samples;
		for(auto& slot : samples.slots)
			slot = {pcm.data(), static_cast<uint32_t>(pcm.size()), 48000.0};
		const auto make = [&](const int _play, const uint8_t _length, const uint8_t _fine)
		{
			auto p = std::make_unique<Dt2Player>();
			p->setSamples(&samples);
			p->setParameter(0, Dt2Player::Mode, static_cast<uint8_t>(Dt2Player::Stretch * 128 / 6 + 10), 0);
			p->setParameter(0, Dt2Player::Play, static_cast<uint8_t>(_play * 32), 0);
			p->setParameter(0, Dt2Player::Length, _length, _fine);
			p->setTempo(120);
			p->trig(0);
			p->renderBlock();
			return p;
		};
		// The tempo moved in the first grain: the same outputs to the second grain (2048 outputs, 64 blocks), others after
		auto steady = make(Dt2Player::Forward, 127, 0), moved = make(Dt2Player::Forward, 127, 0);
		bool same = true, after = false;
		for(int b = 0; b < 128; ++b)
		{
			if(b == 10)
				moved->setTempo(240);
			const Block& x = steady->renderBlock();
			const Block& y = moved->renderBlock();
			for(int n = 0; n < BlockSize; ++n)
			{
				const bool equal = x.s[n][0] == y.s[n][0];
				if(b < 64)
					same = same && equal;
				else
					after = after || !equal;
			}
		}
		require(same && after, "STRETCH: the tempo is not taken at the next grain");
		// A looped LEN of 128/256 of a step (189 samples, less than the time's 256 a grain, the ratio held at 1/8): it
		// plays on
		auto tiny = make(Dt2Player::ForwardLoop, 0, 128);
		double late = 0;
		for(int b = 0; b < 1500; ++b)
		{
			const Block& x = tiny->renderBlock();
			if(b >= 1400)
				for(int n = 0; n < BlockSize; ++n)
					late = std::max(late, static_cast<double>(std::abs(x.s[n][0])));
		}
		require(late > 0.01, "STRETCH: a looped portion shorter than the time's move falls silent");
		std::printf("PASS: STRETCH takes the tempo at the next grain; a short looped portion plays on\n");
	}

	// ---- the kernel's own laws, without vectors ----
	void kernel()
	{
		// Even, DC gain 1 after the scaling, zero past +-3
		double dc = 0;
		for(int i = -3000; i <= 3000; ++i)
			dc += Dt2Player::kernelAt(i / 1000.0) / 1000;
		require(std::abs(dc - 1) < 0.002, "the kernel's DC gain is not 1");
		require(Dt2Player::kernelAt(1.3) == Dt2Player::kernelAt(-1.3), "the kernel is not even");
		require(Dt2Player::kernelAt(3) == 0 && Dt2Player::kernelAt(-3.5) == 0, "the kernel reaches past +-3");
		// The half-band's all-pass: its four coefficients give a flat gain (an all-pass) and a delay near 3.5 at 1 kHz
		const auto& a = Dt2Player::halfBand();
		for(const double f : {0.01, 0.1, 0.2, 0.3, 0.45})
		{
			const std::complex<double> w = std::polar(1.0, -2 * bench::Pi * f);
			std::complex<double> num = 1, den = 1;
			std::complex<double> wk = 1;
			for(int k = 0; k < 4; ++k)
			{
				wk *= w;
				num += a[static_cast<size_t>(k)] * wk;	// 1 + a1 w + ... as the denominator
			}
			den = num;
			std::complex<double> rev = 0;
			wk = 1;
			for(int k = 3; k >= 0; --k)
			{
				rev += a[static_cast<size_t>(k)] * wk;
				wk *= w;
			}
			rev += wk;	// a4 + a3 w + a2 w^2 + a1 w^3 + w^4
			require(std::abs(std::abs(rev / den) - 1) < 1e-9, "the half-band's branch is not all-pass");
		}
		std::printf("PASS: the kernel is even, of unit gain, within +-3; the half-band's branch is all-pass\n");
	}

	// The portion a Hit plays, as the editor marks it (ticket 37 of the v2 map): trig's rules before its floors
	void spans()
	{
		const auto near = [](const double _a, const double _b) { return std::abs(_a - _b) < 1e-12; };
		const auto at = [](const Dt2Player::PlayerMode _mode) { return (static_cast<int>(_mode) * 128 / 6 + 1) << 8; };
		auto s = Dt2Player::span(at(Dt2Player::OneShot), 0, 0, 127 << 8);
		require(s && near(s->start, 0) && near(s->end, 1), "ONESHOT, STRT 0 LEN 127: the whole sample");
		s = Dt2Player::span(at(Dt2Player::OneShot), 0, 64 << 8, 127 << 8);
		require(s && near(s->start, 64 / 127.0) && near(s->end, 1), "STRT 64: from 64/127, LEN held to the end");
		s = Dt2Player::span(at(Dt2Player::Stretch), 0, 32 << 8, 32 << 8);
		require(s && near(s->start, 32 / 127.0) && near(s->end, 64 / 127.0), "STRETCH, STRT 32 LEN 32");
		// GRID of 16 (LOOP 52), slice 5 (STRT 32), 2 more (LEN 16): 4/16 to 7/16
		s = Dt2Player::span(at(Dt2Player::Grid), 52 << 8, 32 << 8, 16 << 8);
		require(s && near(s->start, 4 / 16.0) && near(s->end, 7 / 16.0), "GRID 16, slice 5 and 2 more");
		require(!Dt2Player::span(at(Dt2Player::Werp), 0, 0, 127 << 8), "WERP's window walks: no span");
		// SLICE: marks at 1000, 2000 and 9000 over 4000 frames cut 3 slices (9000 lies past the end); slice 2 (STRT 43)
		// and 1 more (LEN 43): 1000 to 4000
		Slices cuts;
		cuts.marks[0] = 1000, cuts.marks[1] = 2000, cuts.marks[2] = 9000, cuts.count = 3;
		require(Dt2Player::sliceCount(&cuts, 4000) == 3 && Dt2Player::sliceCount(nullptr, 4000) == 1,
			"SLICE: the marks inside the sample cut it");
		s = Dt2Player::span(at(Dt2Player::SliceMode), 0, 43 << 8, 43 << 8, &cuts, 4000);
		require(s && near(s->start, 0.25) && near(s->end, 1), "SLICE, slice 2 and 1 more");
		s = Dt2Player::span(at(Dt2Player::SliceMode), 0, 0, 0, nullptr, 4000);
		require(s && near(s->start, 0) && near(s->end, 1), "SLICE without marks: the whole sample");
		std::printf("PASS: a Hit's portion in ONESHOT, STRETCH, GRID and SLICE; none in WERP\n");
	}
}

int main()
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	try
	{
		kernel();
		spans();
		cost(Dt2Player::OneShot);
		cost(Dt2Player::Stretch);
		stretchLaws();
		const char* folder = std::getenv("DT2_VECTORS");
		if(!folder || !*folder || !std::ifstream(std::string(folder) + "/os_hit_unison.json").good())
		{
			std::printf("SKIP: DT2_VECTORS does not name the vectors-dt2 folder\n");
			return 77;
		}
		oracleCases(folder);
		lanes(folder);
		q15(folder, "os_hit_unison", -8);
		// WERP at 300 BPM, a step every 1200 samples at 48 kHz: three in the 80 ms; the lanes, 12 to 27 semitones down,
		// behind the window, play on where a lane ahead of it would stop (a cut that onset alignment does not judge)
		q15(folder, "wp_fwd_down12", -15);
		// STRETCH at 240 BPM: grains every 2048 outputs, the 80 ms crossing two of them
		q15(folder, "st_hit_ratio1", -8);
	}
	catch(const std::exception& e)
	{
		std::printf("FAIL: %s\n", e.what());
		return 1;
	}
	std::printf("PASS\n");
	return 0;
}
