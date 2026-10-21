# Map : les Machines intégrées d'X.20 dans MoDule 0.1.3

Label: wayfinder:map

## Destination

Les 6 Machines intégrées qu'ajoute MDX X.20 (GND-SN-PRO, GND-SW, GND-PU, NFX-EV, NFX-CO, NFX-UC) jouent sur une Track
de MoDule quand l'utilisateur lui a donné son OS X.20, avec le son qu'elles ont sous X.20 (selon le code d'X.20 et
l'oreille de l'utilisateur). Fini quand : garde-fou vert avec les nouveaux tests, poussé sur `main`, joué par
l'utilisateur dans Live sous Windows (macOS construit par la CI, pas vérifié à la main). Pas de tag : 0.1.3 sort à la
demande de l'utilisateur.

## Notes

- **Exécution dans la carte**, comme les cartes des Samples et des Machines utilisateur : les tickets `task`
  construisent. Un ticket par session, sauf la recherche.
- Décidé au cadrage du 2026-10-21 (l'utilisateur : « tout comme recommandé ») :
  - les 6 GND et NFX ; INP-CA et INP-CB hors carte (voir Out of scope) ;
  - leur code vient de l'OS X.20 de l'utilisateur (`Machinedrum_SPS1-UW_OS_X.20_B.syx`), chargé comme le flash image
    (une entrée du Menu, un lâcher sur la fenêtre, une copie dans le dossier de données) ; jamais dans le dépôt, une
    release ou un test commité ; sans lui, MoDule reste tel qu'aujourd'hui et les 6 restent cachées ;
  - GND-SN-PRO prend le numéro 1 comme sous X.20 : avec l'OS X.20 chargé, un Kit qui joue GND-SN joue GND-SN-PRO ;
  - la référence du son : le code d'X.20 et l'oreille de l'utilisateur, X.20 ne bootant pas dans mdLib (research 02
    de la carte des Machines utilisateur).
- Point de départ : `.scratch/user-machines/research/01-decoding-x20.md` (le fichier OS, ses quatre charges, le code
  DSP des 8 à P:`$13b600-$13bece`, leurs routines 68k) ; puis research 12 et 14 de la même carte (l'anneau de Y:`$7F0`,
  les tampons par Track que MoDule a construits pour les NEIGHBOUR, que les NFX d'X.20 lisent aussi).
- Domaine : `CONTEXT.md`. Une **Machine intégrée** (built-in Machine) a un numéro fixe du paramètre Machine ; les 6
  ici en sont, pas des Machines utilisateur (USR-NN).
- Fichiers pour les tests à la main : `MD_X20_OS` nomme le fichier OS X.20 de l'utilisateur (jamais dans le dépôt),
  comme `MD_X20_MACHINES` nomme ses Machines utilisateur.
- Skills : `grilling` + `domain-modeling` par défaut ; `research` pour la recherche ; `prototype` pour le premier son ;
  `tdd` pour les tranches.
- Garde-fou du `CLAUDE.md` avant chaque commit et push ; chaque tranche ajoute ses tests au garde-fou. Push sans
  demander une fois vert. Jamais de tag.

## Decisions so far

## Not yet specified

- Ce que voit l'utilisateur : où il charge l'OS X.20 (Menu, lâcher, ligne d'état), un OS refusé, une autre version
  qu'X.20 B (les adresses bougent : chercher par forme).
- Le navigateur : où vont GND-SW, GND-PU (famille GND) et les trois NFX, leurs noms, et GND-SN-PRO à la place de
  GND-SN.
- Un set ou un Kit qui joue une des 6, ouvert sans l'OS X.20 : silence, GND-SN au lieu de PRO, la ligne d'état ?
- Les NFX : le menu « hears » des NEIGHBOUR leur sert-il tel quel ; leur délai (une Track avant) et leur aperçu.
- Le reste de la plomberie : les noms et défauts de leurs paramètres (Kit, MoDule Seq, Push), l'aperçu du Hit, la
  chauffe du JIT, ANTI-CLICK (X.20 adoucit-il les 6 ?).

## Out of scope

- INP-CA et INP-CB : des compresseurs sur l'entrée audio du Machinedrum ; MoDule, un instrument, n'en a pas. Il
  faudrait une entrée sidechain, un chantier à part (choix de l'utilisateur du 2026-10-21).
- Le reste d'X.20 hors des 6 : sa DSP de mixage (INTERPOLATE, le routage des sorties du Kit), son OS 68k entier.
