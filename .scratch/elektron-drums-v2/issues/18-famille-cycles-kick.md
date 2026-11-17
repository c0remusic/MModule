# Famille Cycles avec KICK seul

Type: AFK
Status: ready-for-agent
Assignee:
Blocked by: 17

## Quoi

Troisième famille, Cycles, à côté de Syntakt et MD, avec sa première machine, KICK. PTCH, DEC, COLR, SHPE, SWEP,
CONT dans Machine 1-6, PUNCH et GATE dans Machine 7-8 ; plages et défauts des fiches de paramètres de l'OS 1.13. Chaîne
par défaut de la famille : Cycles dist en post, pas de filtre. Vélocité : gain linéaire après la voix. GATE allumé : la
voix tient tant que la note tient (note-off de l'hôte ou durée du trig). Les deux LFO de Lockstep ; le LFO du Cycles
n'est pas porté. Port SIMD, 16 pistes sur les voies, 48 kHz puis `fast::Host`.

## Critère de fin

- Port jugé contre les vecteurs KICK de `vectors-cycles` par les tests Q.
- GATE : la voix tient et se relâche à la note-off, à l'échantillon près.
- Hits exacts, latence et gigue inchangées.
- Coût visé : ≈ 0,1 % d'un cœur par piste active.
- Même son à 44,1 et à 48 kHz (tests Q de `research/19-qualite-audio.md`).
- Aucun set existant ne change de son.
- Coût mesuré sur machine calme et rapporté face à son guide (spécification v2, §3) ; le pire kit tient sous 15 %.
- Gate vert, nouveau test inscrit dans sa liste.

## Références

Spécification v2, §6 (`../elektron-drums/spec-v2.md`) ; ses tickets de décision dans `../lockstep-post-v1/issues/`.
