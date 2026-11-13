# Interface : box de pas et page SEQ

Type: AFK
Status: resolved
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

## État (2026-11-13)

Audit des gestes de la liste du ticket « Interface façon Overbridge » (`issues/09-interface.md`, points tranchés du
2026-10-25), lus dans la forme que leur donnent la spec §10 et `concepts/t-ui-spec.md` (souris seule, sans modificateur
ni clic droit ; mode TRIG fondu dans VEL ; Ctrl et Maj remplacés par les bandes sous les pas ; Alt remplacé par le lock
sans note : un lock posé sur un pas sans trig). L'état « avant » vient du code (`edDrumsEditor.cpp`,
`edDrumsEditorScreen.cpp`) et de `edDrumsEditorTest.cpp`, pas du texte du ticket. Aucun geste ne manquait : tous étaient
faits, la plupart sans test. Chaque test joue le geste à la souris et lit l'état du pas dans le pattern du séquenceur,
celui que `Processor::applyEvent` joue (`stepGestures`, `liveGestures`).

| Geste (ticket 09) | Forme Lockstep | Avant | Maintenant |
|---|---|---|---|
| Modes Trig, Vel, Accent, Slide | VEL, ACCENT, SLIDE dans la tête TRIGS | fait, non testé | testé |
| Clic pose ou retire un trig | touche de pas ; rangée TRIGS en VEL, à 127 ; Overview | fait ; touche et Overview testés | rangée TRIGS testée |
| Clic bascule accent, slide | rangée TRIGS en ACCENT, SLIDE, le trig gardé | fait, non testé | testé |
| Retirer un trig retire ses locks | spec 7 | fait, non testé | testé |
| Ctrl et Maj sélectionnent | appui sur une bande, glisser sur les bandes en prend plusieurs, nouvel appui retire le pas | fait ; une bande testée | glisser et retrait testés |
| Glisser horizontal peint | trigs, accents, slides ; efface s'il part d'un pas allumé | fait, non testé | testé dans les trois modes |
| Glisser vertical règle la valeur | vélocité d'un trig, 1/32 par mm ; rien sur un pas vide | fait, non testé | testé |
| Knob tourné sur un pas vide sélectionné : trig avec lock | cellule glissée en édition de lock : un lock sur chaque pas choisi, sans créer de trig | fait ; un pas à trig testé | plusieurs pas et pas sans trig testés |
| Alt : lock sans note | même geste, sur un pas sans trig | fait, non testé | testé |
| Liste des locks de la sélection, éditable, croix | lanes de Detail : clic pose à la base, clic sur un lock ne fait rien, glisser vertical règle, horizontal peint ou efface, double clic retire ; double clic sur une cellule de Sound retire le lock des pas choisis ; X de lane | fait ; X testé | tout testé |
| Mode LIVE | bouton LIVE de Detail ; transport joué, une cellule glissée devient le lock du pas joué | fait ; enregistrement testé dans `edDrumsProcessorTest` | bouton et geste testés dans l'éditeur |
| Lignes de locks de la page SEQ | lanes, + LOCK, LANES, X | fait, testé (État du 2026-11-12, `lanesAsPlayed`) | inchangé |

Seul ajout au code de l'éditeur : des accesseurs pour les tests, `SeqButton::Live`, `Vel`, `Accent`, `Slide` et
`trigRowArea`, sans changer aucun geste. Forme choisie là où la maquette n'en donne pas : aucune ; le mode ACCENT ou
SLIDE pose son drapeau sur un pas sans trig, comme le code le faisait déjà, et le trig posé ensuite le garde. Un glissé ne
compte qu'à partir de 0,8 mm (`Editor::mouseDrag`) : 8 mm vers le haut sur un lock le montent de 0,225, pas 0,25.

Critère de fin atteint : chaque geste a son test d'éditeur, qui passe. Une image sans changement ne repeint rien sur
aucune page.
