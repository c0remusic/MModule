# Les conversions d'X.20 dans MoDule

Type: task
Status:
Blocked by:

## Question

Remplacer les devinettes de `md::mds::convertFine` par ce que fait X.20 ([research/10](../research/10-pitch-and-decay.md)) :
Pitch au mode DEFAULT par sa table L au TUNING 0, Decay par la table d'OS 1.63 que le moteur lit dans le flash image
(`$24bf94`, 512 mots), Unipolar à $7FFFFF à 127 et à 0 sous un réglage, Index tronqué et Boolean non nul entre deux
réglages. `HostModel` donne la table Decay à la conversion ; `BareHost` et `mdsPlay` la prennent du flash image quand
il est là. Tests : les 128 valeurs de Pitch et de Decay contre celles de la recherche, et la Machine de test de
`mdDrumsEngineTest` toujours égale à l'hôte nu.
