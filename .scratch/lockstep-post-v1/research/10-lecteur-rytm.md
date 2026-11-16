# 10 — Où le Rytm rend sa couche sample

Recherche du 2026-11-16, ticket [10](../issues/10-lecteur-rytm-localise.md), suite de [08](08-couche-sample-rytm.md).
Lecture de la section 3 de l'OS 1.74 (Analog Rytm MKII) et banc oracle3 (`rytmrig.py`) sous garde de durée ; scripts
hors dépôt, dans `C:\dev\elektron\elektron-work\tools\analog\` (`rytm_sample_bench.py`, `rytm_interp*.py`,
`rytm_pitch_table.py`, `rytm_vs_dt2_kernel.py`, `rytm_refs.py`, `rytm_lin.py`). Inférences marquées [INFER].

## Réponse courte

- **Sur le CPU**, pas sur le FPGA (certitude haute : lu dans le code, puis exécuté au banc). Le ColdFire principal rend
  le sample de chaque voix par bloc de 32 échantillons à 48 kHz et l'écrit dans la voie de la voix dans l'anneau TDM vers
  les CNA. Le FPGA ne fait que sérialiser l'anneau [INFER, comme en 08].
- **Routines :** `0x4011C0B2` (init des 8 voix et de deux canaux eDMA), puis à chaque bloc, dans l'interruption audio
  `0x4011FB66` : `0x4011C1E0(trigs, stops)` (DMA de la voix 0, pas de toutes les voix), `0x4011C6B8(trigs, stops)`
  (rend la voix v pendant que le DMA amène la fenêtre de la voix v+1), puis `0x4010EA98(anneau)` qui somme dans l'anneau.
- **Le lecteur est celui du Digitakt II :** 64 échantillons à 96 kHz par interpolation polyphasée à 6 prises et 256
  phases, puis décimation 2:1. Les coefficients sont ceux du DT2 arrondis à 16 bits (écart maximal 1,5e-5, moins d'un
  demi-LSB, sur les 1 536 prises). L'interpolation ne diffère pas.
- **Ce qui diffère du DT2 :** la loi de BR (dans la voix, pas continu de quantification, 0,073 octave par cran ; DT2 :
  table de 128, puissances de deux, après la voix), la table de hauteur (2 048 pas par octave, ±24 demi-tons), la
  précision (MAC 16 × 16 bits), peut-être le décimateur (récursif ici ; celui du DT2 n'est pas décodé). BR devient un
  choix de la couche sample. L'interpolation, elle, ne justifie pas un lecteur à part.

## 1. Le chemin, du sample à l'anneau

Lu dans le code, puis confirmé au banc (§3).

1. **Mémoire.** Table des emplacements à `0x41329E70` + 16 × slot (131 entrées, `cmpi #$82`) : pointeur des données
   (DDR), fréquence (mot, 48 000 par défaut), longueur en échantillons, rapport fréquence/48 kHz (Q30, 2^30 à 48 kHz,
   écrit par `0x4011BF8A`). L'octet SMP des paramètres de piste choisit l'emplacement.
2. **DMA.** Deux canaux eDMA (TCD `0xFC0453E0` et `0xFC045400`, pointeurs en `0x80005820/24`) copient une fenêtre de
   0x8C échantillons de 16 bits (alignée sur 8) de la DDR vers un double tampon en SRAM (`0x80005880`, `0x800059E0`).
   Passé la fin, la source devient un tampon de silence (`0x4F9372E0`). Le CPU attend le bit DONE avant de lire.
3. **Interpolation** (`0x4011CC5E`-`0x4011CCAE`) : 64 sorties à 96 kHz ; position entière + phase 32 bits ; les 8 bits
   hauts de la phase choisissent une ligne de 6 coefficients de 16 bits (SRAM `0x8000BC00`, trois tables de 256 mots
   doubles, copiées au boot depuis la section 3) ; fenêtre avant de 6 échantillons depuis la position, `mac.w` 16 × 16
   dans un accumulateur 32 bits.
4. **Décimation 2:1** (`0x4011CD46`-`0x4011CE20`) : filtre récursif à quatre coefficients sur les 64 sorties, état par
   voix dans le bloc d'état (`0x80005B30` + 0x5C × voix, octets +0x2C-+0x5B), saturé ; 32 sorties à 48 kHz.
5. **BR puis LEV** (`0x4011CE5E`-`0x4011CF58`), §2 ; résultat : 32 mots de 32 bits à `0x800067F8` + 0x80 × voix.
6. **Mixage dans l'anneau** (`0x4010EA98`) : pour la voix v, voie v (octets +4v de chaque trame de 0x40) de la moitié
   courante (`0x80000000` + index `0x42F92124` << 11) = bloc sample + bloc machine de `0x80006BF8` + bloc
   `0x80007040` × niveau rampé des « heads » `0x80007000` (le bruit de la machine [INFER]) + 0,5 × une voie externe
   choisie par la table `0x401BED40` (aucune si négatif ; tampon `0x42AC6F30` [INFER : entrée audio ou USB]). Les voies
   8-10 restent le clic des toms (08), la voie 11 un niveau lissé écrit à `0x4012134A`.

Conséquence pour 08 : le sample n'est pas sommé au générateur en analogique. Il est sommé **en numérique** aux signaux
numériques de la machine (bruit, clic) dans la voie de CNA de la voix ; seul l'oscillateur analogique s'y ajoute après
le CNA [INFER].

Paramètres de piste (`0x8000F7BA` + 0x54 × piste), page SAMPLE à +0x10 : TUNE +0x10, FINE +0x12, SMP octet +0x14,
BR +0x16, STRT +0x18, END +0x1A, LOOP octet +0x1C, LEV +0x1E (mots valeur << 8). Les noms de +0x16, +0x1A et +0x1C
suivent l'ordre de la page et l'usage lu [INFER pour BR : seul mot restant, usage conforme au §2].

## 2. Les lois

| Quoi | Loi | Preuve |
|---|---|---|
| Pas | pas (Q28, en échantillons source par sortie à 96 kHz) = rapport × 2^(s/12) / 2, soit 0,5 à l'unisson (32 échantillons source par bloc). s = (TUNE − 64) + (FINE − 64)/64 + (note − 60), borné à ±24 demi-tons. Le code ajoute le mot FINE brut (centré sur 0x4000, soit +1 demi-ton) à une constante de 23 : 23 + 1 = 24, le milieu de la table. Note : 60, ou la hauteur de piste `0x80006388` si le bit 1 de `0x8000EA18` est mis | `0x4011C5F4`-`0x4011C690` ; banc : TUNE 0/±12/±24 donnent 0,5/1, 0,25/2, 0,125 exactement ; +25 borné à 2 |
| Table de hauteur | `0x40228F14`, 8 193 mots, 2^(i/2048) à 3,6e-9 près (formule exacte) ; index tronqué : 170,67 pas par demi-ton, erreur jusqu'à −0,4 cent (TUNE +1 : 1,059224 contre 1,059463) | `rytm_pitch_table.py`, banc |
| Interpolation | 6 prises, 256 phases, coefficients 16 bits = noyau du DT2 arrondi ; passe-bas, pas interpolant (gain DC 0,4047, ligne 0 non impulsionnelle) ; réponse propre +0,9 dB à 0,1 fs, +1,9 dB à 0,2 fs, −3,9 dB à 0,4 fs, −12,7 dB à fs/2, rejet ≥ 53 dB au-delà de 0,7 fs | `rytm_interp2.py`, `rytm_vs_dt2_kernel.py` |
| Chaîne entière à l'unisson | 100 Hz 0 dB ; 1 kHz +0,05 ; 5 kHz +0,96 ; 10 kHz +1,88 ; 15 kHz +0,29 ; 18 kHz −2,4 ; 20 kHz −5,2 ; 22 kHz −8,9 ; 23 kHz −11,3. Le décimateur est plat dans la bande : la bosse de 2 dB vers 10 kHz est celle du noyau. Gain : 1 LSB d'entrée = 26 101 en sortie (32 bits) | banc, sinus de 16 384 |
| BR | quantification du signal décimé, avant LEV, par troncature [INFER : produit fractionnaire tronqué] : pas = 2^(6,06 + 0,0729 × BR) LSB d'entrée, soit ≈ 9,9 − 0,073 × BR bits équivalents ; BR 0 contourné ; BR 1 donne déjà 9,9 bits, BR 64 5,3, BR 96 3,0, BR 127 0,7 (deux niveaux). Pas continu (exposant + mantisse par table), pas des puissances de deux | `0x4011CEC6`-`0x4011CF58` ; banc sur rampe : pas de 2^6,13 à 2^15,31 |
| LEV | gain = (LEV/127)^2 exactement, rampé linéairement sur les 32 échantillons du bloc ; mis à 0 au bloc d'un retrig de la voix (masque `0x8000586C`) | banc : 0,62000 à 100, 0,25395 à 64, 0,06349 à 32 |
| STRT/END | début = longueur × STRT/120, fin = longueur × END/120 (saturé à la longueur), mots en 1/256 de cran | `0x4011BF44` |
| Sens | END < STRT : lecture à l'envers (pas négatif, fenêtre DMA décalée) | code ; banc : pente −78 319 contre +78 301 |
| LOOP | OFF : une lecture, puis le niveau est divisé par 4 à chaque bloc (−12 dB par 0,67 ms) ; ON : reboucle entre STRT et END | code `0x4011CEA4` ; banc : END 60 sur 640 échantillons, LOOP 0 s'éteint en deux blocs, LOOP 1 continue |
| Latence | un trig agit au bloc suivant (masque du bloc précédent, `0x80005868`, le DMA ayant un bloc d'avance) ; le noyau ajoute environ 2 échantillons (prise de pic 2 sur 6) | banc : trig au bloc 1, premier échantillon au bloc 2 |

Ce qui n'a pas été vérifié : la vélocité et le LFO (ils agissent en amont, sur les mots de paramètres [INFER]) ; les
samples à une autre fréquence que 48 kHz (le rapport Q30 le prévoit) ; le décodage complet du décimateur, mesuré
seulement par sa réponse.

## 3. Le banc

`rytm_sample_bench.py` : oracle3 avec la section 3, l'image SRAM copiée comme au boot (`0x402CE000` → `0x80000000`,
`0x402D6000` → `0x80008000`), un eDMA émulé (un START sur le CSR d'un TCD copie CITER × NBYTES octets et met DONE), un
sample synthétique (impulsion, sinus, rampe) dans un emplacement. Chaque bloc appelle `0x4011C1E0` puis `0x4011C6B8`
comme l'interruption ; 8 blocs en 1,4 s. Le pas lu dans l'état de la voix est 0x08000000 à l'unisson, la position avance
de 32 par bloc, et la sortie de la voix 0 est bien le sinus d'entrée à sa fréquence : la lecture se fait entièrement sur
le CPU. Aucune routine ne passe au FPGA autre chose que l'anneau.

## 4. Face au lecteur du Digitakt II

Le DT2 (recherche 04, et `digikit` : `06-sharc-engine-and-startup.md`, `sharc_harness`) rend aussi 64 échantillons à
96 kHz par 6 prises et 256 phases (table DSP `0x25D940`, Q31, fenêtre avant), puis décime 2:1 (`0xB80000`, « lit 64,
écrit 32, × 0,5 » : moyenne de paires dans la référence du harnais, non confirmée).

| | Rytm MKII 1.74 | Digitakt II 1.16 | Verdict |
|---|---|---|---|
| Noyau | le même, 16 bits | Q31 | **identique** (mesuré) |
| Arithmétique | entiers 16 × 16 → 32 bits | flottant après la MAC | écart sous le bruit 16 bits [INFER] |
| Décimateur | récursif, plat à ±0,3 dB jusqu'à 15 kHz | « × 0,5 », non décodé | **à mesurer** quand le DT2 tournera (ticket 03) |
| Hauteur | table 2 048 pas/octave, troncature, ±24 demi-tons | `powf`, pas Q31, ±5 octaves | différence sous 0,4 cent ; la plage du Rytm est plus courte |
| BR | dans la voix, avant LEV ; pas continu, 0,073 octave par cran ; BR 1 = 9,9 bits, BR 127 = 0,7 | après la voix, table de 128, puissances de deux, 16 à 1 bit | **diffère** par la loi et l'échelle des crans |
| LEV | (LEV/127)^2, rampé par bloc | loi non relevée | ouvert |
| Fin sans boucle | niveau ÷ 4 par bloc | anti-clic à part [D] | proche [INFER] |

Pour Lockstep (ticket 09) : la couche Rytm se porte. Elle n'a pas besoin d'un lecteur à elle pour l'interpolation : le
lecteur DT2 prévu (ONESHOT) a le même noyau. Ce qui lui est propre tient en trois réglages du même lecteur : la loi de BR
du Rytm, placée dans la voix avant LEV, en choix à côté de la BR du DT2 ; la borne ±24 demi-tons et LEV au carré ; la
division par 4 en fin de lecture. Si le décimateur du DT2 se révèle différent, celui du Rytm s'ajoute en variante.
Rien de la chaîne analogique ne change par rapport à 08.

## Sources

- Section 3 de l'OS 1.74 (`scratch-arch\research\rytm\rytm_174_sec3.bin`, chargée à 0x40000400), lue et exécutée sur
  place ; aucune donnée du firmware recopiée ici.
- `digikit` (`scratch-arch\ext\digikit`) : `docs/findings/06-sharc-engine-and-startup.md` (voix du DT2),
  `tools/sharc_dev/sharc_harness` (lecture de la table `0x25D940` dans l'image DT2 1.16, pour la comparaison).
- Recherches [04](04-sampler-dt2-fonctions.md) et [08](08-couche-sample-rytm.md).
