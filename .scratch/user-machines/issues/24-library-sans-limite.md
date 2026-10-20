# La Library sans limite

Type: task
Status: resolved
Assignee: c0remusic (session du 2026-10-20)
Blocked by:

## Question

Le 2026-10-20, l'utilisateur veut toutes les Machines d'X.20 sous la main (« On peut pas avoir toutes les batteries + les
nfx dans un menu secondaire peut etre ? »), et accepte que MoDule donne lui-même les numéros USR (« Oui, go »). La
Library cesse donc d'être seize numéros : elle tient toute Machine utilisateur installée, sans limite de nombre, un
fichier par paquet dans `UserMachines` du dossier de données, nommé par son code MDM1 (`TX8-BD.mds`, un suffixe si deux
paquets différents portent le même code). Elle lit aussi les `.syx` d'X.20 posés là à la main. Un paquet déjà installé
ne s'installe pas deux fois. Elle suit encore les autres instances par la date d'écriture du dossier.

Installer (le Menu, le lâcher) ne prend plus de numéro : le paquet entre dans la Library, et la Track montrée le joue par
le ticket « Les numéros USR donnés par le Kit ». Plusieurs fichiers entrent tous, sans refus pour cause de place.

Tests : les 43 du zip de l'utilisateur (par `MD_X20_MACHINES`, jamais commités) entrent tous ; le même fichier deux
fois donne une entrée ; deux codes égaux donnent deux fichiers ; une instance voit ce qu'une autre installe ; un `.syx`
posé à la main se lit.

## Answer

Construit le 2026-10-20 avec « Les numéros USR donnés par le Kit », qu'il ne pouvait pas précéder seul (installer
prenait un numéro de la Library) :

- `mdDrums::Library` tient tous les paquets du dossier `UserMachines`, `.mds` et `.syx`, un même paquet sous deux formes
  compté une fois, un fichier que la DSP des voix ne joue pas ignoré ; rangés par nom puis par fichier
  (`machines()`), cherchés par octets (`find`). `install` écrit `TX8-BD.mds`, ou `TX8-BD 2.mds` pour un autre paquet du
  même nom, et rend celui qu'elle tient déjà pour un paquet installé. `follow` relit quand la date du dossier ou d'un
  fichier bouge.
- Installer ne prend plus de numéro : `Processor::importFile` met le paquet dans la Library, puis la Track le joue
  (ticket 25) ; sans Track, il est seulement installé.
- Vérifié : `mdDrumsBankTest` (installer, le même deux fois, deux du même nom, suivre une autre instance, retirer, un
  fichier qui n'est pas une Machine ; avec `MD_X20_MACHINES`, les 43 du zip dans une Library, de NFX-BC à TXZ-ST, et un
  `.syx` posé à la main) ; `mdDrumsProcessorTest` (23 installées dans une instance).
