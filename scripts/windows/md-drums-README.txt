MD Drums (preview) - Windows x64, VST3
======================================

The Elektron Machinedrum's synthesis engines as a VST3 instrument, edited like
Overbridge and sequenced by your DAW. The machines' own code from OS 1.63 runs
on emulated DSPs (the voice DSP and the mixer DSP's master effects), without
the Machinedrum's OS or sequencer.

Tested in Ableton Live 12 on Windows 11. Reports are welcome:
https://github.com/c0remusic/MModule/issues

Source: https://github.com/c0remusic/MModule
Not affiliated with Elektron or with The Usual Suspects.


Requirements
------------

- Windows 10 or 11, x64, and a VST3 host.
- Your own Machinedrum UW firmware: the 8 MB flash image with OS 1.63, the
  same one Gearmulator MD uses (an OS update in .syx won't do). Elektron owns
  it, so it doesn't come with MD Drums, which is meant for people who own the
  hardware; please don't ask for it.


Install
-------

1. Run the installer, or copy the zip's "MD Drums.vst3" folder to
   C:\Program Files\Common Files\VST3\ yourself. The installer isn't signed:
   if SmartScreen says it protected your PC, click "More info", then
   "Run anyway".
2. Rescan plug-ins in your host and load "MD Drums" (c0remusic) as an
   instrument.
3. Give it the flash image: click "No Machinedrum flash image: choose the
   file" in its top bar (or right-click the editor and choose "Load
   Machinedrum flash image...") and pick your file, whatever its name. MD
   Drums checks it, plays at once and keeps a copy in its own folder, so it
   finds it on its own next time.


Playing
-------

- Notes 36-51 (C1 to D#2 in Live) on any channel play Tracks 1-16; velocity
  scales the level.
- Locks: notes 64-87 set SYN1 ... LFOM for one Hit, the velocity carrying the
  value: on the Track's channel, or on channel 1 right after the Hit note.
- 608 host parameters. Live shows none until you add them (Configure mode, or
  the Push list: right-click the editor, then "Prepare Push list").
- Outputs: Main (stereo) plus Out 01-15 (mono); any Track can go to any Out.
  Live opens at most 16 output buses per plug-in.
- Kits: a Bank of 64 Slots, seeded with the 16 factory Kits, shared by every
  instance; Import and Export .syx Kit dumps.
- Push 2: docs/md-drums-push2.md in the source.


Licence
-------

The plug-in combines Gearmulator (GNU GPL v3) and Machinemodule's engine
(sd88me, GNU AGPL v3), and is distributed under the GNU AGPL v3. See NOTICE.md,
LICENSE.md (GPL v3), LICENSE-AGPL.txt (AGPL v3) and Inter-OFL.txt (the Inter
font, SIL Open Font License 1.1).

"Elektron" and "Machinedrum" are trademarks of Elektron Music Machines MAQ AB,
used only to name the hardware.
