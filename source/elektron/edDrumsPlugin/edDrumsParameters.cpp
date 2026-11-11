#include "edDrumsParameters.h"

#include <cmath>

namespace elektron::drums::parameters
{
	namespace
	{
		constexpr const char* DriveTypes = "Off|Syntakt|MD DIST|MD SRR";
		constexpr const char* OutputNames = "Main|Out 01|Out 02|Out 03|Out 04|Out 05|Out 06|Out 07|Out 08|Out 09"
			"|Out 10|Out 11|Out 12|Out 13|Out 14|Out 15";
		constexpr const char* Targets = "None|Drive A|Drive B|Band A|Band B|Frequency|Resonance|Width|Env Depth";

		// SY BITS starts in ADSR, its voice held: the engine's startsSustained, which it keeps to itself
		constexpr uint8_t SyBits = 7;

		int nearest(const double _value)
		{
			return static_cast<int>(std::lround(_value));
		}
	}

	const std::array<Spec, ChainValues> chainSpecs{{
		{"drv1_type", "Drive 1 Type", 0, 3, DriveTypes},
		{"drv1_post", "Drive 1 Post", 0, 1, nullptr, true},
		{"drv1_amount", "Drive 1 Amount"},
		{"drv1_mix", "Drive 1 Mix"},
		{"drv2_type", "Drive 2 Type", 0, 3, DriveTypes},
		{"drv2_post", "Drive 2 Post", 0, 1, nullptr, true},
		{"drv2_amount", "Drive 2 Amount"},
		{"drv2_mix", "Drive 2 Mix"},
		{"band_type", "Band Type", 0, 3, "Off|Base-Width|MD EQ|3 Band"},
		{"band_after", "Band After Filter", 0, 1, nullptr, true},
		{"band_1", "Band 1"}, {"band_2", "Band 2"}, {"band_3", "Band 3"},
		{"band_4", "Band 4"}, {"band_5", "Band 5"}, {"band_6", "Band 6"},
		{"flt_type", "Filter Type", 0, 2, "Off|Syntakt|Machinedrum"},
		{"flt_mode", "Filter Mode", 0, 7, "LP|HP|EQ1|EQ2|EQ3|EQ4|EQ5|OFF"},
		{"flt_freq", "Filter Freq"},
		{"flt_res", "Filter Res"},
		{"flt_width", "Filter Width"},
		{"fenv_delay", "Filter Env Delay"},
		{"fenv_attack", "Filter Env Attack"},
		{"fenv_decay", "Filter Env Decay"},
		{"fenv_sustain", "Filter Env Sustain"},
		{"fenv_release", "Filter Env Release"},
		{"fenv_reset", "Filter Env Reset", 0, 1, nullptr, true},
		{"fenv_depth", "Filter Env Depth"},
		{"flt_key", "Filter Key Track", -128, 127},
		{"amp_on", "Amp On", 0, 1, nullptr, true},
		{"amp_attack", "Amp Attack"},
		{"amp_hold", "Amp Hold"},
		{"amp_decay", "Amp Decay"},
		{"amp_sustain", "Amp Sustain"},
		{"amp_release", "Amp Release"},
		{"amp_mode", "Amp Mode", 0, 1, "AHD|ADSR"},
		{"vel_target", "Vel Target", 0, 8, Targets},
		{"vel_depth", "Vel Depth", -127, 127},
	}};

	const std::array<Spec, LfoValues> lfoSpecs{{
		{"sync", "Sync", 0, 1, nullptr, true},
		{"speed", "Speed", 0, LfoDivisions - 1, "1/64|1/32|1/16|1/8|1/4|1/2|1 Bar|2 Bars|4 Bars|8 Bars|16 Bars"},
		{"feel", "Feel", 0, 2, "Straight|Dotted|Triplet"},
		{"hz", "Hz"},
		{"shape", "Shape", 0, 6, "Triangle|Sine|Square|Saw|Exponential|Ramp|Random"},
		{"mode", "Mode", 0, 4, "Free|Trig|Hold|One|Half"},
		{"depth", "Depth", -127, 127},
		{"track", "Track", 0, Tracks, "Own|T01|T02|T03|T04|T05|T06|T07|T08|T09|T10|T11|T12|T13|T14|T15|T16"},
		{"target", "Target", 0, static_cast<int>(LfoTarget::Count) - 1, "None|Machine 1|Machine 2|Machine 3|Machine 4"
			"|Machine 5|Machine 6|Machine 7|Machine 8|Level|Pan|Drive A|Drive B|Band A|Band B|Frequency|Resonance|Width"
			"|Env Depth"},
	}};

	const std::array<Spec, SendCount> sendSpecs{{
		{"send_delay", "Delay Send"}, {"send_reverb", "Reverb Send"}, {"send_chorus", "Chorus Send"},
		{"send_phaser", "Phaser Send"},
	}};
	const Spec accentSpec{"accent", "Accent"};

	const std::array<Spec, 2> sourceSpecs{{
		{"delay_source", "Delay Source", 0, 1, "MD Echo|Syntakt"},
		{"reverb_source", "Reverb Source", 0, 1, "MD Gate Box|Syntakt"},
	}};
	const std::array<Spec, SendCount> sendOutputSpecs{{
		{"delay_output", "Delay Output", 0, 15, OutputNames}, {"reverb_output", "Reverb Output", 0, 15, OutputNames},
		{"chorus_output", "Chorus Output", 0, 15, OutputNames}, {"phaser_output", "Phaser Output", 0, 15, OutputNames},
	}};
	const std::array<Spec, 8> echoSpecs{{
		{"echo_time", "Echo Time"}, {"echo_mod", "Echo Mod"}, {"echo_mfrq", "Echo Mod Freq"},
		{"echo_fb", "Echo Feedback"}, {"echo_fltf", "Echo Filter Freq"}, {"echo_fltw", "Echo Filter Width"},
		{"echo_mono", "Echo Mono"}, {"echo_lev", "Echo Level"},
	}};
	const std::array<Spec, 8> gateBoxSpecs{{
		{"gate_dvol", "Gate Box Dry Vol"}, {"gate_pred", "Gate Box Pre-Delay"}, {"gate_dec", "Gate Box Decay"},
		{"gate_damp", "Gate Box Damp"}, {"gate_hp", "Gate Box High Pass"}, {"gate_lp", "Gate Box Low Pass"},
		{"gate_gate", "Gate Box Gate"}, {"gate_lev", "Gate Box Level"},
	}};
	const std::array<Spec, StDelayBytes> syntaktDelaySpecs{{
		{"delay_time", "Delay Time"}, {"delay_cross", "Delay Ping Pong"}, {"delay_width", "Delay Width"},
		{"delay_fdbk", "Delay Feedback"}, {"delay_hp", "Delay High Pass"}, {"delay_lp", "Delay Low Pass"},
	}};
	const std::array<Spec, StReverbBytes> syntaktReverbSpecs{{
		{"reverb_pre", "Reverb Pre-Delay"}, {"reverb_dec", "Reverb Decay"}, {"reverb_freq", "Reverb Shelf Freq"},
		{"reverb_gain", "Reverb Shelf Gain"}, {"reverb_hp", "Reverb High Pass"}, {"reverb_lp", "Reverb Low Pass"},
	}};
	const Spec syntaktDelaySendSpec{"delay_rev", "Delay Reverb Send"};
	const std::array<Spec, ChorusBytes> chorusSpecs{{
		{"chorus_rate", "Chorus Rate"}, {"chorus_depth", "Chorus Depth"}, {"chorus_delay", "Chorus Delay"},
		{"chorus_fdbk", "Chorus Feedback"}, {"chorus_level", "Chorus Level"},
		{"chorus_shape", "Chorus Shape", 0, 5, "Sine|Triangle|Saw|Square|S&H|Glide"},
	}};
	const std::array<Spec, PhaserBytes> phaserSpecs{{
		{"phaser_mode", "Phaser Mode", 0, 2, "Phaser|Flanger|DP/4"},
		{"phaser_stages", "Phaser Stages", 1, 6},
		{"phaser_rate", "Phaser Rate"}, {"phaser_depth", "Phaser Depth"}, {"phaser_freq", "Phaser Freq"},
		{"phaser_spread", "Phaser Spread"}, {"phaser_fdbk", "Phaser Feedback"}, {"phaser_level", "Phaser Level"},
		{"phaser_dp4_phase", "Phaser DP4 Phase", 0, 1, "In|Out"},
		{"phaser_dp4_left", "Phaser DP4 Delay L"}, {"phaser_dp4_right", "Phaser DP4 Delay R"},
		{"phaser_dp4_fdbk", "Phaser DP4 Delay Fdbk"},
	}};
	const Spec masterOnSpec{"master_on", "Master On", 0, 1, nullptr, true};
	const std::array<Spec, MasterBytes> masterSpecs{{
		{"master_threshold", "Master Threshold"}, {"master_ratio", "Master Ratio"}, {"master_attack", "Master Attack"},
		{"master_release", "Master Release"}, {"master_makeup", "Master Makeup"}, {"master_mix", "Master Mix"},
		{"master_filter", "Master Filter"}, {"master_drive", "Master Drive"}, {"master_volume", "Master Volume"},
	}};
	const Spec oversamplingSpec{"oversampling", "Oversampling", 0, 2, "1x|2x|4x"};

	// As the engine tests set them to hear each effect: the delays at TIME 32, the reverbs at half decay, the filters
	// open, the levels up, nothing fed back (64 is none for our chorus's and phaser's feedback)
	const std::array<int, 8> echoDefaults{32, 0, 0, 0, 0, 127, 0, 127};
	const std::array<int, 8> gateBoxDefaults{0, 0, 64, 64, 0, 127, 127, 127};
	const std::array<int, StDelayBytes> syntaktDelayDefaults{32, 0, 64, 0, 0, 127};
	const std::array<int, StReverbBytes> syntaktReverbDefaults{0, 64, 64, 0, 0, 127};
	const std::array<int, ChorusBytes> chorusDefaults{64, 64, 64, 64, 127, 1};
	const std::array<int, PhaserBytes> phaserDefaults{0, 6, 64, 64, 64, 64, 64, 127, 1, 0, 0, 64};
	const std::array<int, MasterBytes> masterDefaults{127, 0, 0, 64, 0, 127, 64, 0, 127};

	ChainSettings familyChain(const Machine& _machine)
	{
		if(_machine.family == Family::Syntakt)
			return syntaktChain(_machine.id == SyBits);
		if(_machine.family == Family::Machinedrum)
			return machinedrumChain();
		return {};
	}

	ChainArray valuesOf(const ChainSettings& _s)
	{
		ChainArray v{};
		for(size_t d = 0; d < 2; ++d)
		{
			const auto& drive = _s.drive[d];
			const size_t at = Drive1Type + 4 * d;
			v[at] = static_cast<int>(drive.type);
			v[at + 1] = drive.post;
			v[at + 2] = drive.amount;
			v[at + 3] = drive.mix;
		}
		v[BandKind] = static_cast<int>(_s.band.type);
		v[BandAfter] = _s.band.after;
		for(size_t b = 0; b < 6; ++b)
			v[Band1 + b] = _s.band.values[b];
		v[FilterKind] = static_cast<int>(_s.filter.type);
		v[FilterMode] = _s.filter.mode;
		v[FilterFrequency] = _s.filter.frequency;
		v[FilterResonance] = _s.filter.resonance;
		v[FilterWidth] = _s.filter.width;
		v[EnvelopeDelay] = _s.filter.envelope.delay;
		v[EnvelopeAttack] = _s.filter.envelope.attack;
		v[EnvelopeDecay] = _s.filter.envelope.decay;
		v[EnvelopeSustain] = _s.filter.envelope.sustain;
		v[EnvelopeRelease] = _s.filter.envelope.release;
		v[EnvelopeReset] = _s.filter.envelope.reset;
		v[EnvelopeDepth] = _s.filter.depth;
		v[KeyTrack] = _s.filter.keyTrack;
		v[AmpOn] = _s.amp.on;
		v[AmpAttack] = _s.amp.attack;
		v[AmpHold] = _s.amp.hold;
		v[AmpDecay] = _s.amp.decay;
		v[AmpSustain] = _s.amp.sustain;
		v[AmpRelease] = _s.amp.release;
		v[AmpAdsr] = _s.amp.adsr;
		v[VelTarget] = static_cast<int>(_s.velocityTarget);
		v[VelocityDepth] = _s.velocityDepth;
		return v;
	}

	ChainSettings chainOf(const ChainArray& _v)
	{
		ChainSettings s;
		for(size_t d = 0; d < 2; ++d)
		{
			auto& drive = s.drive[d];
			const size_t at = Drive1Type + 4 * d;
			drive.type = static_cast<DriveType>(_v[at]);
			drive.post = _v[at + 1] != 0;
			drive.amount = _v[at + 2];
			drive.mix = _v[at + 3];
		}
		s.band.type = static_cast<elektron::drums::BandType>(_v[BandKind]);
		s.band.after = _v[BandAfter] != 0;
		for(size_t b = 0; b < 6; ++b)
			s.band.values[b] = _v[Band1 + b];
		s.filter.type = static_cast<elektron::drums::FilterType>(_v[FilterKind]);
		s.filter.mode = _v[FilterMode];
		s.filter.frequency = _v[FilterFrequency];
		s.filter.resonance = _v[FilterResonance];
		s.filter.width = _v[FilterWidth];
		s.filter.envelope.delay = _v[EnvelopeDelay];
		s.filter.envelope.attack = _v[EnvelopeAttack];
		s.filter.envelope.decay = _v[EnvelopeDecay];
		s.filter.envelope.sustain = _v[EnvelopeSustain];
		s.filter.envelope.release = _v[EnvelopeRelease];
		s.filter.envelope.reset = _v[EnvelopeReset] != 0;
		s.filter.depth = _v[EnvelopeDepth];
		s.filter.keyTrack = _v[KeyTrack];
		s.amp.on = _v[AmpOn] != 0;
		s.amp.attack = _v[AmpAttack];
		s.amp.hold = _v[AmpHold];
		s.amp.decay = _v[AmpDecay];
		s.amp.sustain = _v[AmpSustain];
		s.amp.release = _v[AmpRelease];
		s.amp.adsr = _v[AmpAdsr] != 0;
		s.velocityTarget = static_cast<VelocityTarget>(_v[VelTarget]);
		s.velocityDepth = _v[VelocityDepth];
		return s;
	}

	LfoArray valuesOf(const LfoSettings& _s)
	{
		LfoArray v{};
		v[LfoSync] = _s.rate.sync;
		v[LfoSpeed] = _s.rate.division;
		v[LfoFeelValue] = static_cast<int>(_s.rate.feel);
		v[LfoHz] = _s.rate.hz;
		v[LfoShapeValue] = static_cast<int>(_s.shape);
		v[LfoModeValue] = static_cast<int>(_s.mode);
		v[LfoDepth] = _s.depth;
		v[LfoTrack] = _s.track + 1;
		v[LfoTargetValue] = static_cast<int>(_s.target);
		return v;
	}

	LfoSettings lfoOf(const LfoArray& _v)
	{
		LfoSettings s;
		s.rate.sync = _v[LfoSync] != 0;
		s.rate.division = nearest(_v[LfoSpeed]);
		s.rate.feel = static_cast<LfoFeel>(nearest(_v[LfoFeelValue]));
		s.rate.hz = _v[LfoHz];
		s.shape = static_cast<LfoShape>(nearest(_v[LfoShapeValue]));
		s.mode = static_cast<LfoMode>(nearest(_v[LfoModeValue]));
		s.depth = nearest(_v[LfoDepth]);
		s.track = nearest(_v[LfoTrack]) - 1;
		s.target = static_cast<elektron::drums::LfoTarget>(nearest(_v[LfoTargetValue]));
		return s;
	}
}
