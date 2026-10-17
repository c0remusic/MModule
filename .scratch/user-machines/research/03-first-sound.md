# A User Machine on a bare DSP56300: the first sound

The prototype of ticket 03, "Faire jouer TX8-BD sur un DSP56300 nu". Written 2026-10-17 against `main` at cb44f07.
`mdsPlay` (`source/elektron/md/mdEngine/tools/mds/`, target `mdsPlay`) reads a User Machine's `.syx` dump or `.mds`
package, loads it on a DSP56300 of this repository's emulator alone, and plays one Track: INIT, then TRIG on each Hit
and RENDER every block of 32 samples, as jmamma/MDS's `docs/MDX_MachineDumpStandard.txt` describes the host. It
writes a 24-bit mono WAV at 44.1 kHz and the cycles of each routine. The 43 User Machines of the user's X.20 B zip
were played from a scratch folder; nothing of them is in the repository.

Marks: **[V]** read at the place named; **[M]** measured here with `mdsPlay`; **[I]** inferred.

## In short

- Our reading of the ABI is enough to make User Machines sound: all 43 load, relocate, link and run INIT, TRIG and
  RENDER without a fault, and every one that makes its own sound does [M].
- The one thing the documents do not say outright: a package's program must sit in external memory, where P, X and Y
  are the same storage. TX8-BD reads its tables with `x:` moves at addresses inside its own code; loaded in internal P
  it played silence [M].
- Cost on the DSP: RENDER takes 1,000 to 3,400 cycles for most, inside the 3,951 of a Track. Four measure above their
  own declared figure, and TX9-SD above the limit (5,537 a block on average) [M]. Either the emulator's cycle count
  differs from the hardware's or those packages under-declare; X.20 checks only the declared figure [V].
- Cost on the host: 2 to 9 microseconds a block for one Track, 0.3 to 1.2 % of a core in real time [M]: 16 Tracks of
  the heaviest stay under 20 % of a core.
- Two conversions are guesses, Pitch and Decay; none of the 43 uses Decay, 15 use Pitch (the tonal ones: P-I, SYN-O,
  SYN-VP, TXZ) [M]. The other conversions are specified exactly and are what 161 raw-14, 85 unipolar, 18 index, 5
  bipolar and 5 boolean parameters use [M].

## 1. What the host does

- Memory: the program at P:$30000, above the bridge from $20000 where P, X and Y share storage; the Track's 64 X and
  64 Y words at $1000 (R6); the output buffer at Y:$0800 (R7, M7=$1F); the neighbour descriptor's address at
  Y:$07F0, pointing to four buffers; the scratch areas of imports 7 and 8 at X:$0000 and Y:$0000 (where SAMPLE_READ's
  documented X:$00-$1F and Y:$00-$1F are); the sine table of import 1 at X:$8000, 32,768 words of
  `sin(2 pi i / 32768)` at full scale; the rings of imports 2 to 5, silent; the external memory of import 6 at
  $40000, 1,536 words per Track. The documents leave these addresses to the host [V].
- Relocation adds the program's base to each flagged word; imports add their resource's address to the patched
  word's offset; a service import (kind 5) is refused, none of the 43 has one [V], [M].
- Each call is a stub that loads R6, R1 (Track 1), R7, M7 and M0-M5 as the documents list them, then `jsr` to the
  routine and a `jmp` to itself where the host stops [V].
- Conversions (`MdsPackage.cpp`, `convert`): raw 14-bit `setting << 7`; unipolar `setting << 16`; squared
  `setting^2 << 9`; bipolar `(setting - 64) << 17`; boolean; index `setting - min`: each matches the documents'
  examples (64 is 8192, 0.5, 0.25, zero) [V]. Pitch: the setting as a MIDI note at A=440 Hz, `P = f * 2^21 / 44100`
  [I, a guess]. Decay: 5 ms to 10 s to -60 dB [I, a guess].

## 2. TX8-BD

At its defaults (PTCH, DEC, TONE, ATK raw 14-bit 64; ACC 96 and SIGH 64 unipolar), 4 Hits every 0.5 s [M]:

- a bass drum at about 54 Hz falling to 50 Hz, peak -1.5 dBFS 4.3 ms after the Hit, the same on every Hit;
- INIT 32 cycles, TRIG 39, RENDER 1,292 a block on average and 1,473 at most, against 1,738 declared;
- the WAV is `TX8-BD.wav` in the session's scratchpad `mdsplay` folder (not in the repository).

## 3. The 43 User Machines at their defaults

- Every one runs without a fault [M].
- NFX (9 effects) and SYN-F (4 filters) process the four Tracks before theirs (NEIGHBOUR); alone, with silent
  neighbours, they are silent or below -69 dBFS, as they should be [M].
- The other 30 sound, peaks from -1.4 to -35 dBFS (TX8-CB the quietest) [M].
- RENDER above the declared figure: P-I-PN (3,177 against 975), TX9-BD (2,995 against 2,720), TX9-XT (3,040 against
  2,667), TX9-SD (5,537 on average and 6,691 at most against 1,542, over the 3,951 limit) [M]. TX9-SD's RENDER runs a
  subroutine from inside two nested loops of 8 under a carry test, so its cost depends on its state [M, from its
  disassembly].
- Host time per block for one Track: TX8-BD 1.97 us, P-I-EP 5.62, TXZ-PD 5.76, TX9-SD 6.06, TX8-CY 8.74 [M], on the
  user's PC with the JIT.

## Open

- Whether X.20 sounds the same: needs ticket 02 (X.20 booted in mdLib) or a Machinedrum. Pitch and Decay especially.
- Whether the emulator's cycle count matches the DSP's for code in external memory (wait states), and so whether
  TX9-SD really exceeds the limit on hardware.
- The listening: the user hears the renders and says whether they sound like the machines they are named after.
