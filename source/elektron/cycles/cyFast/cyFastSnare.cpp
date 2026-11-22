#include "cyFastSnare.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace cycles::fast
{
	using syntakt::fast::FlushDenormals;

	namespace
	{
#include "cyFastSnareCurves.inc"

		// The tables, once a process: in double where the voice reads them as values, in the OS's integers where they
		// reach its integer chains (the pitch, the noise's feedback, SHPE's index)
		struct Tables
		{
			Table op0Decay, op1Decay, op2Decay, op3Decay, ampDecay, attackStep, bodyDecay, op0Target, op1Target;
			std::array<int32_t, 128> sweepDecay, sweepDepth, feedback, op0Factor, op1Factor, op3Factor, op3Index;

			Tables() : op0Decay(makeTable(Op0Decay)), op1Decay(makeTable(Op1Decay)), op2Decay(makeTable(Op2Decay)),
				op3Decay(makeTable(Op3Decay)), ampDecay(makeTable(AmpDecay)), attackStep(makeTable(AttackStep)),
				bodyDecay(makeTable(BodyDecay)), op0Target(makeTable(Op0Target)), op1Target(makeTable(Op1Target)),
				sweepDecay(toQ31(makeTable(SweepDecay))), sweepDepth(toQ31(makeTable(SweepDepth))),
				feedback(toQ31(makeTable(Feedback))), op0Factor(toQ31(makeTable(Op0Factor))),
				op1Factor(toQ31(makeTable(Op1Factor))), op3Factor(toQ31(makeTable(Op3Factor))),
				op3Index(toQ31(makeTable(Op3Index)))
			{
			}
		};

		const Tables& tables()
		{
			static const Tables t;
			return t;
		}

		// The operators' multipliers: 0 and 1 at 1/16, 2 (the body) at 1/32, 3 at 3/32; the machine's -64 semitones
		constexpr int64_t Multiplier[4]{0x8000000, 0x8000000, 0x4000000, 0xC000000};
		constexpr int64_t Offset = -0x400000;
		// Operator 2's index on operator 1 (fm(0x1000000, 0x1D70A3D7), 0.23 cycles) and operator 3's (0.25)
		const float Op1IntoOp2 = static_cast<float>(fm(0x1000000, 0x1D70A3D7) / 16777216.0);
		const float Op1IntoOp3 = static_cast<float>(fm(0x1000000, 0x20000000) / 16777216.0);
		// The body's high-pass, y = b0 x + b1 x1 - a1 y1 (250 Hz), and its attack step a block
		const float HighPass[3]{static_cast<float>(fraction(0x4B9047AB)), static_cast<float>(fraction(0xB46FB855)),
			static_cast<float>(fraction(0x841F111A))};
		constexpr double BodyStep = -fraction(0xC028DB9C);
		// The drive: PUNCH off, a clip and nothing else; on, the gain 0.5 x 4 into the clip, falling from 1 to 0.5
		// (21 ms) then rising back (347 ms)
		constexpr double PunchFall[3]{fraction(0x1F10E1D), fraction(0x1F10E1D), fraction(0x83E21C3A)};
		constexpr double PunchRise[3]{fraction(0x1F7C5C), fraction(0x1F7C5C), fraction(0x803EF8B8)};
		constexpr float One = static_cast<float>(Max31 / 2147483648.0);
	}

	Snare::Snare()
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

	void Snare::setLanes(const int _lanes)
	{
		m_lanes = _lanes;
		syntakt::fast::setLanes(_lanes, {&m_out});
	}

	void Snare::start(Voice& _v, const int _l)
	{
		const auto& t = tables();
		const auto word = [this, _l](const int _i) { return m_controls.words[_i][_l]; };
		// The machine change's init at a first trig: the noise's last feedback and SHPE's last index from 0
		if(!_v.started)
		{
			_v.noise.feedback = 0;
			_v.index = 0;
		}
		_v.started = true;
		std::fill(std::begin(_v.phase), std::end(_v.phase), 0u);
		_v.noise.phase = 0;
		_v.noise.last = 0;
		const auto decay = static_cast<size_t>(hb(word(Decay)));
		std::fill(std::begin(_v.env), std::end(_v.env), 1.0);
		_v.coef[0] = -t.op0Decay[decay];
		_v.coef[1] = -t.op1Decay[decay];
		_v.coef[2] = -t.op2Decay[decay];
		_v.coef[3] = -t.op3Decay[static_cast<size_t>(hb(word(Contour)))];
		_v.depth = t.sweepDepth[static_cast<size_t>(hb(word(Sweep)))];
		_v.sweepCoef = t.sweepDecay[static_cast<size_t>(hb(word(Sweep)))];
		_v.sweep = Max31;
		std::fill(std::begin(_v.hp), std::end(_v.hp), 0.0f);
		std::fill(std::begin(_v.lp), std::end(_v.lp), 0.0f);
		_v.punch = word(Punch) >> 8 >= 64;
		_v.body.start(_v.punch ? 1.0 : BodyStep, false, false);
		_v.amp.start(-t.attackStep[decay], word(Gate) >> 8 >= 64, true);
		_v.bodyLevel = _v.ampLevel = 0;
		_v.noteOff = false;
		_v.drive = PunchGain{};
		_v.drive.on = _v.punch;
	}

	void Snare::render(Voice& _v, const int _l, const bool _trigBlock, const bool _cut, const bool _release)
	{
		const auto& t = tables();
		const auto& sn = sine();
		const auto word = [this, _l](const int _i) { return m_controls.words[_i][_l]; };
		const int32_t color = word(Color), shape = word(Shape), contour = word(Contour);
		const auto decay = static_cast<size_t>(hb(word(Decay)));

		// The block-rate control (0x400ab3b0): the sweep (held at 1 in the trig's block), the increments, the
		// noise's feedback word, the envelopes' targets, SHPE's index, the noise low-pass's pole
		if(!_trigBlock)
			_v.sweep = fm(_v.sweep, -_v.sweepCoef);
		const int64_t factor[4]{lookup(t.op0Factor, color), lookup(t.op1Factor, color), 0x40000000,
			lookup(t.op3Factor, color)};
		int64_t inc[Units];
		increments(word(Pitch), m_controls.notes[_l], Offset, _v.depth, _v.sweep, Multiplier, factor, inc);
		const int64_t feedback = fm(0x4000000, lookup(t.feedback, shape));
		const double target[Units]{lookup(t.op0Target, contour), lookup(t.op1Target, contour), 0.5, 0.25};
		const double index = static_cast<double>(fm(0x10000000, lookup(t.op3Index, shape))) / 16777216.0;
		// The pole's entry, 126 for any SHPE above 0, 127 for 0 (the OS's)
		const int64_t entry = s32(fm(0x7F000000, shape) + (0x7F00 - shape)) >> 8;
		const auto lpA = static_cast<float>(NoisePole[std::clamp<int64_t>(entry - 126, 0, 1)]);
		const auto lpB = static_cast<float>(0.4 * (0.5 + static_cast<double>(lpA) / 2));

		// The operators' envelopes, a block, ramped over it
		double envFrom[Units];
		std::copy(std::begin(_v.env), std::end(_v.env), envFrom);
		if(!_trigBlock)
			for(int k = 0; k < Units; ++k)
				_v.env[k] = _v.coef[k] * _v.env[k] + target[k] * (1 - _v.coef[k]);
		// The noise: operator 0 in the OS's integers
		int32_t noise[BlockSize];
		_v.noise.block(inc[0], feedback, noise);
		// The body envelope (no ramp in the trig's block with PUNCH), the amp envelope; a retrig's block ramps both
		// out
		double bodyFrom = _v.bodyLevel;
		_v.body.advance(_trigBlock, -t.bodyDecay[decay], false, _cut);
		if(_trigBlock && _v.punch)
			bodyFrom = _v.body.v;
		_v.bodyLevel = _v.body.v;
		const double ampFrom = _v.ampLevel;
		_v.amp.advance(_trigBlock, -t.ampDecay[decay], _v.noteOff, _cut);
		if(_v.amp.consumed)
			_v.noteOff = false;
		if(_release)
			_v.noteOff = true;
		_v.ampLevel = _v.amp.v;
		// The drive's gain
		const double gainFrom = _v.drive.y;
		const double gain = _v.drive.on ? _v.drive.advance(PunchFall, PunchRise, 0.5) : 1.0;
		const float pre = _v.punch ? 4 * One : 1.0f, post = _v.punch ? 0.5f : One;

		float e[Units], de[Units];
		for(int k = 0; k < Units; ++k)
		{
			e[k] = static_cast<float>(envFrom[k]);
			de[k] = static_cast<float>(_v.env[k]) - e[k];
		}
		const auto indexFrom = static_cast<float>(_v.index), indexTo = static_cast<float>(index);
		const auto bodyA = static_cast<float>(bodyFrom), bodyB = static_cast<float>(_v.bodyLevel);
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
			const float o0 = (e[0] + de[0] * r) * (static_cast<float>(noise[n]) / 2147483648.0f);
			const float o1 = (e[1] + de[1] * r) * wave(sn, at(1));
			const float o2 = (e[2] + de[2] * r) * wave(sn, at(2) + Op1IntoOp2 * o1);
			const float o3 = (e[3] + de[3] * r) * wave(sn, at(3) + Op1IntoOp3 * o1
				+ (indexFrom + (indexTo - indexFrom) * r) * o0);
			// The body: operator 2 high-passed, under its envelope
			float y = HighPass[0] * o2 + HighPass[1] * _v.hp[0] - HighPass[2] * _v.hp[1];
			_v.hp[0] = o2;
			_v.hp[1] = y;
			const float body = y * (bodyA + (bodyB - bodyA) * r);
			// The noise: operator 3 low-passed, under the amp envelope
			y = lpB * o3 + lpB * _v.lp[0] - lpA * _v.lp[1];
			_v.lp[0] = o3;
			_v.lp[1] = y;
			const float noiseOut = y * (ampA + (ampB - ampA) * r);
			const float x = One * body + 0.5f * noiseOut;
			out[n * Lanes] = (gainA + (gainB - gainA) * r) * post * std::clamp(pre * x, -1.0f, 1.0f) / 2;
		}
		for(int k = 1; k < Units; ++k)
			_v.phase[k] = (_v.phase[k] + static_cast<uint32_t>(BlockSize) * static_cast<uint32_t>(inc[k])) & 0xffffff;
		_v.index = index;
	}

	const Block& Snare::renderBlock()
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
