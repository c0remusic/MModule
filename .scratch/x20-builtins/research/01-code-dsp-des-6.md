# The 6's DSP code in the voice program MoDule runs

Research for ticket 01 of the map "X.20's built-in Machines in MoDule 0.1.3". Written 2026-10-21 against `main` at
53c9629. Nothing in the repository was built, run or changed apart from this note, and no part of X.20 or of a flash
image was copied into it: addresses, counts and behaviour only.

Sources:

- **X.20 B's voice DSP program** (payload 1), decoded from the user's `Machinedrum_SPS1-UW_OS_X.20_B.syx`.
  - A probe reads it from `MD_X20_OS`. Its CRC-32 equals the first decoding's (research/01 of the User Machines map).
  - It decodes it with that research's `dsu.py`, and disassembles P with this tree's `dsp56kDisassemble`
    (md-drums' Release build, not rebuilt).
- **OS 1.63's voice program**, from the user's flash image, by the same scripts.
- **MoDule's code**, at the places named: `mdEngine/engine/VoiceEngine.*`, `HostModel.cpp`, `MdEngine.h`,
  `mdDrums/mdDrumsSamples.h`.
- **A probe outside the repository**, `probe/x20b.cpp`. It is linked against `temp\cmake_vs22`'s Release libraries
  as they stood (not rebuilt) and runs under a wall-clock guard (`run.ps1`).
  - It builds `md::engine::Engine`s on the 1.63 flash image, then writes into their voice DSP, after `reset()`, X.20's
    words for the 6 and their table, plus the 18 dispatch entries.
  - It plays them through `VoiceEngine::setSlot` and `renderBlock`, with no HostModel.
  - Its slot words are synthetic, or the defaults that research/02's port of X.20's 68k routines gives (DEFAULT
    tuning, 125 BPM).
- Everything stays in the session scratchpad, `x20b-dsp/`:
  - `scripts/x20b.py`, the helpers;
  - `s01_tables.py`, the dispatch entries and the records;
  - `s02_refs.py` and `s03_indexed.py`, every address the code names, and its voice memory;
  - `s04_reloc.py`, relocation, and the words the probe reads;
  - `s05_tab.py`, the table at `$147600`;
  - their reports `out/s0*.txt`, and the probe's reports `probe/*.txt`.

Marks: **[V]** read in the disassembly at the address named; **[M]** measured by a script or the probe; **[I]**
inferred. Every X.20 address is X.20 B's. "Type" is the voice DSP's machine code, the Machine id + 1. Track t counts
from 1 in prose.

## In short

- **Place: at X.20's own addresses, with no relocation [M].** In MoDule's voice DSP nothing holds or uses
  P:`$13b600-$13bece` (the code, 2,255 words) or `$147600-$147c00` (a 1,537-word P record the NFX read).
  - All 3,792 target words read 0 in a fresh engine.
  - No 1.63 record covers them; P-I's buffers end at `$13b5ff`; User Machine code is at `$020000-$0FFFFF`; the user's
    Samples start at `$18fc12`.
  - Moving the code would mean patching 31 absolute words (25 DO ends, 6 JSRs). Its 105 relative branches would need
    nothing.
- **Dispatch: 18 entries in the extended tables, not the bases [M]/[V].** INIT, TRIG and RENDER for types 2, 5, 6, 8, 9
  and 10.
  - They are written after `extendDispatch`, as `writeUserMachine` writes its own. `reset()` loses them otherwise [M].
  - The bases the harness hardcodes are already X.20's: P:`$8A` → `$145D38`, P:`$95` → `$145E0B`, and RENDER through
    `$145EDE` in the skip stub. Nothing changes there.
  - Type 2 is GND-SN's in 1.63. Its three entries move only when the user's OS X.20 is loaded.
- **What the code reads outside the voice [V]/[M].**
  - GND-SN-PRO reads 1.63's sine table, X:`$148000+`, which MoDule already runs 1.63's init to build.
  - NFX-CO reads two 512-word curves in P at `$147800` and `$147A00`, from X.20's `$147600` record.
  - NFX-UC keeps a 768-word delay line in its own Track's P-I buffer, `$135600 + $600·t`, found through Y:`$142` [M].
  - The three NFX read Y:`$7F0`'s words 1 and 2 only. That is the descriptor MoDule already writes [M].
  - None calls, jumps to or names X.20's loop hooks (`$13ff3c`, `$13ff88`), their records (`$13fb80`, `$13fc80`) or
    any routine outside the region [M].
- **Which Tracks an X.20 NFX hears [V].** Word 1 and word 2, so Tracks t−1 and t−2 on X.20's ring. Tracks 1 and 2 hear
  the previous block's 16, or 16 and 15. MoDule's default sources are the same, and its "hears" menu applies unchanged
  [M]. None reads its own buffer before writing it, so the four-back copy (types 195 and up) does not concern them.
- **One instruction the emulator lacks [M].** NFX-UC's RENDER uses MACRI once (P:`$13be03`). The dsp56300 fork
  implements it neither in the JIT nor in the interpreter: the JIT emits nothing for it.
  - NFX-UC then renders silence at every word set tried.
  - With the MACRI rewritten in the probe as MACI and RND (the NOP after it takes the RND), NFX-UC sounds at its
    default words [M].
  - The other five sound, with nothing else missing [M].
- **Registers [M].** The harness sets X0 = `$20` and X1 = `$100` before RENDER, as 1.63's flip did. X.20's ring leaves
  them as they were. Removing the two moves changed 0 of 150 voice blocks for each of the 6.
- **Cost [M].** At their default words, extra cycles a block per voice:

| Machine | Cycles |
|---|---|
| GND-SN-PRO | 666 |
| GND-PU | 1,236 |
| GND-SW | 1,650 |
| NFX-EV | ~4,430 |
| NFX-UC | ~4,650 |
| NFX-CO | ~4,690 |

  - A block has 71,680 cycles. The engine's host time stays 7 to 17 µs a block with one of them.
  - Sixteen Tracks on GND-SW with synthetic words reached 73,164 cycles: more than a Machinedrum's block, which MoDule
    does not enforce. The host time was 93 µs, against 725 µs of real time.
- **JIT [M].** After today's `warmUpVoices`, a first Hit costs one block of 1.3 ms (NFX-CO) to 27-28 ms (GND-SW), the
  JIT compiling on the audio thread.
  - A warm-up of the 6 takes 48.6 ms: 4 blocks of all six at once, then `restoreState`. After it, their first Hit costs
    23-64 µs.
  - `warmUpVoices` walks 1.63's Machine table, which lacks ids 4, 5, 7, 8 and 9, so it needs the 6 added.
- **Recommendation for ticket 04 (end).** Keep the code where X.20 has it. Give VoiceEngine a set of X.20 built-ins
  that `reset()` writes after `extendDispatch`. Write GND-SW's three entries first, then the other five's. Add the 6 to
  the warm-up.

## 1. Where the code and its data go

### 1.1 What X.20 adds, and what 1.63 and MoDule hold there [M]

`s01_tables.py`, `out/s01.txt`; probe mode `free`.

| Range | Words | X.20 B | OS 1.63 | MoDule |
|---|---|---|---|---|
| P:`$13b600-$13bece` | 2,255 | one record (mode 1): 15 data words, then the INIT, TRIG and RENDER routines of the 6 and of INP-CA/CB | no record | nothing: after P-I's buffers (`$135600-$13b5ff`, `kImportExternal`), below `$145D38` |
| P:`$147600-$147c00` | 1,537 | one record (mode 2) | no record | nothing: research/04 of the User Machines map finds `$147600-$147DFF` free; the Sample directory starts at `$147E00` |

- **In a fresh MoDule engine** (flash image, ROM bank loaded), all 3,792 words read 0. The 18 entries hold 1.63's:
  GND-SN's for type 2 and the null routines (`$10008E`, `$10008F`) for the others.
- **MoDule's other users of P [V].**
  - User Machine code is at `$020000 + $E000·n`, below the bridge's end (`VoiceEngine.cpp:79`).
  - The harness's stubs are at P:`$C00-$C9F`.
  - The user's Samples are written from `$18fc12` (`mdDrumsSamples.h:53`).
  - Nothing reaches the two ranges.
- **The program's init** (1.63's `$100024`, which MoDule's `reset()` runs after loading the image) clears `$135600`
  through `$13b600`, one word past P-I's buffers (research/04's `map.txt`) [M].
  - Written before `reset()`, X.20's word at `$13b600` came out 0; the other 3,791 were kept [M].
  - That word belongs to the 15 leading data words. No instruction names any of them: none of the 6's (`s02`), and
    none in the first decoding's listings of X.20's P:`$0-$3FF`, `$13b600-$13ffff` and `$145fb1+` [M]. So losing it is harmless [I].
  - X.20 reaches the same init through its own (`$145fb1` calls `$100024`) [V], so it probably loses the word too [I].

### 1.2 The table at `$147600` [M]

`s05_tab.py`, `out/s05.txt`.

- **`$147600-$1477FF` is not a table.** 495 of its 512 words equal NFX-CO's code from `$13bccf`, and its first 103 in a
  row. It reads like a stale copy left in the record. Nothing names it.
- **`$147800-$1479FF`** is a monotonic curve from −1.0 to 0. NFX-CO and the INP pair read it as P:(R3+N3) with R3 =
  `$147800` and N3 a magnitude [V].
- **`$147A00-$147BFF`** rises monotonically from `$0040C9` to `$7E732B`. `$147C00` holds `$7FFFFF`. NFX-CO reads
  P:(R2+N2) and the word after it, so it interpolates and needs the 513th word [V].
- **Copying the whole record is simplest.** The range is free, and the first half then costs nothing [I].

### 1.3 Relocation [M]

`s04_reloc.py`.

- **Absolute words that name the region:** 31, all in code (25 DO loop ends, 6 JSRs).
- **Relative branches:** 105, which need nothing.
- **Absolute data addresses inside the region:** none (no `#>$13b...` immediate, no absolute operand).
- **So a move is possible.** It costs 31 patched words, found by decoding every instruction, and the 18 entries
  offset. Nothing calls for it while the range is free [I]. Kept in place, X.20's words are written as they are.

## 2. The dispatch entries

### 2.1 Which entries [M]

- **X.20 B's own copies of 1.63's three base tables** (`$145AF5`, `$145BB6`, `$145C77`, in its image) differ from 1.63's
  in exactly 24 entries: types 2, 5, 6, 8, 9, 10, 87 and 88, in each table.
  - Its init copies the bases to the extended tables `$145D38`, `$145E0B` and `$145EDE` (211 words each), which only
    exist at run time (research/01 §7).
- **For the 6, the entries are 18:**

| Type (id) | Machine | INIT | TRIG | RENDER | 1.63 had |
|---|---|---|---|---|---|
| 2 (1) | GND-SN-PRO | `$13b60f` | `$13b610` | `$13b622` | GND-SN: `$1000a3`, `$1000a4`, `$1000ad` |
| 5 (4) | GND-SW | `$13b6c2` | `$13b6c3` | `$13b6d4` | null routines |
| 6 (5) | GND-PU | `$13b969` | `$13b96a` | `$13b97f` | null routines |
| 8 (7) | NFX-EV | `$13bb93` | `$13bb94` | `$13bba3` | null routines |
| 9 (8) | NFX-CO | `$13bca7` | `$13bcaf` | `$13bcb0` | null routines |
| 10 (9) | NFX-UC | `$13bd44` | `$13bd5d` | `$13bd5e` | null routines |

- **The three GND INITs are a bare RTS,** and so is NFX-UC's TRIG.
- **INP-CA and INP-CB** (types 87 and 88, one shared set of entries) stay out of the map. Their code lies at the region's
  end, `$13be1b-$13bece`. Copying it with the rest is harmless while their entries keep the null routines [I].

### 2.2 Where to write them [V]/[M]

- **The block loop reads only the extended tables.** P:`$8A` and `$95` hold `$145D38` and `$145E0B` once `extendDispatch`
  has run, and the skip stub reads RENDER as Y:(R0 + `$145EDE`) (`VoiceEngine.cpp:215`).
  - These are X.20's own three bases (research/12 §1.2). The harness hardcodes them and needs no change for the 6.
- **`extendDispatch` copies 1.63's base tables each `reset()`** (`VoiceEngine.cpp:332-345`). Entries written before it
  are lost: the probe wrote all 18, called `reset()`, and found 0 of 6 RENDER entries kept [M].
- **So they go where `writeUserMachine` writes the User Machines' entries:** after `extendDispatch` in `reset()`, and
  at install, through `memWriteP` so that the JIT drops what it compiled.
- **Removing X.20** writes back the base tables' entries, as `removeUserMachine` does for type 195 and up.
- **The extended tables could also be left alone,** with X.20's entries written into the base tables before
  `extendDispatch` copies them. Nothing else reads the bases [V]. The extended tables are the cleaner place: removal
  then needs no copy of 1.63's values [I].

## 3. What the code reads and writes outside its own voice

`s02_refs.py`, `s03_indexed.py` (`out/s02.txt`, `out/s03.txt`, `out/s02_refs.txt`).

### 3.1 Voice memory [V]

R6 is the voice's slot, Y:`$800` + `$40`·t.

- **Words 1 to 8 (CO: 1 to 9) are the host's.** The RENDERs read them and never write them.
- **The words above are their own state,** set by TRIG (GND, EV) or INIT (CO, UC) and kept by RENDER.

| Machine | Reads from the host | State |
|---|---|---|
| GND-SN-PRO | 1-8 | 9-`$16` |
| GND-SW, GND-PU | 1-8 | 9-`$13` |
| NFX-EV | 1-8 | 9-`$10` |
| NFX-CO | 1-5, 7-9 (not 6) | `$A-$C` |
| NFX-UC | 1-8 (1 and 2 again at INIT) | 9-`$11` (9 rewritten by INIT) |

- **This matches research/02's counts** (9 words for the GND and NFX-EV, 10 for CO and UC, word 0 included). `HostModel`
  writes exactly the count the routine returns (`HostModel.cpp:571`). So it never reaches the state, as long as the
  port returns the same counts.
- **UC's word 9 is the one overlap.** The host writes word 9 every block (count 10), and UC's INIT also sets it to 0.
  Under X.20 the routine writes it too, so the host's value wins in both [I].

### 3.2 Outside the voice [V]/[M]

| What | Who | Notes |
|---|---|---|
| X:`$148000`+ (R2), the sine table | GND-SN-PRO | 1.63's init builds it (research/04: `$147fff-$14ffff`); MoDule runs that init [V] |
| X:`$000-$01F` through R4 = 0, M4 = `$1F`; X:`$62-$69`; X:`$0-$11`, Y:`$0-$E`; X:`$20-$21` | GND-SN-PRO, GND-SW; NFX-UC; NFX-EV | The renderers' scratch (research/04 §1), reloaded from the slot at each RENDER's start |
| P:`$147800`, `$147A00` | NFX-CO (and INP) | §1.2 |
| Y:`$142` | NFX-UC's INIT | The dispatcher's Track number |
| Y:`$135600 + $600·t`, `$300` words | NFX-UC | Its delay line, cleared by its INIT |
| Y:`$7F0`, words 1 and 2 | NFX-EV, -CO, -UC | §4 |

- **NFX-UC's buffer [M].** Its INIT computes `$135600` + `$600` × Y:`$142` and keeps it at slot words `$F` and `$10`.
  The probe found `$135600`, `$137400` and `$13b000` on Tracks 1, 6 and 16 (probe mode `uc`).
  - That is the Track's own P-I region, which no other Track's machine touches.
  - A User Machine with import 6, though, gets `$135600`, Track 1's region, on any Track (`VoiceEngine.cpp:82`). An
    NFX-UC on Track 1 would then share its delay line with it. This is the same overlap P-I Machines on Track 1 already
    have [I].
- **X.20's hooks and records.** No instruction of the 6 names `$13ff3c`, `$13ff88`, `$13f7ec+` (the MDS services),
  `$13fb80` or `$13fc80`, and no flow leaves the region. The only two "targets" outside it are the 15 data words
  misread as code [M].
- **So the 6 do not depend on X.20's fetch (extended ids, ANTI-CLICK's hold) or on its post-render (PRECISE's shift).**
  Their types fit 1.63's word 0 [I]. MoDule already does both in C++ (research/13 of the User Machines map, and the
  delay rule).

### 3.3 Registers at entry [V]/[M]

- **What every RENDER sets itself.** Its own R4/M4 and N registers.
- **What they rely on.**
  - R6 (the slot) and R7 (the buffer, M7 = `$1F`).
  - M0 linear: NFX-EV and -UC copy M0 into M7 to read two back as Y:(R7+N7), the offset form NFX-BC uses (research/14
    §2.1). 1.63's loop sets M0-M5 linear before RENDER, and MoDule's path keeps it.
- **X0 and X1.** The probe removed the stub's `move #>$100,x1` and `move #>$20,x0` (X.20's ring sets neither). Each
  of the 6 gave the same voice in 150 of 150 blocks, with two 1.63 Tracks rendering before it [M].

## 4. The NFX and Y:`$7F0`

- **What they read [V].** Each loads Y:`$7F0` once per RENDER, by its absolute address:
  - NFX-EV (`$13bba9`) and NFX-UC (`$13bd60`) take word 1 into R5, and word 2 as an offset from R7 (N7 = word 2 − R7,
    M7 linear);
  - NFX-CO (`$13bcb9`) takes word 1 into R5 and word 2 into R4.
  - None reads word 0 or word 3, and none reads its own buffer before writing it.
  - So they are the Standard's NEIGHBOUR shape, two back.
- **The same descriptor as MoDule's [M].** MoDule's per-Track descriptor (Y:`$7A0` + 4·t, research/14) gives them
  what X.20's ring would, with no change. Probe modes `desc` and `real`:
  - NFX-EV and NFX-CO on Track 3, at their default words, sound when Tracks 1 and 2 play TRX-BD. They render silence
    when `setSources` points their words at idle Tracks.
  - With only Track 9 sounding, NFX-CO followed it from word 1 alone and from word 2 alone. NFX-EV followed it from
    word 2 (at synthetic words; word 1 alone gave silence, so EV reads word 1 for something other than its output) [M]/[I].
- **Which Tracks they hear under X.20 [V].** Word 1 is one back and word 2 two back on X.20's ring: Tracks t−1 and t−2.
  - Tracks 1 and 2 hear Tracks 16 and 15, or 1 and 16, of the previous block (research/12 §1.3).
  - MoDule's `defaultSources` give the same two. A set that chooses nothing therefore hears what X.20's NFX hear [I].
  - MoDule's choice of sources is its own extension, as for the packages (research/14 §5).
- **One difference in timing [I].** Under X.20's PRECISE, the sources' buffers are already shifted to their triggers'
  sample before the NFX reads them (research/12 §1.5). MoDule shifts after the voice DSP, so its rule for a NEIGHBOUR
  (the delay of its first source) carries over to these three.
- **Types below 195 get no four-back copy** (`VoiceEngine.cpp:262-264`). None of the three needs it [V].

## 5. The missing MACRI

- **The JIT reports it [M].** `[dsp56k JIT] instruction not implemented at P:$13be03 ... macri`, the first time
  NFX-UC renders.
  - `JitOps::op_Macri_xxxx` is `errNotImplemented` (`jitops.h:207`), and so is the interpreter's
    `DSP::op_Macri_xxxx` (`dsp_ops_alu.inl:1112`) [V].
  - In Release the assert is off, so the instruction does nothing.
- **A scan of the 6's mnemonics** against the JIT's `errNotImplemented` list finds no other. Their `rep` is the
  immediate form, which the JIT has [M].
- **Measured [M]** (probe mode `real`), NFX-UC on Track 3 with TRX-BD on Tracks 1 and 2:
  - as it is, silence in 300 of 300 blocks, at its default words and at 12 other word sets;
  - with the MACRI rewritten as `maci` and `rnd b` (the NOP after it takes the RND), it sounds in 291 of 300 blocks.
    With idle sources it is silent.
- **The fix belongs in `source/dsp56300`, the user's fork,** in both the JIT and the interpreter, as MPYRI was added
  beside MPYI (`jitops_alu.cpp:1219-1236`). MACRI is MACI with `alu_mpy`'s rounding flag, which MACR already uses.
  The fork's unit tests pin it.
  - Rewriting X.20's word in place would also work, but it would change X.20's code rather than the emulator [I].
- **GND-SW, ticket 04's Machine, does not need it.**

## 6. Cost

### 6.1 Cycles [M]

Probe mode `real`: the default words of research/02's port, the Machine on Track 3, a Hit every 64 blocks, 280 blocks
counted. An idle engine renders 2,482 cycles a block, and 5,613 with the NFX's two TRX-BD sources.

| Machine | Cycles a block | Per voice | Engine host time (mode `each`) |
|---|---|---|---|
| GND-SN-PRO | 3,148 | 666 | 7.3-7.9 µs |
| GND-SW | 4,132 | 1,650 | 10.1-10.3 µs |
| GND-PU | 3,718 | 1,236 | 10.0-11.0 µs |
| NFX-EV | 10,040 | ~4,430 | 15.4-16.9 µs |
| NFX-CO | 10,298 | ~4,690 | 14.0-16.1 µs |
| NFX-UC (MACI + RND) | 10,262 | ~4,650 | 16.2-17.1 µs |

- **The cost moves with the words.** With synthetic words, GND-SW took 4,419 cycles a voice and GND-SN-PRO 1,717.
- **Sixteen Tracks** on one of them, all struck (mode `sixteen`, synthetic words):
  - GND-SN-PRO 29,938 cycles a block, NFX-EV 61,266, NFX-CO 71,938, GND-SW 73,164, NFX-UC 77,042, against a
    Machinedrum block of 71,680.
  - MoDule's voice DSP has no deadline in cycles. Host time stayed at 51 to 96 µs a block, 7 to 13 % of a block's
    725.6 µs real time.
  - X.20 B's own budget for these on a Machinedrum was not read [I].

### 6.2 The JIT [M]

Probe mode `jit`: 16 Tracks idle, then each Machine struck in turn on Track 3. Host time of the Hit's block, against
later blocks:

| | GND-SN-PRO | GND-SW | GND-PU | NFX-EV | NFX-CO | NFX-UC |
|---|---|---|---|---|---|---|
| Stock `warmUpVoices` | 1,455 µs | 28,581 µs | 2,746 µs | 1,604 µs | 1,264 µs | 7,600 µs |
| The 6 warmed too | 29 µs | 42 µs | 26 µs | 64 µs | 23 µs | 35 µs |
| Later blocks | 9-14 µs | | | | | |

- **The stock warm-up does not reach them.** `warmUpVoices` walks `os->machines()`, 1.63's table, which has no ids 4, 5,
  7, 8 or 9 and plays id 1 with GND-SN's words (`MdEngine.h:65-98`). GND-SW's first Hit then costs 28 ms in one
  32-sample block, against 0.7 ms at 32 samples.
- **The probe's warm-up of the 6** took 48.6 ms: all six on Tracks 1-6, 4 blocks, then `restoreState`. The stock warm-up
  takes 177 ms (CLAUDE.md).
- **`HitPreview`'s fresh engines need the same** words and warm-up as the Device's [I].

## 7. Recommendation for ticket 04 (GND-SW's first sound)

### 7.1 Placement and dispatch

- **Keep X.20's addresses.**
  - Write P:`$13b600-$13bece` and `$147600-$147c00` as the user's OS gives them, through `memWriteP`.
  - No relocation, and nothing else in MoDule moves.
  - Ticket 03's reader finds the two records by shape, not by X.20 B's addresses: the 18 entries are the base tables'
    differences from 1.63's at types 2, 5, 6, 8, 9 and 10; the code record is the one that holds their targets; the
    table record is the one that holds the immediates `$147800`/`$147A00`, or simply the record that 1.63 lacks below
    the extended tables.
- **One owner in VoiceEngine.** For example `installBuiltins(std::shared_ptr<const X20Builtins>)`, which holds the
  words and the 18 entries. `reset()` writes them after `extendDispatch`, beside `writeUserMachine`, and `reset()` keeps
  them as it keeps the User Machines.
  - Between blocks only. Removing them writes back the bases' entries.
  - Each Device and each `HitPreview` engine gets the same immutable words.
- **For ticket 04, GND-SW's three entries alone,** to keep the slice small.
  - Type 2 stays GND-SN until the map decides GND-SN-PRO's switch. Writing it would change GND-SN under
    `mdEngineFirmwareTest`'s eye whenever an X.20 OS is loaded.
  - Writing all the code at once costs nothing.
- **Leave the harness's bases, the stub at P:`$C40` and the four-back copy as they are.** Nothing in the 6 needs a
  change there (§2.2, §3.3, §4).

### 7.2 The rest of the slice

- **HostModel.** GND-SW (id 4) becomes an audio Machine whose words come from research/02's port. Count 9, called on
  every tick as 1.63's routines are; word 0 the trigger flag, id + 1 = 5.
  - `setSlot`'s count must stay 9, so that the state at words 9-`$13` survives.
- **Warm-up.** `warmUpVoices` plays GND-SW at its default words, and the others when they come. Without it the first
  Hit costs 28 ms of audio thread.
- **Start and latency.** Measure GND-SW's `voiceStart` as `mdDrumsEngineTest` does for every Machine, and hold it to
  `AlignedStart`.
  - The probe saw its first non-zero sample at sample 0 of the Hit's block, at its defaults [M]. `voiceStart` uses its
    own threshold.
  - CLAUDE.md asks for latency before and after.
- **Tests.** That nothing else changes:
  - the 112 Machines of 1.63 and the User Machines, sample for sample, with and without the X.20 built-ins installed;
  - `mdEngineFirmwareTest` untouched;
  - the voice DSP's words at the two ranges and the 18 entries after a `reset()`;
  - skipped (code 77) without `MD_X20_OS`.
- **After the slice.**
  - GND-PU and GND-SN-PRO need nothing more on the DSP side.
  - The three NFX add the NEIGHBOUR delay rule and the preview's sources for built-in ids 7-9.
  - NFX-UC waits for MACRI in the dsp56300 fork (§5).

## Open

- What NFX-EV reads word 1 for (its output followed word 2 alone at synthetic words). It bears on which source's delay
  a built-in NFX should take.
- X.20 B's own limit, if any, on Tracks running these Machines at once, given that 16 GND-SW, NFX-CO or NFX-UC exceed
  a Machinedrum block in cycles (§6.1).
- Whether X.20's init clears `$13b600` as MoDule's does (inferred from its call of `$100024`, not run).
- The 15 data words at `$13b600-$13b60e`: never named by an instruction, purpose unknown.
- Bit-exact checks against X.20 itself still wait on X.20 booting somewhere (research/02 of the User Machines map); this
  note checked that the code runs and sounds, not that it sounds as on X.20.
