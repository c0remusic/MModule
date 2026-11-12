// Sophie's voice against the original's (SOPHIE_ORIGINAL_DIR at configure time, else exit 77 after the port's own
// checks), its cost, its start, its decay to silence and the machine page's defaults

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

#include "sophieMachine.h"
#include "sophieVoice.h"

#ifdef SOPHIE_HAS_ORIGINAL
extern "C"
{
	void* sophieOriginalCreate();
	void sophieOriginalDestroy(void* _instance);
	void sophieOriginalSet(void* _instance, const char* _key, const char* _value);
	void sophieOriginalNote(void* _instance, int _note, int _velocity);
	void sophieOriginalRender(void* _instance, float* _out, int _frames);
	int sophieOriginalActiveVoices(void* _instance);
}
#endif

namespace
{
	constexpr int SampleRate = 44100;
	constexpr int Block = 32;
	constexpr int MaxBlocks = 5 * SampleRate / Block;
	constexpr const char* ModelNames[6] = {"Fuse", "Stack", "Split", "Shard", "Pipe", "Boom"};

	int g_failed = 0;

	void check(const bool _condition, const char* _what)
	{
		if (_condition)
			return;
		++g_failed;
		std::printf("FAIL: %s\n", _what);
	}

	using Values = std::array<float, SOPHIE_PARAM_COUNT>;

	struct Setting
	{
		sophie_param_t param;
		float value;
	};

	struct Scenario
	{
		const char* name;
		int pad;
		int velocity;
		std::vector<Setting> settings;	// over the pad's factory patch, after Model
		std::vector<Setting> changeAt40;	// set at block 40, while the hit rings
	};

	std::vector<Scenario> scenarios()
	{
		return {
			{"pad 1 factory", 0, 127, {}, {}},
			{"pad 2 factory", 1, 100, {}, {}},
			{"heavy", 4, 127, {{SOPHIE_METAL, 100}, {SOPHIE_FEEDBACK, 100}, {SOPHIE_DECAY, 1.0f}, {SOPHIE_CRUSH, 60},
				{SOPHIE_DRIVE, 90}, {SOPHIE_CUTOFF, 70}, {SOPHIE_RESONANCE, 80}, {SOPHIE_RING_TIME, 3},
				{SOPHIE_RING_FEEDBACK, 80}, {SOPHIE_RING_MIX, 50}, {SOPHIE_RING_TONE, 40}, {SOPHIE_METAL_DECAY, 300},
				{SOPHIE_FEEDBACK_MODE, 1}}, {}},
			{"live tail", 2, 64, {{SOPHIE_TAIL_MODE, 1}, {SOPHIE_DECAY, 1.5f}, {SOPHIE_FILTER_TYPE, 3},
				{SOPHIE_CUTOFF, 30}, {SOPHIE_RING_MIX, 30}, {SOPHIE_RING_FEEDBACK, 60}},
				{{SOPHIE_COLOR, 90}, {SOPHIE_METAL, 90}, {SOPHIE_FEEDBACK, 90}, {SOPHIE_CUTOFF, 90},
				{SOPHIE_RESONANCE, 60}, {SOPHIE_DRIVE, 10}}},
			{"smooth kick", 0, 127, {{SOPHIE_FEEDBACK_MODE, 1}, {SOPHIE_METAL, 60}, {SOPHIE_METAL_DECAY, 200},
				{SOPHIE_SWEEP, -40}, {SOPHIE_TUNE, 12}, {SOPHIE_FILTER_TYPE, 1}, {SOPHIE_CUTOFF, 20}}, {}},
		};
	}

	void setup(sophie_t& _s, const int _model, const Scenario& _scenario)
	{
		sophie_set_pad(&_s, _scenario.pad);
		_s.patch = sophie_factory_patch(_scenario.pad);
		sophie_set_param(&_s, SOPHIE_MODEL, static_cast<float>(_model));
		for (const auto& setting : _scenario.settings)
			sophie_set_param(&_s, setting.param, setting.value);
	}

	// Renders one hit until silence, or 5 s; returns the samples
	std::vector<float> renderHit(sophie_t& _s, const int _velocity, const std::vector<Setting>& _changeAt40 = {})
	{
		std::vector<float> out;
		sophie_trigger(&_s, _velocity);
		float block[Block];
		for (int b = 0; b < MaxBlocks && sophie_active(&_s); ++b)
		{
			if (b == 40)
				for (const auto& setting : _changeAt40)
					sophie_set_param(&_s, setting.param, setting.value);
			sophie_render(&_s, block, Block);
			out.insert(out.end(), block, block + Block);
		}
		return out;
	}

	float peakOf(const std::vector<float>& _samples)
	{
		float peak = 0.0f;
		for (const float x : _samples)
			peak = std::max(peak, std::fabs(x));
		return peak;
	}

#ifdef SOPHIE_HAS_ORIGINAL
	constexpr const char* Keys[SOPHIE_PARAM_COUNT] = {"model", "tune", "color", "metal", "feedback", "sweep", "decay",
		"crush", "drive", "cutoff", "resonance", "filter_type", "ring_time", "ring_feedback", "ring_mix", "ring_tone",
		"metal_decay", "feedback_mode", "tail_mode", "level"};

	void setOriginal(void* _original, const int _pad, const sophie_param_t _param, const float _value)
	{
		char key[40];
		char value[40];
		std::snprintf(key, sizeof(key), "p%02d_%s", _pad + 1, Keys[_param]);
		std::snprintf(value, sizeof(value), "%.9g", static_cast<double>(_value));
		sophieOriginalSet(_original, key, value);
	}

	// Every parameter of the port's patch to the original's pad
	void copyPatch(void* _original, const sophie_t& _s)
	{
		for (int p = 0; p < SOPHIE_PARAM_COUNT; ++p)
		{
			const auto param = static_cast<sophie_param_t>(p);
			setOriginal(_original, _s.pad, param, sophie_get_param(&_s, param));
		}
	}

	// The same hits through the port and the original, from fresh instances, scenario after scenario on each (the
	// random generator runs on from hit to hit in both)
	bool compareWithOriginal()
	{
		std::printf("Port against the original, 44.1 kHz, blocks of %d (the original's own render is per sample):\n",
			Block);
		bool allExact = true;
		for (int model = 0; model < 6; ++model)
		{
			sophie_t s;
			sophie_init(&s, SampleRate, 0);
			void* original = sophieOriginalCreate();

			for (const auto& scenario : scenarios())
			{
				setup(s, model, scenario);
				copyPatch(original, s);
				sophie_trigger(&s, scenario.velocity);
				sophieOriginalNote(original, SOPHIE_FIRST_NOTE + scenario.pad, scenario.velocity);

				float a[Block];
				float b[Block];
				double maxDiff = 0.0;
				int samples = 0;
				int b0 = 0;
				for (; b0 < MaxBlocks && (sophie_active(&s) || sophieOriginalActiveVoices(original)); ++b0)
				{
					if (b0 == 40)
					{
						for (const auto& setting : scenario.changeAt40)
						{
							sophie_set_param(&s, setting.param, setting.value);
							setOriginal(original, scenario.pad, setting.param, setting.value);
						}
					}
					sophie_render(&s, a, Block);
					sophieOriginalRender(original, b, Block);
					for (int i = 0; i < Block; ++i)
						maxDiff = std::max(maxDiff, std::fabs(static_cast<double>(a[i]) - b[i]));
					samples += Block;
				}
				const bool ended = !sophie_active(&s) && !sophieOriginalActiveVoices(original);
				std::printf("  %-6s %-14s %7d samples, max difference %.3g%s%s\n", ModelNames[model], scenario.name,
					samples, maxDiff, maxDiff == 0.0 ? " (bit-exact)" : "", ended ? "" : ", still ringing at 5 s");
				allExact = allExact && maxDiff == 0.0 && ended;
			}
			sophieOriginalDestroy(original);
		}
		return allExact;
	}
#endif

	void measureCost()
	{
		const double blockUs = 1e6 * Block / SampleRate;
		std::printf("Cost of one voice per %d-sample block (%.1f us of audio), ringing at Decay 4 s:\n", Block, blockUs);
		for (int model = 0; model < 6; ++model)
		{
			for (int heavy = 0; heavy < 2; ++heavy)
			{
				sophie_t s;
				sophie_init(&s, SampleRate, heavy ? 4 : 1);
				sophie_set_param(&s, SOPHIE_MODEL, static_cast<float>(model));
				sophie_set_param(&s, SOPHIE_DECAY, 4.0f);
				if (heavy)
				{
					// The scenarios held for the loop: a range over scenarios()[2].settings reads them after the
					// vector returned has gone, which the gate saw crash
					const auto all = scenarios();
					for (const auto& setting : all[2].settings)
						sophie_set_param(&s, setting.param, setting.value);
					sophie_set_param(&s, SOPHIE_DECAY, 4.0f);
					sophie_set_param(&s, SOPHIE_FILTER_TYPE, 3);
					sophie_set_param(&s, SOPHIE_TAIL_MODE, 1);
				}
				constexpr int Blocks = 40000;
				float block[Block];
				volatile float sink = 0.0f;
				sophie_trigger(&s, 127);
				const auto start = std::chrono::steady_clock::now();
				for (int b = 0; b < Blocks; ++b)
				{
					if (!sophie_active(&s))
						sophie_trigger(&s, 127);
					sophie_render(&s, block, Block);
					sink = sink + block[Block - 1];
				}
				const auto elapsed = std::chrono::duration<double, std::micro>(
					std::chrono::steady_clock::now() - start).count();
				const double us = elapsed / Blocks;
				std::printf("  %-6s %-27s %.3f us, %.2f %% of a core\n", ModelNames[model],
					heavy ? "Smooth, Crush, Ring, DJ, Live" : "pad 2 factory", us, 100.0 * us / blockUs);
			}
		}
	}

	void measureStartAndDecay()
	{
		std::printf("Start after a trigger at a block's first sample, and decay to silence:\n");
		for (int model = 0; model < 6; ++model)
		{
			for (int pad = 0; pad < 2; ++pad)
			{
				sophie_t s;
				sophie_init(&s, SampleRate, pad);
				sophie_set_param(&s, SOPHIE_MODEL, static_cast<float>(model));
				const auto hit = renderHit(s, 127);
				const float peak = peakOf(hit);
				int first = -1;
				int tenth = -1;
				int peakAt = 0;
				for (int i = 0; i < static_cast<int>(hit.size()); ++i)
				{
					const float x = std::fabs(hit[i]);
					if (first < 0 && x > 0.0f)
						first = i;
					if (tenth < 0 && x >= 0.1f * peak)
						tenth = i;
					if (x == peak)
						peakAt = i;
				}
				const float decay = sophie_get_param(&s, SOPHIE_DECAY);
				const auto stated = static_cast<int>(decay * SampleRate);
				const auto length = static_cast<int>(hit.size());
				std::printf("  %-6s pad %d: first sample %d, -20 dB of peak at %d, peak %.3f at %d; silent after %d "
					"samples, Decay %.3f s = %d\n", ModelNames[model], pad + 1, first, tenth, peak, peakAt, length,
					static_cast<double>(decay), stated);
				check(first >= 0 && first <= 1, "the hit starts at the trigger's sample, or the next through the LPF");
				check(length >= stated - Block && length <= stated + 2 * Block, "the hit ends at its stated Decay");
			}

			// Decay at its maximum with the longest ring: still silent at 4 s and after
			sophie_t s;
			sophie_init(&s, SampleRate, 1);
			sophie_set_param(&s, SOPHIE_MODEL, static_cast<float>(model));
			sophie_set_param(&s, SOPHIE_DECAY, 4.0f);
			sophie_set_param(&s, SOPHIE_RING_TIME, 30.0f);
			sophie_set_param(&s, SOPHIE_RING_FEEDBACK, 95.0f);
			sophie_set_param(&s, SOPHIE_RING_MIX, 100.0f);
			const auto hit = renderHit(s, 127);
			float after[Block];
			float rest = 0.0f;
			for (int b = 0; b < SampleRate / Block; ++b)
			{
				sophie_render(&s, after, Block);
				for (const float x : after)
					rest = std::max(rest, std::fabs(x));
			}
			const auto length = static_cast<int>(hit.size());
			std::printf("  %-6s Decay 4 s, Ring 30 ms 95 %% 100 %%: silent after %d samples (%.3f s), then %g for 1 s\n",
				ModelNames[model], length, static_cast<double>(length) / SampleRate, static_cast<double>(rest));
			check(length <= 4 * SampleRate + 2 * Block && !sophie_active(&s) && rest == 0.0f,
				"Decay 4 s ends within 4 s");
		}
	}

	void checkRetrigger()
	{
		sophie_t s;
		sophie_init(&s, SampleRate, 0);
		sophie_trigger(&s, 127);
		float block[Block];
		for (int b = 0; b < 20; ++b)
			sophie_render(&s, block, Block);
		const float last = block[Block - 1];
		sophie_trigger(&s, 127);
		sophie_render(&s, block, Block);
		std::printf("Retrigger: last sample %.6f, first after %.6f (Sophie's 1.5 ms steal blend)\n",
			static_cast<double>(last), static_cast<double>(block[0]));
		check(block[0] == last, "a retrigger blends from the ringing hit's last sample");
	}

	void checkMachineDefaults()
	{
		std::printf("Machine page defaults against factory pad 1:\n");
		sophie_t mapped;
		sophie_init(&mapped, SampleRate, 0);
		const sophie_patch_t factory = sophie_factory_patch(0);
		sophie_t reference;
		sophie_init(&reference, SampleRate, 0);
		reference.patch = factory;
		for (int i = 0; i < sophie::MachineParamCount; ++i)
		{
			sophie::setMachineValue(mapped, i, sophie::MachineDefaults[i]);
			const auto param = static_cast<sophie_param_t>(i);
			const float a = sophie_get_param(&mapped, param);
			const float b = sophie_get_param(&reference, param);
			std::printf("  %2d: value %3d -> %8.3f (factory %8.3f)\n", i + 1, sophie::MachineDefaults[i],
				static_cast<double>(a), static_cast<double>(b));
			const int v = sophie::MachineDefaults[i];
			const float step = std::fabs(sophie::machineValue(i, static_cast<uint8_t>(v < 127 ? v + 1 : v))
				- sophie::machineValue(i, static_cast<uint8_t>(v < 127 ? v : v - 1)));
			check(std::fabs(a - b) <= step, "a default is its factory value, to a step of its law");
		}
		check(sophie::machineValue(SOPHIE_MODEL, 127) == 5.0f && sophie::machineValue(SOPHIE_FILTER_TYPE, 127) == 4.0f
			&& sophie::machineValue(SOPHIE_TUNE, 0) == -24.0f && sophie::machineValue(SOPHIE_TUNE, 127) == 24.0f
			&& sophie::machineValue(SOPHIE_SWEEP, 0) == -100.0f && sophie::machineValue(SOPHIE_SWEEP, 127) == 100.0f
			&& std::fabs(sophie::machineValue(SOPHIE_DECAY, 127) - 4.0f) < 1e-5f
			&& std::fabs(sophie::machineValue(SOPHIE_METAL_DECAY, 127) - 2000.0f) < 1e-2f,
			"the laws reach their ranges' ends");

		const auto a = renderHit(mapped, 127);
		const auto b = renderHit(reference, 127);
		double maxDiff = 0.0;
		for (size_t i = 0; i < std::min(a.size(), b.size()); ++i)
			maxDiff = std::max(maxDiff, std::fabs(static_cast<double>(a[i]) - b[i]));
		std::printf("  hit at the defaults against the factory hit: %zu and %zu samples, peaks %.3f and %.3f, max "
			"difference %.3g\n", a.size(), b.size(), static_cast<double>(peakOf(a)), static_cast<double>(peakOf(b)),
			maxDiff);
		check(std::fabs(peakOf(a) - peakOf(b)) < 0.05f * peakOf(b), "the defaults sound like factory pad 1");
	}
}

#ifdef _WIN32
// The gate saw this test crash where a run by hand never did: a crash names its code, where it happened in the
// executable and what it touched, so that it can be found again
static LONG WINAPI reportCrash(EXCEPTION_POINTERS* _e)
{
	const auto* r = _e->ExceptionRecord;
	const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
	std::printf("CRASH: code 0x%08lx at exe+0x%llx, touching 0x%llx\n", static_cast<unsigned long>(r->ExceptionCode),
		static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(r->ExceptionAddress) - base),
		static_cast<unsigned long long>(r->NumberParameters > 1 ? r->ExceptionInformation[1] : 0));
	return EXCEPTION_CONTINUE_SEARCH;
}
#endif

int main()
{
	setvbuf(stdout, nullptr, _IONBF, 0);
#ifdef _WIN32
	SetUnhandledExceptionFilter(reportCrash);
#endif

	measureStartAndDecay();
	checkRetrigger();
	checkMachineDefaults();
	measureCost();

#ifdef SOPHIE_HAS_ORIGINAL
	check(compareWithOriginal(), "the port renders the original's samples");
	std::printf(g_failed ? "%d FAILED\n" : "PASSED\n", g_failed);
	return g_failed ? 1 : 0;
#else
	std::printf("No original (configure with SOPHIE_ORIGINAL_DIR): port checks %s, comparison skipped\n",
		g_failed ? "FAILED" : "passed");
	return g_failed ? 1 : 77;
#endif
}
