# Machinedrum : TrackFx en flottant SIMD, Echo et Gate Box

Type: AFK
Status: ready-for-agent
Assignee: 
Blocked by: 02

## Quoi

Réécrire la chaîne de piste du MD (EQ, filtre, SRR, DIST de `TrackFx`, sans l'AMD) en flottant SIMD sur 16 pistes,
avec une entrée publique par étage pour que la chaîne à emplacements les place seuls. Porter l'Echo et la Gate Box
de son master comme sources de delay et de reverb. SRR garde sa fréquence absolue.

## Critère de fin

- Chaque étage contre `mdEngine` : Q2 (linéaire) ou Q3 (non linéaire) à 44,1 kHz ; Q13 sur SRR à fond.
- Coût de la chaîne MD pour 16 pistes mesuré.

## Références

Spécification §3.2, §4, §5. Tickets « Moteur du Machinedrum », « Sources des effets d'envoi ».
