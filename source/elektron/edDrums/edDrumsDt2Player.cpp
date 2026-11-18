#include "edDrumsDt2Player.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace elektron::drums
{
	using syntakt::fast::Block;
	using syntakt::fast::FlushDenormals;

	namespace
	{
		// The kernel as an even cubic B-spline on [-3, 3], a knot every KnotSpacing source samples, coefficient k for the
		// knots at +-k KnotSpacing: 14 numbers fitted by linear least squares to the oracle's 96 kHz buffer on its
		// fractional-step noise case (ticket 04's "Décisions et relevés": 34 dB there, 50 to 59 dB on its Hits; response
		// within 0.05 dB of the oracle's to 18 kHz). Their sum over the taps is the oracle's DC gain, 0.4045
		constexpr double KnotSpacing = 0.25;
		constexpr double KnotCoefficients[14] = {0.4021180, 0.3688461, 0.2774700, 0.1594079, 0.0493383, -0.0296140,
			-0.0650850, -0.0655028, -0.0494754, -0.0249995, -0.0107263, -0.0019690, -0.0016041, 0.0249207};

		// The half-band's even branch: an all-pass of order 4 in z^-2 (a delay of 3.5 samples at 48 kHz in the band), its
		// four coefficients fitted to the branch identified at the oracle (61 dB); the odd branch the pair 4 back
		constexpr std::array<double, 4> HalfBand{0.4810, -0.0985, 0.0332, -0.0119};
		constexpr int OddDelay = 4;

		// The oracle's start bound: STRT never nearer the end than this
		constexpr uint32_t StartMargin = 141;
		// Where a reversed read starts, from the end; how far below its start it may read
		constexpr double ReverseFrom = 6, ReverseBelow = 32;
		// A block sounds while its last output's position plus a sample (the step where it is more) lies within the end;
		// the last that sounds is faded out (probe_end2.py at the oracle, steps 0.25 to 2, and GRID's slice ends)
		bool sounds(const double _lastRead, const double _step, const double _end)
		{
			return _lastRead + std::max(_step, 1.0) <= _end;
		}

		double bspline3(const double _u)
		{
			const double a = std::abs(_u);
			if(a < 1)
				return 2.0 / 3 - a * a + a * a * a / 2;
			if(a < 2)
				return (2 - a) * (2 - a) * (2 - a) / 6;
			return 0;
		}

		constexpr double WordFull = 127.0 * 256;	// a value of 127 with no fine part: a fraction of 1

		double fraction(const int32_t _word)
		{
			return std::clamp(_word / WordFull, 0.0, 1.0);
		}
	}

	double Dt2Player::kernelAt(const double _t)
	{
		if(std::abs(_t) >= 3)
			return 0;
		double w = 0;
		for(int k = 0; k < 14; ++k)
		{
			const double c = k * KnotSpacing;
			w += KnotCoefficients[k] * (bspline3((_t - c) / KnotSpacing) + (k ? bspline3((_t + c) / KnotSpacing) : 0));
		}
		return w / DcGain;
	}

	const std::array<double, 4>& Dt2Player::halfBand()
	{
		return HalfBand;
	}

	const Dt2Player::Kernel& Dt2Player::kernel()
	{
		// Once a process: an Engine is made at every prepareToPlay. Phase p stands for a fraction p / Phases past the third
		// tap's sample; the 16-bit values to full scale 1
		static const Kernel table = []
		{
			Kernel k{};
			for(int p = 0; p < Phases; ++p)
				for(int t = 0; t < Taps; ++t)
					k[static_cast<size_t>(p)][static_cast<size_t>(t)] = static_cast<float>(kernelAt(t - 2 - p / static_cast<double>(Phases)) / 32768);
			return k;
		}();
		return table;
	}

	Dt2Player::Dt2Player() : m_kernel(kernel())
	{
		std::memset(&m_out, 0, sizeof(m_out));
		for(int l = 0; l < Lanes; ++l)
			convert(l);
	}

	void Dt2Player::setNote(const int _track, const int _note)
	{
		m_controls.notes[_track] = _note;
		convert(_track);
	}

	void Dt2Player::setParameter(const int _track, const int _index, const uint8_t _value, const uint8_t _fine)
	{
		if(m_controls.setParameter(_track, _index, _value, _fine))
			convert(_track);
	}

	void Dt2Player::setLanes(const int _lanes)
	{
		m_lanes = _lanes;
		syntakt::fast::setLanes(_lanes, {&m_out});
	}

	void Dt2Player::convert(const int _track)
	{
		const auto word = [this, _track](const int _i) { return m_controls.words[_i][_track]; };
		// The pitch: (TUNE - 64 + fine / 256) + (note - 60) semitones, the sum held to +-64, source samples an output at
		// 96 kHz: half a 48 kHz sample at unison, the sample's own rate over 96 kHz
		const double semitones = std::clamp((word(Tune) - (64 << 8)) / 256.0 + (m_controls.notes[_track] - 60), -64.0, 64.0);
		m_stepOf[_track] = std::exp2(semitones / 12) / 2;
	}

	void Dt2Player::startVoice(const int _l)
	{
		Lane& v = m_lane[static_cast<size_t>(_l)];
		const Dt2Samples::Slot* slot = m_samples ? &m_samples->slots[static_cast<size_t>(_l)] : nullptr;
		v.pcm = slot ? slot->pcm : nullptr;
		v.length = slot && slot->pcm ? slot->length : 0;
		v.playing = v.length != 0;
		if(!v.playing)
			return;
		// MODE, STRT, LEN, LOOP and PLAY as they stand at the trig; TUNE, the note and the tempo follow block by block
		// (render96)
		const auto word = [this, _l](const int _i) { return m_controls.words[_i][_l]; };
		const PlayMode play = playModeOf(word(Play) >> 8);
		const PlayerMode mode = modeOf(word(Mode) >> 8);
		v.rate = slot->rate / 48000.0;
		const auto length = static_cast<double>(v.length);
		const double bound = v.length > StartMargin ? static_cast<double>(v.length - StartMargin) : 0.0;
		v.repitch = mode == Repitch;
		if(mode == Grid)
		{
			// Machine 4 the slices the sample is cut into, 5 the one played (1-based), 6 how many more follow: the OS's own
			// positions (probe_grid2.py), slice k of n from (k - 1) n-ths of the sample to (k + more) n-ths
			const int grid = gridOf(word(Loop) >> 8);
			const int slice = sliceOf(word(Start) >> 8, grid);
			const int more = moreSlicesOf(word(Length) >> 8, grid);
			v.start = std::floor((slice - 1) * length / grid);
			v.end = std::min(std::floor((slice + more) * length / grid), length);
			v.loopAt = v.start;
		}
		else
		{
			const double startOf = fraction(word(Start)), loopOf = fraction(word(Loop));
			v.start = std::min(std::floor(startOf * length), bound);
			v.end = std::min(startOf + fraction(word(Length)), 1.0) * length;
			// REPITCH has BARS where ONESHOT has LOOP
			const double loopAt = std::floor(loopOf * length);
			v.loopAt = !v.repitch && loopOf > 0 && loopAt > v.start && loopAt < v.end ? loopAt : v.start;
			v.bars = std::exp2(barsOf(word(Loop) >> 8));
		}
		v.reverse = play == Reverse || play == ReverseLoop;
		v.loop = play == ForwardLoop || play == ReverseLoop;
		v.position = v.reverse ? v.end - ReverseFrom : v.start;
		if(v.end <= v.start)
			v.playing = false;
		std::fill(std::begin(v.x), std::end(v.x), 0.0);
		std::fill(std::begin(v.y), std::end(v.y), 0.0);
		std::fill(std::begin(v.oddTail), std::end(v.oddTail), 0.0f);
	}

	void Dt2Player::render96(const int _l, float* _work)
	{
		Lane& v = m_lane[static_cast<size_t>(_l)];
		// The sample as its slot holds it now, and the step from TUNE and the note as they stand: both follow block by block
		if(m_samples)
		{
			const auto& slot = m_samples->slots[static_cast<size_t>(_l)];
			v.pcm = slot.pcm;
			v.length = slot.pcm ? slot.length : 0;
		}
		// REPITCH: the portion over 2^BARS bars of four beats at the tempo, in outputs at 96 kHz, whatever TUNE and the note
		const double step = v.repitch ? (v.end - v.start) / (v.bars * 4 * 60 / m_bpm * 96000) : m_stepOf[_l] * v.rate;
		// The block that reaches the end is the last: faded out linearly, the voice over after it
		bool fade = false;
		if(!v.loop)
		{
			// The next block's last read, 127 steps on: if that block would not sound, this one is the last. A reversed read
			// goes on below its start by up to ReverseBelow, never below the sample's first (five reversed cases at the
			// oracle: 26.5 to 54.5 samples below a start inside the sample, none below 0)
			fade = v.reverse ? !sounds(-(v.position - 127 * step), step, -std::max(v.start - ReverseBelow, 0.0))
				: !sounds(v.position + 127 * step, step, v.end);
		}
		const auto length = static_cast<int64_t>(v.length);
		for(int m = 0; m < Work; ++m)
		{
			// The six taps read the position's sample and the five after it, the kernel centred on the third
			const double base = std::floor(v.position);
			const auto first = static_cast<int64_t>(base);
			const auto phase = std::min(static_cast<int>((v.position - base) * Phases + 0.5), Phases - 1);
			const float* k = m_kernel[static_cast<size_t>(phase)].data();
			float acc = 0;
			for(int t = 0; t < Taps; ++t)
			{
				const int64_t i = first + t;
				if(i >= 0 && i < length)
					acc += k[t] * static_cast<float>(v.pcm[i]);
			}
			_work[m] = fade ? acc * static_cast<float>(Work - 1 - m) / (Work - 1) : acc;
			v.position += v.reverse ? -step : step;
			if(v.loop)
			{
				const double span = v.end - v.loopAt;
				if(!v.reverse && v.position >= v.end)
					v.position -= span;
				else if(v.reverse && v.position < v.loopAt)
					v.position += span;
			}
		}
		if(fade)
			v.playing = false;
	}

	void Dt2Player::decimate(const int _l, const float* _work, const bool _fadeOut)
	{
		Lane& v = m_lane[static_cast<size_t>(_l)];
		const auto& a = HalfBand;
		// The all-pass state in registers for the block
		double x0 = v.x[0], x1 = v.x[1], x2 = v.x[2], x3 = v.x[3], y0 = v.y[0], y1 = v.y[1], y2 = v.y[2], y3 = v.y[3];
		for(int n = 0; n < BlockSize; ++n)
		{
			// The even branch through the all-pass (direct form I, the numerator the denominator reversed)
			const double x = _work[2 * n];
			const double y = a[3] * x + a[2] * x0 + a[1] * x1 + a[0] * x2 + x3 - (a[0] * y0 + a[1] * y1 + a[2] * y2 + a[3] * y3);
			x3 = x2; x2 = x1; x1 = x0; x0 = x;
			y3 = y2; y2 = y1; y1 = y0; y0 = y;
			// The odd branch, the pair OddDelay back: the last block's for the first pairs
			const double o = n >= OddDelay ? _work[2 * (n - OddDelay) + 1] : v.oddTail[n];
			const float gain = _fadeOut ? 1 - static_cast<float>(n) / BlockSize : 1.0f;
			m_out.s[n][_l] = static_cast<float>(0.5 * (y0 + o)) * gain;
		}
		v.x[0] = x0; v.x[1] = x1; v.x[2] = x2; v.x[3] = x3;
		v.y[0] = y0; v.y[1] = y1; v.y[2] = y2; v.y[3] = y3;
		for(int d = 0; d < OddDelay; ++d)
			v.oddTail[d] = _work[2 * (BlockSize - OddDelay + d) + 1];
	}

	const Block& Dt2Player::renderBlock()
	{
		const FlushDenormals ftz;
		m_controls.trigs.start();
		alignas(16) float work[Work];
		for(int l = 0; l < m_lanes; ++l)
		{
			auto& trigs = m_controls.trigs;
			Lane& v = m_lane[static_cast<size_t>(l)];
			if(trigs.now[l])
				startVoice(l);
			if(!v.playing)
			{
				for(int n = 0; n < BlockSize; ++n)
					m_out.s[n][l] = 0;
				continue;
			}
			render96(l, work);
			// A trig seen in this block: the voice fades out over it, the Hit sounds from the next
			const bool pending = trigs.pending[l];
			decimate(l, work, pending);
			if(pending)
				v.playing = false;
		}
		m_controls.trigs.end();
		return m_out;
	}
}
