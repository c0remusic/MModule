# Map : Machines utilisateur d'X.20 dans MoDule

Label: wayfinder:map

## Destination

Une Machine utilisateur faite pour MDX X.20 (un des 43 `.syx` du zip X.20 B, ou toute autre Machine MDS) installée dans
MoDule joue sur une Track comme une Machine intégrée, avec le son qu'elle a sous X.20 (selon la spécification MDS et
l'oreille de l'utilisateur : « La référence du son après Booter X.20 »). Fini quand : garde-fou vert avec
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
  - la référence du son devait être X.20 booté dans mdLib ; il ne boote pas (l'unité MAC du ColdFire), et la carte
    s'en remet à la spécification MDS et à l'oreille (« La référence du son après Booter X.20 ») ;
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
- [Booter X.20 dans mdLib](issues/02-booter-x20-dans-mdlib.md) : pas encore ; le chargeur va au bout, l'OS s'arrête
  sur l'unité MAC du ColdFire que Musashi n'émule pas ; trois manques d'émulateur corrigés sur la branche `x20-boot`.
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
- [Où tourne le code d'une Machine utilisateur](issues/05-ou-tourne-le-code.md) : dans le DSP voix, à la manière
  d'X.20 (tables d'appel à 211 entrées) ; mêmes règles que les intégrées (prévisualisation, JIT à l'avance, départ
  aligné) ; ni plafond de code ni refus au-delà de ce qu'X.20 refuse ; NEIGHBOUR attend son descripteur.
- [Une Machine utilisateur jouée par le moteur](issues/12-charger-dans-le-moteur.md) : construit ; tables d'appel à 211
  types, code à `$020000 + $E000·n` ; la voix d'une Machine de test égale l'hôte nu, 0 différence ; installée en 7 ms ;
  latence inchangée.
- [Machine jusqu'à 209, et USR dans le Kit](issues/13-machine-jusqu-a-209.md) : construit ; USR-01 à USR-16 aux
  numéros 194 à 209 dans le paramètre, `$5B` et le dump ; un numéro vide joue muet.
- [La Library et l'état du set](issues/14-library-et-etat.md) : construit ; Library partagée dans le dossier de
  données, copie par instance, l'état du set porte les Machines de son Kit.
- [Lire les conversions Pitch et Decay dans l'OS X.20](issues/10-conversions-pitch-decay.md) : Pitch DEFAULT par une
  table de 128 (16 réglages par octave), Decay par la table d'OS 1.63 à `$24bf94` ; Unipolar à $7FFFFF à 127.
- [La référence du son après « Booter X.20 »](issues/09-reference-du-son.md) : la spécification MDS et l'oreille pour
  0.1.2, plus la lecture des conversions Pitch et Decay dans l'OS X.20 ; X.20 dans mdLib sur une carte à part ; les
  corrections d'émulateur reviennent sur `main`.

## Not yet specified

- Les drapeaux TONAL et CONTINUOUS RETRIGGER : ce que MoDule en fait (les modes d'accord d'X.20 sont hors périmètre).
- NEIGHBOUR et STEREO PAIR (les 9 NFX et les 4 SYN-F) : le descripteur de quatre tampons qu'X.20 tient à Y:`$7F0` et
  fait tourner Track après Track, que le harnais du DSP voix devra tenir ; le rôle stéréo à Y+9. Refusés d'ici là.
- L'éditeur : le navigateur de Machines (la catégorie de MDM1, le sprite 32x24), les noms de SYN1-8 tirés de MDM1,
  l'installation par lâcher et par le Menu.
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
- X.20 booté dans mdLib (l'unité MAC du ColdFire, puis la suite) : une carte à part, plus tard
  ([La référence du son après « Booter X.20 »](issues/09-reference-du-son.md)).
- La vérification à la main sous macOS.
