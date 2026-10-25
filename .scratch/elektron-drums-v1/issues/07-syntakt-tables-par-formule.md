# Syntakt : tables régénérées par formule

Type: AFK
Status: ready-for-agent
Assignee: 
Blocked by: 01

## Quoi

Le port du Syntakt lit ~183 Ko de la section 7 et ~26 Ko de la section 3 du firmware : ~175 tables, 505
descripteurs, 143 mots d'état SRAM. Cinq tables ont déjà leur formule. Régénérer les autres famille par famille,
chacune testée au mot près contre le firmware ; remplacer descripteurs, défauts et état SRAM par des données à nous.
Une table sans formule est signalée : sa machine reste bloquée ou devient approximative et marquée.

## Critère de fin

- Le rendu du Syntakt ne lit plus aucun octet du firmware (test : firmware absent, mêmes échantillons).
- Chaque table régénérée a son test contre le firmware.

## Références

Spécification §3.1, §11.4. Ticket « Instrument sans firmware chez l'utilisateur ».
