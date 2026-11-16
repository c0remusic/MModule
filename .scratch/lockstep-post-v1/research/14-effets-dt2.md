# 14 — Effets d'envoi du Digitakt II : réglages, adresses, oracle

Recherche du 2026-11-16 pour le ticket [14](../issues/14-effets-dt2-en-envois.md). OS 1.16 (blob DSP identique en
1.17). Marques : **[M]** manuel d'Elektron (OS 1.17, ch. 4.2 et 12.1-12.4, p. 15 et 61-63) ; **[V]** lu ou mesuré ici ;
**[D]** dit par digikit ou une recherche antérieure, non revérifié ; **[INFER]** déduit.

## 1. Réglages

Source des bornes et défauts : la table des descripteurs de la ColdFire (`0x4020f18c` sur 1.16, lue par
`tools/coldfire/paramtable.py` de digikit) **[V]**. Mots s16 de 0 à `0x7f00`, valeur affichée = octet haut, donc 0-127
sauf mention. Les réglages des effets sont au niveau du pattern ; chaque piste a ses trois envois CHO, DEL, REV
(0-127, défaut 0) sur sa page FX **[M]**. Les paramètres d'effet ne se verrouillent pas et ne sont pas des destinations
de LFO (digikit, finding 53) **[D]**.

### Delay (page 1)

| Bouton | Nom long | Défaut | Notes |
|---|---|---|---|
| TIME | Delay Time | 23 | relatif au tempo, en 128es de note ; le tableau du manuel va de 1 (1/128) à 128 (1 mesure), avec des valeurs fractionnaires pour les triolets (2,67, 5,33…) **[M]** ; bornes 0-127 du descripteur **[V]**, d'où un pas fin sous l'octet haut [INFER] ; aucun mode libre en ms **[M]** ; ligne de 8 s au plus **[D]** |
| X | Pingpong | OFF | 0-1 (max `0x100`) **[V]** ; ON : alterne gauche/droite, WID règle l'écart ; OFF : WID place le retour **[M]** |
| WID | Stereo Width | 64 | bipolaire **[M]** |
| FDBK | Feedback Gain | 32 | auto-oscillation possible aux valeurs hautes **[M]** |
| HPF | Feedback HPF | 32 | |
| LPF | Feedback LPF | 96 | les filtres sont dans la boucle (nom long « Feedback ») **[V]** |
| REV | Reverb Send | 0 | delay vers reverb **[M]** |
| VOL | Delay Mix Vol. | 110 | niveau du retour **[M]** |
| — | Delay FX Routing | 1 | 0-1, aucune page ne le montre, mot de trame jamais lu par le DSP (digikit, finding 04) **[D]** |

Le temps se calcule depuis TIME et le tempo (mise à jour `0xb84604` du DN2, constantes 30, 300, 120,0 et 48 000 :
dn2-cfw) **[D]**.

### Reverb (page 2)

| Bouton | Nom long | Défaut |
|---|---|---|
| PRE | Pre-delay | 16 |
| DEC | Decay Time | 31 |
| FREQ | FB Shelving Freq | 64 |
| GAIN | FB Shelving Gain | 50 ; au maximum les aigus restent, en dessous ils s'amortissent **[M]** |
| HPF | Input HPF | 32 (filtre l'entrée de la reverb **[M]**) |
| LPF | Input LPF | 96 (idem) |
| VOL | Reverb Mix Vol. | 110 |
| — | Reverb FX Routing | 1, 0-1, sans page **[V]** descripteur ; lecture DSP non relevée |

### Chorus (page 3)

| Bouton | Nom long | Défaut | Notes |
|---|---|---|---|
| DPTH | Depth | 127 | profondeur du LFO **[M]** |
| SPD | Speed | 32 | vitesse du LFO ; le manuel ne parle d'aucune synchro au tempo **[M]** |
| HPF | High-pass | 0 | filtre l'entrée **[M]** |
| WDTH | Width | 127 | largeur stéréo |
| DEL | Delay Send | 0 | chorus mouillé vers delay **[M]** |
| REV | Reverb Send | 0 | chorus mouillé vers reverb **[M]** |
| VOL | Chorus Mix Vol. | 100 | |

Ni forme d'onde, ni retard de base, ni feedback exposés **[V]** (descripteurs).

### Routage

Diagramme du manuel (4.2) **[M]** : pistes → envois CHO, DEL, REV ; chorus → delay et reverb ; delay → reverb ; les
trois retours au mixer. La page COMPRESSOR ROUTING (9.8.1) choisit pistes, entrées et effets envoyés au compresseur ;
le diagramme montre les retours avant ou après le compresseur **[M]**. Côté DSP, le master ajoute les retours
(`0x254b78`, `0x254c78`, `0x254778`) au bus du compresseur ou à l'autre selon les bits 0-2 de `0x25253c` (recherche 03
de la v1) **[V]**. L'entrée audio a ses propres envois CHO, DEL, REV **[M]** (sans objet pour Lockstep).

## 2. Où ils tournent

| Routine | Adresse | Appel | Coût |
|---|---|---|---|
| Pilote des envois | `0x1c18a6`, appelé par `0x1c2b24` en `0x1c3090`, juste avant le master `0x1c207b` | R4 = `DM(I6-17)`, R8 = `DM(I6-18)` de l'orchestrateur **[V]** | 44 561 instructions par bloc de 32, constant, les trois effets compris **[V]** mesuré ici |
| Chorus | `0xb82680`, appel en `0x1c1d3d`, état DDR `0x80459388` | **[V]** listing | 14 457 (DN2) **[D]** |
| Delay | `0xb82150`, appel en `0x1c1db4`, anneaux DDR `0x8005912c` | **[V]** listing | 5 424 (DN2) **[D]** |
| Reverb | `0xb80fe0`, appel en `0x1c1e6d`, état `0x80000018` | **[V]** listing | 14 563 (DN2) **[D]** |

Avant chaque effet, le pilote écrit sa paire de pointeurs d'entrée et de sortie en `0x252d40`/`0x252d48` **[V]** listing.
Ordre dans le bloc : chorus, delay, reverb **[V]**, compatible avec les envois chorus → delay → reverb [INFER].

Mots lus par le pilote (un bloc, point d'observation en lecture) **[V]** :
- `0x2524cc`-`0x252514`, 19 flottants : les réglages des effets [INFER], écrits à chaque bloc par l'orchestrateur ;
- trois tableaux par piste, pas de 8 octets : `0x252540 + 8t`, `0x2525cc + 8t`, `0x252658 + 8t`. Mis à 1,0 pour la
  piste 1, ils remplissent trois bus d'envoi, `0x253e78`, `0x254178`, `0x254478` (128 mots chacun), avec un bloc de
  retard (lissage) **[V]** ; lequel est CHO, DEL ou REV : **[O]** ;
- `0x254d9c + 0x38k`, 48 mots (16 pistes × 3 envois) : états de lissage [INFER] ;
- le tempo `0x252d50` (flottant, BPM ; 0 dans l'instantané).

## 3. Oracle : probablement oui, pas encore

Essai court ici (scripts jetables dans le scratchpad, sous `guard.ps1`, copie de l'instantané) :
- le pilote `0x1c18a6` s'appelle seul depuis l'instantané d'init du harnais du master, sans ColdFire ni orchestrateur,
  revient à son adresse de retour, et le cœur Python lit et écrit la DDR des effets sans faute **[V]** ;
- 2 à 2,7 s de mur par bloc de 32 (environ 3 500 fois plus lent que le temps réel à 48 kHz) **[V]** ;
- les envois de la piste 1 remplissent les trois bus d'envoi ; mais les trois retours que lit le master restent à zéro
  sur 12 blocs avec un clic, réglages de l'instantané **[V]**. Où le pilote écrit la sortie des effets (pointeurs de
  sortie pris sur la pile pendant l'appel) et comment elle rejoint les retours : non tracé **[O]**.

Ce qu'il faudrait : (1) suivre le chemin de sortie dans `0x1c18a6` jusqu'aux retours (ou appeler le pilote et le master
dans le même bloc, comme `0x1c2b24`) ; (2) relier les 19 mots `0x2524cc`… aux 23 boutons (sonde des mots de trame
`0x6da + 2(k − 25)` jusqu'à leurs flottants, comme `probe_24e9.py` pour la voix) ; (3) identifier les trois tableaux
d'envoi ; (4) poser le tempo. Une seconde de son (1 500 blocs) coûte environ une heure : une reverb à décroissance longue demande des
cas courts ou plusieurs processus en parallèle. Le harnais de la voix (`dt2_voice_oracle.py`) ne sert pas directement :
il appelle `0x1c642a`, pas le pilote des envois.
