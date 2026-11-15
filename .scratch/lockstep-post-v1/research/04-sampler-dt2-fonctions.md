# 04. Le moteur sampler du Digitakt II, face au Sampler de Lockstep

Recherche du 2026-11-15, ticket [04-sampler-dt2-fonctions](../issues/04-sampler-dt2-fonctions.md). Rien n'a été
construit ni exécuté. Aucun nouveau désassemblage : le manuel, les notes de version, les recherches 12 et 23, le code de
Lockstep en lecture.

Marques : **[M]** dit par le manuel d'Elektron (OS 1.17) ; **[N]** dit par les notes de version d'Elektron ; **[D]** dit
par les recherches 12 ou 23 (leurs propres marques [V]/[D] reprises quand elles comptent) ; **[C]** lu ici dans le code de
Lockstep ; **[INFER]** déduit ici ; **[O]** inconnu.

## Sources

- Manuel : *Digitakt II User Manual*, OS 1.17, « last updated September 24, 2026 » (p. 2),
  https://www.elektron.se/wp-content/uploads/2026/10/Digitakt-2-User-Manual_ENG_OS1.17_260930.pdf, lu le 2026-11-15
  (lien trouvé sur https://www.elektron.se/en/support-downloads/digitakt-ii). Pages citées : 15 (architecture), 23 et 68 (enregistrement), 29
  (formats, +Drive), 38-39 (SOUND SETUP), 53-58 (pages TRIG, SRC, FLTR, AMP, FX), 73 (Transfer), 93-101 (machines SRC),
  109 et suivantes (MIDI, destinations des LFO). L'édition OS 1.01 du 2024-04-24 (copie
  https://www.analoguehaven.com/elektron/digitakt-ii/manual.pdf) a servi de comparaison : elle n'a pas la machine SLICE.
- Notes de version : https://www.elektron.se/en/digitakt-ii-os-release-notes, lues le 2026-11-15 (page « last updated
  Oct 1, 2026 », sans dates par version).
- Recherches existantes : `../../elektron-drums/research/12-digitakt-ii.md` (2026-10-24) et
  `../../elektron-drums/research/23-cartographie-dt2.md` (2026-10-24, image DSP 1.16 = 1.17) ; tickets
  `../../elektron-drums/issues/05-layering.md`, `12-digitakt-ii.md`, `23-cartographie-dt2.md` ; spec v1
  `../../elektron-drums/spec-v1.md` (section 8 et choix de machine).
- Code de Lockstep, lu le 2026-11-15 sur la branche `research/elektron-engines` : `source/elektron/edDrums/edDrumsEngine.h`
  et `.cpp`, `edDrumsSamples.h`, `edDrumsVintage.h` et `.cpp`, `edDrumsChain.h`, `edDrumsLfo.h`,
  `source/elektron/md/mdVoice/mdVoiceRom.cpp` (en-tête), `source/elektron/edDrumsPlugin/edDrumsSampleFiles.*`,
  `edDrumsSampleLibrary.h`, `edDrumsLockerImport.h`, `edDrumsParameters.cpp`.

## Réponse courte

Le DT2 apporte surtout ce que Lockstep n'a pas : la stéréo, les boucles (FORWARD LOOP, REVERSE LOOP avec point de
boucle), le calage au tempo (WERP, STRETCH, REPITCH par BARS), le découpage (GRID, SLICE et son éditeur), un TUNE de
±5 octaves, le sample verrouillable par pas, et un lecteur sans limite de longueur pratique (400 Mo de RAM par projet).
Ce qui recouvre l'existant est la chaîne derrière la voix : BR, overdrive (déjà porté en drive), SRR pré/post,
base-width, AMP AHD/ADSR, LFO. Lockstep a ses équivalents dans sa chaîne de piste ; le port DT2 n'a donc à apporter que
la voix SRC, pas une deuxième chaîne. Ce qui s'y oppose : la mono de Lockstep (moyenne des canaux, layer décidé mono),
sa fréquence de 44,1 kHz contre les 48 kHz natifs (96 kHz interne) du DT2, l'unique sample par piste (pas de sample
lock), le GRAIN codé sur deux moitiés de Machine 9 (un troisième grain doit garder les valeurs stockées), et des
sémantiques de STRT/LEN différentes de STRT/END.

## 1. Ce que fait le moteur sampler du DT2

### 1.1 Architecture et format

- 16 voix audio stéréo, une par piste audio ; chaîne de la voix : SRC, BR, overdrive, SRR, filtre machine, base-width,
  enveloppe de filtre, AMP (enveloppe, pan), vers le mixer et les envois chorus, delay, reverb [M p. 15]. Côté DSP, une
  piste stéréo prend deux voix (L paire, R impaire) sur 32 fiches [D 12, 4.3].
- Un sample est « un fichier 16 bits, 48 kHz, mono ou stéréo » [M p. 29]. Transfer convertit tout fichier en 16 bits
  48 kHz, mono et stéréo acceptés [M p. 73]. Les formats d'entrée que Transfer accepte ne sont pas listés dans le manuel
  **[O]**.
- Capacité : +Drive de 20 Go ; jusqu'à 400 Mo de samples chargés par projet (environ 72 min en mono, 36 en stéréo),
  1016 emplacements de sample par projet [M p. 29, 94]. Aucune longueur maximale par sample n'est donnée : la borne
  pratique est la RAM du projet [INFER]. L'enregistrement s'arrête à 66 s [M p. 23, 68].
- Rendu DSP : chaque appel de voix rend 64 échantillons à 96 kHz par interpolation polyphasée à 6 prises, puis
  décimation 2:1 vers 48 kHz ; pas de lecture en Q31 calculé sur le DSP par quatre setters (`powf(2, …/12)`) [D 12 4.3,
  23 2.1]. Le DT2 ne lit donc qu'à 48 kHz ; un sample à une autre fréquence a été converti avant (Transfer) [INFER].

### 1.2 Les machines SRC (OS 1.17)

Paramètres communs à toutes les machines sample : TUNE (bipolaire, ±5 octaves, crans d'octave ou de demi-ton), PLAY
(mode de lecture), SAMP (choix du sample parmi les 1016, OFF muet, verrouillable par pas : « sample locks »), LEV
(niveau du sample) [M p. 93-98].

Modes de PLAY, les mêmes pour toutes : FORWARD (une fois), REVERSE (une fois à l'envers), FORWARD LOOP (de STRT, puis en
boucle entre LOOP et la fin STRT + LEN), REVERSE LOOP (part de la fin, recule jusqu'à LOOP et boucle entre les deux). La
durée d'une boucle vient du LEN de la page TRIG (longueur de la note), et l'enveloppe AMP (HOLD, DEC) la borne aussi
[M p. 94].

| Machine (rôle DSP) | Paramètres propres | Ce qu'elle fait |
|---|---|---|
| ONESHOT (0) | STRT, LEN, LOOP | lecture linéaire, la machine par défaut. LEN est une longueur depuis STRT, pas une fin [M p. 94] |
| WERP (1) | SEG, MODE, BARS | cale le sample au tempo en le coupant en segments joués à la suite ; SEG règle leur taille (coupes au passage par zéro le plus proche) ; MODE joue chaque segment FORWARD, REVERSE, FORWARD LOOP ou REVERSE LOOP ; BARS donne la durée du sample en mesures au BPM [M p. 95-96] |
| STRETCH (2) | STRT, LEN, BARS | cale au tempo en hachant le son en grains minuscules et en fondant de l'un à l'autre ; pas de taille de grain ni de fondu exposés à l'utilisateur [M p. 96]. Routine DSP à part, `0x1c5576` [D 23] |
| REPITCH (3) | STRT, LEN, BARS | cale au tempo en changeant la vitesse, donc la hauteur ; pas de TUNE sur sa page [M p. 96-97] |
| SLICE (6, ajoutée en OS 1.15 [N]) | SLICE, LEN | tranches libres : éditeur de début, fin et point de boucle par tranche, liées ou non à leurs voisines, ajout et retrait, grille de 2 à 64 tranches avec détection de transitoires optionnelle ; SLICE = tranche jouée ou NOTE (tranches depuis C1) ; LEN = nombre de tranches jouées à la suite ; les points sont gardés avec le preset [M p. 97-100] |
| GRID (4) | SLICE, LEN, GRID | GRID tranches égales sur tout le sample (coupes au passage par zéro) ; SLICE et LEN comme SLICE. Nommée SLICE jusqu'à l'OS 1.0x ; GRID est verrouillable par pas, ce que le nombre de tranches de SLICE n'est pas [M p. 100-101] |
| MIDI (5) | — | piste MIDI, hors sujet |

SLICE et GRID peuvent distribuer leurs tranches sur les trigs posés, en ordre ou au hasard (CREATE LINEAR / RANDOM
LOCKS) [M p. 99]. Le menu de machines de la ColdFire en a sept, ramenées à six rôles DSP par la table `0x2567c0` ; ONESHOT,
WERP, REPITCH, GRID et SLICE partagent le rendu `0x1c4ecf`, STRETCH a le sien [D 12 4.3, 23 2.1]. Que SLICE soit le
sélecteur 6 (« MANUAL SLICE » chez digikit) est une lecture de digikit sur 1.17 [D].

### 1.3 Ce qui suit la voix (pages FX, FLTR 2, AMP)

- **BR** : « Bit Reduction », de 16 bits à 1 bit [M p. 58]. Sur le DSP, juste après le rendu de la voix (`0x1cdd8b` →
  `0x1cddff`, table de 128 entrées, quantification par puissance de deux), contourné à 0 [D 23 0 et 1.1]. Elle n'est pas
  dans la voix elle-même mais dans la chaîne de piste, avant tout le reste [D 23 1.1].
- **OVER** (overdrive numérique), **SRR** et leurs routages pré ou post filtre (ROUT pour SRR, l'overdrive depuis
  l'OS 1.10), **BW.RT** (base-width pré ou post) [M p. 55-58, N 1.10]. Le même saturateur sert au master [D 23 2.4].
- **AMP** : ATK, HOLD (0-126 ou NOTE), DEC, SUS, REL, RSET, MODE AHD ou ADSR, PAN, VOL (verrouillable, à la
  différence du LEVEL de piste) [M p. 56-57]. Enveloppe sur le DSP, une par voix (`0x1cbb57`) [D 23 2.6].
- Retrigs du séquenceur (page TRIG 2 : RTRG, RATE de 1/1 à 1/80, VFAD, LEN) [M p. 54] : fonction du séquenceur, pas du
  moteur.

### 1.4 Ce que la voix reçoit de la ColdFire

- Trois LFO par piste audio, sur la ColdFire (tick `0x40139342`) [D 23 2.6]. Leurs destinations couvrent tout le SRC
  (Tune, Play Mode, Sample Slot, les trois knobs E-G propres à la machine, Level), le filtre et son enveloppe, la
  base-width, l'AMP, les envois, BR, SRR, son routage et l'overdrive ; LFO2 et 3 peuvent moduler les LFO précédents
  [M annexe C].
- Autres sources de modulation par preset : VELOCITY MOD, KEY TRACKING (quatre destinations chacune, en décalage),
  aftertouch, pitch bend, molette, souffle ; courbe VELOCITY TO VOL (OFF, LOG, LIN, EXP, EXP 2) [M p. 38-39].
- Ce que la ColdFire fait d'une valeur : kit, puis LFO et matrice de modulation, puis lissage à un pôle (`0x400d92a2`,
  0,97/0,03 en Q15 [INFER de 23]), puis paquetage en mots s16 dans la trame ; le DSP rampe chaque mot de (nouveau -
  ancien)/32 par échantillon dans le bloc, et ses setters convertissent les mots en grandeurs (pas de lecture,
  coefficients) [D 12 4.3, 23 3]. La voix reçoit donc des mots lissés, jamais un pas ou un coefficient tout fait [D 23].
- Ce que la ColdFire calcule pour WERP, STRETCH, REPITCH (tempo, BARS, longueur en échantillons vers un pas) et pour
  SLICE (points des tranches) n'est pas localisé **[O]**.

## 2. Ce que Lockstep a déjà

### 2.1 La machine Sampler

- Une entrée « Sampler » de la famille Machinedrum (id 192, hors des ids du Machinedrum), à la place de ROM-33 à -48 ;
  la piste t joue son sample à l'entrée ROM-33 + t de la banque [C `edDrumsEngine.h`]. SYN1-8 sont ceux du lecteur ROM
  du Machinedrum : PTCH, DEC, HOLD, BRR, STRT, END, RTRG, RTIM [C].
- **GRAIN** (Machine 9, valeur Kit, sans lock) : sous 64, « 12-BIT », le lecteur ROM porté de l'OS 1.63 ; à partir de
  64, « VINTAGE » [C `edDrumsVintage.h`, `edDrumsParameters.cpp`].
  - 12-BIT : le sample converti en codes 12 bits A-law, deux par mot, au plus 57 088 échantillons (1,29 s à 44,1 kHz),
    sans boucle (`0xffffff` dans l'entrée de répertoire) ; le lecteur sait lire à l'envers, boucler et redéclencher
    (RTRG, RTIM), interpole à 4x, filtre, décime, puis BRR et l'enveloppe HOLD/DEC [C `edDrumsSamples.h`,
    `mdVoiceRom.cpp`, `edDrumsEngine.cpp`].
  - VINTAGE : modèle maison d'un 8 bits des années 1980 ; lit les valeurs 16 bits de STRT à END, jusqu'à 480 000
    échantillons (10 s à 48 kHz), à une horloge que règle PTCH (±24 demi-tons, 3/8 de demi-ton par valeur), sans
    interpolation (échantillon tenu), par un DAC de 6 à 12 bits (BITS, Machine 10), linéaire ou à segments (COMPANDING,
    Machine 11), puis un passe-bas de 100 Hz à 20 kHz (OUTPUT FILTER, Machine 12), puis HOLD (jusqu'à 2 s) et DEC (60 dB
    en 20 ms à 10 s, aucun à 127). En avant seulement : END sous STRT ne joue rien ; ni boucle ni répétition. Rendu à
    44,1 kHz [C `edDrumsVintage.*`, `edDrumsEngine.cpp` l. 1310-1320].
- **REPEAT GAP** : la répétition du grain 12-BIT (RTRG/RTIM) en valeurs de note au tempo de l'hôte quand Sync est mis, sa
  loi propre sinon ; VINTAGE ne répète pas [C `edDrumsEngine.h` `setRepeatNote`, `edDrumsParameters.h`].
- Un seul sample par piste (`Engine::setSample`) ; les LFO peuvent viser SYN1-8 et Machine 10-12 du VINTAGE, rien ne
  verrouille Machine 9-12 [C `edDrumsEngine.h`, `edDrumsLfo.h`].
- Moteur de la famille Machinedrum à 44,1 kHz ; deux LFO par piste [C `edDrumsEngine.h`, CLAUDE.md].
- SP TWINSHOT du Syntakt est un autre lecteur, qui joue le jeu de samples que lui donne la Library, pas ceux du Locker
  [C `edDrumsEngine.cpp` l. 186, 303-306].

### 2.2 La chaîne de piste

`machine > drives pré > BAND (avant) > FILTER > BAND (après) > drives post > AMP` [C `edDrumsChain.h` l. 16]. Drives :
Syntakt, MD DIST, MD SRR, DAC (le DAC du VINTAGE), Overdrive (celui du DT2, le saturateur du master seul, commit
`2e2486e6`) [C]. BAND a un type BASE/WIDTH. AMP : attaque, maintien, déclin, soutien, relâche [C].

### 2.3 Le Locker et ses formats

- Lecture : WAV, AIFF, FLAC (formats de base de JUCE), jusqu'à 8 canaux et 384 kHz ; les canaux sont moyennés en mono ;
  de 4 à 48 kHz la fréquence est gardée, au-dessus le sample passe à 44,1 kHz par interpolation de Lagrange, qui ne
  filtre rien (ce qui dépasse 22 kHz replie) ; coupé à 480 000 échantillons [C `edDrumsSampleFiles.cpp`,
  `edDrumsSamples.h`].
- Le Locker : sources LIÉES (dossier de l'utilisateur lu sur place) ou IMPORTÉES (copiées sous `Locker/Imported`), scan
  sur un fil à part (20 000 fichiers au plus), empreinte du contenu (taille et 64 premiers Ko), type deviné par le nom
  puis le son, corrigeable, noms et types de l'utilisateur gardés par empreinte (`names.txt`, `types.txt`) ; l'import
  lit aussi une flash image (mots de la banque UW) et les Kits des SysEx ; un import copie au plus 160 Mo par fichier et
  4 Go en tout [C `edDrumsSampleLibrary.h`, `edDrumsLockerImport.h`]. Aucune métadonnée par sample au-delà du nom et du
  type : ni boucle, ni tranches, ni tempo [C].

## 3. Le tableau

Colonne « Verdict » : **apporte** (le DT2 a, Lockstep n'a pas), **recouvre** (les deux ont, un seul suffit), **s'oppose**
(les deux ont, de façon incompatible, une décision est à prendre).

| Fonction | Digitakt II | Lockstep aujourd'hui | Verdict |
|---|---|---|---|
| Canaux | mono ou stéréo, 16 voix stéréo [M] | mono, canaux moyennés ; layer décidé mono (ticket 05) [C] | **s'oppose** : garder la mono (coût, chaîne mono) ou ouvrir une piste stéréo (deux voix, chaîne doublée) [INFER] |
| Fréquence | 48 kHz seulement, rendu interne 96 kHz puis décimation [M, D] | famille MD à 44,1 kHz, samples gardés de 4 à 48 kHz [C] | **s'oppose** : ou le port rend à 48 kHz et passe par un rééchantillonneur comme `fast::Host` du Syntakt (24 échantillons de latence, réglé contre `HitDelay`), ou le port lit à la fréquence du sample vers 44,1 kHz, et il quitte alors le pas Q31 du DSP [INFER] |
| Résolution | 16 bits [M] | 16 bits gardés ; 12-BIT les recode en A-law 12 bits [C] | recouvre |
| Formats d'entrée | ce que Transfer convertit [M], liste non donnée [O] | WAV, AIFF, FLAC, 8 canaux, 384 kHz [C] | recouvre ; le Locker en lit déjà assez |
| Longueur | pas de borne par sample, 400 Mo par projet [M] | 1,29 s (12-BIT) ou 10 s (VINTAGE) [C] | **apporte** : un lecteur long ; la mémoire d'un sample de plusieurs minutes est à borner pour un plug-in à 16 instances [INFER] |
| Accord | TUNE ±5 octaves [M] | PTCH ±24 demi-tons (VINTAGE), loi du lecteur ROM (12-BIT) [C] | apporte (plage) ; le suivi de note est déjà décidé pour le layer (ticket 05) |
| Début, fin | STRT + LEN (longueur depuis le début) [M] | STRT, END (fin absolue) [C] | **s'oppose** pour l'interface : deux sémantiques sur la même entrée Sampler ; une page par grain, ou une seule paire convertie [INFER] |
| Lecture à l'envers | REVERSE, REVERSE LOOP [M] | 12-BIT oui (lecteur ROM) ; VINTAGE non [C] | apporte pour un sample long |
| Boucles | FORWARD LOOP, REVERSE LOOP, point LOOP, durée par la longueur de note [M] | aucune pour les samples de l'utilisateur [C] | **apporte** |
| Calage au tempo | WERP (SEG, MODE), STRETCH (grains fondus), REPITCH, par BARS [M] | rien ; le tempo de l'hôte est connu du moteur (REPEAT GAP, LFO) [C] | **apporte** ; STRETCH est une routine à part (`0x1c5576`) à porter en plus [D] |
| Tranches | GRID (égales, verrouillable), SLICE (éditeur, 64 tranches, transitoires) [M] | rien ; le Locker ne garde aucun point [C] | **apporte** ; les points de SLICE demandent un champ par sample, par empreinte dans le Locker ou dans le Kit [INFER] |
| Sample par pas | SAMP verrouillable, 1016 emplacements [M] | un sample par piste, Machine 9-12 sans lock [C] | **s'oppose** : le sample lock veut plusieurs samples chargés par piste et un changement de sample au trig sans allocation ; question ouverte en v1 (« combien de samples une piste lit », spec v1) [C, INFER] |
| Répétition | retrigs du séquenceur (RTRG, RATE, VFAD) [M] | REPEAT GAP du grain 12-BIT, en notes [C] | recouvre en partie : chez le DT2 c'est le séquenceur, chez Lockstep la voix ; pas d'équivalent pour un grain DT2 [INFER] |
| Réduction de bits | BR 16 à 1 bit, dans la chaîne après la voix [M, D] | BRR du lecteur ROM ; BITS du VINTAGE (6-12) ; drive DAC (6-12) [C] | **recouvre** : un étage de plus, qui descend plus bas ; comme type de drive plutôt que dans la voix [INFER] |
| Overdrive | OVER pré/post [M] | drive Overdrive (déjà porté, master seul) [C] | recouvre, déjà fait |
| SRR | SRR, ROUT pré/post [M] | drive MD SRR, pré ou post [C] | recouvre |
| Base-width | BASE, WIDTH, BW.RT pré/post [M] | BAND de type BASE/WIDTH, avant ou après [C] | recouvre |
| Enveloppe d'amplitude | AHD ou ADSR, HOLD = NOTE, sur le DSP [M, D] | AMP de la chaîne (ADSR et maintien) ; HOLD/DEC des grains [C] | recouvre : le port n'a pas à reprendre `0x1cbb57` si l'AMP de la chaîne fait le travail ; à juger à l'oreille [INFER] |
| LFO | 3 par piste, sur la ColdFire, cibles SRC comprises (Sample Slot, Play Mode) [M, D] | 2 par piste, SYN1-8 et Machine 10-12 [C] | recouvre ; moduler le choix du sample ou le mode s'oppose comme le sample lock |
| Lissage | un pôle sur la ColdFire, rampe par échantillon sur le DSP [D] | lissage des mots du lecteur ROM ; VINTAGE lit ses mots au trig et au bloc [C] | à reprendre avec la voix (petit : trois étapes connues) [D 23 3] |
| Nom du grain | — | GRAIN binaire : 12-BIT sous 64, VINTAGE dès 64 ; la spec v1 prévoit un grain « Digitakt II » [C, spec v1] | **s'oppose** : un troisième grain doit garder les valeurs des sets existants (0-63 et 64-127 déjà pris) ; un nouveau slot ou une nouvelle entrée de machine plutôt qu'un remappage [INFER] |

## Inconnues

- La liste des formats que Transfer accepte, et ce qu'il fait d'un fichier à une autre fréquence (rééchantillonneur).
- La longueur des grains et des fondus de STRETCH, la taille réelle des segments de WERP par valeur de SEG : non exposées
  par le manuel, à lire dans `0x1c5576` et les setters si le port en a besoin.
- Où se calcule le pas de WERP, STRETCH et REPITCH à partir de BARS et du tempo (ColdFire ou setter DSP), et le format des
  points de SLICE dans la trame.
- Le coût mesuré d'une voix (environ 4 000 instructions SHARC par bloc en statique, recherche 23) et celui de STRETCH.
