# Machinedrum : harnais contre mdEngine et inventaire des tables

Type: AFK
Status: ready-for-agent
Assignee: 
Blocked by: 01

## Quoi

Un harnais qui rend une voix MD portée en C++ et la même voix dans `mdEngine` (oracle), Hit par Hit, et compare
échantillon par échantillon à 44,1 kHz. Une machine factice (GND) le traverse pour le prouver. En même temps,
inventorier les tables que lisent DSP1, DSP2 et l'OS : pour chacune, sa taille, son usage, et si une formule la
régénère au mot près (précédent : le sinus du DSP mixeur). Musashi ne se lie qu'à un seul CPU par exécutable : le
harnais tourne dans son propre processus.

## Critère de fin

- Le harnais passe sur GND et échoue si un échantillon est faussé exprès.
- `research/` de cet effort contient l'inventaire, chaque table marquée « formule trouvée », « à chercher » ou
  « bloquante ».

## Références

Spécification §3.2, §14. Tickets « Port du Machinedrum en C++ », « Instrument sans firmware ».
