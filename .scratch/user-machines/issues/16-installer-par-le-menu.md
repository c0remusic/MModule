# Installer une Machine utilisateur par le Menu

Type: task
Status: resolved
Assignee: c0remusic (session du 2026-10-17)
Blocked by: 

## Question

L'entrée « Install User Machine… » du Menu, comme l'utilisateur l'a décidé le 2026-10-17 : elle ouvre un sélecteur de
fichier (`.syx` ou `.mds`), installe le paquet au premier numéro libre de la Library (refusé si les 16 sont pris), et
la ligne d'état dit le numéro pris ou l'erreur ; l'instance prend sa copie. Choisir un USR dans le navigateur prend la
copie de la Library (choix explicite), même si l'instance en a une autre. Tests : l'installation par le Processor au
premier numéro libre, le refus quand tout est pris, un fichier qui n'est pas une Machine utilisateur refusé avec son
message, et le choix d'un USR qui remplace la copie de l'instance.

## Answer

Construit le 2026-10-17 :

- L'entrée « Install User Machine... » du Menu ouvre un sélecteur (`.syx`, `.mds`) ; le fichier passe par
  `Processor::importFile`, commun au Menu et au lâcher. Le paquet va dans la Library au premier numéro libre, ou au
  numéro que joue la Track montrée ; tout pris, il est refusé. L'instance prend sa copie.
- La Track montrée joue la Machine installée : sans les cases USR du navigateur (« USR-01 à USR-16 dans le
  navigateur »), c'est ainsi qu'on l'entend. Écart assumé par rapport à « Install va au premier numéro libre » : le
  numéro est le même, la Track suit en plus.
- La ligne d'état dit ce qui s'est passé pendant 8 s (`StatusView::say`) : « TST-SW installed as USR-01, Track 3 »,
  ou le refus et sa raison.
- Le même import part d'un fichier lâché sur le nom de la Machine ou sur l'écran Hit (`DropView`, ticket 06 de la carte
  des Samples).
- Vérifié : `mdDrumsProcessorTest` (« imports » : un `.mds` sur la Track 3 va en USR-01 et la Track le joue ; quinze de
  plus remplissent les 16 ; le dix-septième est refusé), `mdDrumsSkinTest` (un lâcher, la ligne dans sa boîte).
- Pas encore : choisir un USR dans le navigateur prend la copie de la Library. Les cases n'existent pas ; le
  branchement viendra avec elles.
