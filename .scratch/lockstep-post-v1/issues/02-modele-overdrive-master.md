# Analog FX du Syntakt dans Lockstep : place, modèle, réglages

Type: grilling
Status: resolved
Assignee: agent (session du 2026-11-16)
Blocked by: 01

## Question

« Circuit de l'overdrive analogique du Syntakt » a montré qu'il n'y a pas d'overdrive de master : c'est le DRIVE du
bloc Analog FX, insert stéréo analogique DRIVE, filtre, AMP sur un bus à part, ses entrées choisies une à une, sa
sortie dans le mix (et vers les envois [INFER]). Dans Lockstep : un bus à part comme sur le Syntakt (quelles pistes y
vont, comment), un type de plus au master à côté de l'overdrive du DT2, ou un drive de piste ? Le bloc entier (drive,
filtre, AMP) ou le DRIVE seul ? Quel modèle (courbe dépendante du niveau, grave gardé puis perdu, compression, bruit),
son coût, et comment on juge qu'il est assez proche sans machine à mesurer ?

## Answer

Décidé avec l'utilisateur le 2026-11-16 (grilling).

- Pas de bus Analog FX : il recouvre plus ou moins les effets master actuels (l'utilisateur). Ni filtre ni AMP du
  bloc, ni ROUT par piste, ni ligne FX au séquenceur.
- Le DRIVE analogique entre comme une **option ANALOG ≈ de ce qui existe**, sans type ni étage en plus :
  - drive de piste : le type « Syntakt drive » gagne l'option ANALOG ≈ à côté de DIGITAL (l'OVER exact, par défaut) ;
    même type, même emplacement, mêmes DRIVE et MIX ; mono, sur les voies SIMD, suréchantillonnage global ;
  - master : l'étage de saturation garde sa place et gagne la même option, l'overdrive du DT2 par défaut ; stéréo, deux
    canaux identiques sous un même DRIVE. La page ne montre que les réglages du modèle choisi.
  Aucun set ne change de son.
- Modèle « ≈ », sans machine pour mesurer, d'après la recherche (§7) : saturation douce qui dépend du niveau d'entrée ;
  passe-haut de couplage dont la coupure monte avec DRIVE (grave gardé à mi-course, perdu au maximum) ; compression
  qui fait plafonner la sortie ; bruit de fond qui monte avec le gain. Jugé à l'oreille contre ces comportements, et
  par un test mesuré chacun : sortie monotone en DRIVE, THD croissante avec le niveau d'entrée, courbe de compression,
  grave relatif à DRIVE 64 et 127, bruit selon DRIVE. Le « ≈ » reste affiché ; une mesure sur un Syntakt (entrée
  externe, recherche §5) recalerait le modèle sans changer l'interface.
- Coût estimé : environ 0,1 % d'un cœur par piste où l'option est active (dans la ligne chaîne, ≤ 2 %), 0,2 % au
  master ; mesuré au banc.
