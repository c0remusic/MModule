# Couche sample par piste

Type: AFK puis HITL (écoute)
Status: ready-for-agent
Assignee:
Blocked by: 04

## Quoi

Une couche sample par piste, sur toute piste, machine Rytm comprise, lue par le lecteur DT2 en ONESHOT et ses
quatre modes de PLAY, sommée à la machine avant les drives pré, puis toute la chaîne. Réglages du layering de la
planification v1 : niveau, accord et fin, début, longueur, boucle, sens, enveloppe (attaque, maintien, déclin), décalage
de 0 à 100 ms, suivi de note ; retrig avec rampe anti-clic ; tout verrouillable et modulable. Éteinte par défaut et
sans coût. Sa propre BR, sur le sample seul, avant LEV, à la loi du Rytm : pas de quantification continu, environ
9,9 − 0,073 × BR bits équivalents, BR 0 contourné.

## Critère de fin

- Couche éteinte : aucun échantillon changé, aucun coût mesurable.
- Hits exacts : la couche sonne à `HitDelay` comme la machine, décalage compris ; latence et gigue rapportées avant et
  après.
- BR : bits équivalents mesurés sur une rampe, à ±0,5 bit de la loi.
- Coût visé : couche ≤ 1,5 % avec les 16 allumées (pire kit).
- Écoute de contrôle de la BR face à celle du DT2 (point ouvert de la spécification).
- Même son à 44,1 et à 48 kHz (tests Q de `research/19-qualite-audio.md`).
- Aucun set existant ne change de son.
- Coût mesuré sur machine calme et rapporté face à son guide (spécification v2, §3) ; le pire kit tient sous 15 %.
- Gate vert, nouveau test inscrit dans sa liste.

## Références

Spécification v2, §7 (`../elektron-drums/spec-v2.md`) ; ses tickets de décision dans `../lockstep-post-v1/issues/`.
