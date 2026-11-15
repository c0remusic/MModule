# Passages entre fil message et fil audio : des primitives

Type: AFK
Status: ready-for-agent
Assignee: 
Blocked by: 

## Quoi

Revue d'architecture du 2026-11-15 (candidat 4) : le Processor tient six protocoles sans verrou écrits à la main, chacun
avec sa règle de retraite, mêlés à la logique de l'instrument :

- courrier des samples (`publishSample`, `pruneSamples`, lecture dans `processBlock`) ;
- courrier d'audition (`sendAudition`) ;
- triple tampon du séquenceur (`publishSequencer`) ;
- Kits préparés retirés au compte de blocs (`prepareKits`, `m_blocksDone`) ;
- `m_kitSwitched` ;
- files LIVE (deux `AbstractFifo`) ;
- sources partagées gardées par `use_count()`.

Les sortir en deux ou trois primitives génériques, testables seules : boîte « le dernier gagne » à libération différée,
instantané publié (triple tampon), liste « retirer après N blocs ». Le Processor ne garderait que ce qui y passe.

Pas de bug connu derrière : décidé avec l'utilisateur de ne pas y toucher pour l'instant.

## Critère de fin

- Chaque primitive testée seule, sous stress (deux fils).
- Latence et gigue mesurées avant et après (CLAUDE.md, « Latency »), sans écart.
- Gate vert, `edDrumsVst3HostTest` compris.

## Références

Rapport de revue d'architecture du 2026-11-15. `edDrumsProcessor.h` (membres des courriers), `edDrumsProcessor.cpp`.
