# Licence notice

MModule, the repository of MoDule, is built from Gearmulator (The Usual Suspects, GNU GPL v3, `LICENSE.md`) and,
since 2026-10-06, from Machinemodule's engine (sd88me, GNU AGPL v3, `source/elektron/md/mdEngine/LICENSE`). Its history
is gearmulator-md-drums' (c0remusic), filtered on 2026-10-17 to MoDule and what builds and tests it.

The GNU GPL v3 and the GNU AGPL v3 may be combined (section 13 of each). The combined work, the plug-in built from
this repository, is distributed under the GNU AGPL v3; each part keeps its own licence and copyright notices.

MoDule's skin bundles four faces of Inter 4.1 (The Inter Project Authors, SIL Open Font License 1.1,
`source/elektron/md/mdDrumsPlugin/skins/mdDrums/Inter-OFL.txt`), reduced to Latin-1 with tabular digits and a legacy
kerning table by `.scratch/md-drums-editor/tools/make_inter.py`. The font keeps its own licence.

The drum instrument's OPEN DRUM machines (`source/elektron/opendrum/`) are four voices ported from the LXR firmware's
sound engine, Copyright (C) 2012-2026 Erica Synths, from `erica-synths/lxr-desktop-app` at commit
`a43cd4fcda23ec3d577dc425261c3eb28bd5abff`, under the GNU GPL v3 only with the additional permission and terms of its
`NOTICE.md` (kept there as `NOTICE-upstream.md`); each file keeps its SPDX line and copyright notice, and its
`README.md` lists what was changed. Under those terms (GPL v3 section 7(e)) the names "Erica Synths", "LXR" and
"LXR-02" are not licensed: they appear here only to name the source, never in the instrument, which presents the voices
as a modified version under a name of its own and suggests no link with Erica Synths.

Elektron's firmware is not part of this repository: the plug-in needs the user's own Machinedrum flash image.
"Elektron" and "Machinedrum" are trademarks of Elektron Music Machines MAQ AB, used only to name the hardware.
