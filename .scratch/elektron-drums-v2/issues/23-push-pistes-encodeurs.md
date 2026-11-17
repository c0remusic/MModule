# Compagnon Push : pads des pistes et relais des encodeurs

Type: AFK puis HITL
Status: ready-for-agent
Assignee:
Blocked by: 22

## Quoi

Une rangée de pads montre les 16 pistes : sélection de la piste affichée, mute. Sans pas tenu, les encodeurs
restent les relais, que le compagnon relaie lui-même.

## Critère de fin

- Test UDP : sélection et mute reçus et rendus dans l'état.
- Essai de l'utilisateur sur Push.
- Gate vert.

## Références

Spécification v2, §8 (`../elektron-drums/spec-v2.md`) ; ses tickets de décision dans `../lockstep-post-v1/issues/`.
