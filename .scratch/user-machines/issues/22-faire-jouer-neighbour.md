# Faire jouer les Machines NEIGHBOUR

Type: task
Status: open
Assignee:
Blocked by: [Les tampons des voisins](19-tampons-des-voisins.md), [NEIGHBOUR et les Hits sample-accurate](20-voisins-et-hits-sample-accurate.md), [L'aperçu du Hit d'une Machine voisine](21-apercu-d-une-machine-voisine.md)

## Question

Lever le refus de NEIGHBOUR à l'installation (`VoiceEngine.cpp`, message qui nomme aussi STEREO PAIR à tort) et donner
aux Tracks NEIGHBOUR leurs quatre entrées comme le ticket 19 le dit, avec ce que les tickets 20 et 21 décident.
Tests : une Machine NEIGHBOUR de test (comme la scie de `mdDrumsTestMachine.h`) contre le banc nu de `mdsPlay`, et les
autres Tracks inchangées ; latence et CPU avant et après. STEREO PAIR (aucune des 43 ne le pose) vient après.
