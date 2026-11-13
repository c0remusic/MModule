#include "edDrumsParameters.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <numeric>

#include "edDrumsMaster.h"
#include "sophieMachine.h"

namespace elektron::drums::parameters
{
	namespace
	{
		constexpr const char* DriveTypes = "Off|Saturate|Distort|Downsample|DAC";
		constexpr const char* OutputNames = "Main|Out 01|Out 02|Out 03|Out 04|Out 05|Out 06|Out 07|Out 08|Out 09"
			"|Out 10|Out 11|Out 12|Out 13|Out 14|Out 15";
		constexpr const char* Targets = "None|Drive A|Drive B|EQ A|EQ B|Cutoff|Resonance|Width|Env Depth";

		// SY BITS starts in ADSR, its voice held: the engine's startsSustained, which it keeps to itself
		constexpr uint8_t SyBits = 7;

		int nearest(const double _value)
		{
			return static_cast<int>(std::lround(_value));
		}

		// ---- the texts of the values ----

		double significant(const double _x)
		{
			if(_x == 0 || !std::isfinite(_x))
				return _x;
			const double scale = std::pow(10.0, 2 - std::floor(std::log10(std::abs(_x))));
			return std::round(_x * scale) / scale;
		}

		// Three significant figures and the unit: "440 Hz", "12.5 ms", "0 ms"
		std::string withUnit(const double _x, const char* _unit)
		{
			const double r = significant(_x);
			const int decimals = r == 0 ? 0 : std::clamp(2 - static_cast<int>(std::floor(std::log10(std::abs(r)))), 0, 5);
			char text[40];
			std::snprintf(text, sizeof(text), "%.*f%s%s", decimals, r, *_unit ? " " : "", _unit);
			return text;
		}

		std::string seconds(const double _s)
		{
			if(!std::isfinite(_s))
				return "INF";
			return significant(_s * 1000) < 1000 ? withUnit(_s * 1000, "ms") : withUnit(_s, "s");
		}

		std::string hertz(const double _hz)
		{
			return significant(_hz) < 1000 ? withUnit(_hz, "Hz") : withUnit(_hz / 1000, "kHz");
		}

		std::string decibels(const double _db, const bool _signed = false)
		{
			if(!std::isfinite(_db))
				return "-inf dB";
			const double r = std::round(_db * 10) / 10;
			char text[40];
			std::snprintf(text, sizeof(text), "%s%.1f dB", _signed && r > 0 ? "+" : "", r == 0 ? 0.0 : r);
			return text;
		}

		// An amplitude's gain in dB
		std::string gain(const double _gain, const bool _signed = false)
		{
			return decibels(_gain > 0 ? 20 * std::log10(_gain) : -std::numeric_limits<double>::infinity(), _signed);
		}

		std::string signedNumber(const long _value)
		{
			return (_value > 0 ? "+" : "") + std::to_string(_value);
		}

		// A length in 128ths of a whole note, as the fraction it reduces to
		std::string noteLength(const int _128ths)
		{
			if(_128ths == 0)
				return "0";
			const int g = std::gcd(_128ths, 128);
			return std::to_string(_128ths / g) + "/" + std::to_string(128 / g);
		}

		// The level of a Track or a send, as Engine::gainOf and Engine::setSend take it: the square of its fraction
		std::string squared(const int _value)
		{
			const double g = _value / 127.0;
			return gain(g * g);
		}

		// syntakt::fast::Multimode: its cutoff word's index in 512 steps a 4400th of the way from 5 Hz (Tables::cosX);
		// FREQ's word as Chain::setFilter gives it, no envelope, no key tracking
		double syntaktCutoffHz(const int _value)
		{
			const int index = syntakt::fast::filterCutoff(_value << 8, 0x4000, 0, 48, 0) >> 22;
			return 5 * std::pow(4400.0, index / 512.0);
		}

		// md::fast::Filter: FLTF's word >> 3 on the 2048 steps from 5 Hz to 20 kHz of tables::cutoff
		double mdCutoffHz(const int _value)
		{
			return 5 * std::pow(4000.0, ((_value << 7) >> 3) / 2048.0);
		}

		// The edge the low-pass sits above FLTF's (Filter::set's lowEdge), in octaves of that scale
		double mdWidthOctaves(const int _value)
		{
			return ((_value << 7) >> 3) / 2048.0 * std::log2(4000.0);
		}

		// md::fast::Eq: EQF's word >> 3 on the 2048 steps from 20 Hz to 20 kHz of tables::eqFrequency
		double mdEqHz(const int _value)
		{
			return 20 * std::pow(1000.0, ((_value << 7) >> 3) / 2048.0);
		}

		// md::fast::Srr's period in steps of 16 a sample: the rate it holds samples at
		double srrHz(const int _value)
		{
			const double x = (_value << 7) + 0x4000;
			return 44100.0 * 16 / ((x * x / 8388608.0 - 0x20) * 4 + 0x10);
		}

		// md::fast::Dist's gain, tables::distGain at DIST x 2 after the DSP's shift of 9
		double mdDistGain(const int _value)
		{
			return 512 * std::pow(64.0, (2 * _value - 255) / 255.0);
		}

		// syntakt::fast::TrackDrive's gain into its clip: 0.1535 at 0, plus the word's square (its DriveFloor)
		double syntaktDriveGain(const int _value)
		{
			const double u = (_value << 8) / 32768.0;
			return 0.1535 + (1 - 0.1535) * u * u;
		}

		// syntakt::fast::FilterEnvelope's blocks of 32 at 48 kHz in seconds
		double blocks(const double _blocks)
		{
			return _blocks * elektron::BlockSize / syntakt::fast::MachineRate;
		}

		// A signed byte as the DP/4 shows it, -99 to 99 (Dp4Phaser)
		std::string dp4(const int _value)
		{
			return signedNumber(std::lround(dp4Signed(static_cast<uint8_t>(_value))));
		}

		double numberIn(const std::string& _text, bool& _found)
		{
			const char* start = _text.c_str();
			char* end = nullptr;
			double x = std::strtod(start, &end);
			_found = end != start;
			while(*end == ' ')
				++end;
			if(*end == 'k')
				x *= 1000;
			else if(end[0] == 'm' && end[1] == 's')
				x /= 1000;
			return x;
		}

		// A signed share of a range: an LFO's or the velocity's depth in values of its target's 0-127
		std::string share(const int _v)
		{
			return (_v > 0 ? "+" : "") + withUnit(100.0 * _v / 127, "%");
		}

		std::string semitones(const double _st)
		{
			char text[40];
			if(_st == std::floor(_st))
				std::snprintf(text, sizeof(text), "%s%d st", _st > 0 ? "+" : "", static_cast<int>(_st));
			else
				std::snprintf(text, sizeof(text), "%+.1f st", _st);
			return text;
		}

		std::string cents(const double _cents)
		{
			char text[40];
			const double r = std::round(_cents * 10) / 10;
			std::snprintf(text, sizeof(text), "%+.1f ct", r == 0 ? 0.0 : r);
			return text;
		}

		// HYPER FM's Machine _index + 1 in Sophie's units (sophie::machineValue, README.md's table), BODY off or on
		std::string sophieText(const int _index, const int _v)
		{
			if(_index == sophie::Body)
				return _v >= 64 ? "ON" : "OFF";
			if(_index >= sophie::MachineParamCount)
				return std::to_string(_v);
			static constexpr const char* Models[]{"FUSE", "STACK", "SPLIT", "SHARD", "PIPE", "BOOM"};
			static constexpr const char* Filters[]{"LPF", "HPF", "NOTCH", "DJ", "BPF"};
			const float x = sophie::machineValue(_index, static_cast<uint8_t>(_v));
			const int choice = static_cast<int>(x);
			switch(_index)
			{
			case SOPHIE_MODEL:			return Models[std::clamp(choice, 0, 5)];
			case SOPHIE_FILTER_TYPE:	return Filters[std::clamp(choice, 0, 4)];
			case SOPHIE_TUNE:			return semitones(x);
			case SOPHIE_SWEEP:			return (x > 0 ? "+" : "") + withUnit(x, "%");
			case SOPHIE_DECAY:			return seconds(x);
			case SOPHIE_RING_TIME:		return seconds(x / 1000);
			case SOPHIE_METAL_DECAY:	return x == 0 ? "OFF" : seconds(x / 1000);
			case SOPHIE_FEEDBACK_MODE:	return x != 0 ? "SMOOTH" : "RAW";
			case SOPHIE_TAIL_MODE:		return x != 0 ? "LIVE" : "HIT";
			// The voice's cutoff, 40 Hz x 400^(% / 100) (sophieVoice.c)
			case SOPHIE_CUTOFF:			return hertz(40 * std::pow(400.0, x / 100));
			default:					return withUnit(x, "%");
			}
		}

		// The value from _min to _max whose text (_textOf) is _text, the nearest to _own among those that show it; else
		// the one whose text's number is nearest to the number _text starts with
		int nearestValue(const std::string& _text, const int _min, const int _max, const int _own,
			const std::function<std::string(int)>& _textOf)
		{
			const auto first = _text.find_first_not_of(' '), last = _text.find_last_not_of(' ');
			const std::string text = first == std::string::npos ? std::string() : _text.substr(first, last - first + 1);
			int found = _min - 1;
			for(int v = _min; v <= _max; ++v)
				if(_textOf(v) == text && (found < _min || std::abs(v - _own) < std::abs(found - _own)))
					found = v;
			if(found >= _min)
				return found;

			bool number = false;
			const double wanted = numberIn(text, number);
			if(!number)
				return std::clamp(_own, _min, _max);
			double best = std::numeric_limits<double>::infinity();
			found = _own;
			for(int v = _min; v <= _max; ++v)
			{
				bool has = false;
				const double x = numberIn(_textOf(v), has);
				if(has && std::abs(x - wanted) < best)
				{
					best = std::abs(x - wanted);
					found = v;
				}
			}
			return std::clamp(found, _min, _max);
		}
	}

	std::string valueText(const std::string& _id, const int _v, const Other& _other)
	{
		const auto is = [&_id](const char* _s) { return _id == _s; };
		const auto starts = [&_id](const char* _s) { return _id.rfind(_s, 0) == 0; };
		const auto ends = [&_id](const std::string& _s)
		{
			return _id.size() >= _s.size() && _id.compare(_id.size() - _s.size(), _s.size(), _s) == 0;
		};
		const double inf = std::numeric_limits<double>::infinity();
		namespace st = syntakt::fast;

		// A Track's level and pan, its sends (Engine::setLevel, setPan, setSend)
		if(is("level") || starts("send_"))
			return squared(_v);
		if(is("pan"))
			return _v == 64 ? "C" : _v < 64 ? "L" + std::to_string(64 - _v) : "R" + std::to_string(_v - 64);
		if(is("note"))
			return noteName(_v);
		// An accented Hit plays at velocity 128 + 2 x ACCENT (Engine::noteOn): its gain over a Hit at 127
		if(is("accent"))
			return gain((128 + 2.0 * _v) / 127, true);
		// The LFOs add depth x their wave in values of the target (Lfos' offset, 128 a value's word), the velocity
		// depth x velocity / 127 (Chain): a share of the target's 0-127
		if(is("vel_depth") || is("lfo1_depth") || is("lfo2_depth"))
			return share(_v);

		// The chain (Chain::setDrive, setBand, setFilter, its TrackAmp and FilterEnvelope)
		if(starts("drv") && ends("_amount"))
		{
			switch(static_cast<DriveType>(_other((_id.substr(0, 5) + "type").c_str())))
			{
			case DriveType::Syntakt:	return gain(syntaktDriveGain(_v), true);
			case DriveType::MdDist:		return gain(mdDistGain(_v), true);
			case DriveType::MdSrr:		return hertz(srrHz(_v));
			case DriveType::Dac:
			{
				// Its bits and its clock (vintage::driveSetting) from the rate the Track's chain runs at
				const int rate = _other("chain_rate");
				const auto s = vintage::driveSetting(_v, rate > 0 ? rate : 44100);
				return std::to_string(s.bits) + " BIT " + hertz(s.clockHz);
			}
			default:					return std::to_string(_v);
			}
		}
		if(starts("drv") && ends("_mix"))
			return withUnit(100.0 * _v / 127, "%");
		if(starts("band_") && _id.size() == 6)
		{
			const int band = _id[5] - '1';
			switch(static_cast<BandType>(_other("band_type")))
			{
			case BandType::ThreeBand:	return band % 2 ? decibels(ThreeBand::db(_v), true) : hertz(ThreeBand::hz(_v));
			case BandType::MdEq:		return band == 0 ? hertz(mdEqHz(_v)) : std::to_string(_v);
			default:					return std::to_string(_v);
			}
		}
		const auto filter = [&] { return static_cast<FilterType>(_other("flt_type")); };
		if(is("flt_freq") && filter() == FilterType::Syntakt)
			return hertz(syntaktCutoffHz(_v));
		if(is("flt_freq") && filter() == FilterType::Machinedrum)
			return hertz(mdCutoffHz(_v));
		// RESO and FLTQ: a Q in equal ratios (Multimode::Tables, md::fast::tables' resonance offsets)
		if(is("flt_res") && filter() == FilterType::Syntakt)
			return "Q " + withUnit(0.501 * std::pow(std::sqrt(500.0) / 0.501, _v / 127.0), "");
		if(is("flt_res") && filter() == FilterType::Machinedrum)
			return "Q " + withUnit(0.501 * std::pow(2 * std::sqrt(2.0) / 0.501, _v / 127.0), "");
		if(is("flt_width") && filter() == FilterType::Machinedrum)
			return withUnit(mdWidthOctaves(_v), "oct");
		if(is("fenv_delay"))
			return seconds(blocks(st::envelopeDelayBlocks(_v)));
		if(is("fenv_attack"))
			return seconds(blocks(st::envelopeAttackBlocks(_v)));
		if(is("fenv_decay") || is("fenv_release"))
			return seconds(_v == 0 ? 0 : _v == 127 ? inf : blocks(st::envelopeDecayBlocks(_v)));
		if(is("fenv_sustain"))
			return withUnit(100.0 * _v / 127, "%");
		if(is("fenv_depth"))
			return signedNumber(_v - 64);
		if(is("amp_attack"))
			return seconds(_v == 0 ? 0 : st::ampAttackSamples(_v) / st::MachineRate);
		if(is("amp_hold") || is("amp_decay") || is("amp_release"))
			return seconds(_v == 127 ? inf : st::ampHoldSamples(_v) / st::MachineRate);
		if(is("amp_sustain"))
			return gain(_v / 128.0);

		// The Echo (md::fast::echoWords: TIME in 128ths of 4 beats, 127 a whole; FB doubled in the loop) and the Gate Box
		// (Y:$18C / 2 frames of pre-delay, DEC's row of reverberation times, mdFastGateBox.cpp)
		if(is("echo_time"))
			return noteLength(_v == 127 ? 128 : _v);
		if(is("echo_fb"))
			return withUnit(100.0 * _v / 64, "%");
		if(is("gate_pred"))
			return seconds(((_v << 7) & 0xfffe) / 2 / 44100.0);
		if(is("gate_dec"))
			return seconds(0.4 * std::pow(10.0, ((_v << 7) >> 8) / 63.0));

		// The Syntakt's (Delay::targetFrames: TIME + 1 in 128ths; its FDBK; Reverb::process's pre-delay at 48 kHz and
		// its shelf's GAIN at Nyquist)
		if(is("delay_time"))
			return noteLength(_v + 1);
		if(is("delay_fdbk"))
			return withUnit(100 * 66053.0 * std::min(_v << 8, 0x7f00) / 2147483648.0, "%");
		if(is("reverb_pre"))
		{
			const int64_t pre = _v << 8;
			return seconds(static_cast<double>(std::min<int64_t>((2 * pre * pre) >> 17, 0x4000 - 0x25) + 0x25)
				/ st::MachineRate);
		}
		if(is("reverb_gain"))
			return gain((_v << 8) / 32768.0);

		// Our chorus and phaser (edDrumsSendFx.cpp)
		if(is("chorus_rate"))
			return hertz(sendFxRateHz(static_cast<uint8_t>(_v)));
		if(is("chorus_depth"))
			return withUnit(100.0 * _v / 128, "%");
		if(is("chorus_delay"))
			return seconds((ModulatedDelay::ChorusStep * _v + 1) / 44100);
		if(is("chorus_fdbk"))
			return withUnit(100 * (_v - 64) / 65.6, "%");
		if(is("chorus_level") || is("phaser_level"))
			return gain(sendFxLevel(static_cast<uint8_t>(_v)));
		const auto mode = [&] { return _other("phaser_mode"); };
		if(is("phaser_rate"))
			return hertz(mode() == 2 ? dp4RateHz(static_cast<uint8_t>(_v)) : sendFxRateHz(static_cast<uint8_t>(_v)));
		if(is("phaser_freq"))
		{
			if(mode() == 0)
				return hertz(phaserHz(_v));
			if(mode() == 1)
				return seconds((ModulatedDelay::FlangerStep * _v + 1) / 44100);
			return dp4(_v);
		}
		if(is("phaser_spread") && mode() != 1)
			return mode() == 0 ? "Q " + withUnit(phaserQ(static_cast<uint8_t>(_v)), "") : dp4(_v);
		if(is("phaser_fdbk"))
		{
			if(mode() == 0)
				return withUnit(100 * 0.9375 * (_v - 64) / 64, "%");
			if(mode() == 1)
				return withUnit(100 * (_v - 64) / 65.6, "%");
			return dp4(_v);
		}
		if(is("phaser_dp4_left") || is("phaser_dp4_right"))
			return seconds(dp4DelayMs(static_cast<uint8_t>(_v)) / 1000);
		if(is("phaser_dp4_fdbk"))
			return dp4(_v);
		if(is("phaser_dp4_sh"))
			return _v == 0 ? "OFF" : hertz(dp4SampleHoldHz(static_cast<uint8_t>(_v)));

		// The master (edDrumsMaster.cpp). Makeup with the compressor's fixed x 16; the drive's mix is 0 at 0
		const auto byte = static_cast<uint8_t>(_v);
		if(is("master_threshold"))
			return decibels(masterThresholdDb(byte));
		if(is("master_ratio"))
			return withUnit(masterRatio(byte), "") + ":1";
		if(is("master_attack"))
			return seconds(masterAttackMs(byte) / 1000);
		if(is("master_release"))
			return seconds(masterReleaseMs(byte) / 1000);
		if(is("master_makeup"))
			return decibels(20 * std::log10(16.0) + masterMakeupDb(byte), true);
		if(is("master_filter"))
			return (_v < 64 ? "LP " : "HP ") + hertz(masterFilterHz(byte));
		if(is("master_drive"))
			return _v == 0 ? "OFF" : gain(masterDriveGain(byte));
		if(is("master_volume"))
			return squared(_v);

		return std::to_string(_v);
	}

	std::string samplerText(const int _index, const int _value)
	{
		switch(_index)
		{
		// The ROM player's grain by what it is, its 12-bit companded words; no machine's name
		case vintage::GrainSlot:		return vintage::isVintage(_value) ? "VINTAGE" : "12-BIT";
		case vintage::BitsSlot:			return std::to_string(vintage::bitsOf(_value)) + " BIT";
		case vintage::CompandingSlot:	return vintage::companding(_value) ? "ON" : "OFF";
		case vintage::FilterSlot:		return hertz(vintage::filterHz(_value));
		default:						return std::to_string(_value);
		}
	}

	int valueOf(const std::string& _id, const std::string& _text, const int _min, const int _max, const Other& _other)
	{
		return nearestValue(_text, _min, _max, _other(_id.c_str()),
			[&](const int _v) { return valueText(_id, _v, _other); });
	}

	std::string noteName(const int _note)
	{
		static constexpr const char* Names[]{"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
		const int n = std::clamp(_note, 0, 127);
		return Names[n % 12] + std::to_string(n / 12 - 1);
	}

	std::string rateText(const double _hz)
	{
		return _hz <= 0 ? "OFF" : hertz(_hz);
	}

	double rateOf(const std::string& _text)
	{
		bool found = false;
		const double x = numberIn(_text, found);
		return found ? x : 0;
	}

	std::string machineText(const Machine& _machine, const int _index, const int _value)
	{
		const int v = std::clamp(_value, 0, 127);
		switch(_machine.family)
		{
		case Family::Syntakt:
			// TUNE: each machine plays the note less its offset plus TUNE's word / 256 semitones (syntakt::fast::pitchQ16),
			// 64 the note's own; the fine part is the LFOs'
			return _index == 0 ? semitones(v - 64) : std::to_string(v);
		case Family::SyntaktAnalog:
			// The kicks' TUNE: word 1 at v << 8 (analog::paramsOf), which pitchWord weighs as the note's semitones
			return _index == 0 && _machine.id <= static_cast<uint8_t>(analog::Machine::KickAcoustic) ? semitones(v - 64)
				: std::to_string(v);
		case Family::OpenDrum:
		{
			// TUNE: the note the oscillator plays at the firmware's default note, which the engine triggers
			// (osc_recalcFreq); FINE: midiParser_calcDetune's factor, +-50 cents
			const char* name = machineParameterName(_machine, _index);
			if(name && std::strcmp(name, "TUNE") == 0)
				return noteName(v);
			if(name && std::strcmp(name, "FINE") == 0)
				return cents(1200 * std::log2(1 + (v / 127.0 - 0.5) * (std::pow(2.0, 1.0 / 12) - 1)));
			return std::to_string(v);
		}
		case Family::Sophie:
			return sophieText(_index, v);
		case Family::Machinedrum:
			return _machine == Sampler ? samplerText(_index, v) : std::to_string(v);
		case Family::Off:
			break;
		}
		return std::to_string(v);
	}

	int machineValueOf(const Machine& _machine, const int _index, const std::string& _text, const int _own)
	{
		return nearestValue(_text, 0, 127, _own, [&](const int _v) { return machineText(_machine, _index, _v); });
	}

	const std::array<Spec, ChainValues> chainSpecs{{
		{"drv1_type", "Drive 1 Type", 0, 4, DriveTypes},
		{"drv1_post", "Drive 1 Place", 0, 1, nullptr, true},
		{"drv1_amount", "Drive 1 Amount"},
		{"drv1_mix", "Drive 1 Mix"},
		{"drv2_type", "Drive 2 Type", 0, 4, DriveTypes},
		{"drv2_post", "Drive 2 Place", 0, 1, nullptr, true},
		{"drv2_amount", "Drive 2 Amount"},
		{"drv2_mix", "Drive 2 Mix"},
		{"band_type", "EQ Type", 0, 3, "Off|Cuts|Peak|3 Band"},
		{"band_after", "EQ Place", 0, 1, nullptr, true},
		{"band_1", "EQ 1"}, {"band_2", "EQ 2"}, {"band_3", "EQ 3"},
		{"band_4", "EQ 4"}, {"band_5", "EQ 5"}, {"band_6", "EQ 6"},
		{"flt_type", "Filter Type", 0, 2, "Off|Multimode|Window"},
		{"flt_mode", "Filter Mode", 0, 7, "LP|HP|EQ1|EQ2|EQ3|EQ4|EQ5|OFF"},
		{"flt_freq", "Filter Cutoff"},
		{"flt_res", "Filter Reso"},
		{"flt_width", "Filter Width"},
		{"fenv_delay", "Filter Delay"},
		{"fenv_attack", "Filter Attack"},
		{"fenv_decay", "Filter Decay"},
		{"fenv_sustain", "Filter Sustain"},
		{"fenv_release", "Filter Release"},
		{"fenv_reset", "Filter Retrig", 0, 1, nullptr, true},
		{"fenv_depth", "Filter Env Amt"},
		{"flt_key", "Filter Key", -128, 127},
		{"amp_on", "Amp Env", 0, 1, nullptr, true},
		{"amp_attack", "Amp Attack"},
		{"amp_hold", "Amp Hold"},
		{"amp_decay", "Amp Decay"},
		{"amp_sustain", "Amp Sustain"},
		{"amp_release", "Amp Release"},
		{"amp_mode", "Amp Mode", 0, 1, "AHD|ADSR"},
		{"vel_target", "Velocity Target", 0, 8, Targets},
		{"vel_depth", "Velocity Amt", -127, 127},
	}};

	const std::array<Spec, LfoValues> lfoSpecs{{
		{"sync", "Sync", 0, 1, nullptr, true},
		{"speed", "Note Rate", 0, LfoDivisions - 1, "1/64|1/32|1/16|1/8|1/4|1/2|1 Bar|2 Bars|4 Bars|8 Bars|16 Bars"},
		{"feel", "Feel", 0, 2, "Straight|Dotted|Triplet"},
		{"hz", "Free Rate"},
		{"shape", "Wave", 0, 6, "Triangle|Sine|Square|Saw|Exponential|Ramp|Random"},
		{"mode", "Trig Mode", 0, 4, "Free|Trig|Hold|One|Half"},
		{"depth", "Depth", -127, 127},
		{"track", "On Track", 0, Tracks, "Own|T01|T02|T03|T04|T05|T06|T07|T08|T09|T10|T11|T12|T13|T14|T15|T16"},
		{"target", "Target", 0, static_cast<int>(LfoTarget::Count) - 1, "None|Machine 1|Machine 2|Machine 3|Machine 4"
			"|Machine 5|Machine 6|Machine 7|Machine 8|Level|Pan|Drive A|Drive B|EQ A|EQ B|Cutoff|Resonance|Width"
			"|Env Depth|Machine 9|Machine 10|Machine 11|Machine 12|Machine 13|Machine 14|Machine 15|Machine 16|Machine 17"
			"|Machine 18|Machine 19|Machine 20|Machine 21|Machine 22|Machine 23|Machine 24"},
	}};

	const std::array<Spec, SendCount> sendSpecs{{
		{"send_delay", "Send Delay"}, {"send_reverb", "Send Reverb"}, {"send_chorus", "Send Chorus"},
		{"send_phaser", "Send Phaser"},
	}};
	const Spec accentSpec{"accent", "Accent"};

	const std::array<Spec, 2> sourceSpecs{{
		{"delay_source", "Delay Source", 0, 1, "Dub Echo|Stereo Dly"},
		{"reverb_source", "Reverb Source", 0, 1, "Gated Verb|Lush Verb"},
	}};
	const std::array<Spec, SendCount> sendOutputSpecs{{
		{"delay_output", "Delay Out", 0, 15, OutputNames}, {"reverb_output", "Reverb Out", 0, 15, OutputNames},
		{"chorus_output", "Chorus Out", 0, 15, OutputNames}, {"phaser_output", "Phaser Out", 0, 15, OutputNames},
	}};
	const std::array<Spec, 8> echoSpecs{{
		{"echo_time", "Echo Time"}, {"echo_mod", "Echo Mod Depth"}, {"echo_mfrq", "Echo Mod Rate"},
		{"echo_fb", "Echo Feedback"}, {"echo_fltf", "Echo HPF"}, {"echo_fltw", "Echo Bandwidth"},
		{"echo_mono", "Echo Mono"}, {"echo_lev", "Echo Level"},
	}};
	const std::array<Spec, 8> gateBoxSpecs{{
		{"gate_dvol", "Gated Dry Level"}, {"gate_pred", "Gated Pre-Delay"}, {"gate_dec", "Gated Decay"},
		{"gate_damp", "Gated Damping"}, {"gate_hp", "Gated HPF"}, {"gate_lp", "Gated LPF"},
		{"gate_gate", "Gated Gate"}, {"gate_lev", "Gated Level"},
	}};
	const std::array<Spec, StDelayBytes> syntaktDelaySpecs{{
		{"delay_time", "Delay Time"}, {"delay_cross", "Delay Ping Pong"}, {"delay_width", "Delay Width"},
		{"delay_fdbk", "Delay Feedback"}, {"delay_hp", "Delay HPF"}, {"delay_lp", "Delay LPF"},
	}};
	const std::array<Spec, StReverbBytes> syntaktReverbSpecs{{
		{"reverb_pre", "Reverb Pre-Delay"}, {"reverb_dec", "Reverb Decay"}, {"reverb_freq", "Reverb Freq"},
		{"reverb_gain", "Reverb Gain"}, {"reverb_hp", "Reverb HPF"}, {"reverb_lp", "Reverb LPF"},
	}};
	const Spec syntaktDelaySendSpec{"delay_rev", "Delay To Reverb"};
	const std::array<Spec, ChorusBytes> chorusSpecs{{
		{"chorus_rate", "Chorus Free Rate"}, {"chorus_depth", "Chorus Depth"}, {"chorus_delay", "Chorus Delay"},
		{"chorus_fdbk", "Chorus Feedback"}, {"chorus_level", "Chorus Level"},
		{"chorus_shape", "Chorus Wave", 0, 5, "Sine|Triangle|Saw|Square|S&H|Glide"},
	}};
	const std::array<Spec, PhaserBytes> phaserSpecs{{
		{"phaser_mode", "Phaser Type", 0, 2, "Phaser|Flanger|90s Rack"},
		{"phaser_stages", "Phaser Stages", 1, 6},
		{"phaser_rate", "Phaser Rate"}, {"phaser_depth", "Phaser Depth"}, {"phaser_freq", "Phaser Freq"},
		{"phaser_spread", "Phaser Spread"}, {"phaser_fdbk", "Phaser Feedback"}, {"phaser_level", "Phaser Level"},
		{"phaser_dp4_phase", "Rack Wet Phase", 0, 1, "In|Out"},
		{"phaser_dp4_left", "Rack Left Delay"}, {"phaser_dp4_right", "Rack Right Delay"},
		{"phaser_dp4_fdbk", "Rack Delay Fdbk"},
	}};
	const Spec masterOnSpec{"master_on", "Master On", 0, 1, nullptr, true};
	const std::array<Spec, MasterBytes> masterSpecs{{
		{"master_threshold", "Comp Threshold"}, {"master_ratio", "Comp Ratio"}, {"master_attack", "Comp Attack"},
		{"master_release", "Comp Release"}, {"master_makeup", "Comp Makeup"}, {"master_mix", "Comp Mix"},
		{"master_filter", "Comp SC Filter"}, {"master_drive", "Master Drive"}, {"master_volume", "Master Volume"},
	}};
	const Spec oversamplingSpec{"oversampling", "Oversampling", 0, 2, "1x|2x|4x"};

	const Spec delaySyncSpec{"delay_sync", "Delay Sync", 0, 1, nullptr, true};
	const Spec delayNoteSpec{"delay_note", "Delay Note Time", 0, 14,
		"1/32|1/16T|1/32D|1/16|1/8T|1/16D|1/8|1/4T|1/8D|1/4|1/2T|1/4D|1/2|1/2D|1/1"};
	const std::array<Spec, 2> lfoSyncSpecs{{
		{"chorus_sync", "Chorus Sync", 0, 1, nullptr, true}, {"phaser_sync", "Phaser Sync", 0, 1, nullptr, true},
	}};
	const std::array<Spec, 2> lfoNoteSpecs{{
		{"chorus_speed", "Chorus Note Rate", 0, 10, "1/64|1/32|1/16|1/8|1/4|1/2|1 Bar|2 Bars|4 Bars|8 Bars|16 Bars"},
		{"phaser_speed", "Phaser Note Rate", 0, 10, "1/64|1/32|1/16|1/8|1/4|1/2|1 Bar|2 Bars|4 Bars|8 Bars|16 Bars"},
	}};
	const Spec dp4SampleHoldSpec{"phaser_dp4_sh", "Rack S&H Rate"};
	// A new type is appended, so that a set keeps the one it chose
	const Spec chorusTypeSpec{"chorus_type", "Chorus Type", 0, 2, "Classic|Vibrato|Slow Sweep"};

	double delayNoteQuarters(const int _choice)
	{
		// A whole note is 4 beats; T two thirds, D three halves
		static constexpr double Beats[] = {0.125, 1.0 / 6, 0.1875, 0.25, 1.0 / 3, 0.375, 0.5, 2.0 / 3, 0.75, 1, 4.0 / 3,
			1.5, 2, 3, 4};
		return Beats[std::clamp(_choice, 0, 14)];
	}

	double lfoNoteQuarters(const int _choice)
	{
		// 1/64 a sixteenth of a beat, doubling to 16 bars of 4
		return std::ldexp(1.0 / 16, std::clamp(_choice, 0, 10));
	}

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
		if(_machine.family == Family::Machinedrum || _machine.family == Family::OpenDrum
			|| _machine.family == Family::Sophie || isAnalog(_machine.family))
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
