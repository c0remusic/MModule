# VEL DEST étendu aux réglages de la machine sur une piste Cycles

Type: AFK
Status: done
Assignee:
Blocked by: 18

## Quoi

Sur une piste Cycles, VEL DEST gagne les réglages de la machine comme cibles, VEL AMT pour leur quantité. Pas de
seconde paire.

## Critère de fin

- Test : chaque cible suit la vélocité de VEL AMT ; sur une autre famille, la liste de VEL DEST ne change pas.
- Aucun set existant ne change de son.
- Gate vert.

## Références

Spécification v2, §6 (`../elektron-drums/spec-v2.md`) ; ses tickets de décision dans `../lockstep-post-v1/issues/`.

## Fait (2026-11-23)

- Cibles : PTCH, DEC, COLR, SHPE, SWEP et CONT (Machine 1-6), comme l'a décidé « Les machines du Model:Cycles dans
  Lockstep » ; PUNCH et GATE n'en sont pas. `VelocityTarget` les ajoute après ENV DEPTH ; `Engine::setChain` les
  garde sur la piste et n'en donne aucune à la chaîne. Un changement de famille les remet à zéro avec la chaîne.
- Loi : celle des cibles de chaîne, VEL AMT x vélocité / 127 valeurs ajoutées à la valeur du Kit (verrou compris) et
  aux mouvements des LFO, posée à chaque Hit avant le trig (`hitSyntakt`). Un VEL AMT changé attend le Hit suivant.
- `vel_target` passe de 0-8 à 0-14. Un état garde la valeur en clair : aucun set enregistré ne change. Une
  automation de l'hôte, un MIDI Learn ou une liste Push sur VEL TARGET gardent la valeur normalisée et visent donc
  une autre cible après la mise à jour (comme à l'ajout des cibles de LFO).
- Éditeur : VEL TARGET liste les neuf cibles de chaîne, et sur une machine Cycles ses Machine 1-6 sous leurs noms.
  Une liste peut nommer les seules premières valeurs de sa plage (`Cell::normalisedOf`, `indexOf`).
- Tests : `edDrumsEngineTest` (velocityOnMachine : chaque cible à deux vélocités égale un Hit de la valeur visée,
  rien sur une machine Syntakt) ; `edDrumsEditorTest` (les deux listes, ENV DEPTH à la valeur 8 sur l'une et
  l'autre).
