# Source de machine d'une piste

Type: grilling
Status: resolved
Assignee: c0remusic
Blocked by: 01, 03

## Question

Comment une piste prend-elle une
machine de n'importe quelle famille (Syntakt, Machinedrum, puis les suivantes) ? Allocation des voix (le CPU voix du
Syntakt rend 8 voix, le MD 16), mélange des familles entre pistes, fréquence propre de chaque famille et
rééchantillonnage, coût selon le budget.

## Answer

Décidé avec l'utilisateur le 2026-10-24 (grilling, Q1-Q6). La fréquence est réglée par « Une fréquence commune pour
tous les moteurs » (tous à la fréquence de l'hôte). Avec les ports et réécritures décidés, chaque famille est une
bibliothèque de voix, plus une machine émulée entière.

- Une voix par piste, mono, de n'importe quelle famille ; un trig redémarre la voix. Pas de polyphonie dans une piste
  (superposer : layer sample ou autre piste).
- Une voix éteinte (enveloppe finie, sous -100 dB) n'est pas rendue et ne coûte rien ; le budget de 15 % vaut au pire
  cas, 16 pistes sonnant ensemble.
- Aucun coût fixe par famille : les routines d'OS dont une famille a besoin (conversion des paramètres de machine, LFO,
  lissage du MD) tournent par piste, seulement pour les pistes qui la jouent ; une famille absente du kit coûte zéro.
- Familles mélangées librement entre les 16 pistes, sans limite par famille.
- Changer de machine en cours : voix coupée avec une rampe de 64 échantillons ; paramètres de machine aux défauts de la
  nouvelle (comme ASSIGN MACHINE du MD) ; locks des paramètres de l'ancienne machine retirés, avec annulation ; chaîne,
  ampli, LFO, envois et autres locks gardés.
- Chokes par cible, pas par groupes (amendé par l'utilisateur le 2026-10-24) : une piste désigne directement la ou
  les pistes qu'elle coupe, toutes familles, comme les Chokes de MoDule ; aucune par défaut. Un LFO vise sa piste par
  défaut et peut viser un paramètre d'une autre piste, comme sur le MD.
