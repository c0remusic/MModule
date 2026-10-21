# Le code DSP des 6 dans le programme des voix que MoDule fait tourner

Type: research
Status: resolved
Assignee: c0remusic (session du 2026-10-21)
Blocked by:

## Question

mdEngine fait tourner le programme des voix d'OS 1.63 (`VoiceEngine`), plus les tampons par Track que MoDule a ajoutés
pour les NEIGHBOUR (research 14 de la carte des Machines utilisateur, Y:`$1000`, descripteurs à Y:`$7A0`, pointeur à
Y:`$7F0`). Établir ce qu'il faut pour que le code DSP des 6 (research 01 §6 : 2 255 mots à P:`$13b600-$13bece` d'X.20
B, entrées INIT, TRIG, RENDER des numéros 1, 4, 5, 7, 8, 9) y tourne :

- où placer ces mots dans la mémoire P du programme 1.63 sans rien écraser de ce que MoDule y met (le stub des
  sources à P:`$C40`, les Machines utilisateur), ou s'ils peuvent rester à leur adresse ; relocation nécessaire ou non ;
- les 18 entrées des trois tables de répartition à changer, et la base des tables que le harnais code en dur ;
- ce qu'ils lisent hors de la voix : la table X:`$148000+`, Y:`$7F0` pour les NFX (même descripteur que celui que
  MoDule écrit pour ses NEIGHBOUR ? quelle Track un NFX d'X.20 entend-il), les crochets de la boucle d'X.20
  (`$13ff3c`, `$13ff88`) ;
- la chauffe du JIT et le coût par voix.

Un document `research/01-code-dsp-des-6.md` avec les marques [V] [M] [I] de la carte des Machines utilisateur et une
recommandation. Rien de construit ni de commité depuis l'OS X.20 ; les sondes lisent `MD_X20_OS`.

## Answer

[research/01](../research/01-code-dsp-des-6.md) : la place est libre. Les 3 792 mots visés lisent 0 dans un moteur
neuf, et rien de 1.63, des P-I, des Machines utilisateur ni des Samples ne les touche : le code reste à ses adresses
d'X.20 (P:`$13b600-$13bece` et la table `$147600-$147c00`), sans relocation. Ses 18 entrées vont dans les tables
étendues, que `extendDispatch` recopie à chaque `reset()` : un seul propriétaire dans `VoiceEngine` les réécrit après
lui, comme les Machines utilisateur. GND-SN-PRO lit la table sinus de X:`$148000` que 1.63 construit ; les trois NFX ne
lisent que les mots 1 et 2 du descripteur de Y:`$7F0`, celui que MoDule écrit déjà, et entendent sous X.20 les Tracks
t-1 et t-2, les sources par défaut de MoDule : le menu « hears » sert tel quel. Aucun crochet de boucle d'X.20.
Bloquant pour NFX-UC : l'émulateur n'a pas l'instruction MACRI (ni JIT ni interpréteur) ; réécrite en MACI + RND dans la
sonde, elle sonne. Coût : 666 à ~4 700 cycles par voix ; sans chauffe, le premier Hit de GND-SW coûte 28 ms dans un
bloc ; chauffer les 6 prend 48,6 ms.
