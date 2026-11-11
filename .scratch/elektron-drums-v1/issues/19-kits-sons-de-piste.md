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

## État (2026-11-11)

Version réduite faite : banque de 64 kits, un fichier gzip XML par emplacement dans `Kits` du dossier de données
(`edDrumsPlugin/edDrumsKits.*`), relu avant chaque écriture, remplacé par fichier temporaire ; un kit garde tous les
paramètres de son par identifiant (sauf Tempo, Oversampling, Pattern), le sample de chaque piste et un nom. Page Kits de
Lockstep : charger, sauver, renommer, effacer. Pas faits : sons de piste en bibliothèque, kit chargé au départ d'un
pattern, import et export de patterns et de kits, contenu d'usine (ticket 21).
