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

## Correctif du 2026-10-20

L'utilisateur, dans Live : « il faut parfois changer de banque dans les user machine pour entendre », avec des clics, du
silence, une hauteur ou une durée fausses, de la saturation. Cause : un autre paquet au numéro que joue la Track (ce que
ce ticket fait quand la Track joue seule son numéro) réécrivait le code et les tables d'appel, mais la voix gardait son
type actif (Y:`$153` + t) ; au Hit suivant le dispatcher, qui n'appelle INIT que pour un type différent, n'appelait que
TRIG, et le nouveau code tournait sur l'état privé de l'ancien paquet. Prendre une autre Machine puis revenir relançait
INIT. Désormais `VoiceEngine::forgetUserMachine` donne aux Tracks qui jouaient ce numéro le type de la Machine vide,
muettes jusqu'à leur Hit suivant, qui appelle INIT, comme le dit la Standard MDS (« A replaced neighbour machine's track
stays silent until the new machine is triggered ») ; retirer un paquet fait de même. `mdDrumsEngineTest` : remplacé
pendant que la Track 3 le joue, muet jusqu'au Hit, puis la voix du paquet nu à l'échantillon près ; sans le correctif,
la Track jouait encore.
