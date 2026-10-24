# 18 — Instrument sans firmware chez l'utilisateur

Ticket : [issues/18-sans-firmware.md](../issues/18-sans-firmware.md). Recherche du 2026-10-24.

Question : que lit encore chaque moteur dans l'image du firmware, et que faire de chaque donnée pour que l'instrument
tourne sans aucun firmware chez l'utilisateur (calculable / remplaçable / indispensable) ? Taille, effort, effet sur la
distribution.

Outils écrits pour cette recherche (hors dépôt, lecture seule sur le dépôt) :
`C:\dev\elektron-work\scratch-arch\research\18\` —
`mask.cpp` (harnais lié à `build-main\Release\stEngine.lib` : joue un scénario, une page ou un bloc de 256 octets d'une
section inversé par XOR, imprime un hash FNV-1a de toutes les sorties), `sweep.py` / `sweep2.py` (balayage par pages
de 4 Ko puis par blocs de 256 o ; résultats `s3.txt`, `s7.txt`, `s3fine.txt`, `s7fine.txt`), `tables.py`,
`tables2.py`, `exp2b.py`, `poles.py`, `waves.py`, `waves2.py`, `sramimg.py`, `sramdup.py` (tables recalculées par
formule, comparées mot à mot à OS 1.41).

## Réponse courte

- **Aucun code n'est lu** : le port du Syntakt n'exécute rien du firmware (pages de code inversées : sortie identique).
  Ce qui reste, ce sont des **données** : ~155 tables de constantes et l'image SRAM de départ du CPU voix
  (section 7, ~285 Ko de zone, ~183 Ko réellement atteints par le scénario), ~25 tables, 505 descripteurs de
  paramètres et 143 mots d'état SRAM du CPU #1 (section 3, ~25,6 Ko atteints sur 3,4 Mo).
- **Calculable, prouvé ici** : 5 tables régénérées à l'identique (0 mot différent) par une formule simple ; des
  familles entières ont visiblement une formule (ondes = sommes de ≤ 16 harmoniques, rampes linéaires, sinus,
  coefficients exponentiels en index) mais l'arrondi exact n'est pas encore retrouvé.
- **Remplaçable** : l'état SRAM du CPU #1 (143 mots non nuls sur 15 076), les descripteurs (min, max, défaut : des
  faits que notre propre table de paramètres donne déjà en partie), les machines par défaut, les samples (SP TWINSHOT,
  ROM du MD, Digitakt II), les kits d'usine.
- **Indispensable** (au sens du ticket : pas de formule trouvée, pas de remplaçant exact) : aujourd'hui, toutes les
  tables sauf les 5 prouvées. Le travail à faire est donc un chantier de **régénération de tables**, une famille à la
  fois, chaque table prouvée par un test qui la compare au firmware (le firmware reste chez le développeur, comme les
  oracles actuels). Effort [INFER] : 4 à 8 sessions d'agent pour le Syntakt.
- **Distribution** : quand toutes les tables d'un moteur sont générées, le plug-in ne lit plus de `.syx` ; rien
  d'Elektron n'est livré, le firmware ne sert qu'aux tests (exit 77 sans lui, comme aujourd'hui). Une table restée
  sans formule bloque son moteur (ou le rend approximatif, signalé comme tel selon la règle de la carte).

## 1. ST Drums / port du Syntakt : ce qui est lu

### 1.1 Chemin de chargement

- `stDrums::load` (`source/elektron/syntakt/stDrums/stDrumsFirmware.cpp`) décode le `.syx` (SysEx 8-dans-7,
  conteneur ELE3, sections dépaquetées en LZ façon aPLib), garde **les sections 3 et 7 seules**, et exige OS 1.41
  (FNV-1a 64 : `Section3Fnv = 0xc21a11109890e0d1`, 3 438 480 o ; `Section7Fnv = 0x641c4f29887bd511`, 383 760 o).
  `stDrumsProcessor.cpp:92` cherche le `.syx` dans le dossier `roms` ; sans lui, le plug-in ne joue pas.
- Section 7 devient `Rom` (`stVoice.h:71`, base 0x40000400), lue par `Rom::at/entry/ptr/byte`.
- Section 3 devient `main::Section3`, projetée page par page en lecture seule dans `main::Memory`
  (`stMainMemory.h`), sauf la page de données 0x402d7000, copiée en écriture.

### 1.2 Mesure : quelles octets atteignent la sortie

Méthode : un scénario (celui de `stSyntaktTest`, puis 24 tours où les 8 pistes passent par les 12 machines,
paramètres et FX tirés au hasard dans leurs plages, tempo, locks de TUNE, notes 0-127, vélocités 1-127 ; ~25 s audio)
joué avec les sections intactes, puis avec un morceau inversé (XOR 0xA5 / 0x5A5A5A5A). Une différence de hash = le
morceau est lu et compte. Limite : un morceau lu pour un réglage que le scénario n'atteint pas passe pour inutile
(couverture, pas preuve) ; la liste statique des adresses du port complète.

| Section | Pages de 4 Ko qui comptent | Blocs de 256 o qui comptent |
|---|---|---|
| 7 (383 760 o, 94 pages) | 60 : `0x40009400`-`0x40040400` et `0x40053400`-`0x4005d400` | 183 296 o en 43 plages (`s7fine.txt`) |
| 3 (3 438 480 o, 840 pages) | 19 (`s3.txt`) | 25 600 o en 21 plages (`s3fine.txt`) |

Pages 0 à 8 de la section 7 (`0x40000400`-`0x40009400`, le code des moteurs) : aucune influence. Le port ne lit
aucun code.

### 1.3 Section 7 (CPU voix) : inventaire

| Zone | Adresse, taille | Contenu | Qui la lit | Classement |
|---|---|---|---|---|
| Code | `0x40000400`-~`0x40009f88`, ~39 Ko | routines des 12 moteurs, étages, dispatch | personne (porté en C++) | rien à faire |
| Tables d'interpolation par machine | `0x4001e7fc`-`0x4003fe38`, tables de 128 mots (+1) | courbes paramètre → coefficient : décroissances (AmpDecay, ModDecay, SweepDecay, DecayU0-3…), niveaux, rapports de fréquence, profondeurs, cibles ; ~120 tables nommées dans `stBdModern.cpp`… `stSyToy.cpp` | `update()` de chaque moteur via `interpolate(_rom.ptr(T), word)` ou `_rom.entry(T, byte)` | calculable probable (courbes lisses, p. ex. BD AmpDecay : ln(1+c) régulier) ; formule non retrouvée |
| SD VINTAGE `Decay3` | `0x40028438`, 16 384 mots = 64 Ko | décroissance indexée par byte16 << 7 \| byte10 | `stSdVintage.cpp:101` | calculable probable (2D) ; non tenté |
| Ondes ROM | `0x40016b80`-`0x4001e7fc`, 31 ondes de 257 mots (~32 Ko) ; pointeurs `0x40014870` (32) | ondes de SY CHORD et des unités (BD MODERN `0x4001e7fc`, CY ALLOY `0x40021000`, PC CARBON `0x40024804`) | `pmOscillator`, `feedbackOscillator` | 27 des 31 ondes = sommes exactes de ≤ 16 harmoniques (résidu 1e-7 à 3e-5 de pleine échelle, `waves2.py`) : formule additive [INFER], arrondi exact à trouver ; 4 ondes (résidu 6e-4 à 6e-3) peut-être échantillonnées |
| exp2 | `0x4000e488`, 1 025 mots | 2^(i/1024)/2 en Q31 | `exp2Entry` (`stStages.cpp:34`) | **calculable, prouvé** : `round(2^(i/1024)/2 · 2^31)`, 1 025/1 025 exacts |
| log2 | `0x4000e48c`+4k, k = 0x400..0x7ff | log2((k+1)/2048) en Q31 | `sqrtLog` (`stStages.cpp:26`) | **calculable, prouvé** : `round(log2((k+1)/2048) · 2^31)`, 1 024/1 024 exacts |
| Filtre multimode, BASE/WIDTH, enveloppe d'ampli | `0x40009f88`-`0x40013e70`, `0x40014070`, `0x40040060` | coefficients, racines, gains de résonance, tables ATK/HOLD/DEC | `stTrackFilter.cpp`, `stTrack.cpp` | calculable probable ; non tenté |
| Résonateur de CP VINTAGE | `0x40014b80`, `0x40015b80`, 1 025 mots chacun | paires sin/cos d'une fréquence gauchie | `resonatorCoefficients` | calculable probable ; ni sin ni cos simples (essais négatifs) |
| Pôles de bande SP TWINSHOT | `0x40014980`, 128 mots | un pôle par réglage | `stSpTwinshot.cpp` | calculable probable |
| Machine → moteur | `0x40014950`, 46 octets | identité de moteur par id de machine | `engineOf` (`stEngine.cpp:32`) | **remplaçable** : 46 petits entiers, un tableau écrit par nous (déjà implicite dans `stEngines.h`) |
| Image SRAM de départ | `0x4004f6e0`-`0x4005df10`, 59 440 o (8 064 mots non nuls, 1 014 pointeurs) | état initial des voix et tables RAM | `boot` (`stBoot.cpp:30`) | mixte, voir dessous |

L'image SRAM du CPU voix est surtout faite de tables (`sramimg.py`, `sramdup.py`) ; aucune n'est une copie d'une
table ROM :
- `0x80004b70` (sinus RAM, 257 mots) : **prouvé** `round(sin(2πi/256) · 2^31)`, 257/257 exacts.
- `0x8000a888` (exp2 RAM) : **prouvé** `round(2^(i/2047) · 2^30)` pour les 2 047 premiers mots ; suite de la plage
  (1 027 mots) non identifiée.
- `0x8000d894` (`0x01000000 · i`), `0x8000dc94` (`-0x01000000 · i`) : rampes linéaires, calculables [INFER, vu sur
  les 6 premiers mots].
- `0x8000e098`, `0x8000e498` : sinus de période 4096 (`0x1921d20` = sin(2π/4096)·2^31), calculables [INFER].
- Le reste (état des voix, pointeurs vers SRAM) : **remplaçable** par l'initialisation que notre code écrit déjà en
  partie (`initVoice` réécrit l'état de chaque voix ; les pages de `0x4004f6e0` à `0x40053400` n'ont aucune influence
  dans le balayage).

### 1.4 Section 3 (CPU #1) : inventaire

Ce que les routines portées lisent (`s3fine.txt`, noms tirés du code) :

| Plage atteinte | Table (code) | Classement |
|---|---|---|
| `0x401db100`-`0x401db400` | courbes de vélocité `0x401db1dc`, 4 × 128 (`stMainMix.cpp:9`) ; gains de sortie `0x401dafdc` (`stMainOutput.cpp:347`) | courbe 0 constante (-1), courbe 2 **prouvée** `round(-i/127 · 2^31)` 128/128 ; courbes 1 et 3 (concave, convexe) non retrouvées (puissances 2, 3, 4, 1/2, 1/3, 1/4 essayées : non) |
| `0x401dea00`-`0x401df100` | enveloppes de filtre : `Delays 0x401deac0`, `Coefficients 0x401decc0`, `Increments 0x401deec0` (`stMainFilter.cpp`) | calculable probable |
| `0x401dfa00`-`0x401e0800` | matrice de la reverb `0x401df8e0` (`stMainReverb.cpp:17`) | calculable probable (signes et gains par DEC) |
| `0x4022c800`-`0x40233d00` (5 plages) | machines par défaut `0x4022c8d8` (12 longs) ; descripteurs `0x4022d59c` (505 × 52 o : groupe, id interne, min, max, défaut) (`stSyntakt.cpp:31-34`) | **remplaçable** : des faits (bornes, défauts) que nous écrivons dans notre propre table ; le plug-in en tire aussi son kit par défaut et ses plages (`stDrumsProcessor.cpp:131`, `takeKit`) |
| `0x40245800`-`0x40245900` (+ `0x40245a50`, `0x40245c50`) | LFO : `HalfTable`, `OneUpTable`, `OneDownTable`, `RandomCurves` (`stMainLfo.cpp`) ; glide : `Spans`, `Rates` (`stMainPitch.cpp`) | calculable probable ; petites |
| `0x4029b700`-`0x4029cf00` | `Decays 0x4029b6fc`, `Shelves 0x4029b7fc`, `Interpolation 0x4029b9fc`, `Poles 0x4029bffc` (512) (reverb, delay, FX) | Poles : `-ln c` exponentiel en index, f = 5,0002 Hz × 2^(i/39,18) (résidu 2e-5 jusqu'à i ≈ 300), puis gauchi vers les aigus (508 non nuls) ; formule exacte non retrouvée (exp(-w), bilinéaire, forme cos essayées) |
| `0x4033b900`-`0x40346500` (9 plages) | image SRAM du CPU #1, `0x40339000`-`0x40347b90` (60 Ko) copiée par `loadSramImage` (`stMainFx.cpp:301`) | **remplaçable** : 143 mots non nuls sur 15 076 (`sramimg.py`), dont 47 pointeurs ; une liste de constantes d'initialisation écrite dans notre code, prouvée contre l'image |
| non atteint | page de données `0x402d7000` (tempo, état du générateur aléatoire des LFO) ; `AnalogGains 0x401dc5e0`, `AnalogLevels 0x401e08e0` (pistes analogiques) | tempo : écrit par le code ; graine aléatoire : 2 longs à reprendre comme constantes [INFER] ; tables analogiques : avec les machines analogiques (ticket 14) |

### 1.5 Samples

SP TWINSHOT lit une mémoire d'échantillons en DDR (répertoire de 64 entrées `0x4404f754`) que le CPU #1 charge ; les
tests l'alimentent avec un jeu synthétique (`C:\dev\elektron-work\tools\twinshot_samples.py`). Rien dans l'OS :
**remplaçable** par les samples de l'utilisateur, comme les Samples de MoDule.

### 1.6 Tables recalculées (preuve)

| Table | Formule | Résultat |
|---|---|---|
| S7 `0x4000e488` exp2, 1 025 mots | `round(2^(i/1024)/2 · 2^31)` | 1 025/1 025 exacts |
| S7 log2 de `sqrtLog`, 1 024 mots | `round(log2((k+1)/2048) · 2^31)`, k = 0x400 + i | 1 024/1 024 exacts |
| SRAM voix `0x80004b70`, 257 mots | `round(sin(2πi/256) · 2^31)` | 257/257 exacts |
| SRAM voix `0x8000a888`, 2 047 mots | `round(2^(i/2047) · 2^30)` | 2 047/2 047 exacts |
| S3 vélocité courbe 2, 128 mots | `round(-i/127 · 2^31)` | 128/128 exacts |
| S7 ondes ROM, 31 × 256 | somme des k plus fortes harmoniques (FFT) | 27 ondes à ≤ 3e-5 de pleine échelle avec ≤ 16 harmoniques ; pas exact au bit |
| S3 `Poles`, 512 | f exponentiel en index + 3 formes de pôle | 1 seul mot exact |

Lecture : là où la formule est simple, l'arrondi est `round()` en double, sans piège. Les échecs viennent de formules
non devinées, pas d'un bruit dans les tables. [INFER] Elektron a généré ses tables hors ligne par script ; une
recherche par famille (même forme, autres constantes) devrait en régénérer la plupart.

## 2. Machinedrum (MoDule, mdEngine)

Le port C++ des voix est l'affaire du ticket 17 ; ici, les données seules. Sources : `CLAUDE.md` racine,
`source/elektron/md/mdEngine/README.md`, `tools/mdfw/Firmware.*`, `engine/TrackFx.cpp`, `engine/HostModel.cpp`,
`tools/mds/MdsPackage.h`. L'image flash de 8 Mo (`elektron_sps1-1uw_os1.63.bin`) n'est pas sur ce PC hors de MoDule
(seul le `.syx` est dans `C:\dev\mpc-vst-machinedrum-os`) : aucune table MD n'a été recalculée ici.

| Donnée | Où | Classement |
|---|---|---|
| Code OS 68k (machines, LFO, lissage, routines appelées par `MachineRunner`, image SRAM `0x2622f4`) | OS dans la flash depuis `$4000` | code : disparaît avec le port C++ (ticket 17) |
| Programme du DSP voix (DSP2) : code P + données X/Y | `fw.dspA` | code : port C++ ; ses tables de données : même travail que le Syntakt |
| DSP mixeur (DSP1) : mémoire externe 64 Ki mots et X interne 0x800 (`TrackFx::Tables`, `TrackFx.cpp:22-32`) ; maître P:$342-$970 | `fw.dspB` | tables : calculables ou indispensables, à inventorier. **Précédent** : le sinus de 32 768 points du DSP mixeur est déjà recalculé par la récurrence du boot (`TrackFx.cpp:34-55`, c = cos(2π/32768)), sans le lire |
| Tables de l'OS lues par `HostModel` et MoDule : décroissance `0x24bf94` (`MdsPackage.h:85`, User Machines), gain DIST `$143777/$143878` (ticket 03), tables d'ASSIGN MACHINE (`sysex::applyKitChange`), valeurs par défaut des machines (`Engine::machineDefaults`) | OS | décroissance, DIST : calculables probables ; défauts et ASSIGN MACHINE : **remplaçables** (faits, comme les descripteurs du Syntakt) |
| Banque UW d'usine (ROM-01 à -32) | flash `0x100000`, échantillons 16 bits, en-têtes de 12 mots | **remplaçable** : samples de l'utilisateur (MoDule remplace déjà chacun des 48) ; sans eux, ROM-xx muet, comme ROM-33 à -48 aujourd'hui |
| Table de conversion 16 → 12 bits | flash `0x7a0000`, 65 536 mots (128 Ko) | calculable probable [INFER : une loi de compression], non vérifié |
| Kits d'usine (image de mémoire patch, section 4 sur UW) | `loadPatchImageFromFlash` | **remplaçable** : nos propres kits |

## 3. Les autres moteurs (d'après les recherches déjà écrites)

- **Digitakt II** ([12](12-digitakt-ii.md)) : échantillons d'usine sur le +Drive, pas dans l'OS → samples de
  l'utilisateur. Tables du programme SHARC : pointeurs de filtres `0x8055c874`, courbes de pan à puissance égale
  `0x8055c440`/`0x8055c640`, taux d'enveloppe `0x26ad30`, table de l'Equalizer `0x26bb68`, interpolation polyphasée
  6 prises `0x25d940`, 32 flottants du Legacy LP/HP `0x8045c3c0`. En flottant 32 bits : formules probables (pan =
  sin/cos [INFER]). **Indispensable hors firmware** : les germes RECIPS/RSQRTS sont dans le silicium SHARC, non
  documentés ; un port exact à l'émulateur doit les reproduire (table de l'émulateur) — ni calculable connue ni
  dans le firmware.
- **Digitone II FM DRUM** ([13](13-digitone-ii-fm-drum.md)) : partage les tables du DT2 (`0x26ad30`) ; tables de
  transitoires et de bruit [INFER] ; table de paramètres T1 `0x401f83a4` (321 × 60 o) → remplaçable (faits).
- **Model:Cycles** ([11](11-model-cycles.md)) : moteur sur la ColdFire (section 3), même posture que le CPU #1 du
  Syntakt ; les 512 presets d'usine ne sont pas dans l'OS → nos défauts.
- **Effets du Virus** ([15](15-effets-virus.md)) : les tables que la BootROM écrit en X/Y sont à régénérer par formule
  ou à embarquer (droit à trancher) ; aucune ROM A/B/C chez l'utilisateur, seule la TI (OsTIrus) ; un oracle demande
  la ROM du développeur.

## 4. Taille, effort, distribution

- **Taille des données à régénérer, Syntakt** : ~155 tables en section 7 (zone ~225 Ko + ~45 Ko de tables RAM dans
  l'image de départ), ~20 tables en section 3 (quelques Ko) ; les plus grosses : `Decay3` (64 Ko), les 31 ondes
  (32 Ko). En code généré, quelques centaines de lignes de formules et de constantes par famille.
- **Remplaçables** : 505 descripteurs (min/max/défaut), 12 machines par défaut, 143 mots d'état SRAM du CPU #1,
  l'état des voix, 46 octets machine → moteur. Une à deux sessions [INFER].
- **Effort** [INFER] : un générateur par famille (courbes de décroissance, niveaux, rapports, filtres, ondes,
  pôles), chaque table prouvée par un test « table générée = table du firmware, mot à mot » (exit 77 sans le
  firmware) puis les oracles existants repassés avec les tables générées (`stEngineOracleTest`, `stMainOracleTest`,
  `stSyntaktTest`). Syntakt : 4 à 8 sessions d'agent. Machinedrum : inventaire des tables DSP1/DSP2/OS à faire avec
  le port du ticket 17 ; même ordre de grandeur [INFER].
- **Risque** : une table sans formule trouvée. Choix alors : (a) la machine reste exacte et exige le firmware de
  l'utilisateur (contraire à la décision de la carte) ; (b) approximation par ajustement (erreur mesurée, signalée
  « approximatif » dans le code et l'interface, règle de la carte) ; (c) l'embarquer (question de droit, hors de
  cette recherche).
- **Distribution** : le plug-in livré ne contient que notre code et nos tables générées ; plus de dossier `roms`, plus
  de vérification FNV, plus de message « No Syntakt OS 1.41 .syx ». Les firmwares restent sur le PC du développeur pour
  les tests (variables `SYNTAKT_SECTION3/7`, `GEARMULATOR_MD_FIRMWARE_BIN`). Les samples et les kits d'usine
  disparaissent : l'instrument démarre sur nos kits et joue les samples de l'utilisateur.

## Inconnues

- Couverture : le balayage ne voit que ce que le scénario atteint ; `Decay3` (64 Ko), les tables analogiques et
  certains réglages extrêmes n'apparaissent que par fragments.
- Formules des familles non prouvées (décroissances, résonateur, `Poles`, courbes de vélocité 1 et 3, ondes au bit).
- Le générateur aléatoire des LFO (page `0x402d7000`) : son état de départ n'a pas changé la sortie ici.
- Machinedrum : contenu et nombre des tables DSP1/DSP2/OS, loi de la table 16 → 12 bits (image flash non trouvée sur
  ce PC hors de MoDule).
- RECIPS/RSQRTS du SHARC : comment les reproduire sans le silicium.
- Le statut juridique d'une table régénérée par formule n'est pas traité ici.
