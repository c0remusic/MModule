# La chaîne à 4x sous 4 % d'un cœur

Type: AFK
Status: ready-for-agent
Assignee:
Blocked by: 01

## Quoi

Au garde-fou du ticket 01, la chaîne du pire kit coûte 5,86 % d'un cœur à 4x (guide 2,0 %), 2,76 % à 2x. Le pire kit
total mesure 13,71 % : la couche sample du ticket 06 (guide 1,5 %) le porterait à 15,2 %, au-dessus du plafond. Décidé
avec l'utilisateur le 2026-11-17 : le pire kit se juge à 4x, et la chaîne s'optimise avant la couche.

Chaque drive suréchantillonne seul (un `Oversampler` par type de drive) : deux drives par piste font deux paires
montée/descente sur 16 pistes. Leviers à mesurer, aucun prouvé :

- moins de sections all-pass dans le second étage du 4x, sous le test de qualité existant des drives ;
- un seul suréchantillonnage pour un segment de chaîne au lieu d'un par drive, si le son reste celui d'aujourd'hui.

Aucune dégradation automatique de qualité (spec v2, §3) : un levier qui change le son se mesure par les tests de
qualité des drives, et l'écoute tranche s'ils bougent.

## Critère de fin

- La chaîne du pire kit à 4x sous 4 % d'un cœur au garde-fou, le pire kit total sous 12 %.
- Les tests de qualité des drives et de l'oversampling passent sans seuil relâché.
- Gate vert.

## Références

Ticket 01 (`01-pire-kit-au-garde-fou.md`, sa réponse) ; spécification v2, §3.
