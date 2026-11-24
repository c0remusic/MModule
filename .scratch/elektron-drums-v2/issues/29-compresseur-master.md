# Le compresseur du master agit

Type: HITL
Status: needs-decision
Blocked by: aucun

## Quoi

Retour de test dans Live (2026-11-24) : le compresseur du master ne semble pas fonctionner.

Mesuré : il fonctionne. `edDrumsProcessorTest` (`compressor`) : Master On, seuil à -30 dB (64), ratio 20:1, un kit
frappé à 127 donne -25,5 dB de réduction ; Master Off, ou seuil à son défaut, rien. Son DSP est comparé à l'oracle
DT2 par les tests du moteur.

Ce qui le rend muet par défaut : Master On vaut Off, et le seuil par défaut vaut 0 dB alors que Main entre à -10 dB
(défauts choisis pour « laisser Main tel quel »). Même allumé, il ne touche rien tant que le seuil reste en haut.

## À décider

- Défauts : Master On allumé ? seuil par défaut plus bas ?
- Signalement : cellules du compresseur grisées tant que Master est Off (le mécanisme `sectionOff` bloque aussi
  leur édition), ou allumage automatique au premier geste sur un réglage du compresseur.

## Critère de fin

- À écrire quand la décision est prise. Test de réduction : fait.
