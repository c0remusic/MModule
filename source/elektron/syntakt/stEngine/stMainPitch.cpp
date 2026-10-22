#include "stMainPitch.h"

#include "stEmac.h"
#include "stMainModulation.h"

namespace syntakt::main
{
	namespace
	{
		constexpr uint32_t Rates = 0x40245c50;		// a glide step per PTIM value
		constexpr uint32_t Spans = 0x40245a50;		// a factor per distance in semitones
		constexpr int32_t Done = INT32_MIN;
		constexpr int32_t Percent = 0x51eb851f;		// 0.64: x 2, a byte << 24 becomes the byte / 100

		bool bit(const int32_t _v, const int32_t _n) { return (static_cast<uint32_t>(_v) >> (_n & 31) & 1) != 0; }

		int32_t mulWrap(const int32_t _a, const int32_t _b)
		{
			return static_cast<int32_t>(static_cast<uint32_t>(_a) * static_cast<uint32_t>(_b));
		}

		// msac then movclr: the product negated
		int32_t negFrac(const int32_t _y, const int32_t _x)
		{
			EmacWrap acc;
			acc.msac(_y, _x);
			return acc.storeClear();
		}

		// swap then ext.l: the high word, signed
		int32_t highWord(const int32_t _v) { return static_cast<int16_t>(static_cast<uint32_t>(_v) >> 16); }

		uint32_t lastOf(const int32_t _t) { return LastPitch + 4 * static_cast<uint32_t>(_t); }

		// The distance a trig starts the glide from, before its scale
		int32_t startDistance(const Memory& _mem, const int32_t _t, const int32_t _source, const int32_t _target,
			const uint32_t _settings)
		{
			const int32_t held = static_cast<uint16_t>(_mem.half(Held));
			const int32_t legato = _mem.word(_settings + 0x68);
			if(legato == 0)
			{
				if(!bit(held, _source) || _source == -1)
					return 0;
				return subWrap(_mem.word(lastOf(_source)), _target);
			}
			const int32_t own = bit(held, _t) ? _t : -1;
			if(legato == 1)
				return _source == own ? subWrap(_mem.word(lastOf(_t)), _target) : 0;
			if(_source == -1 || _source != own || !bit(static_cast<uint16_t>(_mem.half(Gates)), own))
				return 0;
			return subWrap(_mem.word(lastOf(own)), _target);
		}

		// The glide's step: PTIM's rate, or for the time mode the rate over the distance's span
		int32_t step(const Memory& _mem, const uint32_t _track, const uint32_t _settings, const int32_t _distance)
		{
			const int32_t ptim = static_cast<int8_t>(_mem.byte(_track + 0x84));
			const int32_t rate = _mem.word(Rates + 4 * static_cast<uint32_t>(ptim));
			if(_mem.word(_settings + 0x70) != 0)
				return rate;
			const int32_t semitones = highWord(_distance < 0 ? neg(_distance) : _distance);
			if(semitones == 0)
				return Done;
			const int32_t span = _mem.word(Spans + 4 * static_cast<uint32_t>(semitones));
			const int32_t d3 = negFrac(rate, span);
			if(d3 < static_cast<int32_t>(0xfc71c71d))
				return Done;
			int32_t d4 = subWrap(rate, mulWrap(d3, semitones));
			d4 = addWrap(asl(d4, 21), asl(d4, 18));
			d4 = highWord(addWrap(negFrac(d4, span), 0x8000));
			const int32_t s = addWrap(addWrap(asl(d3, 5), asl(d3, 2)), d4);
			return s > 0 ? Done : s;
		}
	}

	int32_t portamento(Memory& _mem, const int32_t _t, const int32_t _source, const int32_t _trig,
		const int32_t _target, const uint32_t _p)
	{
		const auto t = static_cast<uint32_t>(_t);
		const uint32_t settings = _p + 2 * EngineWords + SettingsStride * t;
		const uint32_t track = _p + TrackBytes * t;
		const uint32_t glide = Glide + 8 * t;
		const bool trig = static_cast<uint8_t>(_trig) != 0;

		if(trig)
		{
			_mem.setWord(glide, 0);
			_mem.setWord(glide + 4, startDistance(_mem, _t, _source, _target, settings));
			int32_t scale = negFrac(Percent, asl(static_cast<int8_t>(_mem.byte(settings + 0x64)), 24));
			scale = addSat(scale, scale);
			_mem.setWord(glide + 4, negFrac(_mem.word(glide + 4), scale));
		}
		if(_mem.half(track + 0x86) <= 0x3fff)
			_mem.setWord(glide, Done);

		int32_t distance = _mem.word(glide + 4);
		const bool semitones = _mem.word(settings + 0x6c) != 0;
		if(semitones)
		{
			distance = addWrap(distance, 0x8000);
			distance = subWrap(distance, distance < 0 ? 1 : 0);
			distance = static_cast<int32_t>(static_cast<uint32_t>(distance) & 0xffff0000);
		}
		const int32_t progress = _mem.word(glide);
		int32_t pitch = addWrap(negFrac(subWrap(Done, progress), distance), _target);
		if(semitones)
		{
			pitch = addWrap(pitch, distance > 0 ? 0xffff : 0);
			pitch = static_cast<int32_t>(static_cast<uint32_t>(pitch) & 0xffff0000);
		}

		const bool gate = bit(static_cast<uint16_t>(_mem.half(Gates)), _t);
		const bool frozen = _mem.word(settings + 0x74) != 0 && !gate && !trig;
		if(!frozen)
			_mem.setWord(glide, addSat(progress, step(_mem, track, settings, distance)));
		_mem.setWord(lastOf(_t), pitch);
		return pitch;
	}

	void pitches(Memory& _mem, const uint32_t _p, const int32_t _trigs)
	{
		for(uint32_t t = 0; t < 12; ++t)
		{
			const auto trig = static_cast<int32_t>(static_cast<uint32_t>(_trigs) >> t & 1);
			const int32_t glided = portamento(_mem, static_cast<int32_t>(t), static_cast<int32_t>(t), trig,
				_mem.word(Notes + 4 * t), _p);
			const uint32_t settings = _p + 2 * EngineWords + SettingsStride * t;
			const int32_t range = asl(static_cast<int8_t>(_mem.byte(settings + 0x7a)), 16);
			const int32_t bend = asl(_mem.half(PitchBend + 2 * t), 16);
			_mem.setWord(Pitches + 4 * t, addWrap(glided, fracWrap(range, bend)));
		}
	}
}
