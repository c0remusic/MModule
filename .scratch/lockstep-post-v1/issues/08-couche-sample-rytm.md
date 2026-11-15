# La couche sample du Rytm dans son firmware

Type: research
Status: resolved
Assignee: agent
Blocked by: 

## Question

Ce qu'est la couche sample de l'Analog Rytm MKII (OS 1.74) : où elle est rendue (quel processeur, quelle routine, à
quelle fréquence), comment elle se mélange à la voix analogique (avant ou après le filtre et la distorsion analogiques,
ce qui est analogique et ce qui ne l'est pas), ses réglages (manuel), les formats de sample, et ce que les travaux du
Rytm déjà faits (tickets 28 et 29 de `elektron-drums-v1`, `elektron-work\vectors-rytm`, `rytm-samples`, `record_rytm.py`)
en savent déjà. Résultat : ce qu'il faudrait porter, et ce qui ne peut être qu'approché.

## Answer

Recherche : [research/08-couche-sample-rytm.md](../research/08-couche-sample-rytm.md) (2026-11-15).

- Seul le lecteur est numérique : 16 bits, 48 kHz, mono, un par voix (huit), avec TUNE ±24, FINE, BR, SMP, STRT, END
  (END < STRT joue à l'envers), LOOP et LEV. Overdrive, filtre 2 pôles et VCA sont analogiques (manuel).
- Le sample entre à côté du générateur de percussion, avant l'overdrive et le filtre, et suit la voix jusqu'au VCA.
  C'est la place que la spec v1 donne déjà au layering (après la machine, avant les drives pré).
- Rendu [INFER] sur le ColdFire principal (section 3), par blocs de 32 à 48 kHz, dans l'anneau TDM de 16 voies vers les
  CNA (0x80000000), où les toms écrivent déjà leur clic. La routine n'est pas trouvée, et le rendu par le FPGA reste
  possible : c'est à vérifier avec le banc du ticket 28.
- Portable par vecteurs : la traduction des paramètres (pas, bornes, sens, boucle, niveau), et BR et l'interpolation
  s'ils sont sur le CPU. Seulement approchables : la somme analogique avec la machine et leur rapport de niveau,
  l'overdrive, le filtre et le VCA, qui sont déjà les étages de Lockstep.
- Rien des travaux faits ne couvre la couche : les tickets 28 et 29 ne traitent que le contrôle des machines,
  `vectors-rytm` ne contient que `rytm.rav`, et `rytm-samples` est vide.
