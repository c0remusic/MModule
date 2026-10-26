# Syntakt rapide : première machine à la fréquence de l'hôte

Type: AFK
Status: resolved
Assignee: 
Blocked by: 01

## Quoi

Réécrire une machine du Syntakt (la plus jouée, une BD) en flottant SIMD, paramétrée par la fréquence, sur blocs de
32 échantillons, FTZ/DAZ, sans allocation au rendu. Elle pose le patron des onze autres : forme des données de voix
(SoA sur 16 pistes), conversion des paramètres par piste, constantes de bloc recalculées depuis la fréquence. Le port
exact en entiers (`stEngine`) reste la référence.

## Critère de fin

- Contre le port exact à 48 kHz : Q2 sur les étages linéaires, Q3-Q4 sur la voix (seuils provisoires de la
  recherche « Qualité audio »).
- « Sonne pareil » 44,1 contre 48 kHz.
- Coût mesuré pour 16 pistes, comparé à la part de cette machine dans les 13,7 % actuels.

## Références

Spécification §3.1, §11. Tickets « Une fréquence commune », « Qualité audio », « Budget CPU ».

## Answer

Fait le 2026-10-26. `source/elektron/syntakt/stFast/` : `syntakt::fast::BdModern`, BD MODERN sur 16 pistes en
flottant (pistes en voies SSE2, blocs `float s[32][16]`), sans allocation au rendu, FTZ/DAZ pendant le bloc. Décidé
avec l'utilisateur : tout en flottant, contrôle compris. Le contrôle par bloc (sweep, enveloppes de l'unité 2, du
modulateur, de l'ampli, gain du drive) tourne sur l'horloge de blocs à 48 kHz de la machine, que les blocs et
échantillons de l'hôte lisent : chaque étage tombe au même instant en secondes à toute fréquence, et à 48 kHz ce
sont les blocs de la machine. Seules les constantes par échantillon (pas du click, ses deux filtres) se recalculent
depuis la fréquence. Tables encore lues dans la section 7 (neuf tables de paramètres, onde de l'unité 2, exp2), le
sinus calculé ; ticket 07 pour les formules.

`stFastBdModernTest` (dans le garde-fou), 16 Hits variés et un retrigger :

- Q2, étages linéaires contre l'étage entier : −138,7 à −163 dBFS RMS, max −120,9 à −145,4 dBFS.
- Q3 hors chaos : 73 dB de SNR au moins ; zone chaotique jugée sur ses distributions (décision de l'utilisateur).
- Q4 à 48 kHz : enveloppe 0,030 dB au pire, hauteur 0,00 cent, fatals aux seuils provisoires ; le null de Hit 13
  (20,1 dB) reste non fatal.
- Q15, 44,1 contre 48 kHz : spectre jugé sous 10 kHz à 0,215 dB au pire, enveloppe à 0,2 dB ; cas excusés listés
  au ticket 05.
- Coût, 16 BD MODERN par bloc de 32 : 7,0 µs, 1,05 % d'un cœur à 48 kHz, contre 29-31 µs (4,4 %) pour le port
  exact. Pour comparaison, toute la section voix du Syntakt coûte 7,0 % pour 16 pistes.

Essayé et retiré : la voix entière suréchantillonnée 2x avec décimateur demi-bande, qui faisait passer Q15 sur
Hit 12 mais doublait le coût (2,23 %) et perdait la fidélité à 48 kHz sur tous les Hits (enveloppe 0,4 à 15 dB).

