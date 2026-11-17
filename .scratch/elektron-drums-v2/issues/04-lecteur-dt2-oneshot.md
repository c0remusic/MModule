# Lecteur DT2 du Sampler en ONESHOT

Type: AFK
Status: ready-for-agent
Assignee:
Blocked by: 01

## Quoi

Le Sampler gagne un troisième lecteur, DT2, à côté de 12-BIT et VINTAGE qui restent tels quels. GRAIN (Machine 9)
devient un choix de trois ; un set existant migre à la lecture (0-63 : 12-BIT, 64-127 : VINTAGE). Avec le lecteur DT2,
MODE vaut ONESHOT dans ce ticket ; STRT + LEN, LOOP, PLAY à quatre modes, TUNE ±5 octaves. Mono (un sample stéréo est
sommé), 48 kHz puis `fast::Host`. Lois de l'oracle : interpolation polyphasée à 6 prises et 256 phases, 64 échantillons
à 96 kHz puis décimation 2:1 récursive ; LEV = 2 × (LEV/128)² × courbe de vélocité, rampé sur le bloc ; hauteur
2^(((TUNE − 64) + (note − 60))/12), bornée à ±64 demi-tons. La page ne montre que les réglages du lecteur choisi.

D'abord : relever à l'oracle (`elektron-work\tools\dt2\dt2_voice_oracle.py`) l'ordre des valeurs de PLAY.

## Critère de fin

- Jugé contre les vecteurs ONESHOT de `vectors-dt2` par les tests Q.
- Un set v1 relu garde son lecteur et son son.
- Même son à 44,1 et à 48 kHz (tests Q de `research/19-qualite-audio.md`).
- Aucun set existant ne change de son.
- Coût mesuré sur machine calme et rapporté face à son guide (spécification v2, §3) ; le pire kit tient sous 15 %.
- Gate vert, nouveau test inscrit dans sa liste.

## Références

Spécification v2, §5.1 (`../elektron-drums/spec-v2.md`) ; ses tickets de décision dans `../lockstep-post-v1/issues/`.
