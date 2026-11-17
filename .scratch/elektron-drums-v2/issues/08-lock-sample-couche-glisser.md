# Lock de sample sur la couche, glisser sur un pas, nom court dans les lanes

Type: AFK
Status: ready-for-agent
Assignee:
Blocked by: 06, 07

## Quoi

Le lock de sample vise aussi la couche sample : deux cibles sur une piste Sampler, SAMP machine et SAMP couche. Un
sample glissé sur un pas pose un lock sur la cible de la page affichée. Dans les lanes de Seq, un pas locké montre le
nom court du sample.

## Critère de fin

- Test : les deux cibles sur un même pas jouent chacune leur sample.
- Test d'éditeur : un glisser sur un pas pose le lock de la cible de la page affichée ; la lane montre le nom court.
- Gate vert, nouveau test inscrit dans sa liste.

## Références

Spécification v2, §5.3 (`../elektron-drums/spec-v2.md`) ; ses tickets de décision dans `../lockstep-post-v1/issues/`.
