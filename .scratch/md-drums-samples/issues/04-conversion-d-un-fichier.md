# La conversion d'un fichier lâché

Type: grilling
Status: resolved
Assignee: c0remusic (session du 2026-10-17)
Blocked by: 01

## Question

Ce qu'un fichier devient pour le DSP voix (mono, codes de 12 bits par la table de l'OS, un rate en fraction de
44,1 kHz) : les formats acceptés (WAV et AIFF tels que construits, ou FLAC et MP3 aussi en changeant
`source/juce.cmake`), les rates (garder de 4 à 44,1 kHz, rééchantillonner au-dessus, ou la moitié comme l'OS au-dessus
de 48), le stéréo (somme, gauche seule), une boucle lue dans le fichier (chunk `smpl` d'un WAV) ou aucune, le gain
(tel quel, normalisé), et un fichier plus long que la place libre (refusé, coupé, rate réduit).

## Answer

Décidé par l'utilisateur le 2026-10-17, fidèle à l'UW :

- Formats : WAV, AIFF et FLAC. FLAC est réactivé dans `source/juce.cmake` : sans perte, décodeur BSD, peu de poids.
  MP3, Ogg et Windows Media restent coupés.
- Stéréo : la somme des deux canaux, en mono.
- Rate : le rate du fichier est gardé de 4 à 48 kHz, le DSP voix rééchantillonnant par le rapport du répertoire comme
  l'UW. Au-dessus de 48 kHz, rééchantillonné à 44,1 kHz. Sous 4 kHz, refusé.
- Boucle : celle du chunk `smpl` d'un WAV si elle dure au moins 151 échantillons (l'OS ignore les plus courtes), sinon
  aucune.
- Gain : tel quel, sans normalisation.
- Un fichier plus long que la place libre est refusé, avec les secondes qui restent dans le message.
