// A Rytm sound imported into the sample layer (ticket 21 of .scratch/elektron-drums-v2, edDrumsRytmSound.h):
//   - a dump read back as it was written, and refused when it is not a Rytm sound, from an OS before 1.70, cut short or
//     its checksum wrong;
//   - its SAMPLE page as the layer's: TUNE and FINE within +-24 semitones, STRT and END as START and LENGTH, reversed,
//     looped, LEV, BR and the chromatic mode;
//   - against the Rytm's sample layer run in OS 1.74 (RYTM_LAYER_VECTORS, the folder rytm_layer_vectors.py wrote: per
//     case the dump, its sample and the voice's 48 kHz output): the dump read, made a layer and played at 48 kHz, nulled
//     against the oracle's voice, SNR at least the bound each case states, the 1 ms envelope within its bound where the
//     oracle is above its peak - 40 dB. Exit 77 without the vectors, once the rest has run.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "edDrumsRytmSound.h"

namespace
{
	using namespace elektron::drums;
	using elektron::BlockSize;
	constexpr double Pi = 3.14159265358979323846;

	void require(const bool _condition, const std::string& _message)
	{
		if(!_condition)
			throw std::runtime_error(_message);
	}

	std::vector<uint8_t> readBytes(const std::string& _path)
	{
		std::ifstream f(_path, std::ios::binary);
		require(f.good(), "cannot read " + _path);
		return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
	}

	void reads()
	{
		rytm::Sound s;
		s.name = "KICK LONG";
		s.tune = 71;
		s.fine = 84;
		s.sample = 6;
		s.bitReduction = 33;
		s.start = 30 << 8 | 128;
		s.end = 90 << 8 | 64;
		s.loop = true;
		s.level = 100;
		s.chromatic = true;
		for(const uint8_t id : {uint8_t{0x53}, uint8_t{0x59}})
		{
			const auto m = rytm::writeSound(s, id);
			require(m.size() == rytm::SoundDumpSize, "the dump made is not 201 bytes");
			std::string error;
			const auto r = rytm::readSound(m.data(), m.size(), error);
			require(r.has_value(), "a dump made here is refused: " + error);
			require(r->name == s.name && r->tune == s.tune && r->fine == s.fine && r->sample == s.sample
				&& r->bitReduction == s.bitReduction && r->start == s.start && r->end == s.end && r->loop == s.loop
				&& r->level == s.level && r->chromatic == s.chromatic, "a dump does not read back as written");
		}
		const auto refused = [](std::vector<uint8_t> _m, const char* _what)
		{
			std::string error;
			require(!rytm::readSound(_m.data(), _m.size(), error) && !error.empty(), std::string("not refused: ") + _what);
		};
		const auto good = rytm::writeSound(s);
		auto m = good;
		m[4] = 0x06;
		refused(m, "another machine");
		m = good;
		m[6] = 0x52;
		refused(m, "a Kit");
		m = good;
		m.insert(m.end() - 5, {0, 0, 0, 0, 0, 0});
		refused(m, "a dump from before OS 1.70");
		m = good;
		m.pop_back();
		refused(m, "a dump cut short");
		m = good;
		m[60] ^= 1;
		refused(m, "a byte changed");
		refused({}, "nothing");
		std::printf("a dump read back, and refused: another machine, a Kit, before OS 1.70, cut short, a byte changed\n");
	}

	void translates()
	{
		const auto layer = [](const int _tune, const int _fine, const int _start, const int _end, const bool _loop)
		{
			rytm::Sound s;
			s.tune = _tune;
			s.fine = _fine;
			s.start = _start;
			s.end = _end;
			s.loop = _loop;
			return rytm::layerOf(s);
		};
		// +7.3125 semitones: TUNE +7, FINE 40/128
		auto l = layer(71, 84, 0, 120 << 8, false);
		require(l.on && l.tune == 71 && l.fine == 104, "TUNE +7, FINE +20 as the layer's");
		require(l.start == 0 && l.length == 127 && l.play == 0 && l.loop == 0, "the whole sample forward");
		// +24.98 bounded to +24; -24 the bottom
		l = layer(88, 127, 0, 120 << 8, false);
		require(l.tune == 88 && l.fine == 64, "TUNE +24, FINE +63 not bounded to +24");
		l = layer(40, 64, 0, 120 << 8, false);
		require(l.tune == 40 && l.fine == 64, "TUNE -24");
		// +0.5 semitone: the next semitone less half of one, inside the layer's FINE
		l = layer(64, 96, 0, 120 << 8, false);
		require(l.tune == 65 && l.fine == 0, "FINE +32 as the next semitone less half");
		// A portion, reversed, looped
		l = layer(64, 64, 90 << 8, 30 << 8, false);
		require(l.start == 32 && l.length == 63 && l.play == 32, "END before STRT reversed");
		l = layer(64, 64, 30 << 8, 90 << 8, true);
		require(l.start == 32 && l.length == 63 && l.play == 64, "a loop forward");
		l = layer(64, 64, 120 << 8, 0, true);
		require(l.start == 0 && l.length == 127 && l.play == 96, "a loop reversed");
		// END past 120 saturated
		l = layer(64, 64, 60 << 8, 127 << 8, false);
		require(l.start == 64 && l.length == 63, "END saturated at the sample's end");
		rytm::Sound s;
		s.level = 64;
		s.bitReduction = 48;
		s.chromatic = true;
		l = rytm::layerOf(s);
		require(l.level == 64 && l.bitReduction == 48 && l.noteTrack, "LEV, BR and the chromatic mode as they are");
		require(l.attack == 0 && l.decay == 127 && l.offset == 0, "the envelope open, no offset");
		std::printf("the SAMPLE page as the layer's: pitch, bounds, portion, direction, loop, LEV, BR, chromatic\n");
	}

	// ---- against the oracle ----
	// The layer's output leads the Rytm's by this many samples on every case whose STRT and END the layer's 127ths hold
	// exactly: the Rytm's trig sounds from the block after it, its DMA a block ahead, its kernel and decimator half a
	// sample later than ours
	constexpr double OracleLag = 31.5;

	enum class Judge
	{
		Null,		// nulled at OracleLag
		Shifted,	// STRT or END between two 127ths: the start moved as far as the translation says, then nulled there
		Loop		// a loop: its length the translation's, its level the Rytm's
	};

	struct Case
	{
		const char* name;
		Judge judge;
		double snrBound;	// dB against the oracle's voice
		double envBound;	// dB, the 1 ms envelope where the oracle is above its peak - 40 dB
	};

	const Case Cases[] = {
		{"base", Judge::Null, 35, 0.1}, {"level64", Judge::Null, 35, 0.1}, {"end_exact", Judge::Null, 36, 0.1},
		{"rev_ref", Judge::Null, 36, 0.1}, {"tune_down24", Judge::Null, 41, 0.1},
		// Higher, the Rytm's pitch from its table of 2048 steps an octave, truncated (up to 0.4 cent flat), drifts
		// against ours over the Hit
		{"tune_up", Judge::Null, 26, 0.1}, {"tune_clamp", Judge::Null, 26, 0.1},
		// BR: its truncation of 16 x 16-bit products against ours in float
		{"br48", Judge::Null, 25, 0.3},
		// The Rytm's end, forward and reversed
		{"end", Judge::Null, 35, 0.3}, {"reverse", Judge::Null, 41, 0.3}, {"rev_end", Judge::Null, 41, 0.3},
		{"portion", Judge::Shifted, 27, 0.5}, {"portion_frac", Judge::Shifted, 11, 3}, {"reverse_portion", Judge::Shifted, 23, 6},
		{"combo", Judge::Shifted, 11, 99},
		{"loop", Judge::Loop, 0, 1},
	};

	// _y delayed by _d samples (a 64-tap Blackman-windowed sinc for the fraction)
	std::vector<double> delayed(const std::vector<double>& _y, const double _d, const size_t _length)
	{
		const auto whole = static_cast<long>(std::floor(_d));
		const double f = _d - static_cast<double>(whole);
		std::vector<double> t(_length, 0.0);
		const auto at = [&](const long _i) { return _i >= 0 && _i < static_cast<long>(_y.size()) ? _y[static_cast<size_t>(_i)] : 0.0; };
		for(size_t n = 0; n < _length; ++n)
		{
			const long base = static_cast<long>(n) - whole;
			if(f == 0)
			{
				t[n] = at(base);
				continue;
			}
			double s = 0;
			for(long k = -31; k <= 32; ++k)
			{
				const double x = static_cast<double>(k) - f;
				const double sinc = std::sin(Pi * x) / (Pi * x);
				const double w = 0.42 + 0.5 * std::cos(Pi * x / 32) + 0.08 * std::cos(2 * Pi * x / 32);
				s += at(base - k) * sinc * w;
			}
			t[n] = s;
		}
		return t;
	}

	double snrOf(const std::vector<double>& _ref, const std::vector<double>& _t)
	{
		double s = 0, e = 0;
		for(size_t n = 0; n < _ref.size(); ++n)
		{
			s += _ref[n] * _ref[n];
			e += (_t[n] - _ref[n]) * (_t[n] - _ref[n]);
		}
		return 10 * std::log10(s / std::max(e, 1e-30));
	}

	std::vector<double> envelopeDb(const std::vector<double>& _x)
	{
		constexpr size_t W = 48;
		std::vector<double> e;
		for(size_t i = 0; i + W <= _x.size(); i += W)
		{
			double s = 0;
			for(size_t n = i; n < i + W; ++n)
				s += _x[n] * _x[n];
			e.push_back(10 * std::log10(s / W + 1e-30));
		}
		return e;
	}

	double envelopeOff(const std::vector<double>& _ref, const std::vector<double>& _t)
	{
		const auto er = envelopeDb(_ref), et = envelopeDb(_t);
		const double top = *std::max_element(er.begin(), er.end());
		double off = 0;
		for(size_t i = 0; i < er.size(); ++i)
			if(er[i] > top - 40)
				off = std::max(off, std::abs(et[i] - er[i]));
		return off;
	}

	// The delay of _y that nulls it best against _ref, to a quarter of a sample: the whole samples by correlation
	// within 3 blocks, then the quarters around them
	double bestLag(const std::vector<double>& _ref, const std::vector<double>& _y)
	{
		long lag = 0;
		double best = -1e300;
		for(long d = -96; d <= 96; ++d)
		{
			double s = 0;
			for(size_t n = 0; n < _ref.size(); ++n)
			{
				const long m = static_cast<long>(n) - d;
				if(m >= 0 && m < static_cast<long>(_y.size()))
					s += _ref[n] * _y[static_cast<size_t>(m)];
			}
			if(s > best)
			{
				best = s;
				lag = d;
			}
		}
		double fine = static_cast<double>(lag), least = 1e300;
		for(double d = static_cast<double>(lag) - 1; d <= static_cast<double>(lag) + 1; d += 0.25)
		{
			const auto t = delayed(_y, d, _ref.size());
			double e = 0;
			for(size_t n = 0; n < _ref.size(); ++n)
				e += (t[n] - _ref[n]) * (t[n] - _ref[n]);
			if(e < least)
			{
				least = e;
				fine = d;
			}
		}
		return fine;
	}

	// Where the layer's read starts against the Rytm's, in its outputs: forward its start (the 127th below), reversed
	// its end, against STRT's (fractions of 120 with their 1/256)
	double shiftOf(const rytm::Sound& _s, const LayerSettings& _l, const size_t _frames)
	{
		const auto frames = static_cast<double>(_frames);
		const bool reverse = _s.end < _s.start;
		const double rytm = frames * (reverse ? _s.start : std::min(_s.start, 120 << 8)) / (120.0 * 256);
		const double ours = reverse ? (_l.start + _l.length) / 127.0 * frames : std::floor(_l.start / 127.0 * frames);
		const double semitones = (_l.tune - 64) + (_l.fine - 64) / 128.0;
		// An output reads this many source samples (the voice at 48 kHz and so is the sample)
		const double advance = std::exp2(semitones / 12);
		return (reverse ? rytm - ours : ours - rytm) / advance;
	}

	// The loop's length in samples: the autocorrelation's highest peak within 60 samples of _expect, past its first
	// 10 blocks
	int loopLength(const std::vector<double>& _x, const int _expect)
	{
		const size_t from = 10 * BlockSize;
		int best = 0;
		double top = -1e300;
		for(int d = _expect - 60; d <= _expect + 60; ++d)
		{
			double s = 0;
			for(size_t n = from; n + static_cast<size_t>(d) < _x.size(); ++n)
				s += _x[n] * _x[n + static_cast<size_t>(d)];
			if(s > top)
			{
				top = s;
				best = d;
			}
		}
		return best;
	}

	double rmsDb(const std::vector<double>& _x, const size_t _from)
	{
		double s = 0;
		for(size_t n = _from; n < _x.size(); ++n)
			s += _x[n] * _x[n];
		return 10 * std::log10(s / static_cast<double>(_x.size() - _from) + 1e-30);
	}

	int oracleCases(const char* _folder)
	{
		std::printf("against the oracle: case, lag (expected), SNR dB (bound), envelope dB (bound)\n");
		const std::string folder = _folder;
		const auto text = readBytes(folder + "/cases.txt");
		std::istringstream list(std::string(text.begin(), text.end()));
		std::string name;
		int blocks = 0, judged = 0;
		while(list >> name >> blocks)
		{
			const Case* c = nullptr;
			for(const auto& k : Cases)
				if(name == k.name)
					c = &k;
			require(c != nullptr, "a case the test does not judge: " + name);
			const auto syx = readBytes(folder + "/" + name + ".syx");
			std::string error;
			const auto sound = rytm::readSound(syx.data(), syx.size(), error);
			require(sound.has_value(), name + ": the oracle's dump is refused: " + error);
			const auto bytes = readBytes(folder + "/" + name + ".s16");
			std::vector<int16_t> pcm(bytes.size() / 2);
			std::memcpy(pcm.data(), bytes.data(), pcm.size() * 2);
			const auto f32 = readBytes(folder + "/" + name + ".f32");
			std::vector<float> o(f32.size() / 4);
			std::memcpy(o.data(), f32.data(), o.size() * 4);
			const std::vector<double> ref(o.begin(), o.end());

			const LayerSettings settings = rytm::layerOf(*sound);
			auto layers = std::make_unique<SampleLayers>(48000.0);
			Dt2Samples::Slot slot;
			slot.pcm = pcm.data();
			slot.length = static_cast<uint32_t>(pcm.size());
			layers->setSample(0, slot);
			layers->setNote(0, 60);
			layers->setSettings(0, settings);
			layers->setRytmEnd(0, true);
			// The oracle's trig is at block 1
			std::vector<double> y;
			std::vector<float> block(BlockSize);
			for(int b = 0; b < blocks + 4; ++b)
			{
				if(b == 1)
					layers->hit(0, 0);
				std::fill(block.begin(), block.end(), 0.0f);
				layers->render(0, block.data());
				y.insert(y.end(), block.begin(), block.end());
			}
			if(c->judge == Judge::Loop)
			{
				// The Rytm loops between STRT and END, the layer round its START and LENGTH
				const auto frames = static_cast<double>(pcm.size());
				const int rytm = static_cast<int>(std::lround(frames * (sound->end - sound->start) / (120.0 * 256)));
				const double ours = std::floor((settings.start + settings.length) / 127.0 * frames)
					- std::floor(settings.start / 127.0 * frames);
				const int lr = loopLength(ref, rytm), ly = loopLength(y, rytm);
				const double level = rmsDb(y, 10 * BlockSize) - rmsDb(ref, 10 * BlockSize);
				std::printf("  %-16s loop %d samples (the Rytm's %d), ours %d (%.0f), level %+.2f dB (%.0f)\n",
					name.c_str(), lr, rytm, ly, ours, level, c->envBound);
				require(std::abs(lr - rytm) <= 1, name + ": the oracle does not loop as STRT and END say");
				require(std::abs(ly - ours) <= 1, name + ": the layer does not loop as its START and LENGTH say");
				require(std::abs(level) <= c->envBound, name + ": the loop's level off the Rytm's");
				++judged;
				continue;
			}
			const double expect = OracleLag + (c->judge == Judge::Shifted ? shiftOf(*sound, settings, pcm.size()) : 0.0);
			const double lag = bestLag(ref, y);
			const auto t = delayed(y, lag, ref.size());
			const double snr = snrOf(ref, t), env = envelopeOff(ref, t);
			std::printf("  %-16s %6.2f (%6.2f)  %5.1f (%2.0f)  %5.2f (%4.2f)\n", name.c_str(), lag, expect, snr,
				c->snrBound, env, c->envBound);
			require(std::abs(lag - expect) <= 0.5, name + ": the layer starts elsewhere than the translation says");
			require(snr >= c->snrBound, name + ": SNR against the oracle under its bound");
			require(env <= c->envBound, name + ": envelope off by more than its bound");
			++judged;
		}
		require(judged == static_cast<int>(std::size(Cases)), "a case of the test is missing from the vectors");
		return 0;
	}
}

int main()
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	try
	{
		reads();
		translates();
		const char* folder = std::getenv("RYTM_LAYER_VECTORS");
		if(!folder || !*folder)
		{
			std::printf("RYTM_LAYER_VECTORS not set: the oracle's cases skipped\n");
			return 77;
		}
		return oracleCases(folder);
	}
	catch(const std::exception& e)
	{
		std::printf("FAILED: %s\n", e.what());
		return 1;
	}
}
