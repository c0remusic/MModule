# 01 — Budget CPU de l'instrument à 16 pistes

Ticket : [issues/01-budget-cpu.md](../issues/01-budget-cpu.md). Mesuré le 2026-10-24 sur le PC de référence (AMD Ryzen 7
3700X, 8 cœurs / 16 fils, 3,6 GHz), builds Release. **Machine chargée pendant toutes les mesures** (autres sessions,
Chrome, Overbridge Engine : 3,4 à 6,6 fils occupés selon le bench lui-même) ; un même bench Syntakt 8 pistes donnait
6,3 % machine calme (commit `37930fe6`) contre 7,1 % ici, donc **les chiffres qui suivent sont sans doute 10-15 % trop
hauts** [INFER : rapport de ces deux seules mesures]. Les moyennes sont stables d'un passage à l'autre (±1 point) ;
les p99 et les pires blocs, eux, reflètent surtout la charge et ne valent pas mesure de gigue.

Cible : 15 % d'un cœur = **100 µs par bloc de 32 échantillons à 48 kHz** (666,7 µs) ou 108,8 µs à 44,1 kHz (725,6 µs).

## 1. Ce qui a été mesuré, et comment

| Élément | Outil | Fichier brut (C:/dev/elektron-work/scratch-arch/research/01/) |
|---|---|---|
| Syntakt (moteur, étages) | `stSyntaktBench` (build `C:/dev/elektron-work/build-main`, reconstruit sur HEAD `d885c0bc`), `--tracks 16 --busy N`, `--tracks 8`, `SYNTAKT_NO_AVX2=1` | `st16-*.log`, `st8.log` |
| Machinedrum par étage et par nombre de pistes actives | harnais `mdBench.cpp` (hors dépôt) lié aux bibliothèques Release du worktree (`temp/cmake_vs22`) : `md::engine::Engine` avec `timingOn` (`HostModel::tickUs`, `dspUs`, `EngineT::masterUs`), puis `mdDrums::Engine` (ce qu'appelle le Device) | `md-run1.log`, `md-run2.log`, `md-run3.log` |
| MoDule entier (plug-in) | `mdDrumsInstancesTest 1 10` et `16 10` (worktree) ; `mdDrumsEngineTest` (sa ligne de CPU) | `inst1.log`, `inst16.log`, `engtest.log` |
| Rééchantillonneur de la pile | harnais `rsBench.cpp` lié à `synthLib` du worktree | `rs-run1.log` |
| Ajouts de l'instrument (n'existent pas encore) | harnais `protoBench.cpp`, noyaux prototypes en float scalaire, `/O2 /arch:AVX2` | `proto-run1.log` |

Les trois harnais et leurs scripts de build (`build*.cmd`) sont dans ce même dossier ; aucun code du dépôt n'a été
touché.

## 2. Syntakt (port exact, `syntakt::Syntakt`)

`stSyntaktBench` : motif chargé (trig sur chaque piste chaque 1/16, 12 moteurs en rotation, LFO, envois delay et
reverb, un bouton bougé par bloc, un lock tous les 4 pas), blocs de 32 à 48 kHz.

| Configuration | CPU #1 contrôle + buildBlock | CPU #2 voix + étages | CPU #1 mix, delay, reverb | **Total moyen** | p99 |
|---|---|---|---|---|---|
| 8 pistes (une machine) | 7,7 µs (1,2 %) | 22,5 µs (3,4 %) | 16,8 µs (2,5 %) | **47,2 µs = 7,1 %** | 10,9 % |
| 16 pistes, 16 actives (3 passages) | 16,2-16,4 µs (2,4 %) | 46,3-46,9 µs (7,0 %) | 28,1-28,7 µs (4,3 %) | **90,8-92,3 µs = 13,6-13,8 %** | 22,1 % |
| 16 pistes, 4 actives | | | | 88,5 µs = 13,3 % | 21,0 % |
| 16 pistes, 0 active | | | | 88,8-103 µs = 13,3-15,5 % (charge montante) | |
| 16 pistes, sans AVX2 (`SYNTAKT_NO_AVX2=1`) | | | | 96,0 µs = 14,4 % | 22,7 % |

Faits :
- **Le coût du Syntakt ne dépend pas du nombre de pistes actives** : 0, 4 ou 16 pistes jouées coûtent pareil. Le port
  rend les 8 voix de chaque CPU #2 à chaque bloc, silencieuses ou non (le MD, lui, saute ses voix muettes, §3).
- 16 pistes = deux CPU #2 et un seul delay + reverb : 13,7 % environ, soit **0,86 % par piste** ; le mix par piste
  croît (16,8 → 28,7 µs), les effets partagés non.
- L'AVX2 rapporte 0,6-0,8 point à 16 pistes (14,4 → 13,7 %).

## 3. Machinedrum (`mdEngine` : voix DSP56300, chaîne de piste, maître) par nombre de pistes actives

`mdBench` : N premières pistes sur `Engine::defaultMachine` (TRX-BD, -SD, -XT, … puis EFM-BD/-SD), trig à vélocité 100
tous les 43 blocs (1/64 à 120 BPM, voix toujours sonnantes) ou tous les 172 blocs (1/16), blocs de 32 à 44,1 kHz, après
`warmUpVoices` et une seconde de chauffe ; « chaîne + mix » = total − tick − DSP − maître (TrackFx et Mixer en C++,
plus les `trigger`). Moyennes de trois passages (écart ≤ 2 points sur le total) :

| N actives | tick OS (68k) | voix DSP56300 | chaîne + mix (C++) | maître DSP56300 | **Total, maître on** | Total, maître off |
|---|---|---|---|---|---|---|
| 0 | 8,7 µs | 4,7 µs | 1,4 µs | 20,9 µs | **35,6 µs = 4,9 %** | 16,3 µs = 2,3 % |
| 1 | 9,5 | 9,8 | 3,6 | 22,5 | **45,4 µs = 6,3 %** | 22,6 µs = 3,1 % |
| 2 | 10,5 | 15,7 | 5,8 | 24,5 | **56,6 µs = 7,8 %** | 30,6 µs = 4,2 % |
| 4 | 10,7 | 23,4 | 9,4 | 24,5 | **68,0 µs = 9,4 %** | 43,2 µs = 6,0 % |
| 8 | 11,4 | 45,7 | 17,0 | 25,2 | **99,3 µs = 13,7 %** | 77,7 µs = 10,7 % |
| 12 | 12,8 | 69,5 | 25,2 | 26,5 | **133,9 µs = 18,5 %** | 96,6 µs = 13,3 % |
| 16 | 13,6 | 85,2 | 32,3 | 26,3 | **157,4 µs = 21,7 %** | 123,5 µs = 17,0 % |

Ajustement linéaire [INFER : régression à l'œil sur ces moyennes] : **maître on ≈ 36 µs + 7,6 µs × N** (5 % + 1,05 %
par piste active) ; maître off ≈ 16 µs + 6,7 µs × N. Par piste active : ~5,0 µs de voix DSP (0,69 %), ~1,9 µs de
chaîne + mix (0,27 %), ~0,3 µs de tick. Fixe : maître 21-27 µs (2,9-3,6 %), tick ~9-14 µs (1,2-1,9 %), voix muettes
~0,3 µs chacune (le saut du `kSkipStub`, `VoiceEngine.cpp:51`).

- La fréquence des trigs (1/64 ou 1/16) ne change rien tant que les voix sonnent : c'est le nombre de voix sonnantes qui
  coûte.
- `mdDrums::Engine` (ce que le Device appelle : précision à l'échantillon, sortie float) : 16 actives **21,9-22,7 %**,
  8 → 13,1-14,4 %, 4 → 8,8-9,6 %, 0 → 5,3-6,3 %. La précision à l'échantillon ajoute ~0,7 point (`mdDrumsEngineTest` :
  20,3 % sans, 21,0 % avec, 16 pistes + maître).
- **MoDule entier** (`mdDrumsInstancesTest`, Processor + Device, blocs de 128, motif chargé sur les 16 pistes) : **23,9 %**
  pour une instance (médiane 0,67 ms par bloc de 2,90 ms, pire 1,40 ms) ; 29-30 % chacune à 16 instances (contention L3
  et SMT [INFER]), 37 MB par instance. Le Processor et le Device ajoutent donc ~1-2 points au moteur.
- Non mesuré : le coût selon la machine (seules TRX et EFM jouent ici ; ROM, PI, E12, User Machines, X.20 peuvent coûter
  autrement) et l'ANTI-CLICK (`Engine::setFade`).

## 4. Rééchantillonnage

`rsBench`, blocs de 128 en sortie, source sans coût (générateur soustrait) :

| Mode | 2 voies | 17 voies (Main stéréo + 15 stems, comme ST Drums) |
|---|---|---|
| MameHq 48 → 44,1 kHz (ST Drums) | 2,7 % | **24,0 %** (1,4 %/voie) |
| MameHq 44,1 → 48 kHz | 3,2 % | 28,8 % |
| Legacy 44,1 → 48 kHz (MoDule) | 0,6 % | 5,9 % |
| Legacy 48 → 44,1 kHz | 0,5 % | 4,1 % |
| **Prototype polyphasé 147/160, 16 coefficients par phase** (`protoBench`, float scalaire) | 0,14 % | **1,2 %** (0,072 %/voie) |

Confirme la décision de la carte (MameHq ~22 % à 17 voies) et la cible d'un filtre court à 1-2 % : un noyau naïf à 16
coefficients y est déjà, sans SIMD. Sa qualité (atténuation, bande passante) n'a pas été évaluée. Avec les options
actuelles, **ST Drums dans un hôte à 44,1 kHz coûte 13,7 + 24 ≈ 38 %** à 16 pistes et 17 sorties.

## 5. Ce que l'instrument ajoute (prototypes, pas le code final)

`protoBench`, par piste, stéréo, 48 kHz, blocs de 128 ; chaque ligne inclut ~0,014 % de remplissage du bloc de test.

| Étage prototype | Par piste | × 16 pistes |
|---|---|---|
| EQ 3 bandes (3 biquads TDF-II par canal) | 0,046 % | 0,7 % |
| Drive, `std::tanh` | 0,120 % | 1,9 % |
| Drive, tanh rationnel | 0,029 % | 0,5 % |
| Drive, tanh rationnel, suréchantillonné 2× (demi-bande 23 coefficients) | 0,299 % | 4,8 % |
| Layer échantillon (Hermite 4 points, enveloppe) | 0,059 % | 0,9 % |
| 4 envois stéréo | 0,029 % | 0,5 % |

Deux drives empilés + EQ : 0,10 %/piste sans suréchantillonnage (1,7 % à 16), 0,64 %/piste avec (10,3 % à 16). **Le
suréchantillonnage des drives est le seul ajout qui pèse** ; l'EQ, le layer et les envois tiennent ensemble sous 2 %.
Non mesurés : les effets d'envoi eux-mêmes (le Syntakt apporte son delay et sa reverb, comptés en §2 ; MoDule son
maître, §3) et la « jonction des effets à 16 pistes ».

## 6. Budget à 16 pistes, ce qui dépasse

Pire cas = les 16 pistes jouées, chaîne complète. Avec les mesures ci-dessus (drives sans suréchantillonnage, filtre
polyphasé court, chaque moteur à sa fréquence) :

| Scénario | Moteur(s) | Ajouts (EQ + 2 drives + layer + envois) | Rééch. | **Total** | Dépassement |
|---|---|---|---|---|---|
| 16 pistes Syntakt | 13,7 % | 1,7 + 0,9 + 0,5 = 3,1 % | 1,2 % (hôte à 44,1) | **~18 %** | +3 points |
| 16 pistes Machinedrum, maître MD | 21,7 % | 3,1 % | 1,2 % (hôte à 48) | **~26 %** | +11 points |
| 16 pistes MD, sans maître MD | 17,0 % | 3,1 % | 1,2 % | **~21 %** | +6 points |
| 8 ST + 8 MD (maître MD) | 7,1 + 13,7 % | 3,1 % | 1,2 % | **~25 %** | +10 points |
| Idem, drives suréchantillonnés 2× | | +8,6 % | | **~34 %** | +19 points |

Budget proposé par élément pour tenir 15 % (100 µs par bloc de 32 à 48 kHz) [INFER : répartition, pas une mesure] :
moteurs ≤ 10 % au pire (16 pistes MD) ; chaîne ajoutée ≤ 2 % (≤ 0,12 %/piste, donc drives sans suréchantillonnage
ou suréchantillonnage seulement sur les drives actifs) ; layer ≤ 1 % ; envois + effets partagés ≤ 1 % au-delà de ceux
des moteurs ; rééchantillonnage ≤ 1 %. **Ce qui dépasse : le moteur Machinedrum (21,7 % → 10 %, à diviser par ~2,2)
et, moins, le Syntakt (13,7 % → 10 %).**

## 7. Leviers connus

Machinedrum (par ordre de poids au pire cas) :
1. **Voix DSP56300 : 85 µs = 11,7 %** à 16 actives. Le JIT de Gearmulator a déjà sa série « left-aligned ALU »
   (`bb0a8463` est ancêtre du sous-module `source/dsp56300`, HEAD `77ebd463`) : le levier documenté
   (performance-research.md §3, 9,1 % des instructions émises en extensions de signe) est donc déjà pris. Ce qui reste :
   un port statique en C++ des programmes de voix, machine par machine, comme le Syntakt (son port a été divisé par 5,
   commit `6fb39aef`, puis gagné encore avec l'AVX2) [INFER : gain du même ordre sur le MD, non mesuré ; effort élevé,
   une cinquantaine de machines]. Les threads sont exclus (ticket 25 de MoDule). La carte a déjà décidé « aucun
   émulateur au rendu » (le MD porté en C++, mdEngine en oracle) : ce levier est donc le plan, et ces mesures sont la
   référence que le port doit battre. Cohérent avec le ticket 03 (voix brute seule, 16 pistes ≈ 15,6 %).
2. **Chaîne + mix en C++ : 32 µs = 4,5 %**. Exécution des 16 TrackFx en SoA sur les pistes (R6 de
   performance-research.md) et mixer AVX2 (R1) [INFER : ×2 environ, ~2 points].
3. **Maître DSP56300 : 21-27 µs = 3-3,6 %**, fixe. Soit l'instrument ne le fait pas tourner (ses propres envois), soit un
   port C++ exact du programme maître (un seul programme, mieux borné que les voix).
4. **Tick 68k : 9-14 µs = 1,2-1,9 %**, fixe : port C++ des routines de l'OS (machines, lissage, LFO) au lieu de Musashi.
5. Précision à l'échantillon : 0,7 point ; à garder (latence).

Syntakt :
1. **Sauter les voix muettes** comme le MD : aujourd'hui 0 piste active coûte 13,3 % ; ne change rien au pire cas,
   beaucoup au cas courant.
2. R3-R7 de performance-research.md non encore faits (R1/R2/AVX2 faits, `89b06207`, `37930fe6`) : [INFER, leur
   estimation] 36-40 µs pour 8 pistes au lieu de ~44-47, soit ~11 % à 16 pistes.
3. PGO / clang-cl (R5) : 5-10 % [INFER, guide Microsoft].

Communs : filtre polyphasé court (fait la différence 24 % → 1,2 %) ; drives sans suréchantillonnage par défaut ;
mesurer sur machine calme et au pire bloc avant de trancher.

## Inconnues

- Chiffres sur machine calme (ici chargée ; ~10-15 % de trop [INFER]) et gigue sous MMCSS : à refaire.
- Coût MD selon la machine (ROM, PI, E12, User Machines, X.20) et avec l'ANTI-CLICK.
- Coût réel de la chaîne, du layer, des envois et de la jonction des effets dans l'instrument (prototypes seulement).
- Qualité du filtre polyphasé à 16 coefficients.
- Gain réel d'un port C++ des voix MD.
