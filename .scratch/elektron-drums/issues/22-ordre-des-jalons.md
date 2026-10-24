# Ordre des jalons après la v1

Type: grilling
Status: resolved
Assignee: c0remusic
Blocked by: 

## Question

Dans quel ordre construire l'instrument, de la v1 au complet, avec quel critère de fin pour chaque jalon ? Les
recherches donnent les efforts et coûts estimés : Machinedrum en C++ 2-3 semaines d'agent (+1 flottant), réécriture
rapide du Syntakt (à estimer), Model:Cycles 4-7 sessions, Digitakt II 6-10 semaines
(avec le layering), machines analogiques (30 routines CPU #1, modèles à régler à l'oreille), tables sans firmware 4-8
sessions. Faut-il d'abord une v1 jouable minimale (Syntakt rapide + MD porté + chaîne + interface + séquenceur), puis
les familles par valeur musicale, ou par facilité ?

## Answer

Décidé avec l'utilisateur le 2026-10-24 (grilling, Q1-Q6).

- v1, moteurs d'abord (ils décident si le budget tient) : réécriture rapide du Syntakt à la fréquence de l'hôte avec
  ses tests de tolérance ; port C++ du MD ; chaîne de piste, envois, master (compresseur et overdrive du DT2 portés en
  avance, avec le harnais d'oracle du DT2) ; séquenceur ; kits et explorateur ; interface complète. L'interface avance
  en parallèle sur les mockups.
- La v1 tourne sans firmware chez l'utilisateur : tables du Syntakt et du MD régénérées par formule dans la v1 ;
  machines ROM du MD sur la banque UW importée par le propriétaire ou sur les samples maison.
- Types de chaîne en v1 : drives et filtres du Syntakt et du MD, EQ 3 bandes ; types DT2 (6 filtres, SRR,
  base-width, BR) à son jalon, sauf l'overdrive du DT2, porté avec le master et utilisable en drive de piste.
- Après la v1 : J2 Digitakt II (sampler, layering, filtres, effets, BR, base-width) ; J3 Model:Cycles ; J4 machines
  analogiques du Syntakt et du Rytm, avec l'overdrive analogique du Syntakt au master ; J5 couche sample du Rytm ;
  plus tard compagnon Push, euclidien et aléatoire.
- Critère de fin de chaque jalon : tests contre l'oracle (exacts à la fréquence d'origine, dans la tolérance des 16
  tests de qualité ailleurs) ; test « sonne pareil » à 44,1 et 48 kHz ; pire cas sous 15 % d'un cœur à 16 pistes,
  mesuré sur machine calme ; garde-fou vert ; aucun firmware nécessaire chez l'utilisateur.
- Suite de la carte : rédiger la spécification v1 à partir des décisions (`/to-spec`), puis la découper en tickets
  d'implémentation (`/to-tickets`) ; l'interface continue jusqu'à un mockup approuvé.
