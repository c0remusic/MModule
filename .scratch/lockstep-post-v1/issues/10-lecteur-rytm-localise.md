# Où le Rytm rend sa couche sample

Type: task
Status: open
Assignee: 
Blocked by: 08

## Question

« La couche sample du Rytm dans son firmware » n'a pas trouvé la routine du lecteur : rendu sur le ColdFire principal
(section 3, blocs de 32 à 48 kHz, anneau TDM vers les CNA à `0x80000000`) [INFER], ou par le FPGA. Le trancher avec le
banc du ticket 28 de `elektron-drums-v1` (`record_rytm.py`, `rytmrig.py`), sous garde de durée : jouer un sample connu
sur une voix et voir qui écrit sa voie dans l'anneau. Résultat : la routine et ses entrées, ou la preuve que le CPU ne
le rend pas ; dans ce cas la couche ne se porte pas, elle s'approche (dire avec quoi). Avec « Couche du Rytm et
layering du DT2 » : dire aussi si son interpolation et sa loi de BR diffèrent du lecteur DT2, à l'oreille ou à la
mesure ; si oui, il devient un choix de lecteur de la couche sample.
