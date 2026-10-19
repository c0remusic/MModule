# Garder dans le set une Machine utilisateur que MoDule ne joue pas

Type: grilling
Status: resolved
Assignee: c0remusic (session du 2026-10-19)
Blocked by:

## Question

Un set qui tient une Machine utilisateur refusée par cette build (NEIGHBOUR aujourd'hui) la perd sans un mot : la copie
de l'état est écartée à l'installation, et le prochain enregistrement ne l'a plus ([research/11](../research/11-mdm1-flags.md)).
Garder le paquet tel quel dans l'état, sans le jouer (une Track sur ce numéro joue le silence d'une Machine vide), pour
qu'une build qui le joue le retrouve ? Et le dire dans la ligne d'état ?

## Décision et construction

L'utilisateur, le 2026-10-19 : garder, jouer muet, le dire. Construit le même jour : `Device::setState` garde tel
quel un paquet que le DSP voix refuse (`Device::keptUserMachine`), retire celui qui jouait à son numéro, qui joue alors
la Machine vide ; `getState` le rend au prochain état ; une Machine utilisateur installée à ce numéro le laisse partir.
La ligne d'état le dit (« USR-05 NFX-RV plays nothing here: ... The set keeps it. »). `mdDrumsDeviceTest` (« kept
User Machines ») le vérifie. Le refus nomme STEREO PAIR quand c'est lui.
