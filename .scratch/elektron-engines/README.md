# Elektron engines beyond the Machinedrum

Research for a multi-engine plug-in: MoDule's approach (each machine's own synthesis code, run without its OS) applied
to newer Elektron machines, with a per-track chain of our own (two filters, effects, layering) around those engines.

Status 2026-10-21: research only. Facts were read in xerydian/elektron-emu (commit 03961c6, reference clone in
`C:\dev\elektron-emu`). That repository has **no license**: nothing of its code enters MModule. What follows records
facts (processors, addresses, formats, measures), which we re-implement ourselves. A license was requested on
2026-10-21 in a comment on its commit 03961c6 (its issues and discussions are closed): no answer yet.

## What elektron-emu really emulates

| Machine | Processors | Emulated | Real sound? | Per-track tap |
|---|---|---|---|---|
| Syntakt OS 1.41 | ColdFire MAIN + ColdFire "DSP" (firmware section 7) + FPGA | both ColdFires, lock-step co-simulation; FPGA not | digital tracks 1-8 yes; analog 9-12 absent | none: only MAIN's SSI1 TX (Main L/R dry, Analog FX send) |
| Model:Cycles OS 1.13 | one ColdFire cfv4e, no DSP | the CPU; synthesis is firmware code on it | yes | none: SSI0 TX stereo only |
| Digitone II OS 1.11 | ColdFire + SHARC+ | SHARC code translated to C, compiled on the user's machine | yes (interpreter fallback ~0.5x real time) | to check |
| Digitakt II OS 1.16 | ColdFire + SHARC | ColdFire only; **SHARC not emulated** | **no**: native voice = resampled sample x gain, no filter, no effects, tanh master | yes, but of the native stand-in |
| Analog Rytm MKII OS 1.73 | ColdFire + FPGA + analog voices | ColdFire; FPGA frames feed a native model written by the author | **no**: own model (SVF, overdrive, sends) | yes (`RYTM_VOICE_OUT`), of the model |

Sources (paths in elektron-emu): Syntakt `st_engine.c:430`, `st_cosim.c:130-243`, `st_panel.inc.c:287-334`;
Cycles `engine.c:14-17`, Cycles patches README 205-210; Digitakt II `dt2_dspi.inc.c:1-4`, `dt2_audio.inc.c:328-384`;
Rytm `native/rytm_dsp.c:472-518`; Digitone II `dsp/dn_gensrc.inc:1-11`, README.

## Consequences

- Only three of the five give the machine's real sound: Syntakt (digital), Model:Cycles, Digitone II.
- None of those three is run piecewise: the whole OS boots and runs, the voices only sound inside it, and their output
  is the machine's mix. MoDule's approach (the OS's voice routines called alone, a tap per Track) would need the same
  reverse engineering the Machinedrum needed: where each machine's voice routine starts, what state it reads, where its
  output lands before the mix. elektron-emu gives the processor models and the boot, not that.
- Model:Cycles is the simplest target: one CPU, synthesis and mix in the same RAM, an output stage already located
  (mix x4 `mac.l` at 0x400565c6, then 0x40056bf6, clamp at 0x40056d00, TX buffer at 0x40056d1c, OS 1.13). Its firmware
  is installed on this computer.
- Syntakt adds a second CPU and a 1 MB shared mailbox (MAIN 0x10000000 = DSP 0x0); its per-track audio would be found in
  the DSP's memory before it reaches the mailbox. Needs OS 1.41, not yet here.
- Every address is tied to one OS version (profiles keyed by the MAIN section's SHA-256).
- The core is ColdFire with EMAC, not the 68000 of `source/mc68k/`: a ColdFire core is needed (Unicorn 2.1.4 with
  patches in elektron-emu, which we cannot take as they are).
- Factory samples and presets (Digitakt, Rytm, Model:Samples, Cycles' 512 presets) are not in any OS file: user-imported
  only.

## digikit (ElekLabs/digikit): Digitakt II and Digitone II, under AGPL

AGPL-3.0-or-later, so usable in MoDule, with section 7 terms: keep its NOTICE, show "digikit by Em D -
https://github.com/ElekLabs/digikit" in the About of any interface, mark changes, do not use the name "digikit".

- Targets Digitakt II OS 1.17 and Digitone II OS 1.12, the versions Elektron's site serves today.
- Emulates both the ColdFire and the SHARC+ DSP (where elektron-emu replaces the Digitakt II's SHARC with a stand-in),
  but its DSP does not run in real time yet ("audio plays slower than on the device").
- Reverse engineering documented in `docs/findings/` with evidence classes: the ColdFire-SHARC frame (trig mask 0x22,
  release mask 0x24), the per-track machine dispatch, the voice record contract, the effect stages and master, the
  6-tap polyphase voice render loop (`06-sharc-engine-and-startup.md`).
- Device facts (`.agents/skills/digikit-module/references/devices.md`): blocks of 32 samples at 48 kHz; on the Digitakt
  II 32 voice records (L and R of 16 tracks), the voice rendered then the stock filter, amp, track FX, sends and master;
  the stock DSP busy about 211,000 of about 666,000 instructions per block (Digitone II about 335,000).
- Its voice contract (`render` per voice per block, then the stock chain) is the cut MoDule needs: a voice alone, before
  the Track's chain.

What it changes: the Digitakt II becomes a real target (its SHARC code, not a stand-in), and with the Digitone II the
real-time question moves to the SHARC core's speed (elektron-emu compiles the Digitone II's SHARC code to C for that).

## CPU (elektron-emu's own figures)

- Syntakt: 0.83 s of wall time per emulated second on an M3 Pro, "100 % paced" live; MAIN 62-68 M instr/s.
- Model:Cycles: 40 M instr/s, 4.70 s CPU for 6.1 s emulated (~0.77 of a core).

A full OS per machine therefore costs most of a core; a piecewise engine is what made MoDule a tenth of the firmware.

## First ticket proposed

Model:Cycles, one Track alone: find the voice routine and the buffer one Track writes before the mix (from the output
stage backwards), run the OS once to a known Kit, then call that routine without the OS's scheduler and compare its
samples with the full OS's, as `mdEngineFirmwareTest` does for the Machinedrum. Done when one Machine of one Track
renders the same samples alone, with its CPU measured.
