# Analog voices of the Syntakt: literature and plan

Research note, 2026-10-22. What is public for modelling the Syntakt's analog tracks (9-12), its analog FX block
and its analog mixer, and how to fit models once a Syntakt can be recorded. Read `machines.md` first: CPU #1
computes CVs, switch bits and envelope-generator commands per block, and the circuits make the sound. Port the
CPU #1 routines bit-exact, so the circuit model only has to map **CV words + switch bits + envelope commands
to audio**. That is a much narrower problem than "map knob values to audio".

Tags: **[DOC]** manufacturer document; **[PAPER]** peer-reviewed or thesis; **[CODE]** source code;
**[FORUM]** user claim, not checked; **[INFER]** my guess. "Link not opened" means I took the URL from search
results or from memory and did not load it.

## 0. What Elektron documents (primary)

- **Syntakt User Manual, OS 1.42** [DOC] (Elektron, 2026),
  https://www.elektron.se/wp-content/uploads/2026/10/Syntakt-User-Manual_ENG_OS1.42_260930.pdf.
  Appendix A.3/A.4 (pp. 98-111) describe every analog machine's parameters. Points that constrain topology:
  - BD HARD/CLASSIC/SHARP/ACOUSTIC: one oscillator with selectable waveform (sine, asymmetric sine, triangle;
    SHARP and ACOUSTIC add sinetooth, saw, square), free-running or reset on trig, a pitch sweep (STIM, SDEP),
    a hold then decay, a tick/snap transient, and OVER (gain into the analog overdrive). So these are
    **VCO + envelope** voices, not bridged-T resonators [INFER from the parameter set].
  - BD FM, SD FM: two oscillators with linear FM, separate decays. BD PLASTIC: FM or FM + ring mod.
  - BD/SD ACOUSTIC: IMP is "the level of the digital transient… a mix of PCM and noise", so these two have
    a digital part mixed in.
  - SD NATURAL: noise with its own HPF, LPF and resonance. SD CLASSIC: two detuned oscillators + snap + noise.
  - SY DUAL VCO: two oscillators, mix/ring/FM/FM+ring, and FM "does not pitch track 100% correct". That points
    to analog linear FM. SY RAW: osc 2 "ring modulates oscillator 1 using a square wave". SY CHIP: a fast
    arpeggio over analog waveforms, plus digital SID-style noise and digital PWM.
  - Track 12: HH BASIC and HH LAB use **six oscillators** (HH LAB tunes each one). CY RIDE's TYPE selects
    "fundamental oscillator frequencies, as well as different inter-oscillator logical interaction". CY
    METALLIC uses "ring-modulated oscillators". CB uses two oscillators. That is the TR-808/909 metal recipe:
    square oscillators combined by logic or ring mod, then filters [INFER].
  - The analog tracks' filter types are Lowpass, Bandpass, Highpass, Notch, Lowpass shelf, Peak, Highpass
    shelf, "all analog circuits" (§10.5.7). The FX block's filter types are 4-pole LP, 2-pole LP, BP, HP,
    Notch, Double-Notch, LP-Notch, with an ADSR plus delay and a stereo frequency offset (FPAN), then an
    analog drive and an amplifier (§5.4, §12.3-12.6). The FX block can take the **external inputs** (§5.4.1):
    that is your way to drive the FX filter and drive with test signals.
  - The calibration menu (§14.9.4) retunes "the analog oscillators" for temperature and keeps up to two user
    data sets. So the VCOs are real exponential VCOs with temperature drift.
  - Specifications: 48 kHz, 24-bit A/D and D/A.
- **Analog Rytm MKII User Manual, OS 1.74** [DOC] (Elektron, 2026),
  https://www.elektron.se/wp-content/uploads/2026/10/Analog-Rytm-MKII-User-Manual_ENG_OS1.74_260930.pdf.
  It describes the same machine families on the Rytm's eight voices. Each Rytm voice has its own 2-pole
  multimode filter and overdrive (Elektron product page, https://www.elektron.se/en/analog-rytm-mkii).
  Rytm owners' recordings and sample packs are a second source of reference audio.
- **Overbridge User Manual** [DOC] (Elektron, 2026),
  https://www.elektron.se/wp-content/uploads/2026/08/Overbridge-User-Manual_260826.pdf. It covers the
  per-track audio streams. `machines.md` reads ADC channels 0-3 as the analog tracks digitized. [INFER] The
  Overbridge track streams of tracks 9-12 are those ADC captures, taken before the FX block and the analog
  mixer. Check this by recording the same hit on its track stream and on the main out.

## 1. Physically-informed and circuit models of analog drum voices

- **Werner, Abel, Smith, "A Physically-Informed, Circuit-Bendable, Digital Model of the Roland TR-808 Bass
  Drum Circuit"**, DAFx-14, 2014 [PAPER],
  https://dafx14.fau.de/papers/dafx14_kurt_james_werner_a_physically_informed,_ci.pdf. It splits the circuit
  into sub-circuits (trigger shaping, bridged-T resonator with its pitch-glide feedback, tone, VCA) and models
  each one physically. It is the reference method for "model each block, keep its controls physical", which
  is what a CV-driven Syntakt voice needs.
- **Werner, Abel, Smith, "The TR-808 Cymbal: a Physically-Informed, Circuit-Bendable, Digital Model"**,
  ICMC/SMC 2014 [PAPER], https://speech.di.uoa.gr/ICMC-SMC-2014/images/VOL_2/1453.pdf. It covers the six
  Schmitt-trigger square oscillators, the band-pass filter bank, and the envelope VCAs for each band. This is
  the closest published model of track 12's HH/CY machines.
- **Werner, Abel, Smith, "More Cowbell: a Physically-Informed, Circuit-Bendable, Digital Model of the TR-808
  Cowbell"**, AES 137th Convention, paper 9207, 2014 [PAPER], https://aes2.org/publications/elibrary-page/?id=17530
  (AES members only; abstract at
  https://ccrma.stanford.edu/papers/more-cowbell-physically-informed-circuit-bendable-digital-model-of-tr-808-cowbell).
  It covers two square oscillators into a band-pass and a VCA. Use it for CB CLASSIC/METALLIC.
- **Werner, "Virtual Analog Modeling of Audio Circuitry Using Wave Digital Filters"**, PhD thesis, Stanford,
  2016 [PAPER], https://purl.stanford.edu/jy057cz8322. It covers wave digital filters (WDF) for arbitrary
  topologies and multiple nonlinearities, with the full 808 bass drum as its case study. Read it if you
  white-box any sub-circuit.
- **Wolfram blog, "Digital Vintage Sound: Modeling Analog Drums with the Wolfram Language and System
  Modeler"**, 2021, https://blog.wolfram.com/2021/12/29/digital-vintage-sound-modeling-analog-drums-with-the-wolfram-language-and-system-modeler/.
  It models the 808 kick at component level, simplifies it, and compares each version with hardware. It is a
  good worked example of trading fidelity against simplification.
- **D. Schmidt, TR-606 bass drum notes**, https://users.monash.edu/~dschmidt/tr606/bd.html. A plain-language
  analysis of the bridged-T as a pinged 2-pole band-pass. Use it with UT IMPULSE (an impulse that "pings" the
  filter, manual p. 104).
- **Gordon Reid, Synth Secrets, Sound On Sound**, 2002: "Practical Bass Drum Synthesis" (Feb), "Practical
  Snare Drum Synthesis" (Apr, https://www.soundonsound.com/techniques/practical-snare-drum-synthesis),
  "Practical Cymbal Synthesis" (Jul). Series index: https://www.soundonsound.com/series/synth-secrets-sound-sound.
  These are not papers. They give the block diagrams of the 808/909 voices (two-oscillator snare plus
  filtered noise, the six-square-wave metal), which match the Syntakt's parameter sets.
- **TR-808 and TR-909 service notes** (Roland, 1980-1983) are widely mirrored, e.g. on archive.org (no link
  checked). They carry the full schematics that the papers above analyse. No Syntakt counterpart exists.
- **Clap and 909:** I found no peer-reviewed circuit model of the 808 clap or the 909 voices. The service
  notes plus Synth Secrets are the sources. CP CLASSIC is dead in the Syntakt's OS anyway (`machines.md`).

## 2. Grey-box and black-box fitting when the schematic is unknown

- **Esqueda, Kuznetsov, Parker, "Differentiable White-Box Virtual Analog Modeling"**, DAFx20in21, 2021
  [PAPER], https://dafx2020.mdw.ac.at/Proceedings/papers/DAFx20in21_paper_39.pdf. It writes a circuit model
  in differentiable form and learns its component values from input/output audio. **This is the core method
  here**: guess a topology, make it differentiable, fit it to Overbridge recordings, then export fixed
  coefficients to C++. Runtime cost is that of the white-box model.
- **Parker, Esqueda, Bergner, "Modelling of Nonlinear State-Space Systems Using a Deep Neural Network"**,
  DAFx-19, 2019 [PAPER], https://dafx.de/paper-archive/2019/DAFx2019_paper_42.pdf. A discrete state-space
  model with a small MLP for the nonlinearity, shown on diode clippers and the MS-20 filter. Use it when the
  topology is roughly known but the nonlinearity is not (overdrive, resonant filter). The cost is a few small
  matrix products per sample, real-time on one core [INFER from the network size].
- **Wright, Damskägg, Välimäki, "Real-Time Black-Box Modelling with Recurrent Neural Networks"**, DAFx-19,
  2019 [PAPER], https://acris.aalto.fi/ws/portalfiles/portal/36768418/ELEC_Wright_Real_time_black_box_DAFx2019.pdf;
  and **Wright, Damskägg, Juvela, Välimäki, "Real-Time Guitar Amplifier Emulation with Deep Learning"**,
  Applied Sciences 10(3) 766, 2020 [PAPER],
  https://acris.aalto.fi/ws/portalfiles/portal/41964332/Real_time_guitar_amplifier_emulation.pdf. Small
  LSTM/GRU networks match WaveNet accuracy at much lower CPU cost, and about 3 minutes of training audio is
  enough. This is the black-box fallback for the FX drive and the per-voice OVER stage. A static network
  cannot follow time-varying knobs, so condition it on the control value.
- **Steinmetz, Reiss, "Efficient Neural Networks for Real-Time Modeling of Analog Dynamic Range
  Compression"**, AES 152, 2022 [PAPER], https://arxiv.org/abs/2102.06200. A causal TCN with sparse,
  fast-growing dilations runs in real time on a CPU from about 10 minutes of data. Use it if a stage has long
  memory (envelope-following saturation).
- **Wilczek et al., "Virtual Analog Modeling of Distortion Circuits Using Neural Ordinary Differential
  Equations"**, DAFx-22, 2022 [PAPER], https://arxiv.org/abs/2205.01897. A continuous-time alternative,
  useful when one model must run at several sample rates.
- **Engel et al., "DDSP: Differentiable Digital Signal Processing"**, ICLR 2020 [PAPER],
  https://arxiv.org/abs/2001.04643; code https://github.com/magenta/ddsp (Apache-2.0) [CODE]. The base
  toolkit: differentiable oscillators, filtered noise and multi-scale spectral loss.
- **Hayes, Shier, Fazekas, McPherson, Saitis, "A Review of Differentiable Digital Signal Processing for Music
  and Speech Synthesis"**, Frontiers in Signal Processing, 2024 [PAPER], https://arxiv.org/abs/2308.15422
  (link not opened). A survey covering the known pitfalls: losses for oscillator frequency are badly behaved
  and need good initial values. That is a reason to start frequencies from spectral peak-picking.
- **Shier et al., "Differentiable Modelling of Percussive Audio with Transient and Spectral Synthesis"**,
  Forum Acusticum 2023 [PAPER], https://arxiv.org/pdf/2309.06649; code https://github.com/jorshi/drumblender
  [CODE]. Sinusoids plus noise plus a learned transient, for drums. Use it to pick losses and onset handling.
  Its model is not meant for a real-time plug-in.
- **Shier et al., "Real-time Timbre Remapping with Differentiable DSP"**, NIME 2024 [PAPER],
  https://nime.org/proc/nime2024_55/; project https://instrumentslab.org/project/torchdrum [CODE]. An
  808-style differentiable drum synth in PyTorch with a feature-difference loss over the whole hit. It is a
  ready scaffold for fitting envelope and sweep parameters.
- **Chowdhury, "RTNeural: Fast Neural Inferencing for Real-Time Systems"**, 2021, https://arxiv.org/abs/2106.03037
  (link not opened), BSD-3 [CODE]. A C++ inference engine for the RNN fallbacks. Neural Amp Modeler
  (S. Atkinson, https://github.com/sdatkinson/neural-amp-modeler, MIT, link not opened) shows the
  record-and-train workflow end to end.

CPU guide [INFER from the papers' reported loads]: a white-box or state-space voice costs tens of
operations per sample (negligible ×4 voices). A GRU/LSTM with 8-32 units costs a few percent of a core per
channel at 48 kHz. A WaveNet or TCN costs 5-20× more. Keep neural parts to the nonlinear stages.

## 3. Filters, drives and discretisation

- **Zavalishin, "The Art of VA Filter Design"**, rev. 2.1.0 2018 / 2.1.2 2020 [PAPER, book],
  https://www.native-instruments.com/fileadmin/ni_media/downloads/pdf/VAFilterDesign_2.1.0.pdf. It covers
  TPT/zero-delay-feedback integrators, the SVF, the ladder, Sallen-Key and OTA cascades, and how to solve
  their nonlinear feedback. **Read first.** Its SVF gives the analog tracks' LP/BP/HP/Notch/shelf/peak set
  from one structure [INFER that the hardware uses one SVF-like core].
- **Stilson, Smith, "Analyzing the Moog VCF with Considerations for Digital Implementation"**, ICMC 1996
  [PAPER], https://ccrma.stanford.edu/~stilti/papers/moogvcf.pdf (link not opened). The classic paper on the
  delay-free loop and on tuning compensation.
- **Huovilainen, "Non-Linear Digital Implementation of the Moog Ladder Filter"**, DAFx-04, 2004 [PAPER],
  https://dafx.de/paper-archive/2004/P_061.PDF (link not opened). A tanh per stage, oversampled. The standard
  model for a resonant 4-pole LP like the FX block's.
- **Välimäki, Huovilainen, "Oscillator and Filter Algorithms for Virtual Analog Synthesis"**, Computer Music
  Journal 30(2), 2006 [PAPER], doi 10.1162/comj.2006.30.2.19. Alias-reduced oscillators and the ladder in one
  place.
- **D'Angelo, Välimäki, "Generalized Moog Ladder Filter" Parts I-II**, IEEE/ACM TASLP, 2014 [PAPER] (no link
  checked). Ladders of N stages and a more accurate transistor model.
- **T. Stinchcombe**, circuit analyses of the Moog ladder, Korg MS-10/20 and diode ladders,
  https://www.timstinchcombe.co.uk/ (link not opened). Exact transfer functions for diode and OTA topologies.
  Compare them with measured FX-filter responses to name the topology.
- **Parker, Zavalishin, Le Bivic, "Reducing the Aliasing of Nonlinear Waveshaping Using Continuous-Time
  Convolution"**, DAFx-16, 2016; **Bilbao, Esqueda, Parker, Välimäki, "Antiderivative Antialiasing for
  Memoryless Nonlinearities"**, IEEE SPL, 2017 [PAPER] (no links checked). Anti-derivative antialiasing (ADAA)
  for the overdrives and for the triangle-to-sine shapers, without heavy oversampling.
- **Holters, Zölzer, "Physical Modelling of a Wah-Wah Effect Pedal as a Case Study for Application of the
  Nodal DK Method"**, DAFx-11, 2011 [PAPER]; **D. Yeh, "Digital Implementation of Musical Distortion Circuits
  by Analysis and Simulation"**, PhD thesis, Stanford, 2009 [PAPER] (no links checked). State-space (DK)
  modelling, the alternative to WDF for a drive circuit you can guess.
- **[FORUM] Analog Four teardown**, Elektronauts, https://www.elektronauts.com/t/a4-teardown-and-infrared-pics/928.
  Photos name a CoolAudio V2164M quad VCA (two per voice), TL084 op-amps and an LM339 comparator in the
  Analog Four. [INFER] The Syntakt, from the same design team, likely uses V2164-class VCAs (exponential
  gain law, which you can fit) and op-amp/OTA filters. Not confirmed for the Syntakt.

## 4. Public material on the Syntakt and Rytm analog hardware

- **No Syntakt or Analog Rytm schematic, teardown with chip names, or measurement study turned up.** Searches
  on Elektronauts, Gearspace and ModWiggler found only marketing and listening impressions.
- **[FORUM, staff-voiced] "Machines overlap"**, Elektronauts, user _Niko, 2022-04-23,
  https://elektronauts.com/t/machines-overlap/171185. Quote: "The analog machines are all from rytm". The same
  post says they differ in circuit design and firmware. The poster writes as Elektron ("we") but is not
  labelled staff. Implication: Rytm recordings are useful priors, not ground truth.
- **[FORUM] "HH LAB tips"**, Elektronauts, https://www.elektronauts.com/t/hh-lab-tips/207960. Users list the
  TR-808 metal oscillator frequencies (205.3, 304.4, 369.6, 522.7 Hz, and trimmed 540/800 Hz) and 606 values.
  The sources are unverified and nobody measured the Rytm/Syntakt. Use them only as initial guesses.
  Tiptop's HATS808 notes (https://docs.cherryaudio.com/misfit-audio/transistor-808-hats, [FORUM/vendor])
  report that the frequencies vary widely between real 808s.
- **[FORUM] Syntakt Science Lab** series, Elektronauts, https://www.elektronauts.com/t/syntakt-science-lab/235364
  (e.g. #9 RS HARD/CLASSIC, #22 SY RAW). Patches and audio per machine: listening references, no
  measurements.
- **[FORUM] Filter threads**: https://www.elektronauts.com/t/how-do-the-syntakt-filters-sound/171670. Users ask
  about slopes, which the manual does not give. Unresolved there.
- **[CODE] trigcondition/analog-emu** (MIT, see `resources.md`) stubs the Rytm's analog voices. No analog model
  exists in the open emulator projects.
- **Commercial clones**: none states a method for the Syntakt's analog voices. Elektron's own sample packs and
  Rytm sound packs are recordings only.

## 5. Open-source implementations worth reading

- **Mutable Instruments Plaits** [CODE], É. Gillet, MIT,
  https://github.com/pichenettes/eurorack/tree/master/plaits/dsp/drums: `analog_bass_drum.h` (a pinged
  resonant filter with a pitch-sweep FM term and an overdrive, modelled on the 808 bridged-T),
  `analog_snare_drum.h` (several pinged modes + filtered noise), `hi_hat.h` (six square oscillators, or ring-mod
  "clocked noise", band-pass, VCA), and `synthetic_*` (VCO-based kicks/snares, closer to BD HARD/CLASSIC
  [INFER]). Compact, efficient C++. The best reference for the shape of the code.
- **Mutable Instruments Peaks** [CODE], MIT, same repository, `peaks/drums/` (bass_drum, snare_drum, high_hat,
  fm_drum; link not opened). Simpler 808-style versions.
- **VCVRack/AudibleInstruments** [CODE], the VCV port of the Mutable modules, GPL-3.0 (license not checked),
  https://github.com/VCVRack/AudibleInstruments. VCV's own "Drums" (https://vcvrack.com/Drums) is
  commercial and closed.
- **Faust libraries** [CODE], GRAME, per-function licences (mostly MIT/LGPL-style),
  https://faustlibraries.grame.fr/libs/synths/ (`sy.kick` and the drum-synthesis section) and `vaeffects.lib`
  (`ve.moogLadder`, `ve.diodeLadder`, `ve.korg35LPF`, `ve.oberheim`, the SVF family; names from memory). Quick
  prototypes for each guessed filter topology.
- **chowdsp_wdf** [CODE], J. Chowdhury, header-only C++14, https://ccrma.stanford.edu/~jatin/chowdsp/chowdsp_wdf
  (paper https://arxiv.org/abs/2210.12554). Licence not confirmed (believed BSD-3). Use it for WDF
  sub-circuits in a plug-in. **RT-WDF** (Rest, Werner, Dunkel, DAFx-16,
  https://dafx.de/paper-archive/details/ilZF4akpmSxzyiusoAOolg) handles arbitrary topologies.
- **jorshi/drumblender** and **TorchDrum** (above): PyTorch fitting code for drums.

## 6. Recommended approach

**Principle.** The CPU #1 routines (CVs, switch bits, envelope commands) are ported bit-exact, as the digital
engines were. Each analog voice is then a **grey-box circuit model driven by those same CV words**. Fit it
in a differentiable copy (PyTorch), following Esqueda et al. 2021, then freeze it into C++. Use neural
components only for the nonlinear stages that resist a guessed topology.

Per family:

- **Kicks (BD HARD, CLASSIC, SHARP, SILKY, ACOUSTIC) and SY DUAL VCO/RAW/CHIP:** an exponential VCO with
  per-unit tuning (fit CV to Hz, including the calibration offsets), shapers from triangle to sine and
  asymmetric sine (fitted polynomial with ADAA), and the envelope generator driven by the ported commands. A
  VCA with a fitted exponential law (V2164-like [INFER]) and a tick/snap/click from a short pulse through a
  fitted 2-pole filter. Then the OVER stage as a Wiener-Hammerstein model (filter, static curve, filter); if
  it misses, a GRU-8 conditioned on OVER. For FM, PLASTIC and ring modes, use analog linear FM and a
  four-quadrant multiplier, keeping the documented pitch-tracking error. ACOUSTIC's IMP is digital PCM: find
  it in the firmware, don't model it.
- **Snares and rims (SD HARD, CLASSIC, FM, NATURAL, ACOUSTIC, RS HARD, CLASSIC):** the same VCOs, plus a
  noise source with fitted colour, and SD NATURAL's noise HPF/LPF/resonance as a TPT SVF. Start from Plaits'
  analog snare structure.
- **Track 12 metal (CH/OH/CY/CB, CY RIDE, HH BASIC/LAB):** six band-limited squares (polyBLEP), combined by the
  logic or ring-mod the switch bits select, as in Werner's 808 cymbal model. Then a fitted bank of band-pass
  and high-pass filters, and a VCA per envelope. Get the frequencies from HH LAB recordings by peak-picking
  (each OSC knob alone, long DEC), then refine by gradient. Find CY RIDE's logic types by testing the
  candidates (XOR, AND, ring) against spectra.
- **UT NOISE and UT IMPULSE:** fit the noise spectrum. UT IMPULSE is the **best probe of the track filter**: an
  impulse through the analog filter at each FREQ/RESO/TYPE gives the impulse response directly.
- **Track filters and FX filter:** a TPT SVF and ladder candidates with tanh-type nonlinearities. Identify
  them from responses at low level (linear) and at rising levels (nonlinear). Drive the **FX block from the
  external inputs** with sine sweeps, multi-level sines and noise. That is standard system identification,
  the easiest analog part to get right. The FX drive and the amp are fitted the same way.
- **Analog mixer:** master VCAs and filter (CVs 35-38). Route external input straight to main and measure
  gain law, frequency response and headroom. Probably near-linear [INFER].

**Recordings to make** (Overbridge, 48 kHz, separate streams for tracks 9-12 + main, all MIDI-driven by a
script so each hit is labelled):

1. Warm the unit up for 2+ hours, run CALIBRATION, log room temperature, and record a reference kit at the
   start and the end of each session (to measure drift).
2. Per machine, sweep each SYN parameter alone, 9-17 values including the extremes, others at default. Add
   2D grids for interacting pairs (TUNE×SDEP, STIM×SDEP, DEC×HOLD, FM T×FM A, OVER×TUNE). Use velocities
   of 1, 32, 64, 96 and 127, and notes over the full TUNE/keyboard range for pitch tracking.
3. **8-16 repeats** per setting, spaced far enough apart to decay fully. Free-running oscillators and noise
   make every hit differ: the repeats give the spread the model has to land inside. Add pairs of close
   retriggers to capture the reset and choke behaviour.
4. Long-hold, zero-sweep settings (BD with HOLD high, SDEP 0) give steady tones at known CV. Use them as clean
   probes for the VCO law, the shaper and OVER's harmonics.
5. UT IMPULSE across the track filter's FREQ × RESO × TYPE grid. External-input sweeps through the FX block
   for each TYPE × FREQ × RESO × DRIVE, at 3-4 input levels.
6. For every hit, capture from the emulator the CV words, bits and envelope commands that the ported CPU #1
   code produces for the same parameters. These are the model's inputs.

**Judging success:**

- **Objective:** a multi-resolution STFT loss and a log-mel distance per hit after sample alignment; f0
  trajectory error in cents over the sweep (kicks); amplitude envelope error in dB on 1 ms RMS frames, and
  decay time per band; transient error-to-signal ratio over the first 10 ms; spectral centroid and bandwidth
  of the noise; ESR (error-to-signal ratio) for the filter/drive test signals. Main criterion: the model's
  distance to the recording should fall **within the hardware's repeat-to-repeat spread**.
- **Listening:** blind ABX on loudness-matched loops (LUFS) per machine, then MUSHRA (ITU-R BS.1534) with a
  hidden reference across 10-20 typical patches, including high OVER and high RESO, where models fail first.

## Five sources to start with

1. Esqueda, Kuznetsov, Parker 2021, Differentiable White-Box VA Modeling: the fitting method.
2. Werner, Abel, Smith 2014, TR-808 cymbal (and the bass drum paper): models of the six-square metal voice and
   of drum sub-circuits.
3. Zavalishin, The Art of VA Filter Design: the filters and how to discretise them.
4. Plaits `dsp/drums` (MIT): efficient C++ drum structures.
5. Syntakt manual OS 1.42, Appendix A and §5.4/§12: what each machine contains, and the external-input path
   into the FX block.
