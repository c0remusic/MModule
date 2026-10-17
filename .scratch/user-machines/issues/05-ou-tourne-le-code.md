# Où tourne le code d'une Machine utilisateur

Type: grilling
Status: resolved
Assignee: c0remusic (session du 2026-10-17)
Blocked by: 03, 04

## Question

Dans le DSP voix d'OS 1.63, à côté des Machines intégrées, ou sur un DSP56300 émulé à part dont la sortie rejoint la
chaîne de DSP1 en C++ ? Avec : le coût CPU de chaque voie, où sonne le Hit (`Engine::voiceStart`,
`Engine::AlignedStart`), ce que deviennent NEIGHBOUR et STEREO PAIR, le moteur de la prévisualisation du Hit, la
compilation à l'avance du JIT (`EngineT::warmUpVoices`), et une Machine utilisateur installée pendant que le moteur
joue.

## Answer

Choix de l'utilisateur (2026-10-17), sur [research/03](../research/03-first-sound.md) et
[research/04](../research/04-voice-dsp-memory.md) :

- **Dans le DSP voix, à la manière d'X.20** : les tables d'appel copiées à 211 entrées, USR-01 à USR-16 aux types 195
  à 210, le code des paquets dans `$020000-$0FFFFF` où P, X et Y partagent leur mémoire. Mesuré : ça tient en place et
  en temps, les Tracks intégrées restent identiques au bit près, aucune mémoire de plus. Pas de DSP à part.
  Corrigé le 2026-10-17 : NEIGHBOUR ne marche pas tel quel. X.20 tient à Y:`$7F0` un descripteur de quatre tampons
  qu'il fait tourner Track après Track ; 1.63 n'a que ses deux banques de sortie (research/04, §4). Le harnais devra le
  tenir ; d'ici là, les paquets NEIGHBOUR (les 9 NFX, les 4 SYN-F) sont refusés.
- **Les mêmes règles que les Machines intégrées** : le moteur de la prévisualisation du Hit charge aussi les Machines
  utilisateur ; leur code se compile à l'avance (JIT) quand elles sont installées ou chargées, hors du thread audio,
  comme `EngineT::warmUpVoices` ; leur départ se mesure par Machine comme `Engine::voiceStart` et attend
  `Engine::AlignedStart`, une Machine plus tardive n'étant pas retenue, comme TRX-XT.
- **Pas de plafond de code** : MoDule a 917 504 mots libres, et une Library qu'X.20 ne tiendrait pas (ses 14 052
  mots) reste jouable.
- **Tout paquet qu'X.20 accepte est accepté** (coût déclaré de 1 à 3 951), sans mesure ni refus de plus : MoDule n'a
  pas d'échéance DSP, un paquet plus cher que déclaré (TX9-SD) ne coûte que du temps hôte.
