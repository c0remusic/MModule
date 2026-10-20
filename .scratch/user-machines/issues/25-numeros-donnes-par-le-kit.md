# Les numéros USR donnés par le Kit

Type: task
Status: resolved
Assignee: c0remusic (session du 2026-10-20)
Blocked by: 24

## Question

Choisir une Machine de la Library pour une Track lui donne un numéro du Kit joué, USR-01 à USR-16, sans que
l'utilisateur en choisisse un : le numéro qui la tient déjà, sinon celui que la Track jouait si aucune autre Track ne le
joue, sinon le premier qu'aucune Track ne joue. L'instance y installe sa copie, la Track y passe par ASSIGN MACHINE (ses
24 paramètres par défaut du paquet). Un Kit a seize Tracks : un numéro libre existe toujours.

L'import d'un fichier pour la Track montrée fait de même. Les seize cases USR du navigateur montrent les numéros du Kit
joué (le nom du paquet que l'instance tient, vides grisées) ; un clic y met la Track. « Une Track sur un USR sans copie
prend celle de la Library » disparaît : un numéro n'a plus de sens hors de son Kit. Le set garde les paquets de son Kit
comme aujourd'hui.

Tests : une Machine choisie sur une Track prend USR-01, la même sur une autre Track le partage, une autre Machine sur la
première Track remplace USR-01 si elle seule le jouait, sinon prend USR-02 ; seize Machines différentes sur seize
Tracks ; le choix envoie ASSIGN MACHINE et l'hôte relit les paramètres.

## Answer

Construit le 2026-10-20 :

- `Processor::chooseUserMachine(track, paquet)` : le numéro qui le tient, sinon celui de la Track si elle seule le
  joue, sinon le premier qu'aucune Track ne joue, vide (ni paquet ni paquet gardé, ticket 23) d'abord. L'instance y prend
  sa copie (`installPackage`, sous le verrou du Device), la Track y passe par son paramètre Machine, donc par ASSIGN
  MACHINE ; à son propre numéro, un autre paquet renvoie ASSIGN MACHINE pour les défauts du nouveau. Le set est marqué
  modifié quand la copie change.
- L'import d'un fichier pour la Track montrée passe par là : « TX8-BD on USR-01, Track 3 ».
- Les seize cases USR du navigateur s'allument là où l'instance tient une Machine ; un clic met la Track sur ce numéro,
  sans rien prendre de la Library. Les noms dans les cases attendent « Le menu secondaire des Machines utilisateur »,
  qui montre les noms : les cases gardent leurs numéros d'ici là.
- « Une Track sur un USR sans copie prend celle de la Library » (`takeUserMachineIfAbsent`) et « un Kit du Bank prend
  les Machines de la Library » (`takeUserMachines`) sont retirés : un Kit du Bank joue ce que l'instance tient à ses
  numéros, jusqu'au ticket « Un Kit du Bank se souvient de ses Machines utilisateur ».
- Vérifié : `mdDrumsProcessorTest` (« imports » : USR-01 sur la Track 3, le même paquet sur la Track 6 le partage, un
  autre sur la Track 3 prend USR-02, un autre encore la remplace en place, seize Machines sur seize Tracks donnent seize
  numéros qui tiennent chacun celle de sa Track ; « User Machines » : une Track sur un numéro vide ne prend rien),
  `mdDrumsSkinTest` (le lâcher d'un `.mds` met la Track 1 sur USR-01, la case allumée).
