# X.20's ANTI-CLICK: where it lives, what the fade does, and what MoDule would need

Research for the map's "Not yet specified" line on the ANTI-CLICK fade ("Le fondu ANTI-CLICK qu'X.20 fait avant TRIG").
Written 2026-10-20 against `main` at a5df5cf, whose working tree held an uncommitted change to
`mdDrumsPlugin/mdDrumsSkinTest.cpp` (the drop test, committed since). Nothing in the repository was built, run or changed apart from this note. No byte
or code of X.20, the zip or the flash image was copied into it: addresses, counts and behaviour only. X.20 was not
booted (research/02: it stops on the ColdFire's MAC unit).

Sources:

- **X.20 B's readme**, in the user's zip (research/00), "New engine settings" and the CC table.
- **X.20 B's 68k OS**, as research/01 decoded and disassembled it (`x20data/x20_os.bin`, `x20_os.asm`, session
  scratchpad). Its ENGINE menu strings were read by `anticlick/strings.py`, its pointers by `anticlick/find_ptr.py`.
- **X.20 B's voice DSP program** (DSU payload 1), as research/01 and /12 left it (`x20data/v_x20_low.asm`,
  `v_x20_hi.asm`).
- **X.20 B's mixer DSP program** (DSU payload 2). Decoded here by research/01's `dsu.py` and disassembled with this
  tree's `dsp56kDisassemble` (md-drums' Release build, not rebuilt): `anticlick/mixer_dump.py` writes P:`$0-$A07`,
  P:`$A08-$FFF` and P:`$13ED00-$13F2FF` of X.20 and of OS 1.63 into `anticlick/mixer/`; `mixer_tables.py` prints its
  data records.
- **The table of continuous built-ins**, `$2b371a`, read again by research/11's `builtin_continuous.py` and by
  `anticlick/faded_builtins.py`, which names the ids from MoDule's `mdProtocol/mdmachines.cpp`.
- **MoDule's code**, at the places named.

Marks: **[V]** read at the place named; **[M]** measured or decoded by a script here; **[I]** inferred from the facts
around it. Addresses without a program are X.20 B's 68k OS; P:, X: and Y: name a DSP, which the text says. Every
X.20 address is X.20 B's.

## In short

- **Where it lives [V].** Global CONTROL > ENGINE, row 3 of 5 (TUNING, INTERPOLATE, ANTI-CLICK, TIMING, PTC DISP).
  - One value for the whole machine: a long in the OS's state at `$2d9804` (`$2d7348` + `$24bc`), taking 0, `$40` or
    `$80`. The menu shows them as "OFF (LEGACY)", "64" and "128".
  - Saved in the **workspace** record ("WKSP", version 15, byte `+$31`), which X.20 keeps in its own storage format
    and, by inference, in +Drive snapshots. It is in neither the Kit, nor a Global slot, nor any SysEx dump.
- **Default: 128 samples [V]/[I].** The boot sets 128 (`$2ac65c`) before it applies a saved workspace, so a factory
  reset or a first boot without an X.20 workspace gives 128. The other engine settings default to TIMING PRECISE,
  INTERPOLATE ON, TUNING +0 and PTC DISP NOTE.
- **What the fade does [V].** Two DSPs share the work, and the 68k only decides:
  - **Voice DSP: the new Hit waits.** X.20's fetch hook (P:`$81` → `$3E2` → `$13FF3C`) holds back TRIG, and the
    Hit's parameter words, for 2 blocks (64) or 4 blocks (128). The old voice renders on with its old words. A Hit
    that arrives during the wait replaces the waiting one without restarting the count. Its PRECISE offset is kept.
  - **Mixer DSP: the old sound fades.** A hook X.20 installs at the start of each Track's mixer processing (P:`$A4` →
    `$CD4`) multiplies the Track's raw voice block by a linear ramp: gain `$7FFFFF − i·$FFFF` (128) or
    `− i·$1FFFF` (64), so 1 − i/128 or 1 − i/64. It reaches zero on the last sample of the wait, before the Track's
    filter, EQ and other effects.
  - **The result.** The old sound fades linearly to zero over 64 or 128 samples, then the new Hit starts at full
    level, 64 or 128 samples later than it would without the fade.
- **When [V].** On every trigger while the setting is on, whether or not the Track still sounds, unless the Track's
  Machine is continuous:
  - 20 built-ins never fade: NFX-EV, -CO, -UC, TRX-BD, -XT, -CB, -XC, -B2, P-I-BD, -RC, -CC, -HH and the 8 INP.
  - Every other Machine fades: the rest of GND, TRX, EFM, E12 and P-I, every ROM and RAM Machine (MID and CTR too,
    which are silent).
  - User Machines fade unless they set CONTINUOUS RETRIGGER. That leaves 12 of the 43: the 5 TXZ, P-I-ST, SYN-VP,
    TX8-HH, and TX9-BD, -RS, -SD and -XT.
  - A continuous Machine is faded after all when the Track's X.20 envelope would restart from zero (two per-Track
    words, [I] its ATTK and EVMIX). MoDule has no such envelope.
- **A cost X.20 pays [I].** Nothing found holds back the Hits that are not faded. So, at X.20's default, a TRX-SD
  sounds 128 samples (2.9 ms) after a TRX-BD struck with it. This should be checked on X.20 before it is copied.
- **MoDule today [V].** No fade, no wait: every Machine retriggers as under X.20 with ANTI-CLICK OFF.
- **What MoDule would need [I].** Everything in C++; no voice DSP patch.
  - HostModel holds a faded Hit's slot for 2 or 4 blocks.
  - EngineT multiplies the Track's raw voice block by the ramp before `fxTrack`.
  - mdDrumsEngine applies the Hit's delay when the held Hit fires, not at its note.
  - The Hit latency then grows by 64 or 128 samples, for faded Hits only (as X.20 does), or for every Hit if MoDule
    keeps its Tracks aligned.
  - About 2 to 3 days with tests; negligible CPU.
- **Decision for the user:** do nothing (OFF), build it fixed at 128, or build it with a setting. See the end.

## 1. The setting

### 1.1 The menu and the value [V]

- **The labels.** `$2b2340` points to TUNING, INTERPOLATE, ANTI-CLICK, TIMING and PTC DISP, in that order. The page
  descriptor at `$2af8d8` names the draw routine `$24e810` and, beside it, `$239614`, which reads the same five
  values (probably to tell when the page needs redrawing [I]).
- **The draw**, `$24e810`. Row 2 reads the long at `$2d9804`: 0 shows "OFF (LEGACY)" (`$2adebd`), any other value
  "%d" (`$2adae7`).
  - The same "OFF (LEGACY)" serves INTERPOLATE's 0.
  - TIMING shows "PRECISE" or "LEGACY".
- **The edit**, `$23133a`.
  - The current value becomes an index: 0, 1 for `$40`, 2 for `$80`. The encoder moves it by one, clamped to 0-2.
  - The new value comes from the table at `$2b2354`, which holds 0, `$40` and `$80` [M].
  - While the engine runs (`$2de543`), the value goes out as a message through `$214b68`. That routine accepts only
    0, `$40` and `$80`; its handler writes the engine at `$25f28c`.
  - Otherwise the value goes straight into `$2d9804`.
- **The five values** sit in the OS's state at `$2d7348`. The menu's absolute addresses are that base plus the
  offsets the boot and the workspace code use:

| State offset | Absolute | Setting | Engine field (`$213d68`) |
|---|---|---|---|
| `+$24ba` byte | `$2d9802` | TIMING, nonzero = PRECISE | `+$295e` |
| `+$24bb` byte | `$2d9803` | PTC DISP, 1 = VAL | — |
| `+$24bc` long | `$2d9804` | ANTI-CLICK: 0, `$40`, `$80` | `+$2960` |
| `+$24c0` long | `$2d9808` | INTERPOLATE, nonzero = ON | `+$2964` (and `+$2968`, "changed") |
| `+$24c4` byte | `$2d980c` | TUNING, −50 to +50 cents, −128 = MDX A=433 Hz | — |

- **To the engine.** `$213d68(engine, TIMING, ANTI-CLICK, INTERPOLATE)` is called at boot (`$2ac8b0`) with the state's
  values.
  - It writes `+$2960` only for 0, `$40` or `$80` (`$213d82-$213d9e`); any other value leaves the engine as it was.
  - The tick reads `+$2960` on each trigger, so a change applies from the next Hit.
- **Exhaustive [V].** These are all the readers found of `+$24bc`, `+$24bf` (its low byte) and `$2d9804`, by
  A-register offset and by absolute address:
  - the boot default, the workspace load and save, the engine call;
  - the menu draw, edit and redraw check.

  No Kit, Global or SysEx code reads them. Computed addresses would escape this search.

### 1.2 Where it is saved [V]

- **One workspace, not eight Globals.**
  - The 8 Global slots are `$bc`-byte records below `+$5e4` of the state (loops at `$2ac582-$2ac5aa`).
  - The engine settings live far beyond them, at `+$24ba-$24c5`.
  - "Global CONTROL > ENGINE" is a menu path; the value is machine-wide.
- **The record.** `$22eb4a` builds it at `$2b5bd6`: 650 bytes, cleared, magic "WKSP" (`$574b5350`), version 15.
  - It copies the state: TIMING into `+$30`, ANTI-CLICK's low byte into `+$31` (`$22ec20-$22ec24`), and INTERPOLATE
    into `+$32` as 0 for ON and 2 for OFF.
  - `$20c76e` writes it into X.20's storage format ("SXWS"). The 4 bytes `+$30-$33` land at file offset `$6a`.
  - `$263c02` reads it back. It accepts the record only as WKSP version 15 (`$263d4e-$263d66`).
- **Loading.** `$26418a` copies the record into the state.
  - `+$31` goes to ANTI-CLICK as it is (`$264220-$264224`), unchecked here; `$213d68` checks it later.
  - `+$32` of 0 or 1 gives INTERPOLATE ON, anything else OFF.
- **Two callers.**
  - Boot, `$2ac6d4`, when a workspace was restored (`$2ac40a-$2ac418` sets A4 to `$2b5bd6` or 0).
  - `$29814c`, after a load that fills `$2b5bd6` again: a +Drive snapshot, by its company [I].
- **Kits and dumps.** The Kit (65.1) and the Global dump do not carry it, by the search of §1.1. The readme lists
  only pattern tempo and channel mode as the Global dump's additions.

### 1.3 The default [V]/[I]

- **The boot.** `$2aa3f4`, the boot routine, writes the engine defaults at `$2ac64e-$2ac666`. Only afterwards, at
  `$2ac6cc`, does it apply a restored workspace:
  - `+$24ba` ← word `$0100`: TIMING 1 (PRECISE), PTC DISP 0 (NOTE);
  - `+$24bc` ← `$80`: **ANTI-CLICK 128**;
  - `+$24c0` ← 1: INTERPOLATE ON;
  - `+$24c4` ← 0: TUNING +0, A = 440 Hz.
- **So [I].**
  - A factory reset erases storage, and an X.20 with no workspace of its own boots with 128.
  - Any workspace X.20 writes is built from the state (`$22eb4a`), which starts at 128. So a workspace written on the
    first boot after an upgrade also carries 128, unless the user changed it.
  - Whether the first boot converts an older OS's workspace was not read. Nothing found writes the field otherwise.

## 2. What the fade does

### 2.1 The decision, on the 68k [V]

- **The fade code.** The tick's trigger routine (`$2a596e`, called from the tick `$2a5164`) stores it at `$1000eac` +
  6, in the SRAM (`$2a59f8-$2a5a12`): 1 for `$40`, 2 for `$80`, 0 otherwise.
- **The decision** (`$2a5d4c-$2a5dcc`, research/11 §3.2). D is that code, except 0:
  - for an id up to `$5f` whose byte in `$2b371a` is set;
  - for a USR id whose CONTINUOUS RETRIGGER bit is set.

  The Track's control byte then gets the trigger bit 1 and D in bits 4-5.
  - Every other id takes D: the ids `$60-$c1` (MID, CTR, ROM, RAM) and those past USR-16.
  - The id is the Track's Machine at the trigger. When a Hit changes the Machine, the new Machine's flag decides
    whether the old sound fades [I].
- **No "is it sounding" test.** Neither the decision nor the voice DSP asks whether the old voice still sounds. A Hit
  on a Track that has died away, or on a fresh Machine, waits too.
- **The continuous exception** (research/11's "not identified"). A continuous Track is still faded when all three hold:
  byte +6 of its 8-byte trigger record is clear, word 27 ≥ `$3f7d`, word 24 ≥ `$7d`.
  - [I]: words 24-27 of the Track's 38 are X.20's per-Track envelope: ATTK, HOLD, DEC, EVMIX.
    - The readme's CC table lists the envelope in that order.
    - The block builder (`$200a8a-$200ba0`) reads the four words as a rate table, a squared value ("fine control of
      short holds"), a rate table, and a linear mix where 127 gives `$7FFFFF`.
  - So the exception reads: EVMIX at 127 and an attack of at least 1. The envelope would then restart from zero, a
    jump the fade hides.
  - Byte +6 also sets the control byte's bit 2. It was not identified; perhaps a retrig repeat (RTRG, RENV) [I].
- **To the voice DSP** (`$27d170`, host command `$FFFFFE`, one word per triggered Track, `$27d248-$27d282`):
  - the Track in bits 16-19;
  - the type (id + 1) in bits 8-15;
  - D in bits 5-6;
  - the PRECISE offset in bits 0-4, under TIMING PRECISE only;
  - the number of parameter words in bits 20-23, with bit 7 as its fifth bit.
- **The fade length in samples.** `$206e76-$206e8c` also copies the setting, as 0, 64 or 128, into the block record at
  `+$1e`. No reader of it was found.

### 2.2 The voice DSP: the new Hit waits [V]

`v_x20_hi.asm`. Per Track t there is a 16-word X record at X:`$13FB80` + 16·t.

| Word | What |
|---|---|
| +0 | Blocks left to wait |
| +1 | The trigger word |
| +2 | Its number of parameter words |
| +3 | Flags of the Hit being started |
| +4 | Frame parity |
| +D | PRECISE carry length |
| +E | Waiting type |
| +F | Its flags |

- **The host command** (IRQD, P:`$13FEB2`, the `$FFFFFE` path `$13FEE6-$13FF0D`). It files each trigger word in its
  Track's record: +1 the word, +2 its count.
- **The fetch** (P:`$81` → `$3E2` → `$13FF3C`, decoding at `$3E4-$3F9`): the type is bits 8-15 and the flags bits
  0-6.
  - **A Hit with D ≠ 0, nothing waiting** (`$13FF53-$13FF5A`, `$13FF5B-$13FF63`). The count becomes D·2 (`$20` → 2,
    `$40` → 4), the type and flags are filed in +E and +F, and slot word 0 is cleared. No TRIG this block.
  - **A Hit while a count runs** (`$13FF4D-$13FF4F` → `$13FF5B`). It replaces the waiting one. The count goes on.
  - **A Hit when the count has reached 0** (`$13FF50-$13FF52` → `$13FF6B`). It replaces the waiting one and fires at
    once.
  - **No Hit, count 0, one waiting** (`$13FF64-$13FF6A` → `$13FF6F`). The waiting Hit fires:
    - its parameter words, held at Y:`$13FA40` + 20·t, are copied into Y:(R6+1…) (`$13FF28`);
    - slot word 0 gets its type, and its offset becomes +3;
    - the loop's INIT and TRIG then run as for any Hit (P:`$82-$98`).
- **The post-render** (P:`$B5` → `$3FA` → `$13FF88`). It counts the wait down once per block (`$13FF95`), after the
  PRECISE shift (`$13FF9F`).
  - So a Hit filed in block N fires in block N+2 (64) or N+4 (128).
  - Blocks N to N+1 (or N+3) render the old sound with RENDER as usual.
- **The old voice's words.** While a Track waits, the slot DMA handler (P:`$13F9EC-$13FA14`) diverts the host's DMA
  for that slot (destination Y:`$800` + 64·t) to Y:`$13FA2B` and raises HF2.
  - So the old voice keeps the words it had [I].
  - How the new Hit's words reach Y:`$13FA40` + 20·t was not traced.
- **No gain.** Nothing in X.20's voice hooks multiplies a sample. The voice DSP only delays the Hit.

### 2.3 The mixer DSP: the old sound fades [V]

`anticlick/mixer/mx_000a08.asm`, `mx_13ed00.asm`, `mx_000000.asm`.

- **The hook.**
  - X.20's mixer writes the one-word instruction `jmp $CD4` over P:`$A4` at its init (`$13F014-$13F016`) and again at
    `$13F120-$13F122`.
  - P:`$A4` is where the per-Track loop starts a Track's processing. The image there still holds OS 1.63's
    `lua (r6+$22),r4`.
  - `$CD4` returns through `$D53-$D5B` to P:`$A6`, the start of the Track's chain. That chain is what MoDule's TrackFx
    ports from 1.63 [I].
  - At the hook, R7 points to the Track's 32 raw samples from the voice DSP (P:`$7F-$8C`), in Y.
- **Per-Track state** (t = 1-16, from R1):
  - the command the 68k sends, X:`$13F7FF` + t (X:`$6E1` = `$13F7FF`, set at `$13F010-$13F012`);
  - the state word, X:`$7DF` + t;
  - the fade gain, X:`$7EF` + t;
  - X.20's envelope state, X:`$758` + 8·t.
- **A fade starts** (`$CF0-$CFE`) when the state has bit 21 or 22 set and bit 23 clear. The gain becomes `$7FFFFF`
  (1.0) and bit 23 marks the fade.
- **Each block of the fade** (`$CFF-$D06`, then `$D1E-$D25`). The envelope runs first (`$D5C`), then the ramp
  (`$D88-$DA0`) multiplies the 32 samples in place:
  - `out[i] = in[i] · (g − i·s)`: `mpy`, so truncated to the high word, not rounded;
  - g is the block's starting gain;
  - s is `$1FFFF` when bit 21 is set, else `$FFFF`.

  After the block g has fallen by 32·s, and it is cleared once below `$10000`.

| Setting | Step s | Gain at sample 0, 31, 63, 127 | Blocks |
|---|---|---|---|
| 64 | `$1FFFF` (1/64) | 1.0, 0.516, 0.0156, — | 2: the gain after the second is `$3F`, cleared |
| 128 | `$FFFF` (1/128) | 1.0, 0.758, 0.508, 0.0078 | 4: after the fourth it is `$7F`, cleared |

  Computed from the step [M], with the code's rule [V].
- **The end** (`$D07-$D0D`). With the gain at 0, the next block clears the fade from the state word. No ramp is
  applied; the block passes as it is.
- **A command during a fade** (P:`$EF-$F6`). A new command's fade bits are masked off (`$9FFFFF`), and the running
  fade's bits 21-23 and 14 are kept. So, as on the voice DSP, a retrigger does not restart the fade.
- **The new Hit's block** (`$D26-$D38`). When the command that starts the waiting Hit carries bit 5 (a start at an
  offset, bits 0-4) and bit 6, while bit 23 still stands, the samples before the offset are cleared. Then the envelope
  starts at the offset (`$F1C`).
  - So under PRECISE, the old sound's tail that the voice DSP's shift carried into that block is cut [V for the code];
  - that the 68k sends bit 6 for a faded Hit is [I].
- **Which bit means what [I].** The 68k's command builder was not found.
  - The step test (bit 21 → 1/64, otherwise 1/128) and the fade codes (1 = 64, 2 = 128) agree with bit 21 for 64 and
    bit 22 for 128.
  - That the 68k sends the fade command in the block whose Hit the voice DSP holds back is inferred from the lengths
    matching: 2 or 4 blocks on both DSPs.

### 2.4 Put together [V]/[I]

For a Hit at offset o in block N, on a Track whose Machine is not continuous, with ANTI-CLICK at 128:

| Block | Voice DSP | Mixer DSP, on the Track's raw block |
|---|---|---|
| N | No TRIG; the old sound renders | Gain 1 − i/128, i = 0-31 |
| N+1, N+2, N+3 | The same; the count runs down | i = 32-127; zero at the end of N+3 |
| N+4 | TRIG, with the Hit's words and offset o | No ramp; the samples before o cleared (PRECISE) |

- With 64, the fade lasts N to N+1 and TRIG comes at N+2.
- **The Hit sounds 128 (or 64) samples later** than without ANTI-CLICK, at the same offset in its block.
- **Hits closer than the wait merge.** The latest one fires at the end, at the first one's timing.
- **The fade is applied before the Track's filter, EQ, distortion and the rest.** A resonant filter therefore rings
  through it as it would through any level change.

## 3. Which Machines it applies to in X.20 [V]/[M]

The rule of §2.1 over MoDule's names [M] (`faded_builtins.py`):

| Group | Faded | Not faded (continuous) |
|---|---|---|
| GND | GND---, -SN (X.20's GND-SN-PRO), -NS, -IM, -SW, -PU | — |
| TRX | SD, CP, RS, CH, OH, CY, MA, CL, S2 | BD, XT, CB, XC, B2 |
| EFM | all 8 | — |
| E12 | all 16 | — |
| P-I | SD, MT, ML, MA, RS | BD, RC, CC, HH |
| NFX, INP (X.20's) | — | NFX-EV, -CO, -UC; INP-GA to -CB, all 8 |
| Ids from `$60`: MID, CTR, ROM-01 to -48, RAM-R/P | all (MID and CTR are silent voices: only the wait) | — |
| User Machines (the 43 of X.20 B's zip) | 12 without CONTINUOUS RETRIGGER: TXZ-EP, -LB, -LD, -PD, -ST, P-I-ST, SYN-VP, TX8-HH, TX9-BD, -RS, -SD, -XT | 31 with it (research/11 §2) |

- **The names [M].** The table also lists ids 6 and 11-15 among the faded ones, under names (DPRO-WAVE, FX-…) that
  belong to no UW Machine. They are left out above.
- **X.20's continuous built-ins** are its effects and inputs, and the TRX voices whose TRIG does not restart an
  oscillator's phase [I]. Four of the five are the voices MoDule measures as starting late (`voiceStart`,
  `mdDrumsEngine.cpp:516-539`); B2 has no entry there.
- **Under a Track envelope** that is fully mixed with an attack, even the continuous ones fade (§2.1). MoDule has no
  such envelope, so this case never arises there.

## 4. What MoDule would need

### 4.1 Where each piece goes [I]

MoDule replaced the mixer DSP with C++ (`EngineT::render`, `MdEngine.h:190-217`) and sends each Hit's slot from
HostModel (`HostModel.cpp:370-419`, `498-530`). So the whole fade can be C++, with no patch of the voice program:

| # | Piece | Where | What |
|---|---|---|---|
| 1 | The setting | Device / Processor state or config | OFF, 64 or 128. X.20's is machine-wide (the workspace), not in the Kit |
| 2 | The decision | `HostModel::trigger` / `updateVoice` (`HostModel.cpp:319-419`) | Fade unless OFF, or the id is one of X.20's 20, or a User Machine whose package sets `FlagContinuousRetrigger` (`MdsPackage.h:44`) |
| 3 | The wait | HostModel | Keep the Hit's slot (word 0 = id + 1 and its words, `setSlot`) for 2 or 4 blocks, writing nothing to the Track's slot meanwhile, as X.20 diverts its DMA. The latest Hit replaces the waiting one; the count goes on. Links, Locks and the Choke step follow the Hit when it fires |
| 4 | The ramp | `EngineT::render`, between `m_host->renderBlock` and `fxTrack` (`MdEngine.h:193-204`) | `m_voiceOut[t][i]` × (g − i·s) in Q23, `mpy` truncated as X.20's mixer does (`d56::mpy`); g cleared below `$10000` at a block end. `fxTrack`'s settled-silence skip stays exact: zeros stay zeros |
| 5 | Hit timing | `Engine::trigger` (`mdDrumsEngine.cpp:498-514`), `renderBlock` (`:97-127`) | `pending[t]` must take effect in the block where the held Hit fires, not the note's. Otherwise the old sound's last blocks change delay in the lane mid-fade. The Choke cut (`:129-147`) moves with the cutting Hit |
| 6 | Latency | `HitDelay` (`mdDrumsEngine.h:148-170`) | See §4.2 |
| 7 | Edges | Hit preview (`mdDrumsHitPreview.*`), audition, the Telemetry's Hit capture | The preview strikes a fresh engine with nothing to fade: skip the wait there. The capture starts where the held Hit sounds |

- **No PRECISE cut needed.** MoDule's voice DSP starts every Hit on a block boundary and the delays live in the C++
  lanes. So step 4 needs nothing like X.20's clearing of samples before the offset (§2.3): the old sound is zero at
  the end of the last faded block.
- **Cost.**
  - CPU: 32 multiplies per faded Track per block, for 2 or 4 blocks per Hit. Negligible.
  - Memory: per Track, one held slot (up to 32 words) and a gain.
  - Effort: about 2 to 3 days with the tests of §4.3, more if latency is compensated (§4.2).

### 4.2 Against MoDule's sample-accurate Hits

- **Today.** Every Hit sounds `HitDelay` = 31 + 36 + 16 = 83 samples (1.88 ms at 44.1 kHz) after its note, with no
  jitter, on Main and on its Out (CLAUDE.md, ticket 36).
- **With the fade, a faded Hit can only start once the old sound has faded.** Three ways:
  - **As X.20 [I].** Faded Hits sound 64 or 128 samples later than the rest: 147 or 211 samples. A TRX-SD and a
    TRX-BD struck together then land up to 128 samples apart. Latency is still reported as 83, and the Hits are late
    by Machine. This breaks MoDule's "every Hit `HitDelay` after its note".
  - **Aligned.** While the setting is on, hold every Hit not faded back by the fade length too, as `AlignedStart`
    already holds the quicker voices.
    - `HitDelay` becomes 147 or 211 samples (3.3 or 4.8 ms), reported to the host.
    - `State::Ring` (`mdDrumsEngine.cpp:62-64`, 128 today) must grow to 256.
    - With the setting OFF nothing changes.
  - **Look-ahead.** Fade the old sound's samples still waiting in the lanes, before the new Hit's start. The lanes
    hold at most 31 + 36 = 67 samples ahead, so this can never give 128, and gives 64 only for some Hits. Rejected.
- **The CLAUDE.md latency rule** asks that a trig-path change report latency and jitter before and after. Under "as
  X.20", the jitter between Tracks becomes 0, 64 or 128 samples by Machine.

### 4.3 How a test could check it

- **In the gate, against MoDule itself [I]:**
  - **OFF is bit-identical to today.** Every existing engine, device and firmware test unchanged, including
    `mdEngineFirmwareTest` and the Hit-latency checks.
  - **On.** TRX-SD struck, then struck again while sounding. The Track's raw blocks (`voiceBlock()`) during the wait
    equal today's undelayed old voice × the ramp, sample for sample. The new Hit's blocks equal today's, 2 or 4
    blocks later.
  - **Continuous Machines unchanged.** TRX-BD, the saw test machine (`mdDrumsTestMachine.h`) with
    CONTINUOUS RETRIGGER set, the same saw without it (faded), and a Hit on a silent Track (waits, nothing to hear).
  - **A retrigger during the wait.** The latest Hit fires at the first one's time, once.
  - **Timing.** Hit latency and jitter on Main and on an Out, before and after, as the latency rule asks.
- **Against X.20, without its 68k [I].** Both DSP halves run without the OS, which is what does not boot (MAC unit,
  research/02):
  - **Voice DSP.** Run X.20's voice program (research/01's DSU decoding, read from the user's `.syx` at run time,
    nothing committed) in the dsp56300 emulator, as MoDule runs 1.63's. Send a trigger word with D = 1 or 2 by the
    `$FFFFFE` host command, and check that TRIG runs 2 or 4 blocks later, with the Hit's words.
  - **Mixer DSP.** Run X.20's mixer program's hook P:`$CD4` on a known block, with a command word carrying bit 21 or
    22. Compare its output with MoDule's ramp. Heavier, since the hook expects the mixer's loop registers (R1, R6,
    R7) and its envelope state; a stub that calls it for one Track would do.
- **Against X.20 whole.** Once X.20 boots in mdLib (the MAC unit, then whatever follows: research/02, the map's
  "Booter X.20" card), record TX9-BD retriggers and TRX-SD with TRX-BD at 0, 64 and 128. That would settle the
  latency question (§4.2). A recording from a real Machinedrum on X.20, if the user has one, would settle it sooner.

## What MoDule should decide

| Option | What it gives | Cost | Fidelity |
|---|---|---|---|
| **A. Do nothing** (= ANTI-CLICK OFF, X.20's "OFF (LEGACY)") | Today's sound: every Machine retriggers at once. The 12 User Machines without CONTINUOUS RETRIGGER may click on a retrigger | 0 | X.20 set to OFF, for User Machines; OS 1.63 for the built-ins, which has no fade. A factory X.20 (128) differs on retriggers of non-continuous Machines |
| **B. Build it, fixed at 128** (X.20's default) | Retriggers fade linearly over 128 samples, then the Hit starts 128 samples later | About 2-3 days (§4.1), more with alignment (§4.2) | X.20's rule, curve and wait; the ramp can be bit-exact with X.20's mixer. Built-ins then no longer sound as OS 1.63 on a retrigger, unless the fade is limited to User Machines |
| **C. Build it with a setting** (OFF / 64 / 128) | As B, chosen per instance; OFF keeps existing sets exactly as they are | B + about half a day for the setting, its state and its menu entry | As B at 64 or 128, as A at OFF |

Points for the user's decision:

- **A, B or C.** Research/11's lean still holds: keep A until a user hears the click. B and C trade the click for
  64 to 128 samples of latency, which the latency rule weighs. If built, C with OFF as the default changes no
  existing set.
- **Scope, if built.** User Machines only (what CONTINUOUS RETRIGGER is for, built-ins staying OS 1.63's), or X.20's
  whole rule including about 50 built-ins and every ROM and RAM Machine.
- **Latency, if built.**
  - As X.20: faded Hits are late, Tracks drift apart by Machine, reported latency unchanged.
  - Aligned: every Hit later by the fade length, reported, `Ring` to 256.
- **Where the setting lives, if C.**
  - The set's state: the sound travels with the set.
  - The plug-in's configuration: machine-wide, as X.20's workspace is.
- **Verify the latency inference first** (§4.2, Open) on a booted X.20 or a recording, before copying X.20's late
  Hits.

## Open

- Whether X.20 holds back the Hits it does not fade, so that Tracks stay aligned. The block record's `+$1e` (the
  setting in samples) has no reader found; it may be the one.
- The 68k code that builds the mixer's per-Track command (bits 21, 22, 5, 6 and the offset) and the block in which it
  sends it, relative to the voice DSP's wait.
- How the held Hit's parameter words reach Y:`$13FA40` + 20·t, and what lands at Y:`$13FA2B`, the diverted DMA's
  destination.
- Byte +6 of the Track's trigger record in the continuous exception, and the envelope reading of words 24-27 (§2.1).
- Whether the first boot after an upgrade converts an older OS's workspace. The default of 128 holds either way,
  unless a converted record carries another value (§1.3).
- Every address here is X.20 B's.
