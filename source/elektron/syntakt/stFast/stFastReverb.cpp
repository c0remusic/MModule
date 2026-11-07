#include "stFastReverb.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>

#include "stFastStages.h"
#include "stFastTables.h"

namespace syntakt::fast
{
	namespace
	{
		constexpr int Frames = Reverb::BlockSize;
		constexpr double Pi = 3.14159265358979323846;
		constexpr int DiffuserFrames = 1024, LineFrames = 16384;	// at 48 kHz

		// All measured on the exact port's responses at 48 kHz, none read from its memory. The first 800 samples of an
		// impulse's return give the diffusers: echoes at their lengths, whose sizes give the gains, which alternate in
		// sign and fall by a law, set finer on the null itself. A send made of those diffusers' response time-reversed
		// then comes out of them as one sample, and the return is the lines' echoes: their lengths, and for lines 4 and
		// 8, read between samples, where each stands block by block: the middle of its swing here, 8 samples either
		// side, 0.65 Hz over a minute. Line 1 is shorter than the 2539 the matrix reckons with (tables::reverbMatrix),
		// as its echo shows
		constexpr int DiffuserLengths[8] = {53, 73, 113, 163, 251, 367, 491, 809};
		constexpr double DiffuserDecay = 1.5001e-4;
		constexpr int LineLengths[8] = {2399, 3359, 4721, 7001, 2111, 3109, 4507, 6701};
		constexpr double SineHz = 0.65, Swing = 8;
		constexpr double MachineBlocks = 48000.0 / Frames;
		constexpr int PreDelayShortest = 0x25, PreDelayLongest = 0x4000;

		// The stores' saturation at full scale
		float rail(const float _x) { return std::min(std::max(_x, -1.0f), 1.0f); }

		// _n samples of a line from its sample _at, wrapping
		void read(const float* _line, const uint32_t _mask, const uint32_t _at, const int _n, float* _to)
		{
			const uint32_t first = _at & _mask, run = std::min<uint32_t>(static_cast<uint32_t>(_n), _mask + 1 - first);
			std::memcpy(_to, _line + first, run * sizeof(float));
			std::memcpy(_to + run, _line, (static_cast<uint32_t>(_n) - run) * sizeof(float));
		}

		void write(float* _line, const uint32_t _mask, const uint32_t _at, const float* _from)
		{
			const uint32_t first = _at & _mask, run = std::min<uint32_t>(Frames, _mask + 1 - first);
			std::memcpy(_line + first, _from, run * sizeof(float));
			std::memcpy(_line, _from + run, (Frames - run) * sizeof(float));
		}

		// The frames a line of _frames samples at 48 kHz takes at _scale times its rate: a power of two
		uint32_t mask(const int _frames, const double _scale)
		{
			uint32_t n = static_cast<uint32_t>(_frames);
			while(n < _frames * _scale)
				n <<= 1;
			return n - 1;
		}

		// The cubic through four samples at -1, 0, 1, 2, read at _t, 0 to 1
		void lagrange(const double _t, double* _w)
		{
			_w[0] = -_t * (_t - 1) * (_t - 2) / 6;
			_w[1] = (_t + 1) * (_t - 1) * (_t - 2) / 2;
			_w[2] = -(_t + 1) * _t * (_t - 2) / 2;
			_w[3] = (_t + 1) * _t * (_t - 1) / 6;
		}
	}

	Reverb::Reverb(const double _rate)
		: m_scale(_rate / 48000), m_sineRate(2 * Pi * SineHz / (MachineBlocks * m_scale)),
		  m_poles(tables::sendPoles(_rate)), m_shelves(tables::reverbShelves(_rate)),
		  m_diffusion(tables::reverbDiffusion()), m_matrix(tables::reverbMatrix()),
		  m_preMask(mask(LineFrames, m_scale)), m_diffuserMask(mask(DiffuserFrames, m_scale)),
		  m_lineMask(mask(LineFrames, m_scale))
	{
		for(size_t k = 0; k < Diffusers; ++k)
		{
			m_diffuserLength[k] = static_cast<int>(std::lround(DiffuserLengths[k] * m_scale));
			m_diffuserGain[k] = static_cast<float>((k & 1 ? -1 : 1) * std::exp(-DiffuserDecay * DiffuserLengths[k]));
		}
		for(size_t j = 0; j < Lines; ++j)
			m_lineLength[j] = static_cast<int>(std::lround(LineLengths[j] * m_scale));
		m_preLine.resize(size_t(m_preMask) + 1);
		m_diffuserLines.resize(Diffusers * (size_t(m_diffuserMask) + 1));
		m_lines.resize(Lines * (size_t(m_lineMask) + 1));
	}

	void Reverb::clear()
	{
		std::fill(m_preLine.begin(), m_preLine.end(), 0.0f);
		std::fill(m_diffuserLines.begin(), m_diffuserLines.end(), 0.0f);
		std::fill(m_lines.begin(), m_lines.end(), 0.0f);
		m_low = m_high = 0;
		m_half = {};
		std::memset(m_outs, 0, sizeof(m_outs));
		std::memset(m_input, 0, sizeof(m_input));
	}

	void Reverb::readLine(const int _line, float* const _to) const
	{
		const float* line = &m_lines[static_cast<size_t>(_line) * (size_t(m_lineMask) + 1)];
		const auto length = static_cast<uint32_t>(m_lineLength[static_cast<size_t>(_line)]);
		read(line, m_lineMask, m_write - length, Frames, _to);
	}

	void Reverb::readModulated(const int _line, const double _delay, float* const _to) const
	{
		// The cubic at a 32nd of a sample, and a straight line between two 32nds: the machine's table of rows
		const double whole = std::floor(_delay), steps = (_delay - whole) * 32;
		const double row = std::floor(steps), between = steps - row;
		double from[4], to[4];
		lagrange(row / 32, from);
		lagrange((row + 1) / 32, to);
		float w[4];
		for(int k = 0; k < 4; ++k)
			w[k] = static_cast<float>(from[k] + between * (to[k] - from[k]));

		float s[Frames + 3];
		const float* line = &m_lines[static_cast<size_t>(_line) * (size_t(m_lineMask) + 1)];
		read(line, m_lineMask, m_write - static_cast<uint32_t>(whole) - 2, Frames + 3, s);
		for(int i = 0; i < Frames; ++i)
			_to[i] = rail(w[3] * s[i] + w[2] * s[i + 1] + w[1] * s[i + 2] + w[0] * s[i + 3]);
	}

	// One of the two networks, as the machine's code runs them in turn over one buffer. Slot 2 and slot 3 of the buffer
	// are read for the return before this half's values are written there, in the same pass: slot 2 still holds the
	// last half's third line, slot 3 this half's raw tap, which is what the return hears
	void Reverb::half(const float* const _diffused, const int _firstLine, const float* const _matrix,
		const Shelf& _shelf, const double _delay, HalfState& _state, float* const _outL, float* const _outR,
		const bool _adding)
	{
		const float c0 = _shelf.c0, c1 = _shelf.c1, c2 = _shelf.c2;
		float a[Frames], b[Frames], third[Frames];
		readLine(_firstLine, a);
		readLine(_firstLine + 1, b);
		readLine(_firstLine + 2, third);
		readModulated(_firstLine + 3, _delay, m_outs[3]);

		float u = _state.first, v = _state.second, ya = _state.firstOut, yb = _state.secondOut;
		for(int i = 0; i < Frames; ++i)
		{
			const float next = rail(a[i] + _diffused[i]);
			ya = rail(c0 * next + c1 * u - c2 * ya);
			yb = rail(c0 * b[i] + c1 * v - c2 * yb);
			u = next;
			v = b[i];
			m_outs[0][i] = ya;
			m_outs[1][i] = yb;
		}
		_state.first = u;
		_state.second = v;
		_state.firstOut = ya;
		_state.secondOut = yb;

		float s1 = _state.third, s2 = _state.tap, y0 = _state.thirdOut, y1 = _state.tapOut;
		for(int i = 0; i < Frames; ++i)
		{
			const float raw = m_outs[3][i], old = m_outs[2][i];
			y0 = rail(c0 * third[i] + c1 * s1 - c2 * y0);
			y1 = rail(c0 * raw + c1 * s2 - c2 * y1);
			s1 = third[i];
			s2 = raw;
			if(_adding)
			{
				_outL[i] = rail(_outL[i] + m_outs[1][i] + raw);
				_outR[i] = rail(_outR[i] + m_outs[0][i] + old);
			}
			else
			{
				_outL[i] = rail(m_outs[0][i] + old);
				_outR[i] = rail(m_outs[1][i] + raw);
			}
			m_outs[2][i] = y0;
			m_outs[3][i] = y1;
		}
		_state.third = s1;
		_state.tap = s2;
		_state.thirdOut = y0;
		_state.tapOut = y1;

		// The lines' inputs: each keeps its own with a plus, the others' with a minus
		const float h0 = _matrix[0], h1 = _matrix[1], h2 = _matrix[2], h3 = _matrix[3];
		float* const in0 = m_input[_firstLine];
		float* const in1 = m_input[_firstLine + 1];
		float* const in2 = m_input[_firstLine + 2];
		float* const in3 = m_input[_firstLine + 3];
		for(int i = 0; i < Frames; ++i)
		{
			const float x0 = h0 * m_outs[0][i], x1 = h1 * m_outs[1][i], x2 = h2 * m_outs[2][i], x3 = h3 * m_outs[3][i];
			const float sum = x0 + x1 + x2 + x3;
			in0[i] = rail(2 * x0 - sum);
			in1[i] = rail(2 * x1 - sum);
			in2[i] = rail(2 * x2 - sum);
			in3[i] = rail(2 * x3 - sum);
		}
	}

	void Reverb::process(const float* const _inL, const float* const _inR, const float* const _delayL,
		const float* const _delayR, float* const _outL, float* const _outR)
	{
		const FlushDenormals ftz;

		const int pre = m_words.preDelay;
		const int preDelay = m_preDelay;
		m_preDelay = std::min((2 * pre * pre) >> 17, PreDelayLongest - PreDelayShortest) + PreDelayShortest;
		const auto preFrames = static_cast<uint32_t>(std::lround(preDelay * m_scale));

		float delayed[Frames], diffused[Frames];
		read(m_preLine.data(), m_preMask, m_write - preFrames, Frames, delayed);

		// The send: what the delay gives by REV, ramped, then to mono
		const float sendStep = (m_sendTarget - m_send) / Frames;
		float mono[Frames];
		for(int i = 0; i < Frames; ++i)
		{
			const float g = m_send + sendStep * static_cast<float>(i);
			mono[i] = 0.5f * (rail(_inL[i] + g * _delayL[i]) + rail(_inR[i] + g * _delayR[i]));
		}
		m_send = m_sendTarget;
		write(m_preLine.data(), m_preMask, m_write, mono);

		// The pre-delay's low-pass and high-pass
		const auto highPass = static_cast<size_t>(m_words.highPass >> 6);
		const auto lowPass = static_cast<size_t>((m_words.highPass + m_words.lowPass) >> 6);
		const float hp = m_poles[highPass], lp = m_poles[lowPass];
		const float toLow = 0.5f * (1 - lp), toHigh = 0.5f * (1 + hp);
		float low = m_low, high = m_high;
		for(int i = 0; i < Frames; ++i)
		{
			const float next = lp * low + toLow * delayed[i];
			high = hp * high + toHigh * (next - low);
			low = next;
			diffused[i] = high;
		}
		m_low = low;
		m_high = high;

		// The allpasses, each over a line of its own length, 49 to 743 samples at 44.1 kHz: all over a block, so that a
		// block's reads come before its writes
		const auto decay = static_cast<size_t>(m_words.decay >> 8);
		for(int k = 0; k < Diffusers; ++k)
		{
			const float g = m_diffuserGain[static_cast<size_t>(k)] * m_diffusion[decay], rest = 1 - g * g;
			float* line = &m_diffuserLines[static_cast<size_t>(k) * (size_t(m_diffuserMask) + 1)];
			float d[Frames], v[Frames];
			const auto length = static_cast<uint32_t>(m_diffuserLength[static_cast<size_t>(k)]);
			read(line, m_diffuserMask, m_write - length, Frames, d);
			for(int i = 0; i < Frames; ++i)
			{
				const float x = diffused[i];
				v[i] = rail(x + g * d[i]);
				diffused[i] = rail(rest * d[i] - g * x);
			}
			write(line, m_diffuserMask, m_write, v);
		}

		// The shelf's three coefficients: 1 at DC, GAIN at Nyquist
		const float f = m_shelves[static_cast<size_t>(m_words.frequency >> 8)];
		const float rest = 1 - f, shelfGain = rest * static_cast<float>(m_words.gain) / 32768;
		const Shelf shelf{f + shelfGain, f - shelfGain, f - rest};

		// The sine turns once a block, its two outputs a quarter of a period apart: one for each network
		m_sine -= m_sineRate * m_cosine;
		m_cosine += m_sineRate * m_sine;
		const auto& matrix = m_matrix[decay];
		const double first = (LineLengths[3] + Swing * m_sine) * m_scale;
		const double second = (LineLengths[7] + Swing * m_cosine) * m_scale;
		half(diffused, 0, matrix.data(), shelf, first, m_half[0], _outL, _outR, false);
		half(diffused, 4, matrix.data() + 4, shelf, second, m_half[1], _outL, _outR, true);

		for(int j = 0; j < Lines; ++j)
			write(&m_lines[static_cast<size_t>(j) * (size_t(m_lineMask) + 1)], m_lineMask, m_write, m_input[j]);
		m_write += Frames;
	}
}
