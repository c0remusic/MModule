# Faire jouer les Machines NEIGHBOUR

Type: task
Status: resolved
Assignee: c0remusic (session du 2026-10-19)
Blocked by: [Les tampons des voisins](19-tampons-des-voisins.md), [NEIGHBOUR et les Hits sample-accurate](20-voisins-et-hits-sample-accurate.md), [L'aperçu du Hit d'une Machine voisine](21-apercu-d-une-machine-voisine.md)

## Question

Lever le refus de NEIGHBOUR à l'installation (`VoiceEngine.cpp`, message qui nomme aussi STEREO PAIR à tort) et donner
aux Tracks NEIGHBOUR leurs quatre entrées comme le ticket 19 le dit, avec ce que les tickets 20 et 21 décident.
Tests : une Machine NEIGHBOUR de test (comme la scie de `mdDrumsTestMachine.h`) contre le banc nu de `mdsPlay`, et les
autres Tracks inchangées ; latence et CPU avant et après. STEREO PAIR (aucune des 43 ne le pose) vient après.

## Answer

Construit le 2026-10-19, comme research/12 le recommande :

- `69bcbd2` : le harnais remplace la bascule d'OS 1.63 (P:`$9B-$A7`) par un saut vers un stub à P:`$C40` : la Track t
  rend dans le tampon t mod 4 de l'anneau `$100`, `$120`, `$7A0`, `$7C0`, Y:`$7F0` pointe son descripteur, calculé du
  numéro de Track ; le type entre dans R0 comme à `$A3`, X0 et X1 gardent ce que la bascule y laissait. MoDule écrit
  les deux tampons et les quatre descripteurs (Y:`$7A0-$7F0`). Une voix sautée écrit 32 zéros. NEIGHBOUR accepté,
  STEREO PAIR refusé sous son nom.
- Ensuite : la Track NEIGHBOUR prend à chaque bloc le délai de la Track d'avant (ticket 20) ; l'aperçu d'une NEIGHBOUR
  frappe aussi les quatre Tracks d'avant (ticket 21).
- Tests : la copieuse de `mdDrumsTestMachine.h` contre ses sources, bit pour bit (un à quatre en arrière, le bloc
  d'avant pour les Tracks 1 et 2, des zéros d'une Track jamais frappée ou sur GND--) ; sur `mdDrums::Engine`, ses Hits
  calés sur ceux de la Track d'avant à toutes les places du bloc (corrélation au décalage 0) ; l'aperçu qui l'entend ;
  `mdEngineFirmwareTest` inchangé ; CPU (19,7 % d'un cœur) et latence (85 échantillons calés) inchangés.
- Constat en route : la chaîne d'effets de Track garde un brouillon d'une Track à la suivante, comme le DSP mixeur ;
  deux Tracks aux mêmes entrées et paramètres n'ont donc pas forcément la même sortie.
- Reste : STEREO PAIR (aucune des 43 ne le pose), et l'écoute des 13 du zip par l'utilisateur.
