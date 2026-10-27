# Seuils Q3-Q4 validés à l'écoute

Type: HITL
Status: resolved
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

## Answer

Écouté et tranché par l'utilisateur le 2026-10-26, deux écoutes ABX en aveugle (52 paires, puis 20 dont 4 témoins),
pages et rendus faits par `stFastAbRender` hors du dépôt (`C:\dev\elektron-work\ab-05`, `ab-05-r2`).

- **Q3 : SNR global ≥ 70 dB et pire fenêtre de 10 ms ≥ 60 dB.** Un bruit constant s'entend à 60 dB global partout et
  jusqu'à 80 dB global sur le kick grave (pire fenêtre 49,5 dB) ; les vrais résidus de la réécriture (pire fenêtre
  64,5 à 81,5 dB) ne s'entendent pas.
- **Q4 gardé :** enveloppe 0,1 dB, hauteur 1 cent, prudents ; aucun seuil net à l'écoute.
- **Validés à l'oreille :** le jugement statistique de la zone chaotique et les cas Q15 excusés (44,1/48 « bizarres
  pareil » des deux côtés : le son de la machine à ces réglages).
- Conséquence dans le test : la classification « chaotique » suit le critère de null en vigueur (l'exact contre
  lui-même sous 70 dB global ou 60 dB local) ; TRANSIENT 95 y passe. Q3 est fatal ; le null Q4 reste non fatal, les
  Hits 4, 8, 10 (69,7 à 66,6 dB global, au-dessus de 60 local), 13 et 14 y étant sous les seuils.

Détail et chiffres : `../elektron-drums/research/19-qualite-audio.md` §11, « Seuils Q3-Q4 validés à l'écoute ».

### Révision de Q4, le 2026-10-27

Pendant le ticket 06 (PC CARBON), le balayage de chaque paramètre sur toutes ses valeurs (`stFastSweep`, résultats
hors du dépôt dans `C:\dev\elektron-work\scratch-arch\research\06-sweep`) a montré que le null et l'enveloppe RMS de
1 ms échouent sur des réglages de BD MODERN dont la décroissance du sweep s'écarte un peu du firmware : la phase
dérive au fil du Hit, le null s'effondre (de 30 à 6,7 dB sur les 50 premières ms), mais le son reste le même.
L'utilisateur a écouté en aveugle les 11 paires les plus mauvaises (les 8 pires valeurs du balayage, plus les Hits 10
et 12 de BD MODERN et le Hit 10 de PC CARBON) et n'a reconnu B que 5 fois sur les 9 paires auxquelles il a répondu,
soit le hasard.

Il a donc décidé que Q4, sur les Hits non chaotiques de toutes les machines, se juge sur deux mesures insensibles à la
phase, prises dès l'attaque du port exact, en plus de la hauteur à 1 cent qui ne change pas :

- l'enveloppe de Hilbert (module du signal analytique, lissé sur 5 ms) : le plus grand écart entre les deux,
  rapporté au pic du port exact, doit rester sous -39 dB ;
- le spectre à court terme : trames de Hann de 1024 échantillons, pas de 256, tiers d'octave ; là où le port exact
  est à moins de 30 dB de sa case la plus forte, aucune case ne doit s'écarter de plus de 1 dB.

Le null (global, pire fenêtre de 10 ms, pire tiers d'octave) et l'ancienne enveloppe de 1 ms sont désormais
affichés sans être fatals, dans Q4 comme dans la vérification déterministe à rétroaction nulle. La classification
« chaotique » ne change pas.

Les 11 paires écoutées, avec la réponse de l'utilisateur et les deux nouvelles mesures :

| Paire | Écoute | Enveloppe de Hilbert (dB sous le pic) | Cases de tiers d'octave (dB) | Null des 50 premières ms (dB) |
|---|---|---|---|---|
| bd-sweep-70 | juste | -40,4 | 1,94 | 14,1 |
| bd-sweep-99 | juste | -35,6 | 13,29 | 9,1 |
| bd-sweep-109 | faux | -34,0 | 14,59 | 6,7 |
| bd-sweep-79 | faux | -41,5 | 1,02 | 18,2 |
| bd-sweep-50 | pas de réponse | -40,8 | 1,00 | 17,7 |
| bd-sweep-51 | faux | -44,8 | 0,60 | 22,2 |
| bd-sweep-41 | juste | -43,6 | 2,18 | 20,0 |
| bd-sweep-69 | juste | -50,7 | 0,39 | 29,7 |
| bd-hit10 | faux | -46,4 | 0,77 | 16,7 |
| bd-hit12 | juste | -51,0 | 1,22 | 6,7 |
| pc-hit10 | pas de réponse | -76,6 | 0,06 | 85,2 |

Les mesures sont celles de la version C++ (`bench::envelopeAbs`, `bench::cellMax`), identiques à l'analyse en Python
du coordinateur sauf pour bd-hit10 : le Python trouvait -41,2 dB à 1996 ms, effet de bord de sa transformée de
Hilbert non complétée de zéros sur le fondu de fin du fichier. Plusieurs paires dépassent les nouveaux seuils sans
avoir été reconnues ; les seuils restent prudents.

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
