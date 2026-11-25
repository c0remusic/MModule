# Un LFO module l'autre

Type: HITL
Status: done (essai dans Live à venir)
Assignee: Claude (session 2026-11-25)
Blocked by: aucun

## Quoi

Retour de test dans Live (2026-11-24) : on ne peut pas moduler les LFO entre eux.

## Décidé (2026-11-25, recommandations acceptées par l'utilisateur)

- Destinations : la vitesse et la profondeur du LFO 2. Deux cibles ajoutées en fin d'énumération (`Lfo2Speed`,
  `Lfo2Depth`), identifiants figés.
- Sens : le LFO 1 module le LFO 2 de sa piste, jamais l'inverse. Le LFO 1 est calculé avant le LFO 2 dans le bloc :
  pas de boucle, pas de bloc de retard.
- Piste : la sienne seule. Sur ces deux cibles, ON TRACK ne compte pas et n'est pas affiché.
- Menu : les deux cibles en fin de liste du TARGET du LFO 1 (LFO 2 SPEED, LFO 2 DEPTH) ; celui du LFO 2 ne change pas.
  Le LFO 2 qui les vise (par l'hôte) ne fait rien.
- Vitesse : à profondeur 127, de ÷4 à ×4, exponentielle (2^(2 x sortie x profondeur / 127)). Un LFO 2 calé sur le
  transport (SYNC en FREE ou HOLD) cumule ses cycles depuis sa phase tant que sa vitesse est modulée, et reprend le calage
  sur la position dès que rien ne la module. En HZ et dans les modes qui relancent, le facteur multiplie la vitesse.
- Profondeur : la sortie du LFO 1 s'ajoute à la profondeur du LFO 2, bornée à ±127, comme sur les autres cibles. Un LFO 2
  de profondeur 0 tourne si son TARGET est posé et que le LFO 1 module sa profondeur.

## Critère de fin

- [x] `edDrumsLfoTest` : LFO 1 carré à profondeur 127 sur LFO2 SPEED : le LFO 2 avance 4 fois plus vite sur la moitié
  haute, 4 fois moins sur la basse ; un LFO 2 SYNC FREE modulé quitte la phase du transport, et la retrouve quand le LFO 1
  quitte la cible ; LFO2 DEPTH : profondeur 0 + LFO 1 à 64 donne la sortie du LFO 2 à 64 ; somme bornée à 127 ; un LFO 2
  sur LFO2 SPEED ne bouge rien ; ON TRACK d'une autre piste ignoré.
- [x] `edDrumsEditorTest` : le TARGET du LFO 1 liste LFO 2 SPEED et LFO 2 DEPTH, celui du LFO 2 non ; ON TRACK caché sur
  ces cibles.
- [ ] Essai dans Live. À écouter : quand le LFO 1 cesse de moduler la vitesse (profondeur à 0, autre cible), un LFO 2
  calé sur le transport saute en un bloc à la phase du transport (décidé, Q5 a).
