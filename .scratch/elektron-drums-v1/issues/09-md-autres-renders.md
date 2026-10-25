# Machinedrum : EFM, E12, P-I, GND et les autres renders

Type: AFK
Status: ready-for-agent
Assignee: 
Blocked by: 08

## Quoi

Les renders restants des 39 à porter (hors INP et RAM-R), dans l'ordre EFM, E12, P-I, GND puis le reste, sur le
patron du ticket 08. P-I (rétroactions longues) reste en entiers : c'est la seule famille qui risque de s'entendre en
flottant.

## Critère de fin

- Chaque render bit-exact contre `mdEngine` (Q1) et « sonne pareil » 44,1/48.
- 16 voix MD au pire cas : coût mesuré.

## Références

Spécification §3.2. Ticket « Port du Machinedrum en C++ ».
