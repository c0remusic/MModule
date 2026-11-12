# Interface : box de pas et page SEQ

Type: AFK
Status: ready-for-agent
Assignee: 
Blocked by: 17, 22

## Quoi

La box de pas en bas de chaque page de piste et la page SEQ du mockup v12 : modes Trig, Vel, Accent, Slide ; clic
pose ou retire un trig ; Ctrl et Maj sélectionnent ; glisser horizontal peint, vertical règle la valeur ; knob
tourné sur un pas vide sélectionné crée un trig portant le lock, Alt un lock sans note ; liste des locks de la
sélection, éditable, croix pour retirer ; mode LIVE ; lignes de locks de la page SEQ.

## Critère de fin

- Chaque geste du ticket « Interface façon Overbridge » joué par un test d'éditeur produit l'état de pas attendu.

## Références

Spécification §7, §10. Tickets « Interface façon Overbridge », « Séquenceur interne ».

## État (2026-11-12)

Seq Detail reprend de la variante 2 de `u.html` ce que liste la note `concepts/u-html-synthese-v19.md`, sur la
disposition approuvée : la ligne STEP reste en bas, l'inspecteur vertical de la maquette n'est pas repris. En-tête
« SEQ A01 », et à droite « CHAIN n/m » quand une chaîne joue, rien sinon ; « 1–16 / longueur » à côté de TRIGS ; les
marques des trigs (condition, ratchet, slide, micro) dessinées dans les pas, avec leur légende sous VEL.

Les lanes de locks sont dessinées en paliers comme le séquenceur les joue, vérifié dans `edDrumsSequencer.cpp` et
`Processor::applyEvent` et non supposé comme dans la maquette : un lock tient jusqu'au prochain événement de la piste,
trig ou lock, puis la base revient ; un trig avec SLIDE fait glisser ce qu'il verrouille jusqu'au lock du trig suivant,
ou à la base, sur tous les pas entre les deux. Remplissage depuis la ligne de base pointillée, points sur les pas
verrouillés ; une liste (TYPE…) montre une pastille par lock et la base au début de chaque suite de pas sans lock. Cadre
de lane (bandes de 4 pas, séparations fines, contour), colonne claire du pas choisi à travers les lanes.

Colonne des noms : nom, bloc, « BASE … », et une croix encadrée. Sur une lane qui a des locks, le premier clic arme
(« DELETE n LOCKS? »), le second supprime, tout autre clic désarme. Correction au passage : avant, le clic de
confirmation réarmait au lieu de supprimer, parce que l'appui remettait l'armement à zéro avant le clic. Sans lane,
trois lanes vides pointillées ouvrent + LOCK. Au-delà de cinq lanes, un bouton « LANES a–b / n » tourne la page.
+ LOCK ouvre un menu sur toute la largeur, une colonne par bloc (VOICE sur deux), avec tous les paramètres qui prennent
un lock (Machine 1-24 que la machine utilise) ; ceux qui ont déjà une lane sont grisés et inertes, un clic à côté ferme.

Tests de `edDrumsEditorTest` : lanes vides, menu (76 paramètres sur 76), ajout, pagination, croix avec confirmation et
désarmement ; tout passe. Rendus `seq*.png` et `seq-1280.png`. Coût d'un rafraîchissement complet de Seq : 6,0 ms
avant, 6,4 à 6,6 ms après ; une seule mesure avant, et l'écart entre deux mesures du même code atteint 0,4 ms.
