# STRETCH du lecteur DT2

Type: AFK
Status: ready-for-agent
Assignee:
Blocked by: 04

## Quoi

MODE du lecteur DT2 gagne STRETCH, rendu directement à 48 kHz : grains tous les 2 048 échantillons avec un fondu
de 128. Le facteur de niveau de STRETCH est relevé à l'oracle d'abord.

## Critère de fin

- Jugé contre les vecteurs STRETCH de `vectors-dt2` par les tests Q.
- Même son à 44,1 et à 48 kHz (tests Q de `research/19-qualite-audio.md`).
- Aucun set existant ne change de son.
- Coût mesuré sur machine calme et rapporté face à son guide (spécification v2, §3) ; le pire kit tient sous 15 %.
- Gate vert, nouveau test inscrit dans sa liste.

## Références

Spécification v2, §5.1 (`../elektron-drums/spec-v2.md`) ; ses tickets de décision dans `../lockstep-post-v1/issues/`.
