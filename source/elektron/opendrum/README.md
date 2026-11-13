# OPEN DRUM

Four drum voices for the instrument's Tracks (`elektron::drums::Engine`, family OPEN DRUM): DRUM, SNARE, CYMBAL and
HIHAT, ported from the sound engine of a drum machine firmware that its maker publishes under the GNU GPL v3 only.

## Origin

- Source: `erica-synths/lxr-desktop-app`, commit `a43cd4fcda23ec3d577dc425261c3eb28bd5abff` (2026-10-23), the
  firmware's `Core/Src/Soundengine/DSPAudio/`, `Core/Inc/...`, `Core/Src/Preset/initKit.c` and the voices' cases of
  `Core/Src/Soundengine/MIDI/MidiParser.c`.
- Licence: GPL-3.0-only, with the additional permission and terms of the upstream `NOTICE.md`, kept here as
  `NOTICE-upstream.md`. Copyright (C) 2012-2026 Erica Synths. Each file taken keeps its SPDX line and copyright notice.
- Trademarks: the upstream names are not licensed and do not appear in the instrument; this is a modified version,
  presented under a name of its own, OPEN DRUM, and no link with the upstream maker is suggested.

## Changes (GPL-3.0 section 5(a)), 2026-11-11

- Every voice's state is passed to its functions (`DrumVoice*`, `SnareVoice*`, `CymbalVoice*`, `HiHatVoice*`)
  instead of the globals `voiceArray[3]`, `snareVoice`, `cymbalVoice`, `hatVoice`; the drum voice is voice 1 of three.
- Left out: the voices' LFO and velocity modulation nodes, the pan and the mixer, the dither, the unused `calcNoise`,
  the menu, sequencer, effects and hardware. The fader reads 1.
- The hardware random generator is a xorshift per voice (`GetRngValue` in `Oscillator.c`), which the voice's noise
  oscillators share.
- `midiParser_calcDetune` and `SEQ_DEFAULT_NOTE` moved into `Oscillator.c`; the voices' parameter cases of
  `midiParser_ccHandler` are `VoiceParameters.c`, by front panel parameter number.
- The wavetables hold a guard row of zeros (`[12][1024]`), which the interpolation reads past the last table, as the
  desktop plug-in `mcamaradev/ELABS-RLX-02-LXR02-CLONE-VST` has it; the MSVC fixes: fixed buffers in place of
  variable-length arrays, `distortion.c` without its C99 `inline`.
- Includes flattened to this folder; `stm32f7xx_hal.h` here is MModule's stand-in for the intrinsics.
- `openDrum.h` and `openDrum.c` are MModule's: a voice in the caller's memory, its init kit, Machine 1-24.
- 2026-11-13: `REAL_FS` is 44 100 Hz, the rate the instrument renders the voices at whatever the host's (its engine
  resamples), in place of the hardware's 44 140 Hz, at which the oscillators played 1.57 cents flat. The filters'
  coefficients do not read it (a cutoff is a fraction of the rate), nor the envelopes, which step per block.

edDrumsOpenDrumTest compares the port with renders of the original compiled with `REAL_FS` at 44 100 Hz as well
(`OPENDRUM_REFERENCES`, recorded out of the repository), and the drum voice's pitch with its oscillator's formula.
