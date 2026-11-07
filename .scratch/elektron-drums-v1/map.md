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

- 2026-11-06 : but de l'utilisateur, unité de latence et de son, moins de dépendances, code de l'OS retiré là où ça apporte. L'exactitude bit à bit
  est un outil de mesure : les tickets restants se jugent aux seuils et à l'oreille. Une écoute à l'aveugle n'a rien trouvé d'audible autour des voix exactes ;
  elle n'a jamais comparé une voix refaite.
- 2026-11-06 : [rééchantillonneur à phase minimale](issues/11-moteur-instrument-16-pistes.md), 48 coefficients par phase, `HitDelay` 81 (1,84 ms), gigue 0, bornes de Q15 par
  machine. Écoute du filtre final le 2026-11-07, BD MODERN, SY BITS et CP VINTAGE contre linéaire : rien entendu, filtre gardé.
## Not yet specified

- Réécritures maison ciblées, avant le ticket 14 (décidé le 2026-11-07 : TRX, puis P-I, puis E12), une famille à la fois, en paires à l'aveugle contre la voix actuelle : machines qui lisent la flash (TRX à formes
  d'onde, E12, P-I, banque ROM), table `$24DA14`, machines à démarrage tardif (TRX-XT, TRX-BD, TRX-S2).
- Idée à tester, le CPU prime : faire sortir un son traité dans sa propre piste. Un retour sur l'Out d'une piste suffit déjà pour un effet réservé à cette piste, sans coût. Un compresseur par piste, emplacement de chaîne limité à 2 ou 3, se juge après le port du compresseur DT2, sur son coût mesuré ; au-delà du budget, il n'entre pas. Un envoi pré-fader par piste reste à trancher.
- Idée de l'utilisateur, à trancher au ticket 20 : un bit reducer pour les samples chargés. E12 n'en contient pas, ses 12 bits sont dans sa banque, il n'y a rien à en extraire. Le DT2 BR du jalon J2 le fait ; préférence notée pour la chaîne de piste, réglable et automatisable, plutôt qu'un traitement figé à l'import. Le moteur E12, déjà porté, reste tel quel avec une image ; sans image, ses sons relèvent du contenu d'usine (ticket 21), pas d'une réécriture.
- Nom du produit.
