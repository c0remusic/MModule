# Le pire kit au garde-fou

Type: AFK
Status: done
Assignee: Claude (session 2026-11-17)
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

## Réponse

`edDrumsWorstKitCost` cherche le pire kit à chaque passe : les 16 machines distinctes les plus chères de toutes les
familles (une machine Syntakt classée sur 4 pistes, un groupe de lanes entier ; les autres sur 16 pistes moins le moteur
vide), le type le plus cher de chaque emplacement de chaîne à 4x avec deux LFO par piste, le type le plus cher de chaque
envoi (`SendTypes`), le master au plus cher. Il mesure chaque étape au minimum de 5 passes de 4 s, imprime chaque guide
et échoue au-dessus de 15 %. Le garde-fou recopie ses lignes dans son journal. L'outillage de mesure est commun avec
`edDrumsCost` (`edDrumsCostBench.h`).

Mesure au garde-fou du 2026-11-17, machine calme (0,4 cœur ailleurs avant les tests) :

| Étape | Mesuré | Guide |
|---|---|---|
| Moteurs | 7,14 % | 10 % |
| Couche | pas encore construite | 1,5 % |
| Chaîne (4x) | 5,86 % | 2,0 % |
| Envois | 0,42 % | 1,2 % |
| Master | 0,29 % | 0,3 % |
| Total | 13,71 % | plafond 15 % |

Le kit trouvé varie d'une passe à l'autre dans le bruit : 9 ou 10 machines Syntakt (SD VINTAGE ou CP VINTAGE en tête,
0,70 à 0,86 %), les autres Machinedrum, une fois SOPHIE 0. La chaîne à 4x
dépasse son guide presque trois fois : il reste 1,3 point pour la couche sample, guidée à 1,5. À trancher avant le J2.
