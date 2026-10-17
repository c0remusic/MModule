# Faire jouer TX8-BD sur un DSP56300 nu

Type: prototype
Status:
Blocked by:

## Question

Notre lecture de l'ABI MDS suffit-elle à faire sonner une Machine utilisateur ? Un hôte minimal, hors du plug-in :
lire le paquet MDS1 de TX8-BD du zip, le reloger, fournir les imports 1, 6, 7 et 8, poser ses 8 paramètres convertis
en Y+1 à Y+8, appeler INIT, TRIG puis RENDER par blocs de 32 sur un DSP56300 émulé seul, écrire un WAV. L'utilisateur
l'écoute ; si « Booter X.20 dans mdLib » a donné un Hit de TX8-BD, les deux se comparent. Avec : les cycles réels par
RENDER contre les 3 951 du budget, et ce que l'ABI laisse flou.
