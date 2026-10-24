# 15 — Effets du Virus (et de l'Octatrack)

Recherche du 2026-10-24, ticket [15-effets-virus](../issues/15-effets-virus.md). Suppositions marquées [INFER].

## Réponse courte

Oui en principe, à un coût d'ingénierie élevé et inconnu en détail, et **seulement pour la famille TI** chez
l'utilisateur : la seule ROM Virus présente est le `firmware.bin` d'OsTIrus (installeur TI, qui contient TI, TI2 et
Snow) ; aucune ROM Virus A/B/C (Osirus) n'a été trouvée. Le programme DSP du Virus est un programme monolithique
sans symboles ; Gearmulator ne cartographie aucune routine d'effet (il le fait tourner en entier, avec un
microcontrôleur en C++). Le port en C++ suivrait le chemin de `TrackFx` de Machinemodule (traduction exacte de
routines DSP56300, oracle = DSP émulé) mais il faut d'abord **trouver** chaque routine dans le programme, ce que
personne n'a publié.

L'Octatrack est un meilleur candidat pour phaser, flanger et chorus : ses effets tournent aussi sur un DSP56xxx, son
OS est public et téléchargeable (déjà présent chez l'utilisateur), il est à 44,1 kHz, et ses effets sont des effets
de piste (deux emplacements par piste), comme ceux que l'instrument veut.

## 1. Ce que Gearmulator sait du programme DSP du Virus

Sources : `source/virusLib/` (copie locale `C:\dev\gearmulator-md-drums`, = amont dsp56300/gearmulator) et la page
technique de The Usual Suspects, https://theusualsuspects.io/technical/osirus-c.

| Modèle | DSP (TUS) | Fréquence d'échantillonnage | Mémoire DSP | Code Gearmulator |
|---|---|---|---|---|
| Virus A | 1 x DSP56303, 66 MHz | 12 MHz/256 = 46 875 Hz | 256 K mots | `DspSingle(0x40000, ..., 56303 periph)`, ESSI (`device.cpp:528`, `dspSingle.cpp:39-44`) |
| Virus B | 1 x DSP56311, 100 MHz | 46 875 Hz | 256 K | idem, périphériques 56362 (ESAI) |
| Virus C | 1 x DSP56362, 120 MHz | 46 875 Hz | 256 K | idem |
| Virus TI/TI2 | 2 x DSP56367, 133 MHz | 32-96 kHz (44,1/48 officiels) | 1 M par DSP | `DspMultiTI` (`dspMultiTI.cpp:103`), deux DSP reliés par ESAI_1 |
| TI Snow | 1 x DSP56367, 150 MHz | idem | 1 M | `DspSingleSnow` (`dspSingleSnow.cpp:5`) |

Fréquences d'échantillonnage : `device.cpp:85-101` ; `romfile.h` `getSamplerate()` (TI : 44 100, ABC : 12e6/256).

**Rangement dans la ROM.** ABC (512 Ko) : chunks DSP à partir de `0x18000`, un banc de 32 Ko par chunk, ids 4..0
(3..0 pour le A et les vieux OS B) ; TI (image de 1 Mo extraite de l'installeur) : à partir de `0x70000`, ids 14..0
(`romfile.cpp:176-240`). Chunk = id, deux octets de taille (le premier moins 1), puis des mots de 24 bits big-endian.
Le chunk 0 commence par la taille et l'adresse de la BootROM ; le reste est un flux de commandes que la BootROM
reçoit par HDI08 et écrit en P/X/Y avant de sauter au programme (`romfile.cpp:53-79`, `dspSingle.cpp:150-178`). Entrée
TI : P:$D0C (TUS).

**Paramètres.** Le microcontrôleur de Gearmulator est en C++ (le 68k n'est pas émulé) : il envoie au DSP les octets de
paramètre bruts, page/partie/numéro/valeur (`0xf4f400`, `microcontroller.cpp:403-419`), et les presets entiers en
256/512 octets (`0xf47555`, `microcontroller.cpp:333-337`). **Donc le programme DSP fait lui-même la conversion
valeur -> coefficients** : un port C++ doit porter ces conversions aussi (pas de part 68k à côté, contrairement au
Machinedrum, où le tick de l'OS calcule les mots du DSP).

**Ce qui n'existe pas en amont.** Aucune carte des routines d'effets : `dspMemoryPatches.cpp` est vide, le dossier
`doc/` de l'amont n'a rien sur le Virus (https://github.com/dsp56300/gearmulator/tree/main/doc). TUS dit désassembler
dans IDA Pro (processeur dsp563xx) plus un désassembleur maison, avec des dumps d'assembleur de toutes les variantes,
non publiés ; l'extraction de la BootROM se fait avec leur dépôt dsp563c-tools (page TUS ci-dessus).

**Outils dans le dépôt.** `source/dsp56300` a un désassembleur (`dsp56kEmu/disasm.*`, outil `source/disassemble`), une
interface de débogueur (`debuggerinterface.*`) et le profilage JIT (`jitprofilingsupport.*`). Gearmulator fait tourner
le Virus entier : c'est l'oracle tout trouvé (DspSingle/DspMultiTI + microcontrôleur C++, sans 68k).

## 2. Les effets du Virus

Listes tirées des descriptions de paramètres de Gearmulator : `osirusJucePlugin/parameterDescriptions_C.json` (C) et
`osTIrusJucePlugin/parameterDescriptions_TI.json` (TI).

| Effet | Virus C | Virus TI | Portée (C) |
|---|---|---|---|
| Chorus | un type (Rate, Depth, Delay, Feedback, Mix, LFO Shape) | Off, Classic, Vintage, Hyper Chorus, Air Chorus, Vibrato, Rotary Speaker | par partie |
| Phaser | 1-6 étages (Rate, Depth, Freq, Feedback, Spread, Mix) | idem | par partie |
| Flanger | **pas d'effet à part** ; un chorus à feedback [INFER, d'après les paramètres] | idem | — |
| Delay | Delay, Delay 2:1/4:3/4:1/8:7, Patterns, Clock, Color, Feedback, LFO | Simple, Ping Pong, Patterns ; types Classic, Tape Clocked, Tape Free, Tape Doppler | global en Multi (`MultiOrSingle|NonPartSensitive`), envoi par partie (`Effect Send`) |
| Reverb | modes Reverb, Reverb+Feedback 1/2 (partagée avec le delay), Damping | séparée : Ambience, Small Room, Large Room, Hall ; Time, Predelay, Damping, Color, Clock | globale + envoi |
| Distortion | Off, Light, Soft, Middle, Hard, Digital, Shaper, Rectifier, BitReducer, RateReducer, LowPass, HighPass | les mêmes + Wide, Soft/Hard Bounce, Sine/Triangle/Sawtooth Fold, Rate/Bit Reducer neufs, six Overdrives (Mint ... Chili), Patch Distortion | par partie |
| Character | — | Analog Boost, Vintage 1-3, Pad Opener, Lead Enhancer, Bass Enhancer, Stereo Widener, Speaker Cabinet | par partie |
| EQ | Low/Mid(Q)/High | idem | par partie |
| Analog Boost, Ring Mod, Vocoder, Filter Bank | Analog Boost, Ring Mod | + Vocoder, Filter Bank (comb, voyelles) | — |

## 3. Peut-on les extraire et les réécrire en C++ ?

Le précédent est dans le dépôt : Machinemodule a traduit la chaîne par piste du DSP mixeur du Machinedrum (AMD, EQ,
filtres, SRR, distorsion) en C++ exact, « 3,2 M échantillons et tout l'état identiques » au code du DSP émulé, 16 pistes
≈ 3 % d'un cœur x86, le même jour que le mix (`.scratch/uc-less-engines/machinemodule/HANDOFF.md:171-197`). Pour la
section master (Rhythm Echo, Gate Box, EQ, Dynamix), MModule a gardé le DSP émulé (`MasterEngine`, README de mdEngine),
la traduction n'ayant pas été faite.

Ce qui rend le Virus plus dur que le DSP mixeur du Machinedrum :

1. **Trouver les routines.** Le DSP du MD reçoit des mots d'effet tout calculés et sa fonction par piste est une
   boucle courte ; celui du Virus est tout le synthé (oscillateurs, filtres, arpégiateur, effets, conversions de
   paramètres) dans un seul programme. Méthode [INFER] : faire tourner Gearmulator, activer un seul effet par son octet
   de paramètre (`0xf4f400`), comparer la couverture P (profilage JIT ou hook d'exécution) et les écritures X/Y avec et
   sans ; puis désassembler la plage (outil `disassemble`) et suivre l'entrée audio.
2. **Conversion des paramètres dans le DSP.** Il faut aussi porter le code qui transforme 0-127 en coefficients, y
   compris les tables en mémoire X/Y que la BootROM y écrit.
3. **La fréquence.** ABC à 46 875 Hz : un port exact tourne à cette fréquence (rééchantillonné vers l'instrument, en
   plus du Syntakt et du MD), ou ses coefficients sont recalculés pour une autre fréquence et il n'est plus exact. TI à
   44,1/48 kHz [INFER : le programme TI calcule ses coefficients selon la fréquence choisie, puisqu'il en accepte six].
4. **Les tables et l'algorithme viennent de la ROM d'Access.** Même posture que le port du Syntakt : le code C++ est
   écrit par nous. La carte a décidé « aucun firmware chez l'utilisateur visé » : les tables de constantes que la
   BootROM écrit en X/Y doivent alors être régénérées par une formule retrouvée, ou embarquées (question de droit à
   trancher, comme pour les autres ports).

Pour un oracle, le Virus offre une entrée audio (`getChannelCountIn` = 2, `device.cpp:509`) qui passe par les
effets [INFER : avec un Single dont les oscillateurs sont muets, mode d'entrée] : on enverrait une batterie dans le
Virus émulé et on comparerait la sortie à celle du C++, sans toucher à la synthèse.

**Coût CPU.** Inconnu pour le Virus (aucune mesure de la part des effets dans le programme). Ordre de grandeur
[INFER] : chorus, phaser, flanger, distorsion et EQ sont de quelques dizaines d'opérations par échantillon (comme la
chaîne de piste du MD, 16 pistes ≈ 3 % d'un cœur avec bien plus d'étages), soit moins de 0,2 % d'un cœur par
instance ; une reverb de Virus est un réseau de retards et de passe-tout, une seule instance globale, < 1 %.
**Coût d'ingénierie** : de quelques jours par effet [INFER] (localiser, traduire, prouver à l'oracle), plus une fois le
banc d'oracle Virus (Gearmulator dans un test, chargé de la ROM de l'utilisateur).

## 4. ROM Virus chez l'utilisateur

Cherché sous `C:\dev`, `Documents`, `Downloads`, `Desktop` : fichiers `.bin`/`.rom` de 512 Ko, `.mid` d'OS, dossiers
de The Usual Suspects.

- **`Documents\The Usual Suspects\OsTIrus\roms\firmware.bin`**, 8 569 757 octets, en-tête `FORM....TABL` puis des noms
  `lcd_backup_000.bin` : l'installeur du Virus TI, que Gearmulator déballe (`unpacker.h`, `ROMUnpacker::getFirmware`)
  et qui contient TI, TI2 et Snow. La config d'OsTIrus (`config\OsTIrus.xml`, `disclaimerSeen=1`) montre qu'il a servi.
  Lu au-delà de l'en-tête : rien.
- **Pas de ROM Virus A/B/C trouvée** : pas de dossier `Osirus` sous `The Usual Suspects`, aucun fichier de nom Virus
  ou Osirus hors sources, et les `.bin` de 512 Ko des dossiers TUS sont des JP-8000 (JE8086) et des Nord Lead 2x
  (Nodal Red). Balayage par taille de `C:\dev`, `Documents`, `Downloads`, `Desktop` (tout `.bin`/`.rom` de 512 ou
  256 Ko, tout `.mid` de plus de 200 Ko hors Ableton) : rien d'autre que ces JP-8000 et Nord Lead 2x.

## 5. Octatrack

Sources : manuel Octatrack MKII, OS 1.40C (https://www.elektron.se/wp-content/uploads/2026/08/Octatrack-MKII-User-Manual_ENG_OS1.40C_260827.pdf) ;
https://github.com/mxldyn/octamax (rétro-ingénierie de l'OS 1.40C, sans licence) ; Wikipédia (Elektron Octatrack),
qui cite octamax.

- **Processeurs** : un ColdFire (« probablement MCF5445x, ~266 MHz », octamax) pour le contrôle, séquenceur et
  assemblage des paramètres ; un **DSP Freescale DSP56xxx** pour le signal (lecture, time-stretch, filtres, effets),
  identifié par les mots de 24 bits que le chargeur envoie au DSP trois octets à la fois (octamax). Le modèle exact du
  DSP et sa fréquence : inconnus. Convertisseurs 24 bits, 44,1 kHz (manuel, spécifications techniques, p. 116).
- **Effets** (manuel §11.4.10, p. 62 ; annexe B, p. 122-136) : deux emplacements par piste. FX1 : filtre multimode
  12/24 dB, EQ paramétrique 2 bandes, DJ Kill EQ, **phaser 2-10 étages**, **flanger**, **chorus 2-10 voix**,
  spatializer, comb filter, compresseur Dynamix, Lo-fi. FX2 : les mêmes, plus Echo Freeze Delay, Gatebox Plate
  Reverb, Spring Reverb, Dark Reverb.
  - Phaser (B.5, p. 126) : CNTR, DEP, SPD, FB, WID, MIX ; setup NUM (nombre d'étages).
  - Flanger (B.6, p. 127) : DEL, DEP, SPD, FB, WID, MIX ; pas de setup.
  - Chorus (B.7, p. 128) : DEL, DEP, SPD, FB, WID, MIX ; setup TAPS, FBLP (passe-bas du feedback).
- **Firmware** : `Downloads\OCTATRACK_OS1.40C_dist.zip` chez l'utilisateur (`.bin` 469 852 octets, `.syx`, readme),
  public chez Elektron. Selon octamax : couche ELUP (XOR et somme de contrôle), conteneur ELEK compressé aPLib, OS
  principal de 1 112 560 octets chargé à `0x40000400` ; le programme DSP est envoyé par l'OS, donc contenu dans cette
  image [INFER : à extraire en suivant l'envoi au DSP].
- **Conséquence** [INFER] : le même émulateur DSP56300 servirait d'oracle à l'Octatrack, avec le ColdFire de l'OS
  (comme MoDule) ou un hôte C++ qui envoie les trames de paramètres ; le phaser, le flanger et le chorus de
  l'Octatrack ont la forme d'effets de piste, à 44,1 kHz comme le Machinedrum. Rien de cela n'a été tenté : ni
  extraction du programme DSP, ni oracle.

## Inconnues

- Taille du programme DSP du Virus, place et coût des effets dans le programme (aucune carte publique).
- Lequel des deux DSP du TI fait quels effets (`DspMultiTI` route l'audio entre eux par ESAI_1).
- Le TI recalcule-t-il ses coefficients selon la fréquence (supposé).
- Le modèle et la fréquence du DSP de l'Octatrack ; le format de sa trame de paramètres ; une machine réelle pour
  comparer (l'utilisateur n'en a pas déclaré).
- Licence : octamax n'en a pas (lecture de faits seulement, comme elektron-emu) ; Gearmulator et dsp56300 sont GPLv3,
  compatibles avec l'AGPL de MModule pour un oracle de test.
