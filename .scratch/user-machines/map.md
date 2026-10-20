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
  - 16 Machines utilisateur, USR-01 à USR-16, numérotées comme X.20 ; révisé le 2026-10-20 (l'utilisateur veut
    toutes les Machines d'X.20 sous la main, dans un menu secondaire) : la Library sans limite, seize numéros par Kit
    que MoDule donne lui-même, un Kit du Bank qui se souvient des siens (tickets 24 à 28) ;
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
  tournent, les 30 qui font du son sonnent ; le programme doit vivre en mémoire externe ; Pitch et Decay devinés
  alors, convertis depuis comme X.20.

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
- [Reprendre les corrections d'émulateur de x20-boot](issues/11-corrections-emulateur.md) : faites ; CMPU et EOR
  #xxxx sur le fork dsp56300, le push d'octet du ColdFire sur le fork mc68k de l'utilisateur, chacune testée ; le son
  de MoDule n'en change pas.
- [Les conversions d'X.20 dans MoDule](issues/15-conversions-x20.md) : construit ; Pitch DEFAULT par la table L,
  Decay par la table d'OS 1.63 lue dans l'image de l'utilisateur, bords d'Unipolar, Boolean et Index d'X.20.
- [Lire les conversions Pitch et Decay dans l'OS X.20](issues/10-conversions-pitch-decay.md) : Pitch DEFAULT par une
  table de 128 (16 réglages par octave), Decay par la table d'OS 1.63 à `$24bf94` ; Unipolar à $7FFFFF à 127.
- [La référence du son après « Booter X.20 »](issues/09-reference-du-son.md) : la spécification MDS et l'oreille pour
  0.1.2, plus la lecture des conversions Pitch et Decay dans l'OS X.20 ; X.20 dans mdLib sur une carte à part ; les
  corrections d'émulateur reviennent sur `main`.
- [Installer une Machine utilisateur par le Menu](issues/16-installer-par-le-menu.md) : construit ; Menu et lâcher
  par le même import, premier numéro libre, la Track montrée la joue, la ligne d'état le dit.
- [Les noms d'une Machine utilisateur dans l'éditeur](issues/17-noms-mdm1.md) : construit ; bande de tête, SYN1-8 et
  fichier `.kit` tirés du paquet que l'instance tient.
- [USR-01 à USR-16 dans le navigateur](issues/18-navigateur-usr.md) : accepté tel que prototypé ; sous les familles,
  USR et ROM 33-48, vides grisées ; un clic prend la copie de la Library.
- [Les tampons des voisins](issues/19-tampons-des-voisins.md) : quatre tampons comme X.20 (Y:`$100`, `$120`, `$7A0`,
  `$7C0`), le programme voix d'OS 1.63 patché à P:`$9B-$A7`, 32 zéros pour une voix sautée ; l'aperçu ne porte aucune
  Machine utilisateur aujourd'hui.
- [Garder ce qui ne joue pas](issues/23-garder-ce-qui-ne-joue-pas.md) : construit ; le set garde le paquet refusé, son
  numéro joue la Machine vide, la ligne d'état le dit.
- [NEIGHBOUR et les Hits sample-accurate](issues/20-voisins-et-hits-sample-accurate.md) : la Track NEIGHBOUR prend le
  délai de la Track d'avant.
- [L'aperçu d'une Machine voisine](issues/21-apercu-d-une-machine-voisine.md) : l'aperçu frappe aussi ses voisines et
  ne fait entendre qu'elle ; toute USR s'y entend depuis c33d669.
- [Faire jouer NEIGHBOUR](issues/22-faire-jouer-neighbour.md) : construit ; l'anneau de quatre tampons, le délai de la
  Track d'avant, l'aperçu avec ses voisines ; les 13 NFX et SYN-F s'installent.
- [La Library sans limite](issues/24-library-sans-limite.md) : construit ; tous les paquets du dossier, nommés par leur
  code, `.syx` d'X.20 lus aussi ; les 43 du zip y entrent.
- [Les numéros USR donnés par le Kit](issues/25-numeros-donnes-par-le-kit.md) : construit ; le numéro qui la tient,
  sinon celui de la Track, sinon le premier libre ; un numéro vide joue muet, rien pris de la Library.
- [Copier les 43 du zip X.20 dans la Library de l'utilisateur](issues/28-copier-les-43.md) : fait ; sur son seul PC.
- [Le menu secondaire des Machines utilisateur](issues/26-menu-secondaire.md) : comme le premier menu, au choix de
  l'utilisateur ; « all 43 installed » sous USR, familles de neuf rangées au plus avec leur genre, le code dans la case.
- [Un Kit du Bank se souvient de ses Machines utilisateur](issues/27-kit-du-bank-et-ses-machines.md) : construit ;
  `Bank.machines` à côté du Bank, l'id et le nom du paquet par numéro ; rechargé, il les reprend, ou dit ce qui manque.

## Not yet specified

- STEREO PAIR (rôle à Y+9, aucune des 43 ne le pose, [research/11](research/11-mdm1-flags.md)) : après NEIGHBOUR,
  que l'utilisateur a voulu le 2026-10-19 (tickets 19 à 23 : les tampons des voisins, le délai face aux Hits
  sample-accurate, l'aperçu, la construction, garder un paquet que la build ne joue pas).
- Le sprite 32x24 de MDM1 : où le montrer, si l'éditeur le montre.
- Le fondu ANTI-CLICK qu'X.20 fait avant TRIG : le faire ou non, OS 1.63 n'en a pas. S'il se fait, il suit le
  drapeau CONTINUOUS RETRIGGER (31 des 43, research/11), qu'X.20 lit pour le sauter ; MoDule ne le lit pas d'ici là,
  ce qui revient à ANTI-CLICK OFF. [research/13](research/13-anti-click.md) le décrit : un réglage global d'X.20
  (OFF, 64, 128 ; 128 par défaut), le TRIG retenu 2 ou 4 blocs par la DSP des voix, la voix brute de la Track baissée
  en ligne droite par la DSP du mixer pendant l'attente ; tout en C++ dans MoDule, mais 64 ou 128 échantillons de
  latence en plus. Trois voies pour l'utilisateur : ne rien faire (OFF), 128 fixe, ou un réglage OFF / 64 / 128.
- TONAL : MoDule n'en fait rien, le drapeau n'ouvrant que l'éditeur TUNING d'X.20, hors périmètre (research/10 et 11).
  Mais un `$5B` d'X.20 qui porte des drapeaux d'accord est mal lu (USR-01 TONAL devient P-I-MT, research/11) ;
  aucun message de MoDule ne l'envoie aujourd'hui.
- Les paramètres d'une Machine utilisateur sur Push. Son nom y reste USR-NN, comme le paramètre Machine le montre (choix de l'utilisateur du 2026-10-19).
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
