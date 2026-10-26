# Instrument de batterie Elektron — construction de la v1

## Destination

La v1 livrée selon `../elektron-drums/spec-v1.md`, ses douze critères d'acceptation (§12) passés, garde-fou vert
sans test sauté.

## Notes

- Spécification : [spec-v1.md](../elektron-drums/spec-v1.md). Décisions et leur détail : la carte de planification
  [elektron-drums/map.md](../elektron-drums/map.md) et ses tickets. Une contradiction entre un ticket ici et la
  spécification se règle dans la spécification d'abord.
- Tout code suit les quatre règles `karpathy-guidelines` : hypothèses dites, minimum qui résout, changements
  chirurgicaux, critère de fin vérifié avant d'annoncer fini. À rappeler dans le brief de tout agent délégué.
- Performance : 15 % d'un cœur est un plafond, pas une cible ; chaque ticket rapporte son coût mesuré, sur machine
  calme, et garde tout gain mesuré. Un ticket qui touche au chemin des trigs ou au rendu rapporte latence et gigue
  avant et après.
- Chaque ticket nomme son critère de fin ; un test nouveau entre dans la liste du garde-fou.
- Avant le garde-fou et le commit de chaque ticket : `/code-review` sur son diff, puis une revue d'architecture
  (`mattpocock-skills:codebase-design`, à la place de `improve-codebase-architecture`, absent de cette installation).
  Les constats retenus sont corrigés avant le garde-fou ; ceux qu'on écarte sont nommés dans le commit (règle de
  l'utilisateur, 2026-10-26).
- Ordre : moteurs d'abord (ils décident si le budget tient) ; l'interface avance en parallèle dès que le moteur de
  l'instrument existe.

## Tickets

Ordre de la spécification (§13). `Blocked by:` en tête de chaque ticket.

- Fondations : 01 garde-fou, 02 harnais du MD et inventaire des tables, 03 harnais d'oracle du DT2.
- Syntakt : 04 première machine rapide, 05 seuils Q3-Q4 à l'écoute, 06 les 11 autres, 07 tables par formule.
- Machinedrum : 08 renders ROM et TRX, 09 autres renders, 10 `TrackFx` SIMD, Echo et Gate Box.
- Instrument : 11 moteur à 16 pistes, 12 chaîne à emplacements, 13 LFO, 14 envois, 15 master DT2,
  16 fréquences 88,2 / 96 kHz.
- Séquenceur : 17.
- Hôte et données : 18 paramètres, relais, sorties, état ; 19 kits et sons de piste ; 20 samples, import Elektron,
  explorateur ; 21 contenu d'usine maison.
- Interface : 22 page de piste et knobs, 23 box de pas et page SEQ, 24 pages FX, Master, Kit, Sound browser.
- Fin : 25 mesures d'acceptation.

## Decisions so far

## Not yet specified

- Nom du produit.
