# Le compresseur du master agit

Type: HITL
Status: done (écoute dans Live à venir)
Assignee: Claude (session 2026-11-25)
Blocked by: aucun

## Quoi

Retour de test dans Live (2026-11-24) : le compresseur du master ne semble pas fonctionner.

Mesuré : il fonctionne. Master On, seuil à -30 dB (64), ratio 20:1, un kit frappé à 127 donne -25,5 dB de
réduction. Son DSP est comparé à l'oracle DT2 par les tests du moteur. Deux défauts le rendaient muet : Master On
vaut Off, et le seuil par défaut valait 0 dB (127) alors que Main entre à -10 dB.

## Décidé (2026-11-25, recommandation acceptée par l'utilisateur)

- Seuil par défaut à -19,8 dB (85).
- Un geste de l'éditeur sur un réglage du compresseur ou sur DRIVE (THRESHOLD, RATIO, ATTACK, RELEASE, MAKEUP, MIX,
  SC FILTER, DRIVE, et THR tiré sur la courbe) allume Master, en un geste à lui. Le fader MAIN ne l'allume pas.
  L'automation de l'hôte ne l'allume pas non plus.
- Master On reste Off par défaut : un set neuf sonne comme avant.

## Critère de fin

- [x] `edDrumsProcessorTest` (`compressor`) : au défaut, -5,6 dB de réduction ; à -30 dB, -25,5 ; à 0 dB ou
  éteint, rien.
- [x] `edDrumsEditorTest` : THR tiré allume Master (deux gestes) ; RATIO tiré l'allume ; le fader MAIN non.
- [ ] Écoute dans Live.

## Point ouvert

MAIN (Master Volume) n'agit que Master allumé, comme DRIVE : son fader bouge sans effet tant que Master est Off.
