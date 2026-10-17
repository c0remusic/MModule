# Les 8 Machines intégrées d'X.20 dans 0.1.2 ?

Type: grilling
Status: out of scope (closed)
Assignee: c0remusic (session du 2026-10-17)
Blocked by:

## Question

« Décoder l'OS X.20 » estime à 3 à 5 semaines de sessions les 8 Machines intégrées qu'X.20 ajoute (GND-SN-PRO,
GND-SW, GND-PU, NFX-EV, NFX-CO, NFX-UC, INP-CA, INP-CB), lues dans l'OS X.20 de l'utilisateur. 0.1.2 les
attend-elles, toutes ou une partie (les 3 GND d'abord, les NFX ensuite ; INP-CA et INP-CB jouent muet sans entrée
audio), ou sortent-elles après 0.1.2, sur cette carte ou une autre ? Avec : ce que MoDule demande à l'utilisateur pour
elles (son `.syx` X.20, comme le flash image), et GND-SN-PRO, qui prend le numéro 1 de GND-SN. Le paramètre Machine
réserve déjà leurs numéros (4, 5, 7, 8, 9, 86, 87), nommés mais pas proposés : OS 1.63 ne les a pas
(`mdProtocol/mdmachines.cpp`).

## Answer

Choix de l'utilisateur (2026-10-17) : aucune dans 0.1.2. Les Machines utilisateur sont déjà le gros d'X.20, et 0.1.2
attend aussi les Samples et Push. Les 8 vont sur une carte à part, pour 0.1.3, partant de
[research/01-decoding-x20.md](../research/01-decoding-x20.md) : hors du périmètre de cette carte.
