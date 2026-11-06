#include "mdFastGateBox.h"

#include <algorithm>
#include <cmath>

#include "mdFastDsp.h"
#include "stFastStages.h"

namespace md::fast
{
	namespace
	{
		constexpr int RingFrames = 0x2000;		// the predelay's $4000 words
		constexpr double Full = 8388608.0;		// 2^23
		constexpr double SampleRate = 44100.0;

		// The lines' delays (from their read to their write pointer), primes, and the buffers that hold them
		constexpr int Delays[GateBox::Lines] = {2663, 2447, 1811, 1621, 1399, 613};
		constexpr int LineWords[GateBox::Lines] = {0xa87, 0x9af, 0x733, 0x675, 0x800, 0x400};

		// The allpasses: their lengths, primes, and gains, the code's constants
		constexpr int AllpassWords[5] = {47, 67, 101, 149, 223};
		constexpr float AllpassGains[5] = {-0.96308f, 0.94779f, -0.92235f, 0.88759f, -0.83655f};

		float fraction(const int32_t _word) { return static_cast<float>(_word) / static_cast<float>(Full); }

		float limit(const float _v) { return std::clamp(_v, -1.0f, 8388607.0f / 8388608.0f); }

		// The DSP's fractional product of two words, stored
		int32_t mul(const int32_t _a, const int32_t _b) { return dsp::lim(dsp::mpy(_a, _b)); }

		uint32_t word(const double _v) { return static_cast<uint32_t>(std::floor(_v * Full + 0.5)); }

		GateBox::Tables makeTables()
		{
			GateBox::Tables t;
			for(size_t r = 0; r < GateBox::Rows; ++r)
			{
				const double time = 0.4 * std::pow(10.0, static_cast<double>(r) / 63.0);
				const double pole = (Full - 1 - static_cast<double>(r) * (2.5e-5 * Full)) / Full;
				for(size_t l = 0; l < GateBox::Lines; ++l)
				{
					t.decay[r][l] = word(std::sqrt(0.5) * std::pow(10.0, -3 * Delays[l] / (SampleRate * time)));
					t.damping[r][l] = word(std::pow(pole, Delays[l]));
				}
			}
			return t;
		}
	}

	const GateBox::Tables& GateBox::tables()
	{
		static const Tables t = makeTables();
		return t;
	}

	GateBox::GateBox(const uint32_t* _sine) : m_sine(_sine)
	{
		size_t words = 2 * RingFrames;
		for(const int size : AllpassWords)
			words += static_cast<size_t>(size);
		for(const int size : LineWords)
			words += static_cast<size_t>(size);
		m_memory.assign(words, 0.0f);

		float* next = m_memory.data();
		const auto take = [&next](const int _words)
		{
			float* const words = next;
			next += _words;
			return words;
		};
		m_ring = take(2 * RingFrames);
		for(size_t k = 0; k < m_allpass.size(); ++k)
		{
			m_allpass[k].words = take(AllpassWords[k]);
			m_allpass[k].size = AllpassWords[k];
		}
		for(size_t l = 0; l < Lines; ++l)
		{
			m_lines[l].words = take(LineWords[l]);
			m_lines[l].size = LineWords[l];
			m_lines[l].write = Delays[l];
		}
		setWords({0x347, static_cast<uint32_t>(dsp::wordOf(0.999)), 0x32, 0, 0, 0xf4240, 0, 0});
	}

	void GateBox::setWords(const GateBoxWords& _words)
	{
		m_highCut = fraction(dsp::sx(_words[0]));
		m_lowCut = fraction(dsp::sx(_words[1]));
		const auto& decayRow = tables().decay[_words[2]];
		const auto& dampingRow = tables().damping[_words[3]];
		for(size_t l = 0; l < Lines; ++l)
		{
			m_decay[l] = fraction(dsp::sx(decayRow[l]));
			m_damping[l] = fraction(dsp::sx(dampingRow[l]));
		}

		// P:$481: the allpasses' gains by 0.7 + 0.05 x row / 8, up to 1
		const int64_t scaled = (dsp::mpy(static_cast<int32_t>(_words[2]), 0x66666) << 17) + dsp::acc(0x59999a);
		const float spread = fraction(dsp::lim(scaled));
		for(size_t k = 0; k < m_allpassGain.size(); ++k)
			m_allpassGain[k] = limit(spread * AllpassGains[k]);

		m_level = fraction(dsp::sx(_words[4]));
		m_gateHold = static_cast<int32_t>(_words[5]);
		m_echoSend = fraction(dsp::sx(_words[6]));
		m_predelay = static_cast<int>(_words[7] / 2);
	}

	void GateBox::predelay(const float* _inL, const float* _inR, const float* _echoL, const float* _echoR,
		Samples& _mono)
	{
		// P:$44E
		int write = m_ringWrite;
		for(size_t i = 0; i < BlockSize; ++i)
		{
			m_ring[2 * write] = limit(_inL[i] + m_echoSend * _echoL[i]);
			m_ring[2 * write + 1] = limit(_inR[i] + m_echoSend * _echoR[i]);
			write = (write + 1) & (RingFrames - 1);
		}
		int read = (m_ringWrite - m_predelay) & (RingFrames - 1);
		m_ringWrite = write;
		for(auto& sample : _mono)
		{
			sample = 0.5f * (m_ring[2 * read] + m_ring[2 * read + 1]);
			read = (read + 1) & (RingFrames - 1);
		}
	}

	void GateBox::diffuse(Samples& _mono)
	{
		for(size_t k = 0; k < m_allpass.size(); ++k)
		{
			auto& line = m_allpass[k];
			const float gain = m_allpassGain[k];
			float state = m_allpassState[k];
			int at = line.read;
			for(auto& sample : _mono)
			{
				const float stored = limit(state);
				const float out = line.words[at] - gain * stored;
				line.words[at] = stored;
				if(++at == line.size)
					at = 0;
				state = sample + gain * line.words[at];
				sample = limit(out);
			}
			line.read = at;
			m_allpassState[k] = limit(state);
		}
	}

	void GateBox::readLines(std::array<Samples, Lines>& _reads)
	{
		// P:$4A1: the plain lines, the sample read in one step of the loop filtered in the next
		for(size_t l = 0; l < 4; ++l)
		{
			auto& line = m_lines[l];
			const float gain = m_damping[l];
			float low = m_lowState[l], held = m_readHeld[l];
			int at = line.read;
			for(auto& out : _reads[l])
			{
				const float stored = limit(low);
				out = stored;
				low += gain * (held - stored);
				held = line.words[at];
				if(++at == line.size)
					at = 0;
			}
			line.read = at;
			m_lowState[l] = low;
			m_readHeld[l] = held;
		}

		// P:$4F3: the sine moves the last two lines once a block: its top four bits a whole offset of -8 to 7, the rest
		// the interpolation's fraction
		const int32_t sine = dsp::sx(m_sine[m_sinePhase]);
		m_sinePhase = (m_sinePhase + 32) & 0x7fff;
		const int offset = sine >> 20;
		const float gain = m_damping[4];
		const float gainNext = gain * fraction((sine & 0xfffff) << 3);
		const float gainHere = gain - gainNext;
		for(size_t l = 4; l < Lines; ++l)
		{
			auto& line = m_lines[l];
			float low = m_lowState[l];
			int at = line.read + offset;
			if(at < 0)
				at += line.size;
			else if(at >= line.size)
				at -= line.size;
			for(auto& out : _reads[l])
			{
				const float stored = limit(low);
				out = stored;
				const float here = line.words[at];
				if(++at == line.size)
					at = 0;
				low += gainHere * here + gainNext * line.words[at] - gain * stored;
			}
			line.read = (line.read + BlockSize) % line.size;
			m_lowState[l] = low;
		}
	}

	void GateBox::writeLines(const std::array<Samples, Lines>& _reads, const Samples& _mono)
	{
		// P:$544 and P:$56A: the network, each line's write a sum and a difference of two reads, and a sixth of the
		// diffused input into each
		const auto& d = m_decay;
		for(size_t i = 0; i < BlockSize; ++i)
		{
			const float r0 = _reads[0][i], r1 = _reads[1][i], r2 = _reads[2][i], r3 = _reads[3][i], r4 = _reads[4][i],
				r5 = _reads[5][i];
			const float a = d[0] * r0, b = d[3] * r3, c = d[1] * r1, e = d[2] * r2, f = -d[4] * r4, g = d[5] * r5;
			const float sixth = _mono[i] * 0.16666666f;
			const float writes[Lines] = {limit(f + g), limit(a + b), limit(a - b), limit(f - g), limit(c + e),
				limit(c - e)};
			for(size_t l = 0; l < Lines; ++l)
			{
				auto& line = m_lines[l];
				line.words[line.write] = limit(writes[l] + sixth);
				if(++line.write == line.size)
					line.write = 0;
			}
		}
	}

	void GateBox::gate(const Samples& _mono, Samples& _gains)
	{
		// P:$5B6: an envelope of the diffused input, in the DSP's integers: its truncation at every step sets how low
		// it follows, which decides when a Hit opens the gate. Then the block's rise of its peak
		int64_t envelope = dsp::acc(m_envelope), peak = 0;
		for(const float sample : _mono)
		{
			const auto word = static_cast<int32_t>(std::fabs(sample) * static_cast<float>(Full));
			envelope = dsp::mpy(dsp::lim(envelope), 0x7fef9e) + dsp::mpy(word, 0x1062);
			peak = std::max(peak, envelope);
		}
		const int64_t rise = peak - dsp::acc(m_peak);
		m_envelope = dsp::lim(envelope);
		m_peak = dsp::lim(peak);

		m_gateCount = m_gateCount >= m_gateHold ? 0x7ffff0 : m_gateCount + 1;

		// A hold of $7FFFFF never closes it; a hold of 0 needs the Hit itself, past the count
		const bool held = m_gateHold == 0x7fffff;
		const bool struck = !held && rise >= dsp::acc(0xa8);
		if(struck)
			m_gateCount = 0;

		if(held || struck || m_gateCount < m_gateHold)
		{
			// P:$5EA: up by 0.02 every two samples, 0.01 between, the words limited
			constexpr int32_t top = 0x7fffff, rising = 0x28f5c, half = 0x147ae;
			for(size_t i = 0; i < BlockSize; i += 2)
			{
				_gains[i] = fraction(std::min(m_gateGain, top));
				_gains[i + 1] = fraction(std::min(m_gateGain + half, top));
				m_gateGain += rising;
			}
			m_gateGain = std::min(m_gateGain, top);
			return;
		}

		// P:$5F8: down by 0.99 every two samples, 0.995 between
		constexpr int32_t fall = 0x7eb852, half = 0x7f5bc0;
		for(size_t i = 0; i < BlockSize; i += 2)
		{
			_gains[i] = fraction(m_gateGain);
			_gains[i + 1] = fraction(mul(m_gateGain, half));
			m_gateGain = mul(m_gateGain, fall);
		}
	}

	void GateBox::process(const float* _inL, const float* _inR, const float* _echoL, const float* _echoR, float* _outL,
		float* _outR)
	{
		// A tail in float reaches denormals where the DSP's words stop at their last bit
		const syntakt::fast::FlushDenormals ftz;

		Samples mono;
		predelay(_inL, _inR, _echoL, _echoR, mono);
		diffuse(mono);

		std::array<Samples, Lines> reads;
		readLines(reads);

		// P:$554: the return, before the network writes
		const auto& d = m_decay;
		Samples left, right;
		for(size_t i = 0; i < BlockSize; ++i)
		{
			const float common = d[2] * reads[2][i] - d[5] * reads[5][i] - d[3] * reads[3][i];
			left[i] = limit(2 * (common + d[1] * reads[1][i]));
			right[i] = limit(2 * (common + d[0] * reads[0][i]));
		}

		writeLines(reads, mono);

		Samples gains;
		gate(mono, gains);

		// P:$615 and P:$644: the gate, the high-pass, the low-pass, a sample's delay each, then the level, 16 x LEV
		for(size_t c = 0; c < 2; ++c)
		{
			const auto& in = c ? right : left;
			float* out = c ? _outR : _outL;
			float high = m_highState[c], last = m_highLast[c], low = m_outState[c];
			for(size_t i = 0; i < BlockSize; ++i)
			{
				const float x = limit(in[i] * gains[i]);
				const float stored = limit(high);
				high += 0.5f * (x - last) - m_highCut * stored;
				last = x;
				const float lowStored = limit(low);
				low += m_lowCut * (stored - lowStored);
				out[i] = limit(16 * m_level * lowStored);
			}
			m_highState[c] = high;
			m_highLast[c] = last;
			m_outState[c] = low;
		}
	}
}
