# Syntakt : tables régénérées par formule

Type: AFK
Status: resolved
Assignee: 
Blocked by: 01

## Quoi

Le port du Syntakt lit ~183 Ko de la section 7 et ~26 Ko de la section 3 du firmware : ~175 tables, 505
descripteurs, 143 mots d'état SRAM. Cinq tables ont déjà leur formule. Régénérer les autres famille par famille,
chacune testée au mot près contre le firmware ; remplacer descripteurs, défauts et état SRAM par des données à nous.
Une table sans formule est signalée : sa machine reste bloquée ou devient approximative et marquée.

**Amendé le 2026-10-26 avec l'utilisateur :** seul le Syntakt livré, la réécriture rapide `stFast`, doit tourner
sans firmware. Le port exact `stEngine` reste la référence de test et garde la section 7. Le ticket régénère donc les
tables que `stFast` lit : aujourd'hui les onze de BD MODERN (neuf tables de paramètres, l'onde de l'unité 2, l'exp2),
puis celles que chaque machine du ticket 06 apporte, par formule dès son arrivée. Le ticket se ferme quand `stFast`
ne lit plus aucun octet du firmware.

## Critère de fin

- Le rendu du Syntakt ne lit plus aucun octet du firmware (test : firmware absent, mêmes échantillons).
- Chaque table régénérée a son test contre le firmware.

## Références

Spécification §3.1, §11.4. Ticket « Instrument sans firmware chez l'utilisateur ».

## Answer

Fait le 2026-10-26 pour ce que `stFast` lit, BD MODERN : `syntakt::fast::BdModern(float sampleRate)` ne prend plus
de section 7, et `stFast` ne lie plus `stEngine` (seuls ses tests le lient, pour la référence exacte). Les onze tables
viennent de `stFastTables.*`, chacune comparée mot à mot à la section 7 par `stFastTablesTest` :

- Exactes : exp2 (`round(2^(i/2047)·2^30)`, la dernière saturée), l'onde de l'unité 2 (cinq harmoniques, sinus
  128 : 22 : 11 : 7 : 3, arrondi vers le bas en 16 bits), ModTarget, Feedback, LevelU2 (valeurs à six décimales mises
  en Q31).
- Approchées, courbes de conception : SweepDepth (1 LSB), AmpDecay (5 LSB), ClickLevel (0,03 % de pleine échelle),
  ModDecay (0,5 % de la constante de temps), LevelU0 (0,12 %), SweepDecay (1,24 % sur onze entrées de transition). Les
  décroissances sont une constante de temps `KnotTau` = 1,15142326 fois des valeurs rondes du concepteur (SweepDecay :
  12, 12, 18, 25, 30, 46, 52, 64, 48, 40, 25,4, 19,2, 15 tous les dix pas ; ModDecay : 400 et 3).
- Accordé par l'utilisateur : nœuds ronds et tronçons ajustés entrent dans le dépôt ; aucun mot du firmware n'est
  recopié.
- `stFastBdModernTest` passe : Q4 inchangé sauf l'enveloppe des Hits 13 (0,008 à 0,041 dB), 9 et 3, par LevelU0.

Les machines du ticket 06 apportent leurs tables par formule dès leur arrivée, chacune testée de même. Exactitude
vérifiée avec le runtime de MSVC ; une autre libm peut arrondir un mot à 1 LSB près.
