#include "cyFastChord.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace cycles::fast
{
	using syntakt::fast::FlushDenormals;

	namespace
	{
#include "cyFastChordCurves.inc"

		// SHPE 3-37's chords: voices 1-3's semitones over voice 0
		constexpr int Chords = 35;
		constexpr int8_t Semitones[Chords][3]{{3, 7, 12}, {4, 7, 12}, {2, 7, 12}, {5, 7, 12}, {3, 7, 10}, {4, 7, 10},
			{3, 7, 11}, {4, 7, 11}, {5, 7, 10}, {3, 6, 9}, {3, 7, 14}, {4, 7, 14}, {3, 7, 9}, {4, 7, 9}, {3, 6, 12},
			{4, 6, 12}, {3, 6, 10}, {4, 6, 10}, {4, 8, 12}, {3, 8, 10}, {4, 8, 10}, {3, 7, 8}, {3, 10, 14},
			{4, 10, 14}, {4, 6, 14}, {4, 6, 11}, {4, 10, 13}, {1, 5, 8}, {5, 7, 8}, {4, 6, 7}, {4, 5, 9}, {4, 9, 11},
			{4, 11, 14}, {5, 12, 17}, {7, 12, 19}};

		int32_t toQ31(const double _v)
		{
			return static_cast<int32_t>(std::clamp(std::nearbyint(_v * 2147483648.0), -2147483648.0, 2147483647.0));
		}

		// COLR's detune for SHPE 0-2: 2^(s (i / 127)^2 / 12) / 32 at s semitones
		std::array<int32_t, 128> detune(const double _semitones)
		{
			std::array<int32_t, 128> t{};
			for(int i = 0; i < 128; ++i)
			{
				const double x = i / 127.0;
				t[static_cast<size_t>(i)] = toQ31(std::pow(2.0, _semitones * (x * x) / 12) / 32);
			}
			return t;
		}

		// The tables, once a process: in double where the voice reads them as values (the cutoff's a1 from its ln(1 +
		// a1)), in the OS's integers where they reach its integer pitch chain (the detunes, the chords' ratios / 32)
		struct Tables
		{
			Table attackStep, sweepDepth, cutoff, sweepDecay, ampDecay;
			std::array<int32_t, 128> up2, up23, down2, down23;
			int32_t chord[Chords][3];

			Tables() : attackStep(makeTable(AttackStep)), sweepDepth(makeTable(SweepDepth)), cutoff(makeTable(Cutoff)),
				sweepDecay(makeTable(SweepDecay)), ampDecay(makeTable(AmpDecay)), up2(detune(2.0)),
				up23(detune(0.6667)), down2(detune(-2.0)), down23(detune(-0.6667)), chord{}
			{
				for(double& v : cutoff)
					v = std::expm1(v);
				for(int r = 0; r < Chords; ++r)
					for(int k = 0; k < 3; ++k)
						chord[r][k] = toQ31(std::pow(2.0, Semitones[r][k] / 12.0) / 32);
			}
		};

		const Tables& tables()
		{
			static const Tables t;
			return t;
		}

		// The machine's -64 semitones; an operator at ratio 1 (1/32) plays the note
		constexpr int64_t Offset = -0x400000;
		constexpr int64_t Unison = 0x4000000;
		// An operator whose pitch word exceeds this (2217 Hz) is muted
		constexpr int64_t Highest = 0x454800;
		constexpr int64_t Rate = 0x57619F10;
		// The crossfade's weights sum to this, not 1
		constexpr double Weights = fraction(0x7F000000);
		// The attack step a block beyond DEC 39
		constexpr double LateAttack = -fraction(0xEC11A411);
		// The cutoff: its top (14.7 kHz at the trig) and the word's scale
		constexpr double CutoffTop = fraction(0x55F00000);
		constexpr double CutoffScale = fraction(0x5F533928);
		// The drive: PUNCH off, x 0.5 after the clip; on, x 1, its gain falling from 1 to 0.25 (72 ms) then rising
		// back (832 ms)
		constexpr double PunchFall[3]{fraction(0x96B8C4), fraction(0x96B8C4), fraction(0x812D7188)};
		constexpr double PunchRise[3]{fraction(0xD205F), fraction(0xD205F), fraction(0x801A40BF)};

		// The chord's voices fading in one after another (COLR 0-31), then each dipping to 0 where its octave
		// switches
		double fadeIn(const int _d) { return _d <= 0 ? 0.0 : _d >= 10 ? 1.0 : _d / 10.0; }
		double dip(const int _d)
		{
			if(_d < 0 || _d > 9)
				return 1.0;
			return _d <= 5 ? 1 - _d / 5.0 : (_d - 5) / 5.0;
		}
	}

	Chord::Chord()
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

	void Chord::setLanes(const int _lanes)
	{
		m_lanes = _lanes;
		syntakt::fast::setLanes(_lanes, {&m_out});
	}

	void Chord::layout(const int32_t _shape, const int32_t _color, int64_t* _ratio, double* _gain)
	{
		const auto& t = tables();
		const int s = hb(_shape);
		switch(s)
		{
		case 0:
			_ratio[0] = lookup(t.down2, _color);
			_ratio[1] = lookup(t.up2, _color);
			_ratio[2] = _ratio[3] = Unison;
			_gain[0] = 0.25;
			_gain[1] = _gain[2] = 0;
			return;
		case 1:
			_ratio[0] = _ratio[3] = Unison;
			_ratio[1] = lookup(t.down2, _color);
			_ratio[2] = lookup(t.up2, _color);
			_gain[0] = _gain[1] = 0.25;
			_gain[2] = 0;
			return;
		case 2:
			_ratio[0] = lookup(t.down23, _color);
			_ratio[1] = lookup(t.down2, _color);
			_ratio[2] = lookup(t.up2, _color);
			_ratio[3] = lookup(t.up23, _color);
			_gain[0] = _gain[1] = _gain[2] = 0.25;
			return;
		default:
			break;
		}
		// A chord: COLR's high byte fades the voices in, then switches each an octave down (37, 47, 57 and 31 on) or
		// up (above 100, 110, 120) through silence
		const int c = _color >> 8;
		for(int k = 0; k < 3; ++k)
			_gain[k] = (c > 31 ? dip((c & 31) - 10 * k) : fadeIn(c - 10 * k)) / 4;
		if(s < 7 || s > 35 || s == 17 || s == 18 || s == 21)
			_gain[2] = 0;
		const int32_t* row = t.chord[std::min(s, 37) - 3];
		_ratio[0] = Unison;
		for(int k = 0; k < 3; ++k)
		{
			const int low = 37 + 10 * k, high = 100 + 10 * k;
			const int64_t q = row[k];
			_ratio[k + 1] = c >= low && c <= low + 31 ? q >> 1 : c > high ? 2 * q : q;
		}
	}

	const std::array<float, ChordWaves::Entries>& Chord::wave(const int _index) const
	{
		return _index == 0 ? sine() : m_waves->wave[static_cast<size_t>(_index - 1)];
	}

	uint32_t Chord::waveId(const int _index) const
	{
		return _index == 0 ? 0 : m_waves->address[static_cast<size_t>(_index - 1)];
	}

	void Chord::start(Voice& _v, const int _l)
	{
		const auto& t = tables();
		const auto word = [this, _l](const int _i) { return m_controls.words[_i][_l]; };
		// The machine change's init at a first trig: the increments', the gains' and the crossfade's last values from
		// 0, the last waves the sine
		if(!_v.started)
		{
			std::fill(std::begin(_v.inc), std::end(_v.inc), 0);
			std::fill(std::begin(_v.gain), std::end(_v.gain), 0.0);
			std::fill(std::begin(_v.weight), std::end(_v.weight), 0.0);
			_v.index = _v.waveA = _v.waveB = 0;
		}
		_v.started = true;
		std::fill(std::begin(_v.phase), std::end(_v.phase), 0u);
		const int decay = hb(word(Decay));
		_v.step = decay <= 39 ? -t.attackStep[static_cast<size_t>(decay)] : LateAttack;
		_v.env = _v.step;
		_v.sweep = 1;
		_v.sweepCoef = -t.sweepDecay[static_cast<size_t>(decay)];
		for(auto& f : _v.filter)
			f[0] = f[1] = 0;
		_v.amp.start(1.0, word(Gate) >> 8 >= 64, true);
		_v.ampLevel = 0;
		_v.noteOff = false;
		_v.punch = word(Punch) >> 8 >= 64;
		_v.drive = PunchGain{};
		_v.drive.on = _v.punch;
	}

	void Chord::render(Voice& _v, const int _l, const bool _trigBlock, const bool _cut, const bool _release)
	{
		const auto& t = tables();
		const auto word = [this, _l](const int _i) { return m_controls.words[_i][_l]; };

		// The block-rate control (0x400aae88): the chord's ratios and gains, the pitch word, the increments (an
		// operator over 2217 Hz keeps its last, voices 1-3 then muted), the cutoff's depth
		int64_t ratio[Units];
		double gain[Units - 1];
		layout(word(Shape), word(Color), ratio, gain);
		const int64_t pitch = pitchWord(word(Pitch), std::clamp(m_controls.notes[_l], 0, 96), Offset);
		for(int k = 0; k < Units; ++k)
		{
			const int64_t d = fm(ratio[k], pitch);
			if(d <= Highest)
				_v.inc[k] = fm(d, Rate) >> 2;
			else if(k)
				gain[k - 1] = 0;
		}
		const double depth = (0x55F00000 - pitch) / 2147483648.0 * lookup(t.sweepDepth, word(Contour));

		// The waves (0x400a8080) and their crossfade (0x400a8722), from the last block's pair: the normal case, the
		// index a step up or down (the shared wave carries its weight over), or a jump (both fade out, the new pair
		// fades in at the next block)
		const int32_t sweepWord = word(Sweep);
		const int index = sweepWord >> 10;
		const double frac = (sweepWord & 0x3ff) / 1024.0, comp = Weights - frac;
		int t1, t2;
		double u, uTo, q, qTo;
		if(index == _v.index + 1)
		{
			t1 = index;
			t2 = _v.waveA;
			u = _v.weight[1], uTo = comp, q = _v.weight[0], qTo = 0;
			_v.weight[0] = comp, _v.weight[1] = 0;
		}
		else if(index == _v.index - 1)
		{
			t1 = _v.waveB;
			t2 = index + 1;
			u = _v.weight[1], uTo = 0, q = _v.weight[0], qTo = frac;
			_v.weight[0] = 0, _v.weight[1] = frac;
		}
		else if(waveId(index) != waveId(_v.waveA))
		{
			t1 = _v.waveA;
			t2 = _v.waveB;
			u = _v.weight[0], uTo = 0, q = _v.weight[1], qTo = 0;
			_v.weight[0] = _v.weight[1] = 0;
		}
		else
		{
			t1 = index;
			t2 = index + 1;
			u = _v.weight[0], uTo = comp, q = _v.weight[1], qTo = frac;
			_v.weight[0] = comp, _v.weight[1] = frac;
		}
		_v.index = index;
		_v.waveA = index;
		_v.waveB = index + 1;
		const auto& w1 = wave(t1);
		const auto& w2 = wave(t2);

		// The operators' attack (0x400a9120), the drive's gain, the filter sweep (0x400a91ea) and the cutoff, constant
		// over the block: its 16-bit word on the low-pass's curve
		const double envFrom = _v.env;
		if(!_trigBlock)
			_v.env = std::min(_v.env + _v.step, 1.0);
		const double gainFrom = _v.drive.y;
		const double drive = _v.drive.on ? _v.drive.advance(PunchFall, PunchRise, 0.25) : 1.0;
		if(!_trigBlock)
			_v.sweep *= _v.sweepCoef;
		const double cutoff = std::clamp(CutoffTop - depth * (1 - _v.sweep), 0.0, CutoffTop);
		const double a1 = lookup(t.cutoff, static_cast<int32_t>(std::floor(2 * CutoffScale * cutoff * 32768)));
		const auto c = static_cast<float>(a1), b = static_cast<float>(a1 / 2 + 0.5);

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

		const auto e = static_cast<float>(envFrom), de = static_cast<float>(_v.env) - e;
		const auto uA = static_cast<float>(u), du = static_cast<float>(uTo) - uA;
		const auto qA = static_cast<float>(q), dq = static_cast<float>(qTo) - qA;
		float gA[Units - 1], dg[Units - 1];
		for(int k = 0; k < Units - 1; ++k)
		{
			gA[k] = static_cast<float>(_v.gain[k]);
			dg[k] = static_cast<float>(gain[k]) - gA[k];
			_v.gain[k] = gain[k];
		}
		const auto gainA = static_cast<float>(gainFrom), gainB = static_cast<float>(drive);
		const float post = _v.punch ? 1.0f : 0.5f;
		const auto ampA = static_cast<float>(ampFrom), ampB = static_cast<float>(_v.ampLevel);
		float x1 = _v.filter[0][0], y1 = _v.filter[0][1], x2 = _v.filter[1][0], y2 = _v.filter[1][1];
		float* out = &m_out.s[0][0] + _l;
		for(int n = 0; n < BlockSize; ++n)
		{
			const float r = static_cast<float>(n) / BlockSize;
			const float env = e + de * r, wa = uA + du * r, wb = qA + dq * r;
			float o[Units];
			for(int k = 0; k < Units; ++k)
			{
				const uint32_t step = static_cast<uint32_t>(n) * static_cast<uint32_t>(_v.inc[k]);
				const float ph = static_cast<float>((_v.phase[k] + step) & 0xffffff) / 16777216.0f;
				o[k] = env * (wa * cycles::fast::wave(w1, ph) + wb * cycles::fast::wave(w2, ph));
			}
			float x = 0.25f * o[0];
			for(int k = 0; k < Units - 1; ++k)
				x += (gA[k] + dg[k] * r) * o[k + 1];
			const float driven = (gainA + (gainB - gainA) * r) * post * std::clamp(x, -1.0f, 1.0f);
			const float y = b * driven + b * x1 - c * y1;
			x1 = driven;
			y1 = y;
			const float z = b * y + b * x2 - c * y2;
			x2 = y;
			y2 = z;
			out[n * Lanes] = z * (ampA + (ampB - ampA) * r) / 2;
		}
		_v.filter[0][0] = x1, _v.filter[0][1] = y1, _v.filter[1][0] = x2, _v.filter[1][1] = y2;
		for(int k = 0; k < Units; ++k)
		{
			const uint32_t advance = static_cast<uint32_t>(BlockSize) * static_cast<uint32_t>(_v.inc[k]);
			_v.phase[k] = (_v.phase[k] + advance) & 0xffffff;
		}
	}

	const Block& Chord::renderBlock()
	{
		const FlushDenormals ftz;
		auto& trigs = m_controls.trigs;
		trigs.start();
		for(int l = 0; l < m_lanes; ++l)
		{
			Voice& v = m_voice[static_cast<size_t>(l)];
			const bool trigBlock = trigs.now[l];
			// Without the waves nothing plays; a first trig's init clears its block's note-off
			const bool first = trigBlock && !v.started;
			if(trigBlock && m_waves)
				start(v, l);
			if(!v.started || !m_waves)
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
