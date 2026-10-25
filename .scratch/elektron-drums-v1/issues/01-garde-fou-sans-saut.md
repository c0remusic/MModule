# Garde-fou : un test sauté de sa liste le rend rouge

Type: AFK
Status: ready-for-agent
Assignee: 
Blocked by: 

## Quoi

Le 2026-10-25, le garde-fou a dit `GATE GREEN` avec 9 tests du Syntakt sautés (exit 77, variables `SYNTAKT_*`
absentes) : ctest compte un Skipped comme passé. Faire de tout test sauté de la liste du garde-fou un échec, et
documenter dans le `CLAUDE.md` du dépôt les variables `SYNTAKT_*` que les tests du Syntakt lisent, comme
`GEARMULATOR_MD_FIRMWARE_BIN` l'est pour le MD. Le garde-fou les pose depuis un emplacement local connu s'il le
trouve, sans jamais copier de firmware dans le dépôt.

## Critère de fin

- Garde-fou lancé sans les variables : `GATE RED`, le journal nomme chaque test sauté.
- Garde-fou lancé avec : les 9 tests passent, `GATE GREEN`.

## Références

Spécification §12 critère 12, §14. `scripts/windows/gate.ps1`.
