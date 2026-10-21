# Lire l'OS X.20 que l'utilisateur donne

Type: task
Status: open
Assignee:
Blocked by:

## Question

Un lecteur C++ (sans JUCE, à côté de `md::fw::loadRomBankFromFlash`) qui prend le fichier `.syx` de l'OS X.20 et rend
ses quatre charges décodées (research 01 §1-3 de la carte des Machines utilisateur : le flux du premier étage, `S2MD`,
LZMA1 avec le décodeur du LZMA SDK, domaine public, `DSU`), avec leurs CRC vérifiés ; il trouve ce dont les tickets
suivants ont besoin par forme, pas par les adresses d'X.20 B (le code DSP des 6 par sa différence avec 1.63, les
enregistrements de Machines par leurs noms). Il refuse un fichier qui n'est pas un OS X.20 en disant pourquoi.

Tests : un fichier vide, tronqué ou d'un autre OS refusé ; avec `MD_X20_OS`, les CRC, la taille de chaque charge et
le code des 6 trouvé (sauté sans, code 77). Rien de l'OS dans le dépôt.
