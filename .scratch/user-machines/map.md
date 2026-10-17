# Map : Machines utilisateur d'X.20 dans MoDule

Label: wayfinder:map

## Destination

Une Machine utilisateur faite pour MDX X.20 (un des 43 `.syx` du zip X.20 B, ou toute autre Machine MDS) installée dans
MoDule joue sur une Track comme une Machine intégrée, avec le son qu'elle a sous X.20. Fini quand : garde-fou vert avec
les nouveaux tests, poussé sur `main`, joué par l'utilisateur dans Live sous Windows (macOS construit par la CI, pas
vérifié à la main). Pas de tag : 0.1.2 sort à la demande de l'utilisateur, avec la carte des Samples et Push mappé.

## Notes

- **Exécution dans la carte**, comme la carte des Samples : les tickets `task` construisent. Un ticket par session, sauf
  la recherche.
- Décidé au cadrage du 2026-10-17 :
  - cette carte ne couvre que X.20 ; Push mappé se fait dans une session à part, sans carte ;
  - « firmware X.20 » veut dire ici les Machines utilisateur, hébergées depuis l'ABI MDS publiée, sans l'OS X.20 ;
  - 16 Machines utilisateur, USR-01 à USR-16, numérotées comme X.20 ;
  - installées par le même lâcher que les Samples (où lâcher : ticket « Où lâcher » de la carte des Samples) et par
    une entrée « Install User Machine… » du Menu ;
  - pour 0.1.2, seulement les imports qu'utilisent les 43 du zip (1, 6, 7, 8) ;
  - la référence du son est X.20 lui-même, booté dans mdLib comme 1.63 l'est aujourd'hui (ticket « Booter X.20 dans
    mdLib ») ; si X.20 ne boote pas, la carte revient sur la question ;
  - les 8 Machines intégrées nouvelles d'X.20 (GND-SN-PRO, GND-SW, GND-PU, NFX-EV, NFX-CO, NFX-UC, INP-CA, INP-CB) :
    hors de cette carte et de 0.1.2 (voir Out of scope) ;
  - aucun contact des auteurs requis : rien n'est redistribué.
- Domaine : `CONTEXT.md`. **User Machine** (Machine utilisateur) : Machine dont l'utilisateur installe le code,
  nommée par son numéro, « USR-05 » ; jamais « slot » (la place d'un Kit dans le Bank) ni « machine MDS ».
- Skills : `grilling` + `domain-modeling` par défaut ; `codebase-design` pour « Où tourne le code d'une Machine
  utilisateur » ; `prototype` pour le premier son ; `tdd` ou `implement` pour les tranches ; `research` pour la
  recherche.
- Garde-fou du `CLAUDE.md` avant chaque commit et push ; chaque tranche ajoute ses tests au garde-fou. Push sans
  demander une fois vert. Jamais de tag.
- **Latence** et **fluidité** comme sur les autres cartes : tout changement du chemin de rendu rapporte latence
  note→son et gigue avant et après ; jamais `Plugin::withDeviceLocked` par frame ou par clic.
- Jamais de Machine utilisateur, d'OS X.20, de ROM ni de Sample d'usine dans un dépôt, une release ou un test commité :
  aucune licence ne le permet (MDX est binaire seul ; MDS et les 43 n'en ont pas). Les tests lisent le zip de
  l'utilisateur par une variable d'environnement, comme le flash image.
- Recherches : un doc par ticket dans `research/` à côté de cette carte. Faits établis au cadrage :
  [research/00-mds-and-x20.md](research/00-mds-and-x20.md) (format MDS1, ABI, imports des 43, OS X.20, pull request
  100 de joelanders, adresses 1.63 de mdEngine).

## Decisions so far

<!-- une ligne par ticket résolu : [titre](issues/NN-slug.md) : gist -->

- [Décoder l'OS X.20 et y trouver ses Machines intégrées](issues/01-decoder-l-os-x20.md) : le fichier se décode en
  entier ; le DSP voix d'X.20 est celui de 1.63 plus les 8 et l'hôte des Machines utilisateur ; les 8 coûtent 3 à 5
  semaines.
- [Mémoire libre et point d'appel dans le DSP voix d'OS 1.63](issues/04-memoire-du-dsp-voix.md) : 16 Machines
  utilisateur tiennent dans le DSP voix, en place et en temps, par ses tables d'appel étendues comme X.20 ; un DSP à
  part n'apporte que l'isolement.
- [Faire jouer TX8-BD sur un DSP56300 nu](issues/03-faire-jouer-tx8-bd.md) (construit, écoute en attente) : les 43
  tournent, les 30 qui font du son sonnent ; le programme doit vivre en mémoire externe ; Pitch et Decay devinés.

- [Le paramètre Machine et USR-01 à USR-16](issues/06-parametre-machine-et-usr.md) : Machine va jusqu'à 209, USR aux
  numéros d'X.20 ; l'automation de Machine des sets 0.1.x se décale, dit dans les notes ; USR vide joue muet.
- [Où vivent les Machines utilisateur installées](issues/07-ou-vivent-les-machines.md) : dans la Library du dossier de
  données et dans l'état du set ; le set sonne comme enregistré partout ; chaque instance joue sa copie ; même règle
  pour les Samples.

## Not yet specified

- Les tranches de construction : charger un paquet MDS1 dans le moteur, les imports 1, 6, 7 et 8, la conversion des
  paramètres (l'octet de conversion de MDM1), les drapeaux TONAL, CONTINUOUS RETRIGGER, NEIGHBOUR et STEREO PAIR, une
  Machine utilisateur dans le dump de Kit, la prévisualisation du Hit, la compilation à l'avance du JIT, les tests
  contre X.20. Elles sortent une fois tranchés « Faire jouer TX8-BD sur un DSP56300 nu », « Où tourne le code d'une
  Machine utilisateur », « Le paramètre Machine et USR-01 à USR-16 » et « Où vivent les Machines utilisateur
  installées ».
- L'éditeur : le navigateur de Machines (la catégorie de MDM1, le sprite 32x24), les noms de SYN1-8 tirés de MDM1,
  l'installation par lâcher et par le Menu.
- Où sonne un Hit d'une Machine utilisateur par rapport à `Engine::AlignedStart`.
- Le fondu ANTI-CLICK qu'X.20 fait avant TRIG : le faire ou non, OS 1.63 n'en a pas.
- Le nom et les paramètres d'une Machine utilisateur dans le fichier `.kit` de md-drums-seq et sur Push.
- SAMPLE_INFO et SAMPLE_READ, pour qu'une Machine utilisateur lise les Samples (lien avec la carte des Samples).

## Out of scope

- [Les 8 Machines intégrées d'X.20 dans 0.1.2 ?](issues/08-les-8-machines-integrees.md) : non ; 3 à 5 semaines
  d'après « Décoder l'OS X.20 », sur une carte à part pour 0.1.3 (choix de l'utilisateur du 2026-10-17).
- Le moteur d'X.20 hors Machines : LFO-B, enveloppe par Track, retrigs, accord A=440, INTERPOLATE, timing PRECISE,
  octaves tonales, Kit 65.1 : rétro-ingénierie d'un binaire compressé sans symboles, des mois (cadrage).
- Le séquenceur d'X.20 : le séquenceur du firmware reste au repos, md-drums-seq séquence (cadrage).
- RAM_WORKSPACE et RAM_RECORD : la RAM est hors périmètre comme sur la carte des Samples (cadrage).
- Installer une Machine utilisateur par SysEx depuis l'hôte (cadrage).
- La vérification à la main sous macOS.
