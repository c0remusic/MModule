# Sophie

A metallic FM percussion voice, ported from [mestela/schwung-sophie](https://github.com/mestela/schwung-sophie)
(MIT, `LICENSE`: Matt Estela and the digisophie contributors), upstream commit
`abd132dc50b43f1f1bd775d3a9faf11e70fb9d9c` (2026-10-01), file `src/dsp/sophie.c`. Pipe, Boom and the smooth
feedback come from soejrd/digisophie (MIT, `ee4ff40`), as upstream says.

## What was kept, what was left out

- `sophieVoice.c` keeps `render_voice` line for line and `trigger_voice` with a pool of one voice. Its order stays
  the original's: operators, amplitude, Level, Crush, Drive and velocity, Ring (the feedback delay), then the filter.
- Left out: the 16-pad kit as state (the factory patches stay, `sophie_factory_patch`), the JSON state and its
  parser, the Schwung plug-in API, the UI hierarchy, `render_block`'s int16 output.
- Per instance, no globals: `sophie_t` holds the voice, the pad's current patch, the slews and the random generator
  (seeded as upstream's instance). It allocates nothing: the Ring's 1536-sample buffer is inside it (about 6.3 kB).
- `sophie_render` writes the voice's float output before upstream's mix gain (`SOPHIE_MIX_GAIN`, 0.24, which
  upstream applies to the sum of its 12 voices before converting to int16).

## The voice pool

Upstream plays 16 pads on a pool of 12 voices: each note takes a free voice, the oldest when none is free, and a
voice keeps a snapshot of its pad's patch. Here a Track is one pad and one voice: a new trigger takes the ringing
voice with upstream's own steal blend (1.5 ms from the last sample), as upstream does when its pool is full. A
Track's hits therefore do not overlap; upstream's do when the same pad is played faster than it decays.

A pad also picks the base pitch, note 36 + pad (pad 1, C2, 65.4 Hz), and pad 1 with models 0-3 plays the kick body
(a sine with a slower pitch envelope, the FM as a quiet layer) instead of the model's FM topology: `sophie_set_pad`,
taken at the next trigger. Here the body is a flag of its own (`sophie_set_body`, which `sophie_set_pad` sets as
upstream would), so that any pad can play it.

The drum instrument plays every Track on pad 1 (note 36) and has no pad: Machine 20, BODY, turns the body on or off,
and the 16 factory pads are sounds of that one voice (`sophie::soundValues`), each pad's base note taken into Tune.

## API

```c
sophie_t s;
sophie_init(&s, 44100, 0);                  /* pad 1, its factory patch */
sophie_set_param(&s, SOPHIE_METAL, 50.0f);  /* Sophie's units, clamped as Sophie clamps */
sophie_trigger(&s, velocity);               /* 1-127 */
sophie_render(&s, out, 32);                 /* any block size: Sophie runs per sample */
```

Parameters set between blocks act from the next sample. As upstream: Hit tails (Tail Mode 0) keep the patch they
started with; Live tails follow Color, Metal, Feedback, Cutoff and Resonance with a 5 ms slew. Tail Mode itself is
latched at the trigger.

## Machine values

`sophieMachine.h` maps Machine 1-19 (0-127) to Sophie's units, and Machine 20, BODY, below 64 off, else on. The
defaults are factory pad 1, the kick, to the nearest value, BODY on. Level (0-100 %, upstream default 100) is not on the page: the Track's level does that job.

| # | Parameter | Law, v = 0-127 | Default |
|---|---|---|---|
| 1 | Model | `v * 6 >> 7`: Fuse, Stack, Split, Shard, Pipe, Boom (21 or 22 values each) | 0, Fuse |
| 2 | Tune | `(v - 64) / 2` semitones, clamped to -24..+24 (v 16-112) | 54, -5 st |
| 3 | Color | `v * 100 / 127` % | 30, 23.6 (24) |
| 4 | Metal | `v * 100 / 127` % | 10, 7.9 (8) |
| 5 | Feedback | `v * 100 / 127` % | 8, 6.3 (6) |
| 6 | Sweep | `(v - 64) * 100 / 64` below 64, `(v - 64) * 100 / 63` above: -100..+100 % | 99, 55.6 (55) |
| 7 | Decay | `0.03 * (4 / 0.03)^(v / 127)` s, 0.03-4 s | 58, 0.280 s |
| 8 | Crush | `v * 100 / 127` % | 0 |
| 9 | Drive | `v * 100 / 127` % | 74, 58.3 (58) |
| 10 | Cutoff | `v * 100 / 127` % (40 Hz x 400^(%/100), 40 Hz-16 kHz) | 127, 100 |
| 11 | Resonance | `v * 100 / 127` % | 0 |
| 12 | Filter Type | `v * 5 >> 7`: LPF, HPF, Notch, DJ, BPF (upstream's indices) | 0, LPF |
| 13 | Ring Time | `0.5 * 60^(v / 127)` ms, 0.5-30 ms | 77, 5.98 ms (6) |
| 14 | Ring Feedback | `v * 95 / 127` % | 0 |
| 15 | Ring Mix | `v * 100 / 127` % | 0 (Ring bypassed) |
| 16 | Ring Tone | `v * 100 / 127` % | 89, 70.1 (70) |
| 17 | Metal Decay | 0: off (the pitch-linked contour, 135 ms on Pipe and Boom); else `5 * 400^((v - 1) / 126)` ms, 5-2000 ms | 0, off |
| 18 | Feedback Mode | below 64 Raw, else Smooth | 0, Raw |
| 19 | Tail Mode | below 64 Hit, else Live | 0, Hit |

## Test

`sophieTest`: the port against the original, compiled into the test from a clone outside the repository
(`SOPHIE_ORIGINAL_DIR` in the environment at configure time, kept in the cache; without it the test runs the port's
own checks and exits 77). Five hits per model (pad 1 and pad 2 factory, a heavy setting through Smooth, Crush, Drive,
Ring and a resonant filter, a Live tail edited while it rings, a Smooth kick), rendered in blocks of 32 at 44.1 kHz:
bit-exact, the original's float voice output against the port's, both compiled with `/fp:precise` (upstream renders
per sample at 44.1 kHz too, so block size and sample rate change nothing). It also measures the cost per voice and
block, the start after a trigger, the decay to silence (default and Decay 4 s with the longest Ring) and the machine
defaults against factory pad 1.
