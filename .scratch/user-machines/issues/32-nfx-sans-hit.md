# Une NFX qui travaille dès qu'on la choisit

Type: task
Status: resolved
Assignee: c0remusic (session du 2026-10-20)
Blocked by:

## Question

Une Machine NEIGHBOUR ne prenait son type qu'au Hit de sa Track : un effet restait muet jusqu'à une note sur sa Track, à
chaque ouverture du set. L'utilisateur, le 2026-10-20 : « nfx oui » à la proposition de la faire travailler dès qu'on la
choisit.

## Answer

Construit le 2026-10-20 :

- `HostModel::start` donne à la voix d'une Track sa Machine utilisateur au tick suivant comme un Hit la donne (son type,
  donc INIT et TRIG, et ses mots), à vélocité 127, sans ce que fait aussi un Hit (relance du LFO, accent, Link, Choke)
  ni Hit pour la Telemetry.
- `mdDrums::Engine::setMachine` l'appelle pour une Machine NEIGHBOUR : choisie, assignée par un Kit, un état ou ASSIGN
  MACHINE, l'effet travaille aussitôt. `installUserMachine` relance de même les Tracks NEIGHBOUR qui jouaient le numéro
  remplacé (que `forgetUserMachine` avait rendues muettes).
- Vérifié : `mdDrumsEngineTest`, une copieuse en Track 6 jamais frappée entend Track 5 et se cale sur ses Hits (décalage
  0) ; les Hits ordinaires inchangés ; 21,2 % d'un cœur.
