# Machinedrum : TrackFx en flottant SIMD, Echo et Gate Box

Type: AFK
Status: resolved
Assignee: 
Blocked by: 02

## Quoi

Réécrire la chaîne de piste du MD (EQ, filtre, SRR, DIST de `TrackFx`, sans l'AMD) en flottant SIMD sur 16 pistes,
avec une entrée publique par étage pour que la chaîne à emplacements les place seuls. Porter l'Echo et la Gate Box
de son master comme sources de delay et de reverb. SRR garde sa fréquence absolue.

## Critère de fin

- Chaque étage contre `mdEngine` : Q2 (linéaire) ou Q3 (non linéaire) à 44,1 kHz ; Q13 sur SRR à fond.
- Coût de la chaîne MD pour 16 pistes mesuré.

## Noté par le ticket 09 (2026-11-05)

La table de l'OS à $24DA14 (`OsTables::onePole`) n'a pas de formule : P-I l'importe de la flash image, par l'exception
de la spec 11.4. L'Echo et la Gate Box la lisent aussi. Ce ticket passe par ce même import tant qu'une formule manque, et
en trouver une lève la dette.

## 2026-11-05 : fait

`md::fast` (`source/elektron/md/mdFast/`) donne une entrée par étage, `Eq`, `Filter`, `Srr`, `Dist`, chacune `set(lane, ...)` et
`process(Block&)`, sur le `Block` de `syntakt::fast`, plus `Echo`, `GateBox` et `SendWords`. `mdEngine` reste la référence, dans
les tests seulement, et n'a pas changé. `mdFastTrackFxTest` et `mdFastSendsTest` entrent au garde-fou.

Écarts au ticket et à la spécification (amendée §3.2, §11.2) :
- La chaîne n'est pas en flottant simple. EQ, filtre et DIST tournent en doubles SSE2, deux pistes par vecteur, avec les mots et
  les planchers du DSP ; SRR seul est en flottant. Le filtre en flottant, forme directe, descend à -88 dBFS à FLTF 0 sur un
  signal à -34 dBFS, et Q3 tombait sur les bandes faibles (-9,7 dB à FLTF 0, FLTW 8 ; -42,6 dB de pire fenêtre à FLTF 8, FLTW 8).
  Le calcul sur différences premières reste limité par le bruit de rétroaction 24 bits de l'oracle, à -94 dBFS. Après
  optimisation la version exacte coûte moins que la version flottante, jamais finie (6,20 contre 5,68 µs avant ses corrections) :
  l'arbitrage est clos.
- La boucle de l'Echo reste en mots entiers, entrée et retour en flottant. FB au-delà de 64 donne un gain de boucle supérieur à 1 :
  tout écart d'arithmétique croît à chaque passage, et le flottant rate Q3 (pire fenêtre 50,5 dB à FB 127, 28,6 dB à FB 100
  sur bruit pleine échelle). L'enveloppe et le gain du gate de la Gate Box restent aussi en entiers.

Tables : aucun mot du firmware dans le dépôt, le test compare chaque table à l'OS à l'exécution. Coupure A et EQ A : cos(w) ;
coupure B et EQ B : exp(-w) ; w = 2π·5·4000^(i/2048)/44100 pour le filtre, 2π·20·1000^(i/2048)/44100 pour l'EQ ; 0 mot différent.
Résonance : Q = 0,501·(2√2/0,501)^(i/127), 0 différent. Décalages EQ : 0 différent, par 17 constantes ajustées (pôle et zéro
exp(polynôme), pas de forme close trouvée). DIST gain et filtre : (2^23-1)·64^(...), 15 et 11 mots sur 257 à 1 LSB. Trois mots
de `eqCos` (indices -55, 945, 1911) sont arrondis vers le haut par l'OS à 0,00015 sous la demie, sans explication : seule
`eqCos` prend le biais 0,5002. Gate Box : table de déclin 2 mots sur 384 à 1 LSB, table d'amortissement 0 différent ; ses cinq
gains de passe-tout sont des décimales écrites, sans formule. Le filtre est deux passe-bas à FLTF+FLTW puis deux passe-haut à
FLTF : les commentaires de `TrackFx` ont les deux inversés.

Mesuré à 44,1 kHz, 2496 voies sur grilles d'EQ, de filtre, de SRR et de DIST, mouvements balayés et sauts, Hits de TRX, EFM, E12,
P-I et ROM, bruit à 0,9 et 0,1, sinus :
- EQ et filtre : 0 échantillon différent, rails, sauts, transitoires instables et rampes compris (Q2 tenu au mot près).
- SRR à travers DIST et chaîne entière : -135,4 dBFS RMS, -110,5 max (Q13 à Q2) ; DIST 115,3 dB de SNR, 78,0 dB de pire
  fenêtre (Q3). Le seul résidu, 111 367 échantillons, vient des 26 mots à 1 LSB de DIST.
- Echo : 0 échantillon différent sur 16 scénarios, queues comprises, FB 127 et 30 s à gain unité compris.
- Gate Box, rendu entier : pire -113,7 dBFS RMS et -97,1 max à DEC 127 sur bruit pleine échelle (Q2), défauts -136,7 et -121,1. Le
  résidu est la troncature du DSP à chaque écriture, non reproduite.
- Un seul endroit où le port s'écarte de la machine émulée : à MOD 127, TIME 127, tempo 720, atteint par un saut de tempo, la
  lecture de l'Echo descend à -67 mots sous la ligne, là où le DSP joue son propre code de boot comme échantillons (-91,9 dBFS
  RMS, -29,7 max, un clic). Ces mots sont du firmware et ne peuvent pas être livrés : le port lit des zéros, le test efface le code
  de boot de l'oracle pour rester exact, et vérifie que le cas diffère bien sans cet effacement.
- Q14 : sans FTZ ni DAZ, aucun dénormal sur la chaîne (drapeaux DE et UE de MXCSR, témoin compris). Le ratio de temps est trop
  fin sur Ryzen 3700X pour prouver quoi que ce soit seul. Gate Box sous FTZ, 1,01 à 1,04 x.
- Mots des paramètres : `echoWords`, `gateBoxWords` et `SendWords`, portés des routines 68k de l'OS, 292 574 jeux de mots identiques
  à `MachineRunner`, plus 4000 ticks de lissage. La routine de la Gate Box est à $20B940, $20B604 étant celle de Dynamix.

Coût (release, minimum de 7 passages, machine à 28 %, 16 pistes, bloc de 32 échantillons, 726 µs) : EQ 0,76 µs, filtre 2,38 fixe
et 4,10 en mouvement, SRR 0,24 et 0 à SRR 0, DIST 1,08 ; chaîne mesurée entière 4,47 µs (0,62 %) fixe, 6,20 µs (0,85 %) en
mouvement, contre 26 µs (3,6 %) pour `TrackFx`. Echo 0,55 à 0,68 µs (0,09 %), Gate Box 2,47 µs (0,34 %), contre 19,6 µs (2,70 %)
pour la section master émulée entière. Mémoire : Echo 776 488 octets, Gate Box 114 852 octets de lignes. Il reste 0,4 µs de
voie par piste pour une piste muette : voir le ticket 11.

Ce que les revues ont changé :
- Efficacité : les récursions étaient limitées par la latence, pas par la largeur du vecteur. Ajouter la rétroaction en dernier,
  fusionner les quatre sections du filtre en une boucle, garder les sept mots en doubles et sauter SRR à 0 donnent 11,27 à 6,20 µs.
  Cette réécriture ne tient que si les sommes partielles restent sous 2^53 : mesuré à 10,94 sur 64 sur tout le test, pire cas
  arithmétique autour de 96, non prouvé ; au-delà, seuls les bits bas d'un accumulateur très au-delà de son rail bougeraient.
- Revue de code : le test Q14 ne mesurait rien (FTZ posé par le test) ; un scénario dont l'oracle se tait passait partout ;
  la comparaison s'arrêtait à -100 dBFS, sans queue ; `setTempo(0)` divisait par zéro sur le fil audio ; `differences` ne bornait pas
  l'écart.
- Altitude : `Guard` de l'Echo, 64 mesuré, lisait 3 mots hors du vecteur à -67 : c'est une borne dérivée désormais, 82.
- Architecture : `mdFast` lie `stFast` au lieu d'un chemin d'include, les en-têtes des effets ne tirent plus `mdVoice`, arithmétique
  commune dans `mdFastDsp.h`, chargeur TRX commun `importTrx` à côté de `importPi`.
- Écartés : AVX2 (0,22 % d'un cœur pour environ 150 lignes et une seconde entrée de test, à reprendre si le budget le demande) et
  Gate Box 4 voies (2,68 à environ 1,9 µs estimés, 60 lignes).

Bug de l'émulateur trouvé en chemin : sous JIT, un bloc pouvait finir entre un REP et son corps, quand le corps lisait un registre
M écrit plus tôt dans le bloc. `emit` émet les deux ensemble, le PC suivant tombait sur le corps, exécuté une fois de plus. Le
boot du mixeur effaçait ainsi un mot de trop à P:$143DC0, d'où un compteur de gate à $9C40 au lieu de 0 jusqu'au premier Hit, sur
l'oracle comme dans MoDule. Corrigé dans `dsp56300` (`JitBlock::getInfo` analyse le REP et son corps comme un seul op) ;
`mdFastSendsTest` échouerait à nouveau au premier gate si ça revenait.
Le boot corrigé décale le mixeur : le banc de `mdEngineFirmwareMaster`, en blocs de 256 trames, ne l'arrêtait plus jamais dans son
attente de bloc (P:$3C-$43) et le test échouait. `Bench::nudge(10)` une fois après le boot décale la trame, tentative 0 dans
les 7 scénarios. Un décalage entre l'état pris et son Hit fait diverger Main de -75 dB à l'attaque : il se fait donc avant le
premier scénario, jamais dans sa boucle.

## Références

Spécification §3.2, §4, §5. Tickets « Moteur du Machinedrum », « Sources des effets d'envoi ».
