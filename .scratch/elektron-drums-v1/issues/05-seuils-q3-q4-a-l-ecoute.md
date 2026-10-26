# Seuils Q3-Q4 validés à l'écoute

Type: HITL
Status: ready-for-human
Assignee: 
Blocked by: 04

## Quoi

Les seuils Q3 (SNR ≥ 60 dB, tiers d'octave sous -60 dB) et Q4 (enveloppe ≤ 0,1 dB, hauteur ≤ 1 cent) jugent toute
réécriture en flottant ; ils sont proposés, pas validés. Préparer des paires A/B aveugles (port exact contre
réécriture, et réécritures dégradées exprès autour du seuil) sur la machine du ticket 04, que l'utilisateur écoute.
Ajuster les seuils à ce qu'il entend.

## Critère de fin

- L'utilisateur a écouté les paires et tranché ; seuils retenus écrits dans la recherche « Qualité audio » §11 et
  dans les tests.

## Références

Spécification §11.3, §14. `../elektron-drums/research/19-qualite-audio.md` §11.

## Comments

2026-10-26, depuis le ticket 04 (BD MODERN). Décisions de l'utilisateur à faire juger aussi à l'écoute, et cas que
`stFastBdModernTest` excuse :

- Zone chaotique (TRANSIENT au-delà de ~75, rétroaction forte de l'unité 0) : jugée sur les distributions (spectre
  par tiers d'octave, enveloppe 10 ms), dans 2 fois la dispersion de l'exact contre lui-même décalé d'1 LSB + 0,5 dB.
- Q15 ne juge le spectre que sous 10 kHz ; une bande où le 48 kHz exact s'écarte du 96 kHz de plus de 0,5 dB est
  rapportée, pas jugée.
- Un écart Q15 que le rendu sans l'unité 0 fait disparaître est excusé, s'il ne dépasse pas 3 fois son seuil.
- Un Hit dont la bande de Carson dépasse la moitié de 44,1 kHz (repliement FM) a son spectre Q15 rapporté, pas jugé.
- Cas excusés, à écouter : Hit 9 (distribution chaotique, bande 31 Hz, 1,34 dB) ; Hit 11 (enveloppe, 0,28 dB) ;
  Hit 12 (spectre entier, repliement FM, unité 2, bord de Carson 48,2 kHz, pire bande 4,64 dB ; enveloppe 0,40 dB) ;
  Hit 15 (distribution chaotique, bande 1 260 Hz, 0,57 dB). Le null de Hit 13 (20,1 dB, verrouillage de période 2 à
  Nyquist) reste non fatal.
