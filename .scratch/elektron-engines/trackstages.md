# Per-track stages after renderVoices, Syntakt OS 1.41, voice CPU (section 7)

Sources: disassembly of 0x40000934 (audio ISR, tail from 0x40000a3a), 0x4000112c, 0x40002320,
0x400019d6, 0x4000168e → 0x40001476 → 0x40001428, boot 0x400004bc (crt0 data copy) and 0x40001860
(stage state init), DMA setup 0x40001020 and 0x40000ac6. Facts checked on the Unicorn harness
(`C:\dev\Synthack-OS\tools\emu`, probes in the session scratchpad): MACSR and the four
accumulators at each stage entry, every address the stages write, which tables are RAM and which
ROM after boot. Statements without a tag come from the code; **[INFER]** marks interpretation.
Notation follows `sytone.md` / `bdmodern.md`; §0 makes the EMAC rules precise because two of the
four stages run in a different MACSR mode from the voice routines.

---------------------------------------------------------------------------------------------

## 0. EMAC mode and accumulator rules on this path

### MACSR on the path (every write)

| Where | Write | Mode in force |
|---|---|---|
| 0x4000433c (renderVoices entry) | `MACSR = 0xA0` | voice routines: OMC + F/I |
| 0x400044d2 (renderVoices exit) | `MACSR = 0x20` | — |
| 0x4000112c | none | **0x20: fractional, NO saturation, truncate** |
| 0x40002328 (0x40002320 entry) | `MACSR = 0xA0` | 0xA0 in 0x40002320 |
| 0x400024d0 (0x40002320 exit) | `MACSR = 0x20` | — |
| 0x400019e4 (0x400019d6 entry) | `MACSR = 0xA0` | 0xA0 in 0x400019d6 (all 8 calls) |
| 0x40002310 (0x400019d6 exit) | `MACSR = 0x20` | — |
| 0x4000168e, 0x40001476, 0x40001428 | none | **0x20** |

Measured: MACSR is 0x20 (plus N/Z flag bits, 0x24/0x28 seen) at the entry of 0x4000112c,
0x40002320, 0x400019d6 and 0x4000168e; the ISR writes no MACSR and never sets MASK-relative
addressing. No `&` (MASK) forms appear in these routines.

### Arithmetic (from `tools/emu/emac.py`, NXP MCF54418RM ch. 5)
- Accumulator: 48-bit signed, holding the Q31 value × 2^8 (8 guard bits).
- `mac Ry,Rx,ACCn`: `acc += (2·Ry·Rx) >> 24` (floor). `msac`: `acc -= (2·Ry·Rx) >> 24`.
  Each product is truncated separately at 2^-39; sums keep the 8 guard bits until the store.
  `-1 · -1` (0x80000000²) gives +2^39 = +1.0 exactly in the acc.
- Store (`move.l ACCn,Rx`, `movclr.l ACCn,Rx`): `v = acc >> 8` (floor, R/T = 0).
  - MACSR 0xA0: saturated to [0x80000000, 0x7FFFFFFF]. Called **st()** below.
  - MACSR 0x20: `v mod 2^32`, no saturation. Called **stw()** below.
  - `movclr` stores then clears the acc; `move.l ACC,Rx` leaves it.
- 48-bit overflow (sticky saturation with OMC, wrap without) is not reachable with these
  operands [INFER].
- `frac(a·b)` below = the store of one product into a cleared acc, `((2ab)>>24)>>8` =
  `(a·b) >> 31` floor, then st/stw.
- "mac with load" (`mac Ry,Rx,<ea>,Rw`): the multiply reads Ry/Rx before Rw is loaded. Loads
  that run one sample ahead read one long past a buffer's end (harmless).
- `sats.l Dn` (non-EMAC): if V, `Dn = Dn[31] ? 0x7FFFFFFF : 0x80000000`; it only acts after
  the `add/sub` right before it. Every `add.l`/`adda.l`/`asl.l` without a following `sats`
  wraps.

### Accumulators across calls
Measured all four accumulators = 0 at the entry of each of the four stages (machines 0, 4, 6,
10, 11, trigs on all tracks). Each routine ends with a `movclr` of every accumulator it
pre-loads (0x400012c4, 0x400024ac, the last sample loops). 0x4000112c's first `mac`s
(0x4000117a, 0x40001180, 0x4000123a, 0x40001248) and 0x40002320's (0x4000245a, 0x40002464,
0x4000246c, 0x4000247a) accumulate onto whatever is there, so a port must start them at 0.
[INFER] This holds for every engine (each one's last EMAC op is a `movclr`); it was observed
for 5 engines, not proven for all 12.

---------------------------------------------------------------------------------------------

## 1. The ISR tail (0x40000a3a .. 0x40000b1e)

trig = `byte[0x80008180] & ~word[0x80008182]`, gateoff = `byte[0x80008181] | word[0x80008182]`
(d2, d4; see `sytone.md` §1). P_t = 0x80003C40 + 0x8E·t, out_t = 0x80003840 + 0x80·t.

```
0x40004324(out, P, trig, gateoff)                         ; voices, MACSR 0xA0 -> 0x20
0x4000112c(out, P)                                        ; drive, all 8 tracks, MACSR 0x20
0x40002320(out, P, trig)                                  ; BASE/WIDTH, all 8, 0xA0 (trig unused)
for t in 0..7:
    0x400019d6(P_t+0x52, out_t, long[0x80008188+4t], t, trig, (int16)word[P_t+0x82])
                                                          ; multimode filter, 0xA0
0x4000168e(out, P, trig, gateoff, byte[0x800081a8])       ; amp envelope, all 8, 0x20
```
0x40000a60: a2 = 0x80003CC2 + 0x8E·t = P_t + 0x82; `mvs.w (a2)` pushes the RST word sign-
extended; `pea -$30(a2)` = P_t + 0x52.

Also in the ISR: `long[0x8000A07C] = long[0x80008184]` every block (0x4000096c), not read by
these stages.

### DMA (0x40000ac6 .. 0x40000afe), eDMA channel 47, TCD at 0xFC0455E0
Set once at boot (0x40001020): ATTR (+4) = 0x0404 (16-byte source and destination
transfers), SOFF (+6) = 0x10, NBYTES (+8) = 0x10, SLAST (+0xC) = 0, DOFF (+0x16) = 0x10,
DLAST_SGA (+0x18) = 0, CSR (+0x1E) = 0.

Per block:
- SADDR (+0) = **0x80003840**.
- DADDR (+0x10) = `long[0x4004F6E0] + 0x1B0`. long[0x4004F6E0] = 0 (a DDR long never written, §3),
  so the destination is shared RAM 0x000001B0 (CPU #2's address), right after the 0x1B0
  parameter block. The emulator reads its output there.
- CITER (+0x14) = BITER (+0x1C) = 0xDE40: ELINK = 1, LINKCH = 47 (itself), count = 64.
  64 × 16 B = **0x400 bytes** = 8 tracks × 32 samples × int32, track t at +0x80·t.
- `byte[0x4004F6E8] = 1` (DMA in flight); `byte[0xFC04401E] = 47` (SSRT) starts it.
- Then, if `long[0x40040050]`: 0x4000090e toggles `long[0x40040054]` and a GPIO (0xEC09401E /
  0xEC09402A) [INFER: activity LED]; then clears 0x40040050. It does not touch audio.
- Nothing writes 0x80003840 after the start. At the next interrupt, 0x40000a04 waits for
  TCD CSR bit 7 (DONE, 0xFC0455FE) while 0x4004F6E8 is set, before renderVoices
  overwrites the buffer.

So the DMA sends exactly what 0x4000168e leaves in 0x80003840 (§3).

---------------------------------------------------------------------------------------------

## 2. The routines

Signal flow per track, all in place in out_t (int32[32] Q31):
```
voice (0x40004324) -> drive (112c) -> BASE/WIDTH LP->HP (2320) -> multimode filter (19d6)
  -> amp envelope (168e/1476) -> DMA
```
Level, pan, delay/reverb sends and the mix happen on CPU #1 (`audio-path.md`). No stage here
reads an LFO. The filter cutoff arrives already modulated (FREQ + key track + filter env, CPU #1
0x400956BC) as one long per track at block offset 0x188+4t.

### Parameter fields read (all words written by the ISR scatter; none are written by the stages)

| P off | source in 0x1B0 block | read by | read as | role |
|---|---|---|---|---|
| +0x50 | machine bytes 18-19 (blk 20t+18) | 112c | w (int16) | drive amount [INFER: the track's overdrive, internal id 26] |
| +0x52 | blk 0xA0+8t | 19d6 | b (mvs.b) clamped 0..7 | filter TYPE |
| +0x56 | blk 0xA2+8t | 19d6 | w<<16 clamped [0, 0x7F000000]; and b | RESO |
| +0x66 | blk 0xA4+8t | 2320 | w | BASE |
| +0x68 | blk 0xA6+8t | 2320 | w | WIDTH |
| +0x6C | blk 0xE0+14t | 1476 | b | amp ATK |
| +0x6E | blk 0xE2+14t | 1476 | b | amp HOLD |
| +0x70 | blk 0xE4+14t | 1476 | b | amp DEC |
| +0x72 | blk 0xE6+14t | 1476 | w<<16 (mode 1 only) | amp SUS |
| +0x74 | blk 0xE8+14t | 1476 | b | amp REL |
| +0x80 | blk 0xEA+14t | 1476 | b | amp env MODE (0 = AHD, 1 = ADSR) [INFER names] |
| +0x82 | blk 0xEC+14t | 1476 (b), 19d6 (w ≠ 0), 2320 (w ≠ 0) | | RST: reset on trig [INFER name] |

Other per-track inputs: `long[0x80008188+4t]` (blk 0x188: filter cutoff F, Q31, [INFER] ≥ 0),
`byte[0x800081A8]` (blk 0x1A8: bit t forces the amp envelope's kill stage 6 [INFER: voice
steal / mute]). P+0x52's low byte, P+0x56's low byte (as an index) and every other P word
(0x54, 0x58..0x64, 0x6A, 0x76..0x7E) are not read here.

---------------------------------------------------------------------------------------------

### 2.1 0x4000112c(out, P): drive with soft clip, dry/wet, level compensation. MACSR 0x20.

Args at 0x7C/0x80(a7) after `lea -$78` and an 11-register movem. Tracks are processed in pairs
(2i, 2i+1), i = 0..3.

**Gain (block rate), 0x40001140..0x400011a4.**
1. For t = 0..7: `G[t] = 2·(w·w)` (muls.w 16×16 → 32, then add.l, wrapping) with
   w = (int16)P_t+0x50, into 0x800083F0[t].
2. Map, two tracks per iteration, 4 iterations: with c = 0x6C5A83AD (0.84650), k =
   0x13A57C52 (0.15350, c + k = 0x7FFFFFFF):
   `g[t] = stw(acc += P(c, G[t])) + k` (add.l, wrap), written back over 0x800083F0[t].
   `step_g[t] = (g[t] − gprev[t]) >> 5` (sub.l, asr, no sats) into 0x80008410[t];
   gprev[t] = 0x80008430[t]. acc0 serves even t, acc1 odd t, each `movclr`ed.
   The `mac` reading g[t] for the next iteration is fused (load from 4(a0)/8(a0)).

**Soft clip (per sample), per pair, 0x40001236..0x400012c4.** Ramped gains:
a5 = gprev[2i] + n·step_g[2i], a4 = gprev[2i+1] + n·step_g[2i+1] (adda.l, wrap). For n = 0..31,
x = out_2i[n], y = out_2i+1[n]:
```
acc3 += P(a5, x)            ; acc2 += P(a4, y) was done one sample ahead (pre-loop at 0x4000123a)
a4 += step; a5 += step
d0 = stw(acc2) ; d4 = stw(acc3)             ; move.l, accs kept
s2 = d0 >> 31 ; s3 = d4 >> 31               ; sign masks, computed ONCE from the first value
repeat 3 times (0x40001260..0x400012ac), for each of (acc2,d0,s2) and (acc3,d4,s3):
    m = d ^ s                               ; |d| for d >= 0, ~d = |d|-1 ulp for d < 0
    acc -= P(0x80000000, d)                 ; = acc + (d << 8) exactly
    acc -= P(d, m)
    d = stw(acc)                            ; next iteration reads it (acc keeps its guard bits)
Y[n] = movclr(acc2) -> 0x800084D0[n]        ; track 2i+1
acc2 += P(a4, out_2i+1[n+1])                ; fused load of x[n+1] too
X[n] = movclr(acc3) -> 0x80008450[n]        ; track 2i
```
Per pass: `acc ← acc + d − d·m`, i.e. `f(u) = 2u − u·|u|` applied three times to u = g·x, on
the same accumulator (the guard bits of each pass carry into the next). Small-signal gain 8,
saturating smoothly to ±1 [INFER: overdrive curve]. After the loop, `movclr acc2` discards the
pre-accumulated 33rd product.

**Dry/wet weights (block rate), 0x400012d2..0x40001328**, for the two tracks of the pair, with
w = (int16)P_t+0x50 and T81 = RAM table 0x800081B0:

| w | dry_t | wet_t |
|---|---|---|
| w ≤ 0 | 0x7FFFFFFF >> 1 = 0x3FFFFFFF | −stw(P(0, T81[w>>8])) = 0 |
| 0 < w ≤ 0x7FF | (0x7FFFFFFF − (w<<20)) >> 1 | −frac(w<<20 · T81[w>>8]) (msac into cleared acc0, stw) |
| w > 0x7FF | 0 | −frac(0x7FFFFFFF · T81[w>>8]) |

`w>>8` is `asr` of the sign-extended word: negative for w < 0, which reads before the table
(inside the 0x1B0 block) and multiplies it by 0. T81 = 128 × Q31, all negative,
−0.5012 (0xBFD918C3) → −0.3163 (0xD785D93B), so wet = +0.501 … +0.316 [INFER: output level
compensation as drive rises]. The drive is crossfaded in over the first 8 coarse steps, and
**the clean path is at half level (dry = 0.5) when the drive is off**.

**Mix (per sample), 0x40001388..0x400013bc.** Ramped, all four steps `(new − prev) >> 5`
(sub.l, asr, no sats), prev in 0x800083D0[t] (dry) and 0x800083B0[t] (wet):
```
out_2i[n]   = movclr(acc0 = P(dry_2i,n, x[n]) + P(wet_2i,n, X[n]))       ; stw
out_2i+1[n] = movclr(acc1 = P(dry_2i+1,n, y[n]) + P(wet_2i+1,n, Y[n]))
```
dry/wet += step after each sample (adda/add.l, wrap). End of pair: prev dry/wet ← new.
End of call (0x40001400): gprev[0..7] ← g[0..7].

State: 0x80008430[8] gprev, 0x800083D0[8] dry prev, 0x800083B0[8] wet prev. Scratch:
0x800083F0[8], 0x80008410[8], X = 0x80008450[32], Y = 0x800084D0[32], reused by every pair.
Table: 0x800081B0[128], **SRAM**, initialised by crt0 from image 0x40057820 (see §2.6).

---------------------------------------------------------------------------------------------

### 2.2 0x40002320(out, P, trig): BASE/WIDTH band, one-pole LP then one-pole HP. MACSR 0xA0.

Args at 0x58/0x5C/0x60(a7); `trig` (0x60) is never read. Pairs (t, t+1), t = 0, 2, 4, 6.
Table Tbw = 0x40040060 (ROM, 512 × Q31 one-pole coefficients, 0.99935 → 0.046 at 507, then 0
up to index ~1025; the same curve as CPU #1's delay/reverb HPF/LPF table).

Per track, block rate, B = (int16)P+0x66, W = (int16)P+0x68:
- `ph = Tbw[B >> 6]` (HP pole), `pl = Tbw[(B + W) >> 6]` (LP pole). asr; index up to 1023 for
  B + W ≤ 0xFFFE stays in the zero tail. Negative words would index before the table.
- Constants: `kl = 0x80000000 + pl` (add.l: pl − 1.0), `kh = (ph >> 1) + 0x40000000`
  ((1 + ph)/2).

Reset, before the loop: if `byte[0x80008550 + t] ≠ 0 && word[P_t+0x82] ≠ 0` clear track t's
state; if `byte[0x80008551 + t] ≠ 0 && word[P_t+0x82] ≠ 0` clear track t+1's state. **The
second test reads the even track's RST word, not P_t+1+0x82** (quirk, keep it). 0x80008550 is
written by 0x400019d6 at the end of the previous block (§2.3), so the reset lands one block
after the trig, when the voice starts.

Per sample (two tracks interleaved; lp, hp = state; x = out[n]; acc2/acc3 start cleared every
sample by the previous movclr; acc0 starts with `P(pl, lp)` from the previous sample or the
pre-loop `mac` at 0x4000245a):
```
acc0 -= P(kl, x)                     ; acc0 = pl·lp + (1−pl)·x
acc2 -= P(kh, lp_old); acc2 += P(ph, hp)
lp = movclr(acc0)  (st)
acc2 += P(kh, lp)                    ; hp' = ph·hp + (1+ph)/2·(lp − lp_old)
acc0 += P(pl, lp)                    ; next sample's first term, with the load of x[n+1]
hp = movclr(acc2) (st) -> out[n]
```
Track t+1 runs the same on acc1 (LP) and acc3 (HP), the ops interleaved as listed at
0x40002460..0x40002496. After the loop `movclr acc0` discards the 33rd term.
No coefficient smoothing: ph/pl change in steps at block rate.

State: 0x800088A0 + 16·(t/2): {lp_t, hp_t, lp_t+1, hp_t+1}. Cleared at boot (0x40001860).
Output: in place, the HP output.

---------------------------------------------------------------------------------------------

### 2.3 0x400019d6(Pf = P_t+0x52, out_t, F, t, trig, rst): multimode state-space filter. MACSR 0xA0.

Args at 0x68..0x7C(a7) after `lea -$64` + 11 regs. `$44 = out_t`.
Inputs: R = clamp(w(Pf+4) << 16, 0, 0x7F000000) (RESO, at $48; clamp via the not/add/subx/and
max(·,0) idiom then `ble`), F (cutoff), TYPEcur = 0x80008560[t] (type of the previous block,
$34), TYPEnew = clamp(mvs.b Pf, 0, 7).

Coefficient set: 8 longs c0..c7. Target set at 0x80008900 (scratch, one call at a time),
current (ramped) set per track at 0x80008740 + 32·t, steps at 0x80008720 (scratch; also used
as temporaries during the coefficient computation).

#### Group by TYPEcur
| TYPEcur | path | M ($4c) | output mix (g_x, g_A, g_B) |
|---|---|---|---|
| 0 | A | — | (0, 0x7FFFFFFF, 0) : out = A |
| 1 | A | — | (0x20000000, 0x80000000, 0) : out = x/4 − A |
| 2 | B | 0 | (0, 0, 0x7FFFFFFF) : out = B' |
| 3 | B | 0x17000000 | idem |
| 4 | B | 0x2B000000 | idem |
| 5 | B | 0x53000000 | idem |
| 6 | B | 0x7F000000 | idem |
| 7 | A (coefficients computed) | — | (0x20000000, 0, 0) : out = x/4 |

[INFER] 0/1 are the low-pass and high-pass outputs of one resonator, 7 is "off" (x at the
filter's 1/4 output scale), 2..6 a morph among band/notch/peak shapes by M. Names need a check
against the device's TYPE list. Any TYPEcur > 7 cannot occur (TYPEnew is clamped).

#### Path A coefficients (TYPEcur 0, 1, 7), 0x40001a98..0x40001c60
Tables (ROM; ranges reachable for F ∈ [0, 2^31), R ∈ [0, 0x7F000000]):
TR = 0x40013870[128]; T1 = 0x40013070 (idx −169..510, i.e. 0x40012DCC..); T2 = 0x40012BCC
(idx 0..128; entry 128 is 0x40012DCC); T3 = 0x40011E70 (idx −169..510); T4 = 0x400113CC and
T5 = 0x4001082C (idx −232..511, i.e. from 0x4001102C and 0x4001048C); LOG = 0x4000F48C[1024];
EXP = 0x4000E488 (idx 1..1024).
```
i1  = (F >> 22) + (TR[R >> 24] >> 22)                          ; asr
d0  = st(acc0 -= P(T1[i1], R))                                 ; ≥ 0 (T1 < 0)
j   = d0 >> 24 ;  f = (d0 & 0xFFFFFF) << 7
d2  = st(acc0 = −P(T2[j], f) + (T2[j]<<8) + (T2[j]<<8) + P(T2[j+1], f))   ; 2T[j] + f·ΔT
i2  = (d2 >> 1) + (F >> 22)
a   = T3[i1] >> 1 ;  b = T4[i2]
u   = st(acc0 = P(a, b) + P(0x80000000, 0x20000000))           ; a·b − 0.25
q   = st(acc1 = P(b, b)) >> 2
c0  = (−(u − q)) >> 1                                          ; sub, neg, asr: (q − u)/2
q4  = q << 2 ;  a2 = a << 1                                    ; low bits dropped
c1  = st(acc0 = P(a2, T5[i2]))
v   = st(acc1 = −P(c1, c1) + 2^39)        ; move.l (acc1 kept), = sat(1 − c1²)
(s, m) = sqrt(v), 1/sqrt(v)  by SQRT below
c4  = s
c3  = (st(P(st(P(c1, m.mant)), c0))) << m.shift               ; two chained movclr products
c2  = st(P(q4, c4))
c5  = st(acc1 = P(c1, q4))                 ; acc1 was cleared by the movclr at 0x40001bdc
```
c6, c7 are not written by path A (they keep the last path-B values; B' is not mixed in).

**SQRT(v)** (inlined three times, 0x40001b98.., 0x40001d50.., 0x40001dc6..):
```
z  = ff1(v) − 1              ; leading zeros − 1 (ff1(0) = 32)
mn = v << z                  ; bit 30 set
k  = st(P(0x800, mn))        ; = mn >> 20, 1024..2047
L  = (((−LOG[k − 1024]) >> 7) + (z << 24)) >> 1            ; −log2(√v) in Q8.24 [INFER]
s  = EXP[(0x1000000 − (L & 0xFFFFFF)) >> 14] >> (L >> 24)    ; √v
mL = −L ; m.mant = EXP[(0x1000000 − (mL & 0xFFFFFF)) >> 14] ; m.shift = −(mL >> 24)  ; 1/√v
```
(`neg`, `asr #7`, `asl #24`, `add`, `asr #1`, `andi #$FFFFFF`, `(0x400<<14 − f) >> 14` exactly
as written; shifts by register are mod 64.) LOG: −0.9986 → ~0 (log2 of the mantissa, negative);
EXP: 0x40000000·2^(i/1024), EXP[1024] = 0x7FFFFFFF.

#### Path B coefficients (TYPEcur 2..6), 0x40001c72..0x40001ea0
Tables (ROM): D = 0x4000D488, C = 0x4000C488, Bm = 0x4000B488 (1024 each), A1 = 0x4000A188[64],
A0 = 0x40009F88[128], COS = 0x4000A488 (1024 + guard; cos over [0, π] [INFER]).
```
c1 = −D[F >> 21]                                    ; = cos ω [INFER]
e  = st(P(C[F >> 21], Bm[M >> 21]))
k1 = (R < 0x40000000) ? A1[R >> 24] : 0x20000000
e  = st(P(A0[R >> 24], e)) ; e = st(P(k1, e))
e  = min(e, 0x016A09E6)                             ; signed
fr = (e & 0x7FFF) << 16
g  = st(P(0x7FFFFFFF − fr, COS[(e & 0x1FFFFFF) >> 15]) + P(fr, COS[idx+1]))   ; -> [0x80008720]
h  = SQRT(0x7FFFFFFF − st(P(g, g))).s               ; sub.l; √(1 − g²) -> [0x80008724]
(c4, m) = SQRT(0x7FFFFFFF − st(P(c1, c1)))          ; sin ω and its inverse
c7 = h >> 1
c0 = (st(P(h, c4)) >> 2) >> 7
c3 = st(P(st(P(c1, m.mant)), c0)) << m.shift
c6 = (g >> 1) >> 7
c2 = st(P(c4, g)) ; c5 = st(P(c1, g))
```
(c7 and c0's products run on acc0, c1·m.mant on acc1, interleaved as at 0x40001e60..0x40001ea0;
all into cleared accs.) Note path B uses `0x7FFFFFFF − st(P(x,x))` (separate store) where path A
uses `−P(c1,c1) + P(−1,−1)` in one acc; both saturate the same except at |c1| = 1 [INFER].
TYPEcur outside 0..7 or the branch at 0x40001c6e: only (TYPEcur − 2) ≤u 4 computes path B.

#### Ramp or snap, 0x40001ea4..0x40001f72
- snap if `(byte[0x80008550+t] && rst ≠ 0) || byte[0x80008558+t]`:
  cur[t][0..7] = c[0..7], step[0..7] = 0, filter state s1 = s2 = 0 (0x80008920 + 8t),
  byte[0x80008558+t] = 0. (0x80008580[t], 0x800088E0[t], 0x80008840 are not reset.)
- else for i = 0..7: `d = sats(c[i] − cur[t][i]); step[i] = (d + (d < 0 ? 31 : 0)) >> 5`
  (rounded toward zero).

#### Main loop, 32 samples, 0x40001fba..0x4000201c (x = out_t[n], c_i = cur + n·step_i)
```
acc0 += P(c0,x) four times            ; 4·c0·x, each product truncated
acc0 += P(c1, s1) ; acc0 -= P(c2, s2)
acc1 -= P(c3,x) four times
acc1 += P(c4, s1) ; acc1 += P(c5, s2)  ; with the load of x[n+1]
s1 = movclr(acc0) (st) ; A[n] = s1 >> 1          -> 0x80008960[n]
s2 = movclr(acc1) (st) ; B[n] = s2               -> 0x800089E0[n]
```
c0..c3 ramp by `adda.l`, c4/c5 by `add.l` (wrap), each step read from 0x80008720..0x80008734
right after its coefficient's last use in the sample. State s1, s2 → 0x80008920 + 8t.
[INFER] A 2×2 state-space (coupled-form) resonator: s' = [[c1, −c2], [c4, c5]]·s +
[4c0, −4c3]·x.
After the loop cur[t][0..5] = c[0..5] exactly (0x40002032), dropping ramp rounding.

#### Second stage on B, 32 samples, 0x400020c4..0x400020fc
gt = (TYPEcur ≤ 1 || TYPEcur == 7) ? long[0x4000A388] (= 0x4000A288[64] = 0x2080FA39)
: 0x4000A288[b(Pf+4)] (RESO byte, 129-entry table 0.063 → 1.0).
gp = 0x800088E0[t]; `gs = (gt − gp) >> 5` (sub.l, asr, no sats). z = 0x80008580[t].
```
acc0 += P(c7, z)              ; with load x = out_t[n]
acc0 += P(c6, x)              ; with load z' = B[n] (old value)
c6 += step6 ; c7 += step7     ; adda.l, steps 0x80008738/0x8000873C
xs = (x >> 1) >> 7            ; x/256
y  = movclr(acc0)
acc0 += P(gp, xs − y)         ; sub.l, wrap
gp = sats(gp + gs)
u  = (y + xs) >> 2            ; add.l, asr
v  = movclr(acc0) + u         ; add.l, wrap
v  = sats((v << 6) + (v << 6))   ; asl #6 wraps; only the final doubling saturates
B[n] = v                      ; B'[n], in place
z = z'
```
End: 0x80008580[t] = last old B[31]; cur[t][6] = c6 target, cur[t][7] = c7 target
(0x80008918/0x8000891C); **0x800088E0[t] = 0x4000A288[RESO byte] whatever the type** (so for
path-A types the ramp restarts from the RESO value every block; harmless since g_B = 0 there).

#### Type change and trig flags, 0x40002138..0x400021ca
```
keep = 1
if TYPEnew != TYPEcur and ((TYPEcur−2) >u 4 or (TYPEnew−2) >u 4):   ; a group-A type involved
    byte[0x80008558+t] = 1 ; keep = 0                                ; snap next block
if byte[0x80008550+t] (old trig flag, read at entry): 0x80008558[t] = 0x80008550[t] = 0 ; keep = 1
if trig bit t (this block):                          0x80008558[t] = 0 ; 0x80008550[t] = 1 ; keep = 1
0x80008560[t] = TYPEnew
```
Type changes within 2..6 just ramp the coefficients. TYPEnew takes effect in the next block.

#### Output mix, 32 samples, 0x400022d6..0x400022f0
Targets (g_x, g_A, g_B) from the table above by TYPEcur when keep, else (0, 0, 0) (fade out
for a snap). Prev in 0x80008840 + 12t {g_x, g_A, g_B}; steps `sats(target − prev)`, rounded
toward zero, `>> 5`.
```
out_t[n] = movclr(acc0 = P(g_x, x[n]) + P(g_A, A[n]) + P(g_B, B'[n]))     ; st
g_x += s_x (adda) ; g_A += s_A (adda) ; g_B += s_B (add)                   ; wrap
```
Prev ← targets. MACSR → 0x20.

State, per track t: 0x80008550+t (byte, trig seen last block), 0x80008558+t (byte, snap
pending), 0x80008560[t] (type in use), 0x80008580[t] (B-stage z), 0x80008740+32t (cur c0..c7),
0x80008840+12t (mix gains), 0x800088E0[t] (B-stage gain), 0x80008920+8t (s1, s2). Scratch:
0x80008720[8], 0x80008900[8], 0x80008960[32] (A), 0x800089E0[32] (B). Init: 0x40001860 clears
0x80008720, 0x80008900, 0x80008580, 0x800088E0, 0x80008560, the two byte arrays, 0x800088A0,
and 0x800085A0 + 0x30·t (12 longs each, not used by these stages [INFER: dead]); the rest is
zero from crt0.

---------------------------------------------------------------------------------------------

### 2.4 0x4000168e(out, P, trig, gateoff, kill): amp envelope dispatcher. MACSR 0x20.

For t = 0..7: `0x40001476(out_t, E_t = 0x800049E0 + 12t, P_t+0x6A, (trig>>t)&1,
(gateoff>>t)&1, t, (kill>>t)&1)`, kill = byte[0x800081A8] (mvz.b of the long arg's low byte).
Argument t is not read by 0x40001476.

### 2.5 0x40001476(out, E, A = P+0x6A, trig, gateoff, t, kill): AHD/ADSR VCA. MACSR 0x20.

E = {stage, holdcount, level}; level in negative representation (0x80000000 = full).
Reads: ATK = b(A+2), HOLD = b(A+4), DEC = b(A+6), REL = b(A+10), MODE = b(A+22), RST =
b(A+24); SUS = (MODE == 1) ? w(A+8) << 16 : 0 (mvs.w, swap, clr.w).
ROM tables: TA = 0x40014070 (attack increment, −1.0 at 0 → −1491), TH = 0x40013C70 (hold,
samples, 2 → 480000), TD = 0x40013E70 (decay/release coefficient, −0.3728 → −1.0).

Entry: if kill → stage = 6; else if trig && RST → stage = 5. Then segments consume the 32
samples (n left = 32, the loop at 0x4000152e; "consumed" = pointer distance):

| stage | segment |
|---|---|
| 2 attack | if level == 0x80000000: holdcount = 0, stage = MODE ≠ 0 ? 1 : 0, consume 0. Else per sample `out = stw(−P(level, x))`, then `level = level + TA[ATK]`; on V: `level = sats`, stop after this sample. |
| 0 hold | if TH[HOLD] > holdcount: k = min(TH − holdcount, n), holdcount += k, consume k samples **unchanged** (full gain; −P(−1, x) = x bit-exactly). Else if HOLD == 127: consume the rest unchanged. Else stage = 1, level = 0x80000000, consume 0. |
| 1 decay | SEG(coef TD[DEC], target SUS), rest of block |
| 5 reset | `d = level >> 5`; for n: `out = stw(−P(L_n, x[0]))`, `L_{n+1} = L_n − d`; **x[0] is loaded once and never reloaded** (0x400015d2, plain `msac`, no load form: a firmware bug to keep). level ← L_32. |
| 6 kill | SEG(0x8148C842 ≈ −0.98999, target 0), rest of block |
| 3, 4, other | SEG(TD[REL], target 0), rest of block |

**SEG = 0x40001428(buf, &level, c, b, n)**: `k = 0x7FFFFFFF + c` (add.l). Per sample:
`out = movclr(acc0 −= P(L, x))` (stw); `L = movclr(acc1 = −P(L, c) − P(b, k))` (stw), i.e.
L ← −L·c − b·(1+c), a one-pole toward −b. Returns buf + n.

After the block (0x4000161c):
```
if trig:
    if RST: level = (ATK == 0) ? 0x80000000 : TA[ATK] >> 1
    stage = 2
    if gateoff: goto G
elif gateoff:
    if MODE == 1: stage = 4 ; done
    goto G
done
G: if MODE == 0 and HOLD == 127:
       if stage == 0: level = 0x80000000
       stage = 1
```
So a trig only arms the attack at the end of its block (the voice itself starts one block
later, `sytone.md` §1); with RST, the block of the trig ramps the old level linearly to ~0
(stage 5). Gate-off releases in ADSR mode (stage 4, REL), and in AHD mode only cuts an infinite
hold (HOLD = 127) into the decay. A trig and a gate-off in the same block in AHD with HOLD 127
skip the attack (stage 2 → 1). Nothing sets stage 3 [INFER: unused]. With MODE ∉ {0, 1},
attack goes to stage 1 with target 0 and gate-off follows the AHD rule [INFER: not a UI value].

State: 0x800049E0 + 12t {stage, holdcount, level}, zero from crt0 (stage 0 hold, count 0).

### 2.6 Tables

| Address | Kind | Size | Used by | Content |
|---|---|---|---|---|
| 0x800081B0 | SRAM (.data from image 0x40057820) | 128 × Q31 | 112c | wet level, −0.501 → −0.316 |
| 0x40040060 | ROM | 512 + zero tail to ~1025 | 2320 | one-pole coefficients |
| 0x40009F88 .. 0x40013A70 | ROM | see §2.3 | 19d6 | filter tables (path A: 0x4000E488 EXP, 0x4000F48C LOG, 0x4001048C, 0x4001102C, 0x40011BCC, 0x40012BCC, 0x40012DCC, 0x40013870; path B: 0x40009F88, 0x4000A188, 0x4000A288, 0x4000A488, 0x4000B488, 0x4000C488, 0x4000D488) |
| 0x40013C70, 0x40013E70, 0x40014070 | ROM | 128 each | 1476 | hold, decay/release, attack |

"ROM" = inside the section 7 image (0x40000400..0x4005DF10, in DDR); checked identical to the
image after boot. Dump the one contiguous span 0x40009F88..0x40014270 and 0x40040060..0x40041070
from the image rather than regenerating anything.

---------------------------------------------------------------------------------------------

## 3. Memory written, and what the DMA sends

Every address the four stages write, measured over machines 0, 4, 11 with trigs, gate-offs
and a type-2 filter (stack frames excluded):

| Range | Owner |
|---|---|
| 0x80003840..0x80003C3F | out, in place (all four stages) |
| 0x800049E0..0x80004A3F | amp envelope state (168e) |
| 0x800083B0..0x8000844F | 112c state and scratch |
| 0x80008450..0x8000854F | 112c X/Y scratch |
| 0x80008550..0x8000859F | 19d6 flags, types, B-stage z |
| 0x80008720..0x8000889F | 19d6 steps, cur coefficients, mix gains |
| 0x800088A0..0x800088DF | 2320 state |
| 0x800088E0..0x80008A5F | 19d6 gain, targets, s1/s2, A/B scratch |

No stage writes DDR. The ISR around them writes DDR globals 0x4004F6E8 (DMA flag),
0x40040050/0x40040054 (LED), and SRAM 0x8000A07C. long[0x4004F6E0] (DMA destination base) is
never written: its four other references (0x400004c0 crt0 source pointer, 0x40000bb0,
0x40000e4e, 0x40000e9e) only read it, and it holds 0 in the image (it is also the first long of
crt0's .data source).
crt0 (0x400004bc) copies image 0x4004F6E0..0x40057670 to SRAM 0x80000000.., zeroes to
0x80008000, copies 0x40057670..0x4005DF10 to 0x80008000.. and zeroes to 0x80010000: all stage
state starts at zero.

**Output buffer:** 0x80003840 + 0x80·t holds track t after the whole chain (drive → BASE/WIDTH
→ filter → amp envelope), int32 Q31, 32 samples, in place. No per-track level, pan, send or
sum happens on this CPU. The DMA sends those 0x400 bytes unchanged to shared RAM 0x1B0.

---------------------------------------------------------------------------------------------

## 4. Porting order (leaves first)

1. EMAC primitives with a **mode flag**: P(), acc add/sub, st() (0xA0) and stw() (0x20); sats;
   ff1. Reuse the voice port's EMAC layer and add the non-saturating store.
2. 0x40001428 SEG and 0x40001476 / 0x4000168e amp envelope (MACSR 0x20, three ROM tables).
   Test stage by stage, including stage 5's x[0] bug, the attack overflow stop, kill (stage 6)
   and the end-of-block trig/gate-off logic.
3. 0x40002320 BASE/WIDTH (one ROM table, 4 longs of state per pair). Test the RST quirk
   (track t+1 reset gated by track t's RST).
4. 0x4000112c drive (MACSR 0x20, the 0x800081B0 table). Test the three-pass soft clip with the
   guard bits kept, the once-computed sign masks, the w ≤ 0 / ≤ 0x7FF / > 0x7FF branches.
5. 0x400019d6 in pieces: SQRT helper; path A and path B coefficient functions (pure, test over
   F × R × TYPE); ramp/snap; main loop; B stage; flags; output mix. The 8 calls share the
   scratch at 0x80008720/0x80008900/0x80008960/0x800089E0, so per-track state must be kept
   per track and scratch per call.
6. The ISR tail in order (voices → 112c → 2320 → 19d6 × 8 → 168e), with the one-block
   relations: 0x80008550 set by 19d6 in block k is read by 2320 and 19d6 in block k+1; TYPEnew
   takes effect in block k+1; a trig arms the amp attack at the end of block k.

Test against the emulator by calling each routine from a snapshot (`tools/oracle.py`,
`Snapshot.call`) with MACSR set as the ISR leaves it (0x20) and the accumulators at 0, then end
to end on the 0x400 bytes the harness reads at 0x1B0. None of this was ported or compared yet.
