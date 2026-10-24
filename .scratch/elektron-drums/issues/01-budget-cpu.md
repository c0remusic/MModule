# Budget CPU de l'instrument à 16 pistes

Type: research
Status: resolved
Assignee: agent
Blocked by: 

## Question

Combien coûte chaque élément de
l'instrument, et que faut-il gagner pour tenir 15 % d'un cœur à 16 pistes au grand maximum ? Mesurer, avec les options
du plug-in : une piste Syntakt (moteur, étages), un moteur Machinedrum (mdEngine : voix DSP56300, chaîne de piste,
maître) par nombre de pistes MD, la chaîne de piste ajoutée (drives empilés, EQ 3 bandes), le layering, les envois,
le rééchantillonnage court. En déduire le budget par élément, ce qui dépasse, et les leviers connus
(performance-research.md, et pour le DSP56300 le JIT de Gearmulator).

## Answer

Mesuré le 2026-10-24 (Ryzen 7 3700X, machine chargée : chiffres sans doute 10-15 % trop hauts). Syntakt 16 pistes :
13,7 % (voix 7,0, contrôle 2,4, mix + FX 4,3), indépendant du nombre de pistes jouées. Machinedrum : ≈ 5 % fixe +
1,05 % par piste active ; 16 actives 21,7 % (voix DSP56300 11,7, chaîne + mix C++ 4,5, maître DSP56300 3,6, tick 68k
1,9) ; MoDule entier 23,9 %. Rééchantillonnage 17 voies : MameHq 24 %, polyphasé 16 coefficients 1,2 %. Ajouts
prototypes à 16 pistes : EQ 3 bandes + 2 drives + layer + envois ≈ 3 % (drives suréchantillonnés 2× : +8,6). Pire cas
≈ 26 % (16 pistes MD) : budget proposé moteurs ≤ 10 %, chaîne ≤ 2, layer ≤ 1, envois ≤ 1, rééchantillonnage ≤ 1 ; le MD
est à diviser par ~2,2 (port C++ des voix, le levier JIT « left-aligned » étant déjà pris ; chaîne en SoA ; maître
porté ou omis), le Syntakt à ramener à ~10 % (R3-R7, PGO). Détails, méthode et inconnues :
[research/01-budget-cpu.md](../research/01-budget-cpu.md).
