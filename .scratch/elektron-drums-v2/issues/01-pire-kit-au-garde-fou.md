# Le pire kit au garde-fou

Type: AFK
Status: ready-for-agent
Assignee:
Blocked by: 

## Quoi

Le banc `edDrumsWorstKitCost` joue le pire kit de la spécification v2 (§3) : 16 pistes sur les machines les plus
chères, en maximisant les machines distinctes ; les 16 couches sample allumées dès qu'elles existent ; chaque emplacement
de chaîne au type le plus cher ; envois et master au plus cher. Machine calme, 44,1 kHz, blocs de 128, minimum de 5
passes de 4 s. Il donne le pourcentage d'un cœur par bloc (moteurs, couche, chaîne, envois, master) et au total, et
entre au garde-fou comme test de temps (relancé une fois s'il échoue seul sous charge). Aujourd'hui `edDrumsCost` se
lance à la main.

Chaque ticket suivant qui ajoute un type ou une machine plus chère l'ajoute au pire kit.

## Critère de fin

- Le banc échoue au-dessus de 15 % au total ; il imprime chaque guide et son écart.
- Il tourne au garde-fou et sa mesure figure dans le journal.
- Gate vert.

## Références

Spécification v2, §3 (`../elektron-drums/spec-v2.md`) ; ses tickets de décision dans `../lockstep-post-v1/issues/`.
