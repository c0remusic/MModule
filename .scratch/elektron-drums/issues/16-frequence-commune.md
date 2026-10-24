# Une fréquence commune pour tous les moteurs

Type: grilling
Status: resolved
Assignee: c0remusic
Blocked by: 01, 03

## Question

Tous les moteurs doivent tourner à la même fréquence, celle de l'hôte, sans rééchantillonneur et sans CPU ajouté
(demande de l'utilisateur, 2026-10-24). Chaque moteur prendrait la fréquence en paramètre et recalculerait ce qui en
dépend (incréments de phase, coefficients de filtre, vitesse des enveloppes et des LFO, longueur des lignes de delay et
de reverb, tables calculées pour 48 kHz ou 44,1 kHz, fréquences internes fixes comme SY TONE à 96 kHz ou SY SWARM et
SY BITS à 24 kHz). À une autre fréquence que la sienne, un moteur n'est plus exact au firmware : reste-t-il exact à sa
fréquence d'origine (prouvé à l'oracle) et adapté ailleurs ? Quelles fréquences d'hôte viser (44,1, 48, 88,2, 96 kHz),
avec quel budget CPU à 96 kHz ? Que devient le rééchantillonneur (abandonné, ou gardé en option « exact ») ?

## Answer

Décidé avec l'utilisateur le 2026-10-24 (grilling, Q1-Q7). Exigence de l'utilisateur : la plupart des producteurs
travaillent à 44,1 ou 48 kHz, et l'instrument doit sonner pareil aux deux.

- Chaque moteur tourne à la fréquence de l'hôte. Testé à 44,1, 48, 88,2 et 96 kHz ; 192 kHz marche sans être budgété.
- Plus aucun rééchantillonneur (MameHq et le polyphasé abandonnés, pas d'option « exact » rééchantillonnée). Un moteur
  est exact à sa fréquence d'origine quand l'hôte y est (Syntakt, DT2, Digitone II, Cycles à 48 kHz ; MD à 44,1 kHz),
  adapté ailleurs ; tests contre l'oracle à la fréquence d'origine, tests de tolérance aux autres.
- Critère « sonne pareil » : un test rend chaque machine à 44,1 et à 48 kHz et compare spectre (sous 20 kHz),
  enveloppe et niveau sous des seuils ; une machine qui dépasse est corrigée avant d'être livrée.
- À 88,2 et 96 kHz, les moteurs rendent à mi-fréquence (44,1 ou 48 kHz) et un suréchantillonneur 2x HIIR remonte
  (~0,1 % à 16 pistes) : budget de 15 % tenu, exactitude gardée ; option « 96 kHz natif » au double du coût.
- Fréquences internes : SY TONE à 2x l'hôte ; SY BITS, SY SWARM et les SRR (MD, DT2) gardent leur fréquence absolue
  en Hz par échantillonneur-bloqueur (leur grain lo-fi est cette fréquence).
- Blocs de 32 échantillons à toutes les fréquences ; les constantes qui avancent par bloc (enveloppes, LFO, lissage)
  sont recalculées depuis la fréquence pour garder les mêmes temps. Trigs : amendé par « Séquenceur interne », ils
  sonnent à l'échantillon près, plus au début du bloc.
- Tolérance quand un étage n'est plus exact : les 16 tests de qualité et seuils de la recherche « Qualité audio »
  (`research/19-qualite-audio.md` §11), deux seuils à valider à l'écoute.
- Le Syntakt livré devient une réécriture rapide (flottant, SIMD) paramétrée par la fréquence et vérifiée à tolérance ;
  le port exact en entiers reste dans le dépôt comme référence de test.
