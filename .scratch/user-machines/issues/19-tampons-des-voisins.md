# Les tampons des voisins sur le DSP voix de MoDule

Type: research
Status: open
Assignee:
Blocked by:

## Question

Une Machine NEIGHBOUR (les 9 NFX et les 4 SYN-F du zip X.20 B, [research/11](../research/11-mdm1-flags.md)) lit au
RENDER les blocs bruts des quatre Tracks qui la précèdent, par un descripteur de quatre mots que pointe Y:`$7F0`.
X.20 fait tourner ce pointeur sur quatre descripteurs à Y:`$7E0-$7EF` dans sa boucle de blocs (P:`$9b-$a6`) ; OS 1.63,
que joue le DSP voix de MoDule, bascule R7 entre Y:`$100` et `$120` et ne garde donc pas les sorties des Tracks
précédentes. Le banc nu de `mdsPlay` tient déjà le descripteur (`MdsHost.cpp`).

Établir ce que le DSP voix de MoDule (`mdEngine/engine/VoiceEngine.*`, le programme voix d'OS 1.63) doit faire pour
qu'une Track NEIGHBOUR reçoive ses quatre entrées comme sous X.20 :

- où vont les sorties des Tracks dans la boucle par voix d'OS 1.63, et ce qu'il faut patcher ou ajouter (comme les 46
  mots qu'X.20 change) pour garder les quatre dernières ;
- l'ordre des Tracks (1 à 16) et le cas de Track 1 à 4, qui lisent des Tracks du bloc d'avant ;
- les voix que le moteur saute (silence, `setSilenceRelease`) : leur tampon doit être vide, pas l'ancien bloc ;
- une Track muette, choquée ou d'une Machine MID/CTR comme voisine ;
- le coût en cycles et en mots Y, et ce que les Machines intégrées n'ont pas à payer.

Sources : research/01 et 11, le programme voix d'X.20 décodé (scratchpad de la session, comme research/01 le dit),
`mdsPlay` et `MdsHost.cpp`, le code du moteur. Mesurer sur X.20 démarré dans `mdLib` (ticket 02) seulement si la
lecture ne suffit pas.
