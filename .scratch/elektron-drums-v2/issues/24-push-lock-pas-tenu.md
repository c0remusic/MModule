# Compagnon Push : lock par pas tenu

Type: AFK puis HITL
Status: ready-for-agent
Assignee:
Blocked by: 23

## Quoi

Un pas tenu arme un lock que les encodeurs règlent ; le pas lâché, les encodeurs redeviennent les relais.

Décidé au ticket 23 (2026-11-28) : sans pas tenu, les encodeurs restent à Live, sur les relais, pour que l'écran de
Push nomme ce qu'ils règlent. Le compagnon prend `Track_Controls` le temps qu'un pas est tenu et les rend au
relâchement ; vérifier sur Push que la prise et le rendu à chaque pas tenu sont assez vifs.

## Critère de fin

- Test UDP : lock posé sur le pas tenu, joué au pas suivant à l'échantillon près.
- Essai de l'utilisateur sur Push.
- Gate vert.

## Références

Spécification v2, §8 (`../elektron-drums/spec-v2.md`) ; ses tickets de décision dans `../lockstep-post-v1/issues/`.
