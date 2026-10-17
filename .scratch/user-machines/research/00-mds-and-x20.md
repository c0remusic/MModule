# MDS User Machines and the X.20 firmware, as MoDule could host them

Research for charting the map "User Machines from X.20 in MoDule". Written 2026-10-17 against `main` at c21da53;
nothing was built or changed in the repository. Read-only probes ran on the user's X.20 B zip
(`%LOCALAPPDATA%\Programs\Gearmulator-Elektron\MDX\X.20B\Machinedrum_SPS1-UW_OS_X.20_B.zip`, SHA-256 `59c43c43…24fb`,
which matches jmamma/MDX's `SHA256SUMS`) and on the 1.63 flash image. Sources are GitHub paths (jmamma/MDS,
jmamma/MDX, joelanders/gearmulator-md-mm) or files of this tree.

Marks: **[V]** read at the place named; **[I]** inferred from the verified facts around it.

## In short

- A User Machine is a public, documented package (MDS1) of DSP56303 code with three entry points, INIT, TRIG and
  RENDER, called per Track. MoDule can host it from the published ABI without the X.20 OS. [V] for the format, [I] for
  the hosting.
- The services a package imports are code of the firmware, not of the package: MoDule writes its own. The 43 User
  Machines of the X.20 B zip import only 1, 6, 7 and 8 (sine table, rings, scratch), none of the Sample or RAM
  services. [V]
- X.20 itself does not load in mdEngine: its OS file packs differently and every OS 1.63 address mdEngine uses would
  have to be found again in a binary without symbols. [V] for the file, [I] for the cost.
- Nothing grants a licence: MDX is binary only, and neither MDS nor the 43 User Machines carry one. [V]

## 1. The MDS package and its ABI (jmamma/MDS)

- Package (`docs/MDX_MachineDumpStandard.txt:28-48`, `62-108`) [V]: a 128-byte "MDS1" header, the DSP56303 program
  (24-bit words, big-endian), relocation bits, an import table, the "MDM1" description and a 128-byte sprite. The
  header gives X and Y words per Track (64 each at most), the INIT, TRIG and RENDER offsets, a declared cycle cost,
  CRC-32s and a SHA-256 content ID.
- MDM1 (`:149-297`) [V]: a category (1-3 characters), a 2-character code, a 16-character name, a 32-character stable
  ID, and 8 parameters, each with a name, default, minimum, maximum and conversion byte. Flags: TONAL, CONTINUOUS
  RETRIGGER, NEIGHBOUR, STEREO PAIR. The sprite is 32x24, one bit per pixel.
- Transfer (`docs/MDX_MachineDumpProtocol.txt:20-72`) [V]: SysEx `$74`, version `$01`, a 42-byte dump header, packets
  of 420 bytes, ACK and NAK, capability and slot queries. A `.syx` file of the zip is that dump.
- Calls (Standard `:304-312`, `332-347`) [V]: INIT when the Machine is assigned, TRIG on a Hit, RENDER every block of
  32 samples, Hit or not; a parameter change calls nothing. On entry R6 points to the Track's memory, R1 holds the
  Track number (INIT only), R7 with M7=$1F is a 32-word Y output buffer.
- Memory (`:369-419`) [V]: Y+1 to Y+8 hold the converted parameters; the rest of the 64 X and 64 Y words is the
  Machine's state; each Track also has 1,536 words of external memory.
- Budget (Standard `:123-142`, Protocol `:218-222`) [V]: 3,951 clocks per Track out of 71,680 per frame; 16 Tracks
  may use 63,216 together.
- Imports and services (`:536-563`, `603-616`) [V]: a sine table, the audio input ring, the main output ring, the
  external memory, two 32-word scratch areas; services SAMPLE_INFO, SAMPLE_READ, RAM_WORKSPACE, RAM_RECORD, called
  with `JSR >address` that the loader patches. "Their code is in the firmware, not in the package" (`:620-622`).
- UC side [V]: converts the 0-127 settings before they reach the DSP (`:194-195`); loads and relocates the program
  into a shared DSP program memory (Protocol `:175-177`, `201-202`); fades the old sound before TRIG when ANTI-CLICK
  is on (`:316-319`); writes the stereo pair's role into Y+9 (`:477`); stops everything on panic (`:325-330`).
- Which DSP [I]: the documents say "the Machinedrum's DSP56303". Everything points to the voice DSP: RENDER per Track
  in Track order matches 1.63's voice loop (`mdEngine/engine/VoiceEngine.cpp:36-44`), SAMPLE_READ reads UW samples
  that live in the voice DSP (`mdEngine/tools/mdfw/Firmware.h:76-83`), and NEIGHBOUR audio arrives "before any track
  effects", which run on the mixer DSP (Standard `:449`).

## 2. The 43 User Machines of the X.20 B zip

- All 43 decode as MDS1 version 1 packages with IDs `org.spsx.usr.*` [V]. Families: NFX (9), P-I (5), SYN (9), TX8
  (10), TX9 (5), TXZ (5).
- None imports services 9 to 12 (Sample and RAM); they use imports 1, 6, 7 and 8 only [V].
- NFX and SYN-F set NEIGHBOUR (flags `0xc`) [V].
- None carries an author, copyright or licence text; the X.20 readme names no licence for them [V].

## 3. MDX X.20 (jmamma/MDX)

- Binary only: release zips, `SHA256SUMS`, `docs/2.0/` (SysEx, PatternKit, readme), a logo; no source, no licence
  [V]. README: "Created by Yatao Li and Justin Mammarella", "independently developed with permission from Elektron".
  X.20 B is a preview of 2026-10-09; X.14 stays the stable release.
- The OS file is a standard Elektron OS upgrade: 15,104 `$7E` data messages over flash `$4000-$F0000`, an end
  marker and a `$7F` verify message [V]. The readme treats it as an upgrade of a real machine (back up, downgrade
  then Factory Reset): a user owns X.20 as a `.syx`, not as a flash image [V].
- X.20's engine changes [I unless marked]: likely 68k side: LFO-B, retrigs, A=440 tuning, the tonal octave ranges;
  likely voice DSP side: PRECISE timing (a voice starting mid-block), ANTI-CLICK, INTERPOLATE, the new built-in
  Machines (which also need UC coefficient code). Kit 65.1, the `$5B` tuning flags and Pattern 64.1 are data and
  protocol changes [V]. TRX-S2 already exists in OS 1.63 (`TRXS2` at `$24f50f`, section 0) [V].

## 4. Booting X.20 in Gearmulator (joelanders/gearmulator-md-mm)

- Pull request 99 ("RAM diff diagnostic"): a memory panel with MCP endpoints; nothing about the OS [V].
- Pull request 100 (open, not merged, 42 files, +9,047/-328) [V]: a +Drive model; `mdosimage`, which decodes OS
  upgrade `.syx` files offline; `mdosvariant`, which lays the decoded OS over the stock 1.63 ROM (split at
  `$200000`); OS switching by reboot. Its tests use X.13; it predates X.20 and never names it. Booting X.20 is
  unverified; X.20's address range fits its overlay window [V].

## 5. How mdEngine binds to OS 1.63 (this tree)

- The flash image must be 8 MiB (`mdDrums/mdDrumsEngine.cpp:19-27`) and pass `loadFirmwareFromFlash`
  (`:256-271`); the "163 " tag is read but never compared (`mdEngine/tools/mdfw/Firmware.cpp:115-122`) [V].
- The container at `$4000-$100000` needs at least 3 sections, checksums checked on sections 1 and 2
  (`Firmware.cpp:169-186`); section 1 goes to the voice DSP (`engine/VoiceEngine.cpp:220`), section 2 to the master
  DSP (`engine/MasterEngine.cpp:64`) [V].
- 1.63 addresses written in the code [V]: voice program patch points `$24`, `$73`, `$a8`, `$b5`, `$d5`, `$e2`,
  `$100093`, AAR and OMR (`VoiceEngine.cpp:31-48`, `133`); mixer P:`$342-$970`, `$100031`, `$58`, `$65`
  (`MasterEngine.cpp:28-36`); 68k SRAM image `$2622f4` and boot SRAM data (`engine/MachineRunner.cpp:20`, `81-86`),
  SRAM variables and routines (`engine/HostModel.cpp:16-28`), master tick bodies `$20b4ae…` (`:39-42`); the machine
  table found by searching "TRXB2PTCH" (`MachineRunner.cpp:112-118`).
- X.20's OS file [V]: section 0 is 463,626 bytes packed (1.63: 164,328) and the 1.63 depacker stops at input byte
  7,090 with a bad offset; a "X20 " tag at `$75340`, then "SFS1" blocks up to `$F0000`; 1.63's sections 1 to 4 are
  not where 1.63 has them. The UW bank (`$100000`) and the conversion table (`$7a0000`) lie outside the file's range.

## Open

- X.20's section 0 packing and its SFS1 format; where its DSP programs and its User Machine dispatcher live.
- Which DSP X.20 really runs User Machines on.
- Whether pull request 100's overlay boots X.20.
- Where X.20 keeps User Machines and INTERNAL samples in onboard flash.
- The Kit 65.1 layout (`MDX_PatternKit_2_0.txt`, not read).
