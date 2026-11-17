# Compagnon Push : lock par pas tenu

Type: AFK puis HITL
Status: ready-for-agent
Assignee:
Blocked by: 23

## Quoi

Un pas tenu arme un lock que les encodeurs règlent ; le pas lâché, les encodeurs redeviennent les relais.

## Critère de fin

- Test UDP : lock posé sur le pas tenu, joué au pas suivant à l'échantillon près.
- Essai de l'utilisateur sur Push.
- Gate vert.

## Références

Spécification v2, §8 (`../elektron-drums/spec-v2.md`) ; ses tickets de décision dans `../lockstep-post-v1/issues/`.
