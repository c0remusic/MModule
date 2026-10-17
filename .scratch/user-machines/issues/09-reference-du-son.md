# La référence du son après « Booter X.20 »

Type: grilling
Status:
Blocked by: 02

## Question

X.20 ne boote pas dans mdLib : il lui manque l'unité MAC du ColdFire (environ 290 instructions de l'OS, et les modes
de son MACSR), puis ce qu'il rencontrera ensuite, inconnu (conversion du stockage au premier boot, pas de +Drive, MIDI
prêt). Poursuivre (émuler la MAC dans Musashi d'après le manuel du ColdFire, sans matériel pour comparer, ticket par
ticket jusqu'à ce qu'un Hit de TX8-BD sorte), ou se contenter, pour 0.1.2, de la spécification MDS et de l'oreille de
l'utilisateur, X.20 dans mdLib devenant une carte à part ? Avec : les conversions Pitch et Decay, qu'aucune autre
source ne donne (lire leur code dans l'OS X.20 décodé par « Décoder l'OS X.20 » en est une troisième voie), et les
trois corrections d'émulateur de la branche `x20-boot` (CMPU, EOR #xxxx, push d'octet), à garder sur `main` ou non.
