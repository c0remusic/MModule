#include "cyFastPerc.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace cycles::fast
{
	using syntakt::fast::FlushDenormals;

	namespace
	{
#include "cyFastPercCurves.inc"

		constexpr int Exp2Entries = 1024;

		// The tables, once a process: in double where the voice reads them as values, in the OS's integers where they
		// reach its integer chains (the ratios, the sweep); the exp2 curve's 1024 entries; and operator 0's wave (256
		// entries and the first again), 0.75 sin + 0.19 sin 5 + 0.06 sin 9, its peak at 1
		struct Tables
		{
			Table op1Index, op3Index, op0Target, op2Target, op0Decay, op2Decay, exp2Index;
			std::array<int32_t, 128> ratio0, ratio1, ratio2, sweepDepth, sweepDecay;
			std::array<double, Exp2Entries> exp2;
			std::array<float, 257> wave;

			Tables() : op1Index(makeTable(Op1Index)), op3Index(makeTable(Op3Index)), op0Target(makeTable(Op0Target)),
				op2Target(makeTable(Op2Target)), op0Decay(makeTable(Op0Decay)), op2Decay(makeTable(Op2Decay)),
				exp2Index(makeTable(Exp2Index)), ratio0(toQ31(makeTable(Ratio0))), ratio1(toQ31(makeTable(Ratio1))),
				ratio2(toQ31(makeTable(Ratio2))), sweepDepth(toQ31(makeTable(SweepDepth))),
				sweepDecay(toQ31(makeTable(SweepDecay)))
			{
				for(int i = 0; i < Exp2Entries; ++i)
					exp2[static_cast<size_t>(i)] = entry(Exp2Decay, i);
				constexpr double Pi = 3.14159265358979323846;
				for(int i = 0; i < 257; ++i)
				{
					const double x = 2 * Pi * i / 256;
					const double w = 0.75 * std::sin(x) + 0.19 * std::sin(5 * x) + 0.06 * std::sin(9 * x);
					wave[static_cast<size_t>(i)] = static_cast<float>(std::clamp(w, -1.0, 1.0));
				}
			}
		};

		const Tables& tables()
		{
			static const Tables t;
			return t;
		}

		// An envelope's coefficient a block from the exp2 curve, x in [0, 1): linear between its entries, the last
		// repeated
		double exp2Decay(const double _x)
		{
			const auto& t = tables().exp2;
			const double j = _x * Exp2Entries;
			const int i = std::min(static_cast<int>(std::floor(j)), Exp2Entries - 1);
			const double f = j - i;
			const auto next = static_cast<size_t>(std::min(i + 1, Exp2Entries - 1));
			return -(t[static_cast<size_t>(i)] * (1 - f) + t[next] * f);
		}

		// The machine's -64 semitones; every factor 1/2; operator 3 at the note
		constexpr int64_t Offset = -0x400000;
		constexpr int64_t Factor[4]{0x40000000, 0x40000000, 0x40000000, 0x40000000};
		// The amp's attack step a block, from 0 to 1 over the trig's block
		constexpr double AmpStep = -fraction(0xA2E8BA2F);
		// With GATE on, operators 1 and 3's target (0.7071)
		constexpr double Sustain = fraction(0x5A7EF9DB);
		// The drive: PUNCH off, nothing; on, the gain x 1.32 into the clip, falling from 1 to 0.25 (36 ms) then rising
		// back (555 ms)
		constexpr double PunchFall[3]{fraction(0x12C0F26), fraction(0x12C0F26), fraction(0x82581E4B)};
		constexpr double PunchRise[3]{fraction(0x13AF8C), fraction(0x13AF8C), fraction(0x80275F18)};
		constexpr double PunchDrive = 4 * fraction(0x2A3D70A4);
	}

	Perc::Perc()
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

	void Perc::setLanes(const int _lanes)
	{
		m_lanes = _lanes;
		syntakt::fast::setLanes(_lanes, {&m_out});
	}

	void Perc::start(Voice& _v, const int _l)
	{
		const auto& t = tables();
		const auto word = [this, _l](const int _i) { return m_controls.words[_i][_l]; };
		// The machine change's init at a first trig: the indices' last values from 0
		if(!_v.started)
			_v.index1 = _v.index3 = 0;
		_v.started = true;
		std::fill(std::begin(_v.phase), std::end(_v.phase), 0u);
		std::fill(std::begin(_v.env), std::end(_v.env), 1.0);
		const auto decay = static_cast<size_t>(hb(word(Decay)));
		_v.coef0 = -t.op0Decay[decay];
		_v.coef2 = -t.op2Decay[decay];
		_v.sweep = Max31;
		_v.sweepCoef = t.sweepDecay[static_cast<size_t>(hb(word(Sweep)))];
		_v.gate = word(Gate) >> 8 >= 64;
		_v.punch = word(Punch) >> 8 >= 64;
		_v.amp.start(AmpStep, _v.gate, true);
		_v.ampLevel = 0;
		_v.ampDecay = _v.gate ? 0 : 1;
		_v.lastOp3 = 0;
		_v.noteOff = false;
		_v.drive = PunchGain{};
		_v.drive.on = _v.punch;
	}

	void Perc::render(Voice& _v, const int _l, const bool _trigBlock, const bool _cut, const bool _release)
	{
		const auto& t = tables();
		const auto& sn = sine();
		const auto word = [this, _l](const int _i) { return m_controls.words[_i][_l]; };
		const int32_t color = word(Color), shape = word(Shape), contour = word(Contour);

		// The block-rate control (0x400aa998): operators 1 and 3's coefficients on the exp2 curve (with GATE, the
		// amp's is operator 3's of the block before), the sweep (held at 1 in the trig's block), the increments, the
		// indices, the targets
		if(_v.gate)
			_v.ampDecay = _v.lastOp3;
		const double x3 = t.exp2Index[static_cast<size_t>(hb(word(Decay)))];
		const double coef3 = exp2Decay(x3);
		const double coef1 = exp2Decay(2 * x3 * (0.75 - 0.55 * shape / 32768));
		_v.lastOp3 = coef3;
		if(!_trigBlock)
			_v.sweep = fm(_v.sweep, -_v.sweepCoef);
		const int64_t multiplier[4]{lookup(t.ratio0, color), lookup(t.ratio1, color), lookup(t.ratio2, color),
			0x4000000};
		int64_t inc[Units];
		increments(word(Pitch), m_controls.notes[_l], Offset, t.sweepDepth[static_cast<size_t>(hb(word(Sweep)))],
			_v.sweep, multiplier, Factor, inc);
		const double index1 = 2 * lookup(t.op1Index, shape), index3 = 2 * lookup(t.op3Index, shape);
		const double sustain = _v.gate ? Sustain : 0;
		const double target[Units]{lookup(t.op0Target, contour), sustain, lookup(t.op2Target, contour), sustain};
		const double coef[Units]{_v.coef0, coef1, _v.coef2, coef3};

		// The operators' envelopes, a block, ramped over it
		double envFrom[Units];
		std::copy(std::begin(_v.env), std::end(_v.env), envFrom);
		if(!_trigBlock)
			for(int k = 0; k < Units; ++k)
				_v.env[k] = coef[k] * _v.env[k] + target[k] * (1 - coef[k]);
		// The drive's gain, the amp; a retrig's block ramps the amp out
		const double gainFrom = _v.drive.y;
		const double gain = _v.drive.on ? _v.drive.advance(PunchFall, PunchRise, 0.25) : 1.0;
		const double ampFrom = _v.ampLevel;
		_v.amp.advance(_trigBlock, _v.ampDecay, _v.noteOff, _cut);
		if(_v.amp.consumed)
			_v.noteOff = false;
		if(_release)
			_v.noteOff = true;
		_v.ampLevel = _v.amp.v;
		const float pre = _v.punch ? static_cast<float>(PunchDrive) : 1.0f;
		const float g0 = _v.punch ? 0.5f : 0.25f, g1 = _v.punch ? 0.25f : 0.125f;

		float e[Units], de[Units];
		for(int k = 0; k < Units; ++k)
		{
			e[k] = static_cast<float>(envFrom[k]);
			de[k] = static_cast<float>(_v.env[k]) - e[k];
		}
		const auto i1From = static_cast<float>(_v.index1), i1To = static_cast<float>(index1);
		const auto i3From = static_cast<float>(_v.index3), i3To = static_cast<float>(index3);
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
			const float o0 = (e[0] + de[0] * r) * wave(t.wave, at(0));
			const float o1 = (e[1] + de[1] * r) * wave(sn, at(1) + (i1From + (i1To - i1From) * r) * o0);
			const float o2 = (e[2] + de[2] * r) * wave(sn, at(2));
			const float o3 = (e[3] + de[3] * r) * wave(sn, at(3) + (i3From + (i3To - i3From) * r) * o2);
			const float x = g0 * o3 + g1 * o1;
			const float driven = (gainA + (gainB - gainA) * r) * std::clamp(pre * x, -1.0f, 1.0f);
			out[n * Lanes] = driven * (ampA + (ampB - ampA) * r) / 2;
		}
		for(int k = 0; k < Units; ++k)
			_v.phase[k] = (_v.phase[k] + static_cast<uint32_t>(BlockSize) * static_cast<uint32_t>(inc[k])) & 0xffffff;
		_v.index1 = index1;
		_v.index3 = index3;
	}

	const Block& Perc::renderBlock()
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
