# Syntakt OS 1.41, CPU #1 (section 3): what it does to the sound of the digital tracks 1-8

Read-only research, 2026-10-22, on `C:\dev\elektron-work\syntakt-1.41\section_3_MAIN_OS.bin` (loads at 0x40000400,
as SyntHack's tests and `cfdis.py` assume; `cfdis.py` works on section 3 unchanged when given the file).
Scripts and per-routine disassemblies: `C:\dev\elektron-work\scratch-arch\cpu1\` (`inv.py` = recursive inventory,
`r/`, `r2/`, `r3/`, `r4/` = one `.s` per routine, `irq.s` = the interrupt, `desc.py`/`desc.txt` = the 505 parameter
descriptors with their internal ids, `ptr.py`). Guesses are marked [INFER].

Track indices below are CPU #1's *engine* order: t = 0..7 are the digital tracks (user tracks 1-8), t = 8..11 the
analog tracks (user 9-12), t = 12 the FX track. "Engine copy" = 0x800021B0 + 142 t (target values). Word offset of
internal id `i` in a track = 28 + 2 i (SyntHack). Names of ids come from the descriptor table 0x4022D59C (`desc.txt`).

## 0. Corrections to earlier notes

- The 0x1B0 block is **not** built from the engine copy 0x800021B0: it is built from the **smoothed copy at
  0x80006348** (same layout, 0x754 words + 0x64C bytes of track settings), which `0x40097952` recomputes every block
  and which the LFOs and the modulation matrix then modify in place (section 2). machines.md's "copies the 20 machine
  bytes unchanged from the engine copy" holds only for the bytes that are not smoothed or modulated.
- The block's machine id byte (+0x3E) and the amp-hold word (+0x4C) are rewritten each block in the smoothed copy
  before the copy (0x400A4BC6).

### Corrections from the port of steps 3-6 (2026-10-23, `stMainModulation`, `stMainLfo`, `stMainPitch`, `stMainFilter`)

Each read in the code the C++ port matches bit for bit (0 differ, every reachable instruction and branch side run):
- [CORR 3.2] A list entry is (depth word, id word): depth in the high word, the internal id in the low one; and the
  matrix **adds** source × depth (`msac` then `sub` of the negated product), it does not subtract.
- [CORR 3.1] The RNG's seed is in section 3's initialized data, not BSS: two longs at **0x402D701C / 0x402D7020**,
  an additive (Fibonacci) generator: `new = [7020] + [701C]; [701C] = [7020]; [7020] = new`.
- [CORR 3.1] FADE 0x4000 makes 0x40118C74 return 0x40000000 (full at once), not 0. Waveform order and modes are now
  read from the code, no longer inferred (3.1 below).
- [CORR 3.5] Only settings +0x6C rounds to semitones; +0x74 freezes the glide while the gate bit is clear (no trig).
  +0x64 is the glide distance in percent; +0x70 non-zero = constant rate (PTIM, table 0x40245C50), zero = a time set
  by the distance (table 0x40245A50). The progress runs from 0 **down** to INT32_MIN (done); the rates are negative.
- [CORR 3.6] The envelope runs in the negative domain (0 to INT32_MIN full): attack increments are negative, the
  attack ends when `sats` reaches INT32_MIN. +0x64 is not an envelope depth: non-zero, a trig restarts the value
  from 0. Stage 0 and any value other than 1-3 release.
- [CORR 3.7] 0x4550E1A8 + 4t holds env × ENV depth (what is added to the cutoff), not the envelope itself.
- [CORR 1.11] The pitch loop's trig bit comes from the mask at −0x5C(a6), not d3; the LFOs get d5.

### Corrections from the port of step 7 (2026-10-23, `stMainBlock`, the segment 0x400A49A6-0x400A4DB8)
- [CORR 1.5] 0x400A49A6: `d3 &= ~(signed word 0x80003150)` (the mutes, 16 tracks; 0x80003151 is its low byte);
  −0x5C(a6) = d3 then (the pitch's trigs); a track t < 12 whose settings long +0x60 is 3 and whose bit is set in
  `Gates (0x80003152) & d3` loses its bit in a5, d3 and d5 only (the voice, filter envelope and LFOs do not restart;
  the glide does) [INFER: legato]. −0x4C(a6) = d2. Then `0x4011A2FC(0x800033F8)`, an analog CV frame (7 below).
- [CORR 1.7] The cut writes HOLD +0x6E = 0x7F00, DEC +0x70 = 0, **REL +0x74** = 0 (not ATK); bit 12 writes
  +0x738/+0x73A/+0x73E instead; bits 13-15 land in the settings copy. Lowest bit first.
- [CORR 1.8] `0x43BDC824 ≠ 0`: the eDMA of CPU #2's last output and byte 0x43BDD73C = 1 (0x400A2036, after the block,
  waits on it and clears it); 0: `memset(0x80003D50, 0, 0x500)`.
- [CORR 1.12] The reload, while 0x43BDC4C8 ≠ 0: tracks 0-11's HOLD = 0x7F00, DEC = REL = 0; the FX track's +0x706
  fades (`w × 127 / 128` toward 0, kept at 0x43BDC808), +0x6EC, +0x6F4, +0x6FC = 0. At 2 it first saves +0x706,
  sets d2 = d4 = −1, a5 = d3 = 0, word 0x80003154 = 0, and moves to 1.
- [CORR 1.14] 0x400A4C02, t 8-12: bit t of d4 → long 0x454E3C58 + 0x20 (t − 8) = 4 and the long after it 0; else bit
  t of a5 → both 0.
- [CORR 1.15] `0x4009652C(a2, a5, d4, mute)`: **a5 is the filter trig mask, d4 the gate-off** (swapped above).
- Tools: `oracle3.reach` did not follow a pc-relative `jsr $x(pc)` (cfdis.target does not parse it), so the EMAC
  of 0x4011A1E4 was not a site and the rig skipped its `mac.l` without a fault; it follows them now (the earlier
  steps' only such call, 0x40119266 → 0x40118EF8, was a root itself). `vectors3.segment_insns` decodes with capstone
  alone, which stops at the first ColdFire-only instruction: pitchLoop's "22/22" counted 0x400A4ADE-0x400A4B20 only.
  `block3.py` decodes the segment as cfdis does, the pitch loop included.

## 1. The audio interrupt 0x400A3856 (vector set at 0x400002FC; `rte` 0x400A54A8), per 32-sample block

MACSR: saved, the accumulators saved, then **MACSR = 0x20** (fractional, no saturation, truncation) for the whole
interrupt (0x400A38C2). Some routines switch to 0xA0 (fractional + OMC saturation) and back
(0x400956BC, 0x4008F82E). `0x40118C74` asserts MACSR&0xF0 == 0x20 (calls the assert 0x40163334 otherwise).

Order, digital-track path in bold (everything else is sequencer, UI, analog or USB):

1. 0x400A386A-0x400A38A6: DMA position of the DAC/ADC ring (`0xFC0456C0`, `0xFC045690`), half-buffer flag
   `0x4553F6DC` (selects which half of the TDM rings 0x80000000 / 0x80001800 this block writes/reads).
2. 0x400A38C8 `0x40102066` (DSPI wait, SyntHack), then sequencer / MIDI clock / UI work (0x400A38E4-0x400A3B54).
3. **Event loop 0x400A3B54-0x400A475E** (sequencer out of scope; this is the plug-in's input seam). A linked list of
   trig events (`next` at +0x4C, time at +0x4), each for one track t:
   - +0x24 flags: 0x10000 note present, 0x80 trig, 0x400, 0x200 (gate-off/stop [INFER]), 0x8000 (`0x400A1B7C`).
   - Note: `0x800046B8[t] = clamp(note(+0x18) + track transpose byte(kit +168), 0, 127) << 16` (0x400A43A8,
     0x400A4452); also `0x45511208[t]`.
   - Velocity: `0x80004670[t] = word +0x14` (0x400A4492).
   - Parameter locks: list at +0x44 → `0x400978B4(t, list, 0x800021B0)` (section 3.4).
   - Sound change → `0x400A31A2` (copies a 142-byte sound into the engine copy, machines.md).
   - Kit change → `0x400A1D18(kit)` (kit's FX blocks → engine copy, with "global FX" overrides 0x41B9D3B0;
     SyntHack) and `0x40097874` / `0x40097894` / `0x400976B4` (snap the smoothing state, section 3.4).
   - Trig mask d3, gate-off mask d2, and a5 / -0x5C(a6) masks accumulate here per track.
4. MIDI-performance setters, called from MIDI handlers at 0x40022F74-0x400230AE (not the interrupt), write
   per-track arrays of 13 words / 12 longs (t 0..12), read by the interrupt:
   `0x800046F8` (0x400A3564, ≥0), `0x80004746` (0x400A35A2, **signed**), `0x8000472C` (0x400A35D4, ≥0),
   `0x80004712` (0x400A3612, ≥0), `0x80004670` (0x400A36A4, ≥0, also set by trigs: velocity),
   `0x800046B8` (0x400A3650, longs, value 0..0x7FFF << 16). Through `0x400A1C86`.
   [INFER] 4746 = pitch bend (signed; it feeds the pitch, section 2.1); 46F8/472C/4712 = aftertouch, mod wheel,
   breath (descriptor group 56: PB, AT, MW, BC are ids 21-24 of the FX/MIDI group).
5. 0x400A49A6-0x400A49F0: masks 0x80003150/52 and track setting +0x60 == 3 (per 124-byte settings struct
   0x80002904 + 124 t) clear trigs [INFER: MIDI-machine / mute handling].
6. **0x400A4A00 `0x40097952(0x800021B0)`**: parameter smoothing → returns a2 = 0x80006348 (section 3.3).
7. 0x400A4A10-0x400A4A6A: tracks whose bit is set in 0x43BDE440 get amp HOLD = 0x7F00, DEC = REL = 0 [CORR 1.7] (in the
   smoothed copy; t = 12 at +0x738..) [INFER: "kill/choke" fast release]. Same word low byte → block 0x182.
8. 0x400A4A6C-0x400A4ABE: start eDMA of CPU #2's previous output to 0x80003D50 (8 × 128 B; TCD 0xFC0455E0, src
   0x100001B0), or clear it.
9. **0x400A4AC8 `0x40097AFC(a2)`**: modulation matrix (velocity, 4 MIDI sources, note) onto the smoothed copy (3.2).
10. **0x400A4AD8 `0x40119266(a2, tempo = long 0x402D7010, trigmask)`**: the 26 LFOs (2 per track, 13 tracks) onto
    the smoothed copy (3.1).
11. **0x400A4ADE-0x400A4B3E, t = 0..11: pitch**: `0x80004688[t] = 0x40119FE8(t, t, trigbit, 0x800046B8[t], a2)` +
    `(settings[t]+0x7A (signed byte) << 16) × (0x80004746[t] << 16)` (EMAC frac). Portamento in 3.5.
12. 0x400A4B40-0x400A4BC2: kit reload state machine (`0x43BDC4C8`): on 2, every track's amp ATK/HOLD/DEC reset
    (0, 0x7F00, 0). [CORR 1.12: HOLD/DEC/REL, while the state is not 0]
13. **0x400A4BC6-0x400A4C00, t = 0..7**: smoothed `+0x3E = machine id (0x80002F50[t]) << 8`;
    `+0x4C = (AMP MODE +0x80 != 0) ? 1 : (HOLD +0x6E > 0x7EFF)` (= CPU #2's "hold until gate-off").
14. 0x400A4C02-0x400A4C32: per-voice state 0x454E3C58 + 32 v for t 8..12 (analog).
15. **0x400A4C54 `0x4009652C(a2, trig a5, gateoff d4, mute byte 0x80003151)`** [CORR 1.15]: filter envelopes of t 0..7 (3.6).
16. **0x400A4C7A-0x400A4D74, block builder, t = 0..7** (source P = a2 + 142 t, block B = 0x80002000):
    - `B[20t .. 20t+19] = P[+0x3E .. +0x51]` (memcpy 0x40115F78); then unless machine id is 37 (SY BITS) or
      44 (SP TWINSHOT): `word B[20t+10] = 0x7F00 - P+0x48`.
    - filter `B[0xA0+8t]` = TYPE +0x52, RESO +0x56, BASE +0x66, WDTH +0x68.
    - amp `B[0xE0+14t]` = ATK +0x6C, HOLD +0x6E, DEC +0x70, SUS +0x72, REL +0x74, MODE +0x80, AENR +0x82.
    - pitch `B[0x150+4t] = 0x80004688[t]` (long, semitones Q16 after glide and bend).
    - velocity `B[0x170+2t] = 0x80004670[t]` (word).
    - cutoff `B[0x188+4t] = 0x400956BC(&P+0x52, settings[t]+0x79 (key track byte), 0x80004688[t], t)` (3.7).
17. **0x400A4D78-0x400A4DB2**: `B[0x184] = tempo long 0x402D7010`; `B[0x180] = trig & ~mute`,
    `B[0x181] = gateoff & ~mute`, with mute = byte 0x80003151; `B[0x182] = low byte of 0x43BDE440`;
    `B[0x1A8] = mute | (0x43BDE440 & 0xFF)`. Then `0x400A2036`, and the eDMA of B to CPU #2 (TCD 0xFC0455E0,
    dst 0x10000000, ch 47, 0x400A4DC4).
18. 0x400A4E00 `0x40095590`, 0x400A4E0E `0x40097A48(a2)` (analog tracks' level × per-machine table 0x401E08E0),
    0x400A4E3C `0x400A2792`, 0x400A4E44-0x400A4EB4 analog machine dispatch (machines.md), 0x80005160 envelope
    counters.
19. 0x400A4F08-0x400A4FB4: analog and FX-track control (`0x4008EE54`, `0x400962E8`, `0x40095DA4`, CVs
    `0x400903CA`), ADC handling (`0x4009622C`, `0x40096234` reads the ADC ring 0x80001800, `0x4008EE0E`,
    `0x4008EBBC` = analog tracks' post-ADC processing with the per-sample amp tables 0x401DADDC [INFER],
    `0x40095794`).
20. **0x400A4FCE `0x4008F1CA(DAC half 0x80000000+..., ADC slot, a2)`: mix, sends, delay, reverb, buses, DAC
    write, Overbridge frame** (section 4).
21. 0x400A4FD4-0x400A5240: USB/Overbridge transfer (`0x40006F36` state == 5; two chosen channels
    0x8000313F/40 copied to 0x45511108, `0x40003FEE`), 0x400A5304 `0x40101FF0` CV DSPI frame (machines.md),
    `0x40095640` (tempo-related, 0x80003130-38), MACSR/ACC restore, `rte`.

What a plug-in gives in place of the sequencer and MIDI: the event fields (note, velocity, trig, gate-off,
locks), the MIDI-performance arrays, tempo (0x402D7010 = BPM × 120), the masks (0x80003151 mute, 0x43BDE440 kill)
and the kit (engine copy + settings). Everything from step 6 to 17 and step 20 is then deterministic code.

### 1.1 The seam of the block (step 7 of the porting order, `stMainBlock.h`)

The event loop ends at 0x400A49A6 with five masks of tracks (bit t = track t) in registers; the frame words the
segment reads (−0x4C to −0x6C(a6)) it writes first. `syntakt::main::buildBlock(mem, BlockMasks)` runs
0x400A49A6-0x400A4DB8 from them. Who sets them in the event loop:

| Mask | Reg. | Set by |
|---|---|---|
| trigs | d3 | an event with flag 0x80 (a note trig) sets bit t (0x400A44C2); a reload clears it |
| gate-offs | d2 | 0x400A487A: (~d3 & −0x50) or −0x5C, −0x50 = the tracks whose note length counter 0x80003050[t] (−tempo per block) reached 0, −0x5C = forced offs (0x43BDC4D4's, a cut's) |
| LFO trigs | d5 | flag 0x400 (the trig's LFO trig) (0x400A43DA, 0x400A459A) |
| filter trigs | a5 | flag 0x200 (the trig's filter trig) (0x400A43F2, 0x400A45A4) |
| filter gate-offs | d4 | 0x400A487C: (~a5 & −0x34) or d6 or the cuts, −0x34 = the filter length counter 0x80003084[t] reached 0 |

Memory the event loop writes before (the plug-in writes it in its place): `Notes` 0x800046B8[t] =
clamp(note + transpose, 0, 127) << 16 and `Velocity` 0x80004670[t] = the event's word +0x14 on a trig; the locks,
`lock(t, list, 0x800021B0)` (3.4); the MIDI sources (4); `Mutes` 0x80003150, `Gates` 0x80003152, `Held` 0x80003154;
`Kills` 0x43BDE440 (the tracks a trig cuts, from a per-track word of the kit at 0x800030B8 + 0x15FE + 2 t [INFER]);
`Reload` 0x43BDC4C8 (2 for the block a kit reloads, cleared by the event loop at 0x400A4706 when it is done);
`Tempo` 0x402D7010; `0x43BDC824` (1: the voice CPU runs). The kit: the engine copy, its settings, the machine ids.
A plug-in's note-on is a trig (and the LFO / filter trigs its trig settings give), its note-off a gate-off on both
masks; Kills, Mutes and Reload stay 0 until it models cuts, mutes or kit loads.

buildBlock then runs the rest in the interrupt's order: the legato filter (CORR 1.5), `0x4011A2FC` (the CV frame,
analog only), the smoothing, the cuts, the eDMA flag or the clear, the matrix, the LFOs (d5), the pitch loop (the
trigs after mutes), the reload, the machine words, the analog voices' state, the filter envelopes (a5, d4), the
block of tracks 0-7 and its tail (0x180 = trigs & ~mute, 0x181 = gate-offs & ~mute, 0x182 = cuts & 0xFF,
0x184 tempo, 0x1A8 = mute | cuts). The block's bytes 0x183 and 0x1A9-0x1AF are never written.

## 2. How each block field is computed

| Block field | Source | Routines |
|---|---|---|
| machine bytes 20t | smoothed P+0x3E..0x51 (ids 17-26) incl. LFO / mod matrix / locks; byte 0 = id << 8; word 10 inverted except SY BITS, TWINSHOT; word 14 = hold flag | 0x40097952, 0x40119266, 0x40097AFC, 0x400A4BC6 |
| filter 0xA0+8t | smoothed TYPE, RESO, BASE, WDTH (+ modulation) | same |
| amp 0xE0+14t | smoothed ATK, HOLD, DEC, SUS, REL, MODE, AENR; kill (0x43BDE440) and kit reload overwrite HOLD/DEC/REL [CORR 1.7] | same, 0x400A4A10, 0x400A4B7A |
| pitch 0x150+4t | note (event) → glide → + bend × range; **not** LFO/tune: TUNE is machine word 2 (id 18), modulated by LFO as a word | 0x40119FE8, 0x400A4AF2 |
| velocity 0x170+2t | event +0x14 or MIDI setter | 0x400A4492, 0x400A36A4 |
| trig 0x180, gate-off 0x181 | event loop masks & ~mute | event loop (out of scope) |
| 0x182 / 0x1A8 | kill mask 0x43BDE440, mute 0x80003151 | — |
| tempo 0x184 | 0x402D7010 | — |
| cutoff 0x188+4t | FREQ + key track × (pitch − 48) + filter env × ENV | 0x4009652C, 0x400956BC |

Parameter locks set the *target* (engine copy) and snap the smoothing state, so a lock jumps; the LFOs and the
modulation matrix act on the smoothed copy every block and are never smoothed.

## 3. Control routines

### 3.1 LFO 0x40119266(P = smoothed base, tempo, trigmask) — 269 instructions, 6 EMAC
- 13 tracks × 2 LFOs, LFO2 first (j = 1, params at P+0x2E..0x3C, ids 9-16), then LFO1 (P+0x1E..0x2C, ids 1-8).
  Descriptor flags: LFO1's parameters carry bit 0x400 (targetable by LFO2), so LFO2 runs first to modulate LFO1.
- Per LFO, relative to a3 = its DEST word: SPD −6 (word − 0x4000, ×2), MULT −4 (byte: 0..11 BPM-synced to
  `tempo`, 12..23 free with constant 0x3840 [INFER: the free "×1..×2k" multipliers]), FADE −2 (→ `0x40118C74`, an
  exp2 via `0x40115EC8`, ×0x1C200000; accumulated, clamped 0..0x3FFFFFFF), DEST 0 (byte, internal id ≤ 0x46),
  WAVE +2 (0..6, clamped), SPH +4, MODE +6 (0..4: FREE, TRIG, HOLD, ONE, HALF [INFER from Elektron's order]),
  DEP +8 (word − 0x4000, ×2).
- State: 0x444DA778 + 0x50 t − 0x28 j (phase −0x24, fade −0x14, RND value −0x1C/−0x18/−0x10, output −0x20/(a4),
  flags 0/1). Phase step = SPD × (tempo or 0x3840) >> (11 − MULT); wrap/one-shot logic with 0x5265C000 and
  0xD6CD2000 offsets; start phase via 0x636BA875 (Q31 1/... ) and tables 0x402457E4 / 0x40245800 / 0x4024581C.
- Waveforms (table 0x40245838): [0] 0x40118D8E, [1] 0x40118DA4, [2] 0x40118D70, [3] 0x40118D82, [4] 0x40118DFE,
  [5] 0x40118DEE; [6] = RND: `0x40118EF8` (random) with interpolation coefficient table 0x40245850[SPH >> 8].
  Read from the code: TRI (fold of phase + 1/4), SIN (parabolic, ×0x73333334 correction), SQR (sign), SAW
  (phase ^ 0x7FFFFFFF), EXP (exp2 0x40115EC8 of a scaled phase), RMP (the phase on its positive half, 0 on the
  other), RND (a new value when the phase moved more than 1/16 cycle, 0x5265BFF, since the last one, or on a trig
  in modes other than FREE; output = coef × last output + (1 − coef) × value; a trig resets RND's phase to 0, not
  SPH). The phase is Q32 after `× 0x636BA875 << 2`; a cycle is 0x5265C000 of the state's phase.
- Modes, read from the code: 0 FREE (a trig restarts only the fade), 1 TRIG (phase to SPH, fade restarts),
  2 HOLD (a trig samples once, then the output holds; the fade does not restart), 3 ONE (stops at the cycle's end
  with the output from 0x40245800 forward / 0x4024581C backward), 4 HALF (stops at half a cycle, 0x402457E4).
  Rates: MULT 0-11 use the tempo argument, 12-23 the constant 0x3840 (= 120 BPM × 120).
- State fields (a = 0x444DA778 + 0x50 t − 0x28 (1 − j)): a−0x24 phase, a−0x20 output, a−0x1C random value,
  a−0x18 last raw output, a−0x14 fade (0..0x3FFFFFFF, × 2 on the output), a−0x10 phase of the last random value,
  a−0xC DEST, a−8 the amount added, a−4 phase or 0 while stopped, byte a+0 stopped/holding, a+1 restarted last block.
- Output × fade × depth is **added to the destination word** of the same track's smoothed copy, clamped 0..0x7F00
  (0x4011950C).

### 3.2 Modulation matrix 0x40097AFC(P) — 89 instructions + 0x40097AAC (26), 1 EMAC each
- Per track t 0..11: `0x40097AAC(P_t+0x1C, velocity 0x80004670[t], settings_t+0x00)`.
- Per track t 0..12: four sources 0x80004746, 0x8000472C, 0x80004712, 0x800046F8 → settings_t +0x10, +0x20,
  +0x30, +0x40; and the note `(0x800046B8[t] >> 8) − 0x3C00` (note − 60) → settings_t +0x50.
- Each destination list = 4 × (id word, depth word): `param[id] = sat(param − source × depth)` with the
  0x40000000 bias trick (signed 16-bit saturation of a 0..0x7F00 word). settings_t = 0x80006A9C + 124 t (copy of
  0x80002904 + 124 t).

### 3.3 Parameter smoothing 0x40097952(target = 0x800021B0) — 65 instructions, 4 EMAC
- For all 938 words of the engine copy (0x754 bytes = 13 tracks + FX/global blocks): state long at 0x8000E9B0 + 4 i,
  `state = 0x03D7 × target + 0x7C29 × state` (Q15 weights, sum 0x8000, EMAC `mac.w`/`mac.l` frac), output word =
  `(state + 0x8000) >> 16` into 0x80006348 + 2 i. One pole per block, time constant ≈ 33 blocks ≈ 22 ms.
- Then copies 0x64C bytes of settings unsmoothed (0x80002904 → 0x80006A9C), and per track the unsmoothed words
  +0x24 (LFO1 DEST), +0x34 (LFO2 DEST); for SP TWINSHOT also +0x42, +0x4A; for SY RAW (39) on t 8..11 +0x4B8.
- Returns 0x80006348.

### 3.4 Locks and snapping
- `0x400978B4(t, list, 0x800021B0)` — 52 instr.: for each (id, value) of the event's lock list (count +8, entries
  at +0x14, 8 B): engine word 14 + 71 t + id = value, smoothing state = value << 16, lock bit in 0x4550E1CC + 12 t.
- `0x40097802(dst, src)` — 39: for each set bit of 0x4550E25C..: restore word 0x362 + .. from src and snap.
- `0x40097894(idx, value, base)` — 9: set one word and snap. `0x40097874(t)` — 11: clear t's lock bits.
  `0x400976B4` (19), `0x400976EE` (23), `0x40097760` (55), `0x40097732` (12): block snaps.

### 3.5 Portamento 0x40119FE8(t, t, trig, target pitch, P) — 167 instructions, 5 EMAC
- State 0x45565328 + 8 t (progress Q31, delta), last pitch 0x444DB6F8 + 4 t. On trig: delta = previous −
  target (legato rules from settings +0x68, masks 0x80003152/54). PORT (+0x86) > 0x3FFF enables; PTIM (+0x84 high
  byte) → rate table 0x40245C50; settings +0x70 selects linear vs. constant-time (table 0x40245A50) [INFER];
  settings +0x6C/+0x74 quantise to semitones. Output = target + delta × (1 − progress).

### 3.6 Filter envelope 0x4009652C(P, trig, gateoff, mute) — 94 instructions, 4 EMAC
- t 0..7, state 0x43BDC09C + 12 t: {stage, value (+4, read by `0x4009664E`), counter}. Params from P_t:
  DEL +0x5A, ATK +0x5C, DEC +0x5E, SUS +0x60, REL +0x62, FENR +0x64.
- Trig: stage 3 if DEL ≠ 0 else 2, counter 0; FENR ≠ 0 → value 0. Gate-off: stage 0. Muted tracks are skipped.
- Stage 3 delay: counter += 32 until 0x401DEAC0[DEL byte]. Stage 2 attack: value += 0x401DEEC0[ATK >> 8]
  (saturating) until 0x7FFFFFFF... (compared with 0x80000000 after `sats`). Stage 1 decay: one pole to
  SUS × 0x40810204 with 0x401DECC0[DEC byte]. Stage 0 release: value × 0x401DECC0[REL byte].

### 3.7 Cutoff 0x400956BC(&P+0x52, keytrack, pitch, t) — 63 instructions, 3 EMAC, MACSR 0xA0 inside
- `c = FREQ << 16 + sat(((pitch − 0x300000) << 8) × (keytrack << 24)) × 0x483244B3`;
  `c −= env(t) × ((ENV − 0x4000) << 17)` (env also stored at 0x4550E1A8 + 4 t for the UI); clamp 0..0x7F000000.

## 4. What CPU #1 does with the 8 voices' outputs: 0x4008F1CA(dac, adc, P) — 986 instructions, 49 EMAC

Input: CPU #2's previous block at 0x80003D50 (8 × 32 int32; one block old). ADC: 0x80003950 (8 ch).

1. `0x40097414(P)` (173, 5 EMAC): delay time `= (TIME + 256) × 48000 >> 10 × 900 / tempo`, clamped 0x5DC00,
   smoothed (×255/256 per block, 0x8000DAC0), read pointers 0x8000DAC4/DAC8 into a 2^19-frame stereo ring at
   **DDR 0x4FC00000** (8 B per frame); sets up the reads into SRAM 0x8000C528.. [INFER: by eDMA].
2. Per track t 0..11 (0x4008F21E-0x4008F474):
   - gain `g = 0x4008ED48(level word P[2t], velocity, VOL +0x7C, kit byte (0x800030B8 + 0x123 t)+205)`:
     velocity curve table 0x401DB1DC[(mode & 3) × 128 + vel] × (LEV × VOL)² (frac EMAC, 38 instr.).
     UT NOISE/IMPULSE on analog tracks × 0x401DC5E0[P+0x3E byte].
   - pan `0x400A171A(PAN +0x7A)`: constant power via sine `0x400A16BC` (30 + 14 instr.).
   - Digital t: direct-bus targets (e188 L / e160 R) = g × pan unless **ROUT +0x7E ≠ 0**; FX-bus targets (e138 /
     e110) = g × pan when ROUT ≠ 0 and bit t of the mask 0x80003148 (or 0x8000314C if 0x80003148 < 0) is clear.
     Analog t: e0d0 / e0b0 (their ADC signal; the analog direct path is analog).
   - Sends: DEL +0x76 and REV +0x78 as −2 × v² (Q31), × g × pan, zero if bit t of 0x8000314A (or 0x8000314E) is set
     [INFER: mute]; current gains at 0x80006328/08 (delay L/R), 0x800062E4/C0 (reverb) for digital,
     0x80006280/60/40/20 for analog.
   - Every gain ramps linearly over the block: increment = (target − current) >> 5.
3. External in (0x4008F478-0x4008F76C): analog inputs' gains/sends (SyntHack's dual-mono block), from P+0x70A..
4. Delay bus 0x8000DBD0 = Σ digital sends (8-ch sum `0x4008F03E`) + analog sends (4-ch `0x4008F13A`, at 0x8000DED0,
   added in 0x4008F7DA). Waits on eDMA (0xFC0453DE bit 4). **Delay `0x40096890(out 0x8000DCD0, in 0x8000DBD0,
   P+0x6E6)`** (229 instr., 34 EMAC): TIME, X (ping-pong), WID, FDBK, HPF/LPF one-poles from table 0x4029BFFC,
   state 0x8000D908-0x8000DA1C, line via pointer 0x8000D6B0. `0x40096802` (30) advances the write pointer (+32
   frames, mask 0x7FFFF) and programs the eDMA (TCDs 0xFC045540/50) between SRAM 0x8000C6A0 and DDR.
5. Delay return → channel 8 of the track buffer (0x80004150, copy `0x4008F1B4`); delay→reverb send
   (P+0x6F2, −2 v², zero if delay routed or muted). Reverb bus 0x8000DBD0 = Σ reverb sends (8 digital + delay
   channel + 4 analog). **Reverb `0x40096E3C(out 0x8000DDD0, in 0x8000DBD0, P+0x6FA)`** (407 instr., 113 EMAC):
   PRE, DEC, FREQ/GAIN shelving, HPF/LPF (table 0x4029B9FC and 0x4029BFFC), state 0x8000C310-0x8000D650; lines in
   DDR: 8 × 4 KB at 0x4D600000 and 9 × 64 KB at 0x4D570000, pointers 0x8000D690 / 0x8000C660 advancing 0x80 B per
   block, fed through SRAM descriptors 0x8000C6B0 / 0x8000C7B0 by `0x40096DBA` (39) [INFER: eDMA].
   Reverb return → channel 9 (0x800041D0).
6. Return levels: delay VOL P+0x6F4, reverb VOL P+0x706 (−2 v², muted by bits 12/13 of the mute mask); routed to
   the direct bus (0x8000E1A8/E180) or the FX bus (E158/E130, E1AC..) by **delay FX routing P+0x6F6 (id 25)** and
   **reverb FX routing P+0x708 (id 34)**.
7. Buses (10-ch sum `0x4008EF14`, msac with per-sample gain ramps, gains stored negated):
   **0x8000DAD0** (L at +0, R at +4, 8 B frames) = digital tracks not routed + routed-direct returns;
   **0x8000DFD0** = digital tracks with ROUT + returns routed to the FX track. `0x4008EECA(bus, n)` (23 instr.)
   post-processes each [INFER: DC/clip].
8. DAC write (0x4008FBF0-0x4008FD68): DAD0 → TDM slots +0x1C/+0x08 of the DAC half `dac`, DFD0 → +0x14/+0x0C,
   with one-pole smoothed output gains (0x7EB851EB / 0x0147AE14, state 0x43BD7688/84 and table 0x401DAFDC[P+0x740],
   shift 3 − byte 0x80003144; muted when 0x8000313D/3C say so) [INFER: crossfade of the analog/digital headroom
   setting]. Both buses then enter the **analog** mixer and the analog FX block; nothing digital follows.
9. Meters (0x4008FD6A-0x4008FE2A): per track `0x4008ED48` again and `0x4008FEBC` (32) → 0x43BD768C (UI only).
10. Overbridge frame **0x80004760** (32 frames × 20 longs): 0-1 = 0x8000DED0 (ADC-side mix [INFER: ADC 6/7, the
    analog main before the master VCAs]), 2-9 = the 8 digital tracks (raw CPU #2 output, pre-gain, pre-pan),
    10-13 = ADC 0-3 (analog tracks), 14-15 = ADC 6/7, 16-17 = delay + reverb returns (0x8000DCD0 + 0x8000DDD0).

Digital vs analog: everything in sections 1-4 is code. The analog part is after the DACs: the analog mixer
(master VCAs = CVs 35/36), the FX track's analog filter/drive (DFD0 goes through it), the master overdrive/filter,
the analog voices 9-12. The device's main output is analog. A software MoDule-style "Main" therefore needs a
model of that mixer; the stems (per-track, Overbridge 2-9 and the gains of step 2) are fully digital.

## 5. State and isolation in Unicorn

| Routine | Reads | Writes (state) | Alone? |
|---|---|---|---|
| 0x40097952 | 0x800021B0.., 0x80002904.., 0x80002F50 | 0x8000E9B0 (3752 B), 0x80006348.. | yes, pure memory |
| 0x40097AFC/AAC | P, settings copy, 5 source arrays | P in place | yes |
| 0x40119266 | P, tempo, trigs; tables 0x402457E4-0x40245850 | 0x444DA778 (13 × 0x50), RNG 0x402D701C/20 [CORR: found] | yes; needs MACSR 0x20 |
| 0x40119FE8 | P, settings, 0x80003152/54 | 0x45565328, 0x444DB6F8 | yes |
| 0x4009652C | P, tables 0x401DEAC0/EEC0/ECC0 | 0x43BDC09C (8 × 12 B) | yes |
| 0x400956BC | P, pitch, env 0x43BDC0A0 | 0x4550E1A8 | yes (sets MACSR itself) |
| block builder | everything above | 0x80002000 (0x1B0) | as an interrupt segment 0x400A4A00-0x400A4DB8 with a6 frame and d2/d3/a5 masks seeded (like test_master_comp's HOOK..AFTER), stubbing the eDMA writes (0xFC04xxxx) |
| 0x4008ED48, 0x400A171A, summing 0x4008EF14.. | args | none | yes, trivially |
| 0x40096890 + 0x40097414 + 0x40096802 | P+0x6E6, tempo, SRAM windows | 0x8000D908.., 0x8000DAC0.. | yes for the DSP part if the SRAM windows 0x8000C528.. are filled by the harness as the eDMA would; the 4 MB DDR ring must be emulated (periph.py's eDMA, or copy TCD src→dst on the CSR write 0x11 to 0xFC04555E) |
| 0x40096E3C + 0x40096DBA | P+0x6FA | 0x8000C310-0x8000D650, DDR lines | same eDMA caveat |
| 0x4008F1CA | all buses, ADC/DAC rings, P | buses, DAC ring, 0x80004760 | yes with SRAM 64 KB + DDR pages mapped, the poll at 0x4008F80A answered (map 0xFC045000 with bit 4 clear), external-in state |

Memory to map: DDR 0x40000000 (section 3 at 0x40000400, BSS pages 0x41B9xxxx, 0x43BDxxxx, 0x444Dxxxx,
0x454Exxxx, 0x4550xxxx-0x4556xxxx, FX lines 0x4D570000-0x4D610000, 0x4FC00000 + 4 MB), SRAM 0x80000000 (64 KB),
peripherals 0xFC040000-0xFC060000 as RAM. **EMAC sites:** `emac_sites_os141.json` covers **section 7 only**
(`code_range` 0x40000404-0x40009F88, section-7 SHA). Section 3 needs its own list: `cfdis.is_emac` over the
recursive descent of the routines run (`inv.py` already walks them), or objdump over the ranges, as
`test_dual_mono.py` lists its sites by hand.

## 6. Inventory and porting order

| Routine | Role | Instr. | EMAC | Callees |
|---|---|---|---|---|
| 0x400A3856 | interrupt (inline block builder ~200 instr. of 2155) | 2155 | 6 | all below |
| 0x40097952 | smoothing | 65 | 4 | 0x40115F78 memcpy |
| 0x40097AFC / 0x40097AAC | mod matrix | 89 / 26 | 1 / 1 | — |
| 0x40119266 | 26 LFOs | 269 | 6 | 0x40118C74 (37, 1), 0x40115EC8 (17, 2), 0x40118CF0 (7), 6 waves (60, 4), 0x40118EF8 RNG (6) |
| 0x40119FE8 | portamento | 167 | 5 | — |
| 0x4009652C | filter env | 94 | 4 | — |
| 0x400956BC / 0x4009664E | cutoff | 63 / 9 | 3 | — |
| 0x400978B4, 0x40097802, 0x40097894, 0x40097874, 0x400976B4, 0x400976EE, 0x40097760, 0x40097732 | locks, snaps | 52, 39, 9, 11, 19, 23, 55, 12 | 0 | — |
| 0x400A1D18, 0x400A31A2 | kit / sound → engine copy | 186, 39 | 0 | memcpy, 0x400A647A |
| setters 0x400A3564..0x400A36D0, 0x400A1C86 | MIDI sources | ~90 | 0 | — |
| 0x4008F1CA | mix, routing, DAC, Overbridge | 986 | 49 | below |
| 0x4008ED48 | track gain | 38 | 6 | — |
| 0x400A171A / 0x400A16BC | pan law / sine | 30 / 14 | 1 / 2 | — |
| 0x4008EF14, 0x4008EFAC, 0x4008F03E, 0x4008F0C6, 0x4008F13A, 0x4008F1B4, 0x4008EE80, 0x4008EECA | summing, copy, post | 37, 37, 35, 31, 27, 9, 22, 23 | 10, 9, 8, 6, 4, 0, 4, 4 | — |
| 0x40097414 / 0x40096890 / 0x40096802 | delay prep / delay / write DMA | 173 / 229 / 30 | 5 / 34 / 0 | — |
| 0x40096E3C / 0x40096DBA | reverb / line DMA | 407 / 39 | 113 / 0 | — |
| 0x4008FEBC, 0x40097A90 | meters | 32, 7 | 4, 1 | — |

About 45 routines and 3,600 instructions on the digital path (control ~1,250, mix/FX ~2,350), not counting the
interrupt's sequencer part.

Porting order (each step testable alone against the routine run in Unicorn). Steps 1-6 are done (2026-10-23):
`stMainMix`, `stMainControl`, `stMainModulation`, `stMainLfo`, `stMainPitch`, `stMainFilter`, checked by
`stMainOracleTest` on `vectors-main/*.stv` (tools `vectors3.py`, and `sequence3.py` for 900 consecutive blocks of
smoothing, matrix, LFOs, pitch loop, filter envelopes and cutoffs on one carried memory).
1. Harness: section-3 Unicorn rig (memory map above, MACSR 0x20, EMAC sites by `cfdis`), checked on 0x4008ED48 and
   0x400A171A.
2. Smoothing 0x40097952 + lock/snap helpers.
3. Modulation matrix 0x40097AFC.
4. LFO 0x40119266 with its waves, exp and RNG (bit-exact phase, fade, trig modes).
5. Portamento 0x40119FE8 and the pitch loop (bend).
6. Filter envelope 0x4009652C, then cutoff 0x400956BC.
7. Block builder: compare the whole 0x1B0 block with the interrupt segment 0x400A4A00-0x400A4DB8 over random kits,
   events and MIDI sources. With CPU #2 already ported this gives bit-exact per-track stems.
   **Done 2026-10-23** (`stMainBlock`, seam in 1.1): the segment 0x400A49A6-0x400A4DB8 and 0x4011A2FC, checked by
   `stMainOracleTest` on `vectors-main/block.stv` (`tools/block3.py cases`) and `block_sequence.stv` (`block3.py
   sequence`: consecutive blocks with locks, cuts, mutes, reloads), and end to end: the original voice CPU
   (SyntHack's harness) plays each original block into `block_sequence.sto`, the test plays the port's own blocks on
   `syntakt::runBlock` (SYNTAKT_MAIN_OUTPUTS, SYNTAKT_SECTION7, SYNTAKT_SAMPLES) and compares the 8 tracks.
   Results: 600 cases 0 differ; 3 × 300 blocks (2053 calls with the locks) 0 differ; end to end 900 blocks × 8
   tracks × 32 samples 0 differ (every track sounding, peaks −53 to −23 dBFS). Coverage (`block_coverage.txt`):
   the segment 308/308 instructions, 38/38 branch sides; 0x4011A2FC/1E4/250 whole; memset 0x401159A4 and memcpy
   0x40115F78 all but their tails, dead (lengths 0x500 and 20, multiples of 16 and 4).
8. Gains, pans, ramps and summing (0x4008F21E loop, 0x4008EF14 family): DAD0/DFD0 buses without FX.
9. Delay (0x40097414, 0x40096890, 0x40096802) with the DDR ring modelled as the eDMA moves it.
10. Reverb (0x40096E3C, 0x40096DBA).
11. 0x4008F1CA whole: buses, routing flags, DAC write, Overbridge frame.
12. (Not code) model of the analog mixer and FX block, if a "Main" is wanted.

## Unknowns

- Names of the four MIDI sources (46F8/472C/4712 vs AT/MW/BC) and of the settings struct's other fields
  (+0x60, +0x64, +0x68, +0x6C, +0x70, +0x74, +0x79, +0x7A); the event struct's flag bits beyond those used here.
- ~~LFO waveform order and trig-mode numbering~~: read from the code (3.1), 2026-10-23.
- ~~Where the RNG of 0x40118EF8 keeps its seed~~: 0x402D701C / 0x402D7020 (section 3's data), 2026-10-23.
- The names of the glide settings (+0x64 percent, +0x68 legato 0/1/2, +0x6C, +0x70, +0x74) and of the masks
  0x80003152 / 0x80003154 (gate, held [INFER]) as the UI calls them.
- The meaning of masks 0x80003148/4A/4C/4E (FX-bus and send muting) and of 0x43BDE440 (kill).
- The DAC output gain stage (0x401DAFDC, 0x80003144, 0x8000313C/D) and 0x4008EECA's role.
- How the eDMA moves delay/reverb data between DDR and SRAM, exactly (TCD fields not decoded); delay and reverb
  algorithms were located, not decoded. 0x8000DED0's exact content (Overbridge 0-1).
- Whether CPU #2's track stages read block 0x170 (velocity): not checked here (see trackstages.md).
