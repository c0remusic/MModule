# Séquenceur interne

Type: AFK
Status: ready-for-agent
Assignee: 
Blocked by: 11

## Quoi

Le séquenceur de la spécification §7 : 128 patterns de 64 pas, longueur et échelle par piste, pas complets (trig,
note, vélocité, durée, 65 conditions, micro-timing, ratchets, accent, slide, locks sans plafond, locks sans note),
swing, mutes, fill, chaînes ; conditions aléatoires à graine ; transport de l'hôte seul ; pattern par paramètre
« Pattern » ou program change, à la fin du pattern en cours ; mode LIVE ; copier, coller, effacer, décaler ; export
MIDI. Les conditions et le micro-timing peuvent reprendre le CPU #1 du Syntakt (`0x401181D4`, `0x4008B882`).

## Critère de fin

- Critère 6 de la spécification : pattern de test rendu deux fois à l'identique, chaque trig à l'échantillon
  attendu, gigue 0, changement de pattern au bon pas, export MIDI relu.
- Coût du séquenceur mesuré.

## Références

Spécification §7, §12. Tickets « Séquenceur interne », « Fonctions de MoDule Seq », « Séquenceurs des machines ».

## Noté par le ticket 11 (2026-11-06)

Le séquenceur vit dans `edDrums`, sans JUCE, et fusionne ses trigs aux notes de l'hôte là où `processBlock` coupe son bloc. Il faudra une API de Locks : le
moteur n'en a pas. L'identité de deux rendus ne tient que depuis la même phase de grille : une voie Syntakt dépend de son bloc à
48 kHz, et les voix éteintes gèlent (spécification §3). Le critère 6 démarre donc ses deux rendus de la même position, moteur neuf.
