# Un Sample joué par le moteur

Type: task
Status: resolved
Assignee: c0remusic (session du 2026-10-17)
Blocked by: 

## Question

Première tranche, comme « Écrire un Sample dans le DSP voix en marche » l'a mesuré : `mdDrums::Engine` reçoit le Sample
d'une Machine ROM (ses mots de 12 bits, sa longueur, sa boucle, son rate en fraction de 44,1 kHz) ou son retrait, et le
Device l'écrit dans le DSP voix sur le thread audio, entre deux blocs, par tranches de 4 096 mots, l'entrée de
répertoire en dernier. Les Samples de l'utilisateur vivent dans la place sûre au-dessus de la banque d'usine
($18FC12-$1F85B4, sous ce que lisent les E12), à la suite, réécrits à partir du premier qui change ; un Sample qui ne
tient pas est refusé. Retirer remet celui d'usine (ROM-01 à ROM-32) ou rend la Machine muette (ROM-33 à ROM-48). Le
moteur de la prévisualisation du Hit reçoit les mêmes à sa construction. Tests : un Sample joue sur ROM-33 et sur
ROM-05 (à la place de celui d'usine), retiré il rend l'usine ou le silence, la prévisualisation joue le même son, une
écriture ne dépasse pas sa tranche par bloc, aucune allocation sur le thread audio, latence note→son inchangée.

## Answer

Construit le 2026-10-17 :

- `mdDrums::Samples` (`mdDrums/mdDrumsSamples.*`) tient les Samples des 48 Machines ROM tels que le DSP voix les a :
  ceux de l'utilisateur à la suite, dans l'ordre des ROM, de $18FC12 à $1F85B4 ; sans le sien, une Machine joue celui
  d'usine (ROM-01 à ROM-32) ou rien (ROM-33 à ROM-48).
- Un changement s'écrit à partir de la première ROM qu'il déplace, sur le thread audio avant chaque bloc du moteur,
  4 096 mots par bloc, sans allouer : les entrées de ce qui bouge pointent d'abord sur rien, puis les mots, puis les
  entrées. Un Hit d'une telle Machine entre-temps ne joue rien.
- `Engine::setSample` et `Device::setSample` prennent un Sample, ou aucun pour rendre celui d'usine, là où le moteur
  ne rend pas (le verrou du Device) ; un Sample qui ne tient pas avec les autres est refusé et ne change rien. La
  requête de la prévisualisation du Hit porte les Samples de l'instance, que son moteur neuf écrit d'un coup
  (`writeSamples`) avant son premier bloc.
- Vérifié par `mdDrumsEngineTest` (« user Samples ») : ROM-33, muette, joue le Sample du test, écrit en 6 blocs
  (22 050 mots) ; ROM-05 joue celui de l'utilisateur à la place de celui d'usine, puis celui d'usine à l'échantillon
  près une fois l'autre retiré ; ROM-33 redevient muette ; un Sample trop grand est refusé ; la prévisualisation de
  ROM-33 joue le Sample de l'instance (crête 12 584) et rien sans lui.
- Reste à la tranche suivante : la prévisualisation dans l'éditeur reçoit les Samples quand l'instance en tient
  (ticket 10, qui leur donne un propriétaire).
