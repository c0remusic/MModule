# Pitch and Decay, as MDX X.20 converts a User Machine's settings

Research for ticket 10 of the map "User Machines from X.20 in MoDule", "Lire les conversions Pitch et Decay dans l'OS
X.20". Written 2026-10-17 against `main` at 18f6b91, whose working tree holds another session's uncommitted
`convertFine`; nothing in the repository was built, run or changed apart from this note, and no part of X.20, the zip
or a flash image was copied into it. Read statically: X.20 B's 68k OS as research/01 decoded it (payload 3,
`$200000-$2be93c`), disassembled with Musashi's disassembler; the section 0 of the user's OS 1.63 flash image, for the
one table X.20 imports from it; the MDS Standard (`docs/MDX_MachineDumpStandard.txt` of jmamma/MDS) and X.20's readme.
No firmware was booted: the RAM dumps that ticket 02 took stop before these tables are built (all zero there). Python
scripts in the session scratchpad (`x20conv/`: `conv.py`, `tables.py`, `fetchlist.py`, `decaylaw.py`,
`decayfit.py`) replay the table builder in integers and compute the values below.

Marks: **[V]** read in X.20 B's code at the address named; **[M]** measured or computed by a script here; **[I]**
inferred from the facts around it. Every address is X.20 B's unless it says 1.63.

## In short

- One routine, `$21aeba`, converts all 8 parameters of every User Machine. It takes the Track's 14-bit parameter words
  (setting << 7, plus modulation), clamps each to [min << 7, max << 7] and dispatches on the low nibble of the
  conversion byte alone. No MAC instruction is on this path. [V]
- **Pitch, DEFAULT:** P = L[(v >> 4) & 127] << (v >> 11), where L holds 128 entries of 2048 · 2^(k/127.5). At a whole
  setting s, P ≈ 2048 · 2^(s/16): 16 settings per octave, from 2,048 to 503,296, 43.07 Hz to 10.58 kHz by the
  Standard's formula. [V]/[M]
- **Pitch, TONAL modes:** P = W[d mod 192] << (⌊d/192⌋ + a), with d = (v >> 4) + 192. W holds 192 entries of
  1673.13 · 2^(i/192), and a is 0, −1 or +1 for TONAL, TONAL-1 and TONAL+1. That makes 2 settings per semitone, and
  setting 64 is MIDI note 69 (A), as X.20's own note display shows it. TONAL-1 is exactly half of TONAL, TONAL+1
  exactly double. [V]/[M]
- **Tuning:** the Global TUNING in cents scales both tables by 2^(c/1200). The legacy setting (A=433 Hz) changes only
  W, and puts TONAL's A at P = 20,920, which is 439.9 Hz by the Standard's formula. TUNING 0 ("A=440 Hz") puts it at
  P = 21,248, which is 446.8 Hz by that formula. [V]/[M]
- So X.20 places its A=440 26.5 cents above the Standard's formula. Its tables fit a codec rate near 43.4 kHz:
  100 MHz / 2,304 cycles per frame gives 439.75 Hz and 432.96 Hz, X.20's two labels. They do not fit the 44,100 Hz
  that the Standard states. [I]
- **MDM1's TONAL flag** does not enter the conversion. It lets EDIT KIT offer the Track's TUNING choice and the note
  display. The Track's tuning mode (0 to 3) then applies to every Pitch parameter of the Machine. [V]
- **Decay:** D = T[v >> 5], where T is OS 1.63's own 512-entry decay table at 1.63 `$24bf94`. X.20 imports it byte
  for byte, and 1.63's built-in Machines read it too. At a whole setting, D = T[4s]. Roughly D = exp(−1/τ) with
  τ = 3.37 · 2^(4s/29.52) samples, so −60 dB takes 0.53 ms at 0, 0.22 s at 64 and 82 s at 127. MoDule already holds
  this table in the user's 1.63 flash image. [V]/[M]
- **The other conversions** match our `convert` at whole settings, except Unipolar at 127, where X.20 gives $7FFFFF
  and we give $7F0000. Between settings, X.20 truncates Index and counts any nonzero word as Boolean 1. Its Unipolar
  gives 0 below one setting and $7FFFFF from 127. STEREO PAIR works as the Standard says. [V]

## 1. Where the conversion runs

- **Tick [V].** `$2a5164(engine, group)` serves Tracks 4·group to 4·group+3. When a Track's coefficients are due, it
  calls the Track's coefficient routine through `$294118` (a trampoline) with two arguments:
  - **context:** engine + `$125c` + `$98` · Track, 152 bytes;
  - **words:** engine + `$cb8` + 76 · Track, the Track's 38 parameter words. The LFO code writes the same words
    (`$2a6e2a`, `$3f80` − x), so they hold setting << 7 with modulation, as 1.63's do (`HostModel.cpp`, `kWork`)
    [I].
- **Before the call [V]** (`$2a57b0-$2a57d8`), the tick writes:
  - context +`$84`: the Track;
  - +`$88`: the Machine id;
  - +`$8c`: the tuning mode (section 3.3);
  - +`$90`: engine + 4, the set of tables imported from 1.63 (section 4).
- **Which routine [V].** At assignment (`$2946da`), ids 194-209 (USR-01 to USR-16) get `$21aeba`. Every other id
  gets its routine through `$2b0d04`, as research/01 found.
- **`$21aeba(context, words)` [V].**
  - **Descriptor.** `$217892(id)` returns the installed Machine's descriptor: 8 triples (minimum, maximum, conversion
    byte) at +0, the parameter count at +`$18`, MDM1's flags at +`$19`.
  - **Each parameter p below the count.** v = clamp(words[p], min << 7, max << 7). Then an 8-entry jump table on
    (byte & `$f`); a nibble above 7 makes the routine return 0 words. The result goes to output word p + 1, which is
    Y:(R6+1+p).
  - **Result.** The routine returns count + 1 words and leaves word 0 to its caller, the convention of 1.63's
    routines. It has no `tst.l (out)` gate, so it converts at every call.
  - **STEREO PAIR (flags bit 4).** Words count+1 to 8 are zeroed and word 9 gets context +`$8e`, the role; the
    routine then returns 10. `$2178ca` sets that role: 1 and 2 for two adjacent Tracks (not 16 with 1) that hold the
    same Machine id, 0 for a Track alone. That is the Standard's rule (`:464-494`).
- **No MAC.** The conversion, its helpers and the table builder use the integer unit and a 64-bit multiply helper
  (`$2a7098`, a `__muldi3`). [V]

## 2. The eight conversions

| Byte nibble | Conversion | X.20, from v (14-bit) | At a whole setting s | Our `convert` / `convertFine` |
|---|---|---|---|---|
| 0 | Raw 14-bit | v | s << 7 | same |
| 1 | Unipolar Q23 | `$218600`: 0 if v ≤ `$7f`; `$7fffff` if v ≥ `$3f80`; else v << 9 | 0; s << 16 for 1-126; **`$7fffff` at 127** | v << 9: `$7f0000` at 127 |
| 2 | Squared Q23 | (v · v) >> 5 | s² << 9 (`$7e0200` at 127) | same |
| 3 | Bipolar Q23 | (v << 10) − `$800000` | (s − 64) << 17, low 24 bits | same |
| 4 | Pitch | `$2189b0(v, mode)` → `$218184(v, mode, 192)` | section 3 | a MIDI-note guess |
| 5 | Boolean | 1 if v ≠ 0 | 0 or 1 | same at whole settings; `convertFine` takes v ≥ 64 |
| 6 | Index | (v >> 7) − min | s − min | same at whole settings; `convertFine` rounds, (v + 64) >> 7 |
| 7 | Decay | `$2189c6(context, v >> 5)` | section 4 | a 5 ms to 10 s guess |

[V] for the X.20 column, read at `$21af02-$21afa8` and in the helpers named. The upper nibble (the control style)
plays no part in the conversion. [V]

## 3. Pitch

### 3.1 The tables

- **Rebuild [V].** `$21bd6c(c)` builds them, called through `$21bfb6`. That happens at boot (`$2ac544` with the
  stored tuning, then `$2ac66c` with 0) and from the TUNING setter (`$231104`). `$21bfb6` clamps c to −50..+50 (or
  −128, the legacy setting) and keeps it at `$2cdc7c`. It rebuilds only while the rate marker `$2cdc78` holds
  44,100.
- **Arithmetic [V]/[M].** Values have 24 fraction bits in 64 bits. Every step is x ← (x · m + 2^23) >> 24, through
  `$2a7098`.
- **Cents [V]/[M].** The start value is multiplied |c| times by m = `$10025de`/2^24 (2^(1/1200)) when c > 0, or by
  `$ffda28`/2^24 (2^(−1/1200)) when c < 0. The legacy setting skips this.
- **W, 192 words at `$2cdc80`, for the TONAL modes [V]/[M].**
  - Start: x₀ = `$6_892121f0`/2^24 = 1673.1294 (times the cents).
  - Entries: W[i] = round(xᵢ), with xᵢ₊₁ = xᵢ · `$100ed06`/2^24 (2^(1/192)).
  - Legacy: x₀ = `$6_6fada09f`/2^24 = 1647.6782, no cents, and W[i] = floor(xᵢ).
- **L, 128 longs at `$2cde00`, for DEFAULT [V]/[M].**
  - Start: x₀ = 2048 (times the cents; legacy 2048).
  - Entries: L[k] = round(xₖ), with xₖ₊₁ = xₖ · `$1016541`/2^24.
  - That ratio is 2^(1/127.5), not 2^(1/128). The octave does not close exactly (r^128 = 2.0054), so DEFAULT strays
    up to 4.3 cents from 2048 · 2^(s/16). [M]
  - Legacy and TUNING 0 give the same L. [M]
- **A third table [V].** 192 longs at `$2ce000` start at `$5e7ebde0` · 2^(−c/1200), shrink by 2^(−1/192) per entry
  and are stored >> 10. User Machines do not use it.

### 3.2 The conversion, `$218184(v, mode, 192)` [V]

mode is context +`$8c`: 0 DEFAULT, 1 TONAL, 2 TONAL-1, 3 TONAL+1.

- **DEFAULT:** P = L[(v >> 4) & 127] << (v >> 11), with v at most `$3fff`. At a whole setting,
  P(s) = L[8 · (s mod 16)] << ⌊s/16⌋.
- **TONAL modes:**
  - d = (v >> 4) + 192, or 1024 + 192 when v > `$3ffe`.
  - The octave is o = ⌊d/192⌋, read from `$2b0ad0`: 30 words of (192 · o) << 4 | o, one per 64 values of d.
  - P = W[d − 192 · o] << (o + a), with a = −1, 0, +1 for TONAL-1, TONAL, TONAL+1. A shift of −1 is an arithmetic
    >> 1, which a User Machine never reaches.
  - At a whole setting, P(s) = W[8s mod 192] << (⌊s/24⌋ + 1 + a).
  - The 192 entries are 1/16 semitone apart and a setting moves d by 8, so 2 settings make a semitone and 24 an
    octave.
- **Between settings.** v keeps fractions of a setting. DEFAULT's index steps every 1/8 of a setting. The TONAL index
  steps every 1/8 of a setting too (1/16 semitone), with no quantizing to semitones.

### 3.3 The tuning modes and MDM1's TONAL flag

- **The Track's mode [V].** `$213298(engine, Track, Machine, mode)`, used on assignment and Kit load, sets the
  Track's tonal bit (engine +`$2f6`) when mode ≠ 0 and stores the mode at context +`$8c`. A Kit load takes the mode
  from two per-Track bits of the Kit (`$25dac0`). The tick then passes 0 when the bit is clear, keeps 2 or 3, and
  passes 1 otherwise.
- **Where the flag counts [V].** MDM1's TONAL flag (descriptor +`$19` bit 0) is read by `$21c000`, "can this Machine
  be tonal". It is called from EDIT KIT's TUNING editor (`$275ac8`), which then offers modes 0 to 3, from another
  editor routine (`$278fdc`), and from the note display's `$21c064`. The conversion never reads the flag. Every
  Pitch parameter of a Track converts with the Track's one mode.
- **The note display [V].** `$21c064` gives the note of setting 0 for a Pitch parameter of a tonal USR Machine: 37 in
  TONAL, 25 in TONAL-1, 49 in TONAL+1. `$277d86` shows note = that + s/2, the octave as note/12 − 2 (Elektron's
  naming), and "~" after an odd setting. So setting 64 is MIDI note 69, the A of the tuning.
- **Messages [I].** The readme's `$5B` flags map to these modes: TONAL `$02` → 1, TONAL-1 `$0A` → 2, TONAL+1
  `$12` → 3. Codes 2 and 3 are one octave down and up in `$218184` [V]. The `$5B` decoder itself was not read.

### 3.4 What "A=440" means

| | TONAL, setting 64 | Hz at 44,100 (the Standard's formula) | Hz at 43,402.8 |
|---|---|---|---|
| TUNING 0, "A=440 Hz" | 21,248 | 446.81 | 439.75 |
| Legacy, "MDX (A=433HZ)" | 20,920 | 439.92 | 432.96 |

- **Measured [M].**
  - TUNING 0's TONAL grid lies 26.54 cents above the legacy grid.
  - By the Standard's formula, f = P · 44,100 / 2^21, the legacy grid is A=440: setting 0 gives 69.27 Hz against
    C#2's 69.30, 0.7 cents off through rounding.
- **Inferred [I].** X.20's labels hold if the codec runs at about 43.4 kHz:
  - 43,402.8 Hz is 100 MHz / 2,304. mdLib counts 2,304 DSP cycles per codec frame but assumes a 101.6064 MHz clock,
    which makes 44.1 kHz (`mdLib/mdhardware.cpp:40-44`).
  - The Standard says 44,100 Hz for Pitch and for RENDER's output (`:221-222`, `:415-416`).
  - No hardware was at hand to settle it.
- **For MoDule [I].** MoDule renders at 44.1 kHz.
  - With X.20's TUNING-0 tables, a TONAL User Machine's A plays 446.8 Hz. If the Machinedrum runs near 43.4 kHz,
    that is the same 27 cents by which MoDule then plays 1.63's built-in Machines sharp of the hardware, so User and
    built-in Machines keep X.20's relation.
  - With the legacy W, A plays 440 Hz by the Standard. A choice for the map.
- **DEFAULT.** DEFAULT's L is the same in legacy and at TUNING 0. Setting 64 gives P = 32,768, 689.06 Hz at 44,100.

### 3.5 The 128 values at TUNING 0 [M]

Pitch words P from the recurrences of 3.1 and 3.2 (`tables.py`). TONAL-1 is half of TONAL and TONAL+1 is double;
DEFAULT is the same under legacy tuning. By the Standard's formula, DEFAULT runs from 43.07 Hz · 2^(s/16) (4.3 cents
at most from it) and TONAL from 70.36 Hz · 2^(s/24) (0.55 cents at most from it).

DEFAULT (mode 0):

```
  0-  7    2048   2139   2234   2333   2437   2545   2659   2777
  8- 15    2900   3029   3164   3304   3451   3605   3765   3932
 16- 23    4096   4278   4468   4666   4874   5090   5318   5554
 24- 31    5800   6058   6328   6608   6902   7210   7530   7864
 32- 39    8192   8556   8936   9332   9748  10180  10636  11108
 40- 47   11600  12116  12656  13216  13804  14420  15060  15728
 48- 55   16384  17112  17872  18664  19496  20360  21272  22216
 56- 63   23200  24232  25312  26432  27608  28840  30120  31456
 64- 71   32768  34224  35744  37328  38992  40720  42544  44432
 72- 79   46400  48464  50624  52864  55216  57680  60240  62912
 80- 87   65536  68448  71488  74656  77984  81440  85088  88864
 88- 95   92800  96928 101248 105728 110432 115360 120480 125824
 96-103  131072 136896 142976 149312 155968 162880 170176 177728
104-111  185600 193856 202496 211456 220864 230720 240960 251648
112-119  262144 273792 285952 298624 311936 325760 340352 355456
120-127  371200 387712 404992 422912 441728 461440 481920 503296
```

TONAL (mode 1):

```
  0-  7    3346   3444   3546   3650   3756   3866   3980   4096
  8- 15    4216   4340   4466   4598   4732   4872   5014   5160
 16- 23    5312   5468   5628   5792   5962   6138   6316   6502
 24- 31    6692   6888   7092   7300   7512   7732   7960   8192
 32- 39    8432   8680   8932   9196   9464   9744  10028  10320
 40- 47   10624  10936  11256  11584  11924  12276  12632  13004
 48- 55   13384  13776  14184  14600  15024  15464  15920  16384
 56- 63   16864  17360  17864  18392  18928  19488  20056  20640
 64- 71   21248  21872  22512  23168  23848  24552  25264  26008
 72- 79   26768  27552  28368  29200  30048  30928  31840  32768
 80- 87   33728  34720  35728  36784  37856  38976  40112  41280
 88- 95   42496  43744  45024  46336  47696  49104  50528  52016
 96-103   53536  55104  56736  58400  60096  61856  63680  65536
104-111   67456  69440  71456  73568  75712  77952  80224  82560
112-119   84992  87488  90048  92672  95392  98208 101056 104032
120-127  107072 110208 113472 116800 120192 123712 127360 131072
```

TONAL under legacy tuning, for comparison:

```
  0-  7    3294   3390   3490   3592   3698   3806   3918   4032
  8- 15    4150   4272   4398   4526   4660   4796   4936   5082
 16- 23    5230   5384   5542   5704   5870   6042   6220   6402
 24- 31    6588   6780   6980   7184   7396   7612   7836   8064
 32- 39    8300   8544   8796   9052   9320   9592   9872  10164
 40- 47   10460  10768  11084  11408  11740  12084  12440  12804
 48- 55   13176  13560  13960  14368  14792  15224  15672  16128
 56- 63   16600  17088  17592  18104  18640  19184  19744  20328
 64- 71   20920  21536  22168  22816  23480  24168  24880  25608
 72- 79   26352  27120  27920  28736  29584  30448  31344  32256
 80- 87   33200  34176  35184  36208  37280  38368  39488  40656
 88- 95   41840  43072  44336  45632  46960  48336  49760  51216
 96-103   52704  54240  55840  57472  59168  60896  62688  64512
104-111   66400  68352  70368  72416  74560  76736  78976  81312
112-119   83680  86144  88672  91264  93920  96672  99520 102432
120-127  105408 108480 111680 114944 118336 121792 125376 129024
```

Our guess reads setting 69 as A=440 (P = 20,924). X.20 gives 40,720 there in DEFAULT, and TONAL reaches A at
setting 64 (21,248).

## 4. Decay

- **The lookup [V].** `$2189c6(context, i)` with i = v >> 5, at most `$1ff`. It follows context +`$90` (engine + 4),
  then +`$24`: the pointer at engine +`$28`.
- **The pointer [V].** At start-up `$213a6e` fills engine +`$10` to +`$5c` with 20 pointers. Each comes from the
  1.63-data fetcher `$206f22` → `$247ee8` → `$247d90`, driven by a list at `$2b2fb2` of (1.63 address − `$200000`,
  length) pairs. The decay pointer is entry 6: 1.63 `$24bf94`, 2,048 bytes = 512 longs. The neighbours fit the same
  reading: `$21afe8` indexes entry 8 (1.63 `$24c794`, 1,024 longs) with a bound of `$3ff`.
- **Byte-identical [M].** X.20's SPSC block stores that range raw (entry 7: 1.63 `$24bf94`, `$1a00` bytes), equal to
  the user's 1.63 image. This reruns research/01's check (`x20scripts/s13_spsc.py`).
- **1.63's own [M].** 1.63's section 0 refers to `$24bf94` 59 times, from `$201164` on: the decay table of 1.63's own
  coefficient routines.
- **The law [M].** At setting s the word is T[4s]; between settings, T[v >> 5], 4 entries per setting.
  - Fit (`decayfit.py`): T[i] ≈ 2^23 · exp(−1/τ), with τ = 2^(1.7541 + 0.033880 · i) samples. τ doubles every 7.38
    settings, and the fit stays within 66 LSB.
  - The top end is quantized to Q23. T[508] = `$7ffff0` (1 − c = 2^−19). From index 474 up, some neighbours are
    equal.

| Setting | Word | Coefficient | −60 dB at 44.1 kHz |
|---|---|---|---|
| 0 | `$5f29c2` | 0.74346 | 0.53 ms |
| 16 | `$77d4a9` | 0.93618 | 2.4 ms |
| 32 | `$7e22ba` | 0.98543 | 10.7 ms |
| 48 | `$7f9536` | 0.99674 | 48 ms |
| 64 | `$7fe836` | 0.99927 | 0.216 s |
| 80 | `$7ffab5` | 0.99984 | 0.97 s |
| 96 | `$7ffed2` | 0.99996 | 4.35 s |
| 112 | `$7fffbd` | 0.999992 | 19.6 s |
| 127 | `$7ffff0` | 0.999998 | 82 s |

- **The other 119 values.** They are OS 1.63's data, so they stay out of the repository. A converter reads them from
  the user's flash image, as mdEngine already reads 1.63's tables at fixed 1.63 addresses (`MachineRunner.cpp`).
  Nothing of X.20 is needed for Decay. [I]
- **Against our guess.** Ours runs from 5 ms to 10 s; X.20's from 0.53 ms to 82 s.

## 5. What this changes in `md::mds::convert`

- **Pitch** needs two inputs that the setting alone does not carry: the Track's tuning mode (0 unless the Kit makes the
  Track tonal) and the Global TUNING. Both are formulas of section 3 with integer recurrences, so the tables can be
  recomputed rather than read from X.20.
- **Decay** is `T[v >> 5]`, read from 1.63 at `$24bf94`.
- **Unipolar:** `$7fffff` from v ≥ `$3f80`, and 0 for v ≤ `$7f`.
- **Boolean and Index between settings:** v ≠ 0, and v >> 7 truncated.
- **The conversion byte.** X.20 converts by its low nibble alone. Our parser refuses a byte above `$54` or a nibble
  above 7 (`MdsPackage.cpp:128`), which is stricter. Whether X.20's installer refuses the same bytes was not read.

## Open

- **The rate.** Does a Machinedrum's codec run near 43.4 kHz, as X.20's tuning tables imply, or at the Standard's
  44,100 Hz? Only hardware answers it: X.20 booted in mdLib would inherit mdLib's 44.1 kHz.
- **Which tables MoDule uses.** X.20's TUNING-0 tables, or the legacy W that gives A=440 Hz by the Standard (section
  3.4).
- **Tonal mode without the flag.** Does a `$5B` or a Kit that sets a tonal mode on a USR Machine without MDM1's TONAL
  flag get refused? The setter `$213298` does not check; the `$5B` decoder and the Kit reader were not read.
- **PTC DISP NOTE.** Whether its edits step by two settings (a semitone) was not read; the display reads s/2 with "~"
  for odd settings.
- **TUNING's factory value.** Assumed 0 (A=440); the Global defaults were not read.
- **The DSP side.** How often X.20 sends converted words, and whether the voice program smooths them between ticks.
  The readme's INTERPOLATE names Track volume, pan and master effects only.
- **The table at `$2ce000`.** What it serves (built-in Machines; not User Machines).
- **Later builds.** Every address here is X.20 B's.
