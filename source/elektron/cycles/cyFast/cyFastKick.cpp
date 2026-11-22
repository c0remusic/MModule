#include "cyFastKick.h"

#include "cyFastFm.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>

namespace cycles::fast
{
	using syntakt::fast::FlushDenormals;

	namespace
	{
#include "cyFastKickCurves.inc"

		std::array<float, 128> make(const Curve& _c) { return toFloat(makeTable(_c)); }

		// The tables, once a process: the curves and operator 2's wave (256 entries and the first again)
		struct Tables
		{
			std::array<float, 128> sweepDepth, sweepDecay, clickLevel, indexDecay, indexTarget, feedback, shapeGain,
				colorGain, ampDecay;
			std::array<float, 257> wave2;

			Tables() : sweepDepth(make(SweepDepth)), sweepDecay(make(SweepDecay)), clickLevel(make(ClickLevel)),
				indexDecay(make(IndexDecay)), indexTarget(make(IndexTarget)), feedback(make(Feedback)),
				shapeGain(make(ShapeGain))
			{
				for(int i = 0; i < 128; ++i)
				{
					colorGain[static_cast<size_t>(i)] = static_cast<float>(ColorA * (std::exp(ColorB * i) - 1));
					const double tau = DecayA * std::exp(DecayB * i) + DecayC;
					ampDecay[static_cast<size_t>(i)] = static_cast<float>(-std::exp(-1 / tau));
				}
				// Operator 2's wave: five harmonics, 128, 22, 11, 7 and 3 over 171, floored to 16 bits
				constexpr double Harmonics[5]{128, 22, 11, 7, 3};
				for(int i = 0; i < 257; ++i)
				{
					const double t = 2 * 3.14159265358979323846 * i / 256;
					double w = 0;
					for(int k = 0; k < 5; ++k)
						w += Harmonics[k] / 171 * std::sin((k + 1) * t);
					wave2[static_cast<size_t>(i)] = static_cast<float>(std::floor(w * 32768) / 32768);
				}
			}
		};

		const Tables& tables()
		{
			static const Tables t;
			return t;
		}

		// The four operators' increments (cyFastFm.h's increments): the fine pitch at its centre, operators 0-2 at
		// twice operator 3, every factor 1/2
		void increments(const int32_t _pitch, const int _note, const int64_t _sweep, const float _depth, int64_t* _inc)
		{
			constexpr int64_t Multiplier[4]{0x8000000, 0x8000000, 0x8000000, 0x4000000};
			constexpr int64_t Factor[4]{0x40000000, 0x40000000, 0x40000000, 0x40000000};
			const int64_t depth = std::llround(static_cast<double>(_depth) * 2147483648.0);
			fast::increments(_pitch, _note, -0x580000, depth, _sweep, Multiplier, Factor, _inc);
		}

		// A Q31 constant of the OS's code as a float
		constexpr float q(const uint32_t _word)
		{
			return static_cast<float>(static_cast<int32_t>(_word) / 2147483648.0);
		}

		// Operator 2's index on operator 1 (fm(0x1000000, 0xF1EB851F)) and operator 3's on the mix, in cycles
		const float Op2Index = static_cast<float>(s32(((0x1000000LL * s32(0xF1EB851F) * 2) >> 24) >> 8) / 16777216.0);
		constexpr float Op3Index = 16777215.0f / 16777216.0f;
		// Operator 2's envelope, from 1 toward 0.75: e <- c e + 0.75 (1 - c)
		constexpr float Op2Pole = -q(0x8AA5CA3B), Op2Target = q(0x60000000);
		// The click: its fall a sample (1 to 0 in 479 samples); its filters y = b0 x + b1 x1 - a1 y1, high-pass at
		// 250 Hz, low-pass at 3441 Hz
		constexpr float ClickFall = 0x4468C0 / 2147483648.0f;
		constexpr float HighPass[3]{q(0x7DF07773), q(0x820F888D), q(0x841F111A)};
		constexpr float LowPass[3]{q(0x17DB49CB), q(0x17DB49CB), q(0xAFB69397)};
		// The drive: PUNCH off, 0.25 x 4 into the clip (none) and 0.43 after; on, 0.5 x 4 and 0.49, its gain a
		// block-rate filter y = c0 x + c1 x1 - c2 y1 falling from 1 to 0.5 (14 ms), then rising back (139 ms)
		constexpr float PunchFall[3]{q(0x2E3F426), q(0x2E3F426), q(0x85C7E84D)};
		constexpr float PunchRise[3]{q(0x4E99D0), q(0x4E99D0), q(0x809D339F)};
		constexpr float PostOff = q(0x370A3D71), PostOn = q(0x3EB851EC);
	}

	Kick::Kick()
	{
		tables();
		warmUp();
		m_out = Block{};
		for(int l = 0; l < Lanes; ++l)
		{
			m_controls.notes[l] = 60;
			for(int i = 0; i < Parameters; ++i)
				m_controls.setParameter(l, i, Defaults[static_cast<size_t>(i)], 0);
		}
	}

	void Kick::setLanes(const int _lanes)
	{
		m_lanes = _lanes;
		syntakt::fast::setLanes(_lanes, {&m_out});
	}

	void Kick::start(Voice& _v, const int _l)
	{
		const auto& t = tables();
		const auto word = [this, _l](const int _i) { return m_controls.words[_i][_l]; };
		_v.started = true;
		std::fill(std::begin(_v.phase), std::end(_v.phase), 0u);
		_v.op2 = 1;
		_v.sweep = 0x7fffffff;
		const float sweepDecay = t.sweepDecay[static_cast<size_t>(hb(word(Sweep)))];
		_v.sweepCoef = std::llround(-static_cast<double>(sweepDecay) * 2147483648.0);
		_v.index = 1;
		_v.indexHeld = true;
		_v.indexCoef = -t.indexDecay[static_cast<size_t>(hb(word(Contour)))];
		_v.amp = 1;
		_v.ampStage = word(Gate) >> 8 >= 64 ? 2 : 1;
		_v.released = false;
		_v.feedback = lookup(t.feedback, word(Shape));
		_v.last = 0;
		_v.colorGain = lookup(t.colorGain, word(Color));
		_v.shapeGain = lookup(t.shapeGain, word(Shape));
		_v.click = 1;
		_v.clickLevel = lookup(t.clickLevel, word(Shape));
		std::fill(std::begin(_v.hp), std::end(_v.hp), 0.0f);
		std::fill(std::begin(_v.lp), std::end(_v.lp), 0.0f);
		_v.punch = word(Punch) >> 8 >= 64;
		_v.punchStage = 1;
		_v.punchIn = _v.punchGain = 1;
	}

	void Kick::render(Voice& _v, const int _l, const bool _trigBlock, const bool _cut, const bool _release)
	{
		const auto& t = tables();
		const auto& sine = fast::sine();
		const auto word = [this, _l](const int _i) { return m_controls.words[_i][_l]; };
		// The block-rate control: the sweep (held at 1 in the trig's block), the increments, the curves' values now
		if(!_trigBlock)
			_v.sweep = q31(_v.sweep, _v.sweepCoef);
		int64_t inc[Units];
		const float depth = t.sweepDepth[static_cast<size_t>(hb(word(Sweep)))];
		increments(word(Pitch), m_controls.notes[_l], _v.sweep, depth, inc);
		const float level = lookup(t.clickLevel, word(Shape));
		const float colorGain = lookup(t.colorGain, word(Color)), shapeGain = lookup(t.shapeGain, word(Shape));
		const float feedback = lookup(t.feedback, word(Shape));
		const float target = lookup(t.indexTarget, word(Contour));
		const float ampCoef = -t.ampDecay[static_cast<size_t>(hb(word(Decay)))];

		// Operator 2's envelope and the index envelope (the latter holds one block more), each ramped over the block
		const float op2From = _v.op2;
		if(!_trigBlock)
			_v.op2 = Op2Pole * _v.op2 + Op2Target * (1 - Op2Pole);
		const float indexFrom = _v.index;
		if(!_trigBlock)
		{
			if(_v.indexHeld)
				_v.indexHeld = false;
			else
				_v.index = _v.indexCoef * _v.index + target * (1 - _v.indexCoef);
		}
		// The amp: GATE's hold ends a block after a note-off, its decay the block after that; a trig's block ramps the
		// voice out, the index envelope with it
		const float ampFrom = _v.amp;
		if(!_trigBlock)
		{
			if(_v.ampStage == 2)
			{
				if(_v.released)
				{
					_v.ampStage = 1;
					_v.released = false;
				}
			}
			else
				_v.amp *= ampCoef;
		}
		if(_release)
			_v.released = true;
		const float ampTo = _cut ? 0.0f : _v.amp, indexTo = _cut ? 0.0f : _v.index;

		// The drive's gain, a block-rate filter with PUNCH on
		const float punchFrom = _v.punchGain;
		if(_v.punch)
		{
			const float* c = _v.punchStage == 1 ? PunchFall : PunchRise;
			const float in = _v.punchStage == 1 ? 0.0f : 1.0f;
			float y = c[0] * in + c[1] * _v.punchIn - c[2] * _v.punchGain;
			if(y <= 0.5f)
			{
				_v.punchStage = 5;
				y = 0.5f;
			}
			_v.punchIn = in;
			_v.punchGain = y;
		}
		const float drive = _v.punch ? 2.0f : 1.0f, post = _v.punch ? PostOn : PostOff;

		const float colorStep = (colorGain - _v.colorGain) / BlockSize;
		const float shapeStep = (shapeGain - _v.shapeGain) / BlockSize;
		float last = _v.last;
		float* out = &m_out.s[0][0] + _l;
		for(int n = 0; n < BlockSize; ++n)
		{
			const float r = static_cast<float>(n) / BlockSize;
			const auto at = [&](const int _k)
			{
				const uint32_t step = static_cast<uint32_t>(n) * static_cast<uint32_t>(inc[_k]);
				const uint32_t p = (_v.phase[_k] + step) & 0xffffff;
				return static_cast<float>(p) / 16777216.0f;
			};
			// Operator 0 on its own last sample, its feedback ramped from the last block's
			const float fb = _v.feedback + (feedback - _v.feedback) * r;
			last = wave(sine, at(0) + 4 * fb * last);
			const float s1 = wave(sine, at(1));
			const float s2 = wave(t.wave2, at(2) + Op2Index * s1) * (op2From + (_v.op2 - op2From) * r);
			// The mix: its gains ramp from the new value on, as the OS's loop, which runs twice, leaves them
			const float mix = ((colorGain + colorStep * n) * s2 + (shapeGain + shapeStep * n) * last)
				* (indexFrom + (indexTo - indexFrom) * r);
			const float s3 = wave(sine, at(3) + Op3Index * mix);
			// The click: a ramp from 1 to 0 times its level, high-passed, low-passed
			float x = std::max(_v.click - ClickFall * n, 0.0f) * (_v.clickLevel + (level - _v.clickLevel) * r);
			float y = HighPass[0] * x + HighPass[1] * _v.hp[0] - HighPass[2] * _v.hp[1];
			_v.hp[0] = x;
			_v.hp[1] = y;
			x = y;
			y = LowPass[0] * x + LowPass[1] * _v.lp[0] - LowPass[2] * _v.lp[1];
			_v.lp[0] = x;
			_v.lp[1] = y;
			const float g = punchFrom + (_v.punchGain - punchFrom) * r;
			const float driven = g * post * std::clamp(drive * s3, -1.0f, 1.0f);
			const float voice = std::clamp(driven + y, -1.0f, 1.0f) * (ampFrom + (ampTo - ampFrom) * r);
			out[n * Lanes] = voice / 2;
		}
		for(int k = 0; k < Units; ++k)
			_v.phase[k] = (_v.phase[k] + static_cast<uint32_t>(BlockSize) * static_cast<uint32_t>(inc[k])) & 0xffffff;
		_v.last = last;
		_v.feedback = feedback;
		_v.colorGain = colorGain;
		_v.shapeGain = shapeGain;
		_v.click = std::max(_v.click - ClickFall * BlockSize, 0.0f);
		_v.clickLevel = level;
		if(_cut)
		{
			_v.amp = 0;
			_v.index = 0;
		}
	}

	const Block& Kick::renderBlock()
	{
		const FlushDenormals ftz;
		auto& trigs = m_controls.trigs;
		trigs.start();
		for(int l = 0; l < m_lanes; ++l)
		{
			Voice& v = m_voice[static_cast<size_t>(l)];
			const bool trigBlock = trigs.now[l];
			if(trigBlock)
				start(v, l);
			if(!v.started)
			{
				for(int n = 0; n < BlockSize; ++n)
					m_out.s[n][l] = 0;
				continue;
			}
			render(v, l, trigBlock, trigs.pending[l], trigs.releases(l));
		}
		trigs.end();
		return m_out;
	}
}
