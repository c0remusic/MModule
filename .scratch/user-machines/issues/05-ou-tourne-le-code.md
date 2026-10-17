# Où tourne le code d'une Machine utilisateur

Type: grilling
Status:
Assignee: c0remusic (session du 2026-10-17)
Blocked by: 03, 04

## Question

Dans le DSP voix d'OS 1.63, à côté des Machines intégrées, ou sur un DSP56300 émulé à part dont la sortie rejoint la
chaîne de DSP1 en C++ ? Avec : le coût CPU de chaque voie, où sonne le Hit (`Engine::voiceStart`,
`Engine::AlignedStart`), ce que deviennent NEIGHBOUR et STEREO PAIR, le moteur de la prévisualisation du Hit, la
compilation à l'avance du JIT (`EngineT::warmUpVoices`), et une Machine utilisateur installée pendant que le moteur
joue.
