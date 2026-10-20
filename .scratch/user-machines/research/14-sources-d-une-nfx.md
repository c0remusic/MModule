# What the voice DSP needs for an NFX to hear any Track

Research for ticket 29 of the map "User Machines from X.20 in MoDule", "Ce que la DSP des voix doit faire pour qu'une
NFX entende n'importe quelle Track". Written 2026-10-20 against `main` at bcc8fde, working tree clean. Nothing in the
repository was built, changed or run apart from this note. No byte or code of X.20, the zip or the flash image was
copied into it: addresses, counts and behaviour only.

Sources:

- **MoDule's code**, at the places named: `mdEngine/engine/VoiceEngine.*`, `MdEngine.h`, `mdDrums/mdDrumsEngine.cpp`,
  `mdDrums/mdDrumsHitPreview.*`, `mdDrums/mdDrumsTestMachine.h`, and this tree's `dsp56300`.
- **Research/04 and research/12** for the voice DSP's memory map and the ring that ticket 22 built.
- **The 13 NEIGHBOUR packages** of the X.20 B zip, in the session scratchpad (`x20machines/USR-Machines`).
  - Their RENDER offsets were read by `sources29/scripts/offsets.py` (in memory, research/11's `flags.py`).
  - Their RENDER code was followed through research/12's listings (`neighbour/out/*.asm`) by
    `sources29/scripts/reach.py`, which walks the control flow from the RENDER offset and lists every reachable word
    that touches R5, R7, N7, M7 or Y:`$7F0`.
- **X.20 B's readme** (`x20machines/Machinedrum_SPS1-UW_OS-X.20_B_readme.txt`) and **the Standard** (jmamma/MDS
  `MDX_MachineDumpStandard.txt`, the session's copy `mdsdocs/standard.txt`, whose line numbers are given).
- **A probe outside the repository**, `sources29/probe/sources29.cpp` in the session scratchpad.
  - It is linked against `temp\cmake_vs22`'s Release libraries as they stood (`VoiceEngine.obj` newer than
    `VoiceEngine.cpp`; not rebuilt).
  - It ran on the user's 1.63 flash image, under a wall-clock guard (`run.ps1`).
  - It builds `md::engine::Engine`s and patches the voice DSP after `reset()`, through `VoiceEngine::dsp()`. The patch
    is a per-Track layout: a new stub at P:`$C40`, sixteen buffers and sixteen descriptors.
  - It compares that layout with today's ring, voice block by voice block and cycle by cycle.
  - Its reports are `equiv.txt`, `users.txt`, `chosen.txt` and `cost.txt` beside it.

Marks: **[V]** read at the place named; **[M]** measured by the probe, or by the earlier run named; **[I]** inferred
from the facts around it. "Track t" counts from 1 in prose; code indices count from 0.

## In short

- **Memory [V]/[M].** Internal Y has no room for sixteen buffers.
  - The DSP's internal Y runs to `$BFF`. 1.63 and the harness leave 124 words free there, Y:`$783-$7FE`, and today's
    ring already takes 81 of them (research/04 §2, research/12).
  - The sixteen buffers go in the emulator's Y below the bridge: Y:`$1000-$11FF`. 1.63, the harness and every machine
    leave that range untouched (research/04 [M]). It does not exist on a Machinedrum, which does not matter to MoDule.
  - The sixteen descriptors (64 words) and a "four back" word per Track (16) fill exactly the 80 words Y:`$7A0-$7EF`,
    which the ring's two extra buffers and four descriptors free. Y:`$7F0` stays the pointer, since the Standard fixes
    it.
  - Bridged external memory (`$147600`, which exists on the hardware) behaves the same in the emulator:
    - the same cycles, since the emulator charges no wait states;
    - the same host time, since the JIT picks the array with one compare for any address;
    - the same voices [M].
  - The send stub reads any of them over HI08 [M]; the DMA would too [V].
- **The 13 packages [V]/[M].**
  - None reads descriptor word 0.
  - Seven read word 1 alone; NFX-BC, -CH, -FC and SYN-F1 read words 1 and 2; NFX-MX and -EN read words 1, 2 and 3, and
    their own buffer through R7 before writing it.
  - A descriptor whose words point at chosen Tracks' buffers works with no change to their code. All 13 on Track 12,
    hearing Tracks 3, 5, 7 and 9, gave their own voice bit for bit as when hearing copies of those Tracks on 11, 10, 9
    and 8, in 120 of 120 blocks [M].
  - For "four back", the stub copies the chosen source's block into the Track's own buffer before RENDER: 77 cycles,
    only for User Machine types [M].
- **A source rendered after the NFX [M].** The NFX hears that source's previous block, 32 samples late.
  - The probe's copier gave the source's previous block in 76 of 76 blocks, whichever word read it, the four-back copy
    included.
  - Ticket 20's rule, "the delay of the Track before", becomes "the delay of its first source". For a source after the
    NFX, that leaves the effect exactly one block (32 samples) behind the dry sound, a constant lag [I].
  - Ticket 21's preview strikes the chosen sources instead of the four before.
- **Cost [M].** On the default Kit, 38,819 cycles a block against 38,643 today: 11 more a voice for the stub.
  - Add 77 per User Machine voice for the copy, so 16 NFX at most cost 1,408 more, 2.0 % of 71,680.
  - Host time is unchanged within noise on built-in Kits (76.9 against 76.5 µs a block), and 2 to 3 µs more with 16
    User Machines.
- **X.20 [V]/[I].** Neither its readme nor research/01 or 11 says anything of choosing an NFX's sources.
  - The Standard defines the descriptor as the four Tracks before (lines 424-459).
  - X.20's ring shares one descriptor among Tracks 1, 5, 9 and 13 (research/12), so its layout cannot carry a
    per-Track choice.
  - This is MoDule's extension: a package cannot tell the difference.
- **A set that chooses nothing [M].** Default descriptors (one, two, three and four back) give today's voices bit for
  bit. The probe found 0 differing voice blocks in each case below:
  - every one of 112 audio machines on all 16 Tracks with random SYN1-8 (4,480 blocks);
  - 20 mixed Kits (19,200 voice blocks);
  - the test copier one to four back on Tracks 1, 2, 3, 6 and 16;
  - the user's 13 on Tracks 1, 2, 6 and 16 (7,680 voice blocks each).

  The delay rule and the preview reduce to today's with the default sources.
- **Recommendation for ticket 31.** Replace the ring with the per-Track layout in `installHarness`, in a single path:
  - one stateless stub;
  - buffers at Y:`$1000` + 32·t, descriptors at Y:`$7A0` + 4·t, four-back words at Y:`$7E0` + t;
  - the copy for User Machine types;
  - `VoiceEngine::setSources` written between blocks and kept across `reset()`;
  - the delay rule and the preview's struck Tracks following the first source and the four sources.

  §7 gives the design, the C++ changes and the risks.

## 1. Where sixteen buffers and sixteen descriptors can live

### 1.1 What the program and the harness use [V]/[M]

The DSP56303 runs with OMR `$00498D` (`VoiceEngine.cpp:38`). Internal P ends at `$3FF`; internal X and Y run to
`$BFF`. Research/04 §2 gives the map from machinedrum-kit's, with its own poisoning run [M]. In Y:

| Y | Held by | Free |
|---|---|---|
| `$000-$0FF` | The renderers' per-call scratch; import 8 at `$000-$01F` | no |
| `$100-$13F` | 1.63's two output banks, now two of the ring's four buffers (`VoiceEngine.cpp:58`) | no, while the ring runs |
| `$140-$162` | The dispatcher: Y:`$140` the voice's buffer, S, the Track, each Track's active type at `$153` + t | no |
| `$163-$782` | The sample player's interpolation table | no |
| `$783-$79F` | — | 29 words |
| `$7A0-$7EF` | The ring: buffers `$7A0` and `$7C0`, descriptors `$7E0-$7EF` (`writeRing`, `VoiceEngine.cpp:270-281`) | no, while the ring runs |
| `$7F0` | The descriptor pointer, fixed by the Standard (line 431) | no |
| `$7F1-$7FE` | — | 14 words |
| `$7FF` | A word P:`$70` reads | no |
| `$800-$BFF` | The 16 slots of 64 words, each Track's Y memory and parameters | no |

- **The harness's own use [V].**
  - P: the stubs from P:`$C00` (send, skip and silent skip below `$C40`, the ring from `$C40`), the jumps patched into
    the block loop, and the extended dispatch tables at `$145D38-$145FB0`.
  - User Machine code at `$020000-$0FFFFF`.
  - Y: the ring and the slots, which `setSlot` writes.
  - X: nothing.
- **Internal X.** X:`$257-$6FF` (1,193 words) is free, but useless here. The Standard puts every output and every
  neighbour input in Y (lines 415-417, 430-453). Every package reads them with `y:` moves (§2) [V].
- **So internal Y cannot take 16 × 32 words** [I].
  - Without the ring, 124 words would be free at Y:`$783-$7FE`, plus 64 if the banks at `$100-$13F` were taken back.
  - The buffers alone need 512.

### 1.2 Outside internal memory [V]/[M]

- **The emulator's Y below the bridge.** `kSizeXY = $200000` with the bridge at `$020000` (`VoiceEngine.cpp:33`)
  gives Y its own array up to `$1FFFF`.
  - Y:`$C01-$1FFFF` (127,999 words) is never written and never read, by the program, the harness or any of 104
    machines on any Track (research/04 §2, [M] there).
  - A Machinedrum has no such memory (no AAR covers `$000C00-$0FFFFF`, `VoiceEngine.cpp:28-37`). MoDule does not run
    on one.
- **External memory, which the hardware has.** `$147600-$147DFF` (2,048 words) is free in 1.63 (research/04). X.20
  puts a table there, which does not concern MoDule. `$13B601-$13FFFF` is free too. Above the bridge, X, Y and P are
  one array.
- **Wait states.**
  - The emulator charges none: `calcCycles` takes `_extMemAddress` and `_extMemWaitStates` and ignores both
    (`opcodecycles.cpp:27-70`). BCR reads as 0 (`dsp.cpp:139`, `:805`) [V].
  - On a Machinedrum, each external access would pay the bus's wait states. X.20 keeps its ring internal [I that this
    is why].
  - For MoDule the question is host time, not emulated cycles.
- **Host time.** For an address held in a register, the JIT computes the X or Y pointer and swaps in P's when the
  address is at or above the bridge. That is one `cmp` and one `cmov`, whatever the address (`jitmem.cpp:1000-1004`)
  [V].
  - Writes into bridged memory go straight to the host array, with no JIT invalidation (`jitmem.cpp:458-520`) [V].
  - Measured, both places cost the same: identical cycles, and host time within noise of each other and of today
    (§4) [M].
- **What reads a Track's own buffer.**
  - The send stub reads Y:(Y:`$140`) with `movep y:(r0)+,x:HTX` (`VoiceEngine.cpp:173-182`). The probe's voices, read
    over HI08 from Y:`$1000` and from `$147600`, equal today's in every block (§6) [M].
  - 1.63's link DMA, which the harness skips, reads through `Memory::get`, which translates bridged addresses
    (`dma.cpp:520-538`) [V]. So either place would also serve a DMA.

### 1.3 The layout taken [M]

| Y | Words | What |
|---|---|---|
| `$1000` + 32·t | 512 | Track t's buffer, 32-aligned as M7 = `$1F` needs. RENDER writes it; the send stub sends it |
| `$7A0` + 4·t | 64 | Track t's descriptor: word 0 its own buffer, words 1-3 the buffers of its first three sources |
| `$7E0` + t | 16 | The buffer of its fourth source, which a User Machine's own buffer takes before RENDER |
| `$7F0` | 1 | The pointer, `$7A0` + 4·t during Track t's RENDER |

The descriptors and the four-back words fill exactly the 80 words the ring frees. Y:`$100-$13F` and Y:`$783-$79F` are
left free.

## 2. What the 13 packages read, and chosen descriptors

### 2.1 Word by word [V]

Every reachable word of each RENDER, followed from its offset (`reach.py`). Offsets are the package's own, unrelocated
(research/12's listings).

| Package | RENDER | Y:`$7F0` | +0 | +1 | +2 | +3 | Own buffer before writing (four back) |
|---|---|---|---|---|---|---|---|
| NFX-BC | `$17` | `$89` | — | R5, `$90`; read `$95` | `$8D`, as N7 = word 2 − R7, M7 linear (`$8B`); read `y:(r7+n7)` `$96` | — | no: writes `$AD` |
| NFX-CH | `$59` | `$E7` | — | `$EE`; read `$FC` | `$EB`, the same offset form; read `$FE` | — | no: writes `$145` |
| NFX-D1 | `$91` | `$6E` | — | `$75`; read `$EC`, `$13F` | — | — | no: writes `$141`; its first block writes zeros (`$9D-$A0`) |
| NFX-D2 | `$91` | `$6E` | — | `$75`; read `$115`, `$16A` | — | — | no: writes `$16C` |
| NFX-D3 | `$91` | `$6E` | — | `$75`; read `$EC`, `$146` | — | — | no: writes `$148` |
| NFX-D4 | `$91` | `$6E` | — | `$75`; read `$E5`, `$11E` | — | — | no: writes `$120` |
| NFX-EN | `$36` | `$98` | — | `$A2`; read `$AE`, `$B4` | `$9E` (R1) | `$A0` (R3) | yes: its first loop reads all 32 through R7 (`$B3`) before the second writes them (`$E0`, `$100`) |
| NFX-FC | `$23` | `$A9` | — | `$B0`; read `$B5` | `$AD`, the offset form (M7 linear `$AB`); read `$B6` | — | no: writes `$CD` |
| NFX-MX | `$2` | `$F` | — | `$18`; read `$1B`, `$21` | `$14` (R1) | `$16` (R3) | yes: each sample is read at `$20` before `$22` writes it |
| SYN-F1 | `$20` | `$DB` | — | `$E9`; read `$ED` | `$E7` (R6, kept in N7 and restored at `$137`) | — | no: writes `$136` |
| SYN-F3 | `$33` | `$1B7` | — | `$1BB`; read `$1C1` | — | — | no: writes `$20F` |
| SYN-F8 | `$A8` | `$EE` | — | `$F2`; read `$F6` | — | — | no: writes `$129` |
| SYN-FJ | `$1F` | `$D5` | — | `$D9`; read `$E8`, `$13E` | — | — | no: writes `$13A`, `$184` |

- **No package reads word 0.**
  - Each loads Y:`$7F0` once per RENDER by its long absolute address, then words 1 to 3 through R5.
  - None names Y:`$140`, `$100`, `$120` or any buffer by address. The only absolute data address in any RENDER is
    Y:`$7F0`; the others are its own relocated tables.
  - This agrees with research/12 §1.4, now from the whole reachable code.
- **The offset form.** NFX-BC, -CH and -FC set M7 linear, then read two back as `y:(r7+n7)` with N7 = word 2 − R7, in
  24-bit arithmetic. That reaches word 2's buffer wherever it lies, as long as R7 enters RENDER at the Track's own
  buffer, which the Standard promises (line 338). So it needs nothing more [V].
- **The parameters.** NFX-MX and -EN name T-1 to T-4, NFX-CH and SYN-F1 T-1 and T-2 (`offsets.py`): levels of words 1
  to 4, not choices of Track.

### 2.2 Chosen Tracks behind the words [M]

The probe's copier is `mdDrumsTestMachine.h`'s `neighbourDraft`. With four back it reads word 0, its own buffer as
RENDER finds it.

Run on a per-Track engine whose descriptor names a chosen Track (`chosen.txt`), with the source struck every 7 blocks,
76 blocks from the fourth:

| Copier | Source | Equal to the source's | Blocks |
|---|---|---|---|
| Track 12, one back | Track 3 | same block | 76 of 76 |
| Track 12, two back | Track 7 | same block | 76 of 76 |
| Track 12, three back | Track 14 | block before | 76 of 76 |
| Track 12, four back | Track 3 | same block | 76 of 76 |
| Track 12, four back | Track 14 | block before | 76 of 76 |
| Track 3, one back | Track 12 | block before | 76 of 76 |
| Track 1, one back | Track 9 | block before | 76 of 76 |
| Track 16, one back | Track 1 | same block | 76 of 76 |
| Track 6, one back | Track 16 | block before | 76 of 76 |
| Track 12, one back | Track 13 | block before | 76 of 76 |

The same ten runs gave the same results with the buffers at `$147600`.

**The user's 13 (`chosen.txt`, "users with chosen sources").** Each ran on Track 12, all its parameters at 127, in two
engines:

- one hearing Tracks 3, 5, 7 and 9 through its descriptor;
- the other hearing, by default, Tracks 11, 10, 9 and 8, which hold the same four machines (TRX-BD, TRX-SD, EFM-CB,
  TRX-CP) struck alike.

The four sources were equal between the engines in 120 of 120 blocks. Each package's own voice was equal too, in 120 of
120 blocks, for all 13, sounding in 77 (NFX-D3) to 119 blocks. So a descriptor that points at chosen Tracks works with
no change to their code, including NFX-MX's and NFX-EN's four back.

### 2.3 Four back: NFX-MX and NFX-EN [V]/[M]

- **What they rely on.** Both read the Track's own buffer through R7 before writing it. The Standard calls that buffer
  "four tracks back ... until RENDER replaces it" (lines 441-442, 453).
- **Today.** Today's ring makes it true by sharing a buffer between Tracks t and t−4.
- **With one buffer per Track,** the own buffer would hold the Track's own last block. MX and EN would then feed their
  output back to their input, 32 samples later.
- **What it becomes: a copy.** Before RENDER, the stub copies the fourth source's 32 words into the Track's own buffer.
  - The source is named by Y:`$7E0` + t: by default Track t−4, wrapping into the block before as today.
  - MX and EN then read the chosen fourth source and overwrite it with their output, as the Standard has it.
- **Measured.** 77 cycles a voice (§4). MX and EN with a chosen fourth source equal the reference in 120 of 120
  blocks; MX and EN with the default equal today in all blocks (§6).
- **Who gets the copy.** The stub copies for every User Machine type (195 and up), whatever the package. So every User
  Machine finds in its own buffer what today's ring gives it, even one that broke the Standard by reading it.
  Built-ins never pay.
- **The rejected alternative [I].** Pointing R7 at the fourth source's buffer, as X.20's sharing does, would write the
  effect over that source. A later reader of the source would then hear the effect, and the Track's output would move
  from buffer to buffer.

## 3. A source rendered after the NFX

### 3.1 What the NFX gets [M]/[V]

- **The voice DSP renders Tracks 1 to 16 in order, every block** (`$DE-$E1`, research/12 §1.1). A source with a
  higher number than the NFX has not rendered yet when the NFX reads it.
- **Its buffer holds its previous block.** Between blocks the DSP waits for the go word with its memory intact
  (`VoiceEngine.cpp:261-267`, `:506-512`; research/12 §3.1). `reset()`, the warm-ups and `installUserMachine` restore Y.
- **Measured:** the source's previous block in 76 of 76 blocks, from words 1 to 3 and from the four-back copy alike
  (§2.2).
- **Today does the same:** Tracks 1-4 read Tracks 13-16 of the previous block (Standard lines 455-459).
- **A source never struck, on GND-- or MID/CTR** gives zeros: the skip path and the null render write them into its own
  buffer every block (`VoiceEngine.cpp:214-228`). Its block is the same before and after, so order does not matter
  [V]/[M].
- **The NFX itself as a source.** A word naming its own buffer would read the four-back copy, not its last output.
  - For words 1-3 this is wrong.
  - For word 4 (the copy word) it would give the Track's own last output: a 32-sample feedback.
  - The choice should leave the NFX's own Track out [I].

### 3.2 The delay a NEIGHBOUR takes (ticket 20) [V]/[I]

- **Today.** `mdDrumsEngine.cpp:116-127` gives a NEIGHBOUR Track `delay[(t + 15) % 16]`, the delay of the Track
  before, at every block.
- **That delay.** A delay is the rest of the block after the note (0-31) plus the hold back to `AlignedStart`, by
  machine (`trigger`, `voiceStart`, `:497-537`):
  - 32 for most machines and every User Machine (`CommonVoiceStart` 4);
  - 24 for ROM;
  - 0 for TRX-BD, -S2, -XT, -CB and -XC.
- **The rule generalised.** A NEIGHBOUR takes the delay of its first source (word 1). The default first source is the
  Track before, so the rule is today's for a set that chooses nothing.
  - For a source before the NFX, the effect lands where the source's dry sound does, as ticket 22 measured for the
    Track before (correlation at lag 0, `mdDrumsEngineTest`).
  - For a source after the NFX, the effect of block k comes from the source's block k−1. With the same delay it
    lands exactly 32 samples after the dry sound: constant, with no jitter. This is what Track 1 does today with
    Track 16.
- **A refinement, for the user to choose.** For a source after the NFX, take that source's delay minus 32 when that
  is not negative.
  - For a source held back 32 (most machines and every User Machine), the delay is always at least 32. The effect then
    lands on the dry sound exactly, with no block of lag at all.
  - For a TRX-BD-like source (hold 0) it never is, and the lag stays 32 less its delay, 1 to 32 samples.
  - For ROM (hold 24) the lag is 0 from a wait of 8 on, 1 to 8 below it.
  - Applied to Tracks 1-4's defaults too, it would change today's sound for those Tracks.
  - Ticket 31 can offer it only for chosen sources, or not at all. The ticket's prototype line ("un bloc plus tard")
    assumes the constant lag.
- **When the source is struck.** `delay[s]` changes in the block where the Hit lands. The NFX reading s's previous
  block then places that block's tail with the new delay: a gap or overlap of up to the difference, once.
  - Today Track 1 has the same glitch when Track 16 is struck.
  - Keeping last block's delays for sources after the NFX would put it on the Hit's own block instead [I]. It is not
    measured.
- **Chains.** The loop runs in Track order, so a NEIGHBOUR whose source is a NEIGHBOUR before it takes that one's
  delay already followed. One after it gives last block's, which is the block the NFX hears [I].
- **Several sources.** With two to four, only the first's delay is followed. Ticket 20 decided that for the Tracks
  before; it is the same here [V].

### 3.3 The preview (ticket 21) [V]/[I]

- **Today.** `HitPreview::render` strikes the shown Track and, for a NEIGHBOUR, the four before it
  (`mdDrumsHitPreview.cpp:23-39`). It installs their User Machines and hears only the shown Track.
- **With a choice.** Struck: the shown Track and its four sources, without repeats. `Request` (`mdDrumsHitPreview.h:48-56`)
  must then carry the sources unless the Kit does, and `render` must write them into the fresh engine's descriptors
  before the Hit.
- **A source after the NFX.** Struck in the same block, it reaches the NFX one block later, its whole sound shifted 32
  samples. Its Hit block is not lost: the NFX reads it at the second block. The preview, rendered by
  `mdDrums::Engine`, shows that lag as the Device plays it.
- **The cost** is today's: at most four more voices on the preview's own thread.

## 4. Cost

The probe ran each scene on today's ring and on both per-Track layouts in turn. It interleaved 20 rounds of 400 blocks,
after `warmUpVoices`, with the first 100 blocks left out. Cycles are `DSP::getCycles`; host time is `HostModel::dspUs`
(`cost.txt`) [M]:

| Scene | Today's ring | Per-Track, Y:`$1000` | Per-Track, `$147600` |
|---|---|---|---|
| 16 idle Tracks | 2,306 cycles, 4.65 µs | 2,482 (+176), 4.53 µs | 2,482, 4.51 µs |
| Default Kit, a Hit every 21 blocks | 38,643 (max 39,448), 76.50 µs | 38,819 (+176, +0.46 %), 76.90 µs | 38,819, 76.46 µs |
| Default Kit, the copier on Tracks 4, 8, 12, 16 | 31,005, 65.74 µs | 31,485 (+480), 65.83 µs | 31,485, 65.73 µs |
| 16 copiers | 4,786, 25.19 µs | 6,194 (+1,408), 27.05 µs | 6,194, 28.17 µs |

- **The stub: +11 cycles a voice, 176 a block, for every Kit** [M].
  - Its extra work is the four-back word's read, the type test and its branch.
  - Keeping the four-back read inside the copy path, and comparing with an immediate, would save about 3 [I].
  - The equivalence runs agree: +176 a block on all 112 machines and on the mixed Kits (`equiv.txt`).
- **The copy: 77 cycles per User Machine voice** [M]: (1,408 − 176) / 16. It is also (480 − 176) / 4, and 252 − 176
  for one User Machine (`users.txt`).
- **Worst case.** 16 NEIGHBOUR Tracks add 1,408 cycles, 2.0 % of a block's 71,680. Their own RENDERs (204 to 3,189
  each, research/12) dwarf it.
- **Host time.** Within noise on the built-in Kits; 1.9 to 3 µs a block more with 16 User Machines, 0.4 % of a block's
  725.6 µs real time [M].
  - The voice DSP's idle block costs 2,306 cycles today, against research/04's 1,474: the difference is ticket 22's
    zeros on the skip path, 52 a voice [M].
- **The two memory places** cost the same cycles and the same host time within noise [M]. Nothing argues for external
  memory.
- **The host.** The host writes 80 Y words at `reset()`, and five when a Track's sources change. In `renderBlock` the
  delay rule reads one more array element per NEIGHBOUR Track [I].
- **Hit latency.** For a set that chooses nothing, the voices are the same samples (§6) and the delays the same, so
  latency and jitter are unchanged (83 samples, 85 for a NEIGHBOUR on the Track before, ticket 22) [I from M]. A source
  after the NFX adds 32 samples between dry and effect, not between note and sound.
  - CLAUDE.md asks for latency before and after: ticket 31 measures it on the Device.
- **Not measured: the whole engine.** `mdDrums::Engine` hides its `md::engine::Engine` (`State` lives in the .cpp), so
  the probe could not patch it. `mdDrumsEngineTest`'s CPU line, before and after, belongs to ticket 31.

## 5. Does X.20 let the user choose? [V]/[I]

- **The readme.** It mentions neighbours only as the trig condition NEI/!NE, "the neighbouring track's condition
  passed" (line 102), and nothing of what an NFX hears.
  - NFX-EV, -CO and -UC, X.20's own effects, appear only as machine numbers (lines 432-434).
  - Their parameters (research/01) include SIDE and NBAL, a sidechain and a neighbour balance, but no Track.
- **Research/01 and 11.**
  - X.20's 68k code keeps a NEIGHBOUR mask of Tracks (engine +`$4376`, built by `$217A06`), which research/11 did not
    follow.
  - Its voice program steps one pointer through four descriptors (research/12 §1.3). Tracks 1, 5, 9 and 13 therefore
    share the descriptor at Y:`$7E0`.
  - In that layout no Track can hear something its three companions do not. A per-Track choice would need X.20's own
    stub to change [I].
- **The Standard** defines the inputs as "the four preceding tracks", in Track order (lines 424-459). It says nothing of
  a host that names others.
- **So the choice is MoDule's.** A package cannot tell: it reads addresses (§2.1). A set made with chosen sources plays
  differently on a Machinedrum running X.20, where the NFX hears the four before it [I].

## 6. A set that chooses nothing [M]

Engine A ran today's ring; engine B the per-Track layout with each Track's default sources. B's defaults are one, two
and three back in words 1-3, and four back in the copy word, all wrapping to the block before past Track 1. Both were
played alike, block by block (`equiv.txt`, `users.txt`):

| What | Blocks | Voice blocks that differ |
|---|---|---|
| Each of the 112 audio machines (RAM-R and -P included) on all 16 Tracks, random SYN1-8, a Hit every 13 blocks | 4,480 | 0 of 71,680 |
| 20 mixed Kits, a random machine per Track, struck at random | 1,200 | 0 of 19,200 |
| The copier one, two, three and four back, on Tracks 1, 2, 3, 6 and 16, over MD Drums' default Kit with GND-SN, GND-- and a never-struck source | 1,200 | 0 |
| Each of the user's 13, on Tracks 1, 2, 6 and 16 (all its parameters at 127), all 16 Tracks struck every 23 blocks | 480 each | 0 of 7,680 each |

The same runs with the buffers at `$147600` give the same counts. So:

- **Built-in Machines.** None reads its own buffer before writing it. R7's new values, Y:`$1000` + 32·t, change no
  sample. The stub leaves X0, X1, R0 and R2 as today's ring does; B is clobbered as the skip stub already clobbers it.
- **NEIGHBOUR packages.** The default descriptors and the copy give them, word for word, what the ring gave.
- **The host side.**
  - The delay rule with the default first source is today's `(t + 15) % 16`.
  - The preview's default sources are today's four before.
  - A state, Kit or Bank without the choice loads the default.

So a set that chooses nothing sounds exactly as today. It runs on the same single code path as a set that chooses, and
pays only the stub's 11 cycles a voice.

## 7. Recommended design for ticket 31

### 7.1 Memory and the stub

In `VoiceEngine::installHarness`, in place of the ring (`VoiceEngine.cpp:230-259`):

- **Constants.** `kTrackBuffers = $1000` (16 × 32), `kDescriptors = $7A0` (16 × 4), `kFourBack = $7E0` (16) and
  `kRingPointer = $7F0`. `kRingBuffers` goes.
- **The stub at P:`$C40`,** stateless from R1, the Track:
  1. R0 ← Y:(R1 + `$153`), the type, as 1.63's `$A3`;
  2. Y:`$7F0` ← `$7A0` + 4·R1;
  3. R7 ← its word 0, and Y:`$140` ← R7;
  4. for a type of 195 or more: copy 32 words from Y:(R1 + `$7E0`)'s buffer to R7's, then R7 ← Y:`$140` again (M7 is
     linear there);
  5. X1 ← `$100`, X0 ← `$20`;
  6. jump to `$A8`.

  The probe's version (`sources29.cpp`, `patch`) is about 27 words at `$C40` and 9 at `$C80`, and it is what §4 and §6
  measured. Reading the four-back word inside the copy path saves about 3 cycles a voice.
- **The silent skip** (`kSkipSilent`, `:214-228`) keeps its 32 zeros at R7. Its comment changes: an idle Track's buffer
  would otherwise hold its own last block.

### 7.2 The descriptors, written by the host

- **A member** `std::array<std::array<uint8_t, 4>, 16> m_sources`, default (t−1, t−2, t−3, t−4) mod 16.
- **`writeSources()` replaces `writeRing()`.** It writes 512 zeros at Y:`$1000`, then for each Track:
  - Y:`$7A0` + 4t ← `$1000` + 32t;
  - words 1-3 ← its first three sources' buffers;
  - Y:`$7E0` + t ← its fourth's.

  Call it where `writeRing` is called, at the end of `installHarness` and of `reset()`. `reset()` keeps `m_sources`, as
  it keeps the User Machines.
- **`setSources(int _track, const std::array<uint8_t, 4>&)`** writes that Track's five words. Like `setSlot`, call it
  only between blocks: the DSP reads them at the Track's next pass. No word can drift, since the stub recomputes
  everything from R1.
- **Self-reference.** It refuses its own Track as a source, or the caller does (§3.1).
- **`saveState`/`restoreState` already carry Y** (`:481-504`), so warm-ups and installs keep both buffers and
  descriptors.

### 7.3 C++ above the DSP

| Where | Change |
|---|---|
| `VoiceEngine.h` | `setSources` and `sources`; the comment at `:49-56` and `writeRing` at `:77` |
| `MdEngine.h` | Nothing (callers reach `voices()`), or a forwarding `setSources` beside `installUserMachine` |
| `mdDrumsEngine.h/.cpp` | `Engine::setSources(track, sources)`, kept in `State` too; in `renderBlock` (`:116-127`), `delay[t] = delay[sources[t][0]]` for a NEIGHBOUR Track (today's rule for the default). Optionally §3.2's refinement for sources after the NFX |
| `mdDrumsHitPreview.h/.cpp` | `Request` carries the shown Track's four sources (or the Kit does, ticket 30); `render` writes them into the engine and strikes them, without repeats, in place of the four before (`:36-39`) |
| Device, Controller, state, Kit, Bank | Where the choice lives and how it reaches the Device: ticket 30. The Device calls `Engine::setSources` between blocks, as it applies a Link |
| Editor | "Hears track 03", and "a block later" for a source after the NFX: from the Controller's copy of the sources, with no read-back from the DSP |

### 7.4 Tests (ticket 31)

- **Unchanged.** The existing `neighbours` test (copier one to four back on Tracks 1, 2 and 6, wrapping to the block
  before, zeros from idle sources) must pass unchanged: it encodes today's defaults. So must `mdEngineFirmwareTest`
  and the "a User Machine changes no other Track" check.
- **New, from §2.2.**
  - The copier one to four back on Track 12, hearing Track 3, against Track 3's raw block in the same block.
  - On Track 3 hearing Track 12, against Track 12's block before.
  - Four back with a chosen fourth source.
- **On `mdDrums::Engine`.** A NEIGHBOUR on Track 12 hearing Track 3 lands on Track 3's Hits (correlation at lag 0),
  and one hearing a later Track lands 32 samples after it.
- **The preview** strikes the chosen source.
- **Cost.** CPU and latency before and after, as CLAUDE.md asks.
- **The built-in sweep** run here (§6) has no reference once the ring is gone, so it stays a probe. `mdEngineFirmwareTest`
  remains the repository's check against the firmware.

### 7.5 Risks

- **A built-in that reads its own buffer.** The sweep found none (§6) [M]. It ran random SYN1-8 at the default
  effects, not every parameter combination; `mdEngineFirmwareTest` covers the rest against the firmware.
- **The copy for every User Machine.** 77 cycles each, 2 % of a block at worst [M]. It is harmless to a package that
  writes all 32 words, as the Standard requires (lines 417-419).
- **Overruns.** A User Machine that writes past its 32 words now hits Track t+1's buffer, which renders after it and
  rewrites it before anyone reads it this block. Track 16's would hit Y:`$1200`, which is free.
  - Today, from `$7C0`, the same overrun hits the descriptors.
  - Descriptors at `$7A0-$7EF` lie below every buffer [I].
- **A source changed mid-sound.** The NFX switches input at a block boundary, perhaps with a click. A crossfade would
  need the package to read two buffers, so none is possible without changing its code [I].
- **Emulator-only memory.** Y:`$1000` exists in MoDule's emulated voice DSP, not on a Machinedrum. A future path that
  runs X.20's own voice program would bring X.20's ring and lose the choice anyway (§5) [I].
- **Faithfulness.** A set with chosen sources has no X.20 equivalent (§5). Shown as MoDule's own in the editor, it
  misleads no one [I].
- **STEREO PAIR,** when it comes: the Standard's right instance reads T−2 (lines 477-487). With chosen sources, the pair
  reads its own words 1 and 2, so the choice for a pair should name the same two Tracks on both [I].

## Open

- §3.2's refinement (a source after the NFX takes its delay less 32): offer it or not. It also decides whether Tracks
  1-4's defaults keep today's one-block lag.
- Keeping last block's delays for sources after the NFX, so that the one-block glitch at a source's Hit falls where
  today's ordinary Hits have theirs. Not measured.
- The whole engine's CPU (`mdDrumsEngineTest`) and the Device's note-to-sound latency, before and after: ticket 31.
- What X.20's NEIGHBOUR mask of Tracks (engine +`$4376`) is for: not followed (research/11).
- Whether the choice belongs to the Kit (like Links), the set or `Bank.machines`, and what Push shows: ticket 30.
