#include "cyFastTone.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace cycles::fast
{
	using syntakt::fast::FlushDenormals;

	namespace
	{
#include "cyFastToneCurves.inc"

		// COLR's ratios / 32, a list of musical ratios: eight below 1, then for n = 1 to 7 sixteen from n to n + 1,
		// then 8 to 15
		Table ratios()
		{
			Table r{};
			size_t i = 0;
			for(const double v : {0.0125, 0.075, 0.125, 0.25, 0.5, 0.75, 0.9, 0.99})
				r[i++] = v;
			for(int n = 1; n <= 7; ++n)
			{
				r[i++] = n;
				r[i++] = n * 1.001;
				r[i++] = n * 1.01;
				for(const double d : {.1, .15, .2, .25, .3, .4, .5, .6, .75, .8, .9})
					r[i++] = n + d;
				r[i++] = (n + 1) * 0.99;
				r[i++] = (n + 1) * 0.999;
			}
			for(int n = 8; n <= 15; ++n)
				r[i++] = n;
			for(double& v : r)
				v /= 32;
			return r;
		}

		// The tables, once a process: in double where the voice reads them as values, in the OS's integers where they
		// reach its integer pitch chain (the ratio)
		struct Tables
		{
			Table feedback, op1Index, op0Target, op0Decay, ampDecay;
			std::array<int32_t, 128> ratio;

			Tables() : feedback(makeTable(Feedback)), op1Index(makeTable(Op1Index)), op0Target(makeTable(Op0Target)),
				op0Decay(makeTable(Op0Decay)), ampDecay(makeTable(AmpDecay)), ratio(toQ31(ratios()))
			{
			}
		};

		const Tables& tables()
		{
			static const Tables t;
			return t;
		}

		// The machine's -64 semitones; every factor 1/2; operator 1 at the note; increments a 96 kHz sample
		constexpr int64_t Offset = -0x400000;
		constexpr int64_t Factor[4]{0x40000000, 0x40000000, 0x40000000, 0x40000000};
		constexpr int64_t Rate = 0x2BB0CF88;
		// The decimator's recursion (0x400a9f58): a polyphase half-band of gain 2
		const float D0 = static_cast<float>(fraction(0x3D91D187)), D1 = static_cast<float>(fraction(0xF3656E2D));
		const float D2 = static_cast<float>(fraction(0x043F7701)), D3 = static_cast<float>(fraction(0xFE7B06ED));
		// The drive, after the amp: PUNCH off, a quarter; on, x 3 into the clip, its gain falling from 1 to 0.25
		// (36 ms) then rising back (347 ms)
		constexpr double PunchFall[3]{fraction(0x12C0F26), fraction(0x12C0F26), fraction(0x82581E4B)};
		constexpr double PunchRise[3]{fraction(0x1F7C5C), fraction(0x1F7C5C), fraction(0x803EF8B8)};
	}

	Tone::Tone()
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

	void Tone::setLanes(const int _lanes)
	{
		m_lanes = _lanes;
		syntakt::fast::setLanes(_lanes, {&m_out});
	}

	void Tone::start(Voice& _v, const int _l)
	{
		const auto& t = tables();
		const auto word = [this, _l](const int _i) { return m_controls.words[_i][_l]; };
		// The machine change's init at a first trig: the feedback's and the index's last values from 0
		if(!_v.started)
			_v.feedback = _v.index = 0;
		_v.started = true;
		std::fill(std::begin(_v.phase), std::end(_v.phase), 0u);
		_v.env = 1;
		_v.coef = -t.op0Decay[static_cast<size_t>(hb(word(Contour)))];
		_v.last = 0;
		std::fill(std::begin(_v.history), std::end(_v.history), 0.0f);
		std::fill(std::begin(_v.recursion), std::end(_v.recursion), 0.0f);
		_v.amp.start(1.0, word(Gate) >> 8 >= 64, true);
		_v.ampLevel = 0;
		_v.noteOff = false;
		_v.punch = word(Punch) >> 8 >= 64;
		_v.drive = PunchGain{};
		_v.drive.on = _v.punch;
	}

	void Tone::render(Voice& _v, const int _l, const bool _trigBlock, const bool _cut, const bool _release)
	{
		const auto& t = tables();
		const auto& sn = sine();
		const auto word = [this, _l](const int _i) { return m_controls.words[_i][_l]; };

		// The block-rate control (0x400aa7b8): the feedback, the index, the increments at 96 kHz, operator 0's
		// envelope
		const double feedback = m_withoutChaos ? 0.0 : lookup(t.feedback, word(Sweep));
		const double index = lookup(t.op1Index, word(Shape));
		const int64_t multiplier[4]{lookup(t.ratio, word(Color)), 0x4000000, 0x4000000, 0x4000000};
		int64_t inc[4];
		increments(word(Pitch), m_controls.notes[_l], Offset, 0, 0, multiplier, Factor, inc, Rate);
		const double envFrom = _v.env;
		if(!_trigBlock)
			_v.env = _v.coef * _v.env + lookup(t.op0Target, word(Contour)) * (1 - _v.coef);

		// The operators at 96 kHz: operator 0 modulated by its last enveloped output at 8 F, operator 1 by operator 0's
		// at 4 L, each ramped over the 64 samples from the last block's
		const auto e = static_cast<float>(envFrom), de = static_cast<float>(_v.env) - e;
		const auto fFrom = static_cast<float>(8 * _v.feedback), df = static_cast<float>(8 * feedback) - fFrom;
		const auto iFrom = static_cast<float>(4 * _v.index), di = static_cast<float>(4 * index) - iFrom;
		float x[8 + Oversampled];
		std::copy(std::begin(_v.history), std::end(_v.history), x);
		float y = _v.last;
		for(int n = 0; n < Oversampled; ++n)
		{
			const float r = static_cast<float>(n) / Oversampled;
			const auto at = [&](const int _k)
			{
				const uint32_t step = static_cast<uint32_t>(n) * static_cast<uint32_t>(inc[_k]);
				return static_cast<float>((_v.phase[_k] + step) & 0xffffff) / 16777216.0f;
			};
			y = (e + de * r) * wave(sn, at(0) + (fFrom + df * r) * y) / 2;
			x[8 + n] = wave(sn, at(1) + (iFrom + di * r) * y) / 2;
		}
		_v.last = y;
		for(int k = 0; k < Units; ++k)
			_v.phase[k] = (_v.phase[k] + static_cast<uint32_t>(Oversampled) * static_cast<uint32_t>(inc[k])) & 0xffffff;
		_v.feedback = feedback;
		_v.index = index;
		std::copy(x + Oversampled, x + Oversampled + 8, _v.history);

		// The amp (full from the trig's block, no ramp there); a retrig's block ramps it out
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
		const double gain = _v.drive.on ? _v.drive.advance(PunchFall, PunchRise, 0.25) : 1.0;
		const float pre = _v.punch ? 3.0f : 1.0f;

		// The decimator, 64 samples to 32: out[m] = x[2m + 2] + y[m], y a recursion on the odd samples, clipped
		const auto ampA = static_cast<float>(ampFrom), ampB = static_cast<float>(_v.ampLevel);
		const auto gainA = static_cast<float>(gainFrom), gainB = static_cast<float>(gain);
		float* rec = _v.recursion;
		float* out = &m_out.s[0][0] + _l;
		for(int m = 0; m < BlockSize; ++m)
		{
			const float r = static_cast<float>(m) / BlockSize;
			const float* in = x + 2 * m + 1;
			const float v = in[0] + D0 * in[2] + D1 * in[4] + D2 * in[6] + D3 * in[8] - D3 * rec[0] - D2 * rec[1]
				- D1 * rec[2] - D0 * rec[3];
			rec[0] = rec[1];
			rec[1] = rec[2];
			rec[2] = rec[3];
			rec[3] = v;
			const float s = std::clamp(in[1] + v, -1.0f, 1.0f) * (ampA + (ampB - ampA) * r);
			out[m * Lanes] = (gainA + (gainB - gainA) * r) * 0.25f * std::clamp(pre * s, -1.0f, 1.0f) / 2;
		}
	}

	const Block& Tone::renderBlock()
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
