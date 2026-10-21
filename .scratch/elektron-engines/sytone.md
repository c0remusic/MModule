# SY TONE (engine 4), Syntakt OS 1.41, voice CPU (section 7)

Sources: `dis/sytone_render.s`, `dis/sytone_update.s`, `dis/voices_render.s`, and further
disassembly of 0x40000934 (audio ISR), 0x40003ee0 (voice init), 0x4000255e, 0x40004dc6,
0x40002770, boot loop 0x40000fd6. RAM tables were read from the Unicorn emulator after
`Engine.boot()`. Statements without a tag come straight from the code. **[INFER]** marks
interpretation.

Disassembler caveats (cfdis.py):
- It drops the index scale when the displacement is nonzero. `move.l $4(a5, d2.l), d2` at
  0x40002d26 and `movea.l $4(a5, d2.l), a3` at 0x40003ba8 have extension word 0x2C04, which is
  scale x4: `4(a5,d2.l*4)`, the next table entry.
- It leaves `mvs`/`mvz` with indexed or postincrement EA as `dc.w`, and decodes their extension
  word as a separate instruction. In 0x40004324:
  - 0x40004388 `7901` = `mvs.b d1,d4`; 0x4000438a `7931 4800` = `mvs.b 0(a1,d4.l),d4`
    (the `nbcd.b d0` line is that extension word).
  - 0x4000438e `7702` = `mvs.b d2,d3`; 0x40004390 `7731 3800` = `mvs.b 0(a1,d3.l),d3`.
  - 0x400043b6 `751b` = `mvs.b (a3)+,d2`.
  - 0x400043f4 `7332 583e` = `mvs.b $3e(a2,d5.l),d1`.
  - 0x4000440c `7930 1800` = `mvs.b 0(a0,d1.l),d4` (the `move.b d0,d4` line is the extension).
  - 0x4000444c `7b72 584c` = `mvs.w $4c(a2,d5.l),d5`. The `addq.w #4,a4` line is that
    extension word, so a4 stays 0x80000000.

EMAC mode: 0x40004324 writes `MACSR = 0xA0` (OMC | F/I: signed fractional, saturating,
truncating, RT=0) at 0x4000433c, before any engine runs. It writes `MACSR = 0x20` back at
0x400044d2. No engine routine below writes MACSR, so every `mac`/`msac` described here is
signed Q31 fractional with saturation on `move acc,Rx`. "frac(a·b)" below means the EMAC
fractional product `(a·b·2) >> 32` (truncated). `move.l Ry,ACC` loads `Ry<<8` (40-bit
fractional alignment). `move.l ACC,Rx` returns `ACC>>8` saturated to 32 bits (see
`tools/emu/emac.py` `load_acc` / `store_acc`).

---------------------------------------------------------------------------------------------

## 1. Calling convention: ISR → 0x40004324 → update / render

### Audio ISR 0x40000934 (per 32-sample block)
1. `memcpy(0x80008000, 0x00000000, 0x1B0)`: copies the shared block from CPU #1 (0x400010e4 is
   memcpy(dst, src, n)).
2. For t = 0..7, scatters the block into a **per-track parameter array at 0x80003C40, stride
   0x8E** (142 B, the same layout as CPU #1's 0x800021B0+142·t, word offset 28 + 2·internal id):
   - `memcpy(P+0x3E, 0x80008000+20·t, 20)`: the 20 machine bytes. Byte P+0x3E is the
     machine id.
   - P+0x52 ← blk[0xA0+8t], P+0x56 ← [0xA2+8t], P+0x66 ← [0xA4+8t], P+0x68 ← [0xA6+8t]
     (words).
   - P+0x6C,+0x6E,+0x70,+0x72,+0x74,+0x80,+0x82 ← blk[0xE0+14t … 0xEC+14t] (words).
3. trig = `byte[0x80008180] & ~word[0x80008182]`. gateoff = `byte[0x80008181] | word[0x80008182]`.
   [INFER] 0x181 holds note-off bits, and 0x182 is a kill/mute mask that blocks trigs and forces
   release.
4. `0x40004324(out = 0x80003840, params = 0x80003C40, trig, gateoff)`. Output: 8 × 32 × int32,
   voice v at 0x80003840 + 128·v. The ISR then post-processes it (0x4000112c, 0x40002320,
   0x400019d6 per voice, 0x4000168e) and DMAs it out. These are per-track stages outside the
   engine (filter, amp and so on, [INFER]) and not part of SY TONE.

### 0x40004324(out, params, trig, gateoff): stack args at 0x34/0x38/0x3C/0x40(a7) after `lea -$30`
- **Voice state struct**: `S = 0x80000000 + 0x708·v` (8 × 0x708 = 0x3840, ending at the output
  buffer).
- **Render order**: a voice-index list at 0x80004A48[0..7] (= 0..7) is bubble-sorted, stable
  and ascending, on key 0x80004A40[v] = machine byte P_v+0x3E. Voices are rendered grouped by
  machine id. [INFER] The grouping is for cache locality, or because the engines share scratch
  buffers (see §3, global scratch).
- Per voice v, in sorted order (d3 = 0x708·v, d5 = 0x8E·v):
  1. `S+0x34 = (trig >> v) & 1` (trig arriving this block). `S+0x3C = (gateoff >> v) & 1`.
  2. If `S+0x38` (trig of the **previous** block) is set: engine = map[clamp(P+0x3E, 0, 45)]
     from 0x40014950 (46 B). If engine ≠ S+4, the code calls `0x40003ee0(S)` (full voice
     init), sets `S+0x38 = 1`, `S+0 = S+4 = engine`. A machine change only takes effect on a
     trig.
  3. `S+0x3EC = (int16)P+0x4C` (sign-extended word, every block, before the engine runs).
  4. `0x4000255e(S)`: if S+0x38, clears S+0x4F4, +0x4F8, +0x494, +0x498, +0x4B4, +0x4B8 and the
     phases `S+0x6C+0x80k` for k = 0..6 (phase reset on trig).
  5. If engine ≤ 11:
     - `update[engine](pitch = *(int32*)(0x80008150+4v), S, P+0x1C)`. Words 0x150+4t of the
       0x1B0 block hold the track's note in semitones Q16 (the emulator's `make_params`
       writes `note<<16` at 0x150). [INFER] They also carry CPU #1's pitch modulation.
     - then `render[engine](out + 128·v, S)`.
  6. `S+0x38 = S+0x34`.
- **One-block trig latency**: a trig is seen as `S+0x34` ("pending") in the block it arrives in,
  and as `S+0x38` ("start now") in the next block. The engines use the pending block to fade
  the voice out (see 400045a0), so every hit starts 32 samples after its trig.

### Arguments
| routine | arg0 | arg1 | arg2 |
|---|---|---|---|
| update 0x4000709c | pitch, Q16 semitones (int32) | S (voice state) | A = P+0x1C: track parameter words, A+2·id = internal id `id` |
| render 0x40007214 | out: int32[32], voice's slot in 0x80003840 | S | — |

The 20 machine bytes sit at P+0x3E..0x51, which is A+0x22..0x35. SY TONE reads them as follows
("w" = signed 16-bit word, value7·256 + fine8; "b" = its high byte, mvs.b). Byte k is byte k of
the 20-byte machine block, i.e. byte 20t+k of the 0x1B0 block.

| P offset | A offset | byte k | read | used for |
|---|---|---|---|---|
| +0x3E | 0x22 | 0 | b (caller) | machine id |
| +0x40 | 0x24 | 2 | w | tune (§2.8) |
| +0x42 | 0x26 | 4 | w | modulator ratio, table 0x40021604 |
| +0x44 | 0x28 | 6 | w | FM index (op0→op1 depth), table 0x40021804 |
| +0x46 | 0x2A | 8 | w | op0 self-feedback, table 0x40021404 |
| +0x48 | 0x2C | 10 | w and b | modulator envelope: sustain/target (w, 0x40021A04) and decay rate (b, 0x40021C04, on trig only) |
| +0x4A | 0x2E | 12 | w ≠ 0 | output drive/duck mode (on trig only) |
| +0x4C | 0x30 | 14 | w (caller) → S+0x3EC | amp hold until gate-off when ≠ 0 |
| +0x4E | 0x32 | 16 | b | amp decay coefficient, table 0x40021E04 |

[INFER] These are the user-facing SY TONE parameters. Matching them to names (TUNE, RATIO,
DEPTH/INDEX, FEEDBACK, DECAY…) needs a check against CPU #1's mapping.

---------------------------------------------------------------------------------------------

## 2. The routines

The "unit" is the sub-structure `U_k = S + 0x80·k` (k = 0..6; voice init 0x40003ee0 sets up 7
of them). SY TONE uses unit 0 as **operator 0 (modulator)** and unit 1 as **operator 1
(carrier)**.

Signal flow per block: two operators run at **96 kHz, 2× oversampled, 64 samples per block**.
Op0 is a sine with self-feedback phase modulation, scaled by its own envelope. Op1 is a sine
phase-modulated by op0. Op1's output (unscaled) is halfband-decimated 2:1 to 32 samples. Then
come an amp envelope with VCA, then a drive/clip stage with a ducking gain envelope.

Call sequence in render 0x40007214:
```
40003c0e(S, 2)                 ; envelopes of units 0 and 1 (block rate)
40003aac(0, S)                 ; op0: feedback sine, 64 samples
40002c16(1, S)                 ; op1: PM sine, 64 samples
400051c6(S, 0x80008D84, out)   ; 2:1 decimator -> out[32]
40003d42(S)                    ; amp envelope step (block rate)
400045a0(out, S)               ; VCA with per-block linear ramp, declick on pending trig
400047ea(out, S)               ; (tail call) drive / clip / ducking gain
```

### 2.1 0x40003c0e(S, n): per-unit AD envelope, block rate. Shared.
Called by the render routines of engines 0, 1, 2, 3, 4, 5, 6, 7, 8 (not 9, 10, 11).
Integer arithmetic except the decay step (EMAC fractional).

Loop k = 0..n−1 over unit U = U_k:
- Fields:
  - U+0x54: attack increment / start level.
  - U+0x58: decay coefficient c (stored negative).
  - U+0x5C: decay target b.
  - U+0x60: delay length in blocks.
  - U+0x7C: **level L**, in negative representation: 0x80000000 = full, 0 = silent.
  - U+0x80: previous level, written here on trig and by the oscillators.
  - U+0x84: stage.
  - U+0x88: delay counter.
- Stage step:
  - **3 (delay)**: `++U+0x88`. When it reaches U+0x60: `L = U+0x54`, stage 0.
  - **0 (attack)**: if `L == 0x80000000`, stage 1. Otherwise `L = sats(L + U+0x54)`, a linear
    ramp toward −1.0 (full).
  - **4**: hold, nothing happens.
  - **1, 2 (decay)**: `L = −frac(L·c) − frac(b·(c + 0x7FFFFFFF))` (plain adds, no saturation).
    With c = −a this is `L ← a·L − (1−a)·b`, a one-pole convergence to −b.
- Trig (`S+0x38`), applied after the step:
  - If U+0x60 ≠ 0: stage 3, counter 0, L = 0.
  - Otherwise: `L = U+0x80 = U+0x54` (no ramp). Stage 1 if U+0x54 == 0x80000000 (instant
    attack), else stage 0.
- SY TONE values:
  - U0: U+0x54 = 0x80000000 and U+0x60 = 0 (init, never changed), so the attack is instant.
    U0+0x58 = c = table 0x40021C04[b(P+0x48)], set on trig. U0+0x5C = b = interp(0x40021A04,
    w(P+0x48)), set every block. U0's level is op0's output amount (FM index envelope).
  - U1: only the init values (instant attack, c = 0 so L is held). It scales op1's output
    buffer A, which nothing in SY TONE reads. [INFER] It has no audible effect here.

### 2.2 0x40003aac(k, S): operator with self-feedback (phase accumulator + sine lookup), 64 samples. SY TONE only.
Called only by 0x40007214.

| | |
|---|---|
| U | U_k (k = 0) |
| Phase | `S+0x6C+0x80k`, 24-bit, masked `& 0xFFFFFF`, at 96 kHz |
| Increment | U+0x78 (from 0x40002686) |
| Wave | `U+0xB0` = 0x80004B70 (init): RAM sine, 257 × Q31, `sin(2π i/256)`, entry 256 = guard (= 0), built at boot. 0x40002770 returns 0x80004B70 or one of 32 tables at 0x40014870 (other engines). |
| Amount (op output level) | ramps linearly from U+0x80 to U+0x7C over 64 samples: step `sats(U+0x7C − U+0x80) >> 6`, rounded toward zero (+63 when negative) |
| Feedback amount | ramps from S+0x30 to target `frac(0x08000000 · S+0x2C)` (= S+0x2C/16), step `(target − S+0x30) >> 6`, arithmetic shift |
| Feedback sample | S+0x28: the last buffer-A sample of the previous block |
| Outputs | buffer B = `*(U+0xCC)+0x20` (= 0x80008C80 for k = 0): raw sine/2. Buffer A = `*(U+0xC8)+0x20` (= 0x8000947C): `−frac(amount · sine/2)`, i.e. sine/2 × \|L\| |

Per sample:
- `acc1 = phase<<8; acc1 += fb·y_prev`. y_prev is the buffer-A sample just written, or S+0x28
  on the first sample.
- `p = acc1 → reg` (saturates if > 0x7FFFFFFF). `i = (p & 0xFFFFFF) >> 16` (8 bits).
  `f = (p & 0xFFFF) << 15` (Q31).
- `s = T[i] + frac(f·T[i+1]) − frac(f·T[i])`, in acc0 (linear interpolation).
- `phase = (phase + inc) & 0xFFFFFF`.
- `B[n] = s >> 1`. `A[n] = −frac(amount · B[n])`. amount and fb advance by their steps.

End of block:
- Writes back S+0x28 = last A sample, the phase, `U+0x80 = U+0x7C`, `S+0x30 = target`.

### 2.3 0x40002c16(k, S): operator with two phase-modulation inputs, 64 samples. SY TONE only.
Same oscillator core as 2.2, with U = U_k (k = 1). Instead of self-feedback it adds two external
modulators:
- Mod sources: `src1 = *(U+0x9C)+0x20` and `src2 = *(U+0xA0)+0x20`.
  - U1+0x9C is set to 0x8000945C every block by update, so src1 = op0's buffer A.
  - U1+0xA0 = 0x8000945C from init.
- Depths:
  - d1 ramps from U+0x94 to 2·U+0x8C (step `>>6`).
  - d2 ramps from U+0x98 to 2·U+0x90.
  - U1+0x8C = S+0x10C, set by update. U1+0x90 = S+0x110 is never set by SY TONE, so it is 0
    and src2 does not contribute.
- Per sample: `acc1 = phase<<8 + d1·src1[n] + d2·src2[n]`, then the same index, fraction and
  interpolation as 2.2.
- Outputs:
  - Buffer B = `*(U+0xCC)+0x20` = **0x80008DA4**: raw sine/2. This is SY TONE's audio.
  - Buffer A = `*(U+0xC8)+0x20` = 0x800095A0: −amount·B, with amount ramping U+0x80→U+0x7C.
    Not read by SY TONE.
- End of block: writes back the phase, `U+0x80 = U+0x7C`, `U+0x94 = 2·U+0x8C`,
  `U+0x98 = 2·U+0x90`.

### 2.4 0x400051c6(S, buf, out): 2:1 polyphase IIR halfband decimator, 96k → 48k. SY TONE only.
Called only by 0x40007214. buf = 0x80008D84 = op1's buffer-B base. It holds 8 longs of header
followed by 64 samples, and the header is filled here.

- History `H = *(S+0x56C)` = 0x80008A60 + 0x30·v, 12 longs per voice, set at boot
  (0x40000fd6 loop). Cleared on SY TONE trig and on init.
- Before the loop:
  - `buf[0..7] = H[0..7]`: the last 8 inputs of the previous block.
  - `0x80009C58[0..3] = H[8..11]`: the last 4 allpass outputs.
- Coefficients:

  | c0 | c1 | c2 | c3 |
  |---|---|---|---|
  | 0x3D91D187 (0.48101) | 0xF3656E2D (−0.09847) | 0x043F7701 (0.03319) | 0xFE7B06ED (−0.01187) |

  Plus 0x7FFFFFFF (≈1) for the direct tap.
- Loop of 16 iterations, each consuming 4 inputs and producing 2 outputs. x = buf[] as longs,
  base index j = 1 + 4m, and y[] is the recursive output stream at 0x80009C68 (y[−4..−1] at
  0x80009C58):
  ```
  y0 = x[j] + c0 x[j+2] + c1 x[j+4] + c2 x[j+6] + c3 x[j+8]
       − (c3 y[−4] + c2 y[−3] + c1 y[−2] + c0 y[−1])
  y1 = x[j+2] + c0 x[j+4] + c1 x[j+6] + c2 x[j+8] + c3 x[j+10]
       − (c3 y[−3] + c2 y[−2] + c1 y[−1] + c0 y0)
  out[2m]   = sats(x[j+1] + y0)
  out[2m+1] = sats(x[j+3] + y1)
  ```
  In order: acc0 accumulates y0, acc2 accumulates y1, and `y` advances by 2.
  [INFER] The y recursion is a 4th-order allpass on one polyphase branch. The other branch is a
  pure delay, and the sum is the classic polyphase IIR halfband. There is no ½ factor because the
  oscillators already output sine/2.
- After the loop:
  - `H[0..7] = buf[64..71]` (the last 8 inputs).
  - `H[8..11] = (y[28..31] + 1) & ~1` (0x80009CD8..E7). This rounding quirk must be kept for
    bit-exactness.
- EMAC: fractional, `mac`/`msac` with parallel loads, accumulators 0 and 2.

### 2.5 0x40003d42(S): amp envelope, block rate. Shared.
Called by all 12 render routines except engine 7 (0x40008988).

| Field | Meaning |
|---|---|
| S+0x3D0 | level A (negative representation) |
| S+0x3DC | stage |
| S+0x3E0 | hold counter |
| S+0x3E4 | release request |
| S+0x3E8 | attack increment (init 0x80000000 = instant) |
| S+0x3D8 | "half start" flag (init 1) |
| S+0x3EC | hold mode |
| S+0x3F0 | decay coefficient (negative) |

1. On trig (S+0x38):
   - `S+0x3E4 = 0`.
   - If `S+0x3E8 == 0x80000000`: `A = 0x80000000`, `S+0x3E0 = 0`. Otherwise, if S+0x3D8 ≠ 0:
     `A = S+0x3E8 >> 1`.
   - `S+0x3DC = 0`.
2. Stage step:
   - **0**:
     - If A == 0x80000000: stage 2 (hold, S+0x3E0 = 0) when `S+0x3E4 == 0 && S+0x3EC ≠ 0`,
       else stage 1.
     - Otherwise: `A = sats(A + S+0x3E8)`.
   - **2**: if S+0x3E4 is set, stage 1 and S+0x3E4 = 0. Then `++S+0x3E0`. The counter is not
     compared here. [INFER] It is unused, or read elsewhere.
   - **other (1)**: `A = −frac(A · S+0x3F0)`, exponential decay.
3. `if (S+0x3C && !S+0x34) S+0x3E4 = 1`: gate-off without a new trig requests release.

SY TONE: S+0x3E8 is never written, so the attack is instant. S+0x3F0 = table
0x40021E04[b(P+0x4E)] is written by update every block. S+0x3EC = w(P+0x4C) is written by the
caller.

### 2.6 0x400045a0(out, S): VCA with a per-block linear ramp and declick. Shared.
Called by the render routines of all engines except 7 (0x40008988); engine 10 (0x400096e8) reaches it through `jmp`.

- If S+0x34 (trig pending): `A = S+0x3D0 = 0`, so the voice fades to silence over this block.
- Else, if S+0x38 and S+0x3E8 == 0x80000000: `S+0x3D4 = A`, so there is no ramp at an instant
  attack.
- `g = S+0x3D4`, `step = sats(A − g) >> 5`.
- For 32 samples: `out[n] = −frac(g · out[n])`, then `g = sats(g + step)`.
- `S+0x3D4 = A`.

### 2.7 0x400047ea(out, S): drive / hard clip with a ducking gain envelope. Shared.
Called by engines 0, 1, 2, 3, 4, 5, 6, 7, 8.

**Gain envelope, block rate.** The filter is first order: `y = c0·x + c1·x1 − c2·y1`.
- On trig: `S+0x438 = 1`.
- If stage == 1: coefficient set = S+0x414..0x41C, input x = 0. Otherwise: set =
  S+0x420..0x428, x = 0x7FFFFFFF. S+0x42C points at the active set.
- `y = frac(c1·S+0x430) + frac(c0·x) − frac(c2·S+0x434)` (adds without saturation).
- If `S+0x43C ≥ y`: stage 5, `y = S+0x43C` (floor).
- If S+0x48 == 0: `S+0x434 = y = 0x7FFFFFFF` (envelope off, unity).

SY TONE update writes, on trig:

| | c0 | c1 | c2 | pole |
|---|---|---|---|---|
| S+0x414/418/41C | 0x012C0F26 | 0x012C0F26 | 0x82581E4B (−0.98169) | 0.9817 per block, falls toward 0 |
| S+0x420/424/428 | 0x001F7C5C | 0x001F7C5C | 0x803EF8B8 (−0.99808) | 0.9981 per block, slow recovery toward 1.0 |

`S+0x430 = S+0x434 = 0x7FFFFFFF`.

**Per sample, 32 samples.**
- `g` ramps linearly from the old S+0x434 to y (step `(y − old) >> 5`, no saturation).
- `out[n] = frac( frac(g · S+0x444) · SAT32(4 · frac(S+0x440 · x[n])) )`.
- The ×4 comes from four identical `mac` into acc0. The 48-bit acc holds the result and
  `movclr` saturates it to ±1, so this is a hard clip.

**SY TONE mode**, from w(P+0x4A), on trig:

| w(P+0x4A) | S+0x43C | S+0x440 | S+0x444 | S+0x48 |
|---|---|---|---|---|
| ≠ 0 | 0x20000000 | 0x60000000 (drive ×3) | 0x20000000 | 1 |
| = 0 | 0x7FFFFFFF | 0x20000000 (×1) | 0x20000000 | 0 |

- Mode on: the gain envelope falls from 1.0 to 0.25 (≈75 blocks), then recovers slowly.
- Mode off: the output is a flat 0.25·x.
- The init defaults differ (S+0x43C = 0x66666666, S+0x444 = 0x7FFFFFFF). SY TONE update
  overwrites them on every trig.
- [INFER] Mode on is a transient "punch" / saturation option. The 0.25 output gain is presumably
  compensated downstream.
- After the loop: `S+0x430 = x`, `S+0x434 = y`.

### 2.8 Update 0x4000709c(pitch, S, A) with tail call 0x40002686. SY TONE only.
Called only through table entry 0x40014930.

Every block:
- `S+0x3F0 = T_1E04[b(A+0x32)]` (P+0x4E): amp decay coefficient.

On trig (S+0x38), then falls through to the common part:
- `S+0x28 = 0` (feedback sample).
- Drive and envelope constants of 2.7, and S+0x48.
- `U0+0x58 = S+0x58 = T_1C04[b(A+0x2C)]`: modulator decay coefficient.
- Clears the 12 longs of the decimator history `*(S+0x56C)`.

Common, every block. `interp(T, w)` = 0x40004dc6:

| Destination | Value | Meaning |
|---|---|---|
| S+0x2C | interp(0x40021404, w(A+0x2A)) | feedback target, 0 .. 0.25, then /16 in 2.2 |
| S+0x40 | 0xFFC00000 | tune offset −64 semitones (Q16) |
| S+0x44 | 0x00800000 | tune scale |
| S+0x50 | interp(0x40021604, w(A+0x26)) | op0 frequency factor |
| S+0x5C | interp(0x40021A04, w(A+0x2C)) | op0 envelope target b, 1.0 → 0.1 |
| S+0xD0 | 0x04000000 | op1 frequency factor = 1/32, the reference ratio 1.0 |
| S+0x10C | frac(0x02000000 · interp(0x40021804, w(A+0x28))) | op1 PM depth from op0, 0 .. 1/64 (×2 in 2.3) |
| S+0x11C | 0x8000945C | op1 src1 = op0's buffer A |

The op0 factor gives the **ratio** op0/op1 = T_1604/0x04000000. The 128 entries run 0.0125,
0.075, 0.125, 0.25, 0.5, 0.75, 0.9, 0.99, 1.0, 1.001, 1.01, 1.1 … 2.0, 2.002 … then
integer-centred clusters up to 8.0, then 9, 10 … 15.

Then `jmp 0x40002686` with the same 3 arguments.

**0x40002686(pitch, S, A): phase increments of units 0..3, block rate. SY TONE only.**
Engines 0 and 2 have an inlined variant at 0x400025c0 (constant 0x57619F10).
1. `p = sats(S+0x40 + frac(S+0x44 · (w(A+0x24) << 16)))`, i.e. `w·256 − 64·65536`. So the tune
   word moves by (w/256 − 64) semitones.
2. `p = sats(p + pitch)`, clamped to [0, 0x7F0000] (notes 0..127).
3. `q = frac((p − 0x450000) · 0x0AAAAAAB)`: (note − 69)/12, octaves in Q16. oct = signed high
   half, fr = low 16 bits.
4. `f = frac(E[fr >> 5] · 0x6E000000) >> (5 − oct)`.
   - E = RAM table 0x8000A888: 2048 × Q31, E[0] = 0x40000000, E[1024] = 0x5A8665F6,
     E[2047] = 0x7FFFFFFF.
   - It matches 0.5·2^(i/2048) within 3.4e-4. [INFER] Copy E from the emulator rather than
     regenerate it.
   - 0x6E000000 = 0.859375.
5. `m = frac((S+0x3F8 − 0x04000000) · S+0x400) + 0x04000000`.
   - This is pitch modulation, crossfaded by S+0x400. S+0x400 is cleared by init and never
     written by SY TONE, so m = 1/32.
   - Note: init does not clear S+0x3F8.
6. For k = 0..3 (U = U_k):
   - `t = sats(2 · (frac(frac((U+0x50 >> 6) · m) · U+0x64) << 11))`.
   - `U+0x78 = frac(frac(t · sats(U+0x68 + f)) · 0x2BB0CF88) >> 2` (arithmetic shift).
   - Defaults: U+0x64 = 0x40000000, U+0x68 = 0 (init).
   - Check: U+0x50 = 1/32 at note 69 gives inc = 76 897 ≈ 440/96000·2²⁴ (76 895). This
     confirms the 96 kHz oversampled rate.
   - Units 2 and 3 are computed but not used by SY TONE.

**0x40004dc6(T, w): linear table interpolation. Shared.** Called by the update routines of
engines 0-10 (all but 11; engine 5 also from its update and its render 0x40007b38 by direct `jsr`). 128 × int32 table.
- `i = max(w >> 8, 0)` (no upper clamp on the first index).
- `f = (w − (i<<8)) << 23`.
- `r = frac((0x7FFFFFFF − f) · T[i]) + frac(f · T[min(i+1, 127)])`.
- The weights sum to 1 − 2⁻³¹. Keep this for bit-exactness.

### Constant tables (section 7 ROM, 128 × int32 BE each)

| Address | Index | Range | Used for |
|---|---|---|---|
| 0x40021404 | w(+0x46), interp | 0 → 0x20000000 (0.25), increasing | op0 feedback |
| 0x40021604 | w(+0x42), interp | 0x000CCCCD → 0x3C000000 | op0 ratio (×32: 0.0125 … 15) |
| 0x40021804 | w(+0x44), interp | 0 → 0x7FFFFFFF, increasing, curved | FM index |
| 0x40021A04 | w(+0x48), interp | 0x7FFFFFFF → 0x0CCCCCCD (0.1), decreasing, flat from ~96 | mod envelope target |
| 0x40021C04 | b(+0x48), direct | −0.99892 → −0.84055 | mod envelope decay coefficient |
| 0x40021E04 | b(+0x4E), direct | 0xFFFFFFFF (≈ −0, instant) → −0.99969 | amp decay coefficient |

RAM tables, built at boot. Dump them from the emulator after `boot()`:

| Address | Content |
|---|---|
| 0x80004B70 | sine, 257 longs |
| 0x8000A888 | exp2, 2048 longs |

---------------------------------------------------------------------------------------------

## 3. Voice state struct S (0x708 B, at 0x80000000 + 0x708·v)

R/W key:
- C = caller 0x40004324, I = init 0x40003ee0, Z = 0x4000255e, U = SY TONE update.
- E = 0x40003c0e, O1 = 0x40003aac, O2 = 0x40002c16, D = 0x400051c6.
- AE = 0x40003d42, V = 0x400045a0, X = 0x400047ea, P = 0x40002686.
- All fields are int32.

### Voice-level fields
| Off | Meaning | W | R |
|---|---|---|---|
| +0x00 | engine id | C, I(0) | C |
| +0x04 | engine id (compare copy) | C, I(0) | C |
| +0x28 | op0 last output sample (feedback memory) | O1, U(0 on trig), I | O1 |
| +0x2C | op0 feedback target (×1/16 in O1) | U | O1 |
| +0x30 | op0 feedback current (ramp start) | O1, I | O1 |
| +0x34 | trig arrived this block (pending) | C | V, AE |
| +0x38 | trig of previous block = start now | C (=+0x34 after render; 1 on engine change) | Z, U, E, AE, V, X |
| +0x3C | gate-off bit this block | C | AE |
| +0x40 | tune offset, Q16 semitones (I: 0xFFF40000; U: 0xFFC00000) | I, U | P |
| +0x44 | tune scale (I: 0x1FFFCB; U: 0x800000) | I, U | P |
| +0x48 | drive/duck envelope enable | U (trig), I(0) | X |
| +0x3D0 | amp level A (negative representation) | AE, V, I | AE, V |
| +0x3D4 | amp level at end of previous block (VCA ramp start) | V, I | V |
| +0x3D8 | "attack starts at increment/2" flag (I: 1) | I | AE |
| +0x3DC | amp stage 0/1/2 (I: 1) | AE, I | AE |
| +0x3E0 | hold counter | AE, I | — |
| +0x3E4 | release request | AE, I | AE |
| +0x3E8 | amp attack increment (I: 0x80000000 = instant) | I | AE, V |
| +0x3EC | hold mode = w(P+0x4C) | C, I | AE |
| +0x3F0 | amp decay coefficient | U, I | AE |
| +0x3F8 | pitch-mod source (not touched by init) | (other engines?) | P |
| +0x400 | pitch-mod amount (I: 0) | I | P |
| +0x414..0x41C | gain env coefficient set 1 (c0, c1, c2) | U (trig), I | X |
| +0x420..0x428 | gain env coefficient set 2 | U (trig), I | X |
| +0x42C | pointer to active set | X, I | X |
| +0x430 | gain env x[n−1] | X, U (trig), I | X |
| +0x434 | gain env y = gain at block end | X, U (trig), I | X |
| +0x438 | gain env stage (1 → 5) | X, I(5) | X |
| +0x43C | gain floor / threshold | U (trig), I | X |
| +0x440 | drive pre-gain (×4 in X) | U (trig), I | X |
| +0x444 | output gain | U (trig), I | X |
| +0x56C | pointer to decimator history (0x80008A60 + 0x30·v, set at boot) | boot | D, U, I |

### Per-unit fields: U_k = S + 0x80·k, k = 0..6
Values given are SY TONE's units 0 and 1; the init values hold for every k.

| Off in U | Meaning | W | R |
|---|---|---|---|
| +0x50 | frequency factor (I: 0x04000000 = ×1). U0: ratio table, U1: 0x04000000 (U) | I, U | P |
| +0x54 | env attack increment / start level (I: 0x80000000) | I | E |
| +0x58 | env decay coefficient (U0: T_1C04 on trig) | U, I | E |
| +0x5C | env decay target b (U0: T_1A04) | U, I | E |
| +0x60 | env delay length, blocks (I: 0) | I | E |
| +0x64 | frequency scale (I: 0x40000000) | I | P |
| +0x68 | linear frequency offset (I: 0) | I | P |
| +0x6C | phase, 24-bit | O1/O2, Z(0 on trig), I | O1/O2 |
| +0x78 | phase increment at 96 kHz | P | O1/O2 |
| +0x7C | env level L = operator output amount | E | O1/O2 |
| +0x80 | previous amount (ramp start) | E (trig), O1/O2 | O1/O2, E |
| +0x84 | env stage | E, I | E |
| +0x88 | env delay counter | E | E |
| +0x8C | PM depth 1 target (½ of applied). U1: S+0x10C | U | O2 |
| +0x90 | PM depth 2 target | I | O2 |
| +0x94 | PM depth 1 current | O2, I | O2 |
| +0x98 | PM depth 2 current | O2, I | O2 |
| +0x9C | PM source 1 buffer pointer (I and U: 0x8000945C) | I, U | O2 |
| +0xA0 | PM source 2 buffer pointer (I: 0x8000945C) | I | O2 |
| +0xB0 | wavetable pointer (I: 0x80004B70; +0xB4..BC same) | I | O1/O2 |
| +0xC8 | buffer A pointer (scaled output) = 0x8000945C + 0x124·k | I | O1/O2 |
| +0xCC | buffer B pointer (raw output) = 0x80008C60 + 0x124·k | I | O1/O2 |

The unit stride 0x80 overlaps the voice-level fields 0x28..0x48 of unit 0. Those are read
relative to S, not to U.

Other fields that init sets and SY TONE does not use:
- S+0x640 + 0x10·k: four longs per unit, (0, 0x68DB9, 0, 0x7FFFFFB6).
- S+0x450..0x5D4, S+0x6B0..0x704.
- S+0x6F8 ← [0x40014B7C], S+0x6FC ← [0x40014980].
- Z clears S+0x494/498/4B4/4B8/4F4/4F8 on trig.

### Global scratch, shared by voices because they render sequentially
Each buffer has 0x124 B per unit slot: an 8-long header followed by 64 samples (+1 long). The
samples start at +0x20.

| Buffer | Content |
|---|---|
| 0x8000945C + 0x124·k | buffer A per unit slot k |
| 0x80008C60 + 0x124·k | buffer B per unit slot k |
| 0x80009C58..0x80009CE7 | decimator recursive outputs: 4 history longs + 32 |

SY TONE uses:
- A0 (0x8000947C): op0 out, the modulator.
- B0 (0x80008C80): unused.
- A1 (0x800095A0): unused.
- B1 (0x80008DA4, header from 0x80008D84): the carrier, read by D.

---------------------------------------------------------------------------------------------

## 4. Porting order

Leaf functions with no calls and no global state other than their arguments and S fields can be
ported and tested alone against the emulator. Feed a synthetic S and buffers, and compare
bit-exactly.

1. **EMAC primitives first**: `frac(a·b)` truncating, 40-bit acc with `<<8` load and saturating
   store, mac/msac accumulation order. Everything below depends on them. `tools/emu/emac.py` is
   the reference.
2. **0x40004dc6 interp(T, w)**: pure function, 6 ROM tables. Trivial to test exhaustively over
   w = 0..0x7FFF.
3. **0x40003c0e envelope (n units)**, **0x40003d42 amp envelope**, **0x400045a0 VCA**: block
   rate, small state, no tables. They are shared with most engines, so porting them once pays
   off.
4. **0x400047ea drive/duck stage**: shared too. Depends only on S+0x414..0x444 and S+0x48.
5. **0x40002686 pitch**: needs the RAM exp table 0x8000A888, dumped from the emulator. Test it
   on inc values over notes 0..127 and tune words.
6. **0x40003aac** then **0x40002c16 oscillators**: need the RAM sine 0x80004B70 and the buffer
   plumbing (A/B buffers, the +0x20 offset). Test per block on buffers A and B. Test the
   feedback path, the `move acc→reg` saturation of the phase sum, and the `& 0xFFFFFF` wrap.
7. **0x400051c6 decimator**: leaf, but needs the 8-long header contract with the oscillator
   buffer, the per-voice history at *(S+0x56C), and the `(y+1) & ~1` writeback.
8. **Update 0x4000709c** (ties the parameter words to the fields above), then **render
   0x40007214** (fixed sequence), and finally the caller's trig/gate-off delay logic
   (S+0x34 → S+0x38, phase reset 0x4000255e, init 0x40003ee0 on machine change).

Suggested first end-to-end test: one voice, trig, 50 blocks, compare `out[32]` per block with
`tools/emu/cli.py render --machine 4`. Machine 4 maps to engine 4.
