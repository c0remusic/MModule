# Le code DSP des 6 dans le programme des voix que MoDule fait tourner

Type: research
Status: open
Assignee:
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
