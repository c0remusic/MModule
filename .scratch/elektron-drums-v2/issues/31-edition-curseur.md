# L'édition au curseur suit la souris

Type: HITL
Status: done (essai dans Live à venir)
Assignee: Claude (session 2026-11-25)
Blocked by: aucun

## Quoi

Retour de test dans Live (2026-11-24) : modifier des valeurs au curseur est parfois bizarre.

Relevé : un potard se réglait en glissant horizontalement (`Editor::cellDrag`), et s'éloigner verticalement de plus
de 6 mm passait en mode fin (gain jusqu'à 0,1). Un geste vertical sur un potard, habituel dans les plug-ins, ne
bougeait presque rien. Les faders se règlent verticalement, les listes horizontalement par pas de 4 mm.

## Décidé (2026-11-25, recommandation acceptée par l'utilisateur)

- Potards et listes : vers le haut ou la droite monte, vers le bas ou la gauche descend. Le sens le plus long du
  geste jusque-là compte, pour qu'un geste vertical qui dérive de côté ne s'annule pas.
- Shift : dix fois plus fin (quatre fois pour une liste). Le mode fin à la distance verticale disparaît.
- Faders inchangés.

## Critère de fin

- [x] `edDrumsEditorTest` : un potard tiré vers le haut monte autant que vers la droite, vers le bas descend autant,
  avec Shift d'un dixième, et un geste vers le haut qui dérive de côté comme un droit.
- [ ] Essai dans Live.
