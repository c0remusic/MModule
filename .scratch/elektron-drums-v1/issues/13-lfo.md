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

## Noté par le ticket 11 (2026-11-06)

`Track::update` est la couture où un LFO s'insère. Le moteur n'a aucune position de transport : ce ticket doit lui en donner une.
`alignTicks` met chaque piste sur sa propre phase de tick au Hit : un LFO qui vise une autre piste tombe donc sur une autre phase.

## Noté par le ticket 12 (2026-11-06)

`Chain::offset(lane, target)` est le seul endroit où une valeur se décale, `setDrive`, `setBand`, `setFilter` les seuls à recalculer ses
mots, `stepEnvelopes` le chemin par bloc : la modulation y entre. Ne pas découper `configure` d'avance : mesurer d'abord le coût des mots
de `ThreeBand` recalculés à chaque bloc. Un type d'étage modulé se fondrait en 96 échantillons : ne pas le moduler.
