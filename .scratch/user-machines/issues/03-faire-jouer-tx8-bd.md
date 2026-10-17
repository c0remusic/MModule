# Faire jouer TX8-BD sur un DSP56300 nu

Type: prototype
Status: construit, attend l'écoute de l'utilisateur
Assignee: c0remusic (session du 2026-10-17)
Blocked by:

## Question

Notre lecture de l'ABI MDS suffit-elle à faire sonner une Machine utilisateur ? Un hôte minimal, hors du plug-in :
lire le paquet MDS1 de TX8-BD du zip, le reloger, fournir les imports 1, 6, 7 et 8, poser ses 8 paramètres convertis
en Y+1 à Y+8, appeler INIT, TRIG puis RENDER par blocs de 32 sur un DSP56300 émulé seul, écrire un WAV. L'utilisateur
l'écoute ; si « Booter X.20 dans mdLib » a donné un Hit de TX8-BD, les deux se comparent. Avec : les cycles réels par
RENDER contre les 3 951 du budget, et ce que l'ABI laisse flou.

## Prototype

Construit le 2026-10-17 : `mdsPlay` (`source/elektron/md/mdEngine/tools/mds/`). Détails et mesures :
[research/03-first-sound.md](../research/03-first-sound.md).

- Oui, notre lecture de l'ABI suffit : les 43 Machines du zip tournent sans faute, et les 30 qui font leur propre son
  sonnent (NFX et SYN-F traitent les Tracks voisines, muettes ici). TX8-BD : une grosse caisse vers 54 Hz, crête
  -1,5 dBFS.
- Ce que l'ABI laisse flou : le programme doit vivre en mémoire externe, où P, X et Y sont le même stockage (TX8-BD lit
  ses tables en `x:` dans son propre code) ; les conversions Pitch et Decay ne sont pas données (15 Machines utilisent
  Pitch, aucune Decay).
- Cycles : RENDER de 1 000 à 3 400 pour la plupart ; P-I-PN, TX9-BD et TX9-XT au-dessus de leur chiffre déclaré, TX9-SD
  au-dessus de la limite de 3 951 (5 537 en moyenne). Hôte : 0,3 à 1,2 % d'un cœur par Track.

Reste : l'écoute de l'utilisateur (les WAV du dossier `mdsplay` du scratchpad de la session), et la comparaison avec
X.20 si « Booter X.20 dans mdLib » donne un Hit de TX8-BD.
