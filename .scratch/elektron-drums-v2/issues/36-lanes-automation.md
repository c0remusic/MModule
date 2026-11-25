# Des lanes de Lock qui se dessinent

Type: HITL
Status: done (essai dans Live à venir)
Assignee: Claude (session 2026-11-25)
Blocked by: aucun

## Quoi

Retour de test dans Live (2026-11-24) : les lanes d'automation sont très difficiles à travailler. Précisé le
2026-11-25 : les lanes de Lock de l'écran Seq.

Relevé dans les gestes (`buildSeq`, `grid`) :

1. Un clic sur une case vide posait un Lock à la valeur de base : la courbe ne bougeait pas.
2. Un glisser vertical sur une case sans Lock ne faisait rien.
3. Le réglage vertical était relatif (1/32 de la plage par mm), pas à la hauteur pointée.
4. Un glisser horizontal posait ou retirait des Locks à la base, sans tracer de valeur.

## Décidé (2026-11-25, recommandations acceptées par l'utilisateur)

- Lanes de valeur et de liste : l'appui pose le Lock du pas à la hauteur pointée (en haut la fin de la plage, comme
  la lane le dessine) ; un glisser pose chaque pas traversé à la hauteur du pointeur.
- Alt et glisser : retire les Locks traversés. Double-clic : retire un Lock (gardé). Le clic droit, pris par le menu
  contextuel de l'éditeur, n'efface pas.
- Shift : réglage relatif fin du pas pressé, 1/320 de la plage par mm.
- Rangée de trigs (VEL, ACCENT, SLIDE) et lane SAMPLE : inchangées.

## Critère de fin

- [x] `edDrumsEditorTest` : un clic à 3/4 pose 0,75, à 1/5 pose 0,2 sans trig ; un glisser à 1/4 sur cinq pas les
  pose à 0,25 ; Alt et glisser retire trois Locks, ceux d'à côté gardés ; Shift et 8 mm montent de 0,0225 ; le
  double-clic retire.
- [ ] Essai dans Live.
