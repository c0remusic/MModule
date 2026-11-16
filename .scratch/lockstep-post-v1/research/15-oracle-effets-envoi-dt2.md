# 15 — Oracle des effets d'envoi du Digitakt II, et face au Syntakt

Recherche du 2026-11-16, ticket [15](../issues/15-oracle-effets-envoi-dt2.md), suite de [14](14-effets-dt2.md). Image DSP
de l'OS 1.16 sur le cœur SHARC+ concret en Python de digikit, depuis l'instantané d'init du harnais du master (lu, jamais
réécrit). Scripts hors dépôt : `C:\dev\elektron\elektron-work\tools\dt2\` (`dt2_send_oracle.py`, `regen_sends.ps1`,
`guard.ps1`, `lockstep_sends\`) ; sondes et comparaisons dans `C:\dev\elektron\elektron-work\dt2-sends\`. Inférences
marquées [INFER], inconnues [O].

## Réponse courte

- **L'oracle tourne.** Mode `unit` : un effet seul, appelé comme le pilote `0x1c18a6` l'appelle, sur ses propres bus ;
  égal au bit près à l'effet dans le pilote entier (`check_unit.py`, 30 blocs, écart 0,0 sur chorus et reverb). Mode
  `driver` : le pilote entier, la piste 1 et ses trois envois, plus le bus de retour que lit le master. Réglages : les
  24 mots de trame posés, puis la section de réglages du firmware lui-même (`0x1c2d78`-`0x1c3064` de `FUN_1c2b24`,
  336 instructions) en fait les flottants. Garde de durée intégrée (`--seconds`) et `guard.ps1` autour.
- **Pourquoi les retours restaient à zéro** (recherche 14) : les VOL et envois entre effets sont à 0 dans l'instantané
  (l'orchestrateur les écrit depuis la trame à chaque bloc) ; en plus le delay part de 18 744 échantillons de temps,
  et chorus et reverb d'un mélange sec/humide à 0,5, tous deux lissés sur des dizaines de blocs (§3).
- **Vecteurs :** 29 cas dans `elektron-work\vectors-dt2\sends` (13 delay dont 5 `del2_*` de comparaison exacte,
  8 reverb, 6 chorus, 2 pilote entier ; impulsions, salves de bruit de 10 ms, sinus 1 kHz ; 40 à 450 blocs, soit 27 à
  300 ms), `README.md` (format, régénération).
- **Coût mesuré** (instructions SHARC par bloc de 32, l'effet seul) : chorus 14 393-14 460, delay 5 421-5 426, reverb
  14 562-14 566 ; le pilote entier 44 560-44 624, constant à ±64.
- **Delay : celui du Syntakt.** Le port de Lockstep (`syntakt::fast::Delay`) rend le delay du DT2 à −97 à −128 dB près
  (erreur rapportée à l'énergie) sur impulsions, bruit filtré, FDBK 96, X, WID 0 et 127, une fois posées deux
  différences de constantes : le retour du DT2 vaut **× 0,5** celui du port, et FDBK y vaut mot / 32 768 contre
  66 053 · mot / 2³¹ (+0,8 %). Seule autre différence : le lissage du temps, en flottant (0,9961 de l'écart par bloc)
  contre l'entier du Syntakt (255/256) ; le temps atteint peut différer d'un échantillon selon le réglage.
- **Reverb : différente.** Même loi de pré-délai (à un échantillon près) et même table de pôles probable, mais un autre
  réseau : corrélation nulle avec le port (≤ 0,015), écho fort tous les ~1 022 échantillons après le pré-délai, sortie
  creuse (kurtosis 55 à 1 300 sur 50 ms contre 3 pour le port), décroissance plus lente.

## 1. Le banc

| Étape | Comment | Preuve |
|---|---|---|
| État de départ | instantané après l'init `0x1c15e3` (celui du master) | v1, recherche 03 |
| Réglages | trame `0x2558dc` + `0x6da + 2(k − 25)`, k = 25-48, valeur << 8 ; puis `0x1c2d78`-`0x1c3064` du firmware | `probe_fx_frame.py` : chaque mot tracé jusqu'à son flottant |
| Tempo | `0x252d50`, BPM flottant, mot +5 du contexte `0x252d3c` ; lu par le delay seul (`0xb8216d`) | point d'observation en lecture |
| Appel d'un effet | R4 son état DDR, R8 ses flottants, R12 `0x252d3c` ; paires de pointeurs entrée/sortie en `0x252d40`/`0x252d48` ; son 7ᵉ flottant à 1,0 | listing du pilote |
| Envois de piste | `0x252658` CHO, `0x2525cc` DEL, `0x252540` REV, + 4v par voix (piste t = voix 2t), mot / 32 768 | `FUN_1c24e9` écrit les trois depuis la trame de piste +0x126, +0x122, +0x124 ; le pilote les élève au carré |
| Entrées audio du pilote | R8 vers deux tampons muets en L2 | |
| Captures | points d'arrêt aux appels et retours des trois effets dans le pilote | |

Effets, état et bus :

| Effet | Entrée | État | Flottants (k) | Bus entrée → sortie |
|---|---|---|---|---|
| Chorus | `0xb82680` | `0x80459388` | `0x252504`-`0x252510` (DPTH SPD HPF WDTH, k 25-28) | `0x254478` → `0x254578` |
| Delay | `0xb82150` | `0x8005912c` | `0x2524e8`-`0x2524fc` (TIME X WID FDBK HPF LPF, k 32-37) | `0x253e78` → `0x253f78` |
| Reverb | `0xb80fe0` | `0x80000018` | `0x2524cc`-`0x2524e0` (PRE DEC FREQ GAIN HPF LPF, k 41-46) | `0x254178` → `0x254278` |

Le 7ᵉ flottant de chaque bloc (`0x252514`, `0x252500`, `0x2524e4`), mis à 1,0 par le pilote avant chaque appel, règle le
mélange sec/humide de chorus et reverb, lissé (environ 0,887 de l'écart par bloc) : l'instantané part de 0,5, d'où une
fuite de 0,5 × l'entrée au premier bloc. L'oracle fait 96 blocs muets avant chaque cas de chorus, de reverb et de
pilote (reste < 1e-5).

Lois des mots, lues par la sonde :

| Mots (k) | Flottant |
|---|---|
| boutons d'effet (sauf X, WID, FDBK) | mot / 32 512 (0-1) |
| X (33) | mot brut (0 ou 256) |
| WID (34), FDBK (35) | mot / 32 768 |
| CHO DEL (29), CHO REV (30), CHO VOL (31) | `0x2526e0`, `0x2526e4`, `0x2526d8`, mot / 32 768 |
| DEL REV (38), DEL VOL (39) | `0x252654`, `0x25264c`, idem |
| REV VOL (47) | `0x2525c0`, idem |
| routages (40, 48) | non lus par cette section |

Les tableaux d'envoi ont 32 entrées de voix puis, juste après, le VOL et les envois de l'effet (`0x252540` + 0x80 =
`0x2525c0`, REV VOL) : c'est ce qui les identifie, et le chemin du pilote le confirme (bus du chorus, du delay, de la
reverb ; chorus × CHO DEL² ajouté au bus du delay, chorus × CHO REV² et delay × DEL REV² à celui de la reverb).

Mode pilote, mesuré : le bus `0x254778` que lit le master = 2 × (DEL VOL/128)² × delay + (REV VOL/128)² × reverb, au
résidu de 7e-9 près ; le chorus n'y est pas (son retour va ailleurs [O]). La reverb lit son bus × 0,5.

## 2. Delay face au Syntakt

Port : `stFastDelay.*` (worktree, lu seulement), compilé hors dépôt dans un petit harnais
(`elektron-work\tools\dt2\lockstep_sends\`, `build-sendcmp\harness.exe`) ; même entrée, mêmes mots, 48 kHz.

| Quoi | DT2 | Syntakt (port) | Preuve |
|---|---|---|---|
| Temps | (mot TIME + 256) × 48 000 >> 10 × 900 / (BPM × 120), borné à 8 s ; mêmes constantes lues dans `0xb81f28` (30-300 BPM, × 120, + 256, × 48 000, >> 10, × 900) | même formule | TIME 8 à 240 BPM : 3 375 ; TIME 1 / 2 à 120 : 1 500 / 2 250, les deux côtés |
| Lissage du temps | flottant, 0,99610 de l'écart par bloc (état +0x400240, cible +0x400244) | entier, ~255/256, s'arrête à quelques échantillons | 18 744 → 18 684 → 18 624 ; au repos : 3 375 contre 3 374 |
| Deux prises | prise du bloc et du bloc précédent (états +0x40010c, +0x400110) | idem, fondu sur le bloc | |
| Filtres | pôle HPF 0,9937195, 0,5 (1 + pôle) 0,9968598 à HPF 32 | table du port : 0,99371946, 0,99685973 | état +0x400130/+0x400134 |
| FDBK | mot / 32 768 | 66 053 · mot / 2³¹ | `0x2524f4` ; écho 2 / écho 1 |
| Retour | × 0,5 | × 1 | pics 0,37382 contre 0,74764, rapport 0,50000 |

Avec ces deux corrections (port × 0,5, FDBK du port pris à FDBK × 32 512,4/32 768) et le temps au même échantillon
(`del_exact.py`) :

| Cas | Erreur 2 × DT2 − port, rapportée à l'énergie |
|---|---|
| impulsion, TIME 1 / TIME 2, 120 BPM | −105,7 / −128,5 dB |
| FDBK 96 | −99,7 dB |
| X ON, impulsion à gauche | −102,2 dB |
| WID 0 / 127, impulsion à gauche | −104,9 / −104,2 dB (WID 0 : droite muette des deux côtés) |
| bruit 10 ms, FDBK 64, HPF 64, LPF 64 | −97,5 dB |
| TIME 8 à 240 BPM | le port sort un échantillon plus tôt par écho (lissage), sinon même forme |

**Verdict : identique** (même algorithme, mêmes lois), à deux constantes et l'arithmétique du lissage près. Le port de
Lockstep sert le DT2 avec un gain de retour de 0,5, FDBK sur 32 768 et, pour un temps au bit près, un lissage en
flottant. L'écrêtage doux (2x − x|x|) est compris dans l'accord : à 0,5 d'entrée les échos s'accordent à −100 dB.

## 3. Reverb face au Syntakt

| Quoi | DT2 | Syntakt (port) |
|---|---|---|
| Pré-délai, premier échantillon humide | PRE 0 : 37 ; PRE 16 : 292 ; PRE 64 : 4 132 | (2 · mot² >> 17) + 37 : 37, 293, 4 133 |
| Après le pré-délai | queue brève du filtre d'entrée, puis un écho fort à + ~1 022, et ainsi de suite (pré + 1 022, 2 044, 3 068, 4 092…) dont l'amplitude suit DEC | huit diffuseurs (53 à 809) en série, réponse dense dès le pré-délai |
| Densité (kurtosis par 50 ms) | PRE 0, DEC 31 : 1 286, 354, 223, 164 ; défauts : 354 à 104 de 50 à 250 ms ; DEC 127 : 322 → 55 | 3 à 4 dès le début |
| Corrélation avec le port | ≤ 0,015 à décalage 0 (impulsion, bruit), meilleur décalage sans sens (45-54) | |
| Décroissance, bruit 10 ms, DEC 31 | −7 dB en 300 ms | −12 dB |
| Accès mémoire, un bloc | 3 711 lectures et 751 écritures de mots DDR (116 lectures par échantillon), écritures en pages espacées de 32 Ko (8 192 flottants) | |

**Verdict : différente** (autre topologie et autres longueurs), avec deux points communs mesurés : la loi du pré-délai
et, probablement, la table de pôles de ses filtres d'entrée [INFER : même pôle mesuré au delay ; la reverb non sondée].
Le port de Lockstep ne la rend pas : il faut un port à part si Lockstep doit sonner comme le DT2.

Lecture : la sortie creuse n'est pas un défaut de l'émulateur [INFER] : la reverb lit ses lignes en DDR sans faute, aucun
accès aux registres (DMA ou autres), 62 % de lectures non nulles après 100 blocs ; mais aucun enregistrement de la
machine n'a été comparé ici.

## 4. Chorus

Pas d'équivalent au Syntakt. Aux défauts (DPTH 127, SPD 32, WDTH 127), une impulsion ressort à 274-283 échantillons à
gauche, quelques échantillons plus loin à droite, étalée (lecture interpolée [INFER]) ; DPTH 0 donne un retard fixe ;
SPD 127 module plus vite (`cho_lag.py`). Lois de DPTH, SPD, WDTH et du HPF : non relevées [O].

## 5. Coût

| Routine | Instructions par bloc de 32 | Note |
|---|---|---|
| Chorus `0xb82680` | 14 393-14 460 | 14 393 à WDTH 0 |
| Delay `0xb82150` | 5 421-5 426 | 5 421 avec X, 5 424 à WID 0 |
| Reverb `0xb80fe0` | 14 562-14 566 | |
| Pilote `0x1c18a6` entier | 44 560-44 624 | dont ~10 k de bus, envois et lissages |
| Section de réglages `0x1c2d78`-`0x1c3064` | 336 | |

Instructions, pas cycles [INFER : cycles ≥ instructions]. Mur : 0,35 s par bloc pour le delay seul, 0,9 à 1,6 s pour
chorus et reverb, selon la charge.

## Ce qui reste

- Retour du chorus : où il rejoint le master [O] (pas dans `0x254778`).
- Lois de la reverb (DEC, FREQ, GAIN, HPF, LPF) et longueurs de son réseau : non relevées ; vecteurs disponibles.
- Lois du chorus : idem.
- Le facteur 0,5 de l'entrée de la reverb en mode pilote et la rampe des envois : mesurés, non lus dans le code.
- Le mélange sec/humide (7ᵉ flottant) : sa loi exacte n'est pas relevée ; l'oracle le laisse à 1,0, comme le pilote.
- Aucune comparaison à la machine réelle.

## Sources

- Image DSP DT2 1.16 de digikit, exécutée sur place ; aucune donnée du firmware recopiée ici (adresses, constantes
  mesurées et réponses seulement).
- digikit : `docs/findings/04-coldfire-dsp-link.md` (mots Fx `0x6da + 2(k − 25)`, indices 25-48),
  `53-stock-dsp-shape.md` (pilote et états des effets).
- Port : `source/elektron/syntakt/stFast/stFastDelay.*`, `stFastReverb.*`, `stFastTables.*`.
- Recherches [14](14-effets-dt2.md), [03](03-oracle-voix-dt2.md).
