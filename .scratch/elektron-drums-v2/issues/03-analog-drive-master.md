# ANALOG ≈ au master

Type: AFK puis HITL (écoute)
Status: ready-for-agent
Assignee:
Blocked by: 02

## Quoi

L'étage de saturation du master gagne l'option ANALOG ≈, à côté de l'overdrive du DT2 (défaut) : le modèle du
ticket 02 en stéréo, un DRIVE pour les deux canaux. La page du master ne montre que les réglages du modèle choisi.

## Critère de fin

- Les tests mesurés du ticket 02, sur les deux canaux ; les deux canaux identiques pour une entrée mono.
- Coût visé : 0,2 % d'un cœur.
- Même son à 44,1 et à 48 kHz (tests Q de `research/19-qualite-audio.md`).
- Aucun set existant ne change de son.
- Coût mesuré sur machine calme et rapporté face à son guide (spécification v2, §3) ; le pire kit tient sous 15 %.
- Gate vert, nouveau test inscrit dans sa liste.

## Références

Spécification v2, §4 (`../elektron-drums/spec-v2.md`) ; ses tickets de décision dans `../lockstep-post-v1/issues/`.
