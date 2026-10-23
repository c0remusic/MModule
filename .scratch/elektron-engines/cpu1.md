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

### Corrections from the port of steps 8-11 (2026-10-23, `stMainBus`, `stMainFx`, `stMainDelay`, `stMainReverb`, `stMainOutput`)
- [CORR 4] 0x4008F1CA reads only its third argument (P). The DAC half it writes is 0x80000000 + (long 0x4553F6DC << 10).
  MACSR: 0x20 at entry; 0x40097414 sets 0xA0 then 0x20 (the track loop runs at 0x20); 0xA0 from 0x4008F768 to the end
  (0x4008EECA switches to 0x80, integer with saturation, and back); 0x20 at exit.
- [CORR 4.2] An analog track's gain is × 0x401DC5E0[signed byte P+0x3E] unless its machine id is 32 or 33 (the test
  is `(id − 32) & 0xFF > 1`), not "UT NOISE/IMPULSE". An analog track's ADC direct gains (entries 0-3 of 0x8000E0F0 /
  0x8000E0D0) are gain × pan only when ROUT ≠ 0 and its bit of the FX bus mask is clear; else 0.
- [CORR 4.2] The masks: d4 = signed word 0x80003148, or 0x8000314C when 0x80003148 is negative, is the **FX bus mask**
  (bit t: track t leaves the FX bus; bit 12, 13: the delay's, the reverb's return level is 0; bit 14: the FX track's
  input loses its direct gain). −0x1FC(a6) = 0x8000314A, or 0x8000314E, is the **send mask** (bit t: track t's sends
  are 0; bit 12: the delay→reverb send is 0). cpu1.md had "muted by bits 12/13 of the mute mask": of the FX bus mask.
- [CORR 4.3] The ADC side is 8 channels, all through the 8-channel sum 0x4008F03E: 0-3 the analog tracks, 4-5 the
  external input (level +0x70A, halved when byte 0x80003146 = 0, pan +0x70E, sends +0x710 / +0x714, direct while
  +0x71A ≠ 0), 6-7 the FX track's input (LEV word P+0x18 × +0x748, squared, pan +0x746, × the long 0x454E3C18 + 4 s
  for L and 0x454E3C38 + 4 s for R, s = signed byte +0x71C clamped 0-7; sent to the delay only while the delay is
  routed to the FX track (+0x6F6 ≠ 0) by +0x742, to the reverb while +0x708 ≠ 0 by +0x744). 0x4008F13A and
  0x4008F0C6 are not called by 0x4008F1CA.
- [CORR 4.5] The delay→reverb send is 0 when the reverb is not routed to the FX track and the word +0x742 is > 0, or
  by bit 12 of the send mask; else −2 (P+0x6F2)².
- [CORR 4.7] 0x8000DED0 at the end = (ADC × 0x8000E0F0 / 0x8000E0D0: the analog tracks routed to the FX track, the
  external input's and the FX track input's direct) + 0x8000DFD0 (0x4008EE80 at unity), × 8 (0x4008EECA). 0x4008EECA
  is `sat32(x × n)` (MACSR 0x80): 0x8000DAD0 × 10, 0x8000DFD0 × 8, 0x8000DED0 × 8.
- [CORR 4.8] DAC slots, per frame of 8 longs: 7 and 2 = (3 g1 × DAD0 − 3 g0 × X) >> 8, X the slots 2 and 3 of a second
  TDM ring at 0x8000E1B0 + (long 0x43BDC4D8 << 10); g1 and g0 ease by 0.99 / 0.01 per frame (0x7EB851EB, 0x0147AE14,
  state 0x43BD7688 / 0x43BD7684) to T = 0x401DAFDC[signed byte P+0x740] and T >> (3 − byte 0x80003144). 5 and 3 =
  (−[0x43BDBF64] × B[i] + (INT32_MIN >> (3 − byte 0x80003144)) × X slots 0, 1 + (−1) × DFD0) >> 8, B the 32 longs
  at 0x43BDBF68 (mono), the first and last gains 0 when byte 0x8000313D is 0, or 1 with byte 0x8000313C ≠ 0.
- [CORR 4.9-10] The meters loop is not UI only: 0x4008FEBC **scales each track's buffer in place** (−x × g, g ramped
  from 0x43BD768C[t] to the new gain), g = 0x4008ED48(level, velocity, VOL +0x7C, kit byte) at MACSR 0xA0, level =
  the LEV word P[2t] when byte 0x80003145 ≠ 0, else 0x7F00 (t ≤ 7) or 0x40097A90(machine id, 0x7F00) =
  −0x401E08E0[id] × 0x7F00 (t ≥ 8). Overbridge (32 frames × 20 longs at 0x80004760): 0-1 = 0x8000DED0 as above;
  **2-9 = the digital tracks after that scaling** (velocity curve × VOL², × LEV² when 0x80003145 is set; post-gain,
  pre-pan, phase inverted), not CPU #2's raw output; 10-13 = ADC 0-3 scaled likewise; 14-15 = ADC 6-7 raw;
  16-17 = sat(delay return + reverb return); 18-19 are not written here.
- [CORR 4.4-5] The eDMA, read from the code: channel 30 (TCD 0xFC0453C0), programmed and started (CSR 0x11 = START +
  ESG) at the end of 0x40097414, reads at the start of the block: the delay's tap A (the new time) 17 × 16 bytes
  into 0x8000D6D0 − (address & 15), then by scatter-gather tap B (the last block's time, descriptor 0x8000DAA0) into
  0x8000D7E0, the 8 diffusers' read positions (descriptors 0x8000C8C0 + 32 k, 9 × 16 bytes into 0x8000D120 + 0xA0 k)
  and the 9 lines' (0x8000C9C0 + 32 k, 9 × 16 bytes, lines 4 and 8 10 × 16, into 0x8000CAF0 + 0xB0 k). Source
  modulo 2^22 (the ring), 2^12, 2^16. Channel 42 (0xFC045540), started by 0x40096802 after the reverb, writes 256
  bytes from the pointer at 0x8000D6B0 into the ring at the old write position, then by scatter-gather the 8 diffusers'
  and the 9 lines' 128 bytes from their SRAM buffers to their write positions (0x40096DBA aims them; descriptors
  0x8000C6A0 + 32 k, 0x8000C7A0 + 32 k; destination modulo 2^12, 2^16). Descriptors' fixed fields: 0x40096668 (delay
  init) and 0x40096B86 (reverb init). 0x4008F80A polls channel 30's ESG: clear once the last descriptor is loaded.
- [CORR 5] The SRAM is initialized at the start (0x4000045E): 0x40339000-0x40340330 to 0x80000000, 0x40340330-0x40347B90
  to 0x80008000, the rest zero. It holds the pointer 0x8000D6B0 = 0x8000D6D0, the diffusers' lengths 0x8000D670
  (53, 73, 113, 163, 251, 367, 491, 809 samples, ×4 bytes) and gains 0x8000D630, the lines' lengths 0x8000C63C (pre-delay
  2000, then 2399, 3359, 4721, 7004, 2111, 3109, 4507, 6704 samples), the sine's rate 0x005937C8 at 0x8000C510.
- [CORR 4.4] 0x8000DAC4 is the last block's delay time, 0x8000DAC8 the ring's write position (frames).
- The EMAC: 0x40096890 leaves accumulator 2 holding the fade-out weight × tap B's frame 32 L from its first loop into
  its second (the high-pass's), an accumulator carried between loops; the port keeps four accumulators and the MACSR
  through the routines (`Emac4`). Assumed and checked by listing only: every routine the interrupt runs between the
  block segment and 0x4008F1CA ends each accumulator with a `movclr`, so they are clear at its entry.

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

Ported (2026-10-23, `stMainOutput` and the files below; [CORR] in section 0). Input: CPU #2's previous block at
0x80003D50 (8 × 32 int32; one block old). ADC: 0x80003950 (8 ch: 0-3 analog tracks, 4-5 external in, 6-7 the FX
track's input). The routine reads only P (its first two arguments are not used). Gains are negative (INT32_MIN full);
every one ramps over the block, step (target − current) >> 5 added after each sample.

1. `0x40097414(P)` (173, 5 EMAC, `prepareEffects`): the reverb's diffusers' and lines' read positions (write
   position − length) into channel 30's descriptors; the reverb's sine oscillator (a rotation by 0x8000C510, outputs
   0x8000C514 / 0x8000C518, the second also at 0x4550E1C8) and line 4's modulated position; the delay's time
   `(TIME + 256) × 48000 >> 10 × 900 / tempo` frames, clamped (unsigned) to 0x5DC00, eased by about 1/256 per
   block (sum = 255 (s >> 1) + 32 (t >> 1), 0x8000DAC0 = sum >> 7, which settles at 32 t; the time used is sum >>
   12), its two taps (the new time, the last block's at 0x8000DAC4) behind the write position 0x8000DAC8; then
   channel 30 runs (CORR 4.4-5).
2. Per track t 0..11 (0x4008F21E-0x4008F474, `trackSends`):
   - gain `g = 0x4008ED48(level word P[2t], velocity, VOL +0x7C, kit byte (0x800030B8 + 0x123 t)+205)`:
     velocity curve table 0x401DB1DC[(mode & 3) × 128 + vel] × (LEV × VOL)² (frac EMAC, 38 instr.).
     [CORR] An analog track's × 0x401DC5E0[signed byte P+0x3E] unless its machine id is 32 or 33.
   - pan `0x400A171A(PAN +0x7A)`: constant power via sine `0x400A16BC` (30 + 14 instr.).
   - Digital t: direct-bus targets (0x8000E188 L / 0x8000E160 R) = g × pan unless **ROUT +0x7E ≠ 0**; FX-bus
     targets (0x8000E138 / 0x8000E110) = g × pan when ROUT ≠ 0 and bit t of the FX bus mask (0x80003148, or
     0x8000314C if 0x80003148 < 0) is clear. [CORR] Analog t: the ADC direct gains 0x8000E0F0 / 0x8000E0D0
     (entries 0-3) take the FX-bus rule.
   - Sends: DEL +0x76 and REV +0x78 as −2 v² (Q31), × g × pan, zero if bit t of the send mask (0x8000314A, or
     0x8000314E) is set; current gains at 0x80006328 / 0x80006308 (delay L/R), 0x800062E4 / 0x800062C0 (reverb) for
     digital t, entries 0-3 of 0x800062A0 / 0x80006280 / 0x80006260 / 0x80006240 for analog t.
3. External in and FX track input (0x4008F478-0x4008F76C): entries 4-7 of the ADC arrays (CORR 4.3).
4. Delay bus 0x8000DBD0 = Σ digital sends (8-ch sum `0x4008F03E`) + the 8 ADC channels' sends (the same sum into
   0x8000DED0, added saturating at 0x4008F7E6). The poll of channel 30 (0xFC0453DE bit 4). **Delay
   `0x40096890(out 0x8000DCD0, in 0x8000DBD0, P+0x6E6)`** (4.1). `0x40096802` (30, `writeLines`) moves the write
   position 32 frames on (mask 0x7FFFF) and starts channel 42.
5. Delay return → channel 8 of the track buffer (0x80004150, copy `0x4008F1B4`, one side at a time); delay→reverb
   send (CORR 4.5). Reverb bus 0x8000DBD0 = Σ reverb sends (9-ch sum `0x4008EFAC`: 8 digital + the delay channel) +
   the 8 ADC channels'. **Reverb `0x40096E3C(out 0x8000DDD0, in 0x8000DBD0, P+0x6FA)`** (4.2), then `0x40096DBA`
   (39, `advanceReverbLines`): the lines' write positions (0x8000D690, 0x8000C660) into channel 42's descriptors,
   then 0x80 bytes on. Reverb return → channel 9 (0x800041D0).
6. Return levels: delay VOL P+0x6F4, reverb VOL P+0x706 (−2 v², zero by bits 12/13 of the **FX bus mask**);
   routed to the direct bus (0x8000E1A8 / E180, E1AC / E184) or the FX bus (E158 / E130, E15C / E134) by **delay FX
   routing P+0x6F6 (id 25)** and **reverb FX routing P+0x708 (id 34)**.
7. Buses (10-ch sum `0x4008EF14`, `msac` with per-sample gain ramps): **0x8000DAD0** (L at +0, R at +4, 8 B frames)
   = digital tracks not routed + the returns not routed; **0x8000DFD0** = digital tracks with ROUT + the returns
   routed to the FX track. Then 0x8000DED0 (CORR 4.7). `0x4008EECA(bus, n)` = sat32(x × n): DAD0 × 10, DFD0 × 8.
8. DAC write (0x4008FBF0-0x4008FD68): CORR 4.8. Both buses then enter the **analog** mixer and the analog FX block;
   nothing digital follows.
9. Overbridge gains (0x4008FD6A-0x4008FE2A): each track's buffer scaled in place (CORR 4.9-10).
10. Overbridge frame **0x80004760** (32 frames × 20 longs):

| Channel | Content |
|---|---|
| 0-1 | 0x8000DED0: the ADC side's direct mix (analog tracks routed to the FX track, external input, FX track input) + the FX bus 0x8000DFD0, × 8 |
| 2-9 | the digital tracks 1-8: CPU #2's output × −(velocity curve × VOL², × LEV² when byte 0x80003145 ≠ 0), ramped; pre-pan, pre-send |
| 10-13 | ADC 0-3 (analog tracks 9-12), scaled likewise (the machine's level 0x401E08E0 when 0x80003145 is 0) |
| 14-15 | ADC 6-7, as read |
| 16-17 | delay return + reverb return, saturated |
| 18-19 | not written by 0x4008F1CA |

### 4.1 The delay, 0x40096890 (229 instructions, 34 EMAC; `stMainDelay`)

A stereo delay on a ring of 2^19 frames (4 MB at 0x4FC00000; the longest time, 0x5DC00 frames, is 8 s), read by
channel 30 at two taps and written by channel 42, 32 frames per block. Per block: tap A at the new smoothed time,
tap B at the last block's, crossfaded linearly over the block (B's weight from 0x8000D908 down, A's up to full): a
time change glides without a click. The sum is soft-clipped (y = 2x − x|x|), scaled by 1/32 (ramped from
0x8000D90C), then a one-pole high-pass (pole 0x4029BFFC[HPF >> 6]) and a one-pole low-pass (pole 0x4029BFFC[(HPF +
LPF) >> 6]), × 16: that is the return (0x8000DCD0). The ring's next input, in place in the tap buffer: FDBK × −66053
(saturated, ramped from the last block's, 0x8000D918) times the return, L and R swapped when X ≠ 0 (ping-pong), plus
the input through four width gains from WID (with X: both inputs summed, (1 − w) to L, w to R; without: L' = (w +
½) L + (½ − w) R, R' = 2w R for w ≤ ½, the mirror above), ramped (0x8000C300).

### 4.2 The reverb, 0x40096E3C (407 instructions, 113 EMAC; `stMainReverb`)

A feedback delay network of eight lines in two groups of four, after a pre-delay and eight diffusers:
- Pre-delay: line 0 (64 KB at 0x4D570000), (PRE² >> 16) + 37 samples, at most 16384; its input the mono (L + R) / 2;
  its output through a one-pole low-pass and high-pass (LPF, HPF, poles 0x4029BFFC).
- Diffusion: 8 Schroeder allpasses in series on 4 KB lines at 0x4D600000 (53 to 809 samples), gain g_k =
  0x8000D630[k] × 0x4029B6FC[DEC >> 8] (0x5B851EB8 above 63): y = −g x + (1 − g²) d, line input x + g d.
- Two halves, lines 1-4 and 5-8 (64 KB each, 2399, 3359, 4721, 7004 and 2111, 3109, 4507, 6704 samples): each line's
  output through a first-order shelving filter (coefficients f = 0x4029B7FC[FREQ >> 8], f ± (1 − f) GAIN, 2f − 1);
  the first line of a half also takes the diffused input; the fourth (lines 4 and 8) is read at a position the
  sine moves by about ±8 samples, interpolated with 4 taps of a 32-phase table (0x4029B9FC) and its fraction. The
  return (0x8000DDD0) sums four lines on each side: L = lines 1, 6, 7 (the last block's, read before the second half
  rewrites it) and 8, R = lines 2, 3, 4 and 5, lines 4 and 8 taken before their shelf. Each half's lines' inputs are
  its four outputs through a 4 × 4 sign matrix (+ on the diagonal, − elsewhere), input j scaled by the long j of
  0x401DF8E0 + 32 DEC (+ 16 for the second half).
- Its state: 0x8000C310-0x8000C63C (filters, the half's four outputs, the shelves), 0x8000D650 (the diffusers'
  gains); the lines' buffers 0x8000CAF0 + 0xB0 k and 0x8000D120 + 0xA0 k, read and written by channels 30 and 42.

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
| 0x40096890 + 0x40097414 + 0x40096802 | P+0x6E6, tempo, the taps' buffers, the ring | 0x8000D908.., 0x8000DAC0.., 0x8000C300, the ring | yes: `tools/fx3.py` runs channels 30 and 42 at their CSR write (CORR 4.4-5) |
| 0x40096E3C + 0x40096DBA | P+0x6FA | 0x8000C310-0x8000D650, the lines | the same |
| 0x4008F1CA | all buses, ADC, P, the SRAM image | buses, DAC ring, 0x80004760 | yes (fx3: SRAM image, the inits, peripherals as RAM, the DDR lines); the poll passes since the chain has ended |

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
   **Steps 8-11 done 2026-10-23** (`stMainEmac`, `stMainBus`, `stMainFx`, `stMainDelay`, `stMainReverb`,
   `stMainOutput`; section 4, CORR in section 0). Tools: `tools/fx3.py` (the rig with channels 30 and 42 of the eDMA,
   the SRAM image, the inits), `tools/mix3.py cases` (`vectors-main/mix.stv`: the 9 summing and scaling routines,
   0x40097414, the delay, 0x40096802, the reverb, 0x40096DBA, the segment 0x4008F1DE-0x4008F80A and 0x4008F1CA, 250
   cases each, plausible and one in four at edges, and the two inits) and `mix3.py sequence`
   (`mix_sequence.stv/.sto`: 2 × 300 blocks of the block segment and 0x4008F1CA on one memory, the delay's and the
   reverb's tails sounding, one with a feedback that saturates and one that dies away). Results: 3752 cases 0
   differ; the sequences' 1959 calls 0 differ; end to end, 600 blocks of the port's own blocks played by the ported
   voice CPU and mixed by the port (buses, returns, DAC slots, Overbridge frame) 0 differ. Coverage
   (`mix_coverage.txt`): every routine and the segment 100 % of instructions and branch sides, but the poll's
   loop-back 0x4008F816, dead (the eDMA has ended when the code polls).
   The port's memory for the effects: SRAM as on the device (the image, `loadSramImage`, then `delayInit`,
   `reverbInit`), the reverb's 576 + 32 KB of lines and the delay's ring as the code addresses them, 2^19 frames (4
   MB, the write position runs through all of it in 16384 blocks, 11 s), held in 4 KB pages allocated as written.
12. (Not code) model of the analog mixer and FX block, if a "Main" is wanted.

## Unknowns

- Names of the four MIDI sources (46F8/472C/4712 vs AT/MW/BC) and of the settings struct's other fields
  (+0x60, +0x64, +0x68, +0x6C, +0x70, +0x74, +0x79, +0x7A); the event struct's flag bits beyond those used here.
- ~~LFO waveform order and trig-mode numbering~~: read from the code (3.1), 2026-10-23.
- ~~Where the RNG of 0x40118EF8 keeps its seed~~: 0x402D701C / 0x402D7020 (section 3's data), 2026-10-23.
- The names of the glide settings (+0x64 percent, +0x68 legato 0/1/2, +0x6C, +0x70, +0x74) and of the masks
  0x80003152 / 0x80003154 (gate, held [INFER]) as the UI calls them.
- Which settings write the masks 0x80003148/4A/4C/4E (what their bits do is read, CORR 4.2), and 0x43BDE440 (kill).
- What the DAC stage's other inputs are (the TDM ring 0x8000E1B0, the 32 samples at 0x43BDBF68 and their gain
  0x43BDBF64) and which settings 0x401DAFDC[P+0x740], 0x80003144 and 0x8000313C/D stand for (CORR 4.8).
- ~~The eDMA's moves, the delay and reverb algorithms, 0x8000DED0~~: read and ported, 2026-10-23 (CORR 4.4-5, 4.1, 4.2).
- What the kit byte at +205 of each track's 0x123 bytes (the velocity curve's mode) and the pointer 0x800030B8 are.
- Whether CPU #2's track stages read block 0x170 (velocity): not checked here (see trackstages.md).
