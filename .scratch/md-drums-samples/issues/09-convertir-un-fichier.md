# Un fichier converti en Sample

Type: task
Status:
Blocked by: 08

## Question

Deuxième tranche, comme « Ce qu'un fichier devient » l'a décidé : un fichier WAV, AIFF ou FLAC (FLAC réactivé dans
`source/juce.cmake`) devient un Sample : mono par la somme des canaux ; le rate gardé de 4 à 48 kHz, rééchantillonné à
44,1 kHz au-dessus, refusé sous 4 kHz ; la boucle du chunk `smpl` d'un WAV si elle dure au moins 151 échantillons ;
le gain tel quel ; codé en 12 bits par la table de l'OS du flash image ; refusé s'il ne tient pas, avec les secondes
qui restent. La lecture du fichier par JUCE dans le plug-in, le codage dans `mdDrums`. Tests : des fichiers écrits par
le test (WAV mono et stéréo, 22,05, 44,1 et 96 kHz, avec et sans boucle, AIFF, FLAC, trop long, trop lent) donnent les
Samples attendus, et l'un joue sur ROM-33.
