# Moteur de l'instrument : 16 pistes, familles mélangées

Type: AFK
Status: ready-for-agent
Assignee: 
Blocked by: 04, 08

## Quoi

Le moteur au cœur du plug-in, parti de ST Drums : 16 pistes, chacune une voix mono de n'importe quelle famille
(Syntakt rapide, MD porté), trigs à l'échantillon près, voix éteinte non rendue, aucun coût fixe par famille.
Changer de machine : rampe de 64 échantillons, défauts de la nouvelle, locks de l'ancienne retirés. Chokes par
cible. Notes 36-51 jouent les pistes. Sorties Main et 15 stéréo, chaque piste sur l'une d'elles. Étage AMP minimal
(niveau, pan, vélocité) en attendant la chaîne.

Noté le 2026-10-27 depuis le ticket 06 : les machines de `syntakt::fast` voient un trig au début d'un bloc de 32
échantillons et le font sonner au bloc suivant, comme la machine. Rien ne place encore un trig à l'échantillon près
dans un bloc. CY ALLOY, rendue à 48 kHz puis rééchantillonnée, place son trig sur sa propre grille de blocs à
48 kHz : à 44,1 kHz, jusqu'à 28 échantillons de l'hôte (0,66 ms) de son instant idéal, selon le bloc. Ce ticket
règle les deux, et ajoute la latence du rééchantillonneur (24 échantillons à 44,1 kHz) au délai commun des Hits.
Depuis, tout le Syntakt rend à 48 kHz par `fast::Host` ; `stFastResampleTest` mesure ce placement sur un cycle de
147 blocs (de −0,75 à +28,4 échantillons, gigue 0,66 ms).

Noté le 2026-10-30 depuis la mesure de coût mixte du ticket 06 (`stFastMixCost`, Ryzen 7 3700X) : 16 pistes sur les
neuf machines au pire cas coûtent 11,3 à 11,6 % d'un cœur à 44,1 kHz quand les instances (une par machine utilisée, voie t
= piste t) sont sommées voie par voie puis rééchantillonnées une seule fois ; un `Host` par machine, soit neuf
rééchantillonneurs, coûte 15,7 à 16,7 %. Contrainte : un rééchantillonneur par famille, après la somme des pistes, jamais
un par machine (`Host<M>` accepte déjà toute source de `renderBlock()`, une somme comprise). Autre fait mesuré : une
voie muette coûte presque autant qu'une voie qui sonne (toutes les machines sauf SP TWINSHOT calculent leurs 16
voies en SIMD), si bien que le coût suit le nombre de machines utilisées, pas de pistes : 6 à 11 µs par machine et
par bloc. « Voix éteinte non rendue » demande donc des instances plus étroites (4 voies pour une machine sur 4 pistes
ou moins) ou le saut d'une instance muette ; au pire cas, 8 machines portent une piste chacune, 65 des 71 µs.

Fait le 2026-10-30 dans le ticket 06 : chaque machine de `syntakt::fast` a `setLanes(n)` (4, 8, 12 ou 16) et ne
calcule que ses voies 0 à n − 1, les autres muettes et figées (`stFastLanesTest` : n voies donnent les échantillons
de 16, bit pour bit). Ce ticket possède l'attribution piste → voie : les pistes d'une machine sur ses premières
voies, `setLanes` au multiple de 4 au-dessus, comme `stFastMixCost` le fait déjà. Une piste qui change de machine
libère sa voie : la voix figée d'une voie reprise repart où elle s'est arrêtée, d'où le changement de machine de ce
ticket (défauts, rampe) sur la voie qu'elle reçoit, et le compactage des voies restantes s'il en libère un groupe. Le
rééchantillonneur reste à 16 voies (une par piste, après la somme). La borne vit sur chaque `Block` ; si un bloc
oublié dans un `setLanes` venait à coûter, la forme sûre à la compilation est un nombre de voies passé à chaque étage
(environ 100 appels).

## Critère de fin

- Un Hit tombe à l'échantillon de sa note, gigue 0, latence mesurée.
- Une piste éteinte ne coûte rien (mesuré) ; une famille absente ne coûte rien.
- Choke et changement de machine sans clic (Q12).
- Plug-in chargé par l'hébergement VST3 de JUCE : sorties sur leurs canaux, mêmes échantillons quelle que soit la
  taille de bloc.

## Références

Spécification §3, §9, §11.2. Ticket « Source de machine d'une piste ».
