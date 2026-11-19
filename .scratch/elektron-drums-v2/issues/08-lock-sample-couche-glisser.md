# Lock de sample sur la couche, glisser sur un pas, nom court dans les lanes

Type: AFK
Status: done
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

## Fait (2026-11-19)

**Moteur.** `Engine::lockLayerSample` donne au Hit qui suit le sample de sa couche, sur les deux chemins (Machinedrum
et Syntakt). Le Hit suivant sans lock reprend le sample propre de la couche. Chaque voix de la couche lit son propre
slot, pris au départ de la voix : la voix qui s'estompe sous un nouveau Hit garde son sample.

**Lock.** Paramètre réservé `SampleLayerLock` (0xFFFE), à côté de `SampleLock`. Pattern, état et chargement par
`takeLockSamples` traitent les deux cibles de la même façon.

**Éditeur.** Le menu + LOCK propose LAYER SAMPLE sur toute piste, et SAMPLE sur une piste Sampler. Chaque entrée ajoute
sa lane, et chaque lane ouvre Browse pour sa cible. Une ligne de Browse glissée sur la bande d'un pas pose le lock sur
ce pas de la piste montrée : le sample du Sampler sur une piste Sampler, sinon celui de la couche (choix de
l'utilisateur, 2026-11-19). Le nom court dans les lanes vient du ticket 07, tranche 2.

**Tests.**
- `edDrumsSampleLockTest` : sur les trois grains, quatre Hits qui lockent les deux samples jouent, échantillon pour
  échantillon, ce que joue l'hôte qui pose les deux avant chaque Hit. Chaque lock de couche s'entend, rien n'est alloué.
- `edDrumsProcessorTest` : deux des pas lockés lockent aussi la couche, et le pattern joue ce que joue le moteur.
- `edDrumsEditorTest` : un même pas locke les deux cibles depuis leurs lanes. Une ligne lâchée sur la bande du pas 10
  locke le Sampler sur la piste 1, la couche sur la piste 2.

Mis de côté après revue :
- LAYER SAMPLE est proposé même couche éteinte ; ses locks ne jouent rien.
- Un `setLayerSample` pendant le fondu d'une voix lockée lui fait finir son fondu sur le sample propre.
- Le glisser sur une bande ne vise jamais la couche d'une piste Sampler ; sa lane le fait.

## Références

Spécification v2, §5.3 (`../elektron-drums/spec-v2.md`) ; ses tickets de décision dans `../lockstep-post-v1/issues/`.
