# Place du Digitakt II dans Lockstep

Type: grilling
Status: resolved
Assignee: agent (session du 2026-11-15)
Blocked by: 04

## Question

Avec « Le moteur sampler du Digitakt II : ce qu'il fait » : le sampler du DT2 entre-t-il comme une machine (une
famille DT2), comme le moteur de la couche sample de chaque piste (layering, ticket 05 de la planification v1), ou les
deux ? Que devient la machine Sampler maison (gardée, remplacée, fusionnée) et que deviennent les sets qui l'utilisent ?
Dans quel ordre le jalon livre ses morceaux (voix, layering, 6 filtres, SRR, BR, base-width, effets d'envoi) ?

Ce que la recherche oppose à Lockstep, à trancher ici : la stéréo du DT2 contre une piste mono (layering décidé mono) ;
ses 48 kHz et son rendu interne à 96 kHz contre les 44,1 kHz (rééchantillonneur et sa latence, ou adapter le pas) ; un
sample par pas contre un sample par piste ; le GRAIN binaire de Machine 9 (0-63, 64-127), qui n'a pas de place pour un
troisième lecteur sans casser les sets ; STRT/LEN contre STRT/END ; lesquelles des six machines sample (ONESHOT, WERP,
STRETCH, REPITCH, SLICE, GRID) entrent.

## Answer

Décidé avec l'utilisateur le 2026-11-15 (grilling). Une machine qui ne fait que lire des samples ne sert à rien
(l'utilisateur) : pas de famille de machines Digitakt II. L'architecture du DT2 entre dans le Sampler de Lockstep.

- Le Sampler gagne un troisième lecteur, DT2, à côté de 12-BIT et VINTAGE, qui restent tels quels (REPEAT GAP compris).
  La couche sample de chaque piste (layering) lit avec le même lecteur : une seule voix portée.
- GRAIN (Machine 9) devient un choix de trois ; un set existant migre à la lecture (0-63 : 12-BIT, 64-127 : VINTAGE),
  comme les autres migrations de version : aucun set ne change de son.
- Avec le lecteur DT2, un réglage MODE choisit ONESHOT, GRID, REPITCH, WERP, SLICE ou STRETCH ; la couche n'offre que
  ONESHOT et ses quatre modes de PLAY.
- Sémantique du DT2 là où il joue : STRT + LEN, LOOP, PLAY à quatre modes, TUNE ±5 octaves. 12-BIT et VINTAGE gardent
  STRT/END. La page du Sampler change avec le lecteur et ne montre que les réglages qui existent : aucun emplacement
  vide parmi Machine 1-24.
- Mono, comme toute la chaîne de piste (un sample stéréo sommé, comme le Locker le fait déjà).
- 48 kHz, puis le rééchantillonneur polyphasé du Syntakt (`fast::Host`, 24 échantillons de latence), comme le Syntakt :
  l'oracle et le pas Q31 du DSP tiennent.
- BR et SRR du DT2 deviennent des types de drive, son base-width un type de BAND (celui de la v1 est celui du Syntakt), tous
  après les types actuels.
- Sample par pas (SAMP verrouillable) : ticket à part, « Plusieurs samples par piste ».
- Ordre du jalon : 1 lecteur DT2 dans le Sampler (ONESHOT, GRID, REPITCH : routine `0x1c4ecf`) ; 2 layering ; 3 les
  6 filtres ; 4 BR, SRR, base-width ; 5 WERP, SLICE et son éditeur, STRETCH (`0x1c5576`) ; 6 delay, reverb et chorus du
  DT2 dans les envois.
