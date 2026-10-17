# La Library et l'état du set

Type: task
Status: resolved
Assignee: c0remusic (session du 2026-10-17)
Blocked by: 12

## Question

Troisième tranche, comme « Où vivent les Machines utilisateur installées » l'a décidé : la Library dans le dossier
de données de MoDule (un fichier par numéro, le paquet tel qu'installé), lue au démarrage et relue quand elle change,
comme le Bank ; l'état du plug-in embarque le paquet de chaque Machine utilisateur que son Kit joue (un bloc de plus
dans l'état, à côté de « KSLT ») ; à l'ouverture, la copie du set joue ; chaque instance garde sa copie jusqu'à ce
qu'elle charge un Kit du Bank, un set, ou choisisse cette Machine ; rien ne change de son pendant que l'hôte joue.
Tests : un set restauré sans Library joue sa Machine ; deux instances, deux Machines en USR-05, chacune la sienne ;
un Kit du Bank joue ce que la Library a.

## Answer

Construit le 2026-10-17 :

- `mdDrums::Library` : `USR-01.mds` à `USR-16.mds` dans le dossier `UserMachines` des données, chacun le paquet tel
  qu'installé ; partagée par les instances comme le Bank, suivie quand une autre y écrit (`follow`). Installer prend un
  dump `.syx` ou un paquet `.mds`, vérifié comme le DSP voix le jouera.
- Chaque instance joue sa copie, tenue par son Device : elle prend celle de la Library quand elle en installe une
  (`Processor::installUserMachine`), charge un Kit du Bank (`Processor::takeUserMachines`, avant le dump), ou qu'une
  Track est sur un numéro dont elle n'a pas de copie (`Controller::followUserMachines`, au timer). Installer dans la
  Library ne change aucun son par soi-même.
- L'état du Device porte, après le message mixer, un message `UserMachine` (0x06 : numéro, taille, paquet en 7 bits)
  pour chaque Machine utilisateur que son Kit joue : un set sonne comme enregistré là où elles ne sont pas installées.
  Un état sans Machine utilisateur reste celui d'avant.
- Vérifié : `mdDrumsProcessorTest` (installée à USR-05 et jouée ; un set ouvert avec une Library vide joue sa copie et
  laisse la Library vide ; un Kit du Bank prend la USR-05 de la Library, différente ; deux instances, deux Machines
  en USR-05 ; une Track sur USR-06 prend celle de la Library), `mdDrumsBankTest` (une Library suit l'installation et
  le retrait d'une autre ; un fichier qui n'est pas une Machine utilisateur refusé).
- Reste au brouillard : l'éditeur (installer par le Menu ou un lâcher, le navigateur, les noms de MDM1), et remplacer
  la copie d'une instance quand l'utilisateur choisit la Machine dans le navigateur (aujourd'hui seulement si
  l'instance n'en a pas).
