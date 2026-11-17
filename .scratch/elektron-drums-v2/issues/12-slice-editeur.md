# SLICE du lecteur DT2 et son éditeur

Type: AFK puis HITL (éditeur)
Status: ready-for-agent
Assignee:
Blocked by: 04

## Quoi

MODE du lecteur DT2 gagne SLICE, avec l'éditeur de slices. Vecteurs relevés à l'oracle d'abord, puis port, puis
l'éditeur.

## Critère de fin

- Vecteurs SLICE enregistrés hors dépôt ; le port les tient par les tests Q.
- Test d'éditeur : slices posées, déplacées, retirées, gardées dans l'état.
- Même son à 44,1 et à 48 kHz (tests Q de `research/19-qualite-audio.md`).
- Aucun set existant ne change de son.
- Coût mesuré sur machine calme et rapporté face à son guide (spécification v2, §3) ; le pire kit tient sous 15 %.
- Gate vert, nouveau test inscrit dans sa liste.

## Références

Spécification v2, §5.1 (`../elektron-drums/spec-v2.md`) ; ses tickets de décision dans `../lockstep-post-v1/issues/`.
