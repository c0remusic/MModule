# Syntakt OS 1.41: where each machine runs

Read-only research, 2026-10-22, on `C:\dev\elektron-work\syntakt-1.41\` (section 3 = CPU #1, loads at 0x40000400;
section 7 = voice CPU #2, loads at 0x40000400). Addresses are CPU addresses. Scripts:
`C:\dev\elektron-work\scratch-arch\mach\` (`tbl.py`, `allow2.py`, `rec.py`, `amt.py`, `lin.py`, `refs.py`, `callers.py`).
Guesses are marked [INFER].

## Summary

- The Syntakt's machines split exactly in two. The 12 that CPU #2 renders are the only ones allowed on the digital
  tracks 1-8. Every other sound machine is allowed only on the analog tracks 9-12, and has its own routine on CPU #1,
  which computes control voltages and switch bits for the analog voices each block. Nothing renders them digitally.
- So SY RAW, SY CHIP, BD ACOUSTIC, SD ACOUSTIC and HH LAB are analog machines. The zeros in CPU #2's map
  (0x40014950) are never reached in normal use, because CPU #1 refuses those machines on tracks 1-8.
- Section 2 is not a DSP. SyntHack's `firmware-format.md` calls it the bootstrap (the startup menu and OS upgrade, in
  ColdFire code). Sections 1 and 8 are FPGA bitstreams [INFER for 8].

## Machine ids (CPU #1 name table 0x4022AF88, 8 bytes per id: long name, short name)

There are 45 ids, 0-44. The getters are 0x400A668C (long name, id <= 0x2C) and 0x400A66AC (short name).

| id | machine | id | machine | id | machine |
|---|---|---|---|---|---|
| 0 | BD MODERN | 15 | SD CLASSIC | 30 | CY RIDE |
| 1 | SD BASIC | 16 | SD FM | 31 | HH BASIC |
| 2 | CY ALLOY | 17 | SD NATURAL | 32 | UT NOISE |
| 3 | PC CARBON | 18 | RS HARD | 33 | UT IMPULSE |
| 4 | SY TONE | 19 | RS CLASSIC | 34 | DISABLE ("NONE") |
| 5 | SY CHORD | 20 | SY DUAL VCO | 35 | **MIDI** |
| 6 | SD VINTAGE | 21 | CP CLASSIC | 36 | SY TOY |
| 7 | CP VINTAGE | 22 | CH CLASSIC | 37 | SY BITS |
| 8 | BD HARD | 23 | OH CLASSIC | 38 | SY SWARM |
| 9 | BD CLASSIC | 24 | CY CLASSIC | 39 | SY RAW |
| 10 | BD FM | 25 | CB CLASSIC | 40 | SY CHIP |
| 11 | BD PLASTIC | 26 | CH METALLIC | 41 | BD ACOUSTIC |
| 12 | BD SILKY | 27 | OH METALLIC | 42 | SD ACOUSTIC |
| 13 | BD SHARP | 28 | CY METALLIC | 43 | HH LAB |
| 14 | SD HARD | 29 | CB METALLIC | 44 | SP TWINSHOT |

Id 35 is MIDI, not a sound machine. CPU #2's 46-byte map has no id 45: its last entry (0) is unused.

CPU #2's map at 0x40014950 (46 bytes, checked): ids 0-7 go to engines 0-7, ids 36, 37 and 38 to engines 8, 9 and 10,
and id 44 to engine 11. Every other id maps to 0.

## Q1: which machine goes on which track

**The check.** 0x400A647A(machine, t) returns `(word[0x4022C93A + 4*machine] >> t) & 1`. It refuses
machine > 44 and t > 11. Here t is an *internal* track index. The user track is turned into it by 0x400A6506, through
the table 0x4022C9EC = {4,5,6,7,8,9,10,11,0,1,2,3}: user tracks 1-8 become internal 4-11, and user tracks 9-12 become
internal 0-3. 0x40024A1E(sound, user track) wraps both calls.

Other places reach the check too:
- 0x400A31A2, which loads a sound onto a track. It copies the sound's 142 bytes into the engine copy
  0x800021CC + 142*t only when the check passes.
- Nine call sites in all: 0x4000EC18, 0x40024A0C, 0x400255CE, 0x40025808, 0x40025A16, 0x4008BAF6, 0x400A31C6,
  0x400A94CE and 0x400E7D68.

The records at 0x4022C938 are 4 bytes per machine: byte 0 is a family, and the word at +2 is the track mask.

| ids | mask | user tracks | family byte |
|---|---|---|---|
| 0-7, 36, 37, 38, 44 (CPU #2's 12 engines) | 0x0FF0 | 1-8 | 0-7 for ids 0-7; 0 for 36-38 and 44 |
| 8-20, 39-42 (BD x6, SD x4, RS x2, SY DUAL VCO, SY RAW, SY CHIP, BD/SD ACOUSTIC) | 0x0007 | 9, 10, 11 | 0 BD-like (with DVCO, RAW, CHIP, ACOUSTICs), 1 SD (14-17), 2 RS (18-19); SD ACOUSTIC is 0 |
| 22-31, 43 (CH, OH, CY, CB CLASSIC and METALLIC, CY RIDE, HH BASIC, HH LAB) | 0x0008 | 12 | 3 |
| 32, 33, 34 (UT NOISE, UT IMPULSE, DISABLE) | 0x000F | 9-12 | 0xFF |
| 35 MIDI | 0x0FFF | 1-12 | 0xFF |
| 21 CP CLASSIC | 0x0000 | none | 3 |

- CP CLASSIC is in the table but allowed on no track. Its analog routine is null (see below): it is dead in 1.41.
- 0x400A63C4 is an "is digital" predicate: it is true when id == 35 or the mask == 0x0FF0. Its caller is 0x400A959A.
- Defaults:
  - Per user track, at 0x4022C8D8: tracks 1-8 get BD MODERN, SD VINTAGE, CP VINTAGE, CY ALLOY, SY TONE, SY CHORD,
    SY BITS, SY TOY; tracks 9-12 get BD HARD, SD CLASSIC, SY DUAL VCO, CY RIDE.
  - Per internal track, at 0x4022C8A8: 8, 14, 20, 24, then 0-7.
- [INFER] Tracks 9-11 are three identical, flexible analog voices: two VCOs (calibration strings XD_SIN1, XD_SAW1,
  XD_SQR1, XD_SIN2..., OSCPWM, near 0x40253520), noise, a filter and a VCA. Track 12 is a different voice made for
  metal and noise sounds. The self-test strings VOICE_1 to VOICE_4 and "ERROR ON VOICE %d" match these four voices.

## Q2: where the five "missing" machines run

- They are analog-track machines. SY RAW, SY CHIP, BD ACOUSTIC and SD ACOUSTIC go on tracks 9-11, and HH LAB on
  track 12.
- They never reach CPU #2. In the 0x1B0 block builder, the loop at 0x400A4C7A-0x400A4D74 runs t = 0..7 over the
  digital tracks and copies the 20 machine bytes unchanged from the engine copy (+0x3E) to 0x80002000 + 20t.
  The id is not remapped.
- The only test on the id there: when id != 37 (SY BITS) and id != 44 (SP TWINSHOT), it writes 0x7F00 - word[+0x48]
  into byte 10 of the block, at 0x400A4CA8.

**The per-machine analog routine table is at 0x402A1B34: 45 longs, indexed by id.**

| id | routine | id | routine | id | routine |
|---|---|---|---|---|---|
| 8 BD HARD | 0x400989FA | 19 RS CLASSIC | 0x4009C7B2 | 30 CY RIDE | 0x4009B162 |
| 9 BD CLASSIC | 0x40097C8A | 20 SY DUAL VCO | 0x4009F146 | 31 HH BASIC | 0x4009BE28 |
| 10 BD FM | 0x4009830E | 21 CP CLASSIC | **0 (null)** | 32 UT NOISE | 0x4009B6C4 |
| 11 BD PLASTIC | 0x400996BC | 22 CH CLASSIC | 0x4009BBA4 | 33 UT IMPULSE | 0x4009B5A4 |
| 12 BD SILKY | 0x400990A6 | 23 OH CLASSIC | 0x4009C260 | 34 DISABLE | 0x4009B544 |
| 13 BD SHARP | 0x40099EE2 | 24 CY CLASSIC | 0x4009AE3C | 35 MIDI | 0x4009B544 (same as DISABLE) |
| 14 SD HARD | 0x4009DD0A | 25 CB CLASSIC | 0x4009A810 | 39 SY RAW | 0x4009F986 |
| 15 SD CLASSIC | 0x4009D1DE | 26 CH METALLIC | 0x4009B998 | 40 SY CHIP | 0x400A0096 |
| 16 SD FM | 0x4009D7A2 | 27 OH METALLIC | 0x4009C070 | 41 BD ACOUSTIC | 0x400A0C3A |
| 17 SD NATURAL | 0x4009E28A | 28 CY METALLIC | 0x4009ABA0 | 42 SD ACOUSTIC | 0x4009E7DA |
| 18 RS HARD | 0x4009CC9E | 29 CB METALLIC | 0x4009A574 | 43 HH LAB | 0x4009C440 |

Ids 0-7, 36, 37, 38 and 44 are null: CPU #2's engines.

**The dispatch** is at 0x400A4E44-0x400A4EB4, inside the audio interrupt 0x400A3856. For each analog voice v = 0..3,
in order:
1. The track is t = byte[0x402A1DE8 + v] = 8, 9, 10, 11 (user tracks 9-12, in the engine copy's order).
2. On a trig of t (bit t of d3), it reads the machine m = byte[0x80002F50 + t]. When 8 <= m <= 44, it latches
   fn = table[m] and the parameter pointer 0x800021B0 + 142t + 0x3E (the 20 machine bytes).
3. Then, **every block**, it calls `fn(v, 0x80003160, params, long[0x80004688 + 4t])`.

As on CPU #2, a machine change takes effect on a trig.

## Q3: what the analog machines are made of

**The analog circuits make the sound.** CPU #1 does the control work, per block. Here is what it does, as read in
BD HARD 0x400989FA:

- **Switch bits:** it sets and clears bits of a bit array at 0x80003160 + 0xA0, whose bit numbers come from per-voice
  tables (0x401E5D68, 0x401E5D40 and 0x401E5D18 for voices 0, 1 and 2). [INFER] These are the analog switches that
  give the flexible voice its topology for this machine: oscillator waves, noise, routing.
- **Control values:** it computes values with EMAC Q31 arithmetic from the machine's parameter words, into the voice's
  part of the struct (for example +0x2E0/+0x2F0/+0x300 and +0x118/+0x138/+0x158 per voice).
- **Envelope generator:** it keeps per-voice state at 0x80005160 (stride 0x20), which every analog routine
  references. The amp envelope state machine at 0x40090000-0x400903C8 writes pairs of (level, 0x30000000 / 0xB0000000
  / 0xB0100000 | 20-bit rate) command words, with rates from the tables 0x401DC3E0, 0x401DBFE0 and 0x401DC1E0, into a
  hardware envelope generator. [INFER] That generator is in the FPGA (section 1).

**Transport:** 0x80003160-0x80003547 is a 1000-byte struct (500 16-bit words). At 0x400A5304, every block,
0x40101FF0(0x80003160) wraps each word as a DSPI command word (0x8001xxxx), frames them with 0x8000AAAA and
0x08005555, and double-buffers them at 0x800051E0 + 0x830*n. This is SyntHack's "40 CVs over DSPI1, eDMA ch. 15";
0x4029A040 is its initial or live copy, sent at boot from 0x4008E384.

**The audio comes back digitized:** ADC channels 0-3 (SyntHack's buffer 0x80003950) carry the analog tracks, for the
sends and Overbridge.

**A software model needs two things:**
1. A port of these 30 CPU #1 routines, their tables and the envelope state machine (ColdFire + EMAC code, the same
   oracle as for CPU #2).
2. A model of the analog voices themselves: three flexible voices and one metal/HH voice, including how they answer
   the switch bits, the CVs and the envelope generator's commands.

There is no digital sample path to port for them. [INFER] Calibration data (XDTUNE and others) also shapes the CVs per
unit.

## Q4: other sound not yet ported

- **CPU #1, digital:**
  - Delay 0x40096890 and reverb 0x40096E3C.
  - Track sends and pans (0x4008F21E, constant power at 0x400A171A), the summing routines 0x4008EF14-0x4008F13A, and
    the digital filter cutoff of tracks 1-8 (0x400956BC).
  - The Overbridge stream assembly at 0x80004760.
  - 0x4008EE54, 0x400962E8 and 0x40095DA4, called right after the analog dispatch [INFER: FX track and analog-block
    control].
- **Analog:**
  - The FX track: an analog filter and drive, with a hardware envelope (rate tables 0x401DE254, 0x401DE054). The
    digital tracks routed to it go out through 0x8000DFD0 and a DAC.
  - The analog mixer: master VCAs (CVs 35/36), master filter (CVs 37/38), external in.
  - The final mix is analog: nothing digital follows the master VCAs.
- **CPU #2:** the per-track filter and amp after each engine (see `trackstages.md`).

## Unknowns

- What each switch bit and each CV word of the 1000-byte struct drives in the circuit: the bit tables at 0x401E5D18+
  and the per-voice offsets are not decoded.
- The FPGA's role: the envelope generator, and whether it also generates oscillator or noise signals.
- Track 12's voice topology, compared with tracks 9-11.
- What the call into 0x400A2792, made before the dispatch, does.
- What CPU #2 does with MIDI (id 35) on a digital track. It maps to engine 0 there. [INFER] The voice is muted or never
  triggered by CPU #1.
- Whether the Syntakt manual names the analog machines as listed here. Not checked against the manual.
