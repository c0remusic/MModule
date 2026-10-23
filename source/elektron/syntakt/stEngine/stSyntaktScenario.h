#pragma once

// stSyntaktTest's scenario, which stSyntakt16Test plays too: the calls of half a second (machines on several tracks,
// notes with velocities and lengths, knobs, locks, the delay's and the reverb's sends, the tempo, a level), and the
// sample set SP TWINSHOT plays, as the tests read it

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "stMainMemory.h"
#include "stSection7.h"
#include "stSyntakt.h"
#include "stVoice.h"

namespace syntakt::test
{
	constexpr size_t Total = 24000;		// 750 blocks, half a second

	enum class Kind
	{
		Tempo, Machine, Parameter, Fx, Level, Lock, On, Off
	};

	struct Call
	{
		size_t at;		// the sample the call is made at
		Kind kind;
		int a = 0, b = 0, c = 0;
		double bpm = 0;
	};

	inline std::vector<Call> scenario()
	{
		std::vector<Call> s;
		auto call = [&](const size_t _at, const Kind _kind, const int _a = 0, const int _b = 0, const int _c = 0)
		{
			s.push_back({_at, _kind, _a, _b, _c});
		};

		// The effects within the half second: a short delay with feedback, a short pre-delay; the reverb's return on
		// the direct bus, the delay's on the FX bus (its default); tracks 1-4 on the direct bus, 5-8 on the FX bus
		s.push_back({0, Kind::Tempo, 0, 0, 0, 132.5});
		call(0, Kind::Fx, Syntakt::DelayTime, 0x0400);
		call(0, Kind::Fx, Syntakt::DelayFeedback, 0x5000);
		call(0, Kind::Fx, Syntakt::ReverbPreDelay, 0x0800);
		call(0, Kind::Fx, Syntakt::ReverbDecay, 0x5800);
		call(0, Kind::Fx, Syntakt::ReverbRouting, 0);
		for(int t = 0; t < 4; ++t)
			call(0, Kind::Parameter, t, Syntakt::Routing, 0);
		call(0, Kind::Parameter, 0, Syntakt::DelaySend, 0x6000);
		call(0, Kind::Parameter, 2, Syntakt::ReverbSend, 0x7000);
		call(5, Kind::Parameter, 4, Syntakt::DelaySend, 0x5000);
		call(5, Kind::Parameter, 4, Syntakt::ReverbSend, 0x5000);
		call(40, Kind::Parameter, 5, Syntakt::ReverbSend, 0x6400);
		call(40, Kind::Parameter, 7, Syntakt::DelaySend, 0x4800);
		call(70, Kind::Parameter, 5, Syntakt::Pan, 0x2000);

		// Hits on every track, at offsets that fall anywhere in a block, each with its velocity and length; some end
		// in the block they start in, some run over the next hit of their track
		const int notes[] = {36, 48, 60, 55, 43, 67, 72, 52, 40, 64, 58, 47};
		const int velocities[] = {100, 64, 127, 90, 30, 110, 75, 120};
		const size_t lengths[] = {10, 200, 900, 2500, 64, 31, 1500, 6000, 400};
		for(int k = 0; k < 64; ++k)
		{
			const int t = (k * 3) % 8;
			const size_t at = 37 + 360 * static_cast<size_t>(k) + static_cast<size_t>((k * 7) % 29);
			call(at, Kind::On, t, notes[k % 12], velocities[(k + k / 8) % 8]);
			call(at + lengths[k % 9], Kind::Off, t);
		}
		call(0, Kind::On, 6, 60, 127);
		call(0, Kind::Off, 6);

		// Locks for track 1's second hit (2944): TUNE and the filter's frequency; its third (5822) goes back
		call(1450, Kind::Lock, 0, Syntakt::Tune, 0x5000);
		call(1450, Kind::Lock, 0, Syntakt::FilterFrequency, 0x3000);

		// Knobs: track 5's filter swept down, track 2's tune, track 3's level, track 8's decay
		for(int k = 0; k < 24; ++k)
			call(4000 + 333 * static_cast<size_t>(k), Kind::Parameter, 4, Syntakt::FilterFrequency, 0x7f00 - 0x280 * k);
		call(4011, Kind::Parameter, 4, Syntakt::FilterResonance, 0x5000);
		call(9000, Kind::Parameter, 1, Syntakt::Tune, 0x3000);
		call(7000, Kind::Level, 2, 0x5000);
		call(8200, Kind::Parameter, 7, Syntakt::Decay, 0x2000);

		// The other four machines, then the tempo
		call(12000, Kind::Machine, 1, 1);		// SD BASIC
		call(12000, Kind::Machine, 3, 3);		// PC CARBON
		call(12013, Kind::Machine, 6, 38);		// SY SWARM
		call(12013, Kind::Machine, 7, 44);		// SP TWINSHOT
		s.push_back({15000, Kind::Tempo, 0, 0, 0, 150});

		// The notes still sounding at the end are not ended
		s.erase(std::remove_if(s.begin(), s.end(), [](const Call& _c) { return _c.at >= Total; }), s.end());
		std::stable_sort(s.begin(), s.end(), [](const Call& _x, const Call& _y) { return _x.at < _y.at; });
		return s;
	}

	inline void apply(Syntakt& _st, const Call& _c)
	{
		switch(_c.kind)
		{
		case Kind::Tempo:		_st.setTempo(_c.bpm); break;
		case Kind::Machine:		_st.setMachine(_c.a, _c.b); break;
		case Kind::Parameter:	_st.setParameter(_c.a, _c.b, static_cast<uint16_t>(_c.c)); break;
		case Kind::Fx:			_st.setFxParameter(_c.a, static_cast<uint16_t>(_c.b)); break;
		case Kind::Level:		_st.setLevel(_c.a, static_cast<uint16_t>(_c.b)); break;
		case Kind::Lock:		_st.lock(_c.a, _c.b, static_cast<uint16_t>(_c.c)); break;
		case Kind::On:			_st.noteOn(_c.a, _c.b, _c.c); break;
		case Kind::Off:			_st.noteOff(_c.a); break;
		}
	}

	// As stMainOracleTest reads it: "STS1", 64 directory entries (u32 length, u32 pointer), u32 base, u32 size, data
	inline bool readSamples(const std::string& _path, syntakt::Samples& _samples)
	{
		const std::vector<uint8_t> blob = syntakt::readFile(_path);
		constexpr size_t header = 4 + 8 * 64 + 8;
		if(blob.size() < header || std::string(blob.begin(), blob.begin() + 4) != "STS1")
			return false;
		for(size_t i = 0; i < _samples.directory.size(); ++i)
			_samples.directory[i] = {syntakt::be32(&blob[4 + 8 * i]), syntakt::be32(&blob[8 + 8 * i])};
		_samples.base = syntakt::be32(&blob[4 + 8 * 64]);
		const uint32_t size = syntakt::be32(&blob[8 + 8 * 64]);
		if(blob.size() != header + size)
			return false;
		_samples.data.assign(blob.begin() + header, blob.end());
		return true;
	}
}
