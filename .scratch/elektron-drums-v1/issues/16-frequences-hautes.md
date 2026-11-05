# 88,2 et 96 kHz : rendu à mi-fréquence

Type: AFK
Status: ready-for-agent
Assignee: 
Blocked by: 11

## Quoi

À 88,2 et 96 kHz, rendre les moteurs à 44,1 ou 48 kHz et remonter par un suréchantillonneur 2x HIIR ; option
globale « 96 kHz natif » au double du coût, gardée dans l'état. 192 kHz marche sans être budgété.

Noté le 2026-10-27 depuis le ticket 06 : le Syntakt rend toujours à 48 kHz. À 96 kHz, le 2x HIIR suffit ; à 88,2 kHz,
le rééchantillonneur 48 → 44,1 kHz puis le 2x, ou un 48 → 88,2 kHz direct, à mesurer. L'option « 96 kHz natif » ne
s'applique pas au Syntakt.

## Critère de fin

- Q15 : un Hit à 44,1, 48 et 96 kHz, enveloppe et spectre dans les seuils.
- Pire cas 16 pistes sous 15 % à 88,2 et 96 kHz ; 192 kHz rend sans erreur.

## Références

Spécification §11.2. Ticket « Une fréquence commune ».

## Noté par le ticket 10 (2026-11-05)

Tout `md::fast` dépend de 44,1 kHz sauf `Srr`, qui prend sa fréquence (à SRR 0 il cesse alors d'être un bypass) :
- `Rate` dans `mdFastTables.cpp` porte les tables ; EQ et filtre ne sont exacts qu'à 44,1 kHz, et à 48 kHz ils liraient 8,8 % trop haut.
- Les mots de blocage de DIST ($7FDF3B, -$4189) demandent une formule en Hz ; le suréchantillonnage de drive met DIST à une autre fréquence.
- La rampe de 32 échantillons est liée au bloc.
- Echo et Gate Box : `SampleRate` de la table de déclin, pôles d'amortissement, toutes les longueurs de ligne, le 44100 de TIME,
  le pas du sinus par bloc, les vitesses du gate.
