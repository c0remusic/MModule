# Decoding OS X.20 B, and where its 8 new built-in Machines live

Research for ticket 01 of the map "User Machines from X.20 in MoDule". Written 2026-10-17 against `main` at cb44f07;
nothing in the repository was built or changed, and no part of X.20, the zip or the flash image was copied into it.
Read-only probes ran on the user's `Machinedrum_SPS1-UW_OS_X.20_B.syx` (read from the zip into a scratch folder,
SHA-256 `dae8210a…81cc`) and on the user's OS 1.63 flash image, with Python scripts, Musashi's `m68kdasm.c` compiled
into a scratch tool, and this tree's `dsp56kDisassemble`. No firmware was run. Scripts and decoded images stay in the
session scratchpad (`x20scripts/`, `x20data/`).

Marks: **[V]** read at the place named; **[M]** measured by a script here (a CRC, a byte count, a diff); **[I]**
inferred from the facts around it.

## In short

- Section 0 is a two-stage package. Its first 7,090 bytes are an ordinary 1.63-style stream that unpacks to a
  12,448-byte ColdFire loader. The loader then reads four payloads from the rest of section 0 in flash: the voice DSP
  program, the mixer DSP program, the 68k OS (raw LZMA) and a DSP boot kernel. All four decode here and match their
  CRC-32s. [M]
- X.20's voice DSP program is OS 1.63's, word for word, except for 46 changed words and 4,807 added ones. The 8 new
  Machines' DSP code is 2,255 words at P:`$13b600-$13bece`, self-contained, reached through 1.63's dispatch
  scheme. [M]
- The 68k OS is a new build: about 1 % of 1.63's code survives byte for byte, so none of mdEngine's 1.63 addresses
  carry over. X.20 brings 1.63's data tables (the Machine table among them) as 31 CRC-checked ranges. The
  coefficient routines of the 8 are located and small (22 to 702 bytes each), but they read tables and per-Track
  state that X.20 builds at boot. [M] for the locations, [I] for the dependencies.
- User Machines run on the voice DSP (DSP2), in the same per-voice loop as built-in Machines. Their code must lie in
  P:`$13c108-$13f7eb`, and the OS writes their entries 195 to 210 of three extended dispatch tables. [M]
- Estimate: **weeks, not months**. About 3 to 5 weeks of sessions for the 8. The 3 GND Machines are the cheapest
  slice. The 3 NFX ones need the neighbour Track's audio. INP-CA and INP-CB need an audio input that MoDule does not
  feed. Bit-exact checks need ticket 02 (X.20 booted in mdLib).

## 1. The OS file

- 15,104 `$7E` data messages over flash `$4000-$F0000`, then two trailer messages (research 00) [V]. What lies in that
  range [M]:

| Flash | What |
|---|---|
| `$4000` | Section 0. Header: size 463,626, byte sum correct. A 7,090-byte stage-1 stream, 5,210 bytes described below, then the payloads `$7014-$75312`. |
| `$75312`, `$75329` | Sections 1 and 2. 15-byte streams that unpack to 12 bytes: two start markers for `$14ff00` and no record, which is an empty DSP program. |
| `$75340` | The tag `X20 `, where 1.63 has `163 `. |
| `$75344` | Section 3. 18 bytes that unpack to a 12-byte `SFS1` header (version 1, `0`, `$ffffffff`). |
| `$7535e` | Section 4. 502,938 bytes, byte sum correct. A 21-byte stream that unpacks to `SFS1` (version 1, `1`, `$fffffffe`), followed by raw resources up to `$F0000` (section 4). |

- mdfw's container walker needs at least 16 bytes per section (`Firmware.cpp:110`), so it stops at `$75312`, sees one
  section and refuses the file (`:159`). [V] for the code, [I] for the outcome: the C++ was not run.
- In 1.63, sections 3 and 4 are the factory patch-memory images (`loadPatchImageFromFlash`) [V]. X.20 shrinks them to
  12-byte `SFS1` stubs, so the bootloader's factory reset would write only a format marker, and the X.20 OS would fill
  patch memory from its own resources (section 4). [I]

## 2. Section 0: two stages

- **Stage 1 [M].** 1.63's packer, which `aplibDepack` decodes, unpacks the first 7,090 input bytes into 12,448
  bytes. The stream then ends with a gamma value of `$1000002`: `(g << 8) + byte = $1000002ff`, which is 767 modulo
  2^32, the end marker. So the 32-bit depacker, in C++ or in the bootloader's 68k code, stops cleanly there. Research
  00's "bad offset at 7,090" came from Python's unbounded integers.
- **The stub [M].** The output's first `$40` bytes copy `$c18` longs from `$200040` to `$2f8000` and jump there.
- **The loader [M].** 12,384 bytes at `$2f8000`, compiled C, main at `$2f965a`. It builds a CRC-32 table and checks
  the `S2MD` header that closes its own image (`$2fb000`):
  - version 7, size `$60`, CRC-32 of the header at `+$5c`;
  - the payload bounds, `$7014` to `$75312`;
  - four 16-byte entries: packed size, unpacked size, CRC-32 of the packed bytes, CRC-32 of the unpacked bytes.

  Payload 3 goes to RAM `$200000` (at most `$d0000` bytes). Payloads 1 and 2 go to `$600000` and `$500000`, which are
  DSP2's and DSP1's host ports (`mdLib/mdmemorymap.h:26-27`) [V]. Payload 4 is a DSP kernel at P:`$14f000`.
  - The loader and the kernel exchange 24-bit words that spell `DSU`, `RDY`, `CFD`, `ELD` and `EOK` [M]. A command
    protocol over HI08 [I].
- **The 5,210 bytes between the stages [M].** A 16-byte ASCII tag, a 366-byte stream that unpacks to 576 bytes (a
  bitmap, probably a boot logo [I]), then erased flash.

## 3. The four payloads

| # | Flash | Packed | Unpacked | Format | Destination | CRCs |
|---|---|---|---|---|---|---|
| 1 | `$7014` | 60,218 | 159,324 | `DSU` | voice DSP (DSP2, `$600000`) | packed CRC matches |
| 2 | `$15b50` | 17,434 | 64,440 | `DSU` | mixer DSP (DSP1, `$500000`) | packed CRC matches |
| 3 | `$19f6c` | 367,680 | 780,604 | raw LZMA1, lc 0, lp 0, pb 1 | 68k OS, `$200000-$2be93c` | both match |
| 4 | `$73bac` | 5,987 | 12,954 | 1.63-style section (size, byte sum, stream) | DSP kernel, P:`$14f000` and P:`$148000` | both match |

[M] for every row. Python's `lzma` module decodes payload 3 with no header. Of the 36 lc/lp/pb settings tried, only
lc 0, lp 0, pb 1 decodes. The kernel's records are P:`$14f000-$14fe8f` and P:`$148000` (555 words), both code.

**DSU** [M]. One container per DSP program:
- **Header.** The magic `DSU` and version 4, then a 30-byte header. As 24-bit little-endian fields at bytes 15, 18
  and 21: the word count (52,796 for the voice DSP), the output length in bytes and the size of the LZMA stream.
  Three CRC-32s follow, each split into a 24-bit and an 8-bit word. The second is the CRC of the LZMA output; the
  others matched nothing tried.
- **Record table.** Triples of 24-bit little-endian words: space (P 0, X 1, Y 2), address with a mode in bits 21-23,
  and count. 102 records for the voice DSP, 72 for the mixer.
- **Words.** Raw LZMA1 (lc 0, lp 0, pb 0) holds them in record order. Each record is byte-planed (all low bytes, then
  mid, then high), then summed back: once for mode 0, not for mode 1, twice for mode 2.
- **Check.** Decoded this way, 32,864 of the voice program's 32,910 P words that 1.63 also has are identical, and so
  are all X and Y words. Mode 5 (one record, Y:`$7a0`, 81 words) was not decoded.

## 4. Section 4's resources

All inside section 4's checksummed stream, at fixed flash addresses [M]:

| Flash | Name | What |
|---|---|---|
| `$7537a` | `SFW1` | Version 2. A 64-byte header, then two LZMA1 streams (lc 0, lp 0, pb 0), each after an 8-byte prefix whose first word is the stream's size. The first gives 524,288 bytes, the size of 1.63's patch-memory image, holding factory Kit names such as `TRX UW`: a factory patch-memory image, presumably in X.20's layout [I]. The second gives 69,174 bytes beginning `FDP1`. Both CRC-32s match the header. |
| `$874a0` | `SPSC` | Version 3. A SHA-256 of OS 1.63's unpacked image, then 31 entries (address, length, offset, stored length, CRC-32, packed flag). Each entry is a range of the 1.63 OS image, `$2462e8` to `$260e18`, 81,464 bytes in all. Every CRC equals that of the same range of the user's 1.63 image, and the 10 unpacked entries are byte-identical. Entry 9 is 1.63's Machine table (`$24ef54`, `$2d5a` bytes = 135 records of 86). The packer of the 21 packed entries was not identified; the 1.63 image gives their contents anyway. |
| `$8d752-$8ff44` | — | Small bitmaps and index tables, probably fonts and sprites [I]. |
| `$90000-$da320` | `E12B` | 303,904 bytes, the INTERNAL samples by the readme's "21 samples used by the E12 machines". Format not decoded. |
| to `$F0000` | — | Erased. |

## 5. The voice DSP program against 1.63

[M], from the decoded images.

- **Kept.** Every P, X and Y word that both programs hold is the same, except 46.
- **Dropped.** 201,804 P words of E12 sample data (P:`$103dba` onwards). The readme says INTERNAL samples now live in
  flash.
- **Changed (46 words).**
  - Two interrupt vectors: P:`$16` (IRQD, a host command) now goes to `$13feb2`, and P:`$1e` (DMA3) to `$13f9ec`.
  - The init call at P:`$3b` goes to `$13fea7` instead of `$100024`.
  - In the block loop, the machine fetch at P:`$81` goes through `$3e2`, which jumps to `$13ff3c` and decodes an id
    of 8 bits plus flags.
  - The three table bases at P:`$8a`, `$95` and `$a9`.
  - Each voice's output buffer: 1.63 alternates `$100`/`$120` at P:`$9b-$a7`; X.20 rotates a pointer at y:`$7f0`
    through a ring of 4.
  - A post-render call at P:`$b5` to `$3fa` → `$13ff88`.
  - 24 entries of 1.63's dispatch tables.
- **Added (4,807 P words, 81 Y words).**

| Region | Words |
|---|---|
| P:`$3e2-$3fd` | 28 |
| P:`$13b600-$13bece` | 2,255 |
| P:`$13f7ec-$13f9e8` | 509 |
| P:`$13f9ec-$13fa26` | 59 |
| P:`$13fea7-$13feae` | 8 |
| P:`$13feb2-$13ffff` | 334 |
| P:`$145fb1-$145ffd` | 77 |
| P:`$147600-$147c00` (mode 2, likely a table [I]) | 1,537 |
| Y:`$7a0-$7f0` | 81 |

- mdEngine's patch points `$24`, `$73`, `$a8`, `$aa`, `$d5`, `$e2` and `$100093` still hold what `VoiceEngine.cpp`
  expects, apart from the table base in the instruction at `$a8`. The point at `$b5` now holds the new post-render
  call. The harness's own copy of the render lookup hardcodes `$145c77` (`VoiceEngine.cpp`, kSkipNormal) [V].

## 6. The 8 Machines' DSP code

- **Dispatch, as in 1.63 [V].** The per-voice loop indexes three 193-word tables by the voice's machine code, which
  is the Machine id + 1 (`VoiceEngine.cpp` comments: code 1 = GND--). It calls the first table when the Machine
  changes, the second when the voice's trigger word is set, and the third every block, with r6 at the voice's memory
  (y:`$800` + 64 per voice) and r7 at a 32-word buffer (m7 = `$1f`). These are INIT, TRIG and RENDER, as MDS names
  them [I].
- **The changed entries [M].** X.20 changes exactly the entries for ids 1, 4, 5, 7, 8, 9, 86 and 87. 1.63 pointed 4,
  5, 7, 8, 9, 86 and 87 at its null routines (`$10008e`, `$10008f`), and id 1 at GND-SN's code. X.20 points them into
  `$13b600-$13bece`:

| Id | Machine | INIT | TRIG | RENDER |
|---|---|---|---|---|
| 1 | GND-SN-PRO (replaces GND-SN) | `$13b60f` | `$13b610` | `$13b622` |
| 4 | GND-SW | `$13b6c2` | `$13b6c3` | `$13b6d4` |
| 5 | GND-PU | `$13b969` | `$13b96a` | `$13b97f` |
| 7 | NFX-EV | `$13bb93` | `$13bb94` | `$13bba3` |
| 8 | NFX-CO | `$13bca7` | `$13bcaf` | `$13bcb0` |
| 9 | NFX-UC | `$13bd44` | `$13bd5d` | `$13bd5e` |
| 86, 87 | INP-CA, INP-CB (shared entries) | `$13be1b` | `$13be29` | `$13be2a` |

- **What the code depends on [M].**
  - 1,834 instructions with no call or jump outside the region.
  - The voice's own memory and internal X/Y scratch.
  - X:`$148000+`, a table that 1.63's own init builds (P:`$100069`) and its own Machines read.
  - y:`$7f0`, the new output-buffer ring: read by NFX-EV, NFX-CO and NFX-UC alone, which the "NEIGHBOUR" flag of
    research 00 suggests is the previous Track's audio [I].
- **INP Machines.** INP-CA and INP-CB read the codec input. mdEngine feeds silence there (`VoiceEngine.cpp`, ESSI
  callbacks) [V].

## 7. User Machine dispatch

- **Tables [M].** X.20's DSP init (P:`$145fb1`, reached from P:`$3b` through `$13fea7`) copies 1.63's three tables
  (`$145af5`, `$145bb6`, `$145c77`, 193 words each) to `$145d38`, `$145e0b` and `$145ede`, 211 words each. It fills
  entries 193 to 210 with the null routines; the block loop then uses only the new tables.
- **Install [M].** The OS's installer (`$2408ee-$2409e0`) takes a slot from 0 to 15. It refuses any INIT, TRIG or
  RENDER address outside P:`$13c108-$13f7eb`, a shared program area of 14,052 words, then writes entry `$c3` + slot
  (195-210, machine codes for ids 194-209, USR-01 to USR-16) of each table. The routine before it bounds the code's
  end at `$13f7ec`. The code itself probably goes in by a host command: the IRQD handler at `$13feb2` reads address
  and count words from the host port [I].
- **Services [I].** P:`$13f7ec` onwards (the code at `$145fda` checks y:`$147fff` and the UW sample area at
  `$150000`/`$180000`) is probably where the MDS services and imports live.
- **So [M]/[I].** User Machines run on the voice DSP, as research 00 inferred. X.20's voice program already holds
  their host: the tables, the code area, the loader and probably the services. That bears on ticket 05: MoDule could
  run X.20's voice program from the user's `.syx` instead of writing a host. The relocation and import patching stay
  68k code of X.20 [I].

## 8. The mixer DSP program

- Much more of it changed than of the voice program [M]:
  - 729 of the 4,547 words that both hold outside P:`$140000` (main code P:`$0-$9e1`).
  - The 13,942-word block at P:`$140000`, which matches 1.63 under no transform tried.
  - 2,487 new P words (P:`$a08-$ffb`, `$13ed00-$13f292`) and 128 new X words (X:`$13ff80`).
- The 8 Machines do not need it: none of their code is there [M]. X.20's other engine features (INTERPOLATE, Kit
  output routing) probably are [I]. mdEngine's MasterEngine addresses (P:`$342-$970`) fall in the changed range: not
  checked word by word.

## 9. The 68k OS: Machine table and coefficient routines

- **A new build [M].** 267 of 24,370 non-empty 16-byte chunks of 1.63's OS appear in X.20's. Neither 1.63's
  coefficient routines nor `TRXB2PTCH` is there. X.20's entry writes the ColdFire's MBAR (`$300001`) and probes
  the chip selects; 1.63's entry does neither, and copies its fast routines into the SRAM at `$1000088`. So
  `MachineRunner`'s memory map would need checking for X.20 too.
- **New Machine table [M].** 8 records of 60 bytes at `$2b31a6`: id, 5-character name and NUL, 8 parameter names of 5
  bytes, 8 defaults, one byte, 4 bytes of nibbles (one per parameter, a display type [I]). Parameter names:
  - GND-SN-PRO: PTC1 DEC RAMP RDEC PTC2 PTC3 PTC4 UNIS. Its defaults begin with 73 and 96, as GND-SN's do.
  - GND-SW: PTC1 DEC RAMP RDEC PTC2 PTC3 SKEW UNIS.
  - GND-PU: PTC1 DEC RAMP RDEC PTC2 PTC3 WIDT UNIS.
  - NFX-EV: DEL ATK DEC SUS HOLD REL RING NBAL.
  - NFX-CO: ATK REL THRE RTIO KNEE SIDE MKUP NBAL.
  - NFX-UC: TIM1 TIM2 TD TI FF FB DMIX NBAL.
  - INP-CA and INP-CB: ATK REL THRE RTIO KNEE SIDE MKUP IVOL.
- **The table at boot [M].** The builder at `$2aba40-$2abe24` reads 1.63's table through the SPSC range `$24ef54`. For
  each 1.63 record it takes the new record of the same id if there is one: GND-SN gives way to GND-SN-PRO. It then
  appends the 7 new ids, and fills a RAM map from id to routine index (`$2cf1ec`).
- **Routines [M].** `$217680` maps an id to an index:
  - ROM-01 to ROM-24 → 1;
  - ROM-25 to ROM-48 → 2;
  - USR-01 to USR-16 → 3 (`$21aeba`, all 16);
  - other ids → their position in the byte list `$2b0b24`.

  The routine is then `$2b0d04 + 4 × index` (75 entries):

| Machine | Routine | Bytes, own / with callees | Callees, tables |
|---|---|---|---|
| GND-SN-PRO | `$21ada2` | 280 / 396 | pitch `$2189b0`, `$21822a`, RAM `$2cdb00` |
| GND-SW | `$21acbc` | 230 / 346 | the same |
| GND-PU | `$21abd2` | 234 / 350 | the same |
| NFX-EV | `$2197d2` | 292 | RAM `$2cdc6c` |
| NFX-CO | `$2196c0` | 274 | `$2bd636`, `$2bd836` (data in the image) |
| NFX-UC | `$219402` | 702 | — |
| INP-CA | `$2193ec` | 22, then `$2192d6` | the same as NFX-CO |
| INP-CB | `$2193d2` | 26, then `$2192d6` | the same as NFX-CO |

- **Calling convention [M].** `fn(out, params)` returning a word count in D0, as mdEngine's `MachineRunner::compute`
  calls 1.63's. But `out` is a larger per-Track context: GND-SN-PRO reads `+$8c`, and the helper `$2189c6` follows
  `+$90` then `+$24`. The pitch helpers index tables at `$2cdb00`, beyond the image's end (`$2be93c`), which the OS
  computes at boot [I], perhaps from TUNING and the tonal octave settings [I].
- **Transport [I].** That X.20 delivers these words to the voice DSP as 1.63 does, which `HostModel` reproduces, is
  assumed: the voice program's transfer code is unchanged.

## 10. Estimate

In working days of sessions, for the 8 Machines in MoDule, the user's `.syx` read at run time and nothing
redistributed:

| Work | Days |
|---|---|
| A C++ reader in mdfw: stage-1 end marker, `S2MD`, an LZMA1 decoder (the LZMA SDK's decoder is public domain), `DSU`, finders by pattern rather than X.20 B's addresses | 1-2 |
| Voice DSP: place the 2,255 words and patch the 24 entries into the 1.63 program MoDule runs (or run X.20's program whole, its E12 data from 1.63's flash); the harness's hardcoded table base; y:`$7f0` and the neighbour's buffer for NFX; warm-up | 2-4 |
| 68k: run X.20's routines in a second Musashi with the boot tables, or port the ~2.3 KB of 68k to C++ | 4-8 |
| Plumbing: `mdmachines` (mark them present), parameter names and defaults, browser, Kit dumps, Hit preview, GND-SN-PRO in place of GND-SN for id 1 | 2-3 |
| Checks against X.20 booted in mdLib (ticket 02), Machine by Machine as `mdEngineFirmwareTest` does | 3-5 |
| **Total** | **12-22 days, about 3 to 5 weeks** |

- Cheapest first: GND-SN-PRO, GND-SW and GND-PU have no neighbour and no input (about 1.5 to 2 weeks with the tooling).
  NFX-EV, NFX-CO and NFX-UC add the neighbour buffer (+3 to 5 days). INP-CA and INP-CB play silence until MoDule feeds
  an audio input, which no ticket plans.
- If X.20 does not boot in mdLib, the Machines can still play, but nothing measures them against X.20.

## Open

- What the boot leaves for the coefficient routines: the RAM tables at `$2cdb00` and `$2cdc6c`, the per-Track context
  at `+$8c`/`+$90`, and which X.20 boot routines build them. A dump from X.20 booted in mdLib would answer it, as
  `kBootSram` did for 1.63.
- Whether the new Machines rely on X.20's block-loop hooks (the machine fetch at `$13ff3c`, the post-render call at
  `$13ff88`, the DMA3 and IRQD handlers) beyond y:`$7f0`. Their purpose (PRECISE timing, ANTI-CLICK, extended ids) is
  inferred, not read.
- The y:`$7a0-$7f0` ring: layout and which voice's buffer an NFX Machine sees. DSU mode 5 (its initial words) was not
  decoded.
- The mixer DSP's changes: their purpose, and the P:`$140000` block that no transform matched.
- DSU's first and third CRCs, the payloads' second CRC for DSU, the SPSC packer, the `E12B` and `FDP1` formats, and
  the remaining `SFW1` header fields.
- The loader's own word protocol with the DSP kernel, inferred from the reply words alone.
- Every address here is X.20 B's: a later X.20 will move them, so tools must find them by shape (the 60-byte records
  by their names, the DSP code by its diff from 1.63's tables).
- Whether X.20 boots in mdLib (ticket 02) at all.
