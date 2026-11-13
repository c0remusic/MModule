#pragma once

#include <array>
#include <cstdint>

#include "edDrumsRytm.h"

// The Rytm's own analog voices' circuits (ticket 28, stage 2): models of our own, written here from classic circuits
// (.scratch/elektron-drums/research/28-circuits-classiques.md), that play what the ported control (edDrumsRytm.h)
// computes. Nothing of the original's sound is here; its control is.
//
//   BIG TOM  a pinged resonator, the 808's bridged T: the control's click rings it, its pitch CV tunes it, its second CV
//            sets its damping, its snap switch lifts it an octave for the first periods (the 808's attack), a short
//            burst of noise beside it
//   TOM      the same resonator without the snap, a pitch sweep the control runs, a longer noise burst through a
//            lowpass the control's band setting tunes
//   CLAP     two noises of our own: one through the voice's VCA, retriggered at each clap the control counts out, the
//            other the tail, which the control scales block by block; both through the 808's clap bandpass
//
// As analog::Voice: the control every 32 samples of 48 kHz, the circuit at the host's rate.
namespace elektron::drums::rytm
{
	class Voice
	{
	public:
		static constexpr int TickSamples = 32;
		static constexpr int MaxRate = 192000;

		void setMachine(Machine _m) { m_pending = _m; }
		Machine machine() const { return m_machine; }
		void setControl(const int _index, const uint8_t _value) { m_controls[static_cast<size_t>(_index & 7)] = _value; }
		void setControls(const uint8_t* _controls8)
		{
			for(size_t i = 0; i < m_controls.size(); ++i)
				m_controls[i] = _controls8[i];
		}
		void setNote(const int _note) { m_note = _note; }
		// A Hit started the voice and an envelope is still above zero
		bool active() const;
		void trig();
		// _n host samples at _rate added to _out
		void render(float* _out, int _n, double _rate);

		// SYN1-8 of _m as a machine change gives them: our own defaults, chosen by ear
		static std::array<uint8_t, 8> defaultsOf(Machine _m);
		// Whether _m's routine reads Machine _index + 1 (0-7)
		static bool usesControl(Machine _m, int _index);
		// The machine's 20 parameter bytes from its Machine 1-8 (0-127)
		static void paramsOf(Machine _m, const uint8_t* _controls, std::array<uint8_t, 20>& _params);

	private:
		void tick();
		void renderTick(float* _out, int _n, double _rate);
		void renderTom(float* _out, int _n, double _rate);
		void renderClap(float* _out, int _n, double _rate);

		Machine m_machine = Machine::Tom;
		Machine m_pending = Machine::Tom;
		std::array<uint8_t, 8> m_controls{};
		int m_note = 60;
		VoiceIo m_io;
		bool m_sounding = false;
		int64_t m_ticks = 0;
		std::array<float, MaxRate * TickSamples / 48000 + 8> m_buf{};
		int m_count = 0, m_pos = 0;

		// the circuit's state
		double m_res[2] = {};		// the resonator's two integrators
		double m_snap = 0;			// the snap's octave, smoothed (0-1)
		double m_noiseEnv = 0;		// the noise burst's level
		double m_noiseTarget = 0;	// the level the control last sent it
		double m_lp[2] = {};		// the noise's lowpass, the clap's bandpass
		double m_bp[2] = {};
		double m_vca = 0;			// the clap's VCA envelope
		int32_t m_phase = 0;		// the clap's phase as the control last left it
		uint32_t m_seed = 0x2545f491;
		double m_level = 0;			// the output's peak of the last tick, for active()
	};
}
