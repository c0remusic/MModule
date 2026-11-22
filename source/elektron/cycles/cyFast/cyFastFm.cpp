#include "cyFastFm.h"

#include <cmath>

namespace cycles::fast
{
	double entry(const Curve& _c, const int _i)
	{
		for(size_t s = 0; s < _c.specialCount; ++s)
			if(_c.special[s].x == _i)
				return _c.special[s].v;
		double y = 0;
		if(_c.knots)
		{
			size_t k = 1;
			while(k + 1 < _c.knotCount && _c.knots[k].x < _i)
				++k;
			const Knot& a = _c.knots[k - 1];
			const Knot& b = _c.knots[k];
			y = _i <= a.x ? a.y : _i >= b.x ? b.y : a.y + (b.y - a.y) * (_i - a.x) / (b.x - a.x);
		}
		else
		{
			for(size_t s = 0; s < _c.segmentCount; ++s)
			{
				const Segment& g = _c.segments[s];
				if(_i < g.from || _i > g.to)
					continue;
				const double x = _i - g.from;
				y = g.kind == Segment::Exp ? g.c[0] + g.c[1] * std::pow(g.c[2], x)
					: ((g.c[0] * x + g.c[1]) * x + g.c[2]) * x + g.c[3];
			}
		}
		return _c.tau ? -std::exp(-1 / y) : _c.logTau ? -std::exp(-1 / std::exp(y)) : y;
	}

	Table makeTable(const Curve& _c)
	{
		Table t{};
		for(int i = 0; i < 128; ++i)
			t[static_cast<size_t>(i)] = entry(_c, i);
		return t;
	}

	std::array<float, 128> toFloat(const Table& _t)
	{
		std::array<float, 128> f{};
		for(size_t i = 0; i < 128; ++i)
			f[i] = static_cast<float>(_t[i]);
		return f;
	}

	std::array<int32_t, 128> toQ31(const Table& _t)
	{
		std::array<int32_t, 128> q{};
		for(size_t i = 0; i < 128; ++i)
			q[i] = static_cast<int32_t>(std::clamp(std::nearbyint(_t[i] * 2147483648.0), -2147483648.0, 2147483647.0));
		return q;
	}

	namespace
	{
		struct Sines
		{
			std::array<float, 257> f;
			std::array<int32_t, 257> q;

			Sines()
			{
				for(int i = 0; i < 257; ++i)
				{
					const double s = std::sin(2 * 3.14159265358979323846 * i / 256);
					f[static_cast<size_t>(i)] = static_cast<float>(s);
					q[static_cast<size_t>(i)] = static_cast<int32_t>(std::clamp(std::nearbyint(s * 2147483648.0),
						-2147483648.0, 2147483647.0));
				}
			}
		};

		const Sines& sines()
		{
			static const Sines s;
			return s;
		}

		// The pitch's 2048 steps an octave, Q30
		const std::array<int64_t, 2048>& pitchSteps()
		{
			static const auto t = []
			{
				std::array<int64_t, 2048> p{};
				for(int i = 0; i < 2048; ++i)
					p[static_cast<size_t>(i)] = std::min<int64_t>(std::llround(std::exp2(i / 2047.0) * 0x1p30),
						0x7fffffff);
				return p;
			}();
			return t;
		}
	}

	const std::array<float, 257>& sine() { return sines().f; }
	const std::array<int32_t, 257>& sineQ31() { return sines().q; }

	void warmUp()
	{
		sines();
		pitchSteps();
	}

	int64_t pitchWord(const int32_t _pitch, const int _note, const int64_t _offset)
	{
		// PTCH held to its record's range, 40 to 88 (semitones about 64), on every machine
		const int64_t pitch = std::clamp(_pitch, 40 << 8, 88 << 8);
		const int64_t fine = 0x4000;
		const int64_t d1 = s32((pitch << 16) + s32((fine - 0x4000) << 11)) >> 8;
		const int64_t p = std::clamp<int64_t>(_offset + d1 + (static_cast<int64_t>(_note) << 16), 0, 0x7F0000);
		const int64_t d0 = fm(p - 0x450000, 0xAAAAAAB);
		const int64_t octave = d0 >> 16, step = (d0 & 0xFFFF) >> 5;
		return fm(0x6E000000, pitchSteps()[static_cast<size_t>(step)]) >> (5 - octave);
	}

	void increments(const int32_t _pitch, const int _note, const int64_t _offset, const int64_t _depth,
		const int64_t _sweep, const int64_t* _multiplier, const int64_t* _factor, int64_t* _inc, const int64_t _rate)
	{
		const int64_t d4 = s32(fm(s32(_depth - 0x4000000), _sweep) + 0x4000000);
		const int64_t d3 = std::min<int64_t>(pitchWord(_pitch, _note, _offset), Max31);
		for(int k = 0; k < 4; ++k)
		{
			int64_t x = fm(_multiplier[k] >> 6, d4);
			x = fm(x, _factor[k]);
			x = s32(x << 11);
			x = std::clamp<int64_t>(2 * x, Min31, Max31);
			x = fm(x, d3);
			x = fm(x, _rate);
			_inc[k] = x >> 2;
		}
	}

	void FeedbackOperator::block(const int64_t _inc, const int64_t _feedback, int32_t* _out)
	{
		const auto& sine = sineQ31();
		const int64_t step = s32(_feedback - feedback) >> 5;
		int64_t fb = feedback, ph = phase, s = last;
		int64_t acc = (ph << 8) + ((fb * s * 2) >> 24);
		for(int n = 0; n < 32; ++n)
		{
			fb = s32(fb + step);
			const int64_t x = std::clamp(acc >> 8, Min31, Max31);
			const auto i = static_cast<size_t>((x & 0xFFFFFF) >> 16);
			const int64_t f = (x & 0xFFFF) << 15;
			const int64_t a = sine[i], b = sine[i + 1];
			s = std::clamp(((a << 8) + ((f * b * 2) >> 24) - ((f * a * 2) >> 24)) >> 8, Min31, Max31);
			ph = (ph + _inc) & 0xFFFFFF;
			_out[n] = static_cast<int32_t>(s);
			acc = (ph << 8) + ((fb * s * 2) >> 24);
		}
		phase = ph;
		last = s;
		feedback = _feedback;
	}

	void Attack::start(const double _step, const bool _hold, const bool _trigFirst)
	{
		step = _step;
		hold = _hold;
		trigFirst = _trigFirst;
		v = 0;
		state = Decaying;
		consumed = false;
	}

	void Attack::advance(const bool _trig, const double _decay, const bool _noteOff, const bool _cut)
	{
		const auto trigValues = [this]
		{
			if(step >= 1)
			{
				v = 1;
				state = Top;
			}
			else
			{
				v = step / 2;
				state = Rising;
			}
		};
		consumed = false;
		if(_trig && trigFirst)
			trigValues();
		switch(state)
		{
		case Rising:
			v = std::min(v + step, 1.0);
			if(v >= 1)
				state = Top;
			break;
		case Top:
			state = hold && !_noteOff ? Held : Decaying;
			break;
		case Held:
			if(_noteOff)
			{
				state = Decaying;
				consumed = true;
			}
			break;
		case Decaying:
			v = _decay * v;
			break;
		}
		if(_trig && !trigFirst)
			trigValues();
		if(_cut)
			v = 0;
	}

	double PunchGain::advance(const double* _fall, const double* _rise, const double _floor)
	{
		const bool falling = stage == 1;
		const double* c = falling ? _fall : _rise;
		const double in = falling ? 0.0 : 1.0;
		double out = c[0] * in + c[1] * x - c[2] * y;
		if(out <= _floor)
		{
			stage = 5;
			out = _floor;
		}
		x = in;
		y = out;
		return out;
	}
}
