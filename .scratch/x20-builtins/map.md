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
  - les routines 68k des 6 portées en C++ (choix de l'utilisateur du 2026-10-21, « portage C++ ») : les formules et
    une trentaine de constantes d'X.20 dans le dépôt, comme `md::mds::convertFine`, aucun octet ni table de l'OS ;
    testées contre les routines d'X.20 dans un 68k, sur `MD_X20_OS` ;
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

- [Le code DSP des 6 dans le programme des voix que MoDule fait tourner](issues/01-code-dsp-des-6.md) : à ses
  adresses d'X.20, libres ; 18 entrées réécrites après chaque `reset()` ; les NFX lisent le descripteur de MoDule ;
  NFX-UC attend MACRI dans l'émulateur.
- [Premier son : GND-SW sur une Track](issues/04-premier-son-gnd-sw.md) : joué dans le moteur depuis l'OS X.20 ; à
  l'oreille, « ça semble ok » ; les 5 autres de la même façon.
- [MACRI dans l'émulateur DSP56300](issues/05-macri.md) : dans le fork (77ebd463), sous le JIT et l'interpréteur ;
  NFX-UC sonne.
- [Le portage C++ contre les routines d'X.20 dans un 68k](issues/07-portage-contre-le-68k.md) : `mdX20PortTest`,
  72 576 appels, 0 différence.
- [Lire l'OS X.20 que l'utilisateur donne](issues/03-lire-l-os-x20.md) : `md::fw::parseX20Os` et un décodeur
  LZMA1 ; les quatre charges vérifiées ; `mdX20OsTest` au garde-fou, le fichier de l'utilisateur par `MD_X20_OS`.
- [Les routines 68k des 6 : les appeler ou les porter](issues/02-routines-68k-des-6.md) : petites, sans rien du
  boot d'X.20, tables calculables ; un portage C++ bit pour bit (vérifié en Python, 0 différence) recommandé.

## Not yet specified

- Une autre version qu'X.20 B (les adresses bougent : chercher par forme).
- Les NFX : leur délai (une Track avant) et leur aperçu, le menu « hears » servant tel quel (research/01).
- Le reste de la plomberie : les noms et défauts de leurs paramètres (Kit, MoDule Seq, Push), l'aperçu du Hit, la
  chauffe du JIT, ANTI-CLICK (X.20 adoucit-il les 6 ?).

## Out of scope

- INP-CA et INP-CB : des compresseurs sur l'entrée audio du Machinedrum ; MoDule, un instrument, n'en a pas. Il
  faudrait une entrée sidechain, un chantier à part (choix de l'utilisateur du 2026-10-21).
- Le reste d'X.20 hors des 6 : sa DSP de mixage (INTERPOLATE, le routage des sorties du Kit), son OS 68k entier.
