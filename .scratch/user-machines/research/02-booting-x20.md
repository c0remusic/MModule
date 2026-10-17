# Booting MDX X.20 in mdLib

Ticket 02 of the map "User Machines from X.20 in MoDule", "Booter X.20 dans mdLib". Written 2026-10-17 in the worktree
`C:\dev\MModule-x20`, branch `x20-boot` from `main` at cb44f07, so that nothing ran in `C:\dev\MModule`'s own tree or
build. The user's X.20 B OS upgrade (`Machinedrum_SPS1-UW_OS_X.20_B.syx` of the zip that research/00 names) was
decoded in memory and laid over the user's OS 1.63 flash image, and that image booted in mdLib's whole machine (the
ColdFire UC and both DSP56300s), every run under a wall-clock guard. Nothing of X.20, of a User Machine or of the
flash image is in the repository or in a commit: the tool reads them by environment variable and path; extracted
members, RAM dumps and logs stayed in the session's scratchpad.

Marks: **[V]** read at the place named; **[M]** measured here with `mdX20BootTest`; **[I]** inferred.

## In short

- X.20 does not boot yet. It gets past its own loader and runs its OS for a second and a half, then takes an
  exception: the OS uses the MCF5206e's MAC unit, which Musashi does not emulate [M].
- Three other gaps of our emulators stopped it first, and are fixed on the branch: two DSP56300 instructions the JIT
  compiled to nothing (CMPU, EOR #xxxx) and the ColdFire's byte push to the stack, which Musashi moved by two as a
  68000 does [M].
- So nothing of the sound was reached: TX8-BD was neither installed nor played, and the Pitch conversion ($54) was
  not read. The tool does both once X.20 takes MIDI (`--play`, `--pitch`), untried [M].
- What remains before X.20 can be the reference: the MAC unit (about 290 instruction sites in the OS, and its
  MACSR modes), then whatever the OS meets after it, unknown [M], [I].

## 1. The image

- Our own decoder of the OS upgrade (`decodeOsUpgrade` in `mdLibTest/x20BootTest.cpp`), the format as pull request
  100 of joelanders/gearmulator-md-mm documents it (`mdosimage.h`, read with `gh api`, not fetched or run):
  `F0 00 20 3C 02 00 7E`, eight header bytes (two of check, `00`, the flash address / 16 as four nibbles, `00`),
  then groups of three bytes `c h l`, each a big-endian 16-bit word `c << 14 | h << 7 | l` [V].
- X.20 B: 15,104 data messages, contiguous from `$4000` to `$F0000`, one empty `7E` message (the end marker), a `7F`
  message whose body is `00 0E 0C 00 00 00` [M]. The decoded section 0 begins with X.20's 463,626-byte size, as
  research/00 found [M].
- The overlay is `mdosvariant`'s: the 1.63 image with those bytes from `$4000`; the boot loader below `$4000`, the UW
  bank from `$100000` and the rest of the flash stay 1.63's [V]. Fingerprint of the result `63e96b9c90b52916` [M].
- mdLib took only the pinned 1.63 image: `RomLoader::isRomForModel` refused any other, and `initRom` then silently
  booted the 1.63 it found instead [V]. `RomLoader::acceptOsVariant(fingerprint)` now admits one image that the
  caller vouches for (the fingerprint of the whole image), which also arms the Machinedrum's DSP link rendezvous as
  for 1.63; none of 1.63's RAM addresses (live Kit, patterns, sequencer position) apply to it [V].

## 2. How X.20 boots

What the traces showed, in order [M] unless marked:

1. Elektron's boot loader (flash `$0-$4000`, the same for every OS) unpacks section 0 into RAM. For X.20 that is a
   loader of X.20's own at `$2F8000`, with an "S2MD" header at `$2FB000` (version 7, `$60` bytes, CRC-32 checked;
   the CRC table built at `$2F6C00`). It writes its progress to `$2EFFF0` as `$5332000n`, phases 1 to 9, and an error
   as `$E532xxxx`. On error it returns to a `BRA *` at `$2F8064` and the machine shows its boot logo for ever: that
   is where the first runs stopped, at 1 s of audio.
2. The loader boots both DSPs through their HI08 bootstrap (mdLib's `DspBoot`: done at 1.2 and 1.4 ms), then sends
   each a second-stage loader to P:`$14F000` (tagged "SFW"). That loader says "DSU", reads a program header from the host
   ("USD", version `$E04`, sizes, three 48-bit checks), unpacks and checks the program, says "RDY", and takes the
   commands "ACT", "ELD" and "CFD". It answers an error as `$E00000` plus its code.
3. The UC then unpacks X.20's OS into `$200000-$2BCA1C` (BSS to `$2D72A4`, data in the internal SRAM from
   `$1000E60`), until 21.7 s of audio; about 7 s of wall clock per second of audio meanwhile (70 s of audio took
   190 s, 60 s 178 s, against 18 s for 60 s stopped at the loader) [M]. Loader phase 9 reached.
4. The OS sets VBR to the internal SRAM, writes its own progress to `$2D72F0` as `$5350nnnn` ("SP"), and reaches step
   7 by 23.5 s of audio. Then it takes a line-A exception at `$2A6DE4` (`move.l %acc,%d1`), its handler records
   "EXCP" with the frame (format 4, vector 10, SR `$2700`) and stops with `$5350FFFF` and `STOP #$2700` at
   `$283CE2` [M]. The LCD keeps the boot logo; MIDI input is never enabled [M].

No storage conversion was reached: the flash was never written [M].

## 3. What stopped it, and what was changed

| Where it stopped | Why | Fix (branch `x20-boot`) |
|---|---|---|
| DSP loader answers `$E00003`, UC loader error `$E5320308` | CMPU compiled to nothing: its header checks branched as if unequal [M] | CMPU in the JIT and the interpreter, `source/dsp56300` |
| Same, `$E00008`, `$E5320808` | EOR #xxxx compiled to nothing: the CRC-32 never matched [M] | EOR #xxxx in the JIT |
| OS returns into unmapped memory at 22 s | Musashi moved A7 by two for a byte `-(A7)`; GCC for ColdFire pushes a char with `move.b ...,-(sp)` then `subq.l #3,sp` (PUSH_ROUNDING), two bytes lost per byte argument; the frame later had format 6, a stack misaligned by two [M] | ColdFire moves A7 by one, `source/mc68k` |
| OS line-A exception at `$2A6DE4`, 23.5 s | The MCF5206e's MAC unit: not in Musashi [M] | Not done |

- The DSP fixes follow upstream dsp56300, which implements CMPU from sim56300's behaviour: 48-bit unsigned, N set
  with C, V cleared, E and U unchanged [V]. Our fork also lacks, in its JIT, ADC, SBC, NORM, EXTRACT, DOR, REP and
  MOVEM by address, and others (`jitops.h`) [V]; X.20's code met none of them on its way to the OS, as the JIT now
  says in a release build [M].
- 1.63 after the three fixes: 9 passed out of 9 of the gate's firmware and engine tests, `mdAutomationMidiTest`,
  `mdKitDumpFirmwareTest`, `mdEngineFirmwareTest` with its four fixtures (which boot 1.63 in mdLib),
  `mdDrumsEngineTest` and `mdDrumsDeviceTest`, in 289 s [M]. mdEngine's 68k runs in the same MCF5206e mode, so the
  A7 fix reaches MoDule's engine too [V]. The rest of the gate (plug-in, skin, VST3) was not run: nothing it builds
  changed but through these libraries.
- The MAC: X.20's OS holds about 290 line-A words at instruction starts in 58 places, moves to and from ACC, MACSR
  and MASK among them (raw scan of the OS in RAM with Capstone) [M]. Functions save ACC and MACSR, set MACSR, compute,
  restore (`$2A6DC6`) [M]. So: MAC and MSAC, word and long, with and without the parallel load and MASK, the
  MOVE forms, MACSR to CCR, and MACSR's modes (signed or unsigned, integer or fractional, saturation, rounding) [I].

## 4. The tool

`source/elektron/md/mdLibTest/x20BootTest.cpp`, target `mdX20BootTest` (built by hand, not in the gate):

- `--boot [seconds]`: the boot timeline, a line a second (UC PC, DSPs booted, panel handshake, MIDI receive, flash
  written, LCD digest), the LCD when it changes; once X.20 takes MIDI, the MDS capability request (`$74`, command
  `$06`) and its reply. When it never does: the UC's PCs, registers and stack, and each DSP's PC.
- `--play <machine.syx> <out.wav>`: the User Machine installed closed-loop in slot 0 (each `$74` message waits up to
  4 s for its ACK, NAK, WAIT or CANCEL), assigned to Track 1 as `5B 00 42 01 02` (USR-01, c=1, init 2), routed to
  output A, 4 Hits at velocity 127 every 22,016 samples, all 6 outputs written to a 32-bit float WAV;
  `comparehits.py` (scratchpad) then compares it with `mdsPlay`'s render per Hit (onset, peak, f0 at 2-200 ms, -20 and
  -40 dB times, best-aligned correlation and residual).
- `--pitch <machine.syx>`: a TONAL machine's Pitch parameter (`$54` in its MDM1) set to 21 values, the DSPs' X and
  Y memory dumped after each, the words that follow the setting printed.
- Diagnostics, by environment: `MD_X20_LCD` (LCD on every change), `MD_X20_DUMP=<file>` (main RAM and SRAM),
  `MD_X20_DSP_LIST=<from>-<to>` (DSP P memory disassembled), `MD_X20_DSP_Y=<from>-<to>`; in mdLib,
  `MD_TRACE_HOST_WORDS=n` (the first n HI08 words each way of each DSP) and `MD_UC_PC_TRACE=n` (the UC's last n PCs,
  frozen at the first one outside memory; interrupts acknowledged appear as `$EEEELLVV`).

Run it from the worktree, under a guard:

```powershell
$rom = "$env:LOCALAPPDATA\Programs\Gearmulator-Elektron"
$env:GEARMULATOR_MD_FIRMWARE_BIN = "$rom\elektron_sps1-1uw_os1.63.bin"
$env:MD_X20_OS_SYX = "<folder>\Machinedrum_SPS1-UW_OS_X.20_B.syx"   # unzip -p from the X.20 zip into a scratch folder
cmake --build temp\cmake_vs22 --config Release --target mdX20BootTest
$p = Start-Process temp\cmake_vs22\source\elektron\md\mdLibTest\Release\mdX20BootTest.exe -ArgumentList "--boot","30" `
  -RedirectStandardOutput boot.log -NoNewWindow -PassThru
if(-not $p.WaitForExit(600000)) { $p.Kill() }
```

## 5. Commits

- `source/dsp56300` 0e7b9234 (branch `x20-boot`, on f84b5b6c): CMPU, EOR #xxxx, the JIT's report of what it lacks.
- `source/mc68k` 69939c8 (branch `x20-boot`, on ace95b3): the ColdFire's byte push and pop of A7.
- The superproject's f783147 (branch `x20-boot`, on cb44f07): `RomLoader::acceptOsVariant`, `Bench` from an image in
  memory, the two traces, the tool, and the two submodule pointers.

The submodule commits are in neither fork yet (c0remusic/dsp56300-md-mm, the mc68k remote); they live in the
worktree's module repositories and, fetched, as branch `x20-boot` of `C:\dev\MModule\.git\modules\source\dsp56300`
and `...\mc68k`, so that removing the worktree keeps them. Nothing was pushed.

## Open

- The MCF5206e MAC unit in Musashi: the instruction forms X.20 uses, MACSR's modes, and a check of each against the
  CFPRM, since no hardware is at hand to compare with.
- What X.20 meets after the MAC: its first boot on a flash that 1.63 laid out (the readme speaks of converting
  storage), the absent +Drive, its MIDI readiness, the rendezvous of the DSP link with X.20's DSP programs.
- Whether X.20's DSP programs, past the loader, use the JIT's other missing instructions (ADC, SBC, NORM, EXTRACT...).
- The emulated boot time: 22 s of audio before the OS runs, about 7 s of wall clock per second of audio while the UC
  unpacks it; on a Machinedrum, unknown.
- Everything the ticket asked of the sound: TX8-BD under X.20 against `mdsPlay`'s render, the Pitch conversion, on
  which DSP X.20 runs User Machines.
