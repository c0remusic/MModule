# Une fréquence commune pour tous les moteurs

Type: grilling
Status: open
Assignee:
Blocked by: 01, 03

## Question

Tous les moteurs doivent tourner à la même fréquence, celle de l'hôte, sans rééchantillonneur et sans CPU ajouté
(demande de l'utilisateur, 2026-10-24). Chaque moteur prendrait la fréquence en paramètre et recalculerait ce qui en
dépend (incréments de phase, coefficients de filtre, vitesse des enveloppes et des LFO, longueur des lignes de delay et
de reverb, tables calculées pour 48 kHz ou 44,1 kHz, fréquences internes fixes comme SY TONE à 96 kHz ou SY SWARM et
SY BITS à 24 kHz). À une autre fréquence que la sienne, un moteur n'est plus exact au firmware : reste-t-il exact à sa
fréquence d'origine (prouvé à l'oracle) et adapté ailleurs ? Quelles fréquences d'hôte viser (44,1, 48, 88,2, 96 kHz),
avec quel budget CPU à 96 kHz ? Que devient le rééchantillonneur (abandonné, ou gardé en option « exact ») ?
