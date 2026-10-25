# Syntakt rapide : première machine à la fréquence de l'hôte

Type: AFK
Status: ready-for-agent
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
