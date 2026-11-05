#include "mdFastEcho.h"

#include <algorithm>
#include <cstdlib>

#include <emmintrin.h>

#include "mdFastDsp.h"

namespace md::fast
{
	namespace
	{
		using dsp::acc;
		using dsp::lim;
		using dsp::mpy;

		constexpr int64_t LineEnd = acc(0x17afb);	// where a pointer goes back to the line's first word
		constexpr int LineWords = 0x17afc;			// from one line to the other

		// How far below a line a read can land, derived from P:$361. A pointer sent back over the line's end starts at
		// -1 at the lowest. Then a block of the read rate at its most negative: the rate word pulled back by a distance
		// error of at most the line, over 4, by $200 and by the triangle's half of a depth under $8000, with the write
		// rate above 0 helping. A host reaches -67 (mdFastSendsTest). The DSP has there 64 zeros below the first line,
		// then its boot's code, which this reads as zeros, and the other line's tail below the second
		constexpr int MaxPull = LineWords / 4 + 0x200 + 0x4000;
		constexpr int Guard = 2 + (MaxPull * Echo::BlockSize >> 14);

		int32_t word(const float _sample)
		{
			return _mm_cvtss_si32(_mm_set_ss(std::clamp(_sample, -1.0f, 8388607.0f / 8388608.0f) * 8388608.0f));
		}

		float sample(const int32_t _word) { return static_cast<float>(_word) / 8388608.0f; }

		// A block of a pointer's addresses: at each sample a step, back to the line's first word past its end. Returns
		// where it stopped
		int64_t walk(int64_t _position, const int64_t _step, std::array<int32_t, Echo::BlockSize>& _at)
		{
			for(auto& at : _at)
			{
				_position += _step;
				if(_position >= LineEnd)
					_position = 0;
				at = static_cast<int32_t>(_position >> 24);
			}
			return _position;
		}
	}

	Echo::Echo() : m_lines(Guard + 2 * LineWords, 0)
	{
	}

	void Echo::setWords(const EchoWords& _words)
	{
		m_targetDistance = static_cast<int32_t>(_words[0]);
		m_targetRate = static_cast<int32_t>(_words[1]);
		m_lfoDepth = static_cast<int32_t>(_words[2]);
		m_lfoStep = static_cast<int32_t>(_words[3]);
		m_feedback = static_cast<int32_t>(_words[4]);
		m_highCut = static_cast<int32_t>(_words[5]);
		m_lowCut = static_cast<int32_t>(_words[6]);
		m_cross = static_cast<int32_t>(_words[7]);
		m_straight = 0x7eb852 - m_cross;
		m_level = static_cast<int32_t>(_words[8]);
	}

	void Echo::advancePointers()
	{
		// P:$346: the rate glides to its target, and the write pointer runs at it, its fraction lost at each block
		const int32_t rateError = m_targetRate - m_rate;
		if(rateError != 0)
			m_rate += m_rateStep;
		m_rateStep = rateError >> 1;

		const int64_t writeStep = int64_t(m_rate) << 10;
		m_write = static_cast<int32_t>(walk(acc(m_write), writeStep, m_writeAt) >> 24);
		const int64_t written = (BlockSize * writeStep) >> 24;

		// P:$361: the read pointer's rate pulls its distance to the target, the triangle added
		int64_t rate = acc(m_rate);
		const int32_t distanceError = m_targetDistance - m_distance;
		if(distanceError > 0)
			rate += (acc(distanceError) >> 1) + acc(0x200);
		else if(distanceError < 0)
			rate += (acc(distanceError) >> 2) - acc(0x200);

		// The phase wraps over 24 bits
		m_lfoPhase = dsp::sx(static_cast<uint32_t>(m_lfoPhase + m_lfoStep));
		rate += mpy(m_lfoDepth, std::abs(m_lfoPhase) - 0x400000);
		rate = std::min(rate, acc(0x8000));

		// P:$381: near either end of the line, a rate that goes further is dropped
		const int64_t unity = acc(0x4000);
		if(m_distance > 0xa0 ? m_distance >= 0x17a5b && rate >= unity : rate <= unity)
			rate = acc(m_rate);

		const int64_t readStep = rate >> 14;
		int64_t position = acc(m_write) + acc(m_distance);
		if(position >= LineEnd)
			position -= acc(LineWords);
		walk(position, readStep, m_readAt);
		m_distance = static_cast<int32_t>((acc(m_distance) + BlockSize * readStep - acc(static_cast<int32_t>(written)))
			>> 24);
	}

	void Echo::process(const float* _inL, const float* _inR, float* _outL, float* _outR)
	{
		advancePointers();

		int32_t* const left = m_lines.data() + Guard;
		int32_t* const right = left + LineWords;

		std::array<std::array<int32_t, BlockSize>, 2> loop;
		for(size_t i = 0; i < BlockSize; ++i)
		{
			// P:$3B2: every read before any write of the block, which a short distance would otherwise see
			const int32_t readL = left[m_readAt[i]];
			const int32_t readR = right[m_readAt[i]];

			_outL[i] = sample(lim(mpy(m_level, readL)));
			_outR[i] = sample(lim(mpy(m_level, readR)));

			const int32_t sendL = lim((mpy(m_feedback, readL) + acc(word(_inL[i]))) * 2);
			const int32_t sendR = lim((mpy(m_feedback, readR) + acc(word(_inR[i]))) * 2);
			loop[0][i] = lim(mpy(m_straight, sendL) + mpy(m_cross, sendR));
			loop[1][i] = lim(mpy(m_straight, sendR) + mpy(m_cross, sendL));
		}

		// P:$3FB and P:$416, a channel at a time
		for(size_t c = 0; c < 2; ++c)
		{
			int64_t high = acc(m_highState[c]), low = acc(m_lowState[c]);
			int32_t last = m_highLast[c];
			for(size_t i = 0; i < BlockSize; ++i)
			{
				const int32_t in = loop[c][i];
				high += acc(in - last) - mpy(m_highCut, lim(high));
				last = in;
				const int32_t out = lim(low);
				low += mpy(m_lowCut, lim(high)) - mpy(m_lowCut, out);
				loop[c][i] = out;
			}
			m_highState[c] = lim(high);
			m_highLast[c] = last;
			m_lowState[c] = lim(low);
		}

		for(size_t i = 0; i < BlockSize; ++i)
		{
			left[m_writeAt[i]] = loop[0][i];
			right[m_writeAt[i]] = loop[1][i];
		}
	}
}
