# BD MODERN (engine 0), Syntakt OS 1.41, voice CPU (section 7)

Sources: `dis/bdmodern_render_top.s` and cfdis disassembly of render 0x40006c9e, update
0x40006968 and every routine they reach: 0x400035de, 0x4000290e, 0x40003df2, 0x400049f4,
0x40004674, 0x40004ee6 (+ 0x40004e6c), 0x400046dc, 0x40003cda, 0x40002596, plus voice init
0x40003ee0 for the defaults. Every indexed EA with a displacement was checked on its extension
word (all are scale ×4: 0x2c04, 0x0c00, 0x0c08, 0x0c18, 0x1c00), and every MAC-with-load was
decoded by hand from its two words. ROM tables were read from the image. Nothing here was run
in the emulator yet. Statements without a tag come straight from the code. **[INFER]** marks
interpretation.

Calling convention, voice state S, units U_k = S+0x80·k, the trig delay (S+0x34 pending →
S+0x38 start now), EMAC mode (MACSR = 0xA0) and the shared routines 0x40003c0e, 0x40003d42,
0x400045a0, 0x400047ea, 0x40004dc6 are as in `sytone_notes.md`. Only how BD MODERN uses them is
given here.

EMAC notation, exact (`tools/emu/emac.py`): accumulators are 48-bit. `move.l Ry,ACC` sets
`acc = Ry<<8`. `mac Ry,Rx` adds `(2·Ry·Rx) >> 24` (floor), `msac` subtracts it. `move.l ACC,Rx`
and `movclr.l ACC,Rx` return `sat32(acc >> 8)` (floor), and movclr then clears acc.
`frac(a·b)` = a lone mac then movclr on a cleared acc = `(2ab) >> 32`. When several macs run
before one store, the terms are summed at 40-bit precision and truncated once. That
distinction matters for bit-exactness and is spelled out per routine. Every routine below ends
with `movclr` on each acc it used, and several start with `mac` into acc0 without clearing it
(0x400049f4, 0x40004674, 0x40004e6c). They rely on acc0 = 0 at entry. [INFER] It is an
invariant of the whole voice code.

Correction to `sytone_notes.md`: **0x400025c0 is not a routine.** No absolute or PC-relative
reference reaches it. It is the `add.l pitch` instruction inside 0x40002596, which is the
n-unit, 48 kHz version of SY TONE's 0x40002686 (§2.10).

---------------------------------------------------------------------------------------------

## 1. What BD MODERN is

Everything runs at **48 kHz, 32 samples per block**: no oversampling and no decimator (unlike
SY TONE). One block is 0.667 ms. Four oscillator units, an envelope on the modulator mix, a
pitch sweep on all four units, a filtered click, a drive/duck stage and the amp envelope.

```
                  pitch = note + tune(b2) − 88 st, × sweep m(t) (b8), all units
U1 sine @2f ──B1──▶ PM depth −0.110 cyc ─▶ U2 ROM wave @2f ─▶ ×L2 (unit env 1.0→0.75) ─A2─┐
U0 sine @2f with self-FB (b6) ───────────────────────────────────────────────────B0 ─┐  │
                                         mix = g1(b6)·B0 + g0(b4)·A2  (0x400049f4) ◀─┴──┘
                                         × modulator env (b10)        (3df2 + 4674)
                                         │  0x80009C58
                                         ▼  PM depth ≈ 1 cycle per full scale
                              U3 sine @f  ─B3─▶ drive/duck (b12, 47ea) ─┐
click: 1→0 linear ramp in 479 samples × amp(b6), HP 250 Hz, LP 3.4 kHz ─┴▶ sats(+) ─▶ out
                                                      × amp env (b16 decay, b14 hold), VCA
```

- **Body**: U3, a sine at the note frequency, phase-modulated by an enveloped mix of two
  oscillators an octave up. This gives a bright attack that the modulator envelope (byte 10)
  pulls back toward a purer sine.
- **Modulator mix**:
  - U0: a sine with self-feedback (byte 6 sets the feedback).
  - U2: a ROM wavetable at 0x4001E7FC, lightly phase-modulated by the U1 sine, with a short
    unit envelope that settles at 0.75.
  - Their levels: byte 4 (U2) and byte 6 (U0).
- **Pitch sweep**: one per-block exponential decay of a frequency ratio, from 32·T_EC00[b8]
  (1× to 21.4×) down to 1×, applied to all four units. Byte 8 sets both the depth and the
  decay time.
- **Click**: a linear ramp from 1 to 0 over 479 samples (≈10 ms), scaled by byte 6, then a
  first-order HP at ≈250 Hz and a first-order LP at ≈3.4 kHz. It is added after the drive
  stage.
- **Noise**: there is none in this engine.

Render 0x40006c9e(out, S), exact sequence (locals −4 = 1, −8 = 0):
```
40003c0e(S, 4)                                   ; unit envelopes U0..U3 (block rate)
400035de(0, S)                                   ; U0: self-FB sine -> A0 0x8000945C, B0 0x80008C60
4000290e(1, S)                                   ; U1: sine (depths 0) -> A1 0x80009580, B1 0x80008D84
4000290e(2, S)                                   ; U2: ROM wave, PM from B1 -> A2 0x800096A4, B2 0x80008EA8
40003df2(S)                                      ; modulator envelope S+0x448 (block rate)
400049f4(0x800096A4, 0x80008C60, 0x80009C58, {0,1}, S) ; mix = g0·A2 + g1·B0
40004674(0x80009C58, S)                          ; × modulator envelope, per-block ramp
4000290e(3, S)                                   ; U3: sine, PM from 0x80009C58 -> A3 0x800097C8, B3 0x80008FCC
40004ee6(0x80008BE0, S)                          ; click -> 0x80008BE0, 2 one-pole filters
400047ea(0x80008FCC, S)                          ; drive / duck on B3, in place
400046dc(0x80008BE0, 0x80008FCC, out)            ; out = sats(click + B3)
40003d42(S)                                      ; amp envelope
400045a0(out, S)                                 ; VCA + declick
```
Update 0x40006968(pitch, S, A = P+0x1C) writes the fields (§2.11), then calls `40003cda(S)`
(pitch sweep envelope), then `40002596(pitch, S, A, 4)` (phase increments of U0..U3).

---------------------------------------------------------------------------------------------

## 2. The routines

Sharing below comes from the absolute references to each entry point found in the image (and
the PC-relative ones for 0x40004e6c), mapped to the engine whose render/update range holds
them. Engine names: 0 BD MODERN, 1 SD BASIC, 2 CY ALLOY, 3 PC CARBON, 4 SY TONE, 5 SY CHORD,
6 SD VINTAGE, 7 CP VINTAGE, 8 SY TOY, 9 SY BITS, 10 SY SWARM, 11 SP TWINSHOT.

| Routine | Used by |
|---|---|
| 0x400035de | render 0, 1, 2, 8 |
| 0x4000290e | render 0, 1, 2, 3, 6, 7, 8 |
| 0x40003df2 | render 0, 1, 6 |
| 0x400049f4 | render 0, 1, 2, 3, 7, 9, 11 |
| 0x40004674 | render 0, 1, 6 |
| 0x40004ee6 | render 0 only |
| 0x40004e6c | 0x40004ee6 (engine 0), render 1, 5, 6, 7 |
| 0x400046dc | render 0 only |
| 0x40003cda | update 0, 1, 3, 6; render 5 |
| 0x40002596 | update 0, 1, 2, 3, 6, 7, 8 (0x40002686 is SY TONE's) |

### 2.1 0x400035de(k, S): sine with self-feedback, 32 samples at 48 kHz
The 32-sample counterpart of SY TONE's 0x40003aac. There are four differences: 32 samples,
`>>5` ramps, the feedback taken from the **raw** sample, and no `/2` on the output.

| | |
|---|---|
| U | U_k = S + 0x80k (BD: k = 0) |
| Phase | `S+0x6C+0x80k`, 24-bit |
| Increment | U+0x78 |
| Wave | `T = *(U+0xB0)`, 257 × Q31 (BD: RAM sine 0x80004B70) |
| Output amount | starts at U+0x80, `astep = sats(U+0x7C − U+0x80) >> 5` (asr, no rounding), `a += astep` with no saturation |
| Feedback amount | starts at `fb = S+0x30`, target `tgt = frac(0x04000000 · S+0x2C)` (= S+0x2C/32), `fstep = (tgt − S+0x30) >> 5` (no sats on either the sub or the adds) |
| Feedback sample | `S+0x28` (the previous block's last raw sample) |
| Buffers | A = `*(U+0xC8)` and B = `*(U+0xCC)`, written **from their base** (no +0x20 header): BD's A0 = 0x8000945C, B0 = 0x80008C60 |

Per sample (n = 0..31), registers as in the code:
```
acc1 = phase<<8 ; acc1 += 2·fb_n·y >> 24      ; y = S+0x28 for n=0, else s_{n-1}
p  = sat32(acc1>>8)          (move.l acc1,d2, no clear)
i  = (p & 0xFFFFFF) >> 16                   ; 0..255
f  = (sat32(acc1>>8) & 0xFFFF) << 15        (movclr acc1)
acc0 = T[i]<<8 ; acc0 += 2·f·T[i+1]>>24 ; acc0 -= 2·f·T[i]>>24
phase = (phase + inc) & 0xFFFFFF
s_n = movclr acc0                           ; saturated
acc0 -= 2·a_n·s_n >> 24 ; A[n] = movclr acc0 ; a_{n+1} = a_n + astep
B[n] = s_n
```
- fb_n = S+0x30 + n·fstep: sample 0 uses S+0x30, and the step is added at the top of each
  iteration, before the product for the next sample.
- The final acc1 (sample 32's phase) is discarded by `movclr acc1,a2`.
- d5 = 0x6487ED51 (π/4) is loaded and never used.

End of block: `S+0x28 = s_31`, phase written back, `U+0x80 = U+0x7C`, `S+0x30 = tgt`.

BD: U0 self-modulates with `S+0x2C = interp(0x4001FC00, w6)` (−0.0275 … −0.110, so the
feedback ranges from −0.00086 to −0.0034 of full scale per unit of s, i.e. up to 0.44 cycle).
The mix uses B0. A0 is computed and nothing in BD reads it.

### 2.2 0x4000290e(k, S): oscillator with two phase-modulation inputs, 32 samples at 48 kHz
The 32-sample counterpart of SY TONE's 0x40002c16, with three differences: the depth targets
are U+0x8C/U+0x90 **as is (no ×2)**, the ramps are `>>5`, and B = s with no `/2`.

| | |
|---|---|
| Sources | `src1 = *(U+0x9C)`, `src2 = *(U+0xA0)` (base addresses, no +0x20) |
| Depth 1 | d1 from U+0x94, `step1 = (U+0x8C − U+0x94) >> 5` |
| Depth 2 | d2 from U+0x98, `step2 = (U+0x90 − U+0x98) >> 5` |
| Amount | a from U+0x80, `astep = (U+0x7C − U+0x80) >> 5` |
| Wave | `T = *(U+0xB0)` |
| Outputs | A = `*(U+0xC8)`, B = `*(U+0xCC)` |

**No sats** on any of these subs or ramp adds. Note that `astep` here is not saturated, unlike
2.1: `U+0x7C = 0, U+0x80 = 0x80000000` gives a difference that wraps to 0x80000000.

Per sample, the loads interleaved exactly as in the code (`mac` with load reads the next
operand into d3 while multiplying the old d3):
```
before loop: acc1 = phase<<8 ; d3 = src1[0] ; acc1 += 2·d1_0·d3>>24 ; d3 = src2[0]
n = 0..31:
  acc1 += 2·d2_n·src2[n] >> 24 ; d1 += step1 ; d2 += step2
  p = sat32(acc1>>8) ; i = (p & 0xFFFFFF) >> 16 ; f = (movclr acc1 & 0xFFFF) << 15
  acc0 = T[i]<<8 ; acc0 += 2·f·T[i+1]>>24 ; acc0 -= 2·f·T[i]>>24
  phase = (phase + inc) & 0xFFFFFF ; acc1 = phase<<8
  s = movclr acc0
  acc0 -= 2·a_n·s>>24          (loads src1[n+1]) ; a += astep
  B[n] = s
  acc1 += 2·d1_{n+1}·src1[n+1]>>24   (loads src2[n+1])
  A[n] = movclr acc0
end: movclr acc1 (discarded)
```
- The phase sum `phase + d1·src1 + d2·src2` is truncated once, at 40-bit precision. It
  saturates only if it leaves the int32 range. The `& 0xFFFFFF` then wraps negative sums.
- src1[32] and src2[32] are read and their products discarded.

End of block: phase written back, `U+0x80 = U+0x7C`, `U+0x94 = U+0x8C`, `U+0x98 = U+0x90`.

BD's three uses:

| k | wave | src1 / depth1 | src2 / depth2 | outputs used |
|---|---|---|---|---|
| 1 | sine 0x80004B70 | A0 (init pointer), depth 0 | A0, depth 0 | B1 (0x80008D84) → U2 |
| 2 | **ROM 0x4001E7FC** | B1 0x80008D84, depth `0xFFE3D70A` (−1 845 494 = −0.110 cycle at full scale), no ramp | A0, depth 0 | A2 (0x800096A4) → mix |
| 3 | sine | mix 0x80009C58, depth `0x00FFFFFF` (≈1.0 cycle = 2π at full scale), no ramp | A0, depth 0 | B3 (0x80008FCC) → body |

- The zero depths contribute exactly 0. For U1, U1+0x94/0x98 stay at init's 0, since BD
  writes U1+0x8C = U1+0x90 = 0 every block.
- U2's depth is `frac(0x01000000 · 0xF1EB851F)`, U3's `frac(0x01000000 · 0x7FFFFFFF)`. Both
  are computed in update, and update writes the current depth (U+0x94) equal to its target.

### 2.3 0x40003df2(S): modulator envelope (S+0x448), block rate
Same structure as 0x40003c0e on a single envelope at fixed S offsets, with one difference: a
**trig with an instant attack enters stage 0**, not 1.

| Field | Meaning |
|---|---|
| S+0x448 | L (negative representation, 0x80000000 = full) |
| S+0x44C | ramp start for 0x40004674 |
| S+0x450 | stage (I: 1) |
| S+0x454 | delay counter |
| S+0x458 | attack increment / start (I and BD: 0x80000000) |
| S+0x45C | decay coefficient c (negative) |
| S+0x460 | delay length in blocks |
| S+0x464 | decay target b |

1. Stage step:
   - **3 (delay)**: `++S+0x454`. When `S+0x454 ≥ S+0x460` (signed): `L = S+0x458`, halved
     (asr 1) unless it is 0x80000000, and stage 0.
   - **0 (attack)**: if `L == 0x80000000`, stage 1. Otherwise `L = sats(L + S+0x458)`.
   - **4**: hold.
   - **other (1, 2), decay**: `L = −frac(L·c) + (−frac(b·(c + 0x7FFFFFFF)))`. Two separate
     msac/movclr, then a plain `add.l`, no sats. This is 3c0e's formula.
2. Trig (S+0x38), after the step:
   - If S+0x460 ≠ 0: stage 3, counter 0, L = 0, return.
   - Else if `S+0x458 == 0x80000000`: `L = S+0x44C = 0x80000000`.
   - Else `L = S+0x458 >> 1` (S+0x44C untouched).
   - Then stage 0.

BD, from the trig on:

| Block | Stage | L |
|---|---|---|
| start block | 0 | full |
| next block | 0 → 1 | stays full |
| from the third block | 1 (decay) | `L ← a·L − (1−a)·b`, with a = −c |

c = T_F600[b10] (on trig), b = interp(0x4001FA00, w10) (every block).

### 2.4 0x400049f4(x, y, out, idx[2], S): two-input mixer with ramped gains, 32 samples
- Gains: `G0 = S+0x08+4·idx[0]` (target) with current `S+0x18+4·idx[0]`, and
  `G1 = S+0x08+4·idx[1]` with current `S+0x18+4·idx[1]`. BD passes idx = {0, 1}: g0 = S+0x08
  (current S+0x18), g1 = S+0x0C (current S+0x1C).
- Steps: `st0 = (S+0x08 − S+0x18) >> 5`, `st1 = (S+0x0C − S+0x1C) >> 5`. No sats, plain adds.
- Per sample: `acc0 += 2·g0_n·x[n]>>24 ; acc0 += 2·g1_n·y[n]>>24 ; out[n] = movclr acc0`.
  There is one truncation and one saturation per sample, and x[32] is read and dropped.
- End: `S+0x18 = S+0x08`, `S+0x1C = S+0x0C`.
- BD: x = A2 (U2 scaled), y = B0 (U0 raw), out = 0x80009C58.

### 2.5 0x40004674(buf, S): VCA on the modulator envelope, in place, 32 samples
The 0x400045a0 pattern on the S+0x448/0x44C pair, with no output buffer.
- If S+0x34 (trig pending): `S+0x448 = 0`, so the mix fades out over this block.
- Else if S+0x38 and S+0x458 == 0x80000000: `S+0x44C = S+0x448` (no ramp on an instant
  attack).
- `g = S+0x44C`, `step = sats(S+0x448 − g) >> 5`.
- Per sample: `buf[n] = movclr(acc0 − 2·g·buf[n]>>24)` (i.e. `−frac(g·x)`, with g negative),
  then `g = sats(g + step)`. The next sample is fetched by the msac's load from `4(a2)`, and
  buf[32] is read and dropped.
- End: `S+0x44C = S+0x448`.

### 2.6 0x40004ee6(buf, S): click generator, then two one-pole filters. BD only.
- On trig (S+0x38): `S+0x470 = 0x7FFFFFFF`.
- `st = (S+0x474 − S+0x478) >> 5` (no sats).
- Per sample n = 0..31, re-reading S+0x478 from memory each time:
  ```
  buf[n] = frac(S+0x478 · S+0x470)
  S+0x470 = max(sats(S+0x470 − 0x004468C0), 0)     ; addi #0xFFBB9740, sats, clamp <0 to 0
  S+0x478 += st                                     ; no sats
  ```
  The ramp goes from 0x7FFFFFFF to 0 in 479 samples (≈10 ms, 15 blocks), then stays at 0
  until the next trig. Sample 0 of the start block uses the full value.
- End: `S+0x478 = S+0x474`.
- Then `0x40004e6c(buf, *(S+0x468))`, then a tail call `0x40004e6c(buf, *(S+0x46C))` (it
  rewrites its own argument slots and `bra.w`). Init sets S+0x468 = S+0x47C and
  S+0x46C = S+0x49C.

### 2.7 0x40004e6c(buf, C): first-order IIR in place, 32 samples, 2 per iteration
- Coefficients: `c0 = C[0], c1 = C[1], c2 = C[2]`. State: `x1 = C[6]` (C+0x18), `y1 = C[7]`
  (C+0x1C).
- `y[n] = c0·x[n] + c1·x[n−1] − c2·y[n−1]`, accumulated in one acc and truncated once:
  ```
  acc0 += 2·c1·x1>>24 (load x0) ; acc0 += 2·c0·x0>>24
  loop 16×:  acc0 -= 2·c2·y_prev>>24
             acc1 += 2·c1·x[n]>>24 (load x[n+1]) ; acc1 += 2·c0·x[n+1]>>24
             C[6] = x[n+1] ; y_n = movclr acc0 ; buf[n] = y_n
             acc1 -= 2·c2·y_n>>24
             acc0 += 2·c1·x[n+1]>>24 (load x[n+2]) ; acc0 += 2·c0·x[n+2]>>24
             y_{n+1} = movclr acc1 ; buf[n+1] = y_{n+1}
  end: C[7] = y_31 ; movclr acc0 (discards the terms of x[32], which it read)
  ```
  The x values are always fetched before their slot is overwritten. C[6] ends as x[31].
- BD's two filters, coefficients written by update every block:

  | Filter | C | c0 | c1 | c2 | Response |
  |---|---|---|---|---|---|
  | 1 | S+0x47C | 0x7DF07773 | 0x820F888D | 0x841F111A (−0.96781) | bilinear first-order HP, fc ≈ 250 Hz |
  | 2 | S+0x49C | 0x17DB49CB | 0x17DB49CB | 0xAFB69397 (−0.62725) | bilinear first-order LP, fc ≈ 3.44 kHz |

  The states are S+0x494/0x498 (filter 1) and S+0x4B4/0x4B8 (filter 2). They are cleared on
  trig by update, and also by 0x4000255e.

### 2.8 0x400046dc(a, b, out): sum, 32 samples. BD only.
`out[n] = sats(b[n] + a[n])`, integer, no EMAC. BD: a = click 0x80008BE0, b = B3 0x80008FCC.

### 2.9 0x40003cda(S): pitch-sweep envelope, block rate
| Field | Meaning |
|---|---|
| S+0x400 | sweep level e (positive, 0x7FFFFFFF = full) |
| S+0x408 | stage (0 attack, 1 decay; I: 1) |
| S+0x40C | attack step (BD: 0x80000000 = instant, set on trig) |
| S+0x410 | decay coefficient (negative) |
| S+0x3FC | "start at half" flag (I: 1) |

1. On trig (S+0x38):
   - If `S+0x40C == 0x80000000`: `e = 0x7FFFFFFF`.
   - Else if `S+0x3FC ≠ 0`: `e = (−S+0x40C) >> 1`.
   - (Else e is unchanged.)
   - Then `S+0x408 = 0`.
2. If S+0x408 == 0:
   - If `e == 0x7FFFFFFF`: S+0x408 = 1 and return. In BD the start block keeps e full.
   - Else `e = sats(e − S+0x40C)`.
3. Else: `e = frac(e · (−S+0x410))`, one exponential step per block.

BD: S+0x410 = T_EE00[b8] (−0.930 … −0.987), so e decays by 0.930 to 0.987 per block.

### 2.10 0x40002596(pitch, S, A, n): phase increments of units 0..n−1, 48 kHz
Same arithmetic as 0x40002686 (`sytone_notes.md` §2.8), with two differences: the unit count n
is an argument (BD: 4), and the final constant is **0x57619F10** (= 2 × 0x2BB0CF88, the
48 kHz scale). The pitch-mod factor m is recomputed inside the loop (same value each time).
1. `p = sats(S+0x40 + frac(S+0x44 · (w(A+0x24) << 16)))`. BD: S+0x40 = 0xFFA80000 (−88
   semitones), S+0x44 = 0x00800000, so `p = 256·w2 − 88·65536`.
2. `p = sats(p + pitch)`, clamped to [0, 0x7F0000].
3. `q = frac((p − 0x450000) · 0x0AAAAAAB)`. oct = signed high half of q, fr = low 16 bits.
4. `f = frac(E[fr >> 5] · 0x6E000000) >> (5 − oct)`, with E = RAM 0x8000A888.
5. `m = frac((S+0x3F8 − 0x04000000) · S+0x400) + 0x04000000`.
6. For each unit:
   - `t = sats(2 · (frac(frac((U+0x50 >> 6) · m) · U+0x64) << 11))`. The `lsl #11` does not
     saturate. The doubling does.
   - `U+0x78 = frac(frac(t · sats(U+0x68 + f)) · 0x57619F10) >> 2`.

BD:

| Unit | U+0x50 | Frequency |
|---|---|---|
| U0, U1, U2 | 0x08000000 | 2 × the note |
| U3 | 0x04000000 | the note |

- Check: U+0x50 = 1/32 at note 69 with no sweep gives inc = 153 791 = 440·2²⁴/48000.
- The sweep multiplies every unit's frequency by 32·m, from 32·T_EC00[b8] at the trig down to
  1 as e decays.
- **Saturation**: t ≈ 32·F·m in Q31. For F = 1/16 it saturates once m ≥ 0.5, so U0..U2's sweep
  is clamped at 16× while U3's reaches 21.4×. T_EC00 exceeds 0x40000000 from index 112.
  Keep the `sats` for bit-exactness.

### 2.11 Update 0x40006968(pitch, S, A). BD only.
`interp(T, w)` = 0x40004dc6, called through a4. "w k" / "b k" = word / high byte of machine
byte k, at A+0x22+k.

**Every block, before the trig test:**

| Destination | Value |
|---|---|
| S+0x40, S+0x44 | 0xFFA80000, 0x00800000 |
| S+0x1D0 | 0x04000000 (U3 frequency factor) |
| S+0x50, S+0xD0, S+0x150 | 0x08000000 (U0..U2 frequency factors) |
| S+0x3F8 | T_EC00[b8] (sweep depth) |
| S+0x474 | interp(0x4001F400, w6) (click amplitude) |
| S+0x08 | interp(0x40020000, w4) (g0, the U2 level in the mix) |
| S+0x0C | interp(0x4001FE00, w6) (g1, the U0 level in the mix) |
| S+0x3F0 | T_F800[b16] (amp decay) |

**On trig (S+0x38 ≠ 0) only:**

| Destination | Value |
|---|---|
| S+0x54, D4, 154, 1D4 | 0x80000000 (unit envelope attacks instant) |
| S+0x158 | 0x8AA5CA3B (U2 envelope decay c, a = 0.9168) |
| S+0x60, E0, 160, 1E0 | 0 (unit envelope delays) |
| S+0x58, D8, 1D8 | 0 (U0, U1, U3 decay c = 0) |
| S+0x28 | 0 (U0 feedback sample) |
| S+0x458 | 0x80000000 |
| S+0x460 | 0 |
| S+0x45C | T_F600[b10] |
| S+0x3E8 | 0x80000000 (amp attack instant) |
| S+0x430, S+0x434 | 0x7FFFFFFF |
| S+0x40C | 0x80000000 |
| S+0x410 | T_EE00[b8] (sweep decay) |
| Drive stage, w12 ≠ 0 | 414/418 = 0x02E3F426, 41C = 0x85C7E84D, 420/424 = 0x004E99D0, 428 = 0x809D339F, 43C = 440 = 0x40000000, 444 = 0x3EB851EC, S+0x48 = 1 |
| Drive stage, w12 = 0 | 41C = T_F200[b16], 414/418 = (T_F200[b16] >> 1) + 0x40000000, 428 = T_F000[b16], 420/424 = (T_F000[b16] >> 1) + 0x40000000, 43C = 0x66666666, 440 = 0x20000000, 444 = 0x370A3D71, S+0x48 = 0 |
| S+0x1C | S+0x0C |
| S+0x18 | S+0x08 |
| S+0x478 | S+0x474 (no ramps at the hit) |
| S+0x494, 498, 4B4, 4B8 | 0 (filter states) |

**Every block, after:**

| Destination | Value |
|---|---|
| S+0x15C | 0x60000000 (U2 envelope target 0.75) |
| S+0x5C, DC, 1DC | 0x7FFFFFFF |
| S+0x2C | interp(0x4001FC00, w6) (U0 feedback) |
| S+0x10C, 110, 190, 210 | 0 |
| S+0x19C | 0x80008D84 (U2 src1 = B1) |
| S+0x18C = S+0x194 | frac(0x01000000·0xF1EB851F) = 0xFFE3D70A |
| S+0x464 | interp(0x4001FA00, w10) |
| S+0x21C | 0x80009C58 (U3 src1 = mix) |
| S+0x20C = S+0x214 | frac(0x01000000·0x7FFFFFFF) = 0x00FFFFFF |
| S+0xB0, B4, 130, 134, 230, 234 | 0x80004B70 (sine) |
| S+0x1B0, 1B4 | 0x4001E7FC (U2 ROM wave) |
| S+0x47C/480/484 | 0x7DF07773 / 0x820F888D / 0x841F111A |
| S+0x49C/4A0/4A4 | 0x17DB49CB / 0x17DB49CB / 0xAFB69397 |

Then `0x40003cda(S)`, then `0x40002596(pitch, S, A, 4)`.

The frac values are computed with `mac` + `movclr` into d0/d2. The stack is unbalanced by 16 B
until the final `lea $34(a7)`, which is why the pitch is read from `$34(a7)`.

### 2.12 Shared stages as BD uses them
- **0x40003c0e(S, 4)**:
  - U0, U1, U3: instant attack and c = 0, so after the trig block
    `L = −frac(0x7FFFFFFF·0x7FFFFFFF) = −0x7FFFFFFE` and is held. These only scale buffers A0,
    A1 and A3, which nothing reads. [INFER] No audible effect.
  - U2: from full toward −0.75 with a = 0.9168 per block (τ ≈ 12 blocks ≈ 8 ms). This scales
    A2, so it is audible through the mix.
- **0x400047ea(B3, S)**:
  - w12 ≠ 0 (S+0x48 = 1):
    - Drive: `SAT32(4·frac(0x40000000·x))` is ×2 with a hard clip, then ×0.49 and × the
      duck gain.
    - The duck gain falls from 1.0 with pole 0.9548 per block. Once it reaches the floor 0.5,
      it switches to stage 5 and recovers toward 1.0 with pole 0.9952.
  - w12 = 0 (S+0x48 = 0):
    - The gain is forced to 0x7FFFFFFF, so the T_F000/T_F200 coefficients written on trig
      have no audible effect.
    - Output: `frac(0x370A3D71 · SAT32(4·frac(0x20000000·x)))`, i.e. ≈0.43·x.
- **0x40003d42 / 0x400045a0**: as in SY TONE, with S+0x3E8 = 0x80000000 (instant) written on
  trig, S+0x3F0 = T_F800[b16] and S+0x3EC = w14 (hold).
- **Pending block** (S+0x34): 0x40004674 zeroes S+0x448 and 0x400045a0 zeroes S+0x3D0, so the
  old hit fades over the pending block and the new one starts 32 samples after the trig. The
  click is not faded separately: it is inside `out` before 0x400045a0.

### ROM tables (section 7, 128 × int32 BE unless noted)

| Address | Read as | Range | Destination |
|---|---|---|---|
| 0x4001E7FC | 257 × Q31 wave (U2) | 16-bit samples (low half 0), ±0.711. Harmonics 1-5 = 0.749, 0.129, 0.064, 0.041, 0.018, none above. Entry 256 = 0 | via S+0x1B0 |
| 0x4001EC00 | b8, direct | 0x04000000 (ratio 1) → 0x55A1F58D (ratio 21.4). Non-monotonic | S+0x3F8 sweep depth |
| 0x4001EE00 | b8, direct | −0.930 … −0.987. Non-monotonic | S+0x410 sweep decay |
| 0x4001F000 | b16, direct, w12 = 0 only | +0.994 at [0..6], then −0.99 … −0.997 | S+0x428 (dead) |
| 0x4001F200 | b16, direct, w12 = 0 only | +0.994 at [0..6], then −0.997 … −0.998 | S+0x41C (dead) |
| 0x4001F400 | w6, interp | 0.064 → 0.430 (max at 97) | S+0x474 click amplitude |
| 0x4001F600 | b10, direct | −0.9978 → −0.7486, increasing | S+0x45C modulator envelope decay |
| 0x4001F800 | b16, direct | −0.9085 → −0.9997, decreasing | S+0x3F0 amp decay |
| 0x4001FA00 | w10, interp | 1.0 → 0.075, decreasing | S+0x464 modulator envelope target |
| 0x4001FC00 | w6, interp | −0.0275 → −0.110, decreasing | S+0x2C U0 feedback (/32 in 2.1) |
| 0x4001FE00 | w6, interp | 0 → −0.177 (at 90) → −0.032 | S+0x0C g1 (U0 level) |
| 0x40020000 | w4, interp | 0 → 0.560, increasing | S+0x08 g0 (U2 level) |

RAM tables: the sine at 0x80004B70 (257) and the exp2 table at 0x8000A888 (2048), as for
SY TONE. Dump them from the emulator after `boot()`.

---------------------------------------------------------------------------------------------

## 3. Machine-parameter bytes read

| Byte k | A offset | Read | Effect |
|---|---|---|---|
| 0 | 0x22 | b (caller) | machine id |
| 2 | 0x24 | w (in 2596) | tune: `256·w − 88·65536` added to the note (Q16), so (w/256 − 88) semitones |
| 4 | 0x26 | w | g0, the level of U2 (ROM wave, U1-modulated) in the modulator mix |
| 6 | 0x28 | w, three times | g1 (U0 level in the mix, table 0x4001FE00), U0 self-feedback (0x4001FC00), click amplitude (0x4001F400) |
| 8 | 0x2A | b | pitch sweep: depth (T_EC00, every block) and decay (T_EE00, on trig) |
| 10 | 0x2C | w and b | modulator envelope: target b (w, every block) and decay coefficient (b, on trig) |
| 12 | 0x2E | `tst.w` ≠ 0 | drive ×2 + duck envelope on, versus clean ×0.43 (on trig) |
| 14 | 0x30 | w (caller) → S+0x3EC | amp hold until gate-off when ≠ 0 |
| 16 | 0x32 | b | amp decay coefficient (every block). With w12 = 0, also the dead drive coefficients on trig |

Bytes 18-19 and the odd low bytes of 8, 16 are not read.

[INFER] Names, unconfirmed against CPU #1's mapping. Section 3 holds "Sweep Depth",
"Transient", "CONTOUR" and "SWEEP" near the machine list. Guesses:

| Byte | Guess |
|---|---|
| 2 | TUNE |
| 16 | DECAY |
| 14 | HOLD |
| 8 | SWEEP (depth and time on one knob) |
| 10 | CONTOUR (how fast and how far the brightness falls) |
| 6 | TRANSIENT / character (click level + U0 tone) |
| 4 | a second tone / harmonic level |
| 12 | an on/off switch for punch/drive |

---------------------------------------------------------------------------------------------

## 4. Fields of S and units used by BD MODERN

Writer/reader key:
- C = caller 0x40004324, I = init 0x40003ee0, Z = 0x4000255e, U = update 0x40006968.
- E = 3c0e, F = 35de, O = 290e, M = 3df2, X = 49f4, V2 = 4674, K = 4ee6, L = 4e6c, Q = 3cda,
  P = 2596, D = 47ea, AE = 3d42, V = 45a0.

### Voice-level fields
| Off | Meaning | W | R |
|---|---|---|---|
| +0x08 | g0 target (U2 level) | U | X |
| +0x0C | g1 target (U0 level) | U | X |
| +0x18 | g0 current | X, U (trig) | X |
| +0x1C | g1 current | X, U (trig) | X |
| +0x28 | U0 last raw sample (feedback) | F, U (trig 0), I | F |
| +0x2C | U0 feedback target (/32 in F) | U | F |
| +0x30 | U0 feedback current | F, I | F |
| +0x34 / +0x38 / +0x3C | pending / start / gate-off | C | V2, V, AE / E, M, V2, K, Q, U, D, AE, V / AE |
| +0x40, +0x44 | tune offset −88 st, tune scale | U | P |
| +0x48 | drive envelope enable | U (trig) | D |
| +0x3D0..+0x3F0 | amp envelope (as SY TONE); +0x3E8 = 0x80000000 on trig | AE, V, U, C | AE, V |
| +0x3F8 | sweep depth T_EC00[b8] | U | P |
| +0x3FC | sweep half-start flag (I: 1) | I | Q |
| +0x400 | sweep level e | Q | P, Q |
| +0x408 | sweep stage | Q, I | Q |
| +0x40C | sweep attack step (0x80000000) | U (trig), I | Q |
| +0x410 | sweep decay coefficient | U (trig), I | Q |
| +0x414..+0x444 | drive/duck (as SY TONE) | U (trig), D, I | D |
| +0x448 | modulator envelope L | M, V2 (0 on pending) | M, V2 |
| +0x44C | modulator VCA ramp start | M (trig), V2 | V2 |
| +0x450 | modulator envelope stage | M, I(1) | M |
| +0x454 | delay counter | M | M |
| +0x458 | attack (0x80000000) | U (trig), I | M, V2 |
| +0x45C | decay coefficient | U (trig) | M |
| +0x460 | delay length (0) | U (trig) | M |
| +0x464 | decay target | U | M |
| +0x468 / +0x46C | pointers to filter 1 / 2 (= S+0x47C / S+0x49C) | I | K |
| +0x470 | click ramp level | K | K |
| +0x474 | click amplitude target | U | K, U |
| +0x478 | click amplitude current | K, U (trig) | K |
| +0x47C..+0x484 | HP coefficients | U | L |
| +0x494 / +0x498 | HP x1 / y1 | L, U (trig), Z, I | L |
| +0x49C..+0x4A4 | LP coefficients | U | L |
| +0x4B4 / +0x4B8 | LP x1 / y1 | L, U (trig), Z, I | L |

### Per-unit fields (U_k = S + 0x80k, k = 0..3)
| Off in U | U0 | U1 | U2 | U3 | W | R |
|---|---|---|---|---|---|---|
| +0x50 freq factor | 1/16 | 1/16 | 1/16 | 1/32 | U | P |
| +0x54 env attack | 0x80000000 | = | = | = | U (trig) | E |
| +0x58 env c | 0 | 0 | 0x8AA5CA3B | 0 | U (trig) | E |
| +0x5C env b | 0x7FFFFFFF | = | 0x60000000 | 0x7FFFFFFF | U | E |
| +0x60 env delay | 0 | = | = | = | U (trig) | E |
| +0x64 / +0x68 freq scale / offset | init 0x40000000 / 0 | | | | I | P |
| +0x6C phase | | | | | F/O, Z, I | F/O |
| +0x78 increment | | | | | P | F/O |
| +0x7C / +0x80 env level / ramp start | | | | | E, F/O | F/O |
| +0x8C / +0x94 depth 1 target / current | — | 0 / 0 | 0xFFE3D70A both | 0x00FFFFFF both | U, O | O |
| +0x90 / +0x98 depth 2 | — | 0 | 0 | 0 | U, O | O |
| +0x9C src1 | — | 0x8000945C (I) | 0x80008D84 | 0x80009C58 | U, I | O |
| +0xA0 src2 | — | 0x8000945C (I) | = | = | I | O |
| +0xB0 wave | sine | sine | 0x4001E7FC | sine | U | F/O |
| +0xC8 / +0xCC A / B buffer | 945C / 8C60 | 9580 / 8D84 | 96A4 / 8EA8 | 97C8 / 8FCC | I | F/O |

### Global scratch used by BD
| Buffer | Content |
|---|---|
| B0 0x80008C60 | U0 raw, mixed |
| A0 0x8000945C | U0 scaled, unused (it is the zero-depth src2 everywhere) |
| B1 0x80008D84 | U1 raw, U2's modulator |
| A1 0x80009580 | unused |
| A2 0x800096A4 | U2 scaled, mixed |
| B2 0x80008EA8 | unused |
| 0x80009C58..0x80009CD7 | modulator mix, then U3's modulator (SY TONE's decimator uses this region too) |
| B3 0x80008FCC | U3 raw = body |
| A3 0x800097C8 | unused |
| 0x80008BE0..0x80008C5F | click |

These 32-sample routines write the buffers from their base: there is no 8-long header and no
+0x20 offset as in SY TONE's 64-sample oscillators.

---------------------------------------------------------------------------------------------

## 5. Porting order

1. **Leaves without state**:
   - 0x400046dc (sats sum).
   - 0x40004e6c (first-order IIR, with its acc0/acc1 interleave and single truncation).
   - 0x400049f4 (ramped two-gain mix).
   - 0x40004674 (ramped VCA).
   Test them on synthetic buffers.
2. **Block-rate envelopes**:
   - 0x40003df2: note that a trig enters stage 0, unlike 3c0e.
   - 0x40003cda (pitch sweep).
   0x40003c0e, 0x40003d42 and 0x400045a0 are already ported.
3. **0x40004ee6** (click ramp + 2 × 4e6c, through the S+0x468/0x46C pointers).
4. **0x40002596**: 0x40002686 with n and 0x57619F10. Keep the non-saturating `lsl #11` and the
   saturating doubling. Test U0..U2 at T_EC00[≥112] for the 16× clamp.
5. **0x400035de**, then **0x4000290e**: need the RAM sine, the ROM wave 0x4001E7FC (257
   longs), and the buffer pointers. Check B = s (no /2), the feedback taken from the raw
   sample, and the 40-bit single truncation of the phase sum.
6. **Update 0x40006968**, then **render 0x40006c9e**. Suggested test: one voice, trig, 50
   blocks, w12 both ways, b8 = 0 and 127, against `tools/emu/cli.py render --machine 0`.
