# Les conversions d'X.20 dans MoDule

Type: task
Status: resolved
Assignee: c0remusic (session du 2026-10-17)
Blocked by:

## Question

Remplacer les devinettes de `md::mds::convertFine` par ce que fait X.20 ([research/10](../research/10-pitch-and-decay.md)) :
Pitch au mode DEFAULT par sa table L au TUNING 0, Decay par la table d'OS 1.63 que le moteur lit dans le flash image
(`$24bf94`, 512 mots), Unipolar à $7FFFFF à 127 et à 0 sous un réglage, Index tronqué et Boolean non nul entre deux
réglages. `HostModel` donne la table Decay à la conversion ; `BareHost` et `mdsPlay` la prennent du flash image quand
il est là. Tests : les 128 valeurs de Pitch et de Decay contre celles de la recherche, et la Machine de test de
`mdDrumsEngineTest` toujours égale à l'hôte nu.

## Answer

Construit le 2026-10-17 :

- `md::mds::convertFine` convertit comme `$21aeba` d'X.20 : la valeur bornée à [min << 7, max << 7], puis la
  conversion du quartet bas. Pitch au mode DEFAULT par la table L, recalculée par la récurrence d'X.20 (entiers, 24 bits
  de fraction ; la même au TUNING 0 et sous l'accord hérité) ; Decay par `T[v >> 5]`, la table d'OS 1.63 à `$24bf94` ;
  Unipolar 0 sous un réglage et $7FFFFF dès 127 ; Boolean tout mot non nul ; Index tronqué.
- La table Decay vient de l'image de l'OS de l'utilisateur, jamais du dépôt : `HostModel` la lit dans la mémoire du
  68k (`decayTable()`), `BareHost` la reçoit (`md::mds::decayTable` sur la section 0), `mdsPlay` la prend du flash
  image que `--flash` nomme, sinon de la loi ajustée de la recherche (`approximateDecayTable`, à 126 LSB au plus de la
  table avec ses coefficients arrondis).
- Les modes TONAL et le TUNING global restent dehors, comme la carte l'a cadré : une Machine utilisateur joue toujours
  en DEFAULT, à TUNING 0.
- Vérifié par `mdDrumsEngineTest` : les 128 réglages de Pitch égaux à ceux de la recherche, les neuf mots de Decay de
  la recherche, Decay croissant, les bords d'Unipolar, Boolean et Index. La Machine de test règle désormais son DEC en
  Decay (40, environ 22 ms à -60 dB) : sa voix dans le moteur égale toujours l'hôte nu, 0 différence sur 3 008
  échantillons, latence inchangée (87 échantillons de la note au son sur sa sortie).
- Les 43 paquets d'X.20 B rejoués par `mdsPlay --flash` : tous finissent, les 30 qui sonnaient sonnent encore. Aucun
  n'a de paramètre Decay ; des 15 qui ont un Pitch, 10 changent de crête de 0,1 à 2,7 dB (64 donne P = 32 768 au lieu
  de 15 675).
