#include "edDrumsChain.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include <emmintrin.h>

namespace elektron::drums
{
	struct Chain::StageLevel
	{
		float in, out;
	};

	namespace
	{
		using V = __m128;
		using StageLevel = Chain::StageLevel;

		constexpr double Pi = 3.14159265358979323846;

		constexpr double ShelfQ = 0.7071067811865476, BellQ = 1;
		constexpr int SyntaktOtherFilter = static_cast<int>(FilterType::Count);	// the filter's place holds FilterType's and this

		// The levels the firmware's stages take and give (mdFastTrackFx.h, stFastTrack.h), the voice at 1: the DSP's headroom
		constexpr StageLevel MdEqLevel{1, 1 / 8.0f}, MdFilterLevel{1 / 8.0f, 1 / 32.0f}, MdSrrLevel{1 / 32.0f, 1 / 32.0f};
		constexpr StageLevel MdDistLevel{1 / 32.0f, 1 / 4.0f};		// x 8 at DIST 0
		constexpr StageLevel SyntaktDriveLevel{1, 0.5f}, BaseWidthLevel{0.5f, 0.5f}, MultimodeLevel{0.5f, 0.125f};

		void scale(Block& _buf, const float _gain, const uint16_t _groups)
		{
			if(_gain == 1)
				return;
			const V g = _mm_set1_ps(_gain);
			for(int l = 0; l < Lanes; l += 4)
			{
				if(!(_groups >> l & 15))
					continue;
				for(auto& sample : _buf.s)
					_mm_store_ps(sample + l, _mm_mul_ps(_mm_load_ps(sample + l), g));
			}
		}

		// _to's lanes of _lanes from _from
		void pick(Block& _to, const Block& _from, const uint16_t _lanes)
		{
			for(int l = 0; l < Lanes; ++l)
				if(_lanes >> l & 1)
					for(int n = 0; n < BlockSize; ++n)
						_to.s[n][l] = _from.s[n][l];
		}

		uint16_t groupsOf(uint16_t _lanes)
		{
			uint16_t groups = 0;
			for(int l = 0; l < Lanes; l += 4)
				if(_lanes >> l & 15)
					groups |= static_cast<uint16_t>(15 << l);
			return groups;
		}

		// A stage's pass at its level on _lanes, the others of their groups kept
		template<typename Stage>
		void pass(Stage& _stage, Block& _buf, const uint16_t _lanes, const StageLevel _level)
		{
			if(!_lanes)
				return;
			const uint16_t groups = groupsOf(_lanes);
			if(groups == _lanes)
			{
				scale(_buf, _level.in, groups);
				_stage.process(_buf, _lanes);
				scale(_buf, 1 / _level.out, groups);
				return;
			}
			alignas(16) Block work = _buf;
			scale(work, _level.in, groups);
			_stage.process(work, _lanes);
			scale(work, 1 / _level.out, groups);
			pick(_buf, work, _lanes);
		}

		// The drive's wet share, _from to _to over the block's _part of _parts; _wet takes its dry from _dry
		void blend(Block& _wet, const Block& _dry, const uint16_t _lanes, const float* _from, const float* _to, const int _part,
			const int _parts)
		{
			for(int l = 0; l < Lanes; ++l)
			{
				if(!(_lanes >> l & 1) || (_from[l] == 1 && _to[l] == 1))
					continue;
				const float step = (_to[l] - _from[l]) / static_cast<float>(BlockSize * _parts);
				float g = _from[l] + step * static_cast<float>(BlockSize * _part);
				for(int n = 0; n < BlockSize; ++n)
				{
					_wet.s[n][l] = _dry.s[n][l] + g * (_wet.s[n][l] - _dry.s[n][l]);
					g += step;
				}
			}
		}

	}

	ChainSettings syntaktChain()
	{
		ChainSettings s;
		s.drive[0].type = DriveType::Syntakt;
		s.band.type = BandType::BaseWidth;
		s.band.values = {0, 127};
		s.filter.type = FilterType::Syntakt;
		return s;
	}

	ChainSettings machinedrumChain()
	{
		ChainSettings s;
		s.drive[0] = {DriveType::MdSrr, true, 0, 127};
		s.drive[1] = {DriveType::MdDist, true, 0, 127};
		s.band.type = BandType::MdEq;
		s.band.values = {64, 63};		// 63 is flat, the boot's 64 already 0.24 dB up
		s.filter.type = FilterType::Machinedrum;
		s.filter.frequency = 0;
		return s;
	}

	// ---- the three bands ----

	namespace
	{
		ThreeBand::Words words(const double _g, const double _k)
		{
			ThreeBand::Words w;
			const double a1 = 1 / (1 + _g * (_g + _k)), a2 = _g * a1;
			w.a1 = static_cast<float>(a1);
			w.a2 = static_cast<float>(a2);
			w.a3 = static_cast<float>(_g * a2);
			return w;
		}
	}

	ThreeBand::Words ThreeBand::lowShelf(const double _hz, const double _db, const double _rate)
	{
		const double a = std::pow(10.0, _db / 40), k = 1 / ShelfQ;
		auto w = words(std::tan(Pi * _hz / _rate) / std::sqrt(a), k);
		w.m1 = static_cast<float>(k * (a - 1));
		w.m2 = static_cast<float>(a * a - 1);
		return w;
	}

	ThreeBand::Words ThreeBand::bell(const double _hz, const double _db, const double _rate)
	{
		const double a = std::pow(10.0, _db / 40), k = 1 / (BellQ * a);
		auto w = words(std::tan(Pi * _hz / _rate), k);
		w.m1 = static_cast<float>(k * (a * a - 1));
		return w;
	}

	ThreeBand::Words ThreeBand::highShelf(const double _hz, const double _db, const double _rate)
	{
		const double a = std::pow(10.0, _db / 40), k = 1 / ShelfQ;
		auto w = words(std::tan(Pi * _hz / _rate) * std::sqrt(a), k);
		w.m0 = static_cast<float>(a * a);
		w.m1 = static_cast<float>(k * (1 - a) * a);
		w.m2 = static_cast<float>(1 - a * a);
		return w;
	}

	ThreeBand::ThreeBand()
	{
		for(int b = 0; b < Bands; ++b)
			for(int l = 0; l < Lanes; ++l)
				m_now[b][M0][l] = m_to[b][M0][l] = 1;
	}

	void ThreeBand::set(const int _lane, const int _band, const Words& _words)
	{
		m_to[_band][A1][_lane] = _words.a1;
		m_to[_band][A2][_lane] = _words.a2;
		m_to[_band][A3][_lane] = _words.a3;
		m_to[_band][M0][_lane] = _words.m0;
		m_to[_band][M1][_lane] = _words.m1;
		m_to[_band][M2][_lane] = _words.m2;
	}

	void ThreeBand::clear(const int _lane)
	{
		for(int b = 0; b < Bands; ++b)
		{
			m_ic1[b][_lane] = m_ic2[b][_lane] = 0;
			for(int w = 0; w < WordCount; ++w)
				m_now[b][w][_lane] = m_to[b][w][_lane];
		}
	}

	void ThreeBand::process(Block& _buf, const uint16_t _lanes)
	{
		for(int l = 0; l < Lanes; l += 4)
		{
			if(!(_lanes >> l & 15))
				continue;
			for(int b = 0; b < Bands; ++b)
			{
				V now[WordCount], step[WordCount];
				for(int w = 0; w < WordCount; ++w)
				{
					now[w] = _mm_load_ps(m_now[b][w] + l);
					step[w] = _mm_mul_ps(_mm_sub_ps(_mm_load_ps(m_to[b][w] + l), now[w]), _mm_set1_ps(1.0f / BlockSize));
				}
				V ic1 = _mm_load_ps(m_ic1[b] + l), ic2 = _mm_load_ps(m_ic2[b] + l);
				for(int n = 0; n < BlockSize; ++n)
				{
					const V x = _mm_load_ps(&_buf.s[n][l]);
					const V v3 = _mm_sub_ps(x, ic2);
					const V v1 = _mm_add_ps(_mm_mul_ps(now[A1], ic1), _mm_mul_ps(now[A2], v3));
					const V v2 = _mm_add_ps(ic2, _mm_add_ps(_mm_mul_ps(now[A2], ic1), _mm_mul_ps(now[A3], v3)));
					ic1 = _mm_sub_ps(_mm_add_ps(v1, v1), ic1);
					ic2 = _mm_sub_ps(_mm_add_ps(v2, v2), ic2);
					const V y = _mm_add_ps(_mm_mul_ps(now[M0], x), _mm_add_ps(_mm_mul_ps(now[M1], v1), _mm_mul_ps(now[M2], v2)));
					_mm_store_ps(&_buf.s[n][l], y);
					for(int w = 0; w < WordCount; ++w)
						now[w] = _mm_add_ps(now[w], step[w]);
				}
				_mm_store_ps(m_ic1[b] + l, ic1);
				_mm_store_ps(m_ic2[b] + l, ic2);
				for(int w = 0; w < WordCount; ++w)
					_mm_store_ps(m_now[b][w] + l, _mm_load_ps(m_to[b][w] + l));
			}
		}
	}

	// ---- the chain ----

	namespace
	{
		int clampValue(const int _value) { return std::clamp(_value, 0, 127); }


		// What a Kit or a restored state may hold, inside what the stages' tables hold
		ChainSettings limited(ChainSettings _s)
		{
			for(auto& d : _s.drive)
			{
				d.amount = clampValue(d.amount);
				d.mix = clampValue(d.mix);
			}
			for(auto& v : _s.band.values)
				v = clampValue(v);
			auto& f = _s.filter;
			f.mode = std::clamp(f.mode, 0, 7);
			f.frequency = clampValue(f.frequency);
			f.resonance = clampValue(f.resonance);
			f.width = clampValue(f.width);
			f.depth = clampValue(f.depth);
			f.keyTrack = std::clamp(f.keyTrack, -128, 127);
			for(int* v : {&f.envelope.delay, &f.envelope.attack, &f.envelope.decay, &f.envelope.sustain, &f.envelope.release})
				*v = clampValue(*v);
			auto& a = _s.amp;
			for(int* v : {&a.attack, &a.hold, &a.decay, &a.sustain, &a.release})
				*v = clampValue(*v);
			_s.velocityDepth = std::clamp(_s.velocityDepth, -127, 127);
			return _s;
		}
	}

	Chain::DriveStages::DriveStages(const double _rate)
		: srr(_rate), rates{Oversampler(_rate), Oversampler(_rate), Oversampler(_rate), Oversampler(_rate)}
	{
	}

	Chain::BandStages::BandStages(const double _rate) : baseWidth(_rate), eq(_rate) {}
	Chain::FilterStages::FilterStages(const double _rate) : syntakt(_rate), syntaktOther(_rate), machinedrum(_rate) {}

	Chain::Chain(const double _rate, const bool _syntakt)
		: m_rate(_rate), m_syntakt(_syntakt), m_drives{DriveStages(_rate), DriveStages(_rate), DriveStages(_rate), DriveStages(_rate)},
		m_bands{BandStages(_rate), BandStages(_rate)}, m_filter(_rate), m_amp(_rate), m_envelope(_rate)
	{
		static_assert(static_cast<int>(DriveType::Count) == Types && static_cast<int>(BandType::Count) == Types
			&& static_cast<int>(FilterType::Count) + 1 == Types, "a place holds Types kinds of stage");
		std::fill_n(&m_mix[0][0], Places * Lanes, 1.0f);
		std::fill_n(&m_mixNow[0][0], Places * Lanes, 1.0f);
		for(auto& place : m_pending)
			place.fill(NoPending);
	}

	syntakt::fast::Multimode& Chain::syntaktFilter(const int _lane)
	{
		return m_other[static_cast<size_t>(_lane)] ? m_filter.syntaktOther : m_filter.syntakt;
	}

	void Chain::setOversampling(const int _factor)
	{
		m_factor = _factor;
	}

	Chain::DriveStages& Chain::driveAt(const int _place)
	{
		return m_drives[static_cast<size_t>(_place <= Pre1 ? _place : _place - Post0 + 2)];
	}

	Chain::BandStages& Chain::bandAt(const int _place)
	{
		return m_bands[_place == BandAfter ? 1 : 0];
	}

	bool Chain::raised(const bool _post, const int _type) const
	{
		if(m_factor == 1 || _type == static_cast<int>(DriveType::MdSrr))
			return false;
		// A firmware's drive where its firmware has it stays that firmware's
		return m_syntakt ? !(_type == static_cast<int>(DriveType::Syntakt) && !_post)
			: !(_type == static_cast<int>(DriveType::MdDist) && _post);
	}

	template<typename Function>
	void Chain::withStage(const int _place, const int _type, Function&& _function)
	{
		if(_place == FilterPlace)
		{
			if(_type == static_cast<int>(FilterType::Syntakt))
				_function(m_filter.syntakt, MultimodeLevel);
			else if(_type == SyntaktOtherFilter)
				_function(m_filter.syntaktOther, MultimodeLevel);
			else if(_type == static_cast<int>(FilterType::Machinedrum))
				_function(m_filter.machinedrum, MdFilterLevel);
		}
		else if(_place == BandBefore || _place == BandAfter)
		{
			auto& s = bandAt(_place);
			if(_type == static_cast<int>(BandType::BaseWidth))
				_function(s.baseWidth, BaseWidthLevel);
			else if(_type == static_cast<int>(BandType::MdEq))
				_function(s.eq, MdEqLevel);
			else if(_type == static_cast<int>(BandType::ThreeBand))
				_function(s.threeBand, StageLevel{1, 1});
		}
		else
		{
			auto& s = driveAt(_place);
			if(_type == static_cast<int>(DriveType::Syntakt))
				_function(s.syntakt, SyntaktDriveLevel);
			else if(_type == static_cast<int>(DriveType::MdDist))
				_function(s.dist, MdDistLevel);
			else if(_type == static_cast<int>(DriveType::MdSrr))
				_function(s.srr, MdSrrLevel);
		}
	}

	void Chain::clearStage(const int _place, const int _type, const int _lane)
	{
		withStage(_place, _type, [&](auto& _stage, StageLevel) { _stage.clear(_lane); });
		if(isDrive(_place))
			driveAt(_place).rates[static_cast<size_t>(_type)].clear(_lane);
	}

	void Chain::clearStages(const int _lane)
	{
		for(int p = 0; p < Places; ++p)
			clearStage(p, m_type[static_cast<size_t>(p)][static_cast<size_t>(_lane)], _lane);
	}

	void Chain::place(const int _lane, const int _place, const int _type)
	{
		const auto p = static_cast<size_t>(_place), l = static_cast<size_t>(_lane);
		const auto bit = static_cast<uint16_t>(1 << _lane);

		// One crossfade at a time: the latest change waits for its end
		if(m_fading[p] & bit)
		{
			m_pending[p][l] = static_cast<int8_t>(_type);
			return;
		}
		const int old = m_type[p][l];
		if(old == _type)
			return;
		m_type[p][l] = static_cast<uint8_t>(_type);
		m_lanes[p][static_cast<size_t>(old)] &= static_cast<uint16_t>(~bit);
		m_lanes[p][static_cast<size_t>(_type)] |= bit;
		clearStage(_place, _type, _lane);
		if(m_live & bit)
		{
			m_old[p][l] = static_cast<uint8_t>(old);
			m_fade[p][l] = -WarmSamples;
			m_fading[p] |= bit;
		}
		else
		{
			clearStage(_place, old, _lane);
		}
	}

	void Chain::endFade(const int _place, const int _lane)
	{
		const auto p = static_cast<size_t>(_place), l = static_cast<size_t>(_lane);
		clearStage(_place, m_old[p][l], _lane);
		m_fading[p] &= static_cast<uint16_t>(~(1 << _lane));
		const int pending = std::exchange(m_pending[p][l], NoPending);
		if(pending != NoPending)
			place(_lane, _place, pending);
	}

	int Chain::offset(const int _lane, const VelocityTarget _target) const
	{
		const auto i = static_cast<size_t>(_lane);
		const auto& s = m_settings[i];
		const int velocity = s.velocityTarget == _target ? s.velocityDepth * m_velocity[i] / 127 : 0;
		return velocity * 128 + m_modulation[i][static_cast<size_t>(_target)];
	}

	void Chain::setDrive(const int _lane, const int _place, const ChainSettings::Drive& _drive, const int _offset)
	{
		const int amount = wordOf(_drive.amount, _offset);
		auto& s = driveAt(_place);
		switch(_drive.type)
		{
		case DriveType::Syntakt:	s.syntakt.set(_lane, amount << 1); break;
		case DriveType::MdDist:		s.dist.set(_lane, amount); break;
		case DriveType::MdSrr:		s.srr.set(_lane, amount); break;
		default:					break;
		}
		m_mix[_place][_lane] = static_cast<float>(_drive.mix) / 127.0f;
	}

	void Chain::setBand(const int _lane, const int _place, const bool _a, const bool _b)
	{
		const auto& band = m_settings[static_cast<size_t>(_lane)].band;
		const int a = offset(_lane, VelocityTarget::BandA), b = offset(_lane, VelocityTarget::BandB);
		auto& s = bandAt(_place);
		const auto word = [&](const size_t _i, const int _offset) { return wordOf(band.values[_i], _offset); };
		const auto hz = [](const int _v) { return 20 * std::pow(1000.0, _v / 127.0); };
		const auto db = [](const double _v) { return (_v - 64) * 18.0 / 63; };
		switch(band.type)
		{
		case BandType::BaseWidth:
			s.baseWidth.set(_lane, word(0, a) << 1, word(1, b) << 1);
			break;
		case BandType::MdEq:
			s.eq.set(_lane, word(0, a), word(1, b));
			break;
		case BandType::ThreeBand:
			if(_a)
				s.threeBand.set(_lane, 0, ThreeBand::lowShelf(hz(band.values[0]), db(word(1, a) / 128.0), m_rate));
			if(_b)
				s.threeBand.set(_lane, 1, ThreeBand::bell(hz(band.values[2]), db(word(3, b) / 128.0), m_rate));
			if(_a && _b)
				s.threeBand.set(_lane, 2, ThreeBand::highShelf(hz(band.values[4]), db(band.values[5]), m_rate));
			break;
		default:
			break;
		}
	}

	int Chain::envelopeDepth(const int _lane) const
	{
		return wordOf(m_settings[static_cast<size_t>(_lane)].filter.depth, offset(_lane, VelocityTarget::EnvelopeDepth));
	}

	void Chain::setFilter(const int _lane, const double _envelope)
	{
		const auto i = static_cast<size_t>(_lane);
		const auto& f = m_settings[i].filter;
		const int frequency = wordOf(f.frequency, offset(_lane, VelocityTarget::Frequency));
		const int resonance = wordOf(f.resonance, offset(_lane, VelocityTarget::Resonance));
		const int depth = envelopeDepth(_lane);
		if(f.type == FilterType::Syntakt)
		{
			syntaktFilter(_lane).set(_lane, f.mode, syntakt::fast::filterCutoff(frequency << 1, depth << 1, f.keyTrack, m_note[i],
				_envelope), resonance << 17, true);
		}
		else if(f.type == FilterType::Machinedrum)
		{
			// The envelope moves the whole band: FLTF rises and FLTW follows
			const double moved = frequency + _envelope * (depth - (64 << 7)) * 2;
			const int width = wordOf(f.width, offset(_lane, VelocityTarget::Width));
			m_filter.machinedrum.set(_lane, std::clamp(static_cast<int>(moved), 0, 0x3fff), width, resonance);
		}
	}

	std::array<int, 2> Chain::drivePlaces(const ChainSettings& _settings)
	{
		std::array<int, 2> places{};
		int pre = 0, post = 0;
		for(size_t d = 0; d < 2; ++d)
			if(_settings.drive[d].type != DriveType::Off)
				places[d] = _settings.drive[d].post ? Post0 + post++ : Pre0 + pre++;
		return places;
	}

	void Chain::setValues(const int _lane)
	{
		const auto i = static_cast<size_t>(_lane);
		const auto& s = m_settings[i];
		const auto places = drivePlaces(s);
		for(size_t d = 0; d < 2; ++d)
			if(s.drive[d].type != DriveType::Off)
				setDrive(_lane, places[d], s.drive[d], offset(_lane, d == 0 ? VelocityTarget::DriveA : VelocityTarget::DriveB));
		setBand(_lane, s.band.after ? BandAfter : BandBefore);
		setFilter(_lane, m_envelope.value(_lane));
	}

	void Chain::takeMoves(const int _lane, const bool _filterSet)
	{
		const auto i = static_cast<size_t>(_lane);
		const auto& s = m_settings[i];
		const auto moved = [&](const VelocityTarget _target) { return m_moved[i] >> static_cast<int>(_target) & 1; };
		const auto places = drivePlaces(s);
		for(size_t d = 0; d < 2; ++d)
		{
			const auto target = d == 0 ? VelocityTarget::DriveA : VelocityTarget::DriveB;
			if(moved(target) && s.drive[d].type != DriveType::Off)
				setDrive(_lane, places[d], s.drive[d], offset(_lane, target));
		}
		if(moved(VelocityTarget::BandA) || moved(VelocityTarget::BandB))
			setBand(_lane, s.band.after ? BandAfter : BandBefore, moved(VelocityTarget::BandA), moved(VelocityTarget::BandB));
		if(!_filterSet && (moved(VelocityTarget::Frequency) || moved(VelocityTarget::Resonance) || moved(VelocityTarget::Width)
			|| moved(VelocityTarget::EnvelopeDepth)))
			setFilter(_lane, m_envelope.value(_lane));
		m_moved[i] = 0;
	}

	void Chain::configure(const int _lane, const ChainSettings& _settings)
	{
		const auto i = static_cast<size_t>(_lane);
		const auto bit = static_cast<uint16_t>(1 << _lane);
		const int mode = m_settings[i].filter.mode;
		m_settings[i] = limited(_settings);
		const auto& s = m_settings[i];

		// A Syntakt type that leaves the resonator's group would fade out and snap in a block each, as on the machine, which
		// clicks at -43 dB: the other stage takes it over the crossfade instead
		const int held = m_type[FilterPlace][i];
		const bool wasSyntakt = held == static_cast<int>(FilterType::Syntakt) || held == SyntaktOtherFilter;
		const bool morphs = syntakt::fast::isMorph(s.filter.mode) && syntakt::fast::isMorph(mode);
		if(s.filter.type == FilterType::Syntakt && wasSyntakt && m_live & bit && s.filter.mode != mode && !morphs)
			m_other[i] = !m_other[i];

		// The values first: a stage taken starts settled on them
		setValues(_lane);
		std::array<int, Places> types{};
		const auto places = drivePlaces(s);
		for(size_t d = 0; d < 2; ++d)
			if(s.drive[d].type != DriveType::Off)
				types[static_cast<size_t>(places[d])] = static_cast<int>(s.drive[d].type);
		for(const int p : {Pre0, Pre1, Post0, Post1})
			place(_lane, p, types[static_cast<size_t>(p)]);
		place(_lane, BandBefore, s.band.after ? 0 : static_cast<int>(s.band.type));
		place(_lane, BandAfter, s.band.after ? static_cast<int>(s.band.type) : 0);
		place(_lane, FilterPlace, s.filter.type == FilterType::Syntakt && m_other[i] ? SyntaktOtherFilter
			: static_cast<int>(s.filter.type));

		if(s.amp.on && m_syntakt)
		{
			m_amp.set(_lane, s.amp.attack, s.amp.hold, s.amp.decay, s.amp.sustain, s.amp.release, s.amp.adsr);
			m_ampLanes |= bit;
		}
		else
		{
			m_ampLanes &= static_cast<uint16_t>(~bit);
		}

		// On silence, nothing to ramp from: a first Hit plays what the settings say
		if(!(m_live & bit))
		{
			clearStages(_lane);
			for(int p = 0; p < Places; ++p)
				m_mixNow[p][_lane] = m_mix[p][_lane];
		}
	}

	void Chain::modulate(const int _lane, const VelocityTarget _target, const int _offset)
	{
		const auto i = static_cast<size_t>(_lane);
		auto& now = m_modulation[i][static_cast<size_t>(_target)];
		if(now == _offset)
			return;
		now = _offset;
		m_moved[i] |= static_cast<uint16_t>(1 << static_cast<int>(_target));
	}

	int Chain::moveLimit(const int _lane, const VelocityTarget _target) const
	{
		constexpr int Whole = 127 << 7;
		const bool syntaktFilter = m_settings[static_cast<size_t>(_lane)].filter.type == FilterType::Syntakt;
		return syntaktFilter && _target >= VelocityTarget::Frequency ? Whole / 8 : Whole / 24;
	}

	void Chain::setNote(const int _lane, const int _note)
	{
		const auto i = static_cast<size_t>(_lane);
		m_note[i] = _note;
		if(m_settings[i].filter.keyTrack)
			setFilter(_lane, m_envelope.value(_lane));
	}

	void Chain::clear(const int _lane)
	{
		const auto bit = static_cast<uint16_t>(1 << _lane);
		m_live &= static_cast<uint16_t>(~bit);
		for(int p = 0; p < Places; ++p)
			if(m_fading[static_cast<size_t>(p)] & bit)
				endFade(p, _lane);
		clearStages(_lane);
		m_amp.clear(_lane);
		m_envelope.clear(_lane);
	}

	void Chain::hit(const int _lane, const int _velocity)
	{
		const auto i = static_cast<size_t>(_lane);
		const auto bit = static_cast<uint16_t>(1 << _lane);
		m_velocity[i] = _velocity;
		if(m_settings[i].velocityTarget != VelocityTarget::None || m_moved[i])
			setValues(_lane);

		// What its group left in a lane no one was computing goes, the filter's flags after it, which it would take too
		if(!(m_live & bit))
			clearStages(_lane);
		m_trig[i] = true;
		if(m_syntakt)
		{
			syntaktFilter(_lane).trig(_lane);
			if(m_ampLanes & bit)
				m_amp.trig(_lane);
			m_resetNext |= bit;
		}
		else
		{
			syntaktFilter(_lane).hit(_lane);
			m_reset |= bit;
		}
	}

	void Chain::gateOff(const int _lane)
	{
		if(!(m_live >> _lane & 1))
			return;
		if(m_ampLanes >> _lane & 1)
			m_amp.gateOff(_lane);
		m_gateOff[static_cast<size_t>(_lane)] = true;
	}

	void Chain::stepEnvelopes(const uint16_t _active)
	{
		for(int l = 0; l < Lanes; ++l)
		{
			const auto i = static_cast<size_t>(l);
			const auto& s = m_settings[i];
			const bool active = _active >> l & 1;
			bool filtered = false;
			if(active && s.filter.type != FilterType::Off)
			{
				const double envelope = m_envelope.step(l, s.filter.envelope, m_trig[i], m_gateOff[i]);
				filtered = envelopeDepth(l) != 64 << 7;
				if(filtered)
					setFilter(l, envelope);
			}
			if(active && m_moved[i])
				takeMoves(l, filtered);
			m_trig[i] = m_gateOff[i] = false;
		}
	}

	template<typename Stage>
	void Chain::runDrive(Stage& _stage, const StageLevel _level, const int _place, const int _type, Block& _buf, const uint16_t _lanes)
	{
		bool mixing = false;
		for(int l = 0; l < Lanes; ++l)
			mixing |= _lanes >> l & 1 && (m_mixNow[_place][l] != 1 || m_mix[_place][l] != 1);

		const auto wet = [&](Block& _block, const int _part, const int _parts)
		{
			if(!mixing)
			{
				pass(_stage, _block, _lanes, _level);
				return;
			}
			alignas(16) const Block dry = _block;
			pass(_stage, _block, _lanes, _level);
			blend(_block, dry, _lanes, m_mixNow[_place], m_mix[_place], _part, _parts);
		};

		if(!raised(_place >= Post0, _type))
		{
			wet(_buf, 0, 1);
			return;
		}

		// At the raised rate on both paths, so that the dry signal keeps the wet one's phase for MIX
		auto& rate = driveAt(_place).rates[static_cast<size_t>(_type)];
		const uint16_t groups = groupsOf(_lanes);
		alignas(16) Block up[4];
		rate.up(_buf, up, m_factor, groups);
		for(int k = 0; k < m_factor; ++k)
			wet(up[k], k, m_factor);
		if(groups == _lanes)
		{
			rate.down(up, _buf, m_factor, groups);
			return;
		}
		alignas(16) Block out = _buf;
		rate.down(up, out, m_factor, groups);
		pick(_buf, out, _lanes);
	}

	void Chain::run(const int _place, const int _type, Block& _buf, const uint16_t _lanes)
	{
		if(!_lanes)
			return;
		withStage(_place, _type, [&](auto& _stage, const StageLevel _level)
		{
			if(isDrive(_place))
				runDrive(_stage, _level, _place, _type, _buf, _lanes);
			else
				pass(_stage, _buf, _lanes, _level);
		});
	}

	void Chain::crossfade(const int _place, const uint16_t _fading, const uint16_t _active, Block& _buf)
	{
		const auto p = static_cast<size_t>(_place);

		// The stage a lane leaves and the one it takes each compute once, over the same input
		alignas(16) const Block input = _buf;
		alignas(16) Block old = _buf;
		for(int type = 1; type < Types; ++type)
		{
			uint16_t leaving = 0;
			for(int l = 0; l < Lanes; ++l)
				if(_fading >> l & 1 && m_old[p][static_cast<size_t>(l)] == type)
					leaving |= static_cast<uint16_t>(1 << l);
			const uint16_t taking = m_lanes[p][static_cast<size_t>(type)] & _active;
			if(!(taking | leaving))
				continue;
			alignas(16) Block work = input;
			run(_place, type, work, taking | leaving);
			pick(_buf, work, taking);
			pick(old, work, leaving);
		}
		for(int l = 0; l < Lanes; ++l)
		{
			if(!(_fading >> l & 1))
				continue;
			// A lane's new stage Off holds no lane of a type: its _buf is the input
			auto& done = m_fade[p][static_cast<size_t>(l)];
			for(int n = 0; n < BlockSize; ++n)
			{
				const float g = std::clamp(static_cast<float>(done + n + 1) / FadeSamples, 0.0f, 1.0f);
				_buf.s[n][l] = old.s[n][l] + g * (_buf.s[n][l] - old.s[n][l]);
			}
			done += BlockSize;
			if(done >= FadeSamples)
				endFade(_place, l);
		}
	}

	void Chain::process(Block& _buf, const uint16_t _active)
	{
		stepEnvelopes(_active);
		m_live = _active;

		// The Track's RST, which the Syntakt's BASE/WIDTH reads from where a Hit sounds
		for(int l = 0; m_reset && l < Lanes; ++l)
			if(m_reset >> l & 1)
				for(auto& band : m_bands)
					band.baseWidth.clear(l);
		m_reset = m_resetNext;
		m_resetNext = 0;

		for(int p = 0; p < Places; ++p)
		{
			const auto i = static_cast<size_t>(p);
			if(const uint16_t fading = m_fading[i] & _active)
			{
				crossfade(p, fading, _active, _buf);
			}
			else
			{
				for(int type = 1; type < Types; ++type)
					run(p, type, _buf, m_lanes[i][static_cast<size_t>(type)] & _active);
			}
			std::copy_n(m_mix[p], Lanes, m_mixNow[p]);
		}
		if(m_ampLanes & _active)
			m_amp.process(_buf, m_ampLanes & _active);
	}

	int Chain::pureDelay(const int _lane) const
	{
		return m_type[FilterPlace][static_cast<size_t>(_lane)] == static_cast<int>(FilterType::Machinedrum) ? MachinedrumFilterDelay : 0;
	}

	int Chain::delay(const int _lane) const
	{
		const auto l = static_cast<size_t>(_lane);
		int samples = pureDelay(_lane);
		for(const int p : {Pre0, Pre1, Post0, Post1})
			if(const int type = m_type[static_cast<size_t>(p)][l]; type && raised(p >= Post0, type))
				samples += Oversampler::delay(m_factor);
		return samples;
	}
}
