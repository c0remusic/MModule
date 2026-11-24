# Trier les machines par famille, type ou samples

Type: HITL
Status: done
Assignee: Claude (session 2026-11-24)
Blocked by: aucun

## Quoi

Retour de test dans Live (2026-11-24) : on ne peut pas trier les machines par famille, par type ou par samples dans
le navigateur.

## Décidé (2026-11-24, recommandations acceptées par l'utilisateur)

- Filtres, pas de tri : la liste garde son ordre.
- Trois axes combinables : famille (les noms de famille de Lockstep), type (KICK, SNARE, HAT, CLAP, TOM, CYMBAL,
  PERC, SYNTH, OTHER : les types des samples, plus SYNTH), source (synthèse ou samples).
- La table des types est écrite par Claude, relue par l'utilisateur avant le commit.

## Fait

- `names::type`, `names::playsSamples` (`edDrumsNames.*`) : la table des 105 machines nommées, par nom.
  Samples : SAMPLER, DUO SAMPLE, CRUNCH 12.
- MACHINES : les types en segments au-dessus de la liste (15 lignes), FAMILY et SOURCE en pastilles dans la colonne
  LIBRARY, le type de chaque machine à droite de sa ligne. Cliquer une entrée de LIBRARY, ou la cellule Machine,
  remet tous les filtres à ALL. Écart avec la proposition (deux rangées au-dessus de la liste) : 12 familles ne
  tiennent pas en une rangée, la colonne LIBRARY était vide.
- `edDrumsEditorTest` : chaque machine a un type (OTHER seulement pour OFF, SILENCE, NOISE, IMPULSE, DUO SAMPLE,
  SAMPLER, HYPER FM, NOISE GEN) ; KICK liste 15 machines, toutes des grosses caisses ; KICK et SAMPLES, LOFI KICK
  seule ; ANALOG ses 30 machines ; rendu `browse-machine-filters.png`.

## Critère de fin

- [x] Filtres et test.
- [x] Table relue par l'utilisateur (2026-11-24, acceptée telle quelle).

## Point ouvert

La fiche de droite garde la machine choisie même quand un filtre la retire de la liste.
