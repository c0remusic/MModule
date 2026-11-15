# Une seule liaison d'une piste vers l'Engine

Type: AFK
Status: ready-for-agent
Assignee: 
Blocked by: 

## Quoi

Revue d'architecture du 2026-11-15 (candidat 3) : trois cartes écrites à la main donnent les valeurs d'une piste aux
setters de l'Engine :

- `Processor::syncSound` (chaque bloc, seulement ce qui a changé, les Locks tenues à part) ;
- `Processor::applyTarget` (une Lock) ;
- `Processor::auditionTrackSound` (une piste entière sur un Engine à elle).

Ajouter Gap Sync a demandé cinq retouches. Déjà fait le 2026-11-15 : `markDue` (la liste « due » une seule fois) et
`sendAudition` appelé par l'audition.

Reste : une fonction qui donne une piste entière à un Engine depuis ses valeurs, utilisée par l'audition et par
`syncSound` là où une valeur est « due », pour qu'un nouveau paramètre de piste ne se touche qu'à un endroit. Éventuellement,
pour `syncShared`, chaque Spec portant sa cible des Sends.

Touche le chemin audio : décidé avec l'utilisateur d'en faire un ticket.

## Critère de fin

- Un paramètre de piste ajouté ne demande qu'une retouche dans la liaison (le dire sur un exemple).
- L'audition d'un son de piste et la piste jouée donnent les mêmes échantillons (test).
- Latence et gigue mesurées avant et après, sans écart ; gate vert.

## Références

Rapport de revue d'architecture du 2026-11-15. `edDrumsProcessor.cpp` : `syncSound`, `applyTarget`,
`auditionTrackSound`, `syncShared`.
