# Resources for the Elektron engines

Collected 2026-10-21 from GitHub, the web and Elektron's site. Licences checked through the GitHub API. MoDule is
AGPL-3.0: MIT, BSD, Apache-2.0, CC0, GPL-2.0-or-later and GPL-3.0 code may be combined with it; GPL-2.0-only may not;
code without a licence is read for its facts only and re-implemented.

## Projects that run the target engines

| Project | Licence | What it gives |
|---|---|---|
| [trigcondition/analog-emu](https://github.com/trigcondition/analog-emu) | MIT, Rust | Boots Syntakt **1.42** (the version Elektron serves today), Rytm MKII 1.73, Rytm MKI, Analog Four. Runs the Syntakt's second ColdFire (section 7) for real: digital tracks 1-8 sound. `docs/SYNTAKT-PORT.md`, `docs/RYTM-PORT.md`: voice frame, SPI boot, CPU-to-DSP protocol. Rytm analog voices stubbed. Best base for the Syntakt. |
| [karigardarsson/Synthack-OS](https://github.com/karigardarsson/Synthack-OS) | MIT | Syntakt 1.41 audio path: section 7 renders 8 voices unmixed, engine tables, per-track parameters; renders one engine alone (see README.md). |
| [irpina/digiemu](https://github.com/irpina/digiemu) | GPL-2.0+ | Real firmware of Digitakt MK1, Digitone MK1 (FM engine on a second ColdFire), Model:Cycles, Model:Samples, live audio; DSP link and HLE (`emu/dsplink.py`, `dsp.py`, `hle.py`), container decoders, six Unicorn ColdFire fixes (`patches/`). |
| [ElekLabs/digikit](https://github.com/ElekLabs/digikit) | AGPL-3.0+ with section 7 terms | Digitakt II 1.17 and Digitone II 1.12: ColdFire and SHARC+ emulated (not real-time), documented voice record and chain (`docs/findings/`). |
| [naturarum/dn2-cfw](https://github.com/naturarum/dn2-cfw) | GPL-2.0+ | SHARC+ interpreter in C (`tools/sharcvm/`: 40-bit registers, 80-bit MR, DAGs, loop stacks, delayed branches), checked against selache's 2,039 C tests; renders the Digitone II's stock delay, chorus, reverb, compressor and block handler. Closest SHARC core to build on. |
| [lalzart/dt2-machine-lab](https://github.com/lalzart/dt2-machine-lab) | none (read only) | Digitakt II voice hook and per-voice calling convention, Plaits engines compiled to SHARC. |
| [lalzart/digitakt-ii-firmware-research-public](https://github.com/lalzart/digitakt-ii-firmware-research-public) | CC0 | Digitakt II 1.15C architecture and memory map. |
| [TinyGregAudio/Model-TG](https://github.com/TinyGregAudio/Model-TG) | MIT | Model:Cycles 1.13 patches (a sampler machine): map of the Cycles' machine and voice code. |
| [xerydian/elektron-emu](https://github.com/xerydian/elektron-emu) | none (read only; licence requested) | Five machines, see README.md. |

Mod frameworks that locate the OS's hook points: [irpina/elekloader](https://github.com/irpina/elekloader) (GPL-2.0+,
Digitakt MK1/II, Digitone MK1, Models, Octatrack, Rytm MKII), [Synfact](https://github.com/GSM1980/Synfact-release)
(MIT, Syntakt), [karigardarsson/digitakt-ii-research](https://github.com/karigardarsson/digitakt-ii-research) (MIT),
Bezronczek/syntakt-firmware-workbench and romanjeff/syntaktful (MIT, Syntakt), water-feature/taktone (no licence,
Digitone MK1 second CPU), [ethernet8023/rytm-fw-re](https://github.com/ethernet8023/rytm-fw-re) (no licence, Rytm MKII
1.74 sections, no HMAC).

## Reverse-engineering tools

- ColdFire: GNU objdump (exact EMAC sites, as SyntHack uses it); Unicorn (wrong on signed fractional EMAC as shipped;
  fixes in digiemu `patches/`, a Python model in SyntHack); digikit's `tools/coldfire/` (Ghidra EMAC module from Ghidra,
  Apache-2.0; `cfdb.py`, `cf.py`); [FirestormAudio/coldfire-rs](https://github.com/FirestormAudio/coldfire-rs)
  (Apache-2.0, MCF5441x peripheral map from an SVD); small interpreters jacobrec/coldfire-emulator (Unlicense),
  pahihu/coldfire (GPL-2.0, check "or later").
- SHARC+: [js216/selache](https://github.com/js216/selache) (GPL-3.0: assembler, C99 compiler, linker, LDR, VISA
  disassembler, a decoder oracle); [MAME's SHARC core](https://github.com/mamedev/mame/tree/master/src/devices/cpu/sharc)
  (BSD-3, ADSP-2106x interpreter and recompiler, not SHARC+); dn2-cfw's sharcvm and LDR/VISA scripts; digikit's decode
  tables and `sharc_bootcheck.py`. No open Ghidra module for SHARC or ColdFire EMAC found.
- Blackfin (if a target uses one): sualk/ghidra-blackfin (no licence), geepot/ghidra-blackfin, hekar-lab/Ghidra-BlackFinPlus.
- Containers: [mischa85/elektron-firmware-tool](https://github.com/mischa85/elektron-firmware-tool) (ELE3 unpack/repack).
- Avoid: ahmadexp/UAD2-OCTO-RE (GPL-2.0-only).

## Formats, protocols, sequencers

- [dagargo/elektroid](https://github.com/dagargo/elektroid) (GPL-3.0): Transfer and SysEx file protocol, sounds, projects, samples of Digitakt, Digitone, Syntakt, Rytm, Models. The importer for users' own factory content.
- [dagargo/overwitch](https://github.com/dagargo/overwitch) (GPL-3.0), [droelfdroelf/dtdump](https://github.com/droelfdroelf/dtdump) (MIT): Overbridge USB audio.
- [bsp2/libanalogrytm](https://github.com/bsp2/libanalogrytm) (MIT): Rytm kit, sound, pattern; alisomay/rytm-rs (MIT); d-huck/libdigitone (MIT, stale).
- [xrcstrecords/syxgrid-digitone-ii](https://github.com/xrcstrecords/syxgrid-digitone-ii) (AGPL-3.0): Digitone II pattern format with p-locks and trig conditions.
- Synfact's `vendor/elektron_object.py` (MIT): `.stsnd`, `.dnsnd`. mzero/elk-herd (no licence): Digitakt project files.
- Sequencers: subvertnormality/mosaic (GPL-3.0), DimaDake/schwung-movy (MIT, 16-track p-locks), zooloo303/digi-roll-studio (GPL-3.0). None does trig conditions as fully as the firmware.

## Official documents

- Manuals (MIDI appendices: the CC/NRPN maps to freeze host parameters from):
  [Syntakt 1.42](https://www.elektron.se/wp-content/uploads/2026/10/Syntakt-User-Manual_ENG_OS1.42_260930.pdf),
  [Digitakt II 1.17](https://www.elektron.se/wp-content/uploads/2026/10/Digitakt-2-User-Manual_ENG_OS1.17_260930.pdf),
  [Digitone II 1.12](https://www.elektron.se/wp-content/uploads/2026/10/Digitone-2-User-Manual_ENG_OS1.12_261008.pdf),
  [Analog Rytm MKII 1.74](https://www.elektron.se/wp-content/uploads/2026/10/Analog-Rytm-MKII-User-Manual_ENG_OS1.74_260930.pdf),
  [Overbridge](https://www.elektron.se/wp-content/uploads/2026/08/Overbridge-User-Manual_260826.pdf),
  [Model:Cycles 1.11](https://www.elektron.se/wp-content/uploads/2020/03/Model-Cycles-User-Manual_ENG_OS1.11_200303.pdf) (URL from a search, may be stale).
- NXP: [MCF54418 reference manual](https://www.nxp.com/docs/en/reference-manual/MCF54418RM.pdf); ColdFire Programmer's
  Reference Manual Rev. 3 (MAC and EMAC chapters; official copy from NXP's library); DSPLIBUM (EMAC idioms).
- Analog Devices: [SHARC+ Core Programming Reference](https://wcm-cce.cldnet.analog.com/media/en/dsp-documentation/processor-manuals/sc58x-2158x-prm.pdf);
  ADSP-21569 hardware reference on its [product page](https://www.analog.com/en/products/adsp-21569.html) (FIR/IIR
  accelerators); [EE-375](https://wcm-cce.cldnet.analog.com/media/en/technical-documentation/application-notes/EE375v01.pdf);
  [Blackfin Programming Reference](https://www.analog.com/media/en/dsp-documentation/processor-manuals/Blackfin_pgr_rev2.2.pdf).
- Hardware evidence: Digitone II and Digitakt II carry an MCF54415 and an ADSP-21569 (Elektronauts PCB threads); the
  Syntakt may carry a Xilinx Spartan FPGA (unconfirmed); no teardown of the Rytm MKII's digital chip found.

## DSP research for our own chain

- Filters: Zavalishin, [The Art of VA Filter Design](https://www.native-instruments.com/fileadmin/ni_media/downloads/pdf/VAFilterDesign_2.1.0.pdf);
  [Cytomic technical papers](https://cytomic.com/technical-papers/) (trapezoidal SVF, input-mixing multimode, Sallen-Key,
  dynamic smoothing); Huovilainen DAFx-04 and D'Angelo-Välimäki (Moog ladder); Rest-Parker-Werner DAFx-17 (MS-50);
  Smith, [Physical Audio Signal Processing](https://ccrma.stanford.edu/~jos/pasp/).
- Analog drums: Werner-Abel-Smith [TR-808 bass drum](https://dafx14.fau.de/papers/dafx14_kurt_james_werner_a_physically_informed,_ci.pdf)
  and [cymbal](https://speech.di.uoa.gr/ICMC-SMC-2014/images/VOL_2/1453.pdf); Werner's
  [WDF thesis](https://purl.stanford.edu/jy057cz8322); Mutable Plaits (MIT) analog drum models. Nothing published on
  the TR-909 or Elektron's analog circuits.
- Steiner-Parker: Leonardo Laguna Ruiz's (Vult) [schematic](https://modlfo.github.io/images/vult-analog-steiner-parker-diagram.pdf)
  (2017, "based on yusynth improved version", no licence stated; a circuit's values are facts): ten-diode ladder D1-D10
  biased through R9/R13 (1.8k), three 1.5 nF capacitors (C9-C11), a THAT300 matched pair as the exponential current
  source (CV1, CV2, CV summed through 220k/150k), the input sent to node A, B or C (SelDestA/B/C, through 2.2 uF and
  180k) to choose low-, band- or high-pass, resonance fed back through IC1C/IC1D (TL074). Enough component values to
  model it with Werner's WDF method or Zavalishin's TPT.
- Effects: Dattorro, [Effect Design Part 1](https://ccrma.stanford.edu/~dattorro/EffectDesignPart1.pdf) (plate) and
  Part 2 (modulated delays); Välimäki et al. "Fifty Years of Artificial Reverberation"; Giannoulis-Massberg-Reiss
  compressor (JAES 2012); antiderivative antialiasing (Parker et al. DAFx-16, Bilbao et al. IEEE SPL 2017), BLAMP
  (Esqueda et al. DAFx-16); wavefolders (Esqueda et al. 2017).
- Oscillators: PolyBLEP (Välimäki et al. JASA 2012), minBLEP (Brandt ICMC 2001); FM: Chowning JAES 1973, Dexed (GPL-3)
  and msfa (Apache-2.0).
- Sampler: Niemitalo's polynomial interpolators; Smith's [resampling page](https://ccrma.stanford.edu/~jos/resample/);
  r8brain-free-src (MIT), libsamplerate (BSD-2); Signalsmith Stretch (MIT); onset detection (Bello et al. 2005).
- Libraries usable under AGPL: Mutable Instruments eurorack (MIT), DaisySP (MIT), chowdsp_utils (GPL-3/BSD per module)
  and chowdsp_wdf (BSD-3), Surge's sst-filters and sst-effects (GPL-3), Vital (GPL-3), Airwindows (MIT), Signalsmith
  DSP (MIT), HIIR (WTFPL), Faust libraries (per function).
- Emulation write-ups: none published on the Gearmulator JIT or on SHARC/ColdFire binary translation; compiled
  simulation background in Zhu-Gajski DATE'99 and Reshadi-Mishra-Dutt DAC'03.

## What this changes

- The Syntakt can start from analog-emu (MIT) on OS **1.42**, the version Elektron serves: no firmware hunt.
- Digitakt MK1, Digitone MK1, Model:Cycles and Model:Samples run in digiemu (GPL-2.0+): their engines are ColdFire too.
- SHARC+ is needed only for the Digitakt II and Digitone II: dn2-cfw's sharcvm, selache and MAME are the parts.
- Factory content is imported from the user's own machine through elektroid's protocol.
