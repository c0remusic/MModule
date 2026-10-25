# Kits, banque et sons de piste

Type: AFK
Status: ready-for-agent
Assignee: 
Blocked by: 18

## Quoi

Kit = tout le son (pistes, chaînes, LFO et cibles, envois, sorties, chokes, effets, master), rien du séquenceur.
Banque de 64 kits dans le dossier de données, partagée entre instances, relue avant chaque écriture, écrite par
emplacement (comme `mdDrumsBank`). Un pattern garde un kit à charger ou « garder le kit en cours ». Sons de piste en
bibliothèque partagée. Patterns exportés et importés en fichier.

## Critère de fin

- Kit sauvé puis rechargé : mêmes échantillons. Deux instances écrivent chacune un emplacement sans perdre l'autre.
- Kit ancien à paramètre inconnu : défaut.

## Références

Spécification §8. Ticket « Format des kits et de l'état ».
