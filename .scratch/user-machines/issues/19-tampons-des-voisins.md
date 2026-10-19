# Les tampons des voisins sur le DSP voix de MoDule

Type: research
Status: resolved
Assignee: c0remusic (session du 2026-10-19)
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

## Answer

[research/12](../research/12-neighbour-buffers.md), le 2026-10-19, par lecture (X.20 non démarré) :

- Le Y d'X.20 que research/01 n'avait pas décodé donne quatre tampons : Y:`$100`, `$120` (les deux d'OS 1.63),
  `$7A0` et `$7C0`. La Track t écrit le tampon (t − 1) mod 4 ; les Tracks 1 à 4 lisent les 13 à 16 du bloc d'avant,
  que la mémoire du DSP garde sans rien de plus.
- Recommandé : patcher le programme voix d'OS 1.63 comme X.20 (les 13 mots de P:`$9B-$A7`, le pointeur calculé du
  numéro de Track plutôt que tourné), écrire les 81 mots Y d'X.20 à Y:`$7A0-$7F0` à chaque reset (OS 1.63 n'y touche
  pas), accepter NEIGHBOUR dans `checkUserMachine` et refuser STEREO PAIR sous son nom. Copier les tampons depuis le
  stub ou le C++ est écarté. Rien ne change dans HostModel, le moteur, les effets ni le mixeur.
- Le vrai défaut : une voix que le moteur saute (jamais assignée, GND--) laisse son tampon tel quel ; avec quatre
  tampons, un voisin entendrait la Track quatre places plus loin. Il faut y écrire 32 zéros, ce que fait le rendu vide
  d'OS 1.63 (35 à 40 cycles de plus par voix sautée).
- Muettes, choquées et MID/CTR rendent toujours leur voix : un voisin entend leur son brut, avant les effets de
  Track, comme le Standard le dit.
- Coût : 81 mots Y dans un trou libre ; l'anneau coûte ce que coûte la bascule à deux tampons d'OS 1.63 ; le rendu
  d'une NEIGHBOUR mesuré de 204 à 3 189 cycles. Les Machines intégrées ne changent pas, seule l'adresse de sortie
  bouge pour la moitié des Tracks (à vérifier au ticket 22, avec deux registres que le patch ne laisse plus comme OS
  1.63 ; repli : un stub de 4 cycles par voix).
- Les 13 paquets lisent tous le descripteur à Y:`$7F0` : 7 ne lisent que la Track d'avant, 6 deux ou trois en
  arrière, NFX-MX et NFX-EN aussi la quatrième.
- Pour le ticket 20 : sous PRECISE, X.20 décale chaque Track à l'instant de son trig dans le DSP avant que les
  suivantes la lisent ; MoDule décale après le DSP, d'où l'écart de −31 à +63 échantillons. Deux pistes de plus :
  suivre le délai de la Track d'avant (exact pour les 7 à une entrée) et décaler dans le DSP comme X.20.
- Pour le ticket 21 : la requête d'aperçu ne porte aucun paquet ; toute Machine utilisateur, pas seulement NEIGHBOUR,
  s'y entend muette aujourd'hui.
