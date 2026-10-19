# MDM1's four flags: what they mean, which User Machines set them, what MoDule does

Research for the map's "Not yet specified" lines on TONAL and CONTINUOUS RETRIGGER, and on NEIGHBOUR and STEREO PAIR.
Written 2026-10-19 against `main` at 503de02 (clean). Nothing in the repository was built, run or changed apart from
this note, and no byte, code or package of the zip or of X.20 was copied into it: names and counts only.

Sources:

- **The MDS Standard.** jmamma/MDS `docs/MDX_MachineDumpStandard.txt`, read through WebFetch. The file has one commit,
  c446179 of 2026-10-09, so the line numbers that research/00 and /10 give still hold; they are cited that way, with
  the section names.
- **The user's X.20 B zip.** SHA-256 `59c43c43…24fb`, as research/00 found. It was copied into a scratch folder and
  read in memory by `flags.py` (session scratchpad, `mdm1flags/scripts/`). That script decodes each `$74` dump as
  `packageFromSysex` does and each MDM1 description as `parsePackage` does.
- **X.20 B's 68k OS and voice DSP program**, as research/01 decoded and disassembled them (`x20data/x20_os.asm`,
  `v_x20_low.asm`), and X.20 B's readme. `builtin_continuous.py` reads one table from the decoded OS.
- **MoDule's code**, at the places named.

Marks: **[V]** read at the place named; **[M]** measured by a script here; **[I]** inferred from the facts around it.
Every X.20 address is X.20 B's.

## In short

- **The flags.** MDM1 byte 6 holds them. Bit 0 is TONAL, bit 2 CONTINUOUS RETRIGGER, bit 3 NEIGHBOUR and bit 4 STEREO
  PAIR. Bits 1 and 5-7 must be zero. [V]
  - TONAL: the Machine can be played chromatically, and needs at least one Pitch parameter.
  - CONTINUOUS RETRIGGER: TRIG never makes the output jump, so the host skips its ANTI-CLICK fade.
  - NEIGHBOUR: the Machine processes the raw audio of the four Tracks before its own.
  - STEREO PAIR: NEIGHBOUR plus a role word at Y+9 for two adjacent instances.
- **The 43 [M].**

  | Flag | Machines | Which |
  |---|---|---|
  | TONAL | 15 | 5 TXZ, 5 P-I, SYN-O1/O3/O8/OJ, SYN-VP |
  | CONTINUOUS RETRIGGER | 31 | everything except the 5 TXZ, P-I-ST, SYN-VP, TX8-HH and 4 of the 5 TX9 |
  | NEIGHBOUR | 13 | 9 NFX, 4 SYN-F |
  | STEREO PAIR | 0 | none |

  No package sets a reserved bit or a parameter link. All 13 NEIGHBOUR packages also set CONTINUOUS RETRIGGER.
- **A correction for the map.** "NEIGHBOUR et STEREO PAIR (les 9 NFX et les 4 SYN-F)": those 13 are NEIGHBOUR only.
  None of the 43 sets STEREO PAIR. [M]
- **X.20 reads all four [V].**
  - TONAL gates EDIT KIT's TUNING choice (research/10).
  - CONTINUOUS RETRIGGER becomes a mask of User Machine numbers. The engine tick reads it to leave the ANTI-CLICK fade
    off a Track's trigger.
  - NEIGHBOUR becomes a mask of Tracks. The voice program gives every Track a rotating four-buffer descriptor at
    Y:`$7F0`.
  - STEREO PAIR sets the role word.
- **MoDule today.**
  - Nothing reads TONAL or CONTINUOUS RETRIGGER. Pitch always converts in DEFAULT mode, and no Track ever fades
    before TRIG. So every User Machine plays as under X.20 with ANTI-CLICK OFF. [V]
  - A package with NEIGHBOUR or STEREO PAIR is refused at install. A set's copy of one is dropped without a word. [V]
- **Recommendation.**
  - TONAL: keep as is.
  - CONTINUOUS RETRIGGER: keep as is, and honour it only if ANTI-CLICK is ever built.
  - NEIGHBOUR: implement. The ring is small, but it raises two choices for the user: Hit timing and preview.
  - STEREO PAIR: keep refusing until NEIGHBOUR plays, then add it (small).

## 1. The flags in the Standard

Section 1, "MDM1 machine description" (lines 149-297 by research/00): byte 6 is the flags and byte 7 the parameter
link mask. The JSON source definition (section 5) spells them `tonal`, `retrigger` (default "restart"), `neighbour`
and `stereo_pair`. No capability bit gates any of the four. [V]

| Bit | Flag | Meaning | Rules |
|---|---|---|---|
| 0 | TONAL | The Machine is tonal | At least one Pitch parameter, and every Pitch parameter ranges 0-127 |
| 1 | — | — | Must be zero |
| 2 | CONTINUOUS RETRIGGER | TRIG never makes the output jump | Set it only if that holds |
| 3 | NEIGHBOUR | Processes the audio of up to four preceding Tracks | Independent of the category |
| 4 | STEREO PAIR | Receives a placement role | Only with NEIGHBOUR; a firmware without support rejects the package |
| 5-7 | — | — | Must be zero |

- **TONAL [V].** The Standard defines only the flag, the rule and the Pitch word: an increment P, f = P · 44,100 /
  2^21 "under the active tuning" (lines 221-222). It says nothing of tuning modes, EDIT KIT or `$5B`. Those are X.20's
  (research/10, §3.3).
- **CONTINUOUS RETRIGGER [V]** (section 2, "Retriggering", lines 316-319).
  - With the engine's ANTI-CLICK on, the firmware fades the old sound out before it calls TRIG, so a Machine needs no
    anti-click code of its own.
  - With the flag set, the firmware calls TRIG at once, since the fade itself would then make the click.
  - The Standard gives no fade length. X.20's readme says "OFF, 64 or 128 samples".
- **NEIGHBOUR [V]** (section 3, "Neighbour machines").
  - At RENDER, four 32-word Y buffers hold the four preceding Tracks' blocks, taken before any track effects (line
    449).
  - Y:`$07F0` holds the address of the Track's four-word descriptor. Word 0 points to the Track's own output (R7);
    words 1-3 point to one, two and three Tracks back.
  - Four Tracks back is the Track's own output buffer, read before RENDER overwrites it. Only that buffer may be
    written.
  - Tracks render 1 to 16. Track 1 therefore reads Tracks 13-16 of the previous block, Track 2 reads Track 1 and
    Tracks 14-16, and from Track 5 on all four inputs come from the current block.
  - A Track whose Machine was replaced stays silent until its new Machine is triggered.
  - The Standard sets no extra cycle budget, and says nothing of idle or muted neighbours.
- **STEREO PAIR [V]** (section 3, "Stereo pairs", lines 464-494).
  - **Pairing.** The host scans Tracks 1-16 and pairs adjacent Tracks holding the same installed User Machine number,
    each Track at most once. The first of a pair is left and the second right; an unpaired Track is mono. Different
    numbers never pair, even with identical packages, and Track 16 does not pair with Track 1. Five in a row give L,
    R, L, R, mono.
  - **The role.** It goes to Y:(R6+9) before INIT and RENDER (line 477): 0 mono and 1 left, both reading T−1; 2
    right, reading T−2. The Machine picks its input itself. A Kit load or a reassignment can change the role without
    calling INIT, and the host refreshes both Tracks.
  - **Memory.** Y+9 is not state; the Machine's Y state starts at Y+10, so its Y workset is at least 10 words.
    Pairing changes no pan, level or routing.

## 2. The 43 User Machines of the X.20 B zip [M]

The zip holds 45 entries: X.20's OS `.syx`, a readme and 43 User Machine dumps (`.syx`, `$74`), with no `.mds` file.
All 43 decode, and their CRCs match.

| Machine | MDM1 name | Flags | TONAL | CONT. RETRIG | NEIGHBOUR | STEREO PAIR |
|---|---|---|---|---|---|---|
| NFX-BC | BUS COMP | `$0c` | | x | x | |
| NFX-CH | BBD CHORUS | `$0c` | | x | x | |
| NFX-D1 | DISTORTION | `$0c` | | x | x | |
| NFX-D2 | OVERDRIVE | `$0c` | | x | x | |
| NFX-D3 | FUZZ | `$0c` | | x | x | |
| NFX-D4 | FILTER DISTORT | `$0c` | | x | x | |
| NFX-EN | SY-VP ENSEMBLE | `$0c` | | x | x | |
| NFX-FC | FET COMP | `$0c` | | x | x | |
| NFX-MX | 4-TRACK MIXER | `$0c` | | x | x | |
| P-I-BW | BOWED STRING | `$05` | x | x | | |
| P-I-EP | E PIANO | `$05` | x | x | | |
| P-I-HP | HANDPAN | `$05` | x | x | | |
| P-I-PN | PIANO | `$05` | x | x | | |
| P-I-ST | STRING | `$01` | x | | | |
| SYN-F1 | SX-1 VCF | `$0c` | | x | x | |
| SYN-F3 | TX-3 VCF | `$0c` | | x | x | |
| SYN-F8 | CS-8 VCF | `$0c` | | x | x | |
| SYN-FJ | SY-FJ VCF | `$0c` | | x | x | |
| SYN-O1 | SX-1 VCO | `$05` | x | x | | |
| SYN-O3 | TX-3 VCO | `$05` | x | x | | |
| SYN-O8 | CS-8 VCO | `$05` | x | x | | |
| SYN-OJ | SY-OJ VCO 1+2 | `$05` | x | x | | |
| SYN-VP | SY-VP STRING VOX | `$01` | x | | | |
| TX8-BD | TX-8 BASS DRUM | `$04` | | x | | |
| TX8-CB | TX-8 COWBELL | `$04` | | x | | |
| TX8-CL | TX-8 CLAVES | `$04` | | x | | |
| TX8-CP | TX-8 HAND CLAP | `$04` | | x | | |
| TX8-CY | TX-8 CYMBAL | `$04` | | x | | |
| TX8-HH | TX-8 HIHAT | `$00` | | | | |
| TX8-MA | TX-8 MARACAS | `$04` | | x | | |
| TX8-RS | TX-8 RIM SHOT | `$04` | | x | | |
| TX8-SD | TX-8 SNARE DRUM | `$04` | | x | | |
| TX8-XT | TX-8 TOM CONGA | `$04` | | x | | |
| TX9-BD | TX-9 BASS DRUM | `$00` | | | | |
| TX9-CP | TX-9 HAND CLAP | `$04` | | x | | |
| TX9-RS | TX-9 RIM SHOT | `$00` | | | | |
| TX9-SD | TX-9 SNARE DRUM | `$00` | | | | |
| TX9-XT | TX-9 TOM | `$00` | | | | |
| TXZ-EP | TXZ ELECTRIC PNO | `$01` | x | | | |
| TXZ-LB | TXZ LATE BASS | `$01` | x | | | |
| TXZ-LD | TXZ HEAVY LEAD | `$01` | x | | | |
| TXZ-PD | TXZ HARMO PAD | `$01` | x | | | |
| TXZ-ST | TXZ SYN STRING | `$01` | x | | | |

- **By combination.**
  - `$00`, none: 5.
  - `$01`, TONAL: 7.
  - `$04`, CONTINUOUS RETRIGGER: 10.
  - `$05`, both of those: 8.
  - `$0c`, CONTINUOUS RETRIGGER and NEIGHBOUR: 13.
  - Totals: TONAL 15, CONTINUOUS RETRIGGER 31, NEIGHBOUR 13, STEREO PAIR 0.
- **The Standard's rules hold.**
  - Each TONAL package has exactly one Pitch parameter, PTCH, ranging 0-127, and no other package has a Pitch
    parameter.
  - No package sets bit 1 or bits 5-7. The link mask (byte 7) is `$00` in all 43.
- **Aside.** MDM1 byte 15, the category order, is nonzero in all 43 (10 to 100 within each family). The Standard ties a
  nonzero order to capability bit 2, and `parsePackage` does not read the byte. It bears on ticket 18's browser order,
  not on the flags.

## 3. What X.20 B does with them

### 3.1 The descriptor and its readers [V]

- **The descriptor.** `$217892(id)` returns the installed descriptor of a USR id (194-209), or 0:
  - 16 records of 27 bytes at `$2cf2ec`, which is engine `$2cdb00` + `$17ec`;
  - the flags at +`$19`, as research/10 found, and a present byte at +`$1a`.
- **The readers.** These five routines read +`$19`. A search for every call of `$217892` and every use of the table's
  addresses found no others:

| Routine | Bit | What it does |
|---|---|---|
| `$21c000` | 0 TONAL | "Can this Machine be tonal": a built-in's bit in a table at `$2b0930`, a USR's descriptor bit. EDIT KIT's TUNING editor and the note display call it (research/10, §3.3) |
| `$217bc6` | 2 CONTINUOUS RETRIGGER | Builds a 16-bit mask of the USR numbers that are installed and have the bit (the flags byte moved into the CCR, then a test of Z, which is bit 2). The mask is stored at `$2bba5e` after an install (`$26633c`) and after a removal (`$217cda`, the end of `$217bf4`, which clears a descriptor's 27 bytes), and cleared at `$2a8834` |
| `$2179b2` | 3 NEIGHBOUR | "Is this a neighbour Machine": yes for ids 7-9 (NFX-EV, NFX-CO, NFX-UC, X.20's built-ins), the descriptor bit for a USR id. `$217a06` uses it to build a mask of Tracks at engine +`$4376`, which `$2005be` reads; `$21e4e8`, `$273876` and `$27ebc4` call it too. None of these was followed |
| `$2178ca` | 4 STEREO PAIR | Gives each Track its role, 0, 1 or 2, by the Standard's pairing rule (research/10, §1) |
| `$21aeba` | 4 STEREO PAIR | The conversion: zeroes words count+1 to 8 and writes the role into word 9 (research/10, §1) |

### 3.2 CONTINUOUS RETRIGGER in the tick

- **Into the block record [V].** `$206e28` fills the record that engine +`$42c8` holds, which `$2a5068` hands to
  the tick `$2a5164`. It copies:
  - engine +`$295e` to +`$20`;
  - engine +`$2964` to +`$21`;
  - the mask `$2bba56` to +`$24`, which the tick also reads for USR ids at `$2a5bf4`;
  - the CONTINUOUS RETRIGGER mask `$2bba5e` to +`$26` (`$206eba`).
- **The fade code [V].** The tick stores at `$1000eac` + 6 (`$2a5a12`):
  - 1 when engine +`$2960` is `$40`;
  - 2 when it is `$80`;
  - 0 otherwise.

  Both writers of +`$2960` (`$213d9e`, `$25f28c`) accept only 0, `$40` and `$80`.
- **ANTI-CLICK [I].** The readme's ANTI-CLICK takes OFF, 64 or 128 samples, so +`$2960` is that setting, in
  samples.
- **The decision [V], `$2a5d4e-$2a5dcc`.** On a Track's trigger, the tick builds the Track's control byte (+`$7a`):
  - bit 1, the trigger;
  - bits 4 and up, a fade code D.

  D is the fade code, except that it is 0:
  - when ANTI-CLICK is OFF;
  - for a built-in id up to `$5f` whose byte in the table at `$2b371a` is set;
  - for USR-01 to USR-16 whose bit is set in the record's +`$26` mask (`btst` on +`$27` for numbers 0-7, +`$26`
    for 8-15).

  Any other id takes the fade code. So a User Machine with CONTINUOUS RETRIGGER retriggers without the fade, as the
  Standard says.
- **An exception, not identified [V]/[I].** Even a continuous Track gets the fade when all three hold:
  - the trigger's own byte +6 is clear;
  - word 27 of the Track's 38 parameter words (engine +`$cb8` + 76 · Track, research/10) is at least `$3f7d`;
  - word 24 is at least `$7d`.

  Those words lie past the Kit's 24 parameters, perhaps X.20's per-Track envelope [I].
- **X.20's continuous built-ins [M]** (`builtin_continuous.py`, the table at `$2b371a`): 20 ids.
  - NFX-EV, NFX-CO, NFX-UC;
  - TRX-BD, TRX-XT, TRX-CB, TRX-XC, TRX-B2;
  - P-I-BD, P-I-RC, P-I-CC, P-I-HH;
  - the eight INP Machines.

  So X.20 skips the fade for its own effects and inputs, as for the User Machines that set the flag.

### 3.3 NEIGHBOUR on the voice DSP [V]

- **The pointer.** X.20's block loop (P:`$9b-$a6`, `v_x20_low.asm`) sets Y:`$7f0` ← (Y:`$7f0` + 4) & `$7ef` and then
  R7 ← Y:(Y:`$7f0`). For each Track, the pointer steps through four 4-word descriptors at Y:`$7e0-$7ef`. Each
  descriptor's word 0 is that Track's output buffer.
- **1.63's banks.** 1.63 flips R7 between Y:`$100` and `$120` instead (research/01, §5).
- **The other words.** The rest of X.20's 81 added Y words (Y:`$7a0-$7f0`) start from DSU mode 5, which research/01
  did not decode.
- **The ring [I].** 16 Tracks make four turns of the ring, so a Track meets the same descriptor in every block, and
  Track 1's inputs are Tracks 13-16 of the block before.
- **mdsPlay.** Its bare host uses the same addresses (`MdsHost.cpp:28-31`, `89-91`): descriptor at Y:`$7e0`, pointer
  at Y:`$7f0`, its own buffer at `$800`, and three silent buffers.

## 4. What MoDule does with each today

- **Reading [V].**
  - `parsePackage` keeps byte 6 as `flags` and byte 7 as `linkMask` (`MdsPackage.cpp:112-113`).
  - `MdsPackage.h:42-46` names the four bits.
  - It checks none of the Standard's flag rules: reserved bits, TONAL's Pitch parameter, STEREO PAIR without
    NEIGHBOUR, the 10 Y words of a pair.
  - Not checking agrees with ticket 05's rule that MoDule refuses no more than X.20 does. Whether X.20's installer
    checks them was not read.
- **TONAL: read by nothing [V].** Only `MdsPackage.h:43` names `FlagTonal`.
  - Every Pitch parameter converts in DEFAULT mode at TUNING 0 (`MdsPackage.cpp:245-261`). HostModel converts each
    User Machine word with `convertFine` (`HostModel.cpp:378-391`), with no Track tuning mode.
  - A Kit keeps each machine word whole (`MdKit::machines`), but the engine reads its low byte only (`mdkit.h:68`).
    So a Kit's tuning bits, wherever X.20 keeps them, change nothing.
  - The `$5B` decoder takes the high bit of an id only when the flags byte is exactly 1
    (`mdkitchange.cpp:104`).
    - X.20's readme gives that byte's tuning flags: TONAL `$02`, TONAL-1 `$0A`, TONAL+1 `$12`, plus `$01` for an id
      of 128 or more.
    - So an X.20 `$5B` that assigns USR-01 with TONAL (`$03`) assigns id 66, P-I-MT, in MoDule.
    - MoDule's own encoder sends only `$00` or `$01` (`mdsysexautomation.h:171-174`), so nothing in MoDule hits
      this today [I].
- **CONTINUOUS RETRIGGER: read by nothing [V].** Only `MdsPackage.h:44` names `FlagContinuousRetrigger`.
  - MoDule has no ANTI-CLICK. HostModel puts the type in word 0 of the slot on a Hit (`HostModel.cpp:390`), and
    1.63's dispatcher calls TRIG in that same block (research/04, §4). No code in `mdEngine` fades a voice.
  - Every User Machine, flag or not, retriggers as under X.20 with ANTI-CLICK OFF. The built-in Machines retrigger as
    under OS 1.63, which has no fade.
  - The 12 packages without the flag may click on a retrigger, as they would under X.20 with ANTI-CLICK OFF [I]: TX8-HH,
    TX9-BD, TX9-RS, TX9-SD, TX9-XT, P-I-ST, SYN-VP and the 5 TXZ.
- **NEIGHBOUR and STEREO PAIR: refused [V].**
  - **The check.** `VoiceEngine::checkUserMachine` throws for either bit (`VoiceEngine.cpp:242-245`). Its message
    names NEIGHBOUR alone, even for STEREO PAIR.
  - **Install.** `Library::install` calls the check before writing (`mdDrumsLibrary.cpp:99-106`). The Menu and the
    drop therefore show the reason on the status line, and nothing reaches the Library.
  - **A set's copy.** `Device::setState` catches the throw and skips the package (`mdDrumsDevice.cpp:71-82`). The
    Track then plays its USR number empty, and silent, and the next save no longer carries the package (`getState`
    writes only what the engine holds, `:55-57`) [I].
  - **The test.** `mdDrumsEngineTest.cpp:1085-1104` checks that a NEIGHBOUR draft is refused. No test covers STEREO
    PAIR.
  - **13 of the 43** are refused: the 9 NFX and the 4 SYN-F.
  - **mdsPlay.** It plays NEIGHBOUR packages against silent neighbours. Research/03 found them silent there, or below
    −69 dBFS, as they should be.
- **Y+9 [V].** HostModel writes 9 slot words, the trigger and 8 parameters, zero beyond the package's count
  (`HostModel.cpp:383-391`). Y+9 stays the package's own memory, which is right for every package that does not set
  STEREO PAIR.

## 5. What NEIGHBOUR and STEREO PAIR would take in MoDule [I]

- **The ring.** The harness or a patch of P:`$9b-$a7` does what X.20's 9 words do (§3.3): a pointer at Y:`$7f0` and
  four descriptors at Y:`$7e0-$7ef` over four 32-word buffers, aligned for M7 = `$1F`.
  - The harness's send stub reads R7 from Y:`$140` (`VoiceEngine.cpp:169`), which X.20's loop still writes at P:`$a6`.
  - Y:`$7a0-$7f0` is free in 1.63 (research/04, §2).
- **Idle voices.** The harness skips RENDER for types 0 and 1 and writes nothing to their buffer
  (`VoiceEngine.cpp:178-216`). 1.63's null render writes 32 zeros instead. With a ring of four, a skipped Track's
  buffer would still hold the block of the Track four places earlier, and a NEIGHBOUR Machine would read stale audio.
  The skip has to clear the buffer, or the descriptor has to point at silence.
- **Hit timing: a choice.**
  - Sample-accurate Hits delay each Track's contributions by an amount its own Hit sets: 31 less the block wait,
    plus `AlignedStart − voiceStart` (`mdDrumsEngine.cpp:40-47`, `491-494`).
  - A NEIGHBOUR Machine reads its neighbours' raw blocks in engine time, but its output takes its own Track's delay.
    So its processed sound can land up to about 63 samples away from its sources' dry sound, and the gap changes from
    Hit to Hit.
  - One option: give a NEIGHBOUR Track no delay of its own and the delay of the source it follows. Another: accept the
    gap. A third: turn sample accuracy off for the Tracks it reads.
- **Hit preview: a choice.** The preview renders one Track alone, without the master effects
  (`mdDrums/mdDrumsHitPreview.*`). An NFX or SYN-F would preview silent unless the preview also plays the Tracks
  before it.
- **The audition and the browser.** Once NEIGHBOUR plays, choosing NFX-D1 with nothing on the Tracks before it plays
  silence. That is correct, but it is a surprise; the status line could say so.
- **STEREO PAIR**, once NEIGHBOUR plays:
  - a role per Track, from the Kit's machines by the pairing rule (§1);
  - a tenth slot word for a STEREO PAIR package, recomputed at each update, so a Kit load or reassignment refreshes
    both Tracks.

  No package of the 43 needs it.

## What MoDule should do

| Flag | Do | Why |
|---|---|---|
| TONAL | **Keep as is** | It gates only X.20's tuning modes (research/10), which the map puts out of scope. All 15 tonal packages play their Pitch in DEFAULT, 16 settings an octave, which X.20 also gives a Track left in DEFAULT. Refusing a package that breaks the TONAL rule would refuse more than X.20 does (ticket 05). |
| CONTINUOUS RETRIGGER | **Keep as is (ignore)** | It only switches off ANTI-CLICK, which MoDule does not have. Today every Machine retriggers immediately, which is what the flag asks for. If ANTI-CLICK is ever built (the map's open "fondu ANTI-CLICK" item), honour the flag then, with §3.2's 20 built-ins and the unidentified exception. |
| NEIGHBOUR | **Implement** | 13 of the 43 (all the NFX effects and the SYN-F filters) are refused today. The DSP side is small (§3.3, §5). |
| STEREO PAIR | **Keep refusing, then implement after NEIGHBOUR** | None of the 43 uses it, and it needs NEIGHBOUR's buffers. Afterwards it costs a role per Track and one slot word. |

Points for the user's decision:

- **NEIGHBOUR's place in the plan.** For 0.1.2, or after it? The 13 are a third of the zip.
- **NEIGHBOUR and sample-accurate Hits (§5).** A NEIGHBOUR Track's delay: follow its source, accept a gap of up to
  about 63 samples that changes from Hit to Hit, or drop sample accuracy for those Tracks.
- **NEIGHBOUR and the Hit preview (§5).** Render the neighbours too, or show the preview empty.
- **ANTI-CLICK.** Already open on the map. Decide it before CONTINUOUS RETRIGGER means anything in MoDule.
- **Smaller points:**
  - Should a set keep, in its state, a package this build cannot play, rather than dropping it on the next save?
  - Should the `$5B` decoder read X.20's tuning flags (bit 0 as the high bit)? Then an X.20 sender's TONAL
    assignment of a USR Machine would no longer land on ids 66-81 (P-I-MT, the other P-I, INP-GA and INP-GB, or
    nothing).
  - The refusal message should name STEREO PAIR when that is the bit.
- **The map.** Correct "NEIGHBOUR et STEREO PAIR (les 9 NFX et les 4 SYN-F)": those 13 are NEIGHBOUR; none is STEREO
  PAIR.

## Open

- What X.20's NEIGHBOUR Track mask (engine +`$4376`, read at `$2005be`) serves, and the three other callers of
  `$2179b2`.
- The exception in §3.2: the trigger's byte +6, and words 24 and 27 of a Track.
- Whether X.20's installer refuses reserved flag bits, STEREO PAIR without NEIGHBOUR, or TONAL without a Pitch
  parameter.
- X.20's own `$5B` decoder for USR ids with tuning flags (research/10, Open).
- DSU mode 5's initial words for Y:`$7a0-$7f0`: which buffers X.20's four descriptors name.
- Every address here is X.20 B's.
