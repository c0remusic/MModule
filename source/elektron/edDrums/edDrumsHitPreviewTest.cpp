// The Hit preview (ticket 37 of .scratch/elektron-drums-v2): the Hit a fresh engine plays, from where it sounds, as long
// as it lasts within its bounds; the latest request alone; an engine built ahead; what it costs. Neither flash image nor
// vectors: BD MODERN, a Syntakt machine rewritten, reads nothing of the firmware
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <stdexcept>
#include <string>
#include <thread>

#include "edDrumsHitPreview.h"

namespace
{
	using namespace elektron::drums;
	using Clock = std::chrono::steady_clock;

	void require(const bool _condition, const std::string& _message)
	{
		if(!_condition)
			throw std::runtime_error(_message);
	}

	constexpr int Track = 3;
	const Machine Kick{Family::Syntakt, 0};

	// The reference: an engine of its own, the Hit at velocity 100, Main's mid from its hitDelay on
	std::vector<float> reference(const Library& _library, const int _level, bool& _clipped)
	{
		Engine e(_library);
		e.setMachine(Track, Kick);
		e.setLevel(Track, _level);
		e.noteOn(Track, 100);
		const int total = e.hitDelay() + HitPreview::MaxLength;
		std::vector<float> left(total), right(total);
		std::array<float*, 2 * Outputs> out{};
		out[0] = left.data();
		out[1] = right.data();
		e.render(out.data(), total);
		std::vector<float> mid;
		_clipped = false;
		for(int n = e.hitDelay(); n < total; ++n)
		{
			mid.push_back(0.5f * (left[n] + right[n]));
			_clipped |= std::abs(left[n]) > 1 || std::abs(right[n]) > 1;
		}
		return mid;
	}

	void sameHit(const Library& _library)
	{
		for(const int level : {100, 127, 0})
		{
			bool clipped = false;
			const auto expected = reference(_library, level, clipped);
			Engine e(_library);
			e.setMachine(Track, Kick);
			e.setLevel(Track, level);
			const auto p = HitPreview::render(e, Track);
			const int length = static_cast<int>(p.mid.size());
			require(length >= HitPreview::MinLength && length <= HitPreview::MaxLength, "the length within its bounds");
			for(int n = 0; n < length; ++n)
				require(p.mid[static_cast<size_t>(n)] == expected[static_cast<size_t>(n)],
					"the preview's sample " + std::to_string(n) + " is not the engine's");
			// Past it, quiet; at level 0, MinLength of silence
			float after = 0;
			for(size_t n = static_cast<size_t>(length); n < expected.size(); ++n)
				after = std::max(after, std::abs(expected[n]));
			require(after <= HitPreview::Quiet * 1.0001f, "the Hit goes on past the length (" + std::to_string(after) + ")");
			if(level == 0)
				require(length == HitPreview::MinLength, "silence: the shortest length");
			require(p.clipped == clipped, "full scale passed, or not, as the engine's");
			std::printf("  level %d: %d samples (%.0f ms), %s\n", level, length, length * 1000.0 / HitPreview::Rate,
				p.clipped ? "past full scale" : "within full scale");
		}
	}

	// Waits for _condition, at most 20 s
	void waitFor(const std::function<bool()>& _condition, const char* _what)
	{
		const auto end = Clock::now() + std::chrono::seconds(20);
		while(!_condition())
		{
			require(Clock::now() < end, std::string("waited too long for ") + _what);
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
	}

	void latest(const Library& _library)
	{
		HitPreview preview(_library);
		constexpr int Requests = 20;
		uint64_t last = 0;
		for(int i = 0; i < Requests; ++i)
		{
			HitPreview::Request r;
			r.track = Track;
			const int level = i == Requests - 1 ? 0 : 100;
			r.prepare = [level](Engine& _e)
			{
				_e.setMachine(Track, Kick);
				_e.setLevel(Track, level);
			};
			last = preview.request(std::move(r));
		}
		waitFor([&] { const auto p = preview.picture(); return p && p->serial == last; }, "the last request's picture");
		const auto p = preview.picture();
		float peak = 0;
		for(const float x : p->mid)
			peak = std::max(peak, std::abs(x));
		require(peak == 0 && static_cast<int>(p->mid.size()) == HitPreview::MinLength, "the last request's sound (level 0)");
		require(preview.rendered() <= 2, "requests replaced were rendered: " + std::to_string(preview.rendered()));
		waitFor([&] { return preview.ahead(); }, "an engine built ahead");
		std::printf("  %d requests in a row: %d rendered, the last; an engine built ahead after it\n", Requests,
			preview.rendered());
	}

	// The period of _x from _from over _length samples: the first peak of its autocorrelation, 8 to 1500 samples, that
	// reaches 85 % of the highest (a later peak at twice the period would score as high)
	int periodOf(const std::vector<float>& _x, const int _from, const int _length)
	{
		std::vector<double> scores(1501, 0.0);
		double top = 0;
		for(int lag = 8; lag <= 1500; ++lag)
		{
			double sum = 0, energy = 0;
			for(int n = _from; n < _from + _length && n + lag < static_cast<int>(_x.size()); ++n)
			{
				sum += _x[static_cast<size_t>(n)] * _x[static_cast<size_t>(n + lag)];
				energy += _x[static_cast<size_t>(n + lag)] * _x[static_cast<size_t>(n + lag)];
			}
			scores[static_cast<size_t>(lag)] = energy > 0 ? sum / std::sqrt(energy) : 0;
			top = std::max(top, scores[static_cast<size_t>(lag)]);
		}
		for(size_t lag = 9; lag < 1500; ++lag)
			if(scores[lag] >= 0.85 * top && scores[lag] >= scores[lag - 1] && scores[lag] >= scores[lag + 1])
				return static_cast<int>(lag);
		return 0;
	}

	// Ticket 39 (PITCH): OPEN DRUM and SOPHIE, which played no NOTE, play it now, an octave up a period halved
	void notes(const Library& _library)
	{
		for(const Machine machine : {Machine{Family::OpenDrum, 0}, Machine{Family::Sophie, 0}})
		{
			int periods[2]{};
			for(int i = 0; i < 2; ++i)
			{
				Engine e(_library);
				e.setMachine(Track, machine);
				e.setNote(Track, i ? 72 : 60);
				const auto p = HitPreview::render(e, Track);
				require(static_cast<int>(p.mid.size()) > 2 * HitPreview::Rate / 10, "a Hit of 200 ms at least");
				periods[i] = periodOf(p.mid, HitPreview::Rate / 20, HitPreview::Rate / 10);
			}
			const double ratio = static_cast<double>(periods[0]) / periods[1];
			std::printf("  family %d: period %d samples at C4, %d at C5, ratio %.3f\n", static_cast<int>(machine.family),
				periods[0], periods[1], ratio);
			require(std::abs(ratio - 2) < 0.1, "NOTE an octave up does not halve the period");
		}
	}

	void cost(const Library& _library)
	{
		double built = 1e9, rendered = 1e9;
		for(int run = 0; run < 5; ++run)
		{
			const auto t0 = Clock::now();
			Engine e(_library);
			const auto t1 = Clock::now();
			e.setMachine(Track, Kick);
			const auto p = HitPreview::render(e, Track);
			const auto t2 = Clock::now();
			built = std::min(built, std::chrono::duration<double, std::milli>(t1 - t0).count());
			rendered = std::min(rendered, std::chrono::duration<double, std::milli>(t2 - t1).count());
		}
		std::printf("  an engine built in %.1f ms, the Hit rendered in %.1f ms (best of 5)\n", built, rendered);
	}
}

int main()
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	try
	{
		const Library library;
		sameHit(library);
		latest(library);
		notes(library);
		cost(library);
	}
	catch(const std::exception& e)
	{
		std::printf("FAIL: %s\n", e.what());
		return 1;
	}
	std::printf("OK\n");
	return 0;
}
