# The neighbours' buffers on MoDule's voice DSP

Research for ticket 19 of the map "User Machines from X.20 in MoDule", "Les tampons des voisins sur le DSP voix de
MoDule". Written 2026-10-19 against `main` at f743ca9, whose working tree holds another session's uncommitted change to
`mdDrums/mdDrumsDevice.h`. Nothing in the repository was built, run or changed apart from this note. No byte or code of
X.20, the zip or the flash image was copied into it: addresses, counts and behaviour only. X.20 was not booted (ticket
02), since reading was enough.

Sources:

- **OS 1.63's voice program.** Read from the user's flash image and decoded by research/01's `dsu.py`
  (`load_163_dsp`). Its P ranges were disassembled with this tree's `dsp56kDisassemble` (md-drums' Release build, not
  rebuilt) into the session scratchpad (`neighbour/v163/`). P:`$0-$3FF` is also kept as research/01 left it
  (`x20data/v_163_low.asm`).
- **X.20 B's voice program**, as research/01 decoded and disassembled it (`x20data/v_x20_low.asm`, `v_x20_hi.asm`).
  Its one mode-5 record, Y:`$7A0-$7F0`, was decoded by the same `dsu.py` (`neighbour/scripts/mode5.py`,
  `changed2.py`).
- **The 13 NEIGHBOUR packages** of the X.20 B zip (research/11 §2).
  - Read in memory by `neighbour/scripts/nprog.py`, which uses research/11's `flags.py`.
  - Their programs were disassembled into the scratchpad (`neighbour/out/`).
  - Their cycles come from mdsPlay's run for research/03 (`mdsplay/all/`).
- **The Standard**, jmamma/MDS `docs/MDX_MachineDumpStandard.txt`. Read from the session's copy
  (`mdsdocs/standard.txt`), whose line numbers are the ones research/11 gives.
- **MoDule's code**, at the places named.

Marks: **[V]** read at the place named; **[M]** measured or decoded by a script here, or by the earlier run named;
**[I]** inferred from the facts around it. An address that names no program is the same in both. Otherwise it is X.20
B's.

## In short

- **1.63 keeps two buffers [V].**
  - The block loop (P:`$64-$E7`) renders Tracks 1 to 16 in order.
  - For each voice, P:`$9B-$A7` flips Y:`$140` between Y:`$100` and `$120` and sets R7 to it.
  - The link DMA (P:`$CF-$D3`) then sends Y:(Y:`$140`) to the mixer DSP while the next voice renders into the other
    bank.
  - So at Track t's RENDER the other bank holds Track t−1's block, and its own bank holds Track t−2's.
- **X.20 keeps four, in the same place [M].**
  - Its ring replaces the flip in the same 13 words, 11 of them changed.
  - Decoded, its Y record gives the whole layout: buffers Y:`$100`, `$120`, `$7A0` and `$7C0`; four descriptors at
    Y:`$7E0-$7EF`; Y:`$7F0` starting at `$7EC`.
  - Track t writes the ((t−1) mod 4)-th buffer of that list. Tracks 1, 5, 9 and 13 therefore share Y:`$100`.
- **What MoDule must add [V]/[I].** The harness already sends each voice from Y:`$140` (`VoiceEngine.cpp:169`) and
  already uses X.20's table bases. Four changes remain:
  - the ring at P:`$9B-$A7`;
  - the 81 Y words at Y:`$7A0-$7F0`, which 1.63 never touches, written at each reset;
  - 32 zeros into R7's buffer on the harness's skip path;
  - lifting the refusal (`VoiceEngine.cpp:244-245`).

  Nothing changes in HostModel, EngineT or the C++ effects and mixer.
- **Tracks 1-4 [V].** They read Tracks 13-16 of the previous block with no extra work. The voice DSP's Y memory
  persists between blocks, and the warm-ups restore it.
- **Idle voices [V].** Types 0 and 1 skip RENDER (`VoiceEngine.cpp:178-216`) and leave their buffer as it was. With a
  ring of four, it would then hold the block of the Track four places earlier. Writing 32 zeros there restores what
  1.63's own null render does. `setSilenceRelease` no longer exists in this tree.
- **Muted, choked and MID/CTR Tracks [V].**
  - Mute and choke act on the mixer words; the voice renders on. So a NEIGHBOUR hears a muted or choked Track's raw
    tail, as the Standard's "before any track effects" says.
  - A MID or CTR Track keeps its previous machine rendering, since no slot is written for it. The NEIGHBOUR hears that
    tail too, as the mix does.
- **Cost [I]/[M].**
  - Memory: 81 Y words, in a hole 1.63 never uses.
  - The ring costs about the flip's cycles.
  - About 35-40 cycles per idle voice.
  - A NEIGHBOUR's own RENDER: 204 to 3,189 cycles [M].
  - Built-in Machines run the same RENDER. Only R7's address changes, for half the Tracks. The host does nothing more.
- **Recommendation.**
  - Patch 1.63's voice program as X.20 does: in place, with X.20's layout, but with the descriptor computed from the
    Track number so that it cannot drift.
  - Write zeros on the skip path.
  - Prove it with a NEIGHBOUR test machine against the sources' raw blocks and against mdsPlay's bare host fed the same
    inputs.
  - Also prove that every built-in Machine's blocks are unchanged.
- **For ticket 20 [V]/[I].** X.20's two other block-loop hooks (P:`$81`, P:`$B5`) are its PRECISE timing.
  - Each Track's block is shifted, inside the voice DSP, by its trigger's offset (0 to 31 samples) before the later
    Tracks read it.
  - So an NFX under X.20 hears its sources already aligned. Its output lags them only by its own last trigger's offset.
  - MoDule delays each Track after the voice DSP, so its neighbours read unshifted blocks.

## 1. Where each Track's block goes

### 1.1 OS 1.63 [V]

`x20data/v_163_low.asm`, the block loop:

| P | What |
|---|---|
| `$64-$69` | S = `$800` (Y:`$141`), Track 0 (Y:`$142`) |
| `$6B-$73` | Every fourth voice, a pacing word to the ColdFire |
| `$74-$7F` | M0-M7 linear, R6 = S, R1 = Track |
| `$81-$98` | A slot word 0 that is set and new calls INIT; a set one calls TRIG (research/04 §4) |
| `$99-$A7` | The flip: Y:`$140` ← `$120` if it held `$100`, else `$100`; R7 ← Y:`$140`; R0 ← the Track's active type (Y:`$153` + t) |
| `$A8-$B4` | RENDER from Y:(`$145C77` + type), R6 = S, M0-M5 linear, M7 = `$1F` |
| `$B5-$CE` | On voice 0 only, wait for frame sync and arm DMA2 for the master return into X:`$700` |
| `$CF-$D3` | Wait for DMA0 to finish the previous voice, then DMA0 from Y:(Y:`$140`) to ESSI0, the link to the mixer DSP |
| `$D5-$E1` | Next voice: Track + 1, S + `$40`, until 16 |
| `$E2-$E7` | RAM-R's input position, then back to `$64` |

- **Tracks run 1 to 16, every block, all 16** (`$DE-$E1`).
- **The banks.**
  - The image's Y:`$140` is `$120` [M], so Track 1 renders into `$100`, Track 2 into `$120`, and so on. 16 is even, so
    Track 1 gets `$100` in every block.
  - At Track t's RENDER, the other bank holds Track t−1's block. Its own bank holds Track t−2's, until RENDER writes it.
    For Tracks 1 and 2, those come from the previous block's Tracks 15 and 16 [I].
  - Two banks are a DMA double buffer: Track t's bank goes to the mixer DSP while Track t+1 renders into the other one
    [I].
- **Who addresses the banks.** Only the block loop names `$100` and `$120`.
  - Built-in code reads Y:`$140` as its own buffer only (P:`$386`, P:`$1451CE`; a search of every 1.63 P range for
    Y:`$140`, `$100`, `$120` and `$7F0`, `neighbour/v163/`).
  - Y:`$783-$7FE` is neither written nor read by any machine (research/04 §2, [M] there).
- **The null render.** `$10008F` writes 32 zeros at R7. It serves GND-- and the empty types (research/01 §6).

### 1.2 MoDule's harness [V]

`VoiceEngine::installHarness` (`VoiceEngine.cpp:139-225`) patches the loop. The rest of the loop runs as 1.63 has it.

| P | Harness | Line |
|---|---|---|
| `$73` | No pacing word | `:158-159` |
| `$A8` | Jump to the skip stub at `$C20`: types 0 and 1 send flag 0 and go to the next voice (`$C38`). Other types send flag 1 and read RENDER from the extended table at `$145EDE` | `:178-216` |
| `$B5` | Jump to the send stub at `$C00`: the 32 words at Y:(Y:`$140`) go out over HI08 | `:161-176` |
| `$E2` | Wait for the host's go word | `:218-224` |
| `$100093-4` | The null render's padding loop removed | `:153-156` |

- **The mixer DSP is replaced.**
  - `readBlock` takes each voice's flag and 32 samples in Track order (`:375-397`).
  - `EngineT::render` hands each Track's raw block to `fxTrack` (TrackFx in C++), then to the Mixer (`MdEngine.h:190-217`).
  - MD Drums then delays each Track's share in its lanes (`mdDrumsEngine.cpp:96-198`).
- **The flip is kept.** P:`$99-$A7` still runs for every voice, skipped or not, since the skip comes after it at `$A8`.
- **X.20's table bases are already there.** `extendDispatch` (`:227-240`) moves the INIT and TRIG bases at P:`$8A` and
  `$95` to `$145D38` and `$145E0B`. The skip stub reads RENDER from `$145EDE` (`:204`). Those are the three addresses
  X.20 writes at P:`$8A`, `$95` and `$A9`.

### 1.3 X.20 B

**The 46 changed words [M]** (`changed2.py`, every word that both programs hold):

| P | Words | What [V] |
|---|---|---|
| `$16-$17`, `$1E-$1F` | 4 | The IRQD (host command) and DMA3 vectors |
| `$3C` | 1 | The init call |
| `$81` | 1 | The machine fetch, now a call to `$3E2` (§1.5) |
| `$8A`, `$95`, `$A9` | 3 | The three table bases; MoDule already has them (§1.2) |
| `$9B-$A2`, `$A5-$A7` | 11 | The ring. `$A3-$A4`, the type fetch, is the same word as in 1.63, at the same address |
| `$B5-$B6` | 2 | A post-render call to `$3FA` (§1.5) |
| `$145AF5`-`$145CCF` | 24 | Entries of 1.63's dispatch tables (research/01 §6) |

NEIGHBOUR needs only the ring.

- **The ring [V]** (`v_x20_low.asm`, P:`$9B-$A7`). Same 13-word window as 1.63's flip:
  1. Y:`$7F0` ← (Y:`$7F0` + 4) AND `$7EF`;
  2. R0 ← the type, as 1.63 does;
  3. R7 ← Y:(Y:`$7F0`);
  4. Y:`$140` ← R7.

  R2 carries the pointer. X0 and X1 are left as they were, where 1.63's flip leaves `$20` and `$100` in them.
- **The Y record [M].** It is mode 5 (the address word's top bits are 5). Research/01 left it undecoded. `dsu.py`
  reads it as byte planes with no running sum, as it reads every mode other than 0 and 2, and the 81 words come out
  self-consistent:

| Y | Contents |
|---|---|
| `$7A0-$7BF`, `$7C0-$7DF` | Two buffers of 32 zeros |
| `$7E0` | Descriptor 0: own `$100`; one, two and three back `$7C0`, `$7A0`, `$120` |
| `$7E4` | Descriptor 1: own `$120`; back `$100`, `$7C0`, `$7A0` |
| `$7E8` | Descriptor 2: own `$7A0`; back `$120`, `$100`, `$7C0` |
| `$7EC` | Descriptor 3: own `$7C0`; back `$7A0`, `$120`, `$100` |
| `$7F0` | `$7EC`: the first voice's step takes it to `$7E0` |

- **The four buffers [M]/[I].** The ring is `$100` → `$120` → `$7A0` → `$7C0`. X.20 keeps 1.63's two banks as two of
  the four and adds two in the hole above the interpolation table. All four are 32-aligned, as M7 = `$1F` needs.

| Tracks | Descriptor | Writes | Reads one, two, three back | Four back (own buffer before RENDER) |
|---|---|---|---|---|
| 1, 5, 9, 13 | `$7E0` | `$100` | `$7C0`, `$7A0`, `$120` | `$100` |
| 2, 6, 10, 14 | `$7E4` | `$120` | `$100`, `$7C0`, `$7A0` | `$120` |
| 3, 7, 11, 15 | `$7E8` | `$7A0` | `$120`, `$100`, `$7C0` | `$7A0` |
| 4, 8, 12, 16 | `$7EC` | `$7C0` | `$7A0`, `$120`, `$100` | `$7C0` |

- **Periodicity [I].** 16 voices step the pointer 64 words, four turns, so it is back at `$7EC` at each block's end.
  Track t therefore meets the same descriptor in every block. Track 1's "back" buffers hold Tracks 16, 15 and 14 of
  the previous block, and its own buffer holds Track 13's, as the Standard says (lines 455-459).
- **What else holds [V].** Y:`$140` still names the voice's buffer, so the link DMA at P:`$CF-$D3` is unchanged, and
  so is MoDule's send stub.

### 1.4 What the 13 packages read [V]

Each package loads Y:`$7F0` once per RENDER, by its absolute address, then the words of its descriptor through R5
(`neighbour/out/*.asm`).

| Package | Descriptor words | Own buffer before writing (four back) | RENDER cycles [M] | Declared |
|---|---|---|---|---|
| NFX-BC | 1, 2 | — | 2,791 | 3,161 |
| NFX-CH | 1, 2 | — | 2,821-2,829 | 3,518 |
| NFX-D1 | 1 | — | 2,938-2,941 | 3,165 |
| NFX-D2 | 1 | — | 3,003-3,022 | 3,235 |
| NFX-D3 | 1 | — | 3,144-3,147 | 3,371 |
| NFX-D4 | 1 | — | 2,115-2,119 | 2,343 |
| NFX-EN | 1, 2, 3 | yes (its T-4) | 2,883 | 3,528 |
| NFX-FC | 1, 2 | — | 2,311 | 2,713 |
| NFX-MX | 1, 2, 3 | yes (its T-4) | 204 | 498 |
| SYN-F1 | 1, 2 | — | 2,715-2,744 | 3,114 |
| SYN-F3 | 1 | — | 3,116-3,189 | 3,556 |
| SYN-F8 | 1 | — | 2,275-2,288 | 2,693 |
| SYN-FJ | 1 | — | 3,090-3,119 | 3,487 |

- **By count.**
  - 7 of the 13 read only the Track before.
  - 6 read two or three back.
  - NFX-MX and NFX-EN, whose parameters are T-1 to T-4, also read four back from their own buffer before writing it.
    The column was checked for those two only.
- **Offsets from R7.** NFX-BC, -CH and -FC address two back as an offset from R7 with M7 set linear. The Standard
  allows this (lines 450-452), and it works wherever the buffers lie.
- **Fixed addresses.** None reads Y:`$140`, `$100` or `$120` by address.
- **What the Standard fixes.** Only Y:`$7F0`, the four words and their order (lines 430-447). Any 32-aligned buffers
  would do. X.20's layout costs nothing more, so taking it changes nothing for a package that assumed it [I].
- **The import gate.** Their imports (6 for NFX-CH; 6 and 7 for NFX-EN) are provided (`VoiceEngine.cpp:246-251`).
  Their declared cycles are at most 3,556 and their programs at most 1,372 words, within `checkUserMachine`'s limits.
  The NEIGHBOUR bit is their only obstacle [V]/[M].

### 1.5 X.20's two other hooks: PRECISE and ANTI-CLICK [V]/[I]

NEIGHBOUR does not need these, but they bear on ticket 20.

- **The fetch, P:`$81` → `$3E2` → `$13FF3C`.**
  - It indexes a 16-word X record per Track at X:`$13FB80` + 16·t.
  - It takes slot word 0, or a word a host command left in the record.
  - A word above `$FF` carries the type in bits 8-15 and seven flag bits:
    - bits 0-4 are an offset;
    - bits 5-6 a count of 2 or 4 blocks.
  - With a count, it defers the trigger (slot word 0 ← 0) and keeps the type. The post-render code counts the blocks
    down (`$13FF95`), then fires the trigger.
  - 2 and 4 blocks are 64 and 128 samples, the readme's ANTI-CLICK settings [I].
- **The post-render, P:`$B5` → `$3FA` → `$13FF88`, `$13FF9F-$13FFFF`.** After RENDER it delays the Track's block, in
  its own buffer Y:(Y:`$140`), by the offset of the Track's last trigger:
  1. it saves the block's last n samples to a carry at X:`$13FC80` + 32·t;
  2. it moves the first 32 − n samples up by n;
  3. it fills the first n with the previous block's carry.
  - The delay persists until the next trigger; a trigger at offset 0 clears it.
  - X.20's readme: "TIMING: PRECISE starts triggers at their exact time. LEGACY retains the original block-aligned
    timing" [I that this is it].
- **What it means for NEIGHBOUR [I].** The shift runs before the next voice. Under PRECISE, every buffer a NEIGHBOUR
  reads already holds its source delayed to its trigger's exact time, and the NEIGHBOUR's output is then delayed by its
  own last trigger's offset. Under LEGACY nothing is shifted.

## 2. What MoDule must add

| # | Where | What | Why |
|---|---|---|---|
| 1 | P:`$9B-$A7`, in `installHarness` | The ring: Y:`$7F0` ← the Track's descriptor; R7 and Y:`$140` ← its word 0; R0 ← the type, as now | The descriptor the Standard promises, and R7 on the ring rather than the flip. The send stub keeps reading Y:`$140` (`VoiceEngine.cpp:169`) |
| 2 | Y:`$7A0-$7F0`, in `installHarness` before `reset()` runs the first block (`:312-337`) | Two zeroed buffers and the four descriptors, X.20's layout (§1.3) | `reset()` reloads 1.63's image, which has nothing there. `saveState` and `restoreState` carry Y (`:405-428`), so warm-ups and installs keep them |
| 3 | The skip path (`kSkipSilent`, `:208-216`) | 32 zeros from R7 before flag 0 | §3.2 |
| 4 | `checkUserMachine` (`:242-245`) | Accept NEIGHBOUR. Refuse STEREO PAIR under its own name | Ticket 22; research/11 §4 |
| 5 | `VoiceEngine.h:49-56` | The comment that names the missing descriptor | — |

- **Two ways to step the descriptor [I].** Both fit the 13 words.
  - **X.20's:** add 4 to Y:`$7F0`, wrap within `$7E0-$7EF`.
  - **Stateless:** Y:`$7F0` ← `$7E0` + 4·(Track AND 3), from R1, which P:`$99` loaded with the Track.

  They give the same descriptors as long as 16 voices run each block from `$7EC`. The stateless one cannot drift if a
  voice ever skips P:`$99-$A7` or a state is restored mid-block. It is the one to take.
- **Registers.**
  - Done in place, either way leaves X0 and X1 as they were rather than 1.63's `$20` and `$100`, and writes A and R0.
    The skip stub already overwrites A and B (`:191-199`), and R0 receives the type.
  - X.20 runs 1.63's built-ins with X0, X1 and R2 so changed [I]. The Standard calls any register outside its table
    unspecified at entry (lines 342-347).
  - If §5's sweep ever finds a built-in that reads X0 or X1 at RENDER, the ring goes to a stub in free P (`$C3C`
    onwards, research/04 §2) that also sets them as 1.63 does: 2 words at `$9B`, about 4 cycles more a voice.
- **What stays as it is.**
  - HostModel writes slots as now.
  - `VoiceEngine::Block` and `readBlock` are unchanged.
  - `EngineT::render` and TrackFx are unchanged.
  - No C++ code copies a buffer.

## 3. Tracks 1-4, idle Tracks, and the other cases

### 3.1 Tracks 1-4 [V]/[I]

- **Nothing to add.** Between two blocks the DSP waits at P:`$E2` with its memory intact (`VoiceEngine.cpp:430-436`). The
  host writes only slot words (`setSlot`, `:339-344`) and the Samples' P range (`mdDrumsEngine.cpp:99`). Track 1 then
  finds Tracks 13-16 of the block before in `$100`, `$7C0`, `$7A0` and `$120` (§1.3).
- **The warm-ups.** `warmUpVoices` and `installUserMachine` render other machines, then restore P, X and Y
  (`MdEngine.h:65-98`, `104-124`). The previous block's buffers come back.
- **A fresh engine.** `reset()` runs from the constructor only, and its first block skips every voice (type 0). With
  §3.2's zeros, all four buffers are silent before the first real block.
- **Timing.** Track 1's inputs are one block late, against Track 5's. X.20 has the same lag, and the Standard describes
  it (lines 455-459).

### 3.2 The voices MoDule skips [V]

- **What is skipped.** The harness skips RENDER for type 0 (never assigned) and type 1 (GND--) and writes nothing to
  the voice's buffer (`VoiceEngine.cpp:178-216`). The host fills the voice's zeros itself (`:383-387`).
- **What the buffer then holds.**
  - With 1.63's two banks, the block of Track t−2.
  - With four, the block of Track t−4 of this block (Track t+12 of the last one for t ≤ 4).
  - A NEIGHBOUR one place after an idle Track would then hear a Track four places further back.
- **What else is not skipped.**
  - An empty USR number (type 195 and up with no package) runs the null render through the extended tables
    (`:233-234`, `:273-275`), so its buffer holds zeros.
  - So do the empty ROM-33 to -48 [I].
- **Silence release.** `setSilenceRelease` is gone. The mdEngine README lists it among the upstream features that never
  turn on here (`README.md:79-82`). Nothing in HostModel stops a voice (`HostModel.cpp:370-411`).
  - `README.md:100-101` still says the engine "stops rendering a voice that has died away". That describes the removed
    feature.
  - `fxTrack`'s skip of a settled silent Track is C++ after the voice DSP (`MdEngine.h:174-188`) and leaves the buffers
    alone.
- **The fix.** The skip path writes 32 zeros at R7.
  - This is what 1.63's null render does for these types (`$10008F`). The Standard asks it of every RENDER: "Write all
    32 words every time, zero for silence. The buffers are shared between tracks" (lines 416-418).
  - M7 is linear on that path (P:`$7C`), so R7 runs 32 words up. The next voice sets R7 again.
- **Why the C++ side cannot do it.** It cannot zero a buffer between two voices, since the DSP renders all 16 on one go
  word. Clearing before the block would not help either: Tracks t and t+4 share a buffer within the block.

### 3.3 Muted, choked, MID/CTR, replaced [V]/[I]

| A neighbour that is | What its voice does | What a NEIGHBOUR reads |
|---|---|---|
| Muted | Its Hits are dropped (`HostModel.cpp:321-322`). Its mixer gains are 0 (`:446-450`). Its voice renders a tail from before the mute | That raw tail, then silence |
| Choked | Its mixer gains are 0 (`:446-450`, `applyChoke` `:414-433`). MD Drums cuts its lanes where the Hit sounds (`mdDrumsEngine.cpp:117-131`). Its voice renders on | Its raw sound, cut nowhere |
| MID or CTR | No slot is written (`HostModel.cpp:397`), so the voice keeps its previous machine (Y:`$153` + t) and renders it every block. `updateMixer` returns before writing (`:440-441`), so that machine also goes on in the mix | That machine's tail, or its whole sound if it is a continuous one; zeros if the Track never played (type 0) |
| An empty USR number, or a package removed | The null render | Zeros |
| A NEIGHBOUR | Its processed block, written in Track order | That block: chains work downward |

- **The Standard's rule.** Inputs are "before any track effects" (line 449). Mute and choke are mixer words on the
  Machinedrum, so they do not reach a NEIGHBOUR's inputs. MoDule follows this with nothing to add [I].
- **X.20.** It is a new 68k build (research/01 §9), so how it mutes and chokes was not read. That it keeps mute and
  choke on the mixer side, as 1.63 does, is assumed [I].
- **A MID or CTR Track** keeping its previous machine's sound is MoDule's behaviour for every listener, not something
  NEIGHBOUR adds. Whether 1.63 does the same was not checked (Open).
- **A replaced NEIGHBOUR.**
  - The Standard: "A replaced neighbour machine's track stays silent until the new machine is triggered" (line 461).
  - In MoDule the voice changes machine only at the new machine's Hit (`HostModel.cpp:346-357`, `394`). Until then the
    old NEIGHBOUR goes on processing.
  - Writing type 1 into the slot when a Track leaves a NEIGHBOUR package would silence it at once. That costs one
    `setSlot` from `setMachine` [I]: a point for ticket 22.

## 4. Cost

| What | Memory | Cycles | Who pays |
|---|---|---|---|
| The ring, in place | 0 more P words (11 rewritten, as X.20 does) | About 13 a voice, about what the flip costs [I] | Every voice, a cycle or two either way |
| The ring through a stub, if §2's fallback is needed | 2 words at `$9B`, about 14 at `$C3C` | About 4 more a voice, about 60 a block [I] | Every voice |
| Buffers, descriptors, pointer | 81 Y words at `$7A0-$7F0`, inside the 124 free at Y:`$783-$7FE` (research/04 §2) | 0 | — |
| Zeros on the skip path | 3 P words | About 35-40 per skipped voice [I] | Idle Tracks only: the zeros 1.63's own GND-- writes |
| A NEIGHBOUR's RENDER | Its own package | 204 to 3,189 [M], declared 498 to 3,556 | Its Track |
| The host (C++) | 0 | 0 | — |

- **The idle block.** 16 idle Tracks now cost 1,474 cycles a block, 92 a voice (research/04 §5, [M] there). The zeros
  would bring that to about 2,100, 0.9 % of 71,680. At research/04's 3.3 ns a cycle for that block, that is about
  2 µs of host time a block at most [I].
- **The busy Kit.** Its 16 Tracks render, so it pays only the ring: about nothing [I].
- **Built-in Machines.** They pay nothing on their RENDER.
  - Their code, their slot words and their dispatch are unchanged.
  - R7's value changes for Tracks 3, 4, 7, 8, 11, 12, 15 and 16, from `$100`/`$120` to `$7A0`/`$7C0`.
  - Their output still leaves through Y:`$140`.
- **Hit latency** is unchanged: nothing on the host's path moves [I]. To be measured anyway, as CLAUDE.md asks.

## 5. Recommended design

**Patch 1.63's voice program as X.20 does, in the DSP.** In `installHarness`:

- the stateless ring at P:`$9B-$A7` (§2);
- X.20's 81 Y words;
- 32 zeros on the skip path;
- then accept NEIGHBOUR in `checkUserMachine`.

Rejected:

- **Copy buffers in the send stub** (it already reads each voice's 32 words). The descriptor must still be set before
  RENDER, so the ring patch is needed anyway. The copy then adds 30 to 60 cycles to every voice, built-ins included,
  and 128 more Y words [I].
- **Copy buffers from C++.** The DSP renders the 16 voices on one go word and `readBlock` only reads the HI08
  (`VoiceEngine.cpp:375-397`, `430-436`). The host could only write between two voices through a handshake per voice:
  16 round trips a block, for every Kit [I].
- **A DSP of its own for User Machines** (research/04 §6). NEIGHBOUR forces it to run after the voice DSP, and it costs
  memory.
- **X.20's voice program whole** (research/01 §7). It brings the ring and PRECISE, but also the 68k side of PRECISE,
  ANTI-CLICK and extended ids, which MoDule does not have.

Risks:

- **Registers at RENDER.** X0 and X1 are no longer 1.63's (§2). Covered by the built-in sweep below, with the stub as
  the fallback.
- **R7 at `$7A0`/`$7C0`.**
  - A built-in that wrote past its 32 words from `$7C0` would hit the descriptors.
  - From `$120`, the same overrun hits the dispatcher's own variables at Y:`$140`, so no 1.63 built-in can be doing it
    [I].
  - A User Machine that breaks the Standard could corrupt the descriptors. With the stateless ring, the host could
    rewrite those 16 words between blocks if that ever shows [I].
- **Bit-exactness against the firmware.** `mdEngineFirmwareTest` must stay green: it compares the patched engine with
  1.63 for TRX-BD, -SD, EFM-CB, P-I-MT, ROM and the groups.
- **JIT.** The P patch goes through `memWriteP`, as the harness's other patches do (`VoiceEngine.cpp:147`). The Y
  words need no invalidation.

Tests (for ticket 22):

- **A NEIGHBOUR test machine.** Written in assembly in `mdDrumsTestMachine.h`, as the saw is. Its RENDER copies one
  input to its output, chosen by an Index parameter: one, two or three back, or four back from its own buffer before
  writing. A copy is exact, so the comparisons can be bit for bit.
- **Engine.** Saws of four different pitches on the four Tracks before it. The test machine's voice block must equal
  the chosen source's raw `voiceBlock()`:
  - in the same block for sources from Track 1 on, in the previous block for those past Track 1;
  - on Track 6 (all four current), Track 2 (one current, three from the block before) and Track 1 (all four from the
    block before);
  - giving zeros for a source never struck, on GND--, or on an empty USR number;
  - giving its raw voice for a muted or choked source.
- **Against the bare host.** `BareHost` already lays out the descriptor, its own buffer and three silent ones
  (`MdsHost.cpp:28-31`, `89-91`). Let it take the four input blocks, so that mdsPlay can feed a package the inputs the
  engine gave it. The engine's NEIGHBOUR voice must then equal the bare host's from its Hit, as `userMachines` compares
  the saw (`mdDrumsEngineTest.cpp:986-1060`).
- **Built-ins unchanged.**
  - Every audio machine on every Track, patched voice DSP against unpatched, all blocks identical. This is research/04's
    sweep, with the patch as the only difference.
  - The existing check that a User Machine changes no other Track's voice (`others == 0`).
  - `mdEngineFirmwareTest`.
- **Cost.** `DSP::getCycles` on the idle and the busy Kit, before and after (research/04 §5: 1,474 and 38,789). Hit
  latency and jitter, before and after.
- **By hand, not in the repository.** NFX-MX with T-1 to T-4 at 127 over four saws, from the user's zip. Its output
  should be about the sum of the four raw blocks.

## What this means for tickets 20, 21, 22

- **Ticket 20 (NEIGHBOUR and sample-accurate Hits).** Research/11 §5 measured the gap in MoDule; X.20 handles the same
  problem differently.
  - **MoDule's gap.** MoDule delays each Track after the voice DSP: (31 − its Hit's wait) + (`AlignedStart` −
    `voiceStart`), set by its own Hit (`mdDrumsEngine.cpp:496-499`, `150-160`). A User Machine counts as
    `CommonVoiceStart`, held back 32 (`:503-525`).
    - A NEIGHBOUR's processed sound therefore lands −31 to +63 samples from a source's dry sound.
    - The gap changes with each source Hit.
  - **X.20's way.** Under PRECISE, X.20 shifts each source inside the voice DSP before the NEIGHBOUR reads it (§1.5).
    The effect then lags all four sources by the NEIGHBOUR's own last trigger offset (0 to 31). Under LEGACY it lags
    them by nothing, plus one block for the inputs past Track 1, in both modes.
  - **The options**, refined:
    - **Follow T-1.** A NEIGHBOUR Track takes its T-1 source's current delay at each block, not only at its own Hit.
      This is exact for the 7 packages that read T-1 alone (§1.4), and off by the delay differences for the 6 that
      also read T-2 or T-3.
    - **Shift on the voice DSP, as X.20 does.** Each Hit's wait moves into a post-render shift in the send stub; the
      lanes keep only the voice alignment. All four inputs are then aligned, but the sample-accurate path of every Track
      is reworked: tickets 19, 36 and 38 of the editor map, the cut where a Choke's Hit sounds among them. The shift
      costs X.20 about 70 to 140 cycles per shifted voice [I].
    - **Accept the gap.**
    - **Turn sample accuracy off** for NEIGHBOUR Tracks and their sources.
  - Latency before and after, as the ticket says.
- **Ticket 21 (the preview).**
  - **What the preview hears.** It strikes the shown Track alone on a fresh engine (`mdDrumsHitPreview.cpp:13-45`). The
    Kit's other Tracks are never struck there: type 0, skipped. With §3.2's zeros, a NEIGHBOUR previews its effect on
    silence. mdsPlay heard silence alone for most of the 13, and −69, −86 and −77 dBFS for NFX-CH, SYN-F1 and SYN-FJ
    (research/03 [M]).
  - **A wider gap.** The preview's `Request` carries the Kit, the Track, the velocity and the Samples, but no package
    (`mdDrumsHitPreview.h:43-49`). So today any User Machine previews its USR number empty: silence [V] for the fields,
    [I] for the sound.
  - **The choice for the user.** Install the instance's packages on the preview engine (each install compiles, 4
    blocks), and strike the four Tracks before a NEIGHBOUR at their last velocities: up to four more voices a block, on
    the preview's own thread. Or show a NEIGHBOUR's preview empty, with a note.
- **Ticket 22 (play them).**
  - The implementation is §2 and §5's design.
  - Accept NEIGHBOUR; keep refusing STEREO PAIR under its own name.
  - Optionally, silence a replaced NEIGHBOUR at once (§3.3).
  - Whatever tickets 20 and 21 decide.
  - The tests of §5. The 13 packages then pass every other check `checkUserMachine` makes (§1.4).
  - STEREO PAIR after it: the role word at Y+9 (research/11 §5).

## Open

- Whether any 1.63 built-in reads X0, X1 or R2 at RENDER. §5's sweep settles it, and decides between the in-place ring
  and the stub.
- What 1.63's own OS sends the voice DSP when a Track takes a MID or CTR machine. MoDule sends nothing, so the previous
  machine plays on, in the mix too (§3.3). This bears on every listener, not only NEIGHBOUR.
- RAM-R and RAM-P render from X:`$700`, which the harness never feeds. What their buffer holds as a neighbour was not
  checked.
- How X.20's 68k side computes the 5-bit offset and the fade count of §1.5. Read only on the voice DSP.
- `mdEngine/README.md:100-101`'s "stops rendering a voice that has died away" describes the silence release that the
  same README lists as removed (`:79-82`).
- Every X.20 address here is X.20 B's.
