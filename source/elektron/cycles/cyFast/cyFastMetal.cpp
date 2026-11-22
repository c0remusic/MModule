#include "cyFastMetal.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace cycles::fast
{
	using syntakt::fast::FlushDenormals;

	namespace
	{
#include "cyFastMetalCurves.inc"

		// The tables, once a process: in double where the voice reads them as values, in the OS's integers where they
		// reach its integer chains (the ratios, SHPE's level); and operators 1 and 3's wave (256 entries and the first
		// again): harmonics 1, 4, 7, 10 and 13 in the ratio 50 : 67 : 41 : 14 : 2, over their sum
		struct Tables
		{
			Table evenDecay, oddDecay, evenTarget, ampDecay;
			std::array<int32_t, 128> ratio1, ratio2, shapeLevel;
			std::array<float, 257> wave;

			Tables() : evenDecay(makeTable(EvenDecay)), oddDecay(makeTable(OddDecay)),
				evenTarget(makeTable(EvenTarget)), ampDecay(makeTable(AmpDecay)), ratio1(toQ31(makeTable(Ratio1))),
				ratio2(toQ31(makeTable(Ratio2))), shapeLevel(toQ31(makeTable(ShapeLevel)))
			{
				constexpr double Harmonics[5]{1, 4, 7, 10, 13}, Weights[5]{50, 67, 41, 14, 2};
				for(int i = 0; i < 257; ++i)
				{
					double w = 0;
					for(int k = 0; k < 5; ++k)
						w += Weights[k] * std::sin(2 * 3.14159265358979323846 * Harmonics[k] * i / 256);
					wave[static_cast<size_t>(i)] = static_cast<float>(w / 174);
				}
			}
		};

		const Tables& tables()
		{
			static const Tables t;
			return t;
		}

		// The machine's -40 semitones; every factor 1/2
		constexpr int64_t Offset = -0x280000;
		constexpr int64_t Factor[4]{0x40000000, 0x40000000, 0x40000000, 0x40000000};
		// The resonant high-pass (3485 Hz), y = b0 x + b1 x1 + b2 x2 - a1 y1 - a2 y2
		const float B0 = static_cast<float>(fraction(0x3A2102DE)), B1 = static_cast<float>(fraction(0x8BBDFA44));
		const float A1 = static_cast<float>(2 * fraction(0x92100C93)), A2 = static_cast<float>(fraction(0x75283018));
		// The drive: PUNCH off, 0.75; on, the gain x 1.32 into the clip, falling from 1 to 0.33 (68 ms) then rising
		// back (555 ms)
		constexpr double PunchFall[3]{fraction(0xA0AAAA), fraction(0xA0AAAA), fraction(0x81415554)};
		constexpr double PunchRise[3]{fraction(0x13B13D), fraction(0x13B13D), fraction(0x8027627B)};
		constexpr double PunchFloor = fraction(0x2A3D70A4);
		constexpr float One = static_cast<float>(Max31 / 2147483648.0);
	}

	Metal::Metal()
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

	void Metal::setLanes(const int _lanes)
	{
		m_lanes = _lanes;
		syntakt::fast::setLanes(_lanes, {&m_out});
	}

	void Metal::start(Voice& _v, const int _l)
	{
		const auto& t = tables();
		const auto word = [this, _l](const int _i) { return m_controls.words[_i][_l]; };
		// The machine change's init at a first trig: the last feedback and index from 0
		if(!_v.started)
		{
			_v.op2.feedback = 0;
			_v.index = 0;
		}
		_v.started = true;
		std::fill(std::begin(_v.phase), std::end(_v.phase), 0u);
		_v.op2.phase = 0;
		_v.op2.last = 0;
		const auto decay = static_cast<size_t>(hb(word(Decay)));
		std::fill(std::begin(_v.env), std::end(_v.env), 1.0);
		_v.coef[0] = _v.coef[2] = -t.evenDecay[decay];
		_v.coef[1] = _v.coef[3] = -t.oddDecay[decay];
		std::fill(std::begin(_v.x), std::end(_v.x), 0.0f);
		std::fill(std::begin(_v.y), std::end(_v.y), 0.0f);
		_v.amp.start(1.0, word(Gate) >> 8 >= 64, true);
		_v.ampLevel = 0;
		_v.noteOff = false;
		_v.punch = word(Punch) >> 8 >= 64;
		_v.drive = PunchGain{};
		_v.drive.on = _v.punch;
	}

	void Metal::render(Voice& _v, const int _l, const bool _trigBlock, const bool _cut, const bool _release)
	{
		const auto& t = tables();
		const auto& sn = sine();
		const auto word = [this, _l](const int _i) { return m_controls.words[_i][_l]; };

		// The block-rate control (0x400aa49c): the ratios into the multipliers (operator 0 at their product), the
		// increments, SHPE's level into the indices and operator 2's feedback word, the envelopes' targets
		const int64_t r1 = lookup(t.ratio1, word(Sweep)), r2 = lookup(t.ratio2, word(Color));
		const int64_t multiplier[4]{s32(fm(r1, r2) << 5), r1, r2, 0x4000000};
		int64_t inc[Units];
		increments(word(Pitch), m_controls.notes[_l], Offset, 0, 0, multiplier, Factor, inc);
		const int64_t level = lookup(t.shapeLevel, word(Shape));
		const int64_t feedback = fm(0x4000000, level >> 3);
		const double index = static_cast<double>(fm(0x2000000, level)) / 16777216.0;
		const double even = lookup(t.evenTarget, word(Contour));
		const double target[Units]{even, 0.75, even, 0.75};

		// The operators' envelopes, a block, ramped over it
		double envFrom[Units];
		std::copy(std::begin(_v.env), std::end(_v.env), envFrom);
		if(!_trigBlock)
			for(int k = 0; k < Units; ++k)
				_v.env[k] = _v.coef[k] * _v.env[k] + target[k] * (1 - _v.coef[k]);
		// Operator 2 in the OS's integers
		int32_t op2[BlockSize];
		_v.op2.block(inc[2], feedback, op2);
		// The amp envelope, at 1 from the trig's block (no ramp there); a retrig's block ramps it out
		double ampFrom = _v.ampLevel;
		_v.amp.advance(_trigBlock, -t.ampDecay[static_cast<size_t>(hb(word(Decay)))], _v.noteOff, _cut);
		if(_v.amp.consumed)
			_v.noteOff = false;
		if(_release)
			_v.noteOff = true;
		if(_trigBlock)
			ampFrom = _v.amp.v;
		_v.ampLevel = _v.amp.v;
		// The drive's gain
		const double gainFrom = _v.drive.y;
		const double gain = _v.drive.on ? _v.drive.advance(PunchFall, PunchRise, PunchFloor) : 1.0;
		const float pre = _v.punch ? static_cast<float>(4 * PunchFloor) : 1.0f, post = _v.punch ? One : 0.75f;

		float e[Units], de[Units];
		for(int k = 0; k < Units; ++k)
		{
			e[k] = static_cast<float>(envFrom[k]);
			de[k] = static_cast<float>(_v.env[k]) - e[k];
		}
		const auto indexFrom = static_cast<float>(_v.index), indexTo = static_cast<float>(index);
		const auto ampA = static_cast<float>(ampFrom), ampB = static_cast<float>(_v.ampLevel);
		const auto gainA = static_cast<float>(gainFrom), gainB = static_cast<float>(gain);
		float* out = &m_out.s[0][0] + _l;
		for(int n = 0; n < BlockSize; ++n)
		{
			const float r = static_cast<float>(n) / BlockSize;
			const auto at = [&](const int _k)
			{
				const uint32_t step = static_cast<uint32_t>(n) * static_cast<uint32_t>(inc[_k]);
				return static_cast<float>((_v.phase[_k] + step) & 0xffffff) / 16777216.0f;
			};
			const float i = indexFrom + (indexTo - indexFrom) * r;
			const float o0 = (e[0] + de[0] * r) * wave(sn, at(0));
			const float o1 = (e[1] + de[1] * r) * wave(t.wave, at(1) + i * o0);
			const float o2 = (e[2] + de[2] * r) * (static_cast<float>(op2[n]) / 2147483648.0f);
			const float o3 = (e[3] + de[3] * r) * wave(t.wave, at(3) + i * o2);
			const float x = 0.5f * o3 + 0.5f * o1;
			const float y = B0 * x + B1 * _v.x[0] + B0 * _v.x[1] - A1 * _v.y[0] - A2 * _v.y[1];
			_v.x[1] = _v.x[0];
			_v.x[0] = x;
			_v.y[1] = _v.y[0];
			_v.y[0] = y;
			const float driven = (gainA + (gainB - gainA) * r) * post * std::clamp(pre * y, -1.0f, 1.0f);
			out[n * Lanes] = driven * (ampA + (ampB - ampA) * r) / 2;
		}
		for(const int k : {0, 1, 3})
			_v.phase[k] = (_v.phase[k] + static_cast<uint32_t>(BlockSize) * static_cast<uint32_t>(inc[k])) & 0xffffff;
		_v.index = index;
	}

	const Block& Metal::renderBlock()
	{
		const FlushDenormals ftz;
		auto& trigs = m_controls.trigs;
		trigs.start();
		for(int l = 0; l < m_lanes; ++l)
		{
			Voice& v = m_voice[static_cast<size_t>(l)];
			const bool trigBlock = trigs.now[l];
			// A first trig's init clears its block's note-off
			const bool first = trigBlock && !v.started;
			if(trigBlock)
				start(v, l);
			if(!v.started)
			{
				for(int n = 0; n < BlockSize; ++n)
					m_out.s[n][l] = 0;
				continue;
			}
			render(v, l, trigBlock, trigs.pending[l], trigs.releases(l) && !first);
		}
		trigs.end();
		return m_out;
	}
}
