# Un LFO module l'autre

Type: HITL
Status: needs-triage
Blocked by: aucun

## Quoi

Retour de test dans Live (2026-11-24) : on ne peut pas moduler les LFO entre eux.

Décisions à prendre : quelles destinations (vitesse, profondeur, phase de l'autre LFO de la piste ; LFO d'une autre
piste ?), sens (LFO 1 vers LFO 2 seul, ou les deux, avec quelle règle contre la boucle), ordre de calcul dans le
bloc. Les cibles s'ajoutent à la liste des cibles en fin d'énumération (identifiants figés).

## Critère de fin

- À écrire quand les décisions sont prises.
