# Décoder l'OS X.20 et y trouver ses Machines intégrées

Type: research
Status: resolved
Assignee: c0remusic (session du 2026-10-17)
Blocked by:

## Question

Que coûte d'extraire de l'OS X.20 les 8 Machines intégrées qu'il ajoute (GND-SN-PRO, GND-SW, GND-PU, NFX-EV, NFX-CO,
NFX-UC, INP-CA, INP-CB) pour que MoDule les joue ? À établir, sur le `.syx` de l'utilisateur et sans rien commiter
de lui : comment se décompresse la section 0 (le dépaqueteur de 1.63 s'arrête à l'octet 7 090) et ce que sont les
blocs « SFS1 » jusqu'à `$F0000` ; où sont les programmes du DSP voix et du DSP mixeur, et le code DSP de ces 8
Machines ; où sont la table des Machines et leurs routines 68k (coefficients) ; où X.20 répartit les Machines
utilisateur, et sur quel DSP. Rendre une estimation (jours, semaines ou mois) et ce qui resterait inconnu. Limité à
une session : ce qui n'est pas trouvé dedans est rapporté comme inconnu, pas poursuivi.

## Answer

Recherche du 2026-10-17 : [research/01-decoding-x20.md](../research/01-decoding-x20.md).

- **Des semaines, pas des mois** : 3 à 5 semaines de sessions pour les 8. Les 3 GND d'abord (1,5 à 2 semaines, lecteur
  du fichier X.20 compris) ; les 3 NFX demandent l'audio de la Track voisine (3 à 5 jours de plus) ; INP-CA et INP-CB
  jouent muet, MoDule ne recevant pas d'entrée audio. L'identité au son d'X.20 attend « Booter X.20 dans mdLib ».
- **Le fichier se décode en entier**, chaque CRC juste : un chargeur ColdFire, puis le programme du DSP voix, celui du
  DSP mixeur, l'OS 68k (LZMA) et un noyau de boot DSP.
- **Le programme du DSP voix d'X.20 est celui de 1.63** à 46 mots près, plus 4 807 mots ajoutés : le code DSP des 8
  (2 255 mots à P:`$13b600-$13bece`, autonome) et l'hôte des Machines utilisateur.
- **Les Machines utilisateur tournent sur le DSP voix**, dans la même boucle par voix que les intégrées ; leur code vit
  à P:`$13c108-$13f7eb`, et l'OS écrit leurs entrées 195 à 210 dans trois tables d'appel étendues. Fait d'entrée pour
  « Où tourne le code d'une Machine utilisateur ».
- **L'OS 68k est un nouveau build** : environ 1 % du code de 1.63 survit, aucune adresse de mdEngine ne se reporte.
  Les routines de coefficients des 8 sont trouvées et petites (22 à 702 octets), mais lisent des tables et un état par
  Track qu'X.20 construit au boot. Toutes les adresses sont celles d'X.20 B : un outil doit les trouver par motif.
