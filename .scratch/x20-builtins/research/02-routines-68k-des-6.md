# The 68k routines of the 6: call them or port them

Research for ticket 02 of the map "X.20's built-in Machines in MoDule 0.1.3". Written 2026-10-21 against `main` at
53c9629. Nothing in the repository was built, run or changed apart from this note, and no part of X.20 or of a flash
image was copied into it. Read statically: X.20 B's 68k OS (payload 3, `$200000-$2be93c`), decoded from the user's
`Machinedrum_SPS1-UW_OS_X.20_B.syx` by a probe that reads `MD_X20_OS` (the same bytes as research 01's decode of the
User Machines map), and disassembled with Musashi's disassembler. Measured: a scratch tool, `run68k`, runs X.20's own
routines in Musashi in MCF5206E mode (the repository's `68kEmu.lib`, a copy, with a CPU class like `MachineRunner`'s),
and a Python port of the six is compared with it. Everything stays in the session scratchpad, `x20b-68k/`:
`p01_decode.py` (the OS, and OS 1.63's decay table from its section 0), `run68k.cpp` and `build.bat`, `p03_port.py`
(the port and its check), `p04_table.py` (the Machine table), `p05_defaults.py`, `p06_tuning.py`.

Marks: **[V]** read in X.20 B's code at the address named; **[M]** measured by the probes here; **[I]** inferred.
Every address is X.20 B's unless it says 1.63.

## In short

- The six routines are small, pure and self-contained. Each one reads the Track's eight parameter words (setting << 7,
  with modulation), two fields of the per-Track context (+`$8c` the tuning mode, +`$90` a path to the decay table),
  and fixed tables. It writes words 1 to 8 (or 9) of that context and returns the word count: 9 for the three GND and
  NFX-EV, 10 for NFX-CO and NFX-UC. None of them tests word 0, the trigger word, so X.20 calls them on every tick.
  [V]
- Every table they read can be computed, and nothing of X.20's data is needed. [V]/[M]
  - `$2cdb00` is only a base. The tables are W (192 words at `$2cdc80`), L (128 longs at `$2cde00`) and a third one,
    P3 (192 longs at `$2ce000`). `$21bd6c(TUNING)` builds all three with integer recurrences.
  - `$2cdc6c` is not a table: it holds the tempo × 24 (`$228dbe` writes it). Only NFX-EV reads it.
  - NFX-CO's two tables (`$2bd636`, 128 longs; `$2bd836`, 176 longs) are filled by two loops of X.20's static
    initialiser (`$200376-$2003dc`).
  - The decay table is OS 1.63's (1.63 `$24bf94`). `HostModel` already reads it.
- A port reproduces every routine bit for bit [M]. It was checked on 120,576 calls: the 6 routines, the 4 tuning modes,
  each parameter swept over its 128 settings, and 4,000 random 14-bit vectors per routine and mode. Its computed tables
  equal those X.20's own builders leave in memory, at TUNING −128 (legacy), −50, −13, −1, 0, 1, 7 and 50 [M].
- Called in Musashi, a routine costs 2.1 to 6.4 µs (110 to 357 instructions) [M]. That is cheap. The cost of way (a)
  lies elsewhere: X.20's OS needs a second 68k, because it sits at `$200000` like 1.63's; it needs a boot sequence of
  its own, about 15 addresses of X.20 B, and 1 MB per engine. A port is about 250 lines of C++ and needs nothing of
  X.20. [M]/[I]
- **Recommendation: port them (way b).** `md::mds::convertFine` already sets the precedent: it ports X.20's `$21aeba`
  with its constants. Prove the port with a test that calls X.20's routines in a 68k on the user's OS (`MD_X20_OS`,
  skipped as 77 without it). For ticket 04, GND-SW in the DEFAULT tuning mode is mostly pieces MoDule already has.

## 1. How X.20 calls them

- **The call [V].** The tick (`$2a5164(engine, group)`, Tracks 4·group to 4·group+3) calls the Track's routine
  through the trampoline `$294118`, as `fn(context, words)`. It does so at `$2a57e2`:
  - context: engine + `$125c` + `$98` · Track (152 bytes);
  - words: engine + `$cb8` + `$4c` · Track.

  Just before the call (`$2a57b0-$2a57d8`) the tick writes:
  - +`$84`: the Track;
  - +`$88`: the Machine id;
  - +`$8c`: the tuning mode. That is 0 when the Track's tonal bit (engine +`$2f6`) is clear. Otherwise the tick keeps
    2 or 3 and writes 1 for anything else;
  - +`$90`: engine + 4.

  It then keeps count − 1 at +`$94` and copies the first 16 bytes of the words to engine + `$1e2c` + 16 · Track.
  Research 10 of the User Machines map read the same call.
- **The context's first ten longs are the slot.** Word 0 at +0 is the trigger word: the tick tests it and clears it
  (`$2a548e-$2a549a`). The routines write +4 onwards, words 1 to 9, which are Y:(R6+1..9) on the voice DSP. That is
  1.63's `fn(out, params)` convention, which `MachineRunner::compute` follows, with the context in place of `out`. [V]
- **Which routine [V].** Assignment (`$2946da`) maps the id through the byte map `$2cf1ec` to an index into the
  pointer table `$2b0d04`, and keeps the routine at engine + `$2784` + 4 · Track (research 01 §9). The six:

| Id | Machine | Routine | Count |
|---|---|---|---|
| 1 | GND-SN-PRO | `$21ada2` | 9 |
| 4 | GND-SW | `$21acbc` | 9 |
| 5 | GND-PU | `$21abd2` | 9 |
| 7 | NFX-EV | `$2197d2` | 9 |
| 8 | NFX-CO | `$2196c0` | 10 |
| 9 | NFX-UC | `$219402` | 10 |

- **What they touch [M].** In `run68k` the context held only +`$8c` and +`$90` (pointing at a struct whose +`$24`
  points at a copy of 1.63's table). The tables were filled by X.20's own builders and the tempo was written at
  `$2cdc6c`. No routine made an access outside main RAM. The port, which reads nothing else, matches every word, so
  no other state enters. +`$84` and +`$88` are written by the tick and not read by these six.
- **No MAC [M].** Neither the six, their helpers, nor the two builders run a MAC instruction: Musashi, which has no
  MAC unit (research 02 of the User Machines map), ran them all.

## 2. The helpers

All [V], and all ported and checked [M].

- **Pitch, `$2189b0(v, mode)` → `$218184(v, mode, $c0)`.** This is research 10's Pitch conversion: DEFAULT
  `L[(v >> 4) & 127] << (v >> 11)`; TONAL modes `W[d − 192·o] << (o + a)`, with `d = (v >> 4) + 192` and the octave
  from the 30 words at `$2b0ad0` (`(192·o) << 4 | o`, three per octave).
- **Decay, `$2189c6(context, i)`.** It returns 0 when the context is null, when context +`$90` is null, or when
  i > `$1ff`. Otherwise it returns `T[i]` from the pointer at (context +`$90`) +`$24`, which is 1.63's 512-long table.
  The GND routines index it with `v >> 5`. The NFX routines index it with linear maps that start part way into the
  table:

| Map | Index at setting s | Used by |
|---|---|---|
| `((v · $50) >> 12) + $78` | ⌊2.5 s⌋ + 120 (120 to 437) | NFX-EV ATK DEC REL, NFX-UC TI |
| `((v · $38) >> 12) + $90` | ⌊1.75 s⌋ + 144 (144 to 366) | NFX-CO ATK REL, NFX-UC TD |

- **UNIS, `$21822a(a, b)`.** It computes `(P(min(a + (b >> 3), $3fff)) − P(min(a, $3fff))) >> 5`, where P is the
  DEFAULT pitch whatever the Track's mode. That is a detune in pitch units. At PTC1 73 it gives 8, 624 and 1,498 for
  UNIS 1, 64 and 127 [M].
- **Unipolar, inline in the NFX routines.** 0 when v ≤ `$7f`, `$7fffff` when v > `$3f7f`, otherwise v << 9. This is
  research 10's Unipolar, and `convertFine`'s.
- **Time, `$218624(v, mode)` (NFX-UC in a tonal mode).** It reads P3 octave by octave: `d = v >> 4` picks one of 192
  entries, shifted left by 3, 2 or 1 below d = `$1a0`, and right by 0 to 3 above. The table is 192 longs from
  `$5e7ebde0` · 2^(−c/1200) that fall by 2^(−1/192) per entry, stored >> 10 (research 10 §3.1). Mode 2 then doubles
  the result (capped at `$7fffff` above `$3fffff`) and mode 3 halves it. So it is a period: TONAL-1 is an octave
  lower, which makes the time twice as long. [V]; that it is a period is [I].

## 3. The six routines

In every table below, w*k* is the routine's k-th parameter word, numbered from 0 as in the parameter order. "Rel(x)"
is GND's relative pitch:
- 0 when the word x is 0, or when PTC1 + x − `$2000` falls outside 0 to `$3f80` (compared unsigned, so a negative sum
  counts as outside);
- otherwise Pitch(PTC1 + x − `$2000`, mode).

So PTC2 to PTC4 are offsets from PTC1, centred on 64, and 0 switches them off. [V]

### GND-SN-PRO, `$21ada2`

Parameters PTC1 DEC RAMP RDEC PTC2 PTC3 PTC4 UNIS; defaults 73 96 0 0 0 0 0 0 (as 1.63's GND-SN begins).

| Word | Value |
|---|---|
| 1 | Pitch(PTC1, mode) |
| 2, 3, 4 | Rel(PTC2), Rel(PTC3), Rel(PTC4) |
| 5 | T[DEC >> 5] |
| 6 | (RAMP · RAMP) >> 7, which is s² << 7 at a whole setting |
| 7 | T[RDEC >> 5] |
| 8 | UNIS(PTC1, min(UNIS, `$3f80`)) |

### GND-SW, `$21acbc`

Parameters PTC1 DEC RAMP RDEC PTC2 PTC3 SKEW UNIS; defaults 73 96 0 0 0 0 127 0.

| Word | Value |
|---|---|
| 1 | Pitch(PTC1, mode) |
| 2, 3 | Rel(PTC2), Rel(PTC3) |
| 4 | T[DEC >> 5] |
| 5 | (RAMP · RAMP) >> 7 |
| 6 | T[RDEC >> 5] |
| 7 | SKEW, the raw 14-bit word |
| 8 | UNIS(PTC1, UNIS), with no clamp |

At the defaults, in DEFAULT mode: `00bd50 000000 000000 7ffed2 000000 5f29c2 003f80 000000` [M].

### GND-PU, `$21abd2`

Parameters PTC1 DEC RAMP RDEC PTC2 PTC3 WIDT UNIS; defaults 73 96 0 0 0 0 127 0. The same as GND-SW, except word 7,
which is 2 · WIDT (s << 8). [V]

### NFX-EV, `$2197d2`

Parameters DEL ATK DEC SUS HOLD REL RING NBAL; defaults 0 32 16 100 64 32 0 127.

The routine first derives a unit U from the tempo: U = ((⌊`$10266000` / T24⌋ · 60) >> 12) & `$ffff`, where T24 is the
tempo × 24 at `$2cdc6c` (`$228dbe` clamps it to `$1e0-$1a40`, 20 to 280 BPM). U is the number of samples in a 64th
note at 44.1 kHz: 1,322 at 125 BPM, 8,268 at 20 BPM and 590 at 280 BPM [M]. MoDule's `HostModel::setTempo` already
keeps 1.63's equivalent.

| Word | Value |
|---|---|
| 1 | (U · DEL) >> 7, which is U · s at a whole setting: DEL in 64th notes |
| 2 | T[((ATK · 80) >> 12) + 120] |
| 3 | T[((DEC · 80) >> 12) + 120] |
| 4 | Unipolar(SUS) |
| 5 | (U · HOLD) >> 8, which is U · s / 2 |
| 6 | T[((REL · 80) >> 12) + 120] |
| 7 | Unipolar(RING) |
| 8 | Unipolar(NBAL) |

### NFX-CO, `$2196c0`

Parameters ATK REL THRE RTIO KNEE SIDE MKUP NBAL; defaults 64 16 96 32 127 127 0 127.

| Word | Value |
|---|---|
| 1 | `$7fffff` − T[((ATK · 56) >> 12) + 144] |
| 2 | `$7fffff` − T[((REL · 56) >> 12) + 144] |
| 3 | A[THRE >> 7] |
| 4 | R[RTIO >> 7] |
| 5 | A[(KNEE >> 7) + 48] |
| 6 | not written |
| 7 | Unipolar(SIDE) |
| 8 | Unipolar(NBAL) |
| 9 | ((d · 15) >> 4) + `$7ffff`, with d = (MKUP · MKUP) >> 5 |

- **Word 6** keeps whatever the context held, and assignment does not clear it [V]. NFX-CO's RENDER reads Y:(R6+1 to
  5) and (R6+7 to 9), never R6+6 [V] (P:`$13bcb0-$13bd43` of X.20's voice program). So it does not matter.
- **A (176 longs at `$2bd836`) [V]/[M].** From i = 0: d0 = `$fffd1b90` + `$435` · i and d1 = `$ff980fb0` + `$972f` · i,
  with A[i] = ((d0 >> 12) + d1) & `$ffffff`. That is nearly a straight line, from `$980f81` to `$ff68d0` (−0.813 to
  −0.005 in signed Q23): a level on a logarithmic scale [I].
- **R (128 longs at `$2bd636`) [V]/[M].** R[i] = (⌊(`$3fffffff` + 31 i) / (`$80` + 31 i)⌋ + `$800000`) & `$ffffff`. In
  signed Q23 that is 128 / (128 + 31 i) − 1, which is 1/ratio − 1 for a ratio of 1 + 31 i / 128, from 1:1 to about
  31.7:1 [I].

### NFX-UC, `$219402`

Parameters TIM1 TIM2 TD TI FF FB DMIX NBAL; defaults 16 0 64 0 80 64 0 127.

| Word | Value |
|---|---|
| 1, 2 | mode 0: Unipolar(TIM1), Unipolar(TIM2); a tonal mode: Time(TIM1, mode), Time(TIM2, mode) |
| 3 | `$7fffff` − T[((TD · 56) >> 12) + 144] |
| 4 | 0 when TI ≤ `$80`, otherwise T[((TI · 80) >> 12) + 120] |
| 5, 6 | Curve(FF), Curve(FB) |
| 7 | Unipolar(DMIX) |
| 8 | Unipolar(NBAL) |
| 9 | 0 |

- **Curve [V]/[M].** A bipolar curve about setting 64, negated below it:
  - near the centre: (((x · `$45c29`) >> 20) + x) << 10, after a warp x = x' + ((x' · `$2083`) >> 19) above the
    centre;
  - further out: T[((x − `$1800`) · 178 >> 11) + 78] + `$1859` (or `$185a`). The decay table is used here as a
    saturating curve.

  It gives −`$800000` at 0, 0 at 64, `$29400` at 65, `$52b800` at 96 and `$7fffff` at 127 [M].
- **Word 9** is computed from FF and FB (`eori.l #$ffff8000`, `tst.w`, `seq`) and is non-zero only for a 16-bit word
  of `$8000`, which a parameter word never reaches. The port gives 0 and matches on every call [V]/[M].

### Defaults as words

DEFAULT mode, 125 BPM, from the port [M]:

```
GND-SN-PRO  9  00bd50 000000 000000 000000 7ffed2 000000 5f29c2 000000
GND-SW      9  00bd50 000000 000000 7ffed2 000000 5f29c2 003f80 000000
GND-PU      9  00bd50 000000 000000 7ffed2 000000 5f29c2 007f00 000000
NFX-EV      9  000000 7fa77a 7f1e02 640000 00a540 7fa77a 000000 7fffff
NFX-CO     10  0017c9 00aaa2 d0c13a 8ea0ea ff68d0 ------ 7fffff 7fffff 07ffff
NFX-UC     10  100000 000000 0017c9 000000 295800 000000 000000 7fffff 000000
```

### Names and defaults

The table at `$2b31a6` holds 8 records of 60 bytes [V]/[M] (`p04_table.py`):
- the id;
- the name in 6 bytes, stored without its dash (`GNDSN`, shown "GND-SN-PRO" in the readme's list);
- 8 parameter names of 5 bytes;
- 8 defaults;
- a zero byte;
- one nibble per parameter in 4 bytes.

The nibbles are:

| Machine | Nibbles |
|---|---|
| GND-SN-PRO | 1 1 3 1 5 5 5 1 |
| GND-SW, GND-PU | 1 1 3 1 5 5 1 1 |
| NFX-EV | 3 3 3 3 3 3 1 5 |
| NFX-CO | 3 3 3 3 3 7 3 7 |
| NFX-UC | 1 1 1 1 7 5 5 5 |

They look like a display type [I]. The 5 on GND's PTC2 to PTC4 fits a value centred on 64.

## 4. Where the tables come from

| What the routines read | Where X.20 gets it | Computable without X.20 |
|---|---|---|
| W `$2cdc80`, L `$2cde00`, P3 `$2ce000` | `$21bd6c(c)`, called through `$21bfb6` at boot (`$2ac544`, then with 0) and by the TUNING setter (`$231104`); research 10 §3.1 | yes: integer recurrences of the Global TUNING c (−50 to 50, or −128 legacy) [M] |
| OCT `$2b0ad0` (30 words) | initialised data in the image | yes: `(192·⌊j/3⌋) << 4 \| ⌊j/3⌋` [M] |
| A `$2bd836`, R `$2bd636` | the static initialiser `$200340`, loops at `$200376-$2003dc` | yes: the two formulas of §3 [M] |
| tempo `$2cdc6c` | `$228dbe` and `$22ea28` (the tempo setters, × 24, clamped) | it is the tempo |
| decay table, via context +`$90` → +`$24` | 1.63 `$24bf94`, imported at start-up (`$213a6e`, research 10 §4) | OS 1.63's, which `HostModel::decayTable` already reads |
| tuning mode, context +`$8c` | the tick, from the Track's tonal bit and the Kit's mode (research 10 §3.3) | a Kit value MoDule does not have yet: 0 |

So research 01's "tables at `$2cdb00` and `$2cdc6c` that X.20 builds at boot" are three tuning tables and the tempo.
No X.20 boot, and no dump of one, is needed. [V]/[M]

## 5. The two ways

### (a) Call X.20's routines in an emulated 68k

- **A second 68k.** X.20's OS occupies `$200000-$2be93c` and its BSS runs to `$2d72a4`. OS 1.63's image, whose
  smoothing, LFO and other Machines `HostModel` calls, occupies the same addresses. So it needs a second
  `MachineRunner` over X.20's image (Musashi serves one CPU class per executable, so a second instance of the same
  class, as `clone()` already makes). That costs 1.06 MB per engine: the Device's, and the Hit preview's each time it
  builds one. [V]/[I]
- **A load sequence of its own.** `MachineRunner`'s constructor throws on an image without 1.63's `TRXB2PTCH` table,
  so it needs a variant that instead:
  - runs `$200376-$2003de` (3,045 instructions) and `$21bd6c(c)` (33,006 instructions) [M];
  - copies the decay table in and points a fake engine's +`$28` at it;
  - writes the tempo × 24 into `$2cdc6c` at every `setTempo`;
  - keeps 16 contexts with +`$8c` and +`$90`.
- **Addresses.** About 15 addresses of X.20 B: the 6 routines, the initialiser's loop bounds, `$21bd6c`, `$2cdc6c`,
  `$2cdc78`, and the context offsets. A later X.20 moves them. Finding them by shape means following record → id map
  `$2b0b24` → table `$2b0d04`, and finding the initialiser's loops and the builder by their code. [I]
- **CPU [M].** Measured per call in `run68k` (Release, this PC), 200,000 calls each:

| Routine | µs per call | Instructions |
|---|---|---|
| GND-SN-PRO | 6.4 | 357 |
| GND-SW | 5.3 | 293 |
| GND-PU | 5.6 | 295 |
| NFX-EV | 2.5 | 134 |
| NFX-CO | 2.1 | 110 |
| NFX-UC | 4.0 | 222 |

  At most 16 calls per tick at `HostModel`'s 125 Hz, that is at most 13 ms per second of audio (1.3 % of a core), and
  well below that on a real Kit. It is the same class of cost as 1.63's routines, which `HostModel` already calls this
  way.
- **Risk.** Later X.20 builds may use the MAC unit that Musashi lacks. These do not [M].
- **Effort.** About 1.5 to 2 days: the runner variant, the init, the contexts, the finders by shape, and their tests.
  The reference stays the routine itself, so a test can only check the plumbing. [I]
- **In its favour.** It follows the map's decision to the letter: the 68k code comes from the user's OS at run time.

### (b) Port them to C++, bit for bit

- **Size.** The Python port is about 230 lines with its checker (`p03_port.py`). In C++, beside
  `md::mds::convertFine`, it would be about 250 lines. The pieces are:
  - the three tuning tables (convertFine already builds L);
  - A and R;
  - Pitch with the tonal modes, Rel, UNIS, Unipolar, the two decay maps, Time and Curve;
  - six functions of (8 words, tuning mode, tempo × 24) → count and words.

  [M] for the Python, [I] for the C++.
- **Exact [M].** 0 differences on 120,576 calls against X.20's routines, and the tables equal at 8 TUNING values.
- **Nothing of X.20's data.** Everything is computed or comes from 1.63. It finds no addresses and has no MAC
  exposure. CPU is negligible (tens of nanoseconds per call [I]). It fits the Hit preview, the Device and any number of
  engines without memory.
- **What it puts in the repository.** A re-expression of X.20's arithmetic: about 30 constants (`$10266000`,
  `$45c29`, `$2083`, `$1859`, the table recurrences…) and the formulas of §2-3, not X.20's bytes or tables. The
  repository already does this: `convertFine` ports X.20's `$21aeba` with `$1016541`, and `mdDrumsLfo` ports OS 1.63's
  LFO.
- **Effort.** About 1 day of port, plus half a day for the test below. [I]

### The test that compares them

The routines need no boot, so a test can be the oracle for way (b).
1. Load the X.20 OS decoded from `MD_X20_OS` (ticket 03's reader) at `$200000` in a 68k.
2. Run the two builders, write the tempo, and point context +`$90` at 1.63's decay table.
3. Call each routine with context +`$8c` set and the 8 words written.
4. Compare the words +4 to +`$24` and the count with the port's, for the 6, the 4 modes and the whole-setting sweeps
   and random vectors that `run68k grid` uses.
5. Exit 77 without `MD_X20_OS`.

Write a marker such as `$deadbeef` before each call, so that a word the routine leaves alone (NFX-CO's 6) shows. Since
Musashi serves one CPU class per executable, the test should use `MachineRunner` itself (`call`, `poke`, `peek`). That
needs a constructor that accepts an image without 1.63's Machine table. [M] for the method, [I] for the C++ shape.

## 6. Recommendation, and what ticket 04 needs

- **Take way (b), the port, with the test of §5 as its proof.** The routines are pure arithmetic, and their only
  inputs are the eight words, the Track's tuning mode, the tempo and the Global TUNING. A port removes the second 68k,
  X.20 B's addresses and the MAC risk, and costs less. It follows the repository's precedent, `convertFine`. The user
  should confirm at ticket 04 that a port of X.20's arithmetic is acceptable under the map's rule "nothing of X.20 in
  the repository". If not, way (a) works too, at the cost listed.
- **GND-SW for ticket 04.** In MoDule's DEFAULT tuning mode (no Track tuning mode yet), its eight words are:
  - Pitch on L, which `convertFine` already computes (`Conversion::Pitch`);
  - Rel, which is Pitch of PTC1 + x − `$2000`, gated;
  - Decay, `m_decay[v >> 5]`;
  - (RAMP²) >> 7, which is not `SquaredQ23` (that is >> 5);
  - SKEW raw;
  - UNIS, the L-pitch difference >> 5.

  Count 9, called on every tick like 1.63's routines, word 0 the trigger word as `HostModel::updateVoice` gives it.
  GND-SN-PRO is the same function with a third Rel and the UNIS clamp, and GND-PU differs in word 7 only, so the port
  is best written as one GND function with three variants.
- **Inputs MoDule must decide (the map's open points).**
  - The tuning mode per Track: 0 until the Kit carries X.20's tonal bits.
  - The Global TUNING: TUNING 0, or legacy, which give the same L. W and P3 differ, which matters only in tonal modes.
  - NFX-EV's tempo: `HostModel`'s, clamped to 20-280 BPM as `$228dbe` clamps it.

## Open

- **Word 6 of NFX-CO.** It is never written, and its RENDER does not read it. Whether some other path writes context
  +`$18` was not traced.
- **The words passed.** That they are the smoothed, LFO-applied SYN1-8 in the same order as 1.63's `voiceParams` is
  research 10's inference (`$2a6e2a`); the tick's 38-word block (engine + `$cb8`) was not mapped word by word.
- **X.20's tick schedule.** One group of four Tracks per call of `$2a5164`, which may change how often a Track's words
  are refreshed compared with `HostModel`'s 11-block tick. It was not measured, since X.20 does not boot in mdLib.
- **The nibbles' meaning** (display types) and the GND parameters' display (centred PTC2-4).
- **Later X.20 builds.** For way (b), only the names, defaults and DSP code come from the OS. For way (a), every
  address above moves.
