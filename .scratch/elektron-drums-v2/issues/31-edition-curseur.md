# L'édition au curseur suit la souris

Type: HITL
Status: needs-info
Blocked by: aucun

## Quoi

Retour de test dans Live (2026-11-24) : modifier des valeurs au curseur est parfois bizarre.

À préciser avec l'utilisateur : quels contrôles (potards, cellules, listes), quel geste (glisser, molette,
double-clic), ce qui se passe (saut, vitesse, sens, valeur qui revient). Le correctif du ticket 28 peut en expliquer
une part : une valeur éditée revenait au Play.

Relevé (2026-11-24) : un potard se règle en glissant horizontalement (`Editor::cellDrag`), et s'éloigner
verticalement de plus de 6 mm passe en mode fin (gain jusqu'à 0,1). Les faders se règlent verticalement, les listes
horizontalement par pas de 4 mm. Un geste vertical sur un potard, habituel dans les plug-ins, ne bouge presque rien.
Cause probable, à confirmer avec l'utilisateur, ainsi que le geste voulu.

## Critère de fin

- Le geste décrit reproduit par le test de l'éditeur, puis corrigé.
