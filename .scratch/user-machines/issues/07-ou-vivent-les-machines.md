# Où vivent les Machines utilisateur installées

Type: grilling
Status: resolved
Assignee: c0remusic (session du 2026-10-17)
Blocked by:

## Question

Une Machine utilisateur installée vit-elle dans le dossier de données de MoDule, partagée par toutes ses instances
comme le Bank, dans l'état du plug-in (le set), ou dans les deux ? Avec : un set ouvert sur un autre PC où USR-05
n'est pas installé, deux instances qui installent chacune une Machine différente en USR-05, un Kit du Bank qui joue
une Machine utilisateur absente, et ce qu'un set garde si la Machine est désinstallée. Les mêmes questions sont dans
le brouillard de la carte des Samples (un set sans le fichier d'un Sample, plusieurs instances) : trancher les deux de
la même façon, ou dire pourquoi non.

## Answer

Choix de l'utilisateur (2026-10-17) :

- **Les deux.** La **Library** (`CONTEXT.md`), dans le dossier de données de MoDule et partagée par ses instances comme
  le Bank, tient les Machines utilisateur installées sur ce PC ; c'est ce que le navigateur liste. L'état du plug-in
  embarque en plus le code des Machines utilisateur que son Kit joue (de 1 à 10 Ko chacune pour les 43 du zip).
- **Un set sonne comme il a été enregistré**, sur n'importe quel PC : à l'ouverture, la copie du set joue, que la
  Library n'ait rien en USR-05 ou y ait autre chose. Désinstaller une Machine de la Library ne retire rien aux sets.
- **Chaque instance joue sa propre copie**, prise quand elle installe ou charge. Installer dans une instance écrit la
  Library et cette instance seulement ; les autres gardent leur copie jusqu'à ce qu'elles chargent un Kit du Bank, un
  set, ou choisissent cette Machine. Rien ne change de son pendant que Live joue.
- **Un Kit du Bank** ne garde que les numéros de ses Machines : chargé, il joue ce que la Library a sous ces numéros
  (muet si rien).
- **La même règle vaut pour les Samples** : reportée dans le brouillard de la carte des Samples
  (`.scratch/md-drums-samples/map.md`), où ses tickets la reprendront.
