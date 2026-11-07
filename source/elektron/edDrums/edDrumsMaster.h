// The master on Main, in float: the Digitakt II's compressor, its +10 dB and clip, its overdrive and its volume, in the
// firmware's order. Laws fitted on the firmware's tables and code, about 40 numbers; against the firmware run in an
// emulator, a model of them errs by -100 dB or less. Main goes in at -10 dB, where a Digitakt II's Track sits under its
// +10 dB, so that a Main under the threshold leaves as it came; but it leaves clipped at full scale, as the firmware's.
// The settings take effect at the engine's blocks of 32, where their ramps start.
#pragma once

#include <array>
#include <atomic>
#include <cstdint>

#include "elektronBlock.h"

namespace elektron::drums
{
	// 0-127. Ratio 1.5, 2, 3, 4, 6, 8, 16, 20 to 1, the last at 127 alone; Filter, of the detector: low-pass under 64,
	// high-pass from it, each 20 Hz to 20 kHz
	enum MasterByte
	{
		MasterThreshold, MasterRatio, MasterAttack, MasterRelease, MasterMakeup, MasterMix, MasterFilter, MasterDrive,
		MasterVolume, MasterBytes
	};

	class Master
	{
	public:
		explicit Master(double _rate);

		// From the next block
		void set(const int _byte, const uint8_t _value)
		{
			m_bytes[static_cast<size_t>(_byte)] = _value < 127 ? _value : 127;
			m_changed = true;
		}

		// Nothing held, at the settings at once: for a master that comes on
		void settle();

		// At the engine's every block start, on or off, so that the ramps keep its blocks
		void block();
		// _count samples of Main in place, within the block
		void process(float* _left, float* _right, int _count);

		// The compressor's gain reduction at the last block's start, in dB, 0 or under; from any thread
		float reduction() const { return m_reduction.load(std::memory_order_relaxed); }

	private:
		struct Ramp
		{
			double from = 0, to = 0;
			double at(const int _n) const { return from + (to - from) * _n / elektron::BlockSize; }
			void next(const double _to) { from = to, to = _to; }
		};

		void startBlock();
		void sample(float& _left, float& _right, int _n);

		const double m_rate, m_mixPole;
		std::array<uint8_t, MasterBytes> m_bytes{};
		// The block's
		double m_threshold = 0, m_thresholdPeak = 1, m_ratio = 1, m_attack = 0, m_release = 0, m_wetTo = 1, m_dryTo = 0;
		// Over the block
		Ramp m_makeup, m_b0, m_b1, m_a1, m_satGain, m_satMix, m_volume;
		// Per sample
		double m_x1[2] = {}, m_y1[2] = {}, m_releaseDb = 0, m_attackDb = 0, m_wet = 1, m_dry = 0;
		int m_n = 0;			// in the block
		bool m_changed = true;	// a byte since the last block: its laws to work out again
		std::atomic<float> m_reduction{0};
	};
}
