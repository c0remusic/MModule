# Un fichier converti en Sample

Type: task
Status: resolved
Assignee: c0remusic (session du 2026-10-17)
Blocked by: 08

## Question

Deuxième tranche, comme « Ce qu'un fichier devient » l'a décidé : un fichier WAV, AIFF ou FLAC (FLAC réactivé dans
`source/juce.cmake`) devient un Sample : mono par la somme des canaux ; le rate gardé de 4 à 48 kHz, rééchantillonné à
44,1 kHz au-dessus, refusé sous 4 kHz ; la boucle du chunk `smpl` d'un WAV si elle dure au moins 151 échantillons ;
le gain tel quel ; codé en 12 bits par la table de l'OS du flash image ; refusé s'il ne tient pas, avec les secondes
qui restent. La lecture du fichier par JUCE dans le plug-in, le codage dans `mdDrums`. Tests : des fichiers écrits par
le test (WAV mono et stéréo, 22,05, 44,1 et 96 kHz, avec et sans boucle, AIFF, FLAC, trop long, trop lent) donnent les
Samples attendus, et l'un joue sur ROM-33.

## Answer

Construit le 2026-10-17 :

- `readSampleFile` (`mdDrumsPlugin/mdDrumsSampleFile.*`, JUCE) lit un WAV, un AIFF ou un FLAC (`JUCE_USE_FLAC=1`
  dans `source/juce.cmake`) : la moyenne des canaux en mono, pour qu'un stéréo à pleine échelle reste dans la plage ;
  le rate du fichier jusqu'à 48 kHz, ramené à 44,1 kHz au-dessus par le rééchantillonneur de JUCE, qui filtre ce qui
  replierait ; la première boucle du chunk `smpl` d'un WAV, déplacée avec le rate.
- `makeSample` (`mdDrums/mdDrumsSampleAudio.*`, sans JUCE) en fait un Sample comme l'UW : son rate en fraction de
  44,1 kHz, ses valeurs en 16 bits telles quelles puis en codes de 12 bits par la table de l'OS (`RomBank::codes`, que
  `loadRomBankFromFlash` lit maintenant), sa boucle si elle dure au moins 151 échantillons, le Sample finissant avec
  elle. Refusé avec la raison : un rate hors de 4 à 48 kHz, un fichier vide, ou plus long que la place, avec les
  secondes qui restent, arrondies vers le bas.
- `Processor::loadSampleFile(rom, fichier)` enchaîne les deux et donne le Sample au Device sous son verrou, le temps de
  le passer ; le thread audio l'écrit ensuite.
- La « somme » des canaux décidée au ticket 04 est leur moyenne : une somme ferait saturer un fichier stéréo dont les
  deux canaux sont à pleine échelle, contre « le gain tel quel ».
- Vérifié par `mdDrumsProcessorTest` (« Sample files ») sur des fichiers que le test écrit : WAV, AIFF et FLAC lus
  pareil ; un stéréo à 22,05 kHz, la moyenne à son rate (le Sample à demi-rate) ; un 96 kHz ramené à 44,1 ; une boucle
  `smpl` de 4 001 échantillons gardée, de 101 écartée ; 3 kHz, trop long et un texte refusés ; un WAV chargé sur ROM-33
  la fait sonner (crête 0,0686, muette avant).
