# Un Kit du Bank se souvient de ses Machines utilisateur

Type: task
Status: open
Assignee:
Blocked by: 25

## Question

« Un Kit du Bank ne garde que les numéros de ses Machines » (ticket « Où vivent les Machines utilisateur installées »)
ne tient plus quand les numéros sont ceux du Kit : sauver un Kit dans le Bank doit garder, pour chaque numéro USR que ses
Tracks jouent, quel paquet de la Library il désigne, et le charger doit les remettre dans l'instance. À côté de
`Bank.syx`, qui reste 64 dumps qu'une Machinedrum lit ; écrit un Slot à la fois et relu comme lui, pour que deux
instances gardent chacune les leurs. Un paquet que la Library n'a plus : son numéro joue muet, la ligne d'état le dit.
Import et Export `.syx` : sans eux, comme X.20 quand le snapshot quitte sa Machine Bank.

Tests : un Kit sauvé avec TX8-BD en USR-01 et NFX-D1 en USR-02, rechargé après d'autres choix, rejoue les deux ; le
Bank relu par une autre instance ; un paquet retiré de la Library.
