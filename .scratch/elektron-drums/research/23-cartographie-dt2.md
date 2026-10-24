# 23. Cartographie du firmware du Digitakt II (OS 1.16)

Recherche du 2026-10-24, ticket [23-cartographie-dt2](../issues/23-cartographie-dt2.md). Suite de
[12-digitakt-ii](12-digitakt-ii.md), dont elle corrige deux points (section 0).

Marques : **[V]** lu ici dans le code désassemblé de l'image 1.16 ; **[D]** dit par digikit ou dn2-cfw, non relu ici ;
**[INFER]** déduit ici ; **[O]** inconnu. Adresses DSP en short-word (sw), comme digikit.

## Méthode et outils

- Firmware : `%APPDATA%\Elektron Emu\Digitakt II\Firmware\Digitakt_II_OS1.16.syx`, extrait par
  `tools.firmware.extract` de digikit (commit `9dee5d5`, copie `C:\dev\elektron-work\scratch-arch\ext\digikit`) vers
  `C:\dev\elektron-work\scratch-arch\research\23-dt2\sections\` (section 7 = blob DSP, 321 016 octets ; section 3 =
  OS ColdFire, chargé en `0x40000400`).
- Inventaire : `tools.sharc_dev.sharcinv` de digikit sur la section 7 : **1 147 fonctions** (blk69 : 661, blk93 : 453,
  blk88 : 31, blk76/78 : 1 chacun), bornes, graphe d'appels statique, vecteur d'opérations. Sortie :
  `scratch-arch\research\23-dt2\inv.json`.
- Lecture : base sharcdb de digikit (`sharc.load("dt2-1.16")`, construite en 30 s sous Windows), désassemblage par
  `scratch-arch\research\23-dt2\q.py listing`, tables de sauts par `q.py xref`. Listings gardés :
  `l_1c642a.txt` (rendu des voix et chaîne de piste), `l_1c71ec.txt`, `l_1c18a6.txt`.
- Scripts (tous dans `scratch-arch\research\23-dt2\`) : `tree.py` (arbre d'appels), `info.py` (fiche et fermeture
  transitive d'une routine), `est.py` (instructions par appel, pondérées par les boucles), `closure_md.py` (annexe).
- ColdFire : `C:\dev\elektron-work\tools\cfdis.py` sur la section 3.

Toute la chaîne d'outils Python de digikit tourne sous Windows avec `uv` (Python 3.12 téléchargé, 3 paquets) : rien
à corriger. Le cœur SHARC concret en Python (`sharc_run`) aussi, mais il n'a pas servi à mesurer (section 6).

## 0. Corrections de la recherche 12

- **[C] La table `0x8055c874` ne contient pas les filtres.** C'est une table de sauts du compilateur (7 entrées,
  index 0 à 6) vers des blocs de cas de `0x1c642a` et de ses blocs hors ligne `0x1c71ec`. Chaque cas appelle un
  filtre. L'index 0 saute la chaîne de filtre (`0x1c6eb7`). Un index ≥ 7 la saute aussi (`compu R2, 7` à
  `0x1c6c10`). Voir 2.3. Le fond de la recherche 12 tient : les six cas appellent bien les routines citées.
- **[C] `0x1cdd8b` n'est pas « niveau », c'est le BR (bit reduction).** C'est la routine que la recherche 12 n'avait
  pas trouvée. Son corps actif est hors ligne, en `0x1cddff` : `x*127`, interpolation dans une table de 128 entrées
  `0x2c73d0`, `trunc` et `float_by` (quantification par puissance de deux), boucle SIMD de 14 paires. Elle est appelée
  pour les 32 voix juste après le rendu (`0x1c6b21`), sinon elle copie (contournement si le paramètre ≤ 0). C'est l'ordre
  du manuel : SYN, puis BIT, puis overdrive et filtre. **[V]** pour le code, **[INFER]** pour le nom.

## 1. Forme du programme DSP **[V]** (sauf mention)

Le programme du SHARC+ tourne par bloc de 32 échantillons à 48 kHz. Les voix sont rendues à 96 kHz puis décimées.

```
0x1c75d8  boucle de bloc (convertisseur Q31 en 0x1c7734)
 └ 0x1c2b24  orchestrateur de trame (766 instr.)
    ├ 0x1c24e9  dépaquetage des mots de paramètres de la trame vers les fiches de piste (396)
    ├ 0x1c29fd  reprise de pas des voix (16 pistes), appelle le setter de pas 0x1c4afe
    ├ 0x1c642a  voix et chaîne de piste (1 464 instr. ; blocs hors ligne 0x1c71ec, 235)
    ├ 0x1c18a6  envois et effets : chorus 0xb82680, delay 0xb82150, reverb 0xb80fe0 (654)
    ├ 0x1c207b  master : dynamics 0xb82d41, overdrive 0x1cb3d8, clip, rampe de gain (303)
    ├ 0x1c14e7, 0x1c80f2, 0x1c28b5  post-master, sortie USB [D] (finding 55 de digikit)
    └ 0x1c367e, 0x1c3429, 0x1c149b ×16, 0xb82e4f, 0x1c33fe-0x1c39bf : compteurs, entrée audio, petits helpers
```

La fermeture statique depuis `0x1c75d8`, plus les bornes manquées par l'inventaire (delay, reverb, corps hors ligne du
BR, du SRR, de la voix et de STRETCH), compte **123 fonctions et 12 278 instructions** (annexe). Les 1 024 autres
fonctions de l'image sont le démarrage, le DMA, l'audio d'entrée, l'enregistrement, des routines atteintes par pointeur
sans appel statique (675 fonctions n'ont aucun appelant statique), ou du code mort. Leur rôle n'est pas établi **[O]**.

### 1.1 Ordre réel de la chaîne de piste, dans `0x1c642a` **[V]**

Elle travaille passe par passe, et non voix par voix. Chaque passe boucle sur les 32 voix (`R14 = 0x20`). Une voix
mono vaut une piste, une piste stéréo prend deux voix (L paire, R impaire, [D]).

| # | Passe (site) | Routine | Garde |
|---|---|---|---|
| 1 | setup par voix, sélecteur de machine (table `0x8055c840`, `0x1c6569`) | setters de pas `0x1c4afe`/`0x1c4bf9`/`0x1c4d88`/`0x1c4a31`, reset `0x1c4eaf` | à chaque trig ou changement |
| 2 | setup de filtre par voix (table `0x8055c858`, `0x1c66ec`) | setter du type courant (2.3), reset si le type change (`DM(I1-0x41)` contre `DM(I5-19)`) | à chaque bloc |
| 3 | setup de base-width et d'enveloppe (`0x1c6a03`, `0x1c6a1e`) | `0x1ccc58` (biquad), `0x1cba9e` (enveloppe, 5 arguments) | à chaque bloc |
| 4 | rendu (`0x1c6ae8`) | `0x1c4ecf`, ou `0x1c5576` si STRETCH | octet actif `+0x1b8` |
| 5 | BR (`0x1c6b1b`) | `0x1cdd8b` → `0x1cddff` | paramètre > 0 |
| 6 | rampe de gain inline (`0x1c6b46`, DO 32) | dans `0x1c642a` | masque de voix `R15` |
| 7 | overdrive pré-filtre (`0x1c6b96` → `0x1c6c52`) | `0x1cb3d8` | mot de routage `DM(I6-21)` |
| 8 | SRR pré-filtre (`0x1c6bae` → `0x1c6c3d`) | `0x1cdecb` → `0x1cdf38` | `DM(I6-20)` |
| 9 | base-width pré-filtre (`0x1c6bc4` → `0x1c6c28`) | `0x1ccbd8` | `DM(I6-22)` |
| 10 | filtre (`0x1c6c0b`, table `0x8055c874`) | un des cinq filtres (2.3) | index 1-6 |
| 11 | overdrive post-filtre (`0x1c6ed3` → `0x1c7380`) | `0x1cb3d8` | `DM(I6-21)`, test inverse |
| 12 | SRR post-filtre (`0x1c6eeb` → `0x1c736b`) | `0x1cdecb` | `DM(I6-20)` |
| 13 | base-width post-filtre (`0x1c6f01` → `0x1c7356`) | `0x1ccbd8` | `DM(I6-22)` |
| 14 | enveloppe d'amplitude (`0x1c6f29`) | `0x1cbb57` (fiche de `0x2c` octets par voix) | toutes |
| 15 | rampe de gain inline (`0x1c6f4b`, DO 32) | | |
| 16 | mise à l'échelle par voix (`0x1c6fb6`, DO 32, division `0x1c06ba`) | | |
| 17 | mix des 16 pistes et pan (`0x1c7040`, DO 16) | courbes à puissance égale `0x8055c440`/`0x8055c640` dans `0x1c71ec` [D] | |

Les trois paires pré/post (overdrive, SRR, base-width) sont le routage pré ou post filtre du manuel (« SRR Routing »,
« Overdrive Routing », « BW Routing » : manuel du Digitone II, `scratch-arch\ext\manual\dn2.txt` l. 3059-3171 ; même
chaîne partagée sur le DT2) **[INFER]** : le code est vu, le sens du mot de garde n'est pas lu côté ColdFire.

## 2. Inventaire des routines audio

Colonnes : **Instr.** = taille statique (inventaire). **Par appel** = `est.py` : instructions pondérées par les boucles
littérales, les boucles à compte en registre prises à n = 32 (taille de bloc) ; toutes les branches comptent, c'est donc
une borne haute statique, et non une mesure. Les entrées suivent la convention du compilateur : R4, R8, R12, puis la pile
(`DM(I6, M6)`) [D, finding 53 de digikit].

### 2.1 Voix du sampler

| Routine | Adresse | Instr. | Par appel | Entrées / état | Appels | État |
|---|---|---|---|---|---|---|
| Rendu, sélecteurs 0, 1, 3, 4, 5 (ONESHOT, WERP, REPITCH, SLICED, MIDI/MANUAL) | `0x1c4ecf` (coquille) + `0x1c4f81` (corps) | 84 + 381 | 84 + 6 240 | R4 = fiche voix `0x2412cc + 0x1d8 v`, R12 = R15 ; fiche : tampon de 64 flottants, pas Q31 aux mots 0x6a-0x6b, phase 0x6c-0x6d [D/V finding 06] | `0xb80000`, `0x1c06ba` | [V] appel et bornes |
| Décimateur 2:1 (96 → 48 kHz) | `0xb80000` | 99 | 909 | état à la fiche `+0x104` | — | [V] appel, [D] rôle |
| STRETCH, sélecteur 2 | `0x1c5576` (coquille) + `0x1c5615` (corps) | 76 + 384 | 76 + 3 571 | même fiche | `0xb88f70`, `0x1c06ba`, `0xb80105` | [V] appel |
| Reset de voix | `0x1c4eaf` | 15 | 15 | fiche | — | [V] |
| Setters de pas (Q31) | `0x1c4afe`, `0x1c4bf9`, `0x1c4d88`, `0x1c4a31` | 97, 135, 94, 71 | — | note, tune, rate ; `powf(2, …/12)` | `0x1c0d68`, `0x1c4914` | [V] appels ; formule [D/V finding 06] |
| Bornes de boucle et début | `0x1c4914` | 87 | — | | `0xb88f70` | [D] |

Les deux boucles DO 64 de `0x1c4f81` sont deux chemins exclusifs [INFER]. Le coût réel d'une voix active tombe alors
vers 3 100 + 909 ≈ 4 000 instructions. Cela recoupe la mesure de digikit : une voix sample coûte 4 000 instructions de
plus qu'une voix sinus (finding 06, l. 5597).

### 2.2 Étages de la piste

| Routine | Adresse | Instr. | Par appel | Entrées / état | Coefficients | État |
|---|---|---|---|---|---|---|
| BR (bit reduction) | `0x1cdd8b` → `0x1cddff` | 55 + 91 | 286 actif | R4 = fiche (pas 0x14), R8 = entrée, R12 = sortie, n sur pile ; table `0x2c73d0` (128) | DSP, dans la routine | [V] code, [INFER] nom |
| Overdrive (saturateur) | `0x1cb3d8` | 98 | 532 | R4 = fiche (pas 0x60), R8 = R12 = tampon de voix | DSP (division `0x1c06ba`) | [V] appels ; aussi master |
| SRR | `0x1cdecb` → `0x1cdf38` | 51 + 46 | 480 actif | R4 = fiche, R8 = R12 = tampon ; sample-and-hold [D] | DSP | [V] appels |
| Base-width (HP + LP en tandem, manuel) | `0x1ccbd8` (process), `0x1ccbca` (reset), `0x1ccc58` (setter) | 59, 8, 33 | 214 | fiche de 8 mots, coefficients +2..+5, état +6, +7 [D] ; setter : `DM(I5-18)`, `DM(I5-17)` (BASE, WDTH [INFER]) | DSP (`0x1ccc58`) | [V] appels ; aussi HPF/LPF du delay et de la reverb |
| Enveloppe d'amplitude | `0x1cbb57` (process), `0x1cba9e` (setter) | 104, 54 | 383 | fiche 11 mots, table de taux `0x26ad30` [D] ; setter : 5 arguments `DM(I5-3..-7)` (ATK, HOLD, DEC, SUS, REL [INFER]) | DSP | [V] appels |
| Rampes de gain, mix, pan | inline dans `0x1c642a` / `0x1c71ec` | | ~5 985 pour le bloc entier, hors appels | | | [V] boucles |

### 2.3 Les six filtres, rôles vérifiés **[V]**

Le setup (table `0x8055c858`, index lu en `DM(I0-2)`) et le traitement (table `0x8055c874`, index lu en `DM(I1)`) prennent
le même index de type, de 0 à 6. Chaque type a son état, à une base fixe depuis `I9`, avec un pas par voix :

| Index | Type (menu = index − 1) | Process | Setter | Reset | État (base, pas) | Par appel |
|---|---|---|---|---|---|---|
| 0 | aucun | — | — | — | — | 0 |
| 1 | MULTI-MODE | `0x1cdbb2` (115) | `0x1cd906` (71, appelle `0x1cd68f`, `0x1c06ba`) | `0x1cd5dd` (76) | `I9+0x5204`, 0xcc | 642 |
| 2 | LOWPASS 4 | `0x1cd286` (148) | `0x1cd3b5` (135, `0x1c06ba`) | `0x1cd4fd` (21) | `I9+0x8f04`, 0x12c | 1 140 |
| 3 | EQUALIZER | `0x1cc79e` (173, `0x1c06ba`) | `0x1cc9b4` (73, `0x1c06ba`) | `0x1cca61` (63) | `I9+0xb484`, 0xec | 2 188 |
| 4 | COMB- | `0x1cbf07` (133, anneau de 8 192) | `0x1cbe19` (100, `powf` + division) avec **−1.0** sur la pile (`0x1c6dae`) | `0x1cbdea` (23) | `I9+0x6b84`, 0x48 | 1 590 |
| 5 | LEGACY LP/HP | `0x1ccd96` (99, table `0x8045c3c0`) + boucle de mélange inline `0x1c7415` | `0x1ccfa4` (181, `0x1c06ba`) | `0x1cd137` (50) | `I9+0x7484`, 0xd4 | 489 + ~130 |
| 6 | COMB+ | `0x1cbf07` | `0x1cbe19` avec **+1.0** sur la pile (R10 = `0x3f800000`) | `0x1cbdea` | `I9+0x6b84` | 1 590 |

Pourquoi « index = type + 1 » est vérifié : les deux peignes tombent sur les index 4 et 6, et leur setter reçoit −1.0
pour le 4 et +1.0 pour le 6. Avec l'ordre du menu de la ColdFire (0 MULTI-MODE, 1 LOWPASS 4, 2 EQUALIZER, 3 COMB-,
4 LEGACY, 5 COMB+, finding 02 de digikit), seul le décalage d'un cran met COMB- et COMB+ sur ces deux index avec ce
signe. Le reste en découle. Le nom de chaque type vient du menu, pas du code : que `0x1cdbb2` soit un SVF « state
variable » n'a pas été prouvé sur l'algorithme **[O]**.

**Qui calcule les coefficients : le DSP.** La trame porte des mots s16 lissés (échelle 1/32 512 ou 1/127 : constantes
`0x3c010204` = 1/127 et `0x3f810204` = 128/127 dans le setup, `0x1c6e50`). Les cinq setters appellent la division
`0x1c06ba`, et le peigne appelle aussi `powf 0x1c0d68`. La ColdFire n'envoie aucun coefficient **[V]** côté DSP.

### 2.4 Effets d'envoi et master

| Routine | Entrée | Fermeture statique | Coût mesuré (DN2, même code, dn2-cfw T3.6) | Sous-routines | État |
|---|---|---|---|---|---|
| Pilote des envois | `0x1c18a6` @ `0x1c3090` | 654 instr., 11 008 pondérées (9 boucles de 30-32 et 15-16) | — | appelle chorus `0x1c1d3d`, delay `0x1c1db4`, reverb `0x1c1e6d` | [V] |
| Chorus | `0xb82680` | 11 fonctions, 559 | 14 457 | `0xb822c0`, `0xb8245d`, `0xb82543`, `0xb825ef`, `0x1cc419` ; état DDR `0x80459388` [D] | [V] appel |
| Delay | `0xb82150` (dans la fonction `0xb820b1` de l'inventaire) | 12 fonctions, 1 059 | 5 424 | setter `0xb81f28` (division, `0xb88e04`, biquad `0x1ccc58`), noyaux `0xb817fc`, `0xb81896`, `0xb819bd`, `0xb81baf`, HPF/LPF `0x1ccbd8` ; anneaux DDR `0x8005912c` [D] | [V] appel |
| Reverb | `0xb80fe0` (dans `0xb80fd0`) | 15 fonctions, 1 405 | 14 563 | `0xb8037f`, `0xb80450`, `0xb8063e` (602), `0xb80cdf`, `0xb80d91`, `0xb80e40`, biquad `0x1ccbd8` ; état `0x80000018` [D] | [V] appel |
| Master | `0x1c207b` @ `0x1c3099` | 303 instr., 3 172 pondérées | — | dynamics `0xb82d41` (`0x220a`), **overdrive du master = le même saturateur `0x1cb3d8`** | [V] |
| Compresseur (dynamics) | `0xb82d41` → `0xb82cba` | 11 fonctions, 716 | 16 560 | `0xb828f7`, `0xb82994`, `0xb82a35`, `0xb82b09`, `0xb82bf6` (`powf`) | [V] appel |
| Égaliseur du master | — | — | — | aucun appel d'EQ dans `0x1c207b` | **absent** [V] statique |
| Post-master, USB | `0x1c14e7`, `0x1c80f2`, `0x1c28b5` | 98 / 43 / 127 ; 2 705 / 43 / 8 133 pondérées | — | | [D] rôle |

Les adresses des effets du DN2 que dn2-cfw a mesurés (`0xb8482c` delay, `0xb84d5c` chorus, `0xb836bc` reverb, `0xb8541d`
compresseur, dans `tools/sharcvm/fx.py`) ne sont pas celles du DT2. Que ce soit le même code relocalisé vient de
digikit (finding 53 : hash de relocation égal) **[D]**, non revérifié ici. Les coûts mesurés de la colonne DN2 sont donc
une approximation pour le DT2.

### 2.5 Helpers mathématiques **[V]** (bornes)

| Routine | Adresse | Instr. | Appelants |
|---|---|---|---|
| Division flottante (appel simple, R4/R8 → F0) | `0x1c06ba` | 47 | 30 fonctions, dont les 5 setters de filtre, la voix, le saturateur |
| powf | `0x1c0d68` | 127 | 15, dont les setters de pas, le peigne, le compresseur |
| Racine inverse | `0x1c127c` | 23 | aucun appel statique |
| int → float | `0x1c0874` | 13 | aucun appel statique |
| int64 / conversions | `0xb88f06` (dans `0xb88ef4`), `0xb88f70` | 54, 27 | setters de pas, STRETCH |

### 2.6 LFO et enveloppes

- **Enveloppe d'amplitude : sur le DSP**, une par voix (`0x1cbb57`, 2.2) **[V]**.
- **Enveloppe de filtre : pas de routine d'enveloppe dédiée sur le DSP.** Les setters de filtre reçoivent la coupure déjà
  modulée par des flottants calculés inline dans le setup (`F1 = F5*F1 + …`, `fmax`/`fmin`, `0x1c6e7b`-`0x1c6e8b`).
  Que l'enveloppe de filtre soit sur la ColdFire ou inline ici n'est pas tranché **[O]**.
- **LFO : sur la ColdFire.** Tick `0x40139342` (trois LFO, `moveq #2` en `0x4013935e`), noyau de modulation
  `0x400d9354` (`msac.w` avec chargement, quatre destinations), lissage à un pôle `0x400d92a2` (`mac.w` avec coefficients
  `0x7c29`/`0x03d7`, 0,97/0,03 en Q15 [INFER]), handler du vecteur 191 (construction de la trame à chaque bloc)
  `0x4002dd0c` : les quatre prologues ont été relus avec `cfdis.py` **[V]** (adresses : finding 04 de digikit [D]).

## 3. ColdFire : conversion des paramètres

Ce que la ColdFire fait d'un paramètre de son, d'après le code DSP lu ici et les findings 04 et 53 de digikit :
1. valeur du kit, puis miroir ;
2. LFO et matrice de modulation, ajoutés dans le tableau de valeurs (`0x40139342`, `0x400d9354`) ;
3. lissage à un pôle (`0x400d92a2`) ;
4. paquetage en mots s16 dans la trame DSPI2 (handler `0x4002dd0c`, copie DSP `0x2558dc`).

Côté DSP, `0x1c24e9` dépaquette la trame. Les setters convertissent ensuite les mots en grandeurs musicales : pas de
lecture, coefficients de filtre, temps de delay. **[V]** côté DSP, **[INFER]** qu'aucune autre conversion n'existe côté
ColdFire. La table des descripteurs de paramètres est en `0x4020f59c` sur 1.17 (275 lignes de 60 octets, finding 53)
[D]. Son adresse sur 1.16 n'est pas relevée ici **[O]**. Le port côté ColdFire se réduit donc aux LFO, au lissage et au
format de trame. C'est beaucoup moins que pour le CPU #1 du Syntakt.

## 4. L'oracle est-il prêt ?

| Niveau | État ici, sous Windows |
|---|---|
| Outils statiques de digikit (extraction, sharcinv, sharcdb, listing, xref) | **prêts**, mesuré : extraction 1 s, inventaire 9 s, base 30 s |
| Routine isolée en Python (`tools.sharc_dev.sharc_run`, cœur concret) | présent, non essayé : il faut monter la pile (I6/I7) et l'état des fiches à la main **[O]** |
| sharcvm (dn2-cfw, C + ctypes) | **pas prêt** : `vm.py` compile avec `cc` et charge un `.dylib`, aucun gcc/clang/cl dans le PATH ; ses images mémoire et `dn2_after_init` ne visent que le DN2 (`out/T3.2/mem`). Porter le chargeur au DT2 est à faire **[O]** |
| Programme entier (`coupled-run` de digikit, Rust) | **pas prêt** : Rust 1.98.1 épinglé (`mise.toml`), alors que 1.96 et stable sont installés ; il faut un cœur AOT généré par image (`mise run perf:aot -- dt2 --card-image IMG`), une image de carte eMMC, et les instantanés ColdFire et DSP après le boot (`digi-audio-m5.snap`, `.snap.dsp`, `tools/emulator/digi_env.py` `ready_inputs`) ; rien de cela n'est sur ce PC |

## 5. Coût par routine

**Non mesuré.** La seule mesure par PC qui existe est le diagnostic `SHARC_CYCMIX` de `coupled-run`
(`tools/sharc_dev/sharc_cpi.py`), qui est bloqué (section 4). Ce qui est disponible :

- **Mesuré par d'autres** : programme DT2 entier, 208 600 instructions occupées par bloc (`dt2-sample-trig`, finding 06
  l. 5801), soit 35,5 % du SHARC à 1 GHz ; une voix sample, +4 000 instructions ; effets du DN2 : chorus 14 457, delay
  5 424, reverb 14 563, compresseur 16 560 (dn2-cfw, sharcvm).
- **Statique ici** (`est.py`, borne haute, boucles en registre à n = 32), par voix active et par bloc : rendu ≈ 4 000
  (après la correction des chemins exclusifs, 2.1), BR 286, overdrive 532, SRR 480, base-width 214, filtre 489-2 188
  selon le type, son setter 70-300, enveloppe 383. Total : **≈ 6 500-8 400 par voix active**, soit 105-135 k pour
  16 pistes mono [INFER]. Le fond fixe vaut ≈ 3 900 (orchestrateur), plus 12 700 (dépaquetage, ×32 [D]), plus 11 000
  (envois), plus 3 200 (master), plus 10 900 (post-master et USB, inutile au plug-in), plus 5 985 (inline de
  `0x1c642a`). Pour comparaison, le Machinedrum (ticket 17) fait 11 385 instructions statiques pour 44 renders.

## Inconnues

- Le coût mesuré de chaque routine : il faut `coupled-run` avec `SHARC_CYCMIX` (Rust 1.98.1, cœur AOT, carte,
  instantanés), ou un chargeur DT2 pour sharcvm avec un compilateur C. Estimation : 1 à 3 jours agent **[INFER]**.
- L'algorithme exact de chaque filtre (SVF ou non, pentes), le sens des mots de routage pré/post, l'emplacement de
  l'enveloppe de filtre.
- Le rôle des 1 024 fonctions hors de la fermeture audio (675 n'ont pas d'appelant statique), dont `0x1cb4b2` (183,
  sans appelant) et les petites fonctions `0x1c5b17`-`0x1c5ed4` qui appellent powf.
- La table des descripteurs et le format de trame sur 1.16 (adresses de 1.17 seulement) ; les tables de conversion de
  la ColdFire, s'il en reste.
- Que les effets du DT2 soient le même code que ceux du DN2 mesurés par dn2-cfw : affirmé par digikit, non revérifié.

## Annexe : toutes les fonctions atteintes par la boucle de bloc

Fermeture statique depuis `0x1c75d8`, plus `0x1c71ec`, le delay `0xb820b1`, la reverb `0xb80fd0` et les corps hors ligne
`0x1cddff`, `0x1cdf38`, `0x1c4f81`, `0x1c5615`. Sortie de `closure_md.py` sur l'inventaire sharcinv ; les étiquettes sont
les heuristiques de digikit, et non des rôles vérifiés.

| Fonction | Instr. | Boucles littérales | Appelée par | Appelle | Étiquette digikit |
|---|---|---|---|---|---|
| `0x1c06ba` | 47 | - | `0xb80314` `0xb81cbf` `0xb81f28` `0xb82875` `0x1c35cd` `0x1c4f81` `0x1c5615` `0x1c5ed4` `0x1c642a` `0x1cb3d8` `0x1cb4b2` `0x1cb647` `0x1cb85f` `0x1cbc44` `0x1cbe19` `0x1cbf07` `0x1cc6c4` `0x1cc6f2` `0x1cc79e` `0x1cc935` `0x1cc9b4` `0x1ccae8` `0x1cce70` `0x1ccfa4` `0x1cd3b5` `0x1cd72f` `0x1cd77c` `0x1cd814` `0x1cd906` `0x1cd9b0` | - | IIR or recurrence |
| `0x1c0d68` | 127 | - | `0xb82994` `0xb82b09` `0xb82e4f` `0x1c4afe` `0x1c4bf9` `0x1c5b17` `0x1c5bbb` `0x1c5c4b` `0x1c5ed4` `0x1c642a` `0x1cb343` `0x1cbe19` `0x1cc935` `0x1cce70` `0x1cd77c` | - | envelope or gain |
| `0x1c149b` | 31 | - | `0x1c2b24` | - | unclassified/mixed |
| `0x1c14e7` | 98 | 256 2 16 32 2 16 | `0x1c2b24` | - | IIR or recurrence |
| `0x1c18a6` | 654 | 31 15 32 31 31 16 31 30 31 | `0x1c2b24` | `0xb82680` | interpolating table lookup / wavetable oscillator |
| `0x1c207b` | 303 | 16 16 16 16 16 16 3 15 | `0x1c2b24` | `0xb82d41` `0x1cb3d8` | block copy / move |
| `0x1c24e9` | 396 | - | `0x1c2b24` | - | parameter converter (bitfield unpack) |
| `0x1c28b5` | 127 | 63 16 14 32 14 32 | `0x1c2b24` | - | wavetable/indexed lookup (pointer table) |
| `0x1c29fd` | 83 | 16 | `0x1c2b24` | `0x1c4afe` `0x1c5334` | block copy / move |
| `0x1c2b24` | 766 | 512 16 512 16 32 | `0x1c75d8` | `0xb82e4f` `0x1c149b` `0x1c14e7` `0x1c18a6` `0x1c207b` `0x1c24e9` `0x1c28b5` `0x1c29fd` `0x1c33fe` `0x1c3406` `0x1c340e` `0x1c3416` `0x1c341e` `0x1c3429` `0x1c3486` `0x1c3493` `0x1c3504` `0x1c355c` `0x1c3570` `0x1c367e` `0x1c398f` `0x1c399f` `0x1c39af` `0x1c39bf` `0x1c642a` `0x1c80f2` | orchestrator/dispatcher |
| `0x1c33fe` | 4 | - | `0x1c2b24` | - | glue/trampoline |
| `0x1c3406` | 4 | - | `0x1c2b24` | - | glue/trampoline |
| `0x1c340e` | 4 | - | `0x1c2b24` | - | glue/trampoline |
| `0x1c3416` | 4 | - | `0x1c2b24` | - | glue/trampoline |
| `0x1c341e` | 5 | - | `0x1c2b24` | - | block copy / move |
| `0x1c3429` | 17 | - | `0x1c2b24` | - | DMA/descriptor construction |
| `0x1c3486` | 6 | - | `0x1c2b24` | - | block copy / move |
| `0x1c3493` | 4 | - | `0x1c2b24` | - | glue/trampoline |
| `0x1c349b` | 11 | - | `0x1c3504` `0x1c38a6` | - | DMA/descriptor construction |
| `0x1c3504` | 16 | - | `0x1c2b24` | `0x1c349b` | unclassified/mixed |
| `0x1c355c` | 9 | - | `0x1c2b24` | - | block copy / move |
| `0x1c3570` | 16 | - | `0x1c2b24` | - | block copy / move |
| `0x1c367e` | 211 | 32 32 32 7 | `0x1c2b24` | - | wavetable/indexed lookup (pointer table) |
| `0x1c392a` | 10 | - | `0x1c398f` `0x1c399f` `0x1c39af` `0x1c39bf` | - | block copy / move |
| `0x1c398f` | 8 | - | `0x1c2b24` | `0x1c392a` | glue/trampoline |
| `0x1c399f` | 8 | - | `0x1c2b24` | `0x1c392a` | glue/trampoline |
| `0x1c39af` | 8 | - | `0x1c2b24` | `0x1c392a` | glue/trampoline |
| `0x1c39bf` | 8 | - | `0x1c2b24` | `0x1c392a` | glue/trampoline |
| `0x1c4914` | 87 | - | `0x1c4a31` `0x1c4afe` `0x1c4d88` | `0xb88f70` | envelope or gain |
| `0x1c4a31` | 71 | - | `0x1c642a` | `0x1c4914` | wavetable/indexed lookup (pointer table) |
| `0x1c4afe` | 97 | - | `0x1c29fd` `0x1c642a` | `0x1c0d68` `0x1c4914` | envelope or gain |
| `0x1c4bf9` | 135 | - | `0x1c642a` | `0x1c0d68` | envelope or gain |
| `0x1c4d88` | 94 | - | `0x1c642a` | `0x1c4914` | wavetable/indexed lookup (pointer table) |
| `0x1c4eaf` | 15 | - | `0x1c2ac9` `0x1c642a` | - | unclassified/mixed |
| `0x1c4ecf` | 84 | - | `0x1c642a` | - | block copy / move |
| `0x1c4f81` | 381 | 64 64 | - | `0xb80000` `0x1c06ba` | interpolating table lookup / wavetable oscillator |
| `0x1c5334` | 71 | - | `0x1c29fd` | - | DMA/descriptor construction |
| `0x1c5576` | 76 | - | `0x1c642a` | - | block copy / move |
| `0x1c5615` | 384 | 32 15 32 | - | `0xb88f70` `0x1c06ba` | interpolating table lookup / wavetable oscillator |
| `0x1c642a` | 1464 | 32 32 32 16 16 | `0x1c2b24` | `0xb80105` `0xb88f70` `0x1c06ba` `0x1c0d68` `0x1c4a31` `0x1c4afe` `0x1c4bf9` `0x1c4d88` `0x1c4eaf` `0x1c4ecf` `0x1c5576` `0x1cb3d8` `0x1cba9e` `0x1cbb57` `0x1cbdea` `0x1cbe19` `0x1cc092` `0x1cc09d` `0x1cc1dd` `0x1cc9b4` `0x1cca61` `0x1ccbca` `0x1ccbd8` `0x1ccc58` `0x1ccfa4` `0x1cd137` `0x1cd3b5` `0x1cd4fd` `0x1cd5dd` `0x1cd906` `0x1cdbb2` `0x1cdcb0` `0x1cdd8b` `0x1cdecb` | orchestrator/dispatcher |
| `0x1c71ec` | 235 | - | - | `0x1cb3d8` `0x1cbf07` `0x1cc79e` `0x1ccbd8` `0x1ccd96` `0x1cd286` `0x1cdecb` | orchestrator/dispatcher |
| `0x1c7462` | 27 | 16 | `0x1c75d8` | - | unclassified/mixed |
| `0x1c74a1` | 19 | 32 | `0x1c75d8` | - | block copy / move |
| `0x1c74cd` | 118 | 16 | `0x1c75d8` | `0x1c778a` | DMA/descriptor construction |
| `0x1c75d8` | 159 | 32 | - | `0x1c2b24` `0x1c7462` `0x1c74a1` `0x1c74cd` | DMA/descriptor construction |
| `0x1c778a` | 18 | - | `0x1c74cd` | - | unclassified/mixed |
| `0x1c80f2` | 43 | - | `0x1c2b24` | - | block copy / move |
| `0x1cb3d8` | 98 | - | `0x1c207b` `0x1c642a` `0x1c71ec` | `0x1c06ba` | envelope or gain |
| `0x1cba9e` | 54 | - | `0x1c642a` `0x1cbb1e` | - | envelope or gain |
| `0x1cbb57` | 104 | - | `0x1c642a` | - | wavetable/indexed lookup (pointer table) |
| `0x1cbdea` | 23 | - | `0x1c642a` | `0x1cc6b8` | unclassified/mixed |
| `0x1cbe19` | 100 | - | `0x1c642a` | `0x1c06ba` `0x1c0d68` `0x1cc6c4` | envelope or gain |
| `0x1cbf07` | 133 | - | `0x1c71ec` | `0x1c06ba` | wavetable/indexed lookup (pointer table) |
| `0x1cc092` | 6 | - | `0x1c642a` | - | block copy / move |
| `0x1cc09d` | 6 | - | `0x1c642a` | - | block copy / move |
| `0x1cc0a8` | 33 | - | `0x1cc1dd` | - | block copy / move |
| `0x1cc1dd` | 18 | - | `0x1c642a` | `0x1cc0a8` | block copy / move |
| `0x1cc204` | 15 | - | `0x1cc419` `0x1cc44e` | - | IIR or recurrence |
| `0x1cc373` | 10 | - | `0xb80d91` `0x1c5aa7` | - | unclassified/mixed |
| `0x1cc3dc` | 30 | - | `0xb828f7` | - | IIR or recurrence |
| `0x1cc419` | 22 | - | `0xb82680` | `0x1cc204` | block copy / move |
| `0x1cc44e` | 51 | 3 | `0xb828f7` | `0x1cc204` | block copy / move |
| `0x1cc5da` | 20 | - | `0x1cc635` | - | IIR or recurrence |
| `0x1cc635` | 22 | - | `0xb825ef` | `0x1cc5da` | block copy / move |
| `0x1cc6b8` | 7 | - | `0x1cbd58` `0x1cbdea` `0x1cc6f2` | - | glue/trampoline |
| `0x1cc6c4` | 20 | - | `0x1cbe19` `0x1cc6f2` | `0x1c06ba` | block copy / move |
| `0x1cc79e` | 173 | - | `0x1c71ec` | `0x1c06ba` | interpolating table lookup / wavetable oscillator |
| `0x1cc9b4` | 73 | - | `0x1c642a` `0x1ccae8` | `0x1c06ba` | interpolating table lookup / wavetable oscillator |
| `0x1cca61` | 63 | - | `0x1c642a` `0x1ccae8` | - | unclassified/mixed |
| `0x1ccbca` | 8 | - | `0xb80d91` `0xb81ef1` `0x1c642a` `0x1ccca9` | - | block copy / move |
| `0x1ccbd8` | 59 | - | `0xb80fd0` `0xb820b1` `0x1c642a` `0x1c71ec` | - | IIR or recurrence |
| `0x1ccc58` | 33 | - | `0xb80e40` `0xb81f28` `0x1c642a` `0x1ccca9` | - | unclassified/mixed |
| `0x1ccd96` | 99 | 31 | `0x1c71ec` | - | IIR or recurrence |
| `0x1ccfa4` | 181 | - | `0x1c642a` `0x1cd18b` | `0x1c06ba` | envelope or gain |
| `0x1cd137` | 50 | - | `0x1c642a` `0x1cd18b` | - | block copy / move |
| `0x1cd286` | 148 | - | `0x1c71ec` | - | unclassified/mixed |
| `0x1cd3b5` | 135 | 4 | `0x1c642a` `0x1cd530` | `0x1c06ba` | envelope or gain |
| `0x1cd4fd` | 21 | 9 | `0x1c642a` `0x1cd530` | - | block copy / move |
| `0x1cd5dd` | 76 | - | `0x1c642a` `0x1cd814` | - | envelope or gain |
| `0x1cd68f` | 71 | - | `0x1cd72f` `0x1cd77c` `0x1cd906` | - | envelope or gain |
| `0x1cd906` | 71 | - | `0x1c642a` | `0x1c06ba` `0x1cd68f` | envelope or gain |
| `0x1cdbb2` | 115 | - | `0x1c642a` | - | unclassified/mixed |
| `0x1cdcb0` | 10 | - | `0x1c6154` `0x1c642a` | - | unclassified/mixed |
| `0x1cdd8b` | 55 | - | `0x1c642a` | - | block copy / move |
| `0x1cddff` | 91 | 14 | - | - | envelope or gain |
| `0x1cdecb` | 51 | - | `0x1c642a` `0x1c71ec` | - | block copy / move |
| `0x1cdf38` | 46 | 32 | - | - | unclassified/mixed |
| `0xb80000` | 99 | 31 | `0x1c4f81` | - | IIR or recurrence |
| `0xb80105` | 38 | - | `0x1c59c3` `0x1c642a` | - | DMA/descriptor construction |
| `0xb80314` | 15 | - | `0xb8063e` | `0x1c06ba` | unclassified/mixed |
| `0xb8033b` | 27 | - | `0xb8063e` | - | IIR or recurrence |
| `0xb8037f` | 73 | 8 | `0xb80fd0` | - | IIR or recurrence |
| `0xb80450` | 64 | - | `0xb80fd0` | - | unclassified/mixed |
| `0xb8055d` | 87 | - | `0xb8063e` | - | unclassified/mixed |
| `0xb8063e` | 602 | - | `0xb80fd0` | `0xb80314` `0xb8033b` `0xb8055d` | paired sum/difference (coefficient combine) |
| `0xb80cdf` | 72 | - | `0xb80fd0` | - | IIR or recurrence |
| `0xb80d91` | 78 | - | `0xb80fd0` | `0x1cc373` `0x1ccbca` | unclassified/mixed |
| `0xb80e40` | 174 | - | `0xb80fd0` `0xb8171e` | `0x1ccc58` | block copy / move |
| `0xb80fd0` | 56 | - | - | `0xb8037f` `0xb80450` `0xb8063e` `0xb80cdf` `0xb80d91` `0xb80e40` `0x1ccbd8` | orchestrator/dispatcher |
| `0xb817fc` | 63 | 2 | `0xb820b1` | - | IIR or recurrence |
| `0xb81896` | 123 | - | `0xb820b1` | - | wavetable/indexed lookup (pointer table) |
| `0xb819bd` | 212 | - | `0xb820b1` | - | wavetable/indexed lookup (pointer table) |
| `0xb81baf` | 89 | - | `0xb820b1` | - | block copy / move |
| `0xb81ef1` | 24 | - | `0xb820b1` | `0x1ccbca` | unclassified/mixed |
| `0xb81f28` | 169 | - | `0xb820b1` `0xb821ef` | `0xb88e04` `0x1c06ba` `0x1ccc58` | envelope or gain |
| `0xb820b1` | 133 | - | - | `0xb817fc` `0xb81896` `0xb819bd` `0xb81baf` `0xb81ef1` `0xb81f28` `0x1ccbd8` | orchestrator/dispatcher |
| `0xb822aa` | 10 | - | `0xb8235f` | - | unclassified/mixed |
| `0xb822c0` | 65 | 2 | `0xb82680` | - | envelope or gain |
| `0xb8235f` | 105 | - | `0xb8245d` | `0xb822aa` | envelope or gain |
| `0xb8245d` | 104 | - | `0xb82680` | `0xb8235f` | envelope or gain |
| `0xb82543` | 73 | - | `0xb82680` `0xb82875` | - | IIR or recurrence |
| `0xb825ef` | 60 | - | `0xb82680` | `0x1cc635` | unclassified/mixed |
| `0xb82680` | 63 | - | `0x1c18a6` | `0xb822c0` `0xb8245d` `0xb82543` `0xb825ef` `0x1cc419` | orchestrator/dispatcher |
| `0xb828f7` | 66 | - | `0xb82cba` | `0x1cc3dc` `0x1cc44e` | envelope or gain |
| `0xb82994` | 68 | - | `0xb82cba` | `0x1c0d68` | envelope or gain |
| `0xb82a35` | 86 | - | `0xb82cba` | - | envelope or gain |
| `0xb82b09` | 99 | 2 | `0xb82cba` | `0x1c0d68` | envelope or gain |
| `0xb82bf6` | 78 | 2 | `0xb82cba` | - | envelope or gain |
| `0xb82cba` | 59 | - | `0xb82d41` | `0xb828f7` `0xb82994` `0xb82a35` `0xb82b09` `0xb82bf6` | orchestrator/dispatcher |
| `0xb82d41` | 37 | - | `0x1c207b` | `0xb82cba` | block copy / move |
| `0xb82e4f` | 17 | - | `0x1c2b24` | `0x1c0d68` | unclassified/mixed |
| `0xb88e04` | 99 | - | `0xb81f28` `0xb83641` `0xb88ef4` `0x1c42b8` `0x1c44b0` `0x1c45aa` `0x1c8c75` `0x1c8d2f` | - | envelope or gain |
| `0xb88f70` | 27 | - | `0x1c4914` `0x1c5615` `0x1c642a` | - | unclassified/mixed |

123 fonctions, 12278 instructions statiques.
