#include "edDrumsRytmVoice.h"

#include <algorithm>
#include <cmath>

#include "edDrumsAnalog.h"

namespace elektron::drums::rytm
{
	namespace
	{
		using analog::be16;
		using analog::be32;
		using analog::setBe32;

		constexpr double Pi = 3.14159265358979323846;
		constexpr double Q31 = 2147483648.0;

		// Our unit's calibration, which the control reads as a real voice reads its own (edDrumsRytm.h): the pitch CV's
		// scale, so that the toms' range fills the CV word; the second CV's scale, so that it stays within its word
		constexpr int32_t PitchScale = 0x06000000;
		constexpr int32_t DampScale = 1 << 16;

		// The multiplier the routine puts between its exponential and the pitch word, as a fraction
		double pitchFactor(const Machine _m) { return _m == Machine::BigTom ? 0x3dbd70a / Q31 : 0x326f694 / Q31; }

		// The pitch word back from the pitch CV, through our calibration: CV = a^2 / 2^32, a = hz 256 scale
		double hzOf(const int _cv)
		{
			const double a = std::sqrt(static_cast<double>(std::max(_cv, 1)) * 4294967296.0);
			return a * Q31 / (256.0 * PitchScale);
		}
		// The pitch word (2^24 an octave) of a hz word, as the routine's exponential gives one
		double pitchOf(const double _hz, const Machine _m)
		{
			return (std::log2(_hz / pitchFactor(_m)) - 31.0) * 16777216.0 + 0x8000000;
		}

		// What the control's damping CV says once its share of the pitch is taken out: the decay knob's 256 + DEC << 8,
		// 256 at its shortest, 32 768 + 256 at its longest; a share at or above zero (a note held past its end) damps at once
		double dampingOf(const int16_t _cv, const double _pitchTerm)
		{
			const double share = _cv * 16.0 - _pitchTerm;
			return share >= -1.0 ? 1.0 : -(DampScale * 512.0) / share;
		}

		uint32_t next(uint32_t& _s)
		{
			_s ^= _s << 13;
			_s ^= _s >> 17;
			_s ^= _s << 5;
			return _s;
		}
		double white(uint32_t& _s) { return static_cast<int32_t>(next(_s)) / Q31; }
	}

	std::array<uint8_t, 8> Voice::defaultsOf(const Machine _m)
	{
		switch(_m)
		{
		case Machine::Clap:		return {64, 60, 2, 80, 85, 30, 90, 110};
		case Machine::BigTom:	return {72, 64, 0, 10, 64, 20, 0, 100};
		default:				return {100, 72, 40, 30, 10, 80, 64, 100};
		}
	}

	bool Voice::usesControl(const Machine _m, const int _index)
	{
		// the big tom's routine reads nothing of Machine 3 and 7
		if(_m == Machine::BigTom)
			return _index != 2 && _index != 6;
		return _index >= 0 && _index < 8;
	}

	void Voice::paramsOf(const Machine _m, const uint8_t* _controls, std::array<uint8_t, 20>& _params)
	{
		std::fill(_params.begin(), _params.end(), uint8_t(0));
		// word w from Machine (w + 7) % 8: TUNE first, LEVEL (word 0) last, as the analog family orders its controls
		for(int w = 0; w < 8; ++w)
		{
			uint8_t v = std::min<uint8_t>(_controls[(w + 7) % 8], 127);
			// the big tom's snap counts 0-3 periods
			if(_m == Machine::BigTom && w == 5)
				v = static_cast<uint8_t>(v >> 5);
			_params[static_cast<size_t>(2 * w)] = v;
		}
	}

	bool Voice::active() const
	{
		if(!m_sounding)
			return false;
		if(m_machine == Machine::Clap)
		{
			const auto phase = static_cast<int32_t>(be32(m_io.state.data()));
			return phase >= 0 || m_vca > 1e-6 || m_level > 1e-6;
		}
		return m_ticks < 4 || m_level > 1e-6 || m_noiseEnv > 1e-6;
	}

	void Voice::trig()
	{
		const bool changed = m_pending != m_machine || !m_sounding;
		m_machine = m_pending;
		if(changed)
		{
			m_io = VoiceIo{};
			setBe32(m_io.state.data(), 0xffffffffu);
			m_res[0] = m_res[1] = 0;
			m_lp[0] = m_lp[1] = m_bp[0] = m_bp[1] = 0;
			// the clap's random state: a seed of our own, which the claps' spread then runs from Hit to Hit
			setBe32(m_io.random.data(), 0x2f6b4c1du);
			setBe32(m_io.random.data() + 4, 0x13a7e5b9u);
		}
		m_io.voice = m_machine == Machine::Clap ? ClapVoice : m_machine == Machine::BigTom ? BigTomVoice : TomVoice;
		// our calibration in the voice's trims: (scale, offset) for the pitch, then the damping
		setBe32(m_io.trim.data(), PitchScale);
		setBe32(m_io.trim.data() + 4, 0);
		setBe32(m_io.trim.data() + 8, DampScale);
		setBe32(m_io.trim.data() + 12, 0);
		rytm::trig(m_io);
		m_noiseEnv = 0;
		m_noiseTarget = 0;
		m_vca = 0;
		m_phase = 0;
		m_snap = 1.0;
		m_sounding = true;
		m_ticks = 0;
		m_count = m_pos = 0;
	}

	void Voice::tick()
	{
		paramsOf(m_machine, m_controls.data(), m_io.params);
		m_io.pitch = m_note * 65536;
		if(m_machine == Machine::Clap)
		{
			// the noise the generator writes before the routine scales it: our own, white
			for(int i = 0; i < 32; ++i)
				setBe32(m_io.outA.data() + 4 * i, static_cast<uint32_t>(static_cast<int32_t>(next(m_seed)) >> 1));
		}
		control(m_machine, m_io);
	}

	void Voice::renderTom(float* _out, const int _n, const double _rate)
	{
		const bool big = m_machine == Machine::BigTom;
		const uint8_t* f = m_io.frame.data();
		const double hz = hzOf(be16(f));
		// the resonator's frequency: the hz word in Hz by our circuit's own constant, an octave up while the snap holds
		const double base = hz * (big ? 1.65e-4 : 2.45e-4);
		const bool snapping = big && !(f[4] & 0x08) && m_ticks > 0;
		const double p = pitchOf(hz, m_machine);
		// the damping CV less its pitch share (and the snap's octave): the knob's position, 256 to 33 024
		const double term = (big ? (p + (snapping ? 0x1000000 : 0) + 0x2aaaaa) : p - 0x200000) * 128.0 * DampScale / Q31;
		const double knob = dampingOf(static_cast<int16_t>(be16(f + 2)), term);
		const double tau = (big ? 0.3 : 0.18) * knob / 33024.0;

		const double target = snapping ? 1.0 : 0.0;
		const double fc = std::min(base * (1.0 + m_snap), 0.45 * _rate);
		const double w = 2.0 * std::sin(Pi * fc / _rate);
		// a Chamberlin resonator's ring falls by w damp / 2 a sample: the damping that rings for tau
		const double damp = std::min(2.0 / (w * std::max(tau, 1e-4) * _rate), 1.9);

		// the click the control sends, 32 values at 48 kHz, read across the tick at the host's rate
		const uint8_t* s = m_io.stream.data();
		const double noiseTarget = static_cast<int32_t>(be32(m_io.heads.data() + 4)) / Q31;
		const double noiseRatio = noiseTarget != 0 ? static_cast<int32_t>(be32(m_io.heads.data())) / Q31 / noiseTarget : 0;
		// the burst starts where the control sets its level, once a Hit
		if(noiseTarget != m_noiseTarget)
			m_noiseEnv = std::abs(noiseTarget);
		m_noiseTarget = noiseTarget;
		const double noiseStep = std::pow(std::max(noiseRatio, 0.0), 1.0 / _n);
		const uint8_t band = m_io.noise[0];
		const double lpc = 1.0 - std::exp(-2.0 * Pi * std::min(400.0 * std::exp2((band - 3) * 0.375), 0.45 * _rate) / _rate);
		double peak = 0;
		for(int i = 0; i < _n; ++i)
		{
			const int k = std::min(31, i * 32 / _n);
			const double click = static_cast<int32_t>(be32(s + 4 * k)) / 4194304.0;
			m_snap += (target - m_snap) * 0.02;
			// a state-variable resonator: band output, its damping from the decay
			const double hp = click * 0.5 - m_res[0] - damp * m_res[1];
			m_res[1] += w * hp;
			m_res[0] += w * m_res[1];
			const double ring = m_res[1];
			m_lp[0] += lpc * (white(m_seed) * m_noiseEnv - m_lp[0]);
			m_noiseEnv *= noiseStep;
			const double y = ring * 2.0 + m_lp[0] * 0.5;
			_out[i] += static_cast<float>(y);
			peak = std::max(peak, std::abs(ring) + std::abs(m_lp[0]));
		}
		m_level = peak;
	}

	void Voice::renderClap(float* _out, const int _n, const double _rate)
	{
		// a new clap or the tail: the VCA restarts at the level the heads send, falling by their ratio a tick
		const auto phase = static_cast<int32_t>(be32(m_io.state.data()));
		const double level = static_cast<int32_t>(be32(m_io.heads.data() + 4)) / Q31;
		const double fall = level != 0 ? static_cast<int32_t>(be32(m_io.heads.data())) / Q31 / level : 0;
		if(phase != m_phase && phase >= 2)
			m_vca = std::abs(level) * 4.0;
		m_phase = phase;
		const double step = std::pow(std::clamp(fall, 0.0, 1.0), 1.0 / _n);
		// the 808's clap bandpass, near 1 kHz, moved by the tone the control sends the generator
		const double fc = 700.0 * std::exp2((m_io.noise[0] - 64) / 64.0);
		const double w = 2.0 * std::sin(Pi * std::min(fc, 0.45 * _rate) / _rate);
		const double damp = 0.45;
		double peak = 0;
		for(int i = 0; i < _n; ++i)
		{
			const int k = std::min(31, i * 32 / _n);
			const double tail = static_cast<int32_t>(be32(m_io.outA.data() + 4 * k)) / Q31;
			const double x = white(m_seed) * m_vca + tail * 2.0;
			m_vca *= step;
			const double hp = x - m_bp[0] - damp * m_bp[1];
			m_bp[1] += w * hp;
			m_bp[0] += w * m_bp[1];
			_out[i] += static_cast<float>(m_bp[1] * 0.45);
			peak = std::max(peak, std::abs(m_bp[1]));
		}
		m_level = peak;
	}

	void Voice::renderTick(float* _out, const int _n, const double _rate)
	{
		if(m_machine == Machine::Clap)
			renderClap(_out, _n, _rate);
		else
			renderTom(_out, _n, _rate);
	}

	void Voice::render(float* _out, const int _n, const double _rate)
	{
		if(!m_sounding || _rate > MaxRate)
			return;
		const auto rate = static_cast<int64_t>(std::llround(_rate));
		for(int done = 0; done < _n;)
		{
			if(m_pos == m_count)
			{
				tick();
				const int64_t from = m_ticks * TickSamples * rate / 48000;
				const int64_t to = (m_ticks + 1) * TickSamples * rate / 48000;
				m_count = static_cast<int>(to - from);
				std::fill_n(m_buf.begin(), m_count, 0.0f);
				++m_ticks;
				renderTick(m_buf.data(), m_count, _rate);
				endBlock(m_io);
				m_pos = 0;
			}
			const int n = std::min(_n - done, m_count - m_pos);
			for(int i = 0; i < n; ++i)
				_out[done + i] += m_buf[static_cast<size_t>(m_pos + i)];
			m_pos += n;
			done += n;
		}
	}
}
