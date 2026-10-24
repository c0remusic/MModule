# 07 — Séquenceurs des machines Elektron : ce qu'un séquenceur interne doit reproduire

Recherche du ticket [07](../issues/07-sequenceurs-elektron.md), 2026-10-24. Lecture seule. Marques : **[V]** lu dans le
code ou un document à l'endroit nommé, **[M]** mesuré (par ce dépôt, sur l'oracle), **[I]** déduit.

Sources :
- **Machinedrum OS 1.63** : `C:\dev\MModule\.scratch\slide\research-01-os-slide.md` (lecture statique du gestionnaire
  d'horloge du séquenceur, OS $239762, et mesures `mdSlideFirmwareTest`), `.scratch/md-editor/README.md` (format des
  patterns, chaînage mesuré par `patternChainFirmwareTest`), le codec `source/elektron/md/mdProtocol/mdsysexautomation.cpp:319-403`
  (`parseMdPatternDump`), `mdLib/mdlivepattern.h:23-44` (patterns en RAM), ADR 0003 (accent, slide), le manuel OS 1.63
  (`Downloads/Audio/Elektron/machinedrum_manual_OS1.63_1.pdf`, texte extrait).
- **Syntakt OS 1.41, section 3 (CPU #1)** : `C:\dev\elektron-work\syntakt-1.41\section_3_MAIN_OS.bin` (chargée à
  0x40000400), désassemblée par `tools/cfdis.py` ; désassemblages et scripts dans
  `C:\dev\elektron-work\scratch-arch\research\seq07\` (`tick.s`, `f_*.s`, `peek.py`, `findc.py`). Point d'entrée trouvé
  grâce au mod beat-repeat de SyntHack (`C:\dev\Synthack-OS\mods\beat-repeat\br.c:3-5` : tick du séquenceur
  0x4008C924, pas courant 0x43BD73FC + t). La boucle d'événements de l'interruption audio : `cpu1.md` §1, `cpu1/irq.s`.
- **Manuels publics** : *Syntakt User Manual* OS 1.42 (elektron.se, `Syntakt-User-Manual_ENG_OS1.42_260930.pdf`),
  *Digitakt II User Manual* OS 1.17 (`Digitakt-2-User-Manual_ENG_OS1.17_260930.pdf`), *Digitone II User Manual*
  (`scratch-arch/ext/manual/dn2.txt`, même génération de séquenceur). Texte extrait par pdftotext dans `seq07/`.
  Le manuel Syntakt est d'OS 1.42 et le code d'OS 1.41.

## En bref

- **Le Machinedrum est un séquenceur à 24 ppqn sans conditions, sans micro-timing, sans retrigs.** Un pattern porte
  par piste un masque de 64 trigs, 64 lignes de locks partagées (une par paramètre locké), des rangées accent, slide et
  swing (communes ou par piste), une quantité d'accent, une longueur 2-64, une échelle 1x/2x/3/4x/3/2x, un kit. Le
  format n'a aucun champ pour le reste [V : `parseMdPatternDump`]. Tout est lisible dans le code et déjà mesuré en
  partie dans ce dépôt (slide, accent, chaînage).
- **Le Syntakt est le séquenceur Elektron moderne, lisible dans le code du CPU #1** : un tick à 24 ppqn (0x4008C924)
  évalue chaque pas en avance (0x4008B882 : conditions, probabilité de piste, micro-timing, swing, quantize, locks,
  sound lock, retrig, note, vélocité, longueur) et poste un événement horodaté dans une file ; l'interruption audio le
  consomme **au bloc de 32 échantillons où il tombe** (cpu1.md §1.1). Les conditions (0x401181D4) et les retrigs
  (0x400A19C2, 0x400A1AA6) sont du code isolé, portable comme les étages déjà portés.
- **Le Digitakt II n'est lisible que dans son manuel** (pas de firmware étudié ici) : même famille que le Syntakt, avec
  128 pas, FILL en paramètre séparé, 80 paramètres lockables par pattern.

## 1. Machinedrum OS 1.63 (oracle : `mdLib`, le firmware entier)

### Horloge et temps [V sauf mention]
- Le séquenceur tourne sur le timer 2 du ColdFire en one-shot ; son gestionnaire OS $23C83E appelle OS $239762, une
  fonction de $239762 à $23C6D2 qui distribue les trigs, calcule le swing, envoie l'horloge MIDI, met en file les
  changements de pattern et de kit (research-01-os-slide.md §1).
- Trois comptes à rebours : le pas, le pas swingué, l'horloge 24 ppqn. Une horloge = 30 000 000 / (BPM × 24) unités
  de timer. Un pas = 6, 3, 8 ou 4 horloges selon l'échelle (table OS $247E4C : 1x, 2x, 3/4x, 3/2x [I pour les noms]).
- Swing : le pas de la rangée swing (par défaut un pas sur deux, à partir du deuxième, manuel p. 11) est retardé de
  pas × mot de swing / 16384 ; le mot vaut la part au-delà de 50 %, 0 à 9830 pour 50-80 % (README md-editor §6, MCL).
  Une horloge n'est pas un multiple de 1 ms : le décalage est en unités de timer, pas en horloges.
- Esclave d'une horloge MIDI : le pas suit les horloges reçues ; SONG POSITION est pris modulo la longueur du pattern
  [M, README md-editor §7].

### Pattern [V : codec, `mdlivepattern.h`]
- 16 pistes, longueur 2-64 (manuel p. 38 : total 16/32/48/64, longueur jusqu'à elle), échelle et « double tempo »
  par pattern (pas par piste), kit associé (EXTENDED : le kit se charge avec le pattern, manuel p. 37).
- Trigs : un masque de 64 bits par piste. Pas de note, pas de vélocité par pas (les machines MD n'ont pas de note ; la
  vélocité vient de l'accent).
- Locks : 64 lignes par pattern ; chaque paramètre locké d'une piste (24 par piste, masque par piste) prend la ligne
  suivante, valeurs 0-127 par pas, ≥ 128 = pas de lock. Les locks sont mis en scène **un pas en avance** et appliqués au
  trig par le tick de contrôle (OS $20B078-$20B1D0) ; le trig suivant remet la valeur du kit.
- Accent : rangée commune ou par piste (EDIT ALL), quantité 0-127 par pattern ; un pas accentué joue à vélocité
  128 + 2 × quantité (ADR 0003, OS $20CF8C) [V, M : crête × 3 à 127].
- Slide : rangée commune ou par piste ; rampe entière de la valeur lockée au lock (ou à la valeur du kit) du trig
  suivant de la piste, un pas d'incrément par horloge 24 ppqn, incrément = (fin − début) × 128 / horloges tronqué,
  correction de swing comprise ; figée à l'arrêt [V, M : `mdSlideFirmwareTest`]. Déjà porté dans MoDule
  (`HostModel::slide`).
- Rien d'autre par pas : ni condition, ni probabilité, ni micro-timing, ni retrig, ni longueur de note.

### Hors pattern [V sauf mention]
- Chaînage : le MD s'engage sur le pattern suivant **7 horloges avant la fin** du pattern courant, à tout tempo et
  toute longueur ; demandé après, il attend un tour [M, `patternChainFirmwareTest`]. Le manuel ne le dit pas.
- Song mode (manuel p. 51-54), mutes, TRIG GROUPS/liens (Links, Chokes : déjà dans MoDule, `$65`/`$66`).
- MoDule ne fait pas tourner ce séquenceur (`mdEngine` sans l'OS) : MoDule Seq (`C:\dev\md-drums-seq`) le refait côté
  Live (swing du MD, accent, slide), avec en plus des Conditions, du micro-timing en 24es et des ratchets 1-8, qui
  n'ont pas d'équivalent sur le MD (son `CLAUDE.md`).

## 2. Syntakt OS 1.41, CPU #1 (section 3)

### Base de temps [V]
- Le tick 0x4008C924 est une interruption du séquenceur. Chaque tick avance l'horloge 0x43BCB3A4 de **900 000
  unités** (0xDBBA0) : une horloge 24 ppqn. Dans l'interruption audio, chaque bloc avance de 2 × (BPM × 120) unités
  (`0x800046EC` + 2 × long 0x402D7010, irq.s 400a3944-400a3966), d'où 360 000 × BPM unités par seconde et une horloge
  de 2,5 / BPM s.
- Horloges par pas : table 0x401DA9B8 = 3, 4, 6, 8, 12, 24, 48 pour les index 0-6, soit 2x, 3/2x, 1x, 3/4x, 1/2x,
  1/4x, 1/8x [I pour l'ordre des noms, déduit des valeurs]. Octet du pattern +0x21379 = mode d'échelle (1 = par piste,
  l'index alors à +972 de la piste) ; +0x2137A = index du pattern ; longueur du pattern mot +0x21374 ; longueur de
  piste mot +969.
- Mémoire du pattern [V pour les offsets, I pour les noms marqués] : 13 pistes de 1 177 octets (0x499) à partir du
  pointeur 0x43BD7430 ; **64 pas** par piste. Par pas (index s) :

| Offset piste | Contenu | Défaut (−1 → réglage de piste) |
|---|---|---|
| +0 + 2s | mot de drapeaux : bit 0 trig, bit 2 [I : trig sans note/muet], bit 4 swing, bits 7-10 SYN/?/FLT/LFO trig, bits 11-14 « bit correspondant forcé » | bits 7-10 depuis le mot +0x3C2 |
| +128 + s | vélocité → événement +0x14 (<< 8) | +960 |
| +192 + s | longueur (index de la table 0x401E930C) → +0x30 | +961 |
| +256 + s | micro-timing, signé | — |
| +320, +384, +448 + s | → événement +0x38, +0x34, +0x3C [I : retrig taux, longueur, courbe de vélocité] | — |
| +512 + s | condition (0-99, −1 aucune) | probabilité de piste +973 |
| +576 + s | sound lock (index dans le pool, −1 aucun) | — |
| +704 + s | note → +0x18 | +965 |
| +984 + s | pas réel en mode euclidien (+0x3D1 = 1) [I] | — |

### Évaluation d'un pas : 0x4008B882 (334 instructions) [V]
Appelée par le tick pour chaque piste dont le pas arrive (compteur d'horloges par piste 0x43BD7409 contre la table),
avec (piste, pattern, kit, pas, &décalage, 0). Une variante 0x4008BCEC sert aux pistes dont le bit est mis dans le mot
+5456 du bloc 0x43BD743C [I : les pistes MIDI, que le manuel décrit p. 24]. Dans l'ordre :
1. Pas sans trig (bit 0) ni lock trig : rien.
2. **Condition** (+512 + s). Sans condition : probabilité de la piste (+973, le PROB de TRIG PAGE 1, 0-100 %) par
   0x4011818C : joue si p > 99, sinon si p × 0x28F5C28 (≈ 2³²/100) > un tirage d'un générateur additif (graines
   0x402D6FE0 / 0x402D6FE4). Avec condition : 0x401181D4 (ci-dessous) ; la condition remplace PROB (manuel Syntakt
   §10.2.4).
3. Drapeaux : SYN/FLT/LFO trig du pas, ou ceux de la piste (+0x3C2) quand le bit « forcé » est clair.
4. Sound lock (+576) : pointeur vers le son du pool, ou le son du kit.
5. **Micro-timing et swing** : décalage = (μ × horloges/pas × 25 × Q) >> 16, Q = (127 − max(quantize de piste +971,
   quantize global +0x2137B)) × 0x10205 >> 16 << 9 (Q = 65 536 à quantize 0), puis + swing × horloges/pas × 12 si le
   bit 4 du pas est mis (swing = octet +0x21378, ou celui de la rangée de song, 0x4008AFB0), le tout × 1 500 unités.
   À 1x, un cran de μ = 225 000 unités = **1/24 de pas** ; le swing s = 0-30 (51-80 %) retarde de 2s % du pas, la même
   loi que le MD. Le quantize (0-127, manuel §9.7) **réduit le micro-timing en proportion, pas le swing**.
6. Événement (file 0x4011A4C2 / 0x4011A648) : +0x8 et +0x48 piste, +0x14 vélocité, +0x18 note, +0x24 drapeaux,
   +0x28 son, +0x30 longueur, +0x34/+0x38/+0x3C retrig, +0x44 la liste des locks (0x4008B830, table de locks du pattern
   à +0x3C53 par pas), +0x4C suivant. Le tick le poste à l'instant de l'horloge courante + (horloges restantes − 1) ×
   900 000 + décalage (tick.s 4008cf2c-4008cfc4) : **le pas est évalué en avance** et la file le garde jusqu'à son heure.

### Conditions : 0x401181D4 (213 instructions) [V pour le code, I pour les noms de 21 et 30/31]
Retourne 1 = le trig ne joue pas. État : compteur de boucles **par piste** 0x444DA318 + 4t (A:B compte la piste quand
elle est plus courte, manuel Syntakt §9.10.3), dernier résultat 0x444DA2EE + t (PRE, NEI).

| Code | Condition | Code lu |
|---|---|---|
| 0-20 | 1 % à 99 % | tirage ≥ seuil 0x40245790[c] (1, 2, 4, 6, 9, 13, 19, 25, 33, 41, 50, 59, 67, 75, 81, 87, 91, 94, 96, 98, 99 %) |
| 21 | 100 % [I] | joue toujours |
| 22 / 23 | FILL / !FILL | octet 0x444DA315 (mode FILL) ; sinon 0x444DA308 + t |
| 24 / 25 | PRE / !PRE | dernier résultat de la piste, **non mémorisé** (manuel : PRE ignorés) |
| 26 / 27 | NEI / !NEI | dernier résultat de la piste t − 1 (t = 0 : elle-même) |
| 28 / 29 | 1ST / !1ST | compteur de boucles = 0 |
| 30 / 31 | LST / !LST [I] | octet global 0x444DA2E0 |
| 32, 33 | 1:2, 2:2 | (boucles ^ c) & 1 |
| 34-39, 40-47, 48-57, 58-69, 70-83, 84-99 | A:B et !A:B pour B = 3 à 8, alternés (c pair A:B, impair !A:B), A = (c − base) / 2 + 1 | boucles mod B (instruction `remu`, que cfdis affiche `divu`) |

Le résultat est mémorisé pour PRE/NEI sauf pour PRE/!PRE eux-mêmes. Le manuel Syntakt OS 1.42 donne la même liste,
LST compris (§9.10.3).

### Retrigs [V pour le code, I pour les champs]
- Une piste en retrig a un état de 28 octets à 0x43BDC34C + 28t (13 pistes). Un événement de type 6 se replanifie
  toutes les **90 000 unités** (0x15F90, 1/10 d'horloge = 1/240 de noire, 0x400A182A) ; à chaque passage le compteur
  +0x10 descend d'un cran sur un cycle de 960 (une ronde) et le trig rejoue si le bit du taux choisi (+0x18) est mis
  dans le masque 0x401E8704[compteur] : bit 15 tous les 12 crans (1/80), 14 tous les 15 (1/64), 13 tous les 20 (1/48)…
  Les 17 taux du manuel (1/1 à 1/80, §9.6) tombent tous sur cette grille de 960 par ronde.
- Chaque bloc audio (0x400A1AA6, appelé au début de la boucle d'événements) : la longueur restante +4 descend de
  BPM × 120, la courbe de vélocité +8 avance de BPM × 120 × pente / 4, saturée ; au bout, 0x400A1808 arrête le retrig.
  Le manuel : LEN 0,125 à INF pas, VEL −128 à 127 (§9.6).

### Livraison au son [V : cpu1.md §1, §1.1]
- La boucle d'événements de l'interruption audio (0x400A3B54-0x400A475E) prend les événements dont l'heure est passée
  à la fin du bloc et en fait des masques de pistes (trigs, gate-offs, LFO trigs, filter trigs) : **un trig sonne au
  début du bloc de 32 échantillons (0,67 ms à 48 kHz) qui contient son heure**, micro-timing et retrigs compris. La
  longueur de note (+0x30) arme le compteur 0x80003050[t], décompté de BPM × 120 par bloc, qui donne le gate-off.
- C'est la couture que le port `stMainBlock` prend déjà en entrée (`BlockMasks`, notes, vélocités, locks). Un
  séquenceur interne exact s'y branche en produisant ces événements.

### Ce qui reste du tick (non lu en détail) [V pour la présence, I pour le rôle]
Le reste de 0x4008C924 (4008d1d8-4008d6a2) bascule les pointeurs pattern courant / suivant / précédent
(0x43BD7430/34/38), gère la longueur maîtresse et le CHANGE (0x43BD73C0, 0x43BD7528), la table de fonctions
0x43BD7534 (pattern ou song) et les chaînes. Le moment exact où un pattern en file s'engage n'est pas lu.

## 3. Ce que documentent les manuels (Syntakt OS 1.42, Digitakt II OS 1.17) [V, manuels]

| Fonction | Syntakt | Digitakt II |
|---|---|---|
| Pas par pattern | 64 (4 pages) | 128 (8 pages) |
| Échelle | 1/8x à 2x, par pattern ou par piste (longueur et échelle par piste, CHANGE, RESET) §9.9 | idem, PAGE SETUP |
| Conditions | dans COND, qui remplace PROB sur la trig : %, FILL, !FILL, PRE, NEI, 1ST, LST, A:B, !A:B §9.10.3 | COND : %, PRE, NEI, 1ST, LST, A:B et négations ; **FILL est un paramètre séparé** de TRIG PAGE 1 (ON/OFF) |
| Probabilité de piste | PROB 0-100 %, re-tirée à chaque trig §10.2.4 | PROB, verrouillable par trig |
| Micro-timing | par trig, notes et pistes MIDI §9.5 (± en 24es de pas : code) | idem |
| Quantize | TRK et GLOBAL 0-127, en temps réel, sur le micro-timing §9.7 | TRACK et PATTERN |
| Retrigs | RETRIG, RATE 1/1-1/80 (17 taux, 1/10 compris), LEN 0,125-INF, VEL −128-127 ; pistes audio seulement §9.6 | RTRG, RATE (16 taux, sans 1/10), LEN, VFAD −64-64 |
| Locks | 72 paramètres lockables par pattern, lock trigs, sound locks, conditional locks | 80 paramètres, lock trigs, sample locks |
| Swing | 51-80 %, par pattern | idem |
| Trig page | NOTE, VEL, LEN (∞ possible), PROB/COND, FLT.T, LFO.T, PTIM, PORT | NOTE, VEL, LEN, PROB, LFO.T, FLT.T, FILL, COND |
| Euclidien | deux générateurs de pulsations par piste §9.4 | idem |
| Mutes | GLOBAL et PATTERN MUTE (13 pistes, FX comprise) §7.10 | idem |
| Song mode | 16 songs, 99 rangées : pattern, répétitions, longueur 2-1024, tempo, swing, mutes par rangée | idem |
| Chaînes | jusqu'à 64 patterns, non sauvées | idem |
| FILL | une boucle ([YES]+[PAGE]), tenu, ou verrouillé | idem |

Ni accent ni slide de paramètres sur le Syntakt et le Digitakt II (pas de rangée, pas de paramètre de trig) : la
vélocité par trig et la portamento (PTIM, PORT) en tiennent lieu. Le Syntakt a 13 pistes séquencées (12 + FX).

## 4. Lisible dans le code / seulement dans les manuels

| Comportement | Machinedrum | Syntakt | Digitakt II |
|---|---|---|---|
| Base de temps, échelles, swing | code (OS $239762, $247E4C) | code (0x4008C924, 0x401DA9B8, 0x4008B882) | manuel |
| Locks (mise en scène, valeur du kit au trig suivant) | code + mesure | code (0x4008B830, 0x400978B4) | manuel |
| Accent, slide | code + mesure | — | — |
| Conditions, probabilité | — | code (0x401181D4, 0x4011818C) | manuel |
| Micro-timing, quantize | — | code (0x4008B882) | manuel |
| Retrigs | — | code (0x400A19C2, 0x400A1AA6, 0x401E8704) | manuel |
| Quantification de la sortie | horloge 24 ppqn, timer | bloc de 32 échantillons | inconnue |
| Changement de pattern | mesure (7 horloges avant la fin) | code présent, non lu | manuel (CHANGE, RESET) |
| Song mode, chaînes | manuel | code présent, non lu | manuel |
| Euclidien | — | code présent (+0x3D1, +984), non lu | manuel |

Pour l'instrument : un séquenceur interne peut reprendre bit pour bit les conditions, le tirage, le micro-timing, le
quantize, le swing et les retrigs du Syntakt (code court, sans E/S, comme les étages déjà portés à l'oracle Unicorn),
et le slide et l'accent du MD (déjà portés dans MoDule). Les 128 pas, FILL séparé et la limite de 80 locks du
Digitakt II ne se reprennent que d'après le manuel [I].

## Inconnues

- Syntakt : le rôle exact des bits 2 et 8 du mot de pas, des octets +320/+384/+448 (lequel est taux, longueur,
  vélocité de retrig), du code 21 (100 % supposé), de 0x444DA2E0 (LST supposé) et de 0x444DA308 + t (FILL par piste ?).
  Où sont mis à jour le compteur de boucles 0x444DA318 et le drapeau LST. Ce que fait 0x4008BCEC (pistes MIDI
  supposées). Le moment où un pattern en file s'engage (l'équivalent des 7 horloges du MD), le song mode, l'euclidien.
- Syntakt : rien n'est mesuré (pas d'oracle du séquenceur ici) ; les formules ci-dessus sont lues, pas jouées.
- Digitakt II et Digitone II : aucun firmware lu ; leur résolution de sortie et l'ordre de leurs codes de condition
  sont inconnus.
- Machinedrum : la plage du mot de swing au-delà de 80 %, le song mode et le moment où un kit lié se charge, au-delà
  de ce que mesure `patternChainFirmwareTest`.
