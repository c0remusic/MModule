# 03 — Oracle dynamique de la voix sampler du Digitakt II

Recherche du 2026-11-16, ticket [03](../issues/03-oracle-voix-dt2.md). Image DSP de l'OS 1.16 (blob identique en 1.17),
exécutée sur le cœur SHARC+ concret en Python de digikit (`scratch-arch\ext\digikit`, commit `9dee5d5`), depuis
l'instantané d'init du harnais du master (v1, ticket 03). Scripts hors dépôt, dans
`C:\dev\elektron\elektron-work\tools\dt2\` (`dt2_voice_oracle.py`, `dt2_voice_laws.py`, `regen_voice.ps1`, `guard.ps1`) ;
sondes dans `C:\dev\elektron\elektron-work\dt2-voice\`. Inférences marquées [INFER], inconnues [O].

## Réponse courte

- **L'oracle tourne.** Il appelle bloc après bloc la chaîne par voix `FUN_1c642a` telle que `FUN_1c2b24` l'appelle, une
  voix jouant un sample donné : armement (`0x1c4eaf`), case de la machine (setter de pas et de positions), voix
  `0x1c4ecf` (ONESHOT et ses cousines) ou STRETCH `0x1c5576`, BR `0x1cdd8b`, étage de niveau. Un mode rapide appelle la
  voix seule après le premier bloc : sorties identiques au bit près (vérifié en ONESHOT et en STRETCH), 0,2 s par bloc au
  lieu de 4,4 s. Chaque processus tourne sous `guard.ps1` (délai, kill au dépassement).
- **Vecteurs :** 21 cas dans `elektron-work\vectors-dt2` (16 ONESHOT, 4 STRETCH, 1 STRETCH de niveau, cf. §4), plus
  `laws.json` (pas, décimateur seul, réponse de la voix, niveau) et un `README.md` qui donne le format.
- **Coût mesuré** (instructions SHARC exécutées par voix et par bloc de 32) : ONESHOT 3 613 à 3 620 en lecture (3 782
  au bloc qui passe la fin, 3 977 au plus à l'envers ; 98 voix éteinte), dont 916 pour le décimateur ; STRETCH 1 751 à
  1 865. La chaîne `FUN_1c642a` entière coûte 108 à 113 k instructions par bloc avec une voix active (les 32 voix et
  leurs étages, presque tout fixe). Instructions, pas cycles [INFER : cycles ≥ instructions, cf. digikit finding 18].
- **Décimateur** `0xb80000` : récursif (quatre coefficients en mémoire, état par voix), plat à ±0,01 dB jusqu'à 20 kHz,
  −0,31 dB à 22 kHz, −1,0 dB à 23 kHz, rejet de 52 dB et plus au-delà de 30 kHz. La voix entière à l'unisson suit celle
  du Rytm à 0,35 dB près jusqu'à 23 kHz : **le décimateur ne justifie pas de variante** [INFER : même filtre ou
  équivalent dans la bande ; le Rytm n'a été mesuré que dans la bande].
- **LEV** : gain = 2 × (LEV/128)² × vélocité, rampé linéairement sur le bloc depuis la valeur du bloc précédent ;
  vélocité = courbe(vel) × 127/128 (OFF : 1 ; LIN : vel/127 ; LOG, EXP, EXP2 : tables). LEV 127, vel 127 : × 1,9535
  (+5,82 dB) ; avec le gain DC du noyau (0,4045), une voix ONESHOT pleine sort à −2,04 dB de son sample. STRETCH a le
  même niveau de sortie (son étage de niveau ajoute × 0,4047). **Diffère du Rytm** ((LEV/127)², sans le × 2) par
  l'échelle /128 et le facteur 2.

## 1. Le banc

`dt2_voice_oracle.py`, ce qui vient du firmware et ce qui est posé à la main :

| Étape | Comment | Preuve |
|---|---|---|
| État de départ | instantané après l'init `0x1c15e3` (celui du master, fiche sha256 vérifiée) | v1, recherche 03 |
| Sample | écrit en int16 à `0x310000` ; `FUN_1c4e70(R4 = enregistrement, R8 = pointeur, pile : trames, 0, fréquence)` range pointeur, longueur et fréquence (48 000) comme le bloc de chargement `0x1c3329` | code, puis relu |
| Trig | octet de verrou B (`0x2412c8 + 0xde04 + v`) mis avant le bloc 0 ; `FUN_1c642a` arme la voix et lance la case de la machine dans ce même bloc : le son part au bloc 0 | ACTIVE passe à 1 au bloc 0 |
| Réglages | écrits à chaque bloc dans l'enregistrement de paramètres `0x2506ec + v·0xdc` comme `FUN_1c24e9` les écrit depuis la trame (lois relevées par `probe_24e9.py`, tableau suivant) | sonde : chaque mot de trame tracé jusqu'à son champ |
| Note | tableau `0x24efac + 4v` (flottant, mot de trame / 256, écrit par `FUN_1c2b24` en `0x1c2d11`) | lu par la case en `M8` |
| Tempo | `0x252d50`, BPM en flottant, lu par la case de STRETCH | sonde : le rapport suit le BPM |
| Vélocité | `0x24f02c + 4v` = courbe[vel] × 127/128 (table de l'init en `0x8045a9b8`, `FUN_1c24e9` puis verrouillé au trig) | lu en `0x1c6a2b` |
| Captures | points d'arrêt dans `FUN_1c642a` : après la boucle des voix (`0x1c6b0f`), après BR (`0x1c6b32`), après la rampe de niveau (`0x1c6b8d`) ; tampon 96 kHz lu dans l'enregistrement (+0x4..+0x103) | |

Champs de l'enregistrement de paramètres que la voix lit :

| Champ | Valeur | Source dans la trame |
|---|---|---|
| +0x00 | TUNE / 128 (64 = 0,5) | mot `+0xda`, / 32 768 |
| +0x10 | TUNE | même mot, / 256 |
| +0x04, +0x08, +0x0c | STRT, LEN, LOOP en fraction de 120 | mots `+0xe6`, `+0xe8`, `+0xea`, / 30 720 |
| +0x14 | troisième bouton en entier (STRETCH : BARS) | mot `+0xea` >> 8 |
| +0x44 (court) | octet bas : sens inverse ; octet haut : boucle | dérivé de `+0xdc` [O : correspondance au bouton PLAY, ci-dessous] |
| +0x48 | LEV / 128 | mot `+0xec`, / 32 768 |
| +0x4c | sélecteur de machine (0 ONESHOT, 2 STRETCH) | cache des types |

## 2. Lois de la voix ONESHOT

| Quoi | Loi | Preuve |
|---|---|---|
| Pas | rapport = 2^(((TUNE − 64) + (note − 60))/12), somme bornée à ±64 demi-tons, × fréquence / 96 000 ; pas Q31 en échantillons source par sortie à 96 kHz (0,5 à l'unisson) ; TUNE fractionnaire continu (1/256 de demi-ton) | banc : +1 → 1,059463 ; ±12 → 2 et 0,5 ; TUNE 127 note 72 → +64 (borné) ; TUNE 0 → −64 |
| Interpolation | 6 prises, 256 phases, gain DC 0,4045 (−7,86 dB), bosse +1,88 dB à 10 kHz : le noyau du Rytm | banc, et recherche 10 |
| Retard | une sortie d'environ un échantillon : un Hit lu depuis 0 s'aligne à l'index source −1 ; une impulsion à l'index 64 sort aux index 65-66 | `check_pos.py` |
| STRT, LEN | début = STRT/120 × longueur (borné à longueur − 141), fin = (STRT + LEN)/120 × longueur | banc : STRT 30, LEN 60 sur 4 800 : début 1 199-1 200, 2 400 échantillons |
| Fin | la voix s'éteint au dernier bloc entier avant la fin : les derniers < 32 échantillons source ne sonnent pas (4 800 → dernier non nul 4 767) | banc, test `pos + 64·pas ≥ fin` en tête de `0x1c4f81` [INFER pour la lecture du test] |
| Sens | octet bas de +0x44 : pas négatif, départ à fin − 1 | banc : lecture à l'envers depuis 4 800 |
| Boucle | octet haut : reboucle entre point de boucle et fin ; LOOP 0 → boucle depuis STRT | banc : période 1 200 exacte pour LEN 30 |
| Niveau | gain = 2 × (LEV/128)² × vél ; rampe linéaire sur 32 échantillons depuis le gain du bloc précédent ; borné à [0, 1] avant le × 2 (jamais atteint) | `0x1c6a28`-`0x1c6a50` lu, puis banc : LEV 32 → 0,124023, 64 → 0,496094, 100 → 1,211166, 127 → 1,953 |
| Vélocité | courbe[vel] × 127/128 ; OFF = 1, LIN = vel/127, LOG/EXP/EXP2 tables de 128 (non recopiées) | init, `FUN_1c24e9` `0x1c2577`-`0x1c25cd` ; banc à LEV 127 : LIN 64 → 0,984436 (= 1,953490 × 64/127), LOG 64 → +5,10 dB, EXP 64 → −15,84 dB, EXP2 64 → −14,18 dB |

## 3. STRETCH

| Quoi | Loi | Preuve |
|---|---|---|
| Rapport de temps | BPM × (fin − début) / (96 000 × 120 × 2^BARS) : la portion dure 2^BARS mesures de 4 temps au tempo | sonde : 0,05 à 120 BPM, BARS 0, 4 800 échantillons ; /4 à BARS 2 ; /2 à 60 BPM |
| Hauteur | indépendante du temps, même loi que le pas ONESHOT (TUNE, note) | banc : pas 0x80000000 à TUNE 76 |
| Grains | lecture à hauteur 1 par grains ; saut tous les 2 048 échantillons de sortie (42,7 ms), fondu **linéaire** de 128 échantillons (poids n/127) | rampe à rapport 0,5 : saut de −1 024 positions à 2 048 et 4 096 |
| Pas d'interpolation 96 kHz | rendu direct à 48 kHz, sans le noyau ni le décimateur (gain DC 1) | banc ; tampon 96 kHz inutilisé |
| Niveau | celui d'ONESHOT × 0,4047 : LEV 127 → 0,790578 contre 1,953490, rapport constant sur toute l'échelle de LEV. STRETCH sort donc au même niveau qu'ONESHOT, dont le noyau a ce gain DC [INFER : c'est le facteur `F14` de `0x1c6a45`, 1 pour les autres sélecteurs] | `laws.json`, `lev.selector2` |
| Départ | le premier bloc fond un grain resté de l'init (position 1 024) vers le début [INFER : état de l'init, à vérifier sur la machine] | rampe |

Ce qui n'a pas été vérifié : rapport > 1 (saut vers l'avant), hop constant quel que soit le rapport (la constante 2 048,0
du code le suggère [INFER]).

## 4. Vecteurs

Format : `README.md` de `vectors-dt2`. Par cas : `.s16` (sample), `.f32` (voix, après BR, après niveau à 48 kHz),
`.w96.f32` (tampon 96 kHz par bloc), `.json` (réglages, pas, arguments du setter, ACTIVE et instructions par bloc).

| Cas | Ce qu'ils couvrent |
|---|---|
| `os_sine1k_unison`, `_up12`, `_down12` | sinus 1 kHz, 64 blocs |
| `os_impulse_unison` | réponse impulsionnelle noyau + décimateur |
| `os_noise_tune_frac` | bruit, TUNE 64 + 95/256 (pas non entier) |
| `os_hit_unison`, `_up12`, `_down12`, `_note72` | Hit synthétique entier |
| `os_hit_strt30_len60`, `os_hit_reverse`, `os_hit_loop_len30` | début et longueur, sens inverse, boucle |
| `os_hit_lev127`, `_lev100`, `_lev64`, `_lev32`, `os_hit_vel64_lin` | chaîne complète à chaque bloc : colonne après niveau |
| `st_hit_ratio1`, `st_hit_ratio05`, `st_hit_ratio1_up12`, `st_hit_lev127` | STRETCH à rapport 1, 0,5, +12 demi-tons, niveau |

Commande : `pwsh -NoProfile -File C:\dev\elektron\elektron-work\tools\dt2\regen_voice.ps1` (trois processus gardés,
environ 15 min). Un port rejoue un cas depuis l'état d'init : mêmes réglages, sample au même endroit, comparaison de
toute la longueur.

## 5. Coût

| Routine | Instructions par voix et par bloc | Note |
|---|---|---|
| `0x1c4ecf` ONESHOT, en lecture | 3 613-3 620 | identique de 0,5 à 2 de rapport : boucle fixe de 64 |
| idem, bloc qui franchit la fin / bouclage | 3 629-3 782 | |
| idem, à l'envers | jusqu'à 3 977 | |
| idem, voix éteinte | 98 | remplissage de zéros |
| dont `0xb80000` (décimateur) | 916 | appelé seul par `dt2_voice_laws.py` |
| `0x1c5576` STRETCH | 1 751-1 865 | |
| `FUN_1c642a` entier, une voix active | 108 000-113 000 | 32 voix et leurs étages |

## 6. Décimateur et voix entière, face au Rytm

Décimateur seul : sinus à 96 kHz dans son entrée de 64, comme la voix l'appelle (`R4` état, `R8` entrée, `R12` sortie).

| Entrée | 1 kHz | 10 kHz | 15 kHz | 18 kHz | 20 kHz | 21 kHz | 22 kHz | 23 kHz | 23,5 kHz |
|---|---|---|---|---|---|---|---|---|---|
| Gain | 0,00 | 0,00 | 0,00 | 0,00 | −0,01 | −0,07 | −0,31 | −1,02 | −1,90 dB |

Rejet (repliement vers 48 kHz − f) : −4,5 dB à 24,5 kHz, −11,6 à 26, −25,5 à 28, −52,2 à 30, −72,6 à 33, −55,0 à 36,
−52,3 à 40, −54,5 à 44, −57,4 à 47 kHz. Forme d'un demi-bande elliptique récursif [INFER].

Voix entière à l'unisson (sample à 48 kHz, gain relatif au DC de −7,86 dB) contre le Rytm (recherche 10) :

| | 1 kHz | 5 kHz | 10 kHz | 15 kHz | 18 kHz | 20 kHz | 22 kHz | 23 kHz |
|---|---|---|---|---|---|---|---|---|
| DT2 | +0,05 | +0,95 | +1,88 | +0,29 | −2,43 | −5,09 | −9,07 | −11,65 |
| Rytm | +0,05 | +0,96 | +1,88 | +0,29 | −2,4 | −5,2 | −8,9 | −11,3 |

Fréquences du DT2 calées sur des cycles entiers (19,97, 22,03, 22,97 kHz) : l'écart restant tient à ce décalage et aux
coefficients 16 bits du Rytm [INFER]. La pente au-delà de 18 kHz est celle du noyau ; le décimateur n'y ajoute que
0,3 à 1 dB à 22-23 kHz.

## Ce qui reste

- **PLAY** : la sonde de `FUN_1c24e9` donne, pour le mot de trame `+0xdc` = 0, 1, 2, 3 : (inverse, boucle) = (1, 0),
  (1, 1), (0, 1), (0, 0). Le codage que la ColdFire met dans ce mot n'est pas relu [O] ; l'oracle prend les deux octets.
- Le facteur `F14` du calcul de niveau : 1 en ONESHOT, 0,4047 mesuré en STRETCH ; qui le pose n'est pas lu [O].
- WERP, REPITCH, GRID, SLICE : même routine `0x1c4ecf`, autres setters (`0x1c4a31`, `0x1c4bf9`) ; non exercés.
- Stéréo (voix 2t + 1), lissage et LFO de la ColdFire, BR, filtres et suite de la chaîne : non exercés.
- `RECIPS` approché par digikit (un ulp possible, recherche 03 de v1) ; la voix elle-même n'en dépend pas [INFER : pas
  de division dans `0x1c4f81`].

## Sources

- Image DSP DT2 1.16 (`out/sections/dt2-1.16/section_7_BLOB.bin` de digikit), lue et exécutée sur place ; aucune donnée
  du firmware recopiée ici (adresses, constantes mesurées et réponses seulement).
- digikit : `docs/findings/06-sharc-engine-and-startup.md` (contrat de l'enregistrement de voix), `15-sharc-sample-path.md`
  (chargement, trig), `tools/sharc_dev/sharc_harness/`.
- Recherches [10](10-lecteur-rytm.md), [04](04-sampler-dt2-fonctions.md) ; v1 [03](../../elektron-drums-v1/research/03-oracle-dt2.md).
