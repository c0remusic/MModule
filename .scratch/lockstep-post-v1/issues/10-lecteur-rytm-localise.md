# Où le Rytm rend sa couche sample

Type: task
Status: resolved
Assignee: agent (session du 2026-11-16)
Blocked by: 08

## Question

« La couche sample du Rytm dans son firmware » n'a pas trouvé la routine du lecteur : rendu sur le ColdFire principal
(section 3, blocs de 32 à 48 kHz, anneau TDM vers les CNA à `0x80000000`) [INFER], ou par le FPGA. Le trancher avec le
banc du ticket 28 de `elektron-drums-v1` (`record_rytm.py`, `rytmrig.py`), sous garde de durée : jouer un sample connu
sur une voix et voir qui écrit sa voie dans l'anneau. Résultat : la routine et ses entrées, ou la preuve que le CPU ne
le rend pas ; dans ce cas la couche ne se porte pas, elle s'approche (dire avec quoi). Avec « Couche du Rytm et
layering du DT2 » : dire aussi si son interpolation et sa loi de BR diffèrent du lecteur DT2, à l'oreille ou à la
mesure ; si oui, il devient un choix de lecteur de la couche sample.

## Answer

Fait le 2026-11-16 : [research/10-lecteur-rytm.md](../research/10-lecteur-rytm.md) (section 3 lue, puis exécutée au
banc oracle3 avec un eDMA émulé et des samples synthétiques, sous garde de durée ; scripts hors dépôt).

- **Rendu sur le ColdFire principal**, pas par le FPGA (certitude haute). Par bloc de 32 à 48 kHz, dans l'interruption
  audio : `0x4011C1E0` puis `0x4011C6B8` (une voix rendue pendant que l'eDMA amène la fenêtre de la suivante), puis
  `0x4010EA98` écrit la voix v dans la voie v de l'anneau TDM. Le sample y est sommé **en numérique** au bruit et au
  clic de la machine ; seul l'oscillateur analogique s'ajoute après le CNA [INFER]. Cela corrige « La couche sample du
  Rytm dans son firmware », qui le croyait sommé en analogique ; sa place dans la voix (avant overdrive et filtre) ne
  change pas.
- **Interpolation : celle du DT2.** Même schéma (64 échantillons à 96 kHz, 6 prises, 256 phases, décimation 2:1) et
  même noyau, arrondi à 16 bits (écart sous un demi-LSB). Pas de lecteur Rytm à part pour l'interpolation.
- **BR : diffère.** Rytm : dans la voix, avant LEV, pas de quantification continu, environ 9,9 − 0,073 × BR bits
  équivalents (BR 0 contourné, BR 127 ≈ deux niveaux). DT2 : après la voix, table de 128 en puissances de deux, de 16 à
  1 bit.
- **Autres lois mesurées :** hauteur 2^(s/12) sur une table de 2 048 pas par octave, s borné à ±24 demi-tons (au plus
  −0,4 cent d'écart) ; LEV = (LEV/127)², rampé sur le bloc ; STRT/END = longueur × valeur/120, END < STRT à l'envers ;
  LOOP OFF : niveau divisé par 4 à chaque bloc après la fin ; un trig agit au bloc suivant.
- **Ouvert :** le décimateur du DT2 n'est pas décodé (celui du Rytm est récursif, plat à ±0,3 dB jusqu'à 15 kHz) ; à
  comparer quand « Oracle de la voix du DT2 » tournera. La loi de LEV du DT2, la vélocité et le LFO n'ont pas été
  relevés.
- **Conséquence pour « Couche du Rytm et layering du DT2 »** : la couche sample se porte sur le lecteur DT2 en ONESHOT.
  Ce qui distingue le Rytm n'est pas un lecteur mais une loi de BR. Décidé avec l'utilisateur le 2026-11-16 : la BR
  propre de la couche suit **la loi du Rytm**, sans choix (pas continu, sur le sample seul avant LEV, comme le ticket 09
  la place) ; le type de chaîne BR garde la loi du DT2, qui agit après la voix comme lui. Un son DT2 importé garde
  ainsi sa BR exacte dans la chaîne. À confirmer à l'oreille quand le lecteur tournera. La traduction à l'import garde
  ±24, LEV au carré et le fondu de fin. Si le décimateur du DT2 se révèle différent, celui du Rytm s'ajoute en variante.
- **Suite (2026-11-16) :** « Oracle dynamique de la voix sampler du Digitakt II » a mesuré le décimateur du DT2 : la voix
  suit celle du Rytm à 0,35 dB près jusqu'à 23 kHz, pas de variante. Sa loi de LEV diffère : 2 × (LEV/128)².
