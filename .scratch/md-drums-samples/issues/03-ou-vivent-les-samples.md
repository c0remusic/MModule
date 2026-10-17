# Où vivent les Samples, et comment ils voyagent

Type: grilling
Status: resolved
Assignee: c0remusic (session du 2026-10-17)
Blocked by: 02

## Question

Où les Samples de l'utilisateur sont gardés et comment ils suivent un set : dans l'état du plug-in (le set Live les
porte, il grossit d'autant, jusqu'à 20 s de Samples), dans le dossier data de MD Drums avec une référence dans l'état
(le set reste léger, il dépend du disque), ou dans une banque partagée par toutes les instances comme `Bank.syx` pour
les Kits. Et si un Kit porte ses Samples : sur le vrai Machinedrum, non (les Samples sont globaux, un Kit ne nomme que
sa Machine ROM-NN).

## Answer

Décidé pour les Machines utilisateur le 2026-10-17
([Où vivent les Machines utilisateur installées](../../user-machines/issues/07-ou-vivent-les-machines.md)), et voulu
pareil ici par l'utilisateur :

- Une bibliothèque des Samples de l'utilisateur dans le dossier de données de MoDule, partagée par les instances comme
  le Bank, un fichier par Machine ROM.
- L'état du set embarque les Samples que son Kit joue : le set sonne comme enregistré sur un PC qui ne les a pas.
- Chaque instance joue sa copie, jusqu'à ce qu'elle charge un Kit du Bank, un set, ou qu'on lui lâche un Sample.
- Un Kit ne porte pas ses Samples dans le Bank, comme sur la machine : il nomme sa Machine ROM-NN.
