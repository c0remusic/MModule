# GRID et REPITCH du lecteur DT2

Type: AFK
Status: ready-for-agent
Assignee:
Blocked by: 04

## Quoi

MODE du lecteur DT2 gagne GRID et REPITCH, avec la sémantique du DT2. Vecteurs relevés à l'oracle d'abord (aucun
n'existe encore), puis port.

## Critère de fin

- Vecteurs GRID et REPITCH enregistrés hors dépôt ; le port les tient par les tests Q.
- Même son à 44,1 et à 48 kHz (tests Q de `research/19-qualite-audio.md`).
- Aucun set existant ne change de son.
- Coût mesuré sur machine calme et rapporté face à son guide (spécification v2, §3) ; le pire kit tient sous 15 %.
- Gate vert, nouveau test inscrit dans sa liste.

## Références

Spécification v2, §5.1 (`../elektron-drums/spec-v2.md`) ; ses tickets de décision dans `../lockstep-post-v1/issues/`.
