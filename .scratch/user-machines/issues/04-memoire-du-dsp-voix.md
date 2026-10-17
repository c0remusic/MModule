# Mémoire libre et point d'appel dans le DSP voix d'OS 1.63

Type: research
Status: resolved
Assignee: c0remusic (session du 2026-10-17)
Blocked by:

## Question

Le DSP voix tel que MoDule le charge (le programme de la section 1 d'OS 1.63, `engine/VoiceEngine.*`) a-t-il la
place et le temps pour 16 Machines utilisateur ? Mesurer la mémoire P, X, Y et externe libre ; ce qu'il faut à 16
Tracks (64 mots X et 64 mots Y chacune, 1 536 mots externes, le code relogé de jusqu'à 16 paquets, la table de sinus
et les anneaux des imports) ; où la boucle de rendu par voix pourrait appeler RENDER d'une Machine utilisateur à la
place d'une Machine intégrée ; les cycles qu'elle prend aujourd'hui sur les 71 680 d'une trame. Rapporter aussi ce
qu'un DSP56300 émulé à part coûterait à la place (mémoire, CPU, mixage dans la chaîne de DSP1 en C++).

## Answer

Recherche du 2026-10-17, mesurée sur le moteur : [research/04-voice-dsp-memory.md](../research/04-voice-dsp-memory.md).

- **La place, oui, sans rien allouer** : `$020000-$0FFFFF` (917 504 mots que P, X et Y partagent) n'est jamais écrit
  par 1.63 ; les 16 plus gros des 43 paquets font 27 706 mots. Le reste existe déjà dans la disposition de 1.63 :
  blocs X et Y par Track à `$800 + $40·t`, mémoire externe de 1 536 mots par Track (les tampons de P-I à
  `$135600 + $600·t`), table de sinus à `$148000`, brouillons à X:`$000` et Y:`$000`.
- **Le temps, oui** : un Kit chargé coûte 38 789 cycles par bloc (54 % de 71 680), 16 ROM jusqu'à 67 309 (94 %), 16
  Machines utilisateur à la limite de 3 951 environ autant. Le harnais n'attend aucune échéance : un paquet plus cher
  que déclaré (TX9-SD) ne coûte que du temps hôte.
- **Rien à patcher dans la boucle** : le répartiteur appelle INIT, TRIG et RENDER par trois tables. Une Machine
  utilisateur de test, posée au type 195 (USR-01) dans des tables copiées à 211 entrées comme X.20 le fait, joue sur
  sa Track avec les registres que MDS promet ; les Tracks intégrées restent identiques au bit près.
- **Un DSP à part n'apporte que l'isolement** : environ 82 Mo par instance tel que l'arbre construit un DSP (80,8 Mo
  d'anneaux audio de ses périphériques), environ 1 µs de plus par bloc, et NEIGHBOUR l'oblige à tourner après le DSP
  voix.
- Restent ouverts : les adresses d'import d'X.20 elles-mêmes, son plafond de 14 052 mots de code (que les 16 plus gros
  des 43 dépassent), et les E12 qui lisent `$1F85B5-$1FFFFF`, que la carte des Samples donne aux Samples de
  l'utilisateur.
