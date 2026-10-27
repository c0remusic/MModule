# Moteur de l'instrument : 16 pistes, familles mélangées

Type: AFK
Status: ready-for-agent
Assignee: 
Blocked by: 04, 08

## Quoi

Le moteur au cœur du plug-in, parti de ST Drums : 16 pistes, chacune une voix mono de n'importe quelle famille
(Syntakt rapide, MD porté), trigs à l'échantillon près, voix éteinte non rendue, aucun coût fixe par famille.
Changer de machine : rampe de 64 échantillons, défauts de la nouvelle, locks de l'ancienne retirés. Chokes par
cible. Notes 36-51 jouent les pistes. Sorties Main et 15 stéréo, chaque piste sur l'une d'elles. Étage AMP minimal
(niveau, pan, vélocité) en attendant la chaîne.

Noté le 2026-10-27 depuis le ticket 06 : les machines de `syntakt::fast` voient un trig au début d'un bloc de 32
échantillons et le font sonner au bloc suivant, comme la machine. Rien ne place encore un trig à l'échantillon près
dans un bloc. CY ALLOY, rendue à 48 kHz puis rééchantillonnée, place son trig sur sa propre grille de blocs à
48 kHz : à 44,1 kHz, jusqu'à 28 échantillons de l'hôte (0,66 ms) de son instant idéal, selon le bloc. Ce ticket
règle les deux, et ajoute la latence du rééchantillonneur (24 échantillons à 44,1 kHz) au délai commun des Hits.
Depuis, tout le Syntakt rend à 48 kHz par `fast::Host` ; `stFastResampleTest` mesure ce placement sur un cycle de
147 blocs (de −0,75 à +28,4 échantillons, gigue 0,66 ms).

## Critère de fin

- Un Hit tombe à l'échantillon de sa note, gigue 0, latence mesurée.
- Une piste éteinte ne coûte rien (mesuré) ; une famille absente ne coûte rien.
- Choke et changement de machine sans clic (Q12).
- Plug-in chargé par l'hébergement VST3 de JUCE : sorties sur leurs canaux, mêmes échantillons quelle que soit la
  taille de bloc.

## Références

Spécification §3, §9, §11.2. Ticket « Source de machine d'une piste ».
