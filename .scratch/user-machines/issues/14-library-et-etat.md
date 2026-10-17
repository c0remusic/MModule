# La Library et l'état du set

Type: task
Status:
Blocked by: 12

## Question

Troisième tranche, comme « Où vivent les Machines utilisateur installées » l'a décidé : la Library dans le dossier
de données de MoDule (un fichier par numéro, le paquet tel qu'installé), lue au démarrage et relue quand elle change,
comme le Bank ; l'état du plug-in embarque le paquet de chaque Machine utilisateur que son Kit joue (un bloc de plus
dans l'état, à côté de « KSLT ») ; à l'ouverture, la copie du set joue ; chaque instance garde sa copie jusqu'à ce
qu'elle charge un Kit du Bank, un set, ou choisisse cette Machine ; rien ne change de son pendant que l'hôte joue.
Tests : un set restauré sans Library joue sa Machine ; deux instances, deux Machines en USR-05, chacune la sienne ;
un Kit du Bank joue ce que la Library a.
