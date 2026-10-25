# Deux LFO par piste, SYNC ou HZ

Type: AFK
Status: ready-for-agent
Assignee: 
Blocked by: 11

## Quoi

Deux LFO par piste. SYNC (défaut) : division musicale de 1/64 à 16 mesures, pointée, triolet, phase sur la position
du transport de l'hôte, sauf en mode de trig TRIG où elle redémarre au trig ; vitesse et multiplicateur des LFO
Elektron traduits en division. HZ : vitesse en Hz. Cible sur la piste ou sur un paramètre d'une autre piste.
Avancent au bloc.

## Critère de fin

- Phase identique d'un rendu à l'autre pour une même position de transport ; tempo changé, période suivie.
- Un LFO qui vise une autre piste la module, sans coût pour une piste sans LFO actif.

## Références

Spécification §4. Ticket « Chaîne de piste » (amendement LFO).
