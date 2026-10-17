MD Drums (preview) - macOS universal (Apple Silicon and Intel), VST3 and AU
==========================================================================

The Elektron Machinedrum's synthesis engines as a VST3 and AU instrument,
edited like Overbridge and sequenced by your DAW. The machines' own code from
OS 1.63 runs on emulated DSPs (the voice DSP and the mixer DSP's master
effects), without the Machinedrum's OS or sequencer.

THIS MAC BUILD IS UNTESTED. It was built and signed ad hoc by GitHub's macOS
runner, which has no Machinedrum flash image: nobody has heard it play on a
Mac yet. Reports are welcome, working or not:
https://github.com/c0remusic/MModule/issues

Source: https://github.com/c0remusic/MModule
Not affiliated with Elektron or with The Usual Suspects.


Requirements
------------

- macOS 11 or later, and a VST3 or AU host.
- Your own Machinedrum UW firmware: the 8 MB flash image with OS 1.63, the
  same one Gearmulator MD uses (an OS update in .syx won't do). Elektron owns
  it, so it doesn't come with MD Drums, which is meant for people who own the
  hardware; please don't ask for it.


Install
-------

The installer package (MD-Drums-...-macOS-universal.pkg, beside this zip on
the release page) puts both into /Library/Audio/Plug-Ins/. Apple hasn't
notarized it, so macOS refuses it the first time: open System Settings >
Privacy & Security, scroll down to the message about the package, click
"Open Anyway" and confirm with your password. On macOS 15 Sequoia that is the
only way through: right-click > Open no longer gets past Gatekeeper.

Or by hand, from this zip:

1. Copy "MD Drums.vst3" to ~/Library/Audio/Plug-Ins/VST3/ and, for AU hosts,
   "MD Drums.component" to ~/Library/Audio/Plug-Ins/Components/
2. The bundles are signed ad hoc, not notarized: macOS blocks them until you
   remove the quarantine flag, in Terminal:
     xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/"MD Drums.vst3"
     xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/"MD Drums.component"

Then, either way:

3. Rescan plug-ins in your host and load "MD Drums" (c0remusic).
4. Give it the flash image: click "No Machinedrum flash image: choose the
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
- 608 host parameters. Live shows none until you add them (Configure mode).

See the Windows build's README and docs/md-drums-push2.md in the source for the
rest: the editor, the Kits, the Outs, Push 2.


Licence
-------

The plug-in combines Gearmulator (GNU GPL v3) and Machinemodule's engine
(sd88me, GNU AGPL v3), and is distributed under the GNU AGPL v3. See NOTICE.md,
LICENSE.md (GPL v3), LICENSE-AGPL.txt (AGPL v3) and Inter-OFL.txt (the Inter
font, SIL Open Font License 1.1).

"Elektron" and "Machinedrum" are trademarks of Elektron Music Machines MAQ AB,
used only to name the hardware.
