# Un Kit du Bank se souvient de ses Machines utilisateur

Type: task
Status: resolved
Assignee: c0remusic (session du 2026-10-20)
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

## Answer

Construit le 2026-10-20 :

- `Bank.machines` à côté de `Bank.syx` : pour chacun des 64 Slots, les seize numéros, chacun l'id du paquet
  (`Library::id`, un FNV-1a de 64 bits de ses octets, jamais 0) et son nom (16 octets), 384 octets par Slot. Écrit en
  place un Slot à la fois avant `Bank.syx`, relu avec lui (`Bank::machines`).
- Save, Save here : le Kit avec les Machines utilisateur que ses Tracks jouent (`Processor::kitMachines`). Rename les
  garde ; Delete et Import (un Kit écrit sans elles) les effacent.
- Load : les paquets du Slot pris dans la Library à leurs numéros (`Processor::takeUserMachines`), puis le Kit ; un
  paquet que la Library n'a plus laisse son numéro tel quel, et la ligne des Kits le dit (« Playing 64, P-I UW; the
  Library lacks TST-SW »).
- Save s'allume aussi quand un autre paquet est à un numéro que le Kit joue, ses octets inchangés.
- Vérifié : `mdDrumsBankTest` (gardées avec le Kit, lues par un autre Bank, effacées par un Kit écrit sans elles ou un
  Slot vidé, gardées quand un autre Bank écrit un autre Slot) ; `mdDrumsSkinTest` (Slot 64 sauvé avec USR-01, un autre
  paquet à sa place allume Save, le Kit rechargé reprend le sien, le paquet retiré de la Library est dit).
