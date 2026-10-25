# 88,2 et 96 kHz : rendu à mi-fréquence

Type: AFK
Status: ready-for-agent
Assignee: 
Blocked by: 11

## Quoi

À 88,2 et 96 kHz, rendre les moteurs à 44,1 ou 48 kHz et remonter par un suréchantillonneur 2x HIIR ; option
globale « 96 kHz natif » au double du coût, gardée dans l'état. 192 kHz marche sans être budgété.

## Critère de fin

- Q15 : un Hit à 44,1, 48 et 96 kHz, enveloppe et spectre dans les seuils.
- Pire cas 16 pistes sous 15 % à 88,2 et 96 kHz ; 192 kHz rend sans erreur.

## Références

Spécification §11.2. Ticket « Une fréquence commune ».
