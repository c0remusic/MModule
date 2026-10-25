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

## Critère de fin

- Un Hit tombe à l'échantillon de sa note, gigue 0, latence mesurée.
- Une piste éteinte ne coûte rien (mesuré) ; une famille absente ne coûte rien.
- Choke et changement de machine sans clic (Q12).
- Plug-in chargé par l'hébergement VST3 de JUCE : sorties sur leurs canaux, mêmes échantillons quelle que soit la
  taille de bloc.

## Références

Spécification §3, §9, §11.2. Ticket « Source de machine d'une piste ».
