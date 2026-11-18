// The Sampler's DT2 player in ONESHOT (ticket 04 of .scratch/elektron-drums-v2) against the Digitakt II's voice run in
// its OS (DT2_VECTORS, the folder dt2_voice_oracle.py wrote: per case the sample, the voice's 48 kHz output and its
// settings), on every ONESHOT case:
//   - the player's output, scaled by the oracle's DC gain, nulled against the voice column: SNR at least the bound each
//     case states (the Hits 45 dB, the sines 45, the broadband cases 27: our own kernel, fitted, not the firmware's);
//   - Q4 on top: the 1 ms envelope within 0.1 dB where the oracle is above its peak - 40 dB, a sine's pitch within 1 cent;
//   - 16 lanes on the same case give lane 0's samples, and 4 lanes rendered alone give those of 16;
//   - Q15: the same Hits at 44.1 kHz through Host against 48 kHz: 10 ms envelopes within 0.2 dB, third octaves to
//     16 kHz within 0.5 dB;
//   - the cost of 16 lanes playing, printed.
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
	};

	struct Settings
	{
		double tune = 64, note = 60, strt = 0, len = 120, loopPos = 0, bpm = 120;
		bool reverse = false, loop = false;
		int blocks = 0, selector = 0, bars = 0;	// selector 0 ONESHOT, 3 REPITCH, 4 GRID; bars BARS, or GRID's G
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
		require(s.selector == 0 || s.selector == 3 || s.selector == 4, "not an ONESHOT, REPITCH or GRID case");
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
		const int play = _s.reverse ? (_s.loop ? Dt2Player::ReverseLoop : Dt2Player::Reverse)
			: (_s.loop ? Dt2Player::ForwardLoop : Dt2Player::Forward);
		_p.setParameter(_lane, Dt2Player::Play, static_cast<uint8_t>(play * 32), 0);
		// MODE a sixth of the range each (Dt2Player::modeOf): ONESHOT 0, GRID 1, REPITCH 2
		const int mode = _s.selector == 3 ? Dt2Player::Repitch : _s.selector == 4 ? Dt2Player::Grid : Dt2Player::OneShot;
		_p.setParameter(_lane, Dt2Player::Mode, static_cast<uint8_t>(mode * 128 / 6 + 10), 0);
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
		if(_s.selector == 3)
		{
			// BARS: Dt2Player::barsOf, a fifth of the range each
			const auto value = static_cast<uint8_t>(_s.bars * 26);
			require(Dt2Player::barsOf(value) == _s.bars, "the test's BARS value does not give the case's bars");
			_p.setParameter(_lane, Dt2Player::Loop, value, 0);
			return;
		}
		setFraction(_p, _lane, Dt2Player::Loop, _s.loopPos);
	}

	// _blocks of lane _lane after the trig's block, at the oracle's gain
	std::vector<double> render(Dt2Player& _p, const int _lane, const int _blocks)
	{
		std::vector<double> y;
		y.reserve(static_cast<size_t>(_blocks) * BlockSize);
		_p.renderBlock();	// the block that sees the trig
		for(int b = 0; b < _blocks; ++b)
		{
			const Block& out = _p.renderBlock();
			for(int n = 0; n < BlockSize; ++n)
				y.push_back(out.s[n][_lane] * Dt2Player::DcGain);
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
		for(auto& slot : l.samples.slots)
			slot = {l.pcm.data(), static_cast<uint32_t>(l.pcm.size()), 48000.0};
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
			auto y = render(*p, 0, l.settings.blocks);
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
	void q15(const std::string& _folder)
	{
		const Loaded l = load(_folder, "os_hit_unison");
		auto at48 = std::make_unique<Dt2Player>();
		auto at44 = std::make_unique<syntakt::fast::Host<Dt2Player>>(44100.0f);
		at48->setSamples(&l.samples);
		at44->setSamples(&l.samples);
		// 16 lanes a semitone apart from -8 to +7
		for(int lane = 0; lane < Lanes; ++lane)
			for(auto* p : {static_cast<Dt2Player*>(at48.get()), static_cast<Dt2Player*>(at44.get())})
			{
				Settings s = l.settings;
				s.tune = 64 + lane - 8;
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
		std::printf("Q15: 44.1 against 48 kHz, 16 lanes: 10 ms envelope within %.3f dB, third octaves to 16 kHz within %.3f dB"
			" (lane %d, %.0f Hz)\n", worstEnv, worstBand, worstLane, worstAt);
		require(worstEnv <= 0.2, "Q15: the envelope at 44.1 kHz is off by more than 0.2 dB");
		require(worstBand <= 0.5, "Q15: a third octave at 44.1 kHz is off by more than 0.5 dB");
	}

	// ---- the cost: 16 lanes looping a sample ----
	void cost()
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
		std::printf("cost: 16 lanes playing, %.3f %% of a core at 48 kHz (%.2f us a block)\n",
			100 * best / (Blocks * BlockSize / 48000.0), 1e6 * best / Blocks);
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
}

int main()
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	try
	{
		kernel();
		cost();
		const char* folder = std::getenv("DT2_VECTORS");
		if(!folder || !*folder || !std::ifstream(std::string(folder) + "/os_hit_unison.json").good())
		{
			std::printf("SKIP: DT2_VECTORS does not name the vectors-dt2 folder\n");
			return 77;
		}
		oracleCases(folder);
		lanes(folder);
		q15(folder);
	}
	catch(const std::exception& e)
	{
		std::printf("FAIL: %s\n", e.what());
		return 1;
	}
	std::printf("PASS\n");
	return 0;
}
