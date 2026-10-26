# 03 — Oracle du Digitakt II : le master du firmware tourne

Ticket : [03-harnais-oracle-dt2](../issues/03-harnais-oracle-dt2.md). Recherche du 2026-10-26. Elle prolonge les
recherches 12 et 23 de l'effort `elektron-drums` (cartographie du DSP, arbre d'appels du master).

Marques : **[V]** lu dans le code désassemblé ou mesuré ici ; **[INFER]** déduit ; **[O]** inconnu. Adresses de code en
short-word, adresses de données en octets (L1 bloc 0, `0x240000`-`0x26FFFF`), comme dans les listings de digikit.

## Ce qui tourne

Le cœur SHARC+ concret en Python de digikit (`tools.sharc_dev.sharc_run.Runner`, copie
`C:\dev\elektron-work\scratch-arch\ext\digikit`, commit `9dee5d5`) exécute, sur l'image DSP de l'OS 1.16 (section 7) :

1. une fois, l'init du graphe audio `0x1c15e3` (appelée par `0x1c7ff9` au démarrage) : 1 244 258 instructions, environ
   70 s. Elle écrit le contexte audio (`0x252d2c` = 48 000.0, `0x252d30` et `0x252d3c` = 32, la taille de bloc) et l'état
   initial des effets (saturateur en `0x252c58`, etc.). L'état qui en sort est gardé dans un instantané
   (`init-0x1c15e3.snap`). À côté, `init-0x1c15e3.snap.json` note ce qui l'a produit : le sha256 de la section DSP que
   l'image charge (`section_7_BLOB.bin`), le commit de digikit (lu dans son `.git`), l'adresse de l'init, les registres
   de contexte et la pile du harnais, `approx_recips`. Le harnais ne reprend l'instantané que si cette fiche est
   identique à celle qu'il calcule. Sinon (instantané ou fiche absents, ou un champ différent) il refait l'init et écrit
   la raison (`rebuilding the init snapshot (...)`). L'instantané refait ainsi le 2026-10-26 redonne au bit près les neuf
   vecteurs déjà capturés (sha256 des `.f32` identiques avant et après) ;
2. puis, bloc après bloc, le master `0x1c207b`, rien d'autre : pas de ColdFire, pas d'orchestrateur `0x1c2b24`, pas de
   voix. Chaque appel repart de l'état laissé par le précédent (`Runner.fresh_call`), donc les enveloppes du
   compresseur, les rampes du saturateur et du volume courent d'un bloc à l'autre comme dans la machine.

Aucun outil nouveau : l'environnement `uv` déjà présent de digikit, lancé par son `python.exe` (un `uv run` voudrait
télécharger numpy pour le groupe dev ; le `.venv` suffit). Le cœur n'est pas couplé à la ColdFire et n'en a pas besoin
pour cette routine **[V]** : elle ne lit que de la mémoire DSP.

Une seule approximation du cœur : `approx_recips=True`. La graine ROM de `RECIPS` n'est pas documentée ; digikit la
remplace par un modèle numérique. La division `0x1c06ba` (saturateur) et le compresseur en dépendent. Écart possible
d'un ulp avec le silicium (recherche 12, §2).

## Comment le master est piloté **[V]** (code lu dans `0x1c207b`, `0xb82d41` → `0xb82cba`, `0x1cb3d8`)

Bloc : 32 échantillons, stéréo **planaire** : un tampon stéréo fait 64 mots flottants, L[0..31] puis R[0..31]. Les boucles
SIMD (`MODE1` bit `0x200000`) traitent deux échantillons voisins à la fois, sans entrelacement.

Entrée : les 16 tampons de piste en `0x252df8 + 0x100·t` (sortie de la chaîne de piste `0x1c642a`). Le harnais écrit le
signal dans la piste 1 et laisse les 15 autres à zéro. Le master :

1. vide les deux bus `0x254878` (vers le compresseur) et `0x254a78` (à côté du compresseur) ;
2. pour chaque piste dont le bit de `0x252728` est à 0, l'ajoute au premier bus si son bit de `0x252538` est à 1, sinon
   au second ; ajoute ensuite les retours d'effets (`0x254b78`, `0x254c78`, `0x254778`, selon les bits 16-17 de
   `0x252728` et 0-2 de `0x25253c`) : zéro ici, les effets ne tournent pas ;
3. appelle le compresseur `0xb82d41` (R4 = état `0x252ae0`, R8 = réglages `0x252518`, R12 = contexte `0x252d3c`). Ses
   pointeurs viennent de la table statique `0x256818` de l'image : sidechain, entrée `0x254878`/`0x2548f8`, sortie
   `0x254978`/`0x2549f8` ;
4. somme sortie du compresseur + second bus, multiplie par `0x404a6320` (3,1623, soit +10 dB) et écrête à ±1,0 ;
5. le saturateur `0x1cb3d8` sur L puis R (états `0x252c58`, `0x252cb8`, n = 32 sur la pile), écrit dans le tampon de
   sortie (R4 de l'appel) ;
6. la rampe de volume : gain = `volume²`, interpolé sur le bloc depuis la valeur précédente (`0x2558c0`/`0x2558c4`).

Le harnais fournit R4 = un tampon de sortie en L2 (`0x20098000`) et une pile en `0x2009c000`, comme les bancs de digikit
(`tests/support/dsp_voice_rig.py`) : `[I6]` = cadre de l'appelant, `[I6-1]` = adresse de retour, point d'arrêt au retour.

### Réglages, tels que le firmware les range

Ce sont des flottants que l'orchestrateur `0x1c2b24` écrit à chaque bloc depuis les mots s16 de la trame (lus en
`0x255fxx`-`0x2560xx`, multipliés par `0x38010204` = 1/32 512, soit valeur de bouton / 127). Le harnais les écrit
directement avant chaque bloc.

| Adresse | Réglage | Représentation et usage | Nom |
|---|---|---|---|
| `0x252518` | filtre de sidechain | ×127 → table `0x2ca038`, coefficients de deux filtres (`0x1cc3dc`, `0x1cc44e`) | [INFER] |
| `0x25251c` | seuil | borné 0..1, `60·p − 60` dB (`0xb82994`) ; trame : moitié haute de `0x255ffa` | [V] calcul |
| `0x252520` | ratio | `k/7` (trame : octet de `0x256004` × 1/7), `trunc(clamp(7p, 0, 7))` → table `0x2c9a18` : 0,667 0,5 0,333 0,25 0,167 0,125 0,0625 0,05 | [V] table et entrée lue, [INFER] = 1/ratio, de 1,5:1 à 20:1 |
| `0x252524` | attaque | ×127 → table `0x2c9a38` (0,812 à l'index 0), lissage à un pôle, état `+0x5b` ; trame : moitié basse de `0x255ffe` | [INFER] nom |
| `0x252528` | relâche | ×127 → table `0x2c9c38` (0,9979 à l'index 0), maintien du pic puis retour lissé, état `+0x5c` ; trame : moitié haute de `0x255ffe` | [INFER] nom |
| `0x25252c` | makeup | `0xb82b09`, avec −20·log10(16) = −24,08 dB ; trame : s16 en `0x256002` | [INFER] |
| `0x252530` | mix | ×127 → table `0x2c9e38` lue à k et 127 − k (fondu sec/traité), lissé d'un bloc à l'autre ; trame : moitié basse de `0x25600a` | [INFER] nom |
| `0x252534` | source de sidechain (entier) | 0 : l'entrée elle-même (table `0x256818`) ; 1 : le second bus ; 2-17 : une piste ; 18+ : un retour d'effet ; trame : octet haut de `0x256004` | [V] branches |
| `0x252538` | pistes vers le compresseur (masque) | bit t | [V] |
| `0x2526e8` | drive de l'overdrive | `d` : gain du saturateur `0,8465·d² + 0,1534`, mélange `min(8d, 1)` ; trame : s16 en `0x255ff6`, forcé à 0 par un bit de drapeau | [V] calcul, [INFER] échelle de trame |
| `0x2526ec` | volume du master | élevé au carré, rampe sur le bloc | [V] calcul, [O] échelle de trame |

**Ratio vérifié** : au premier bloc de chaque cas, le harnais pose un point d'observation (lecture seule, sans arrêt)
sur la table `0x2c9a18` et note le mot que lit `0xb82994` (en `0xb829e4`). Avec `comp_ratio` = 6/7, la routine lit
`0x2c9a30`, l'entrée 6, qui vaut 0,0625 : 16:1. Le JSON de chaque cas le donne (`ratio_table_reads`). La valeur par
défaut 1/7 tombe par le calcul sur l'entrée 1, 0,5 (2:1) ; les cas de drive, non rejoués, n'ont pas ce champ.

Valeurs par défaut du harnais : seuil 1,0 (0 dB), ratio 1/7, attaque 0, relâche 0,5, makeup 0, mix 1,0, filtre de
sidechain 0, drive 0, volume 1,0, source 0, toutes les pistes vers le compresseur.

**Réduction de gain** : le mot `0x252ae0 + 4·0x5b` est la réduction de gain en dB après l'étage d'attaque **[INFER]**,
confirmé par la mesure : le gain mesuré du bloc vaut `10 dB + ce mot` à 0,01 dB près sur les Hits.

## Commande de régénération

```powershell
pwsh -NoProfile -File C:\dev\elektron-work\tools\dt2\regen.ps1
```

Elle fait l'init si l'instantané manque (environ 70 s, un processus), puis les dix cas en trois processus, chacun sous
garde de temps (`guard.ps1` : `Start-Process` + `WaitForExit`, kill au délai). Environ 13 min de mur et trois cœurs.
`rerun-review.ps1` ne rejoue que les sept cas que la revue du 2026-10-26 a changés (seuils et cas combiné). Un cas
seul :

```powershell
pwsh -NoProfile -File C:\dev\elektron-work\tools\dt2\guard.ps1 -Seconds 600 -Log C:\dev\elektron-work\vectors-dt2\one.log `
  -Run 'C:\dev\elektron-work\tools\dt2\dt2_master_oracle.py --out C:\dev\elektron-work\vectors-dt2 --case hit_thr_000'
```

Scripts (aucune donnée du firmware dedans), dans `C:\dev\elektron-work\tools\dt2\` : `dt2_master_oracle.py` (le
harnais, ses cas dans `CASES`), `guard.ps1`, `regen.ps1`, `rerun-review.ps1`, `show_vectors.py` (niveaux et mots du compresseur par bloc),
`lag.py`. Leur copie versionnée est dans le dépôt, [`../tools/dt2/`](../tools/dt2/) ; les commandes ci-dessus
tournent aussi depuis elle. Vecteurs dans `C:\dev\elektron-work\vectors-dt2\`, hors du dépôt.

## Format des vecteurs

Par cas, deux fichiers :

- `<cas>.f32` : trames de trois flottants 32 bits little-endian, `in, outL, outR`, à 48 kHz. L'entrée est la même sur
  L et R. Les 48 premiers blocs (`settle_blocks`) sont du silence : le mix et les rampes du firmware sont lissés et
  partent de l'état de l'init, il leur faut une trentaine de blocs pour se poser.
- `<cas>.json` : réglages complets (`params`), niveaux RMS et crête de la sortie L et de l'entrée sur la partie signal,
  les mots `0x54`-`0x5c` de l'état du compresseur après chaque bloc, l'entrée de la table des ratios lue au premier
  bloc (`ratio_table_reads` : adresse, index, valeur, ratio), le compte d'instructions et la vitesse.

Pour rejouer un vecteur, un port part de l'état de l'init (ce que l'instantané garde), applique les réglages à chaque
bloc et compare toute la longueur, silence compris. `init-0x1c15e3.snap` (51 Mo, pickle de digikit) est une image
mémoire issue du firmware : il reste hors du dépôt.

Signaux, générés par le harnais : sinus 220 Hz d'amplitude 0,25 (−15,05 dB RMS), 60 blocs ; Hit synthétique de type
grosse caisse, 150 blocs (sinus balayé de 200 à 50 Hz, décroissance 150 ms, clic de bruit de 2 ms, amplitude 0,5).

## Mesures

Ratio 16:1 (`comp_ratio` = 6/7), drive 0, sortie L sur la partie signal :

| Cas | Seuil | RMS | Crête |
|---|---|---|---|
| sinus | 1,0 (0 dB) | −5,03 dB | −2,04 dB |
| sinus | 0,5 (−30 dB) | −6,69 dB | −2,04 dB |
| sinus | 0,0 (−60 dB) | −30,27 dB | −11,93 dB |
| Hit | 1,0 | −2,49 dB | 0,00 dB (écrêté après +10 dB) |
| Hit | 0,5 | −9,42 dB | 0,00 dB |
| Hit | 0,0 | −29,98 dB | −2,75 dB |

Sans compression, le sinus sort à +10,02 dB de son entrée : c'est le ×3,1623 de l'étape 4.

Drive, compresseur au repos (seuil 1,0) :

| Drive | RMS | Crête |
|---|---|---|
| 0,0 | −5,03 dB | −2,04 dB |
| 0,5 | −2,29 dB | −0,59 dB |
| 1,0 | −0,62 dB | −0,00 dB |

Réduction de gain sur le Hit (mot `+0x5b`, en dB) au bloc n après l'attaque (bloc 0 = premier bloc du Hit) :

| Seuil | n = 0 | 1 | 2 | 3 | 4 | 8 | 16 | 32 | 52 | 72 | 102 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 0,0 | −19,26 | −30,96 | −35,46 | −36,43 | −36,43 | −36,34 | −36,21 | −35,97 | −35,73 | −36,35 | −36,85 |
| 0,5 | 0,00 | −2,90 | −7,33 | −8,30 | −8,31 | −8,25 | −8,17 | −8,00 | −7,83 | −8,22 | −8,73 |
| 1,0 | 0 sur tout le Hit | | | | | | | | | | |

L'attaque prend environ 3 blocs (2 ms) avec `comp_attack` = 0 ; avec `comp_release` = 0,5 la réduction ne remonte que
d'environ 0,7 dB en 50 blocs. La sortie suit l'attaque : au seuil 0,0, le gain du bloc passe de +3,4 dB (bloc 0) à
−15,5, −23,2, −25,9 puis −26,4 dB.

Compresseur et drive ensemble sur le Hit (`hit_thr_050_drive_050` : seuil 0,5, 16:1, drive 0,5), contre le même Hit
sans drive (`hit_thr_050`) :

| Cas | RMS | Crête | Réduction de gain, n = 0 / 1 / 2 / 3 / 52 |
|---|---|---|---|
| seuil 0,5, drive 0 | −9,42 dB | 0,00 dB | 0 / −2,90 / −7,33 / −8,30 / −7,83 |
| seuil 0,5, drive 0,5 | −4,59 dB | −0,23 dB | 0 / −2,90 / −7,33 / −8,30 / −7,83 |

La réduction de gain est la même au centième près : le drive vient après le compresseur, il ne change pas ce que le
compresseur voit. La crête passe de 0,00 dB (l'écrêtage à ±1 après le +10 dB) à −0,23 dB : le saturateur reçoit le
signal déjà écrêté et l'arrondit. Le RMS monte de 4,8 dB. Gain par bloc au début du Hit : +10,79, +6,14, +6,21,
+8,23 dB avec drive, contre +9,29, +6,29, +4,74, +2,21 dB sans. L'ordre compresseur → +10 dB et écrêtage → saturateur
du code est donc exercé par un vecteur.

Ces nombres prouvent les deux critères du ticket : un sinus et un Hit traversent le master émulé, et la sortie change
avec le seuil et avec le drive. Ils ne prouvent pas que les réglages ont le sens de leur nom dans l'interface de la
machine (voir Inconnues).

## Vitesse

Environ 20 600 instructions SHARC par bloc de master, 22 000 à 28 000 instructions par seconde en Python, soit **33 à
41 échantillons par seconde** d'émulation (environ 1 400 fois plus lent que le temps réel), un cœur par processus.
L'init coûte 70 s une fois. Un Hit de 0,1 s plus 48 blocs de mise en place prend environ 3 min.

## Inconnues

- **Graine de RECIPS** : modèle de digikit, non vérifié ; un écart d'un ulp avec le silicium est possible.
- **Le sens exact de quatre réglages** (filtre de sidechain, attaque, relâche, makeup) : le calcul est lu, le nom vient
  du manuel et de la forme du code **[INFER]**. Le mot `+0x55` vaut −24,08 dB avec makeup 0 et son rôle n'est pas établi.
- **L'échelle des réglages côté machine** : la conversion de la trame est lue pour le seuil, le ratio, l'attaque, la
  relâche, le makeup et le mix (s16 × 1/32 512). Elle ne l'est pas pour le drive (`float_by` avec un exposant en R9 non
  relevé) ni pour le volume. La ColdFire (bouton → s16, lissage, LFO) ne tourne pas ici **[O]**.
- **Le second bus et les retours d'effets** restent à zéro : le chemin « piste hors compresseur » est vu dans le code,
  mais aucun vecteur ne l'exerce. Les sources de sidechain autres que 0 non plus.
- **Latence** : l'entrée et la sortie sont alignées au bloc près (le Hit sort dans son propre bloc). Le délai au sein du
  bloc n'est pas mesuré.
- L'état de départ est celui de l'init `0x1c15e3` exécutée seule, sans le reste du démarrage. Que la machine réelle
  arrive au premier bloc avec le même état n'est pas vérifié **[INFER]**.
