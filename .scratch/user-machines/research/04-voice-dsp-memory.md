# The voice DSP's room and time for 16 User Machines

Research for ticket 04 of the map "User Machines from X.20 in MoDule". Written 2026-10-17 against `main` at cb44f07.
`main` moved on meanwhile (to 392eb64, adding `mdEngine/tools/mds/`), but nothing under `mdEngine/engine/` or
`mdEngine/tools/mdfw/` changed.
Read: `mdEngine/engine/VoiceEngine.*`, `MdEngine.h`, `HostModel.h`, `mdEngine/tools/mdfw/Firmware.*`,
`mdDrums/mdDrumsEngine.cpp`, this tree's `dsp56300` (`memory.*`, `dsp.*`, `jit.cpp`, `audio.h`), Machinemodule's
`docs_PROTOCOL.md` and machinedrum-kit's `01-system-overview.md` and `08-dsp2-voice-abi.md` (copies in
`.scratch/uc-less-engines/`), machinedrum-kit's `docs/02-memory-maps.md` (janne808/machinedrum-kit on GitHub), and
jmamma/MDS's `docs/MDX_MachineDumpStandard.txt` and `MDX_MachineDumpProtocol.txt`. Run: a probe outside the
repository, `voicemem.cpp` in the session scratchpad (`voicemem/`), linked against `temp/cmake_vs22`'s Release
libraries (rebuilt through the target `mdDrumsEngineTest`) and run on the user's 1.63 flash image under a wall-clock
guard; two read-only Python decoders of the X.20 B zip's 43 packages, which read the zip in memory and extract nothing.
Nothing in the repository was changed but this file. Research 01 and 03, written alongside, are cited where they
confirm or settle a point.

Memory was measured by poisoning. Before the voice DSP's init, the probe writes a pattern of its own into every word
that the section 1 image does not load. It then plays every audio machine on every Track: 104 machines, 4,992
blocks, random SYN1-8 values and Hits, then the 8 RAM machines. Every word that no longer holds the pattern was
written. A second pair of engines then gets the pattern only in the words never written, and plays the same against
a clean engine, block by block and cycle by cycle. That finds which of those words are read.

Marks: **[V]** read at the place named; **[M]** measured by the probe; **[I]** inferred from the facts around it.

## In short

- **Room: yes, with nothing to allocate.** The emulated voice DSP already holds 2M words of P. In the emulator,
  `$020000-$0FFFFF` (917,504 words, shared by P, X and Y) is never written by the 1.63 program, and read only through
  stale pointers. 16 User Machines need only their code there. The 43 packages are 36 to 2,831 words; the 16
  largest come to 27,706 together. Everything else they import already exists in 1.63's layout: the per-Track X and Y
  blocks at `$800 + $40·t`, 1,536 words of external memory per Track (exactly P-I's buffers at `$135600 + $600·t`),
  the sine table at `$148000`, and scratch at X:`$000` and Y:`$000`. [M], with [I] for the import addresses.
- **Time: yes.** In emulated cycles, a busy Kit costs 38,789 a block (54 % of 71,680). The dearest built-in Kit, 16 ROM
  Machines, costs up to 67,309 (94 %). 16 User Machines at the MDS limit of 3,951 a Track cost about what 16 ROM
  Machines cost today. The harness waits for no deadline, so a heavier package (TX9-SD, research 03) costs host time
  only: 83 µs a block for the busy Kit, up to about 215 µs for 16 ROM Machines, against 725.6 µs of real time. [M]
- **Call site: nothing to patch in the voice loop.** The dispatcher calls INIT, TRIG and RENDER through three tables,
  with the registers MDS promises. A stand-in User Machine was installed both at a type 1.63 leaves empty and at type
  195, X.20's USR-01, in tables copied to 211 entries as X.20 does. In both, it played on its Track while the
  built-in Tracks stayed bit-identical. [M]
- **A separate DSP56300 gains nothing but isolation.** As this tree builds a DSP it costs about 82 MB per instance,
  80.8 MB of which are its peripherals' audio rings; built without them, about 1 MB. It adds about 1.25 µs a block,
  the packages cost the same on it, and NEIGHBOUR forces it to run after the voice DSP. [M]

## 1. What the 1.63 voice program and the UW bank occupy

The image is 135 records, 249,712 words: 248,034 in P or at and above the bridge (`$020000`, where X and Y are P's),
74 in X below it, 1,604 in Y below it (`image.txt`) [M]. The ROM bank adds the directory and 261,138 words of samples
[M]. During play, no word of code or table changes. Only the dispatcher's variables (Y:`$141-$142`,
Y:`$153-$162`), the harness's patches and the sample directory (which the bank overwrites) differ from the image [M].

| Range | What | Written by |
|---|---|---|
| P:`$000000-$0003E1` | vectors, init call at `$24`, block loop `$64-$E1`, host ISRs `$E8-$110`, resident code | image |
| P:`$000C00-$000C3B` | the harness's stubs (`VoiceEngine.cpp:140-194`) | harness |
| X:`$000000-$0001FF` | renderers' per-call scratch (`$000-$0FF`), the ADC ring (`$100-$1FF`, silence from ESSI1) | play |
| X:`$000202-$000256` | tables and pointers; X:`$243` is the master-return ring pointer (P:`$C2-$C7`) | image |
| X:`$000700-$0007FF` | the master return from DSP1 (DMA2) | never: the harness skips P:`$B7-$CE`, which arms it |
| X:`$000800-$000BF9` | per-Track X blocks | play |
| Y:`$000000-$00013F` | scratch (`$000-$0FF`), the two output banks `$100` and `$120` | play |
| Y:`$000140-$000162` | the dispatcher: bank, S, Track, active type of each Track (`$153 + t`) | image, then play |
| Y:`$000163-$000782`, `$0007FF` | the sample player's interpolation table; a word P:`$70` reads | image |
| Y:`$000800-$000C00` | 16 slots of 64 words (word 0 cleared at init, 17 words) | init, play |
| `$100000-$135205` | resident Machine code to `$103C7A`, E12 tables and samples | image |
| `$135600-$13B5FF` | P-I's per-Track buffers: Track t alone writes `$135600 + $600·t`, 1,536 words (`pibuffers.txt`) | init clears, play |
| `$140000-$145D37` | X tables, P-I code `$142100-$145AF4`, the dispatch tables `$145AF5-$145D37` | image |
| `$146000-$1475FF`, `$147E00-$147F07` | X and P tables; the sample directory | image, bank |
| `$147FFF`, `$148000-$14FFFF` | the boot marker; the sine table, 32,768 words | init |
| `$150000-$18FC11` | the UW bank | `loadRomBank` |

All [M] for the extents and writers. [V] for what each holds: the disassembly in `image.txt`, Machinemodule's
protocol notes, and machinedrum-kit's memory map.

## 2. Free memory

Never written, neither image nor bank (`map.txt`) [M]. The last column comes from the region-by-region check:
every region poisoned alone, 5,376 blocks of every machine on every Track against a clean engine (`bisect.txt`,
`bisect2-e12.txt`, `inp.txt`) [M].

| Range | Words | On a Machinedrum | Read by |
|---|---|---|---|
| P:`$0003E2-$000BFF` | 2,078 | only `$3E2-$3FF` (internal P ends at `$3FF`, machinedrum-kit 02) | nothing |
| P:`$000C3C-$01FFFF` | 127,940 | no | nothing |
| P/X/Y `$020000-$0FFFFF` | 917,504 | no (no AAR area covers it, `VoiceEngine.cpp:33`) | INP-FA and INP-FB, through pointers left in the Track's block by TRX-CH, -OH, -MA, EFM-CY or a P-I Machine; INP-EA twice, ROM-08, -09 and -10 once each, in 86,016 voice blocks |
| X:`$000257-$0006FF` | 1,193 | yes, internal | nothing |
| Y:`$000783-$0007FE` | 124 | yes, internal | nothing |
| X:`$000BFA-$01FFFF`, Y:`$000C01-$01FFFF` | 128,006, 127,999 | no | nothing |
| `$135206-$1355FF` | 1,018 | yes (RAM-R's staging, idle in MoDule) | nothing |
| `$13B601-$13FFFF` | 18,943 | yes | nothing |
| `$145D38-$145FFF` | 712 | yes | nothing |
| `$147600-$147DFF` | 2,048 | yes | nothing |
| `$18FC12-$1FFFFF` | 459,758 | yes | E12 Machines read `$1F85B5-$1FFFFF`; the Samples map keeps the whole range for the user's Samples |

- The check also found three regions that are not free. RAM-R and RAM-P read X:`$700-$7FF`. TRX Machines read
  slot words before writing them, a block that the Track's previous Machine left. TRX-S2 read one word of
  X:`$000-$1FF` once. [M]
- Poisoning the never-written words left them all intact over 5,376 blocks, with the same cycles in every block.
  [M]
- X.20 put its additions to this program in exactly these holes (research 01, §5 and §7): P:`$3E2-$3FD`, its 8
  Machines at `$13B600-$13BECE`, the User Machine code area `$13C108-$13F7EB` (14,052 words), its host code up to
  `$13FFFF`, the tables at `$145D38-$145FB0`, its init at `$145FB1-$145FFD`, a table at `$147600-$147C00`, and
  Y:`$7A0-$7F0`. [V] there, [M] here.

## 3. What 16 User Machines need, and where it goes

From the 43 packages' headers and import tables (`mds_headers.py`, `mds_imports.py`) [M]:

- program 36 to 2,831 words (NFX-MX, TX8-HH), 47,951 for all 43, 27,706 for the 16 largest;
- X and Y words up to 64 each;
- declared clocks from 498 to 3,865.

The header's program field is 16 bits, so a package holds at most 65,535 words [V Standard, offset 22]. As for
imports [M], 28 packages import nothing:

| Symbol | Resource | Packages that import it |
|---|---|---|
| 1 | sine table | the five TXZ |
| 6 | Track external memory | NFX-CH, NFX-EN, P-I-BW, P-I-EP, P-I-PN, P-I-ST |
| 7 | shared X scratch | NFX-EN, P-I-HP, SYN-VP, TX8-CY, TX8-HH |
| 8 | shared Y scratch | P-I-HP, P-I-ST, SYN-OJ, SYN-VP, TX8-CY, TX8-HH |

| MDS asks for | 16 Tracks | In 1.63's layout | New memory |
|---|---|---|---|
| 64 X and 64 Y words per Track at R6 | 2,048 | X/Y:`$800 + $40·t`, which the dispatcher puts in R6 (§4) | none |
| Parameters at Y+1 to Y+8, the stereo role at Y+9 | in the slot | words HostModel writes, as it writes a built-in's packet | none |
| 1,536 words of external memory per Track (symbol 6) | 24,576 | `$135600 + $600·t`, P-I's buffers, 1,536 words per Track [M]; MDS warns that the region "may hold another machine's data" [V] | none |
| The built-in sine table, 32,768 words (symbol 1) | shared | `$148000`, built by 1.63's init; X.20's own 8 Machines read it there (research 01, §6) | none |
| 32-word X and Y scratch (symbols 7, 8) | shared | X:`$000-$01F`, Y:`$000-$01F`, the stock renderers' per-call scratch | none |
| The rings and positions (symbols 2 to 5) | — | not imported by the 43. Input X:`$100-$1FF` holds silence; Main X:`$700-$7FF` and its pointer X:`$243` stay unfed (§1) | none, but the harness would have to feed them |
| Code, relocated | ≤ 27,706 for the 43's largest 16 | `$020000-$0FFFFF`. It must sit where P, X and Y alias (research 03: TX8-BD reads its tables with `x:` moves) | none: `kSizeP` already allocates it (`VoiceEngine.cpp:29`) |
| 3 dispatch words per User Machine | 48 | X.20's extended tables at `$145D38`, `$145E0B`, `$145EDE`, free in 1.63 (§4) | none |

[M] for the 1.63 side and the packages. [I] that X.20 resolves symbols 1, 6, 7 and 8 to these same addresses: the
sizes match, and research 01 found X.20 keeping the rest of 1.63's layout. X.20's own code area holds 14,052 words,
fewer than the 16 largest of the 43 [V research 01, M here]. MoDule has room for all 16 whatever their size.

## 4. Where the call goes

The block loop as `reset()` leaves it (`image.txt`, P:`$64-$E5`) [V]:

- `$64`: S = `$800`, Track 0.
- `$6B`: per voice. M0-M7 linear at `$74-$7C`; R6 = S, R1 = Track (`$7D-$7F`).
- `$81-$8D`: if the slot's word 0 is set and differs from the Track's active type (Y:`$153 + t`), the type becomes
  active and `jsr (r2)` calls INIT from Y:`$145AF5 + type` (the base at P:`$8A`).
- `$8E-$98`: Y:S is cleared, then `jsr (r1)` calls the update routine, MDS's TRIG, from Y:`$145BB6 + type` (base at
  P:`$95`). Both this and INIT run only when word 0 is set (`$83`).
- `$99-$A7`: the output bank flips between Y:`$100` and `$120`; R7 is set to it.
- `$A8`: the harness jumps to `$C20`, which skips types 0 and 1 and otherwise reads Y:`$145C77 + type` at `$C32`
  (base at P:`$C33`), then returns to `$AA`.
- `$AA-$B4`: R6 = S, M0-M5 linear, M7 = `$1F`; `jsr (r1)` calls RENDER.
- `$B5`: the harness sends the 32 samples (80 cycles), then `$D5` moves to the next voice.
- `$E2`: the harness waits for the host's go word.

None of VoiceEngine's patch points (`$24`, `$73`, `$A8`, `$B5`, `$D5`, `$E2`, `$100093`) needs to change. A User
Machine replaces a built-in on one Track when HostModel puts its type in the slot's word 0 at the Hit, and its
converted parameters in words 1 to 8. The dispatcher then calls its INIT on the change, its TRIG at each Hit and its
RENDER every block, Hit or not, as MDS describes [V].

The probe's `hook` test installed two stand-ins at P:`$030000` and `$030100` (`hook.txt`). Each saves its entry
registers and counts its calls; its RENDER writes a constant. Both were played on Tracks 6 and 7 for 200 blocks,
with TRX-BD, EFM-CB and P-I-MT on Tracks 1, 2 and 5, and Track 6 changed from one to the other at block 101 [M]:

| | (A) a type 1.63 leaves to the fallback (5, 6) | (B) X.20's way: tables copied to 211 entries, bases at P:`$8A`, `$95`, `$C33` patched, types 195 and 196 |
|---|---|---|
| Built-in Tracks against an unpatched voice DSP | identical in all 200 blocks | identical in all 200 blocks |
| User Machine samples | all right | all right |
| INIT | once per change; R1 = Track, R6 = `$800 + $40·t` | the same |
| TRIG | at every Hit, R6 = S | the same |
| RENDER | every block; R7 = the bank, M7 = `$1F`, M0 and M6 linear | the same |

- 1.63's tables leave 81 types to the fallback. 58 of them belong to no Machine id: 0, 5-16, 31, 32, 41-48, 74-80,
  87-96, 115-120, 125-128, 165 and 170-176 [M].
- X.20 uses types 5, 6, 8, 9, 10, 87 and 88 for its 8 new built-ins, and 195-210 (id + 1 for ids 194-209) for USR-01
  to USR-16 (research 01, §6-7).
- (B) keeps HostModel's rule "type = id + 1" for the numbers of ticket 06, and leaves X.20's built-in types free for
  later.

What an implementation must respect:

- Code goes in through `DSP::memWriteP`, which invalidates what the JIT compiled (`dsp.cpp:1105-1117`).
  `VoiceEngine::writeP` uses `Memory::set`, which does not [V].
- `VoiceEngine::reset()` reloads the image, and with it the tables and P:`$8A`/`$95`. The installation must be
  redone after a reset, as the ROM bank is [V].
- `warmUpVoices` saves and restores P, X and Y (`MdEngine.h:80-120`). User Machines installed before it keep their
  code. Their first RENDER otherwise compiles on the audio thread, as a built-in's did (1.5 to 8 ms, mdEngine
  README) [V].
- NEIGHBOUR: in 1.63 the other output bank holds the previous Track's block (machinedrum-kit 08). X.20 rotates a
  ring of 4 buffers at Y:`$7F0`, and research 03 hands packages a descriptor there. Y:`$7A0-$7F0` is free in 1.63
  (§2) [V], [M].

## 5. Cycles today

Emulated cycles (`DSP::getCycles`, the DSP56300 table without wait states), per 32-sample block (`cycles.txt`) [M].
The per-Track lines measure one voice above the idle block; that includes its dispatch and the harness's 80-cycle
send.

| What plays | Mean | Max | Max / 71,680 | Voice DSP, µs a block |
|---|---:|---:|---:|---:|
| 16 idle Tracks | 1,474 | 1,474 | 2.1 % | 4.8 |
| MD Drums' default Kit, a Hit every 21 blocks | 38,789 | 39,210 | 54.7 % | 83 (p99 140) |
| The same, all 16 struck every 11 blocks | 35,555 | 39,360 | 54.9 % | 78 (p99 137) |
| 16 × ROM-22 | 64,131 | 67,309 | 93.9 % | 202 |
| 16 × EFM-CB | 65,363 | 66,669 | 93.0 % | 54 |
| 16 × P-I-MT | 53,584 | 56,013 | 78.1 % | 129 |
| 16 × TRX-BD | 40,250 | 49,522 | 69.1 % | 95 |
| One Track: ROM-01 to -32 | ~3,890 | 4,115 | | |
| One Track: EFM-CB, TRX-S2, seven of the nine P-I | 2,968 to 3,993 | 4,075 | | |
| One Track: TRX-BD | 2,424 | 3,003 | | |
| One Track: GND-SN, empty ROM-33 to -48 | 694, 355 | 862, 482 | | |

- **Headroom.** On the busy Kit's dearest block, 32,320 cycles of 71,680 are left. The MDS limit is 3,951 a Track.
  16 at the limit, plus the idle loop, come to 64,700 cycles (90 %), or about 66,300 (93 %) if the limit leaves out
  the dispatch and the harness's send. That is what 16 ROM Machines take today, the dearest Kit the voice DSP already
  plays. [M] for ROM, [I] for User Machines at their declared figure.
- **Packages over their figure.** Four measure above their own declared figure, and TX9-SD above the limit (5,537 a
  block, 6,691 at most; research 03). 16 × TX9-SD would be about 108,500 cycles. In MoDule that is no fault: the
  harness waits for no frame, so a block costs what its code costs in host time [V].
- **Host time.** Per emulated cycle it ranges from 0.8 ns (EFM) to 3.3 ns (ROM). Research 03 measured 2 to 9 µs a
  block for one User Machine Track. The real time of a block is 725.6 µs. Other processes kept the CPU 37-52 % busy
  during these runs (Ryzen 7 3700X), so the µs are indicative.
- **The hardware floor.** On the hardware, each Track also waits about 3,000 cycles for the link to DSP1 (the
  "slot floor", machinedrum-kit 08). The harness has no link, so in MoDule a Track costs only its own code [V].

## 6. A separate DSP56300 instead

- **Memory** (`dsp.txt`) [M]:
  - each DSP as this tree builds it: 81.9 MB (VoiceEngine), 82.1 MB (MasterEngine);
  - a bare DSP: 81.3 to 81.9 MB whatever its memory size, from 64K to 2M words;
  - of which `Peripherals56303` alone takes 80.8 MB: its ESSI audio rings, 32,768 frames per direction
    (`audio.h:268-291`) [V];
  - the memory of 64K words takes 0.4 MB, the DSP with `PeripheralsNop` 0.7 MB.

  So a second DSP built like VoiceEngine adds about 82 MB to each plug-in instance. One built on `PeripheralsNop`,
  with the host's go word and the flags passed in memory instead of HI08, adds about 1 to 2 MB. The Hit preview
  builds a fresh engine for each render (`md::engine::Engine`: 83.9 MB measured), and it would need one too.
- **CPU** [M]. A loop that calls 16 renders and the host's handshake costs 1,069 cycles, 1.25 µs a block. With 16
  renders of 3,800 MAC cycles, 62,045 cycles cost 45 µs (0.72 ns a cycle). The packages' code costs the same on
  either DSP, since it is the same JIT. A separate DSP adds about 1 to 5 µs a block. For comparison, the voice DSP's
  idle loop costs 5 µs.
- **Mixing.** `EngineT::render` gets the 16 raw blocks from `HostModel::renderBlock`, then hands each to `fxTrack`
  (`MdEngine.h:181-200`) [V]. A User Machine DSP's block would be copied into its Track's slot between the two, with
  the voice DSP's Track set to GND--. TrackFx, the Mixer and MD Drums' sample-accurate lanes stay as they are [I].
- **NEIGHBOUR.** A User Machine on Track t reads Track t-1's raw block of the same audio block. When Track t-1 is
  built-in, the voice DSP must have rendered first, so the two DSPs run one after the other. Splitting voice DSPs
  across threads already measured poorly: at best 2.5 points off the audio thread, for nearly twice the CPU (ticket
  25, `mdDrumsParallelTest`) [V].
- **What it buys.** Isolation: a faulty package cannot write the 1.63 program's memory. It must be given everything
  the voice DSP already has: the sine table, the external memory, scratch, its own warm-up, and its state in
  `saveState`. [I]

## Open

- Whether X.20 resolves import 6 to `$135600 + $600·t` and import 1 to `$148000`. 1.63's layout and X.20's own 8
  Machines point that way. X.20's 68k installer (`$2408ee`, research 01), which writes the import addresses, would
  settle it.
- X.20 holds at most 14,052 words of User Machine code, and the 16 largest of the 43 do not fit in it. Decide
  whether MoDule enforces that limit, so that a set made in MoDule also installs on a Machinedrum, or takes any 16.
- E12 Machines read `$1F85B5-$1FFFFF`, the top of the room the Samples map gives the user's Samples. A user Sample
  there could change an E12 sound. Not checked on X.20 or a Machinedrum: a point for the Samples map.
- INP-FA and INP-FB follow stale pointers into `$020000-$0FFFFF`, where User Machine code would go. After TRX-CH,
  -OH, -MA, EFM-CY or a P-I Machine, they would read code words as audio. On a Machinedrum they read whatever the
  unmapped bus gives. Not listened to.
- The emulator charges no wait states, so its cycles are optimistic against the hardware's (machinedrum-kit 16).
  That matters only for faithfulness to X.20's budget, not to MoDule, which has no deadline.
- Imports 2 to 5 would need the harness to feed Main into X:`$700-$7FF` and advance X:`$243`. None of the 43 asks
  for them.
- Host time of 16 User Machines among built-ins in one engine: not measured, it needs ticket 05's integration.
- Each emulated DSP spends 80.8 MB on audio rings that the harness does not use for audio, two per engine. A saving
  of about 160 MB per instance, outside this map.
