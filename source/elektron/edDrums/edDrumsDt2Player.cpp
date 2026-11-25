#include "edDrumsDt2Player.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <emmintrin.h>

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
		// A reversed loop goes round this far below its loop point: the DSP's read position runs 5 samples above ours
		// (its reversed read starts at the end less 1), and it wraps where that reaches the loop point (WERP's REV.L at
		// the oracle, ticket 11: the best of 0 to 7; applied to WERP alone, render96)
		constexpr double ReverseLoopLag = 5;
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

		// WERP's sequencer, as the Digitakt II's ColdFire runs it once a frame of 32 samples at 48 kHz (ticket 11): a
		// whole sample is WerpWhole of its units; a segment WerpWhole >> (5 - SEGMENT); a step moves the window by a
		// segment >> BARS when the accumulator, fed (tempo x 120) << 4 >> BARS a frame, reaches that
		constexpr int64_t WerpWhole = 691200000;

		double fraction(const int32_t _word)
		{
			return std::clamp(_word / WordFull, 0.0, 1.0);
		}

		// STRETCH, as the Digitakt II's DSP plays it (0x1c5576, the voice oracle, ticket 13): a grain every GrainHop
		// outputs, faded in linearly over GrainFade (weight n / 127) while the last fades out; the time moves a grain
		// the portion over 2^BARS bars at the tempo times GrainHop, the ratio held to [1/8, 4]. Positions in source
		// samples with a 31-bit fraction, as the DSP's
		constexpr int GrainHop = 2048, GrainFade = 128;
		constexpr double SlowestStretch = 0.125, FastestStretch = 4;
		constexpr int64_t Unit = int64_t{1} << 31;
		// The block whose read would reach this many outputs on past the end is the last (a reversed read: past the
		// start less ReverseBelow, never below the sample's first), faded over the block: ONESHOT's rule, as the
		// oracle's ends show it
		constexpr int StretchLookAhead = 64;
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

	std::optional<Dt2Player::Span> Dt2Player::span(const int _mode, const int _loop, const int _start,
		const int _length)
	{
		const PlayerMode mode = modeOf(_mode >> 8);
		if(mode == Werp)
			return std::nullopt;
		Span s;
		if(mode == Grid)
		{
			const int grid = gridOf(_loop >> 8);
			const int slice = sliceOf(_start >> 8, grid);
			s.start = static_cast<double>(slice - 1) / grid;
			s.end = std::min(static_cast<double>(slice + moreSlicesOf(_length >> 8, grid)) / grid, 1.0);
			return s;
		}
		s.start = fraction(_start);
		s.end = std::min(s.start + fraction(_length), 1.0);
		return s;
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
		v.rate = slot->rate / m_outRate;
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
		else if(mode != Werp)
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
		v.stretch = mode == Stretch;
		if(v.stretch)
		{
			// The portion's end on a whole sample, as the DSP's; the loop goes round the whole portion (Machine 4 is BARS)
			v.end = std::floor(v.end);
			v.loopAt = v.start;
		}
		v.werp = mode == Werp;
		if(v.werp)
		{
			// Machine 5 SEGMENT, 4 BARS: the step, which is also the window's length
			v.werpBars = werpBarsOf(word(Loop) >> 8);
			v.werpStride = (WerpWhole >> (5 - werpSegmentOf(word(Start) >> 8))) >> v.werpBars;
			werpFrame(v, true);
		}
		v.position = v.reverse ? v.end - ReverseFrom : v.start;
		if(v.end <= v.start)
			v.playing = false;
		if(v.stretch)
			stretchStart(v);
		std::fill(std::begin(v.x), std::end(v.x), 0.0);
		std::fill(std::begin(v.y), std::end(v.y), 0.0);
		std::fill(std::begin(v.oddTail), std::end(v.oddTail), 0.0f);
	}

	void Dt2Player::werpFrame(Lane& _v, const bool _trig)
	{
		const int64_t stride = _v.werpStride;
		if(_trig)
		{
			_v.werpLive = _v.werpFresh = true;
			_v.werpAcc = 0;
			_v.werpPos = _v.reverse ? WerpWhole - stride : 0;
		}
		else
		{
			if(!_v.werpLive)
				return;
			// The tempo as the ColdFire holds it, times 120 within 30 to 300 BPM, read every frame; a frame is a block of
			// 32 at 48 kHz, the rate WERP renders at (the Syntakt family's path: the sample layer plays ONESHOT alone)
			const auto tempo = std::clamp(static_cast<int64_t>(std::lround(m_bpm * 120)), int64_t{3600}, int64_t{36000});
			_v.werpAcc += (tempo << 4) >> _v.werpBars;
			if(_v.werpAcc < stride)
				return;
			_v.werpAcc -= stride;
			_v.werpPos += _v.reverse ? -stride : stride;
			// Past the sample: on round with PLAY's loop, else the sequence ends there, its window where it went
			if(_v.reverse ? _v.werpPos < 0 : _v.werpPos > WerpWhole - 1)
			{
				if(_v.loop)
					_v.werpPos += _v.reverse ? WerpWhole : -WerpWhole;
				else
					_v.werpLive = false;
			}
		}
		werpWindow(_v);
	}

	void Dt2Player::werpWindow(Lane& _v)
	{
		// The SHARC reads the position as an unsigned fraction of WerpWhole: one below 0 lies past the end, where the
		// start is bounded as STRT's
		const auto length = static_cast<double>(_v.length);
		const double bound = _v.length > StartMargin ? static_cast<double>(_v.length - StartMargin) : 0.0;
		const auto x = static_cast<double>(static_cast<uint32_t>(_v.werpPos));
		_v.start = std::min(std::floor(x / static_cast<double>(WerpWhole) * length), bound);
		// The window is a step long: at a tempo where the sample lasts its bars, the segments follow one another
		_v.end = std::min(_v.start + static_cast<double>(_v.werpStride) / static_cast<double>(WerpWhole) * length, length);
		_v.loopAt = _v.start;
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
		// WERP: the trig's frame placed the window at the start; every block after moves the sequencer on a frame
		if(v.werp)
		{
			if(v.werpFresh)
				v.werpFresh = false;
			else
				werpFrame(v, false);
		}
		// REPITCH: the portion over 2^BARS bars of four beats at the tempo, in outputs at twice the output rate, whatever
		// TUNE and the note
		const double step = v.repitch ? (v.end - v.start) / (v.bars * 4 * 60 / m_bpm * 2 * m_outRate)
			: m_stepOf[_l] * v.rate;
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
		// In locals for the block: the compiler cannot tell that _work's stores leave the lane alone, and would carry
		// the position through memory from one output to the next
		const int16_t* const pcm = v.pcm;
		const auto& kernel = m_kernel;
		const bool reverse = v.reverse, loop = v.loop;
		const double move = reverse ? -step : step, end = v.end, loopAt = v.loopAt, span = end - loopAt;
		// Measured on WERP alone: ONESHOT's and GRID's REV LOOP keep their wrap until a vector of theirs says otherwise
		const double lag = v.werp ? ReverseLoopLag : 0;
		double position = v.position;
		for(int m = 0; m < Work; ++m)
		{
			// The six taps read the position's sample and the five after it, the kernel centred on the third
			const double base = std::floor(position);
			const auto first = static_cast<int64_t>(base);
			const auto phase = std::min(static_cast<int>((position - base) * Phases + 0.5), Phases - 1);
			const float* k = kernel[static_cast<size_t>(phase)].data();
			float acc = 0;
			if(first >= 0 && first + RowTaps <= length)
			{
				// Inside the sample, as nearly every output is: the six products at once from eight samples read,
				// then summed one after the other in the taps' order, the same sum as below without the bounds (a
				// loop bounded per output runs half as fast again)
				const __m128i x = _mm_loadu_si128(reinterpret_cast<const __m128i*>(pcm + first));
				const __m128 lo = _mm_cvtepi32_ps(_mm_srai_epi32(_mm_unpacklo_epi16(x, x), 16));
				const __m128 hi = _mm_cvtepi32_ps(_mm_srai_epi32(_mm_unpackhi_epi16(x, x), 16));
				const __m128 p = _mm_mul_ps(_mm_loadu_ps(k), lo), q = _mm_mul_ps(_mm_loadu_ps(k + 4), hi);
				__m128 s = _mm_add_ss(p, _mm_shuffle_ps(p, p, 1));
				s = _mm_add_ss(s, _mm_movehl_ps(p, p));
				s = _mm_add_ss(s, _mm_shuffle_ps(p, p, 3));
				s = _mm_add_ss(s, q);
				s = _mm_add_ss(s, _mm_shuffle_ps(q, q, 1));
				acc = _mm_cvtss_f32(s);
			}
			else
			{
				for(int t = 0; t < Taps; ++t)
				{
					const int64_t i = first + t;
					if(i >= 0 && i < length)
						acc += k[t] * static_cast<float>(pcm[i]);
				}
			}
			_work[m] = fade ? acc * static_cast<float>(Work - 1 - m) / (Work - 1) : acc;
			position += move;
			if(loop)
			{
				if(!reverse && position >= end)
					position -= span;
				else if(reverse && position < loopAt - lag)
					position += span;
			}
		}
		v.position = position;
		if(fade)
			v.playing = false;
	}

	int64_t Dt2Player::stretchStep(const int _l) const
	{
		// TUNE and the note's pitch at the output rate, a float as the DSP's (its 96 kHz step doubled), then its fraction
		const auto pitch = static_cast<float>(2 * m_stepOf[_l]);
		return std::llround(pitch * m_lane[static_cast<size_t>(_l)].rate * static_cast<double>(Unit));
	}

	int64_t Dt2Player::stretchAdvance(const Lane& _v) const
	{
		// The portion over 2^BARS bars of four beats at the tempo, in source samples an output, held to [1/8, 4]; a
		// grain's worth in floats, as the DSP's
		const double ratio = (_v.end - _v.start) / (_v.bars * 4 * 60 / m_bpm * m_outRate);
		const auto held = static_cast<float>(std::clamp(ratio, SlowestStretch, FastestStretch));
		return static_cast<int64_t>(static_cast<double>(held * static_cast<float>(GrainHop)) * static_cast<double>(Unit));
	}

	void Dt2Player::stretchStart(Lane& _v) const
	{
		// From the start, or the end's last sample reversed: the time a grain back, a grain starting at once, so that the
		// first block fades into the first grain from the one that would have started a grain back (the oracle's first
		// block)
		const int64_t d = _v.reverse ? -1 : 1;
		const auto origin = static_cast<int64_t>(_v.reverse ? _v.end - 1 : _v.start) * Unit;
		_v.timeline = origin - d * stretchAdvance(_v);
		_v.grainAge = GrainHop;
		_v.stretchRestart = false;
	}

	void Dt2Player::renderStretch(const int _l, const bool _fadeOut, float* _out)
	{
		Lane& v = m_lane[static_cast<size_t>(_l)];
		if(m_samples)
		{
			const auto& slot = m_samples->slots[static_cast<size_t>(_l)];
			v.pcm = slot.pcm;
			v.length = slot.pcm ? slot.length : 0;
		}
		const int64_t step = stretchStep(_l), d = v.reverse ? -1 : 1;
		const auto start = static_cast<int64_t>(v.start) * Unit, end = static_cast<int64_t>(v.end) * Unit;
		// A loop goes round at least StartMargin samples, as the DSP's (st_noise_tiny_loop: a LEN of 100 samples reads 141)
		const int64_t span = std::max(end - start, static_cast<int64_t>(StartMargin) * Unit);
		const int64_t whole = static_cast<int64_t>(v.length) * Unit;
		if(v.grainAge >= GrainHop)
		{
			// A loop that went past the sample's end starts the grains again from STRT, as at the trig (a reversed one
			// goes on). The time moves a grain on and a grain starts there; the last one fades out a grain's read on from
			// where it started, held as the offset between the two before the time goes round a looped portion
			if(v.stretchRestart)
				stretchStart(v);
			int64_t next = v.timeline + d * stretchAdvance(v);
			v.behind = next - (v.timeline + d * step * GrainHop);
			// (a portion shorter than the time's move goes round as often as it takes: the DSP plays on there, its grains
			// not identified)
			while(v.loop && !v.reverse && next >= end)
				next -= span;
			while(v.loop && v.reverse && next < start)
				next += span;
			v.timeline = v.grain = next;
			v.grainAge = 0;
		}
		// Unlooped, the block whose read would pass the end is the last, faded out
		bool last = false;
		if(!v.loop)
		{
			const auto floor = static_cast<int64_t>(std::max(v.start - ReverseBelow, 0.0)) * Unit;
			last = v.reverse ? v.grain - StretchLookAhead * step < floor : v.grain + StretchLookAhead * step >= end;
		}
		const int16_t* const pcm = v.pcm;
		const auto length = static_cast<int64_t>(v.length);
		const auto at = [pcm, length](const int64_t _i)
		{
			return _i >= 0 && _i < length ? static_cast<float>(pcm[_i]) : 0.0f;
		};
		int64_t grain = v.grain;
		const int64_t behind = v.behind;
		bool restart = v.stretchRestart;
		for(int n = 0; n < BlockSize; ++n)
		{
			const int age = v.grainAge + n;
			const float in = age < GrainFade ? static_cast<float>(age) / (GrainFade - 1) : 1.0f;
			// The grain fading out reads round the sample past its end (reversed, past its first)
			int64_t lastGrain = grain - behind;
			if(!v.reverse && lastGrain >= whole)
				lastGrain -= whole;
			else if(v.reverse && lastGrain < 0)
				lastGrain += whole;
			// Both read linearly with the fading-in grain's fraction (the DSP's loop takes it for both)
			const float f = static_cast<float>(grain & (Unit - 1)) / static_cast<float>(Unit);
			const int64_t i = grain >> 31, j = lastGrain >> 31;
			const float a = at(i) + (at(i + 1) - at(i)) * f;
			float y = a;
			if(in < 1)
			{
				const float b = at(j) + (at(j + 1) - at(j)) * f;
				y = a * in + b * (1 - in);
			}
			float gain = last ? static_cast<float>(BlockSize - 1 - n) / (BlockSize - 1) : 1.0f;
			if(_fadeOut)
				gain *= 1 - static_cast<float>(n) / BlockSize;
			_out[n] = y * gain / 32768;
			grain += d * step;
			if(v.loop)
			{
				// Round the portion, the grain fading out with it; past the sample's own end, the grains start again at
				// the next grain
				if(!v.reverse && grain >= end)
				{
					grain -= span;
					restart = restart || end == whole;
				}
				else if(v.reverse && grain <= start)
					grain += span;
			}
		}
		v.grain = grain;
		v.stretchRestart = restart;
		v.grainAge += BlockSize;
		if(last)
			v.playing = false;
	}

	void Dt2Player::decimate(const int _l, const float* _work, const bool _fadeOut, float* _out)
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
			_out[n] = static_cast<float>(0.5 * (y0 + o)) * gain;
		}
		v.x[0] = x0; v.x[1] = x1; v.x[2] = x2; v.x[3] = x3;
		v.y[0] = y0; v.y[1] = y1; v.y[2] = y2; v.y[3] = y3;
		for(int d = 0; d < OddDelay; ++d)
			v.oddTail[d] = _work[2 * (BlockSize - OddDelay + d) + 1];
	}

	void Dt2Player::renderLane(const int _lane, float* _out, const bool _fadeOut)
	{
		Lane& v = m_lane[static_cast<size_t>(_lane)];
		if(!v.playing)
		{
			std::fill_n(_out, BlockSize, 0.0f);
			return;
		}
		if(v.stretch)
			renderStretch(_lane, _fadeOut, _out);
		else
		{
			alignas(16) float work[Work];
			render96(_lane, work);
			decimate(_lane, work, _fadeOut, _out);
		}
		if(_fadeOut)
			v.playing = false;
	}

	const Block& Dt2Player::renderBlock()
	{
		const FlushDenormals ftz;
		m_controls.trigs.start();
		for(int l = 0; l < m_lanes; ++l)
		{
			auto& trigs = m_controls.trigs;
			if(trigs.now[l])
				startVoice(l);
			// A trig seen in this block: the voice fades out over it, the Hit sounds from the next
			float out[BlockSize];
			renderLane(l, out, trigs.pending[l]);
			for(int n = 0; n < BlockSize; ++n)
				m_out.s[n][l] = out[n];
		}
		m_controls.trigs.end();
		return m_out;
	}
}
