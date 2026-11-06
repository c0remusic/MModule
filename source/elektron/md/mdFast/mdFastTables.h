#pragma once

#include <array>
#include <cstdint>

// The tables of the Machinedrum's per-Track chain, made by formula rather than read from the firmware (spec 11.4), as
// OS 1.63's 24-bit words. mdFastTrackFxTest compares each with the OS's, word for word; the formulas are in the .cpp.
// An index below reads each as the DSP does: frequency + offset, the offset the table's base.
namespace md::fast::tables
{
	constexpr int CutoffCosBase = 682, CutoffExpBase = 428, EqCosBase = 541, EqExpBase = 601;

	struct Tables
	{
		std::array<int32_t, 2730> cutoffCos{};	// cos(w) of the filter's scale, 5 Hz to 20 kHz over 2048 steps
		std::array<int32_t, 2476> cutoffExp{};	// exp(-w), the same
		std::array<int32_t, 2589> eqCos{};		// cos(w) of the EQ's scale, 20 Hz to 20 kHz over 2048 steps
		std::array<int32_t, 2649> eqExp{};		// exp(-w), the same
		// A damping b as offsets on those scales: a pole exp(-w b +- j w sqrt(1 - b^2)) reads its radius at the
		// frequency + the Exp one and its angle's cosine at the frequency + the Cos one
		std::array<int16_t, 128> resonanceCos{}, resonanceExp{};	// by FLTQ
		std::array<int16_t, 128> eqPoleCos{}, eqPoleExp{}, eqZeroCos{}, eqZeroExp{};	// by EQG
		std::array<int32_t, 257> distGain{}, distFilter{};			// by DIST x 2

		// The scales' cosines and radii hold a frequency at _rate, the OS's own at 44.1 kHz
		explicit Tables(double _rate = 44100);
	};

	// Built at the first call, which a stage's constructor makes: nothing on the audio thread. 44.1 kHz, the OS's, or 48
	const Tables& tables(double _rate = 44100);
}
