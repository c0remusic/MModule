# 02 — Inventaire des tables que lisent les moteurs du Machinedrum (OS 1.63)

Ticket : [02-harnais-md-et-tables](../issues/02-harnais-md-et-tables.md), partie « inventaire ». Recherche du
2026-10-26. Elle prolonge les recherches 17 (port) et 18 (sans firmware) de l'effort `elektron-drums`.

Question : pour un port C++ qui tourne **sans aucun firmware chez l'utilisateur**, quelles tables lisent le programme
voix (DSP2, section 1), le programme mixeur et master (DSP1, section 2) et les routines ColdFire que mdEngine appelle
(section 0) ? Pour chacune : où elle est, sa taille, qui la lit, et si une formule la régénère au mot près.

Marques : **formule trouvée** (table générée par une formule et comparée mot à mot, 0 différence, la comparaison est
dite), **à chercher** (allure régulière, formule plausible, pas encore exacte), **bloquante** (échantillons ou données
irrégulières que seul le firmware ou l'utilisateur peut fournir). Les suppositions sont marquées [INFER]. Aucune
table n'est recopiée ici : seulement adresses, tailles, quelques valeurs et les formules.

## Méthode et outils

Outils écrits pour ce ticket, hors dépôt, dans `C:\dev\elektron-work\scratch-arch\research\md02\` (leurs sorties
restent là) :

- `dump.cpp` (construit par `build.bat` contre les bibliothèques de `C:\dev\MModule\temp\cmake_vs22`) : lit l'image
  flash `elektron_sps1-1uw_os1.63.bin`, écrit les enregistrements de chargement des deux programmes DSP
  (`dspA_records.*`, `dspB_records.*`), la section 0 décompressée (`os.bin`), puis construit le `VoiceEngine` de
  mdEngine (programme voix chargé, son init exécutée) et écrit sa mémoire externe $100000-$1FFFFF après l'init
  (`dspA_ext_after_init.bin`). Lancé sous garde de temps (Start-Process, WaitForExit 120 s).
- `disb.cpp` (`buildb.bat`) : désassemble le programme DSP1 depuis ses enregistrements (`dspB_all.txt`,
  `dspB_boot.txt`). Le programme DSP2 vient de `md17\mdinv.exe dis` (`dspA_low.txt`, `dspA_init.txt`) et de
  `md17\dump.txt`.
- `refs.py` : chaque adresse absolue du code des machines DSP2 (`md17\dump.txt`), rattachée à son enregistrement et
  aux machines qui la lisent. `initdiff.py` : ce que l'init du DSP2 écrit au-delà de ses enregistrements.
- `osrefs.py` : les adresses de données lues par les routines ColdFire de `machines.txt` (55 fonctions de
  coefficients), le lissage, les quatre routines LFO, la pente des niveaux et les quatre corps du master, avec leurs
  appelées (`cfdis.routine`). Les corps du master sont lus dans leurs bornes de `HostModel` (leur parcours déborde dans
  le tick, comme le notait la recherche 17).
- `look.py`, `ostab.py` (allure d'une table), `sine.py`, `cand.py`, `cand2.py`, `fitexp.py`, `expand.py`, `codes.py`
  (formules candidates comparées mot à mot).

Limites : une table n'entre dans l'inventaire que si le code atteint la nomme (adresse immédiate, base d'un adressage
indexé) ; sa taille vient de son enregistrement de chargement, ou de la base suivante quand plusieurs tables partagent
un enregistrement [INFER quand c'est le cas]. Le code de RAM-R (qui ne joue pas dans MoDule) est inventorié mais pas
détaillé.

## Réponse courte

- **79 tables** en tout : **14 formule trouvée**, **52 à chercher**, **13 bloquantes**. S'y ajoutent des zones
  qu'aucun code atteint ne nomme (5, listées à part) et des données qui ne sont pas des tables (état initial, code,
  faits déjà écrits dans le dépôt).
- **Formules trouvées** : les deux sinus de 32 768 mots (DSP2 et DSP1, la récurrence du boot, 0 différence contre la
  mémoire du DSP2 après son init), les quatre rampes de 32 mots (X:$203/$223 du DSP2, X:$648/$668 du DSP1), la
  constante X:$256, les six ondes carrées des hi-hats et cymbale TRX, et la table de hauteur de l'OS (`$24BD94`,
  `round(2048 · 2^(i/127,5))`, 128/128).
- **Bloquantes** : les échantillons. Formes d'onde échantillonnées de TRX-BD, TRX-SD, TRX-XT, TRX-RS et TRX-XC
  (~5 500 mots en tout), excitations des P-I (BD, SD, MT, ML, RS, ~7 550 mots), les 21 échantillons des E12
  (~198 600 mots) et la banque UW des ROM (flash). Sans elles, TRX-BD/SD/XT/RS/XC, cinq P-I, les 16 E12 et les ROM ne
  sonnent pas comme le Machinedrum.
- **Surprises** :
  - Le programme voix d'OS 1.63 ne charge **aucune table tanh** en X:$280-$680 : sa mémoire X interne ne reçoit que
    X:$202-$256 (73 mots). La table tanh de `machinedrum-kit/08-dsp2-voice-abi.md` appartient aux builds modifiés
    [INFER].
  - Le sinus du DSP2 est **le même calcul** que celui du DSP1 (même code de boot à P:$100069, mêmes constantes) : la
    récurrence de `TrackFx::Tables` le régénère sans différence. `round(sin · 2^23)` en diffère sur 31 464 mots.
  - Les tables de filtre du DSP1 sont lues avec des **indices négatifs** : la base nommée par le code n'est pas le
    début de la table (`$1402AA` commence en `$140000`, `$14221D` en `$142000`). Leurs bornes se déduisent des
    décalages minimaux des tables de résonance (−682, −541…). Entre les deux tables du filtre, 2 730 mots
    (`$140AAA-$141553`), de la taille exacte de la première, ne sont lus par aucun code [INFER : une variante non
    utilisée].
  - La table 12 → 24 bits des ROM (X:$146000) est impaire autour du code 2048, linéaire (512 par code) jusqu'à
    |k| ≈ 440, puis exponentielle : une loi de compression.
  - La table de la flash 16 → 12 bits (`$7A0000`) sert aussi aux Samples de l'utilisateur (`makeSample`) : elle n'est
    pas l'inverse « au plus proche » de la table d'expansion (65 381 codes différents sur 65 536 ; près de zéro, un code
    vaut ~1,5 unités 16 bits et non 2). Sans firmware, il faut soit la retrouver, soit jouer les Samples en 16 bits
    directement, hors bit-exact [INFER].

## 1. DSP2 : programme voix (section 1)

Mémoire : X, Y et P se partagent la mémoire externe à partir de $020000 (une adresse $14xxxx est la même en X, Y et
P) ; la mémoire interne X:$000-$7FF est à part. Les noms de machines viennent du rattachement d'une adresse à
l'entrée la plus proche (`refs.py`) ; les renders partagés sont nommés par leurs machines (E12 : `$103838` sert 9
machines, `$103B46` 5, `$103A10` E12-RS, `$103945` E12-BD).

### 1.1 Construite par l'init

| Table | Adresse, taille | Lue par | Marque |
|---|---|---|---|
| Sinus, une période | X/Y:$148000-$14FFFF, 32 768 mots (le cosinus est la même table à +$2000) | 46 références : GND-SN, TRX (BD, SD, XT, RS, CB, CL, XC, B2, S2), les 8 EFM ; P-I-CC, HH, RC par $14A000 | **formule trouvée** : récurrence s[n+1] = 2c·s[n] − s[n−1] en double précision 48 bits, c = cos(2π/32768) ($7FFFFF:$D88586), départ 0, $000648:$7ED46B, code du boot P:$100069-$10008B. Portée en Python (`sine.py`) depuis `TrackFx::Tables` : **0 mot différent sur 32 768** contre la mémoire du DSP2 après son init |

### 1.2 Mémoire X interne chargée par le flux

| Table | Adresse, taille | Lue par | Marque |
|---|---|---|---|
| Rampe montante | X:$203, 32 mots | INP-EA/EB, INP-FA/FB (P:$100259, $100389) | **formule trouvée** : `$2000 + $4000·i`, 32/32 |
| Rampe descendante | X:$223, 32 mots | les mêmes | **formule trouvée** : 0, puis `$82000 − $4000·i` (i ≥ 1), 32/32 |
| Constante | X:$256, 1 mot | INP-EA/EB, RAM-R | **formule trouvée** : $000100 |
| Filtre FIR symétrique | X:$244-$24A, 7 mots (plus un centre $27ECCB en immédiat) | render ROM (P:$33A-$364, 16 itérations de 15 produits) : interpolation des ROM [INFER] | à chercher (sinc fenêtré probable, fenêtre inconnue) |

Pas d'autre mot en X:$257-$7FF : aucune table tanh n'est chargée par OS 1.63.

### 1.3 Mémoire externe : tables régulières

| Table | Adresse, taille | Lue par | Allure | Marque |
|---|---|---|---|---|
| Décalages de résonance A | X:$140000, 128 mots | filtre résonant de TRX-CB, CH, CL, CP, CY, MA, OH, RS, S2 et INP-EA/FA, indexé par le paramètre (>>7) | −482 à −3, croissante, en 1/x [INFER] | à chercher |
| Décalages de résonance B | X:$140080, 128 mots | les mêmes | −6 à −577, décroissante | à chercher |
| Coefficients de coupure 1 | X:$140100-$1410FF, 4 096 mots, lue en `$140900 ± 2048` | les mêmes plus TRX-SD et E12 (renders $103838, $103A10, $103B46) | ≈ 1/√2 ($5A827A) plat puis chute vers $E704FF : forme en cosinus [INFER] | à chercher |
| Coefficients de coupure 2 | X:$141100-$1420FF, 4 096 mots, lue en `$141900 ± 2048` | les mêmes | $5A8269 décroissant vers $0E39B7 | à chercher |
| Décalages E12 A et B | P:$103C7B et $103CFB, 128 mots chacune | E12 (renders $103838, $103A10, $103B46) | même rôle et même premier mot (−482) que $140000/$140080, autre courbe | à chercher (2 tables) |
| Interpolation E12 | P:$147200, 1 024 mots = 256 phases × 4 prises ; E12-BD lit en $147200, les autres en $147400 | les 16 E12 | **B-spline cubique** : prises (1−t)³/6, (3t³−6t²+4)/6, (−3t³+3t²+3t+1)/6, t³/6, t = ((p+128) mod 256)/256 | à chercher : 886/1 024 exacts avec un arrondi au plus proche (138 mots à 1 LSB) ; troncature, plafond et calcul en float32 essayés, moins bons |
| Expansion 12 → 24 bits | X:$146000-$146FFF, 4 096 mots | render ROM (P:$3BC, $3CF : `insert` du code dans l'adresse), lecture d'échantillons par l'hôte (P:$113), RAM-R | impaire autour de 2048 (t[2048+k] = −t[2048−k]), 512 par code jusqu'à |k| ≈ 440, puis pas croissant d'un facteur ≈ 1,00225 par code | à chercher (`expand.py`) |
| Amortissements P-I | P:$142949 et $142B49, 512 mots chacune | $142949 : P-I-BD, SD, MT, ML, RS ; $142B49 : P-I-BD, SD, MT, RS, CC, HH, RC | croissantes vers 1 ($5D92F7 → $7FFEAE ; 0 → $7F5739) | à chercher (2 tables) |
| Coefficients P-I-CC/HH/RC | P:$145224 et $145424, 512 mots chacune = 128 réglages × 4 mots | P-I-CC, HH, RC | lisses, 4 mots par réglage [INFER : coefficients d'un biquad] | à chercher (2 tables) |
| Petites tables P-I | P-I-BD $142279 (24 mots), P-I-SD $142F33 (24), P-I-MT $143661 (48), P-I-ML $143F38 (24), P-I-RS $144C9A (24) | chacune sa machine | 3 ou 6 lignes de 8 (rapports de modes, gains [INFER]) | à chercher (5 tables). Question pour Antoine : ces 24-48 constantes au milieu du code comptent-elles comme les immédiats du code, que le port écrit de toute façon, ou comme des tables ? |
| Ondes carrées TRX | P:$102600 (144 mots), $102700 (102), $102780 (86), $102800 (80), $102880 (58), $1028C0 (52) | TRX-CH, OH, CY | six oscillateurs carrés | **formule trouvée** (6 tables) : première moitié $7FFCB9, seconde $800347 (−$7FFCB9), période = taille ; 0 différence sur chacune (`cand.py`) |

### 1.4 Mémoire externe : échantillons

| Table | Adresse, taille | Lue par | Marque |
|---|---|---|---|
| Ondes TRX-BD | P:$10050C et $10062C, 288 mots chacune | TRX-BD | **bloquante** (2 tables) : mots 16 bits << 8, une période déformée (pic à 1/8 de période, creux à 5/8) |
| Bruit TRX-SD | P:$100881, 4 097 mots | TRX-SD | **bloquante** : 16 bits << 8, irrégulier, décroissant vers la fin |
| Clic TRX-XT | P:$10190F, 64 mots (deux de 32, $10190F et $10192F) | TRX-XT | **bloquante** |
| Rimshot TRX-RS | P:$101BA0, 768 mots | TRX-RS | **bloquante** : 24 bits pleins, irrégulier |
| Clic TRX-XC | P:$102C0F, 34 mots | TRX-XC | **bloquante** |
| Excitations P-I | P:$142291 (1 480 mots, BD), $142F4B (1 131, SD), $143691 (1 676, MT), $143F50 (2 681, ML), $144CB2 (586, RS) | une machine chacune | **bloquante** (5 tables) : irrégulières, amorties, chacune avec un mot à exactement 0,5 [INFER : impulsions ou excitations échantillonnées] |
| Échantillons E12 | P:$103DBA-$13516D, ~198 600 mots, 21 échantillons ; répertoire P:$103D7B, 63 mots = 21 × (début, longueur, 0) | les 16 E12 | **bloquante** ; le répertoire se refait à partir des échantillons |
| Banque ROM | répertoire Y:$147E00 (264 mots), échantillons en P:$150000, deux codes 12 bits par mot | ROM-01 à -48, RAM-P | **bloquante** : MoDule les construit déjà depuis la banque UW de la flash (`fw::loadRomBankFromFlash`) et l'utilisateur peut remplacer chacun des 48 par ses Samples ; sans flash, ROM-01 à -32 se taisent |

### 1.5 Ce qui n'est pas une table

- Tables de dispatch P:$145AF5 (579 mots = 3 × 193 adresses de code) : disparaissent avec le port.
- Y:$140 (35 mots, deux pointeurs $120 et $800), X:$202 et X:$243 (pointeur d'anneau $700) : état initial.
- Blocs nuls : 240 mots après chaque P-I (P:$142859, $1433B6, $143D1D, $1449C9, $144EFC), 21 blocs de 153 mots
  entre les échantillons E12, mémoire P-I $135600 (effacée par l'init, $6000 mots), tampons RAM-R $135206-$135406.

## 2. DSP1 : chaîne de piste, mixeur et master (section 2)

`TrackFx` et `Mixer` (C++) lisent ces tables dans `TrackFx::Tables` ; `MasterEngine` exécute le master émulé sur
la même image. Les seules adresses externes que nomme le code de la section 2 (`dspB_all.txt`, mots de code entre
$140000 et $150000) sont celles du tableau.

| Table | Adresse, taille | Lue par | Allure | Marque |
|---|---|---|---|---|
| Sinus | $148000-$14FFFF, 32 768 mots ; cosinus à $14A000 | AMD (`TrackFx::amd`), panoramique (`Mixer::gains`), master (pointeur dans $14398D) | | **formule trouvée** : déjà calculée par la récurrence du boot dans `TrackFx::Tables` (`TrackFx.cpp:34-55`), bit-exacte avec le DSP (`mdEngineFirmwareTest`) ; même code que le DSP2 (§1.1, 0 différence) |
| Rampes du filtre | X:$648 et X:$668, 32 mots chacune | rampes des coefficients du filtre (`filter1`, `filter2`) | | **formule trouvée** (2 tables) : identiques à X:$203 et X:$223 du DSP2, 32/32 chacune |
| Décalages de résonance | $141F00 et $141F80, 128 mots chacune, indexées par FLTQ | `filter1`, `filter2` | −682 à −4 ; 0 à −428 | à chercher (2 tables) |
| Coupure A | base $1402AA, lue de $140000 à $140AA9 (2 730 mots : FLTF<<4 + décalage, décalage ≥ −682) | `filter1`, `filter2` | $7FFFFF puis chute vers $DA43A2 | à chercher |
| Coupure B | base $141700, lue de $141554 à $141EFF (2 476 mots) | `filter1`, `filter2` | $7FE8AA décroissant vers $0E32C5 | à chercher |
| Décalages EQ | $143476, $1434F6, $143576, $1435F6, 128 mots chacune, indexées par EQG | `TrackFx::eq` | petites valeurs négatives, rupture à i = 64 (gain positif ou négatif [INFER]) | à chercher (4 tables) |
| EQ A | base $14221D, lue de $142000 à $142A1C (2 589 mots) | `TrackFx::eq` | proche de 1, chute vers $B73776 | à chercher |
| EQ B | base $142C76, lue de $142A1D à $143475 (2 649 mots) | `TrackFx::eq` | $7FA2C3 décroissant vers $0CE197 | à chercher |
| Gain DIST | $143777, 257 mots, indexée par DIST × 2 | `TrackFx::dist` | $020000 → $7FFFFF, croissante | à chercher (une exponentielle simple, `2^17 · 2^(6i/256)`, diffère sur 255 mots) |
| Filtre DIST | $143878, 257 mots | `TrackFx::dist` | $7FFFFF décroissant jusqu'à $080000 (i = 176), puis constant | à chercher (`max(2^19, 2^23 · 2^(−i/k))` essayée pour quatre k : 173 différences au mieux) |
| Tables du master | $1439C0 et $143B40, 384 mots chacune = 128 × 3 mots, indexées par Y:$187 et Y:$188 | master (P:$440-$448) | lisses, croissante et décroissante | à chercher (2 tables) |

Ce qui n'est pas une table : X:$640 (2 mots, pointeurs $4C0 et $100), Y:$1C2 (8 mots, pointeurs), Y:$100 (80 zéros),
$14398D-$1439BF (51 mots : état initial et pointeurs du master, dont $148000), et P:$143DC0, $143DE4, $143E4A, qui
sont du **code** (les inits de l'Echo, de la Dynamix et de la Gate Box, appelées par le boot P:$10005F-$100063).

## 3. Section 0 : routines ColdFire

Les routines lues sont celles que mdEngine appelle (recherche 17, §2). Les tables sont des mots de 32 bits, gros
boutiste. « Lue par » donne le nom de la première machine de chaque fonction de coefficients (`machines.txt`), ROM01
valant pour ROM-01 à -24 et ROM25 pour ROM-25 à -48.

| Table | Adresse, taille | Lue par | Allure | Marque |
|---|---|---|---|---|
| Hauteur | $24BD94, 128 mots (une octave) | 34 fonctions (GND-SN, TRX-BD, XT, XC, B2, les EFM, les E12, les P-I sauf MA, ROM01, ROM25, RAM-R1) et la Gate Box | 2 048 → 4 085, une octave en 128 pas | **formule trouvée** : `round(2048 · 2^(i/127,5))`, 128/128 (`fitexp.py`) |
| Décroissance | $24BF94, 512 mots [INFER : jusqu'à $24C794 ; TRX-BD lit jusqu'à l'indice 437, TRX-S2 aussi par $24C048 et $24C19C] | 41 fonctions (GND-NS, SN, TRX, EFM, E12, INP-EA/EB/GA/GB, P-I-CC/HH/MA/RC, ROM) ; `HostModel` pour les User Machines (`md::mds::DecayTableAddress`) | $5F29C2 → $7FFFF1, vers 1 | à chercher : 2^23 − t n'est pas une exponentielle (pas en log2 de 0,029 à 0,074) |
| Courbe 0-65 535 | $24BB90, 129 mots | E12-RS, P-I-CC, Gate Box | 0 → 65 535, convexe | à chercher |
| P-I et TRX | $24C794, 512 mots [INFER], P-I-ML lit aussi en $24CC44 | P-I-CC, HH, ML, RC, RS, SD, TRX-B2, CB, SD | 110 zéros puis montée vers $7FFE16 | à chercher |
| INP | $24CF94, 512 mots | INP-EA/EB/FA/FB | $7FFFFF (saturé) puis exponentielle décroissante (pas ≈ 0,0339 en log2) jusqu'à 71 | à chercher (mi-plage régulière, arrondi et bornes à trouver) |
| ROM et TRX-B2 | $24D794, ~128 mots [INFER : suivis d'octets $04040404…] | ROM01, ROM25, TRX-B2 | 0 → $26C09, concave | à chercher |
| Master et P-I | $24DA14, 256 mots | Echo, Gate Box, Dynamix, P-I-BD, MT, SD | $7F874B décroissant vers 1 000 000 | à chercher |
| Dynamix | $24DE14, $24E014, $24E214, 128 mots chacune | Dynamix | $24DE14 : 65 536 → $7FFFFF ; les deux autres décroissantes | à chercher (3 tables) : $24DE14 tient à 105/128 avec `round(65536 · 2^(7i/127))`, à 1 LSB ailleurs |
| EQ du master | $24E414, $24E614, $24E814, $24EA14, $24EC14, 128 mots chacune ; $24EE14, 66 mots [INFER : d'autres données suivent] | EQ | croissantes sauf $24EC14 (1/x [INFER]) | à chercher (6 tables) |
| P-I-MA | $2462E8, $2464E8, $2466E8, $2468E8, 128 mots chacune | P-I-MA | lisses, monotones | à chercher (4 tables) |

Ce qui n'est pas une table :

- $2523EE : 8 adresses de code (les formes du LFO), qui disparaissent avec le port (le LFO est déjà porté,
  `mdDrums/mdDrumsLfo.*`).
- Les adresses $1000xxx (image SRAM copiée de $2622F4, 64 Ko) : variables d'état (cibles et valeurs lissées, structs
  LFO, mots du master, tempo $100150C). Le port les initialise lui-même : **remplaçable**.
- Descripteurs des machines ($24EF54, 135 × 86 octets : fonction, noms, valeurs par défaut) et table des ID
  ($252092) : **remplaçables**, des faits (`mdProtocol/mdmachines.*` a déjà les noms).
- Tables d'ASSIGN MACHINE ($246148-$2462DB) : déjà écrites comme constantes dans `mdProtocol/mdkitchange.h`
  (`os163`).

## 4. Flash hors OS

| Table | Adresse, taille | Lue par | Marque |
|---|---|---|---|
| Banque UW (ROM-01 à -32) | flash $100000, en-têtes de 12 mots et échantillons 16 bits | `fw::loadRomBankFromFlash` | comptée au §1.4 (banque ROM, **bloquante**) |
| Conversion 16 → 12 bits | flash $7A0000, 65 536 mots de 16 bits | `loadRomBankFromFlash` (banque d'usine) et `makeSample` (Samples de l'utilisateur) | à chercher : le code le plus proche dans la table d'expansion X:$146000, quel que soit le départage des égalités, diffère sur 65 381 entrées (`codes.py`) ; la loi a un gain : près de zéro, un code vaut ~1,5 unité 16 bits |
| Kits d'usine | image de mémoire patch | `loadPatchImageFromFlash` | **remplaçable** : nos propres Kits |

## 5. Zones qu'aucun code atteint ne nomme

À vérifier avant de les déclarer inutiles (un pointeur calculé à l'exécution peut y mener) :

- DSP2 Y:$163-$782 (1 568 mots) : lisse, presque symétrique autour de son milieu [INFER : un noyau de filtre].
- DSP2 X:$147000-$1471FF (512 mots, la fin de l'enregistrement X:$146000) : croissante de −1 vers 0.
- DSP1 $140AAA-$141553 (2 730 mots) entre les deux tables de coupure.
- DSP1 $143676 (128 mots : constante $0B504F puis rampe de pas ~118 633) et $1436F6 (129 mots).

## 6. Décompte

| Marque | DSP2 | DSP1 | Section 0 | Flash | Total |
|---|---|---|---|---|---|
| formule trouvée | 10 (sinus, 2 rampes, constante, 6 carrés) | 3 (sinus, 2 rampes) | 1 (hauteur) | 0 | **14** |
| à chercher | 18 | 14 | 19 | 1 | **52** |
| bloquante | 12 (TRX 6, P-I 5, E12 1) | 0 | 0 | 1 (banque UW et ROM) | **13** |

Pistes pour la suite, des moins chères aux plus chères [INFER] : les formes connues d'abord (B-spline E12, à 1 LSB
près ; expansion 12 bits et conversion 16 → 12, liées ; exponentielles de la section 0, dont $24DE14 à 1 LSB près),
puis les familles de filtre (DSP2 $140000-$1420FF, DSP1 $140000-$143675), qui partagent une structure (décalage par
résonance + table de coupure lue en ±2 048) et devraient tomber ensemble. Les échantillons ne se calculent pas : il
faut les demander à l'utilisateur, les remplacer, ou laisser ces machines hors de l'instrument.
