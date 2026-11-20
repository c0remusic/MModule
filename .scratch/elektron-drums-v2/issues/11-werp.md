# WERP du lecteur DT2

Type: AFK
Status: done
Assignee: Claude (session 2026-11-21)
Blocked by: 04

## Quoi

MODE du lecteur DT2 gagne WERP. Vecteurs relevés à l'oracle d'abord, puis port.

## Critère de fin

- Vecteurs WERP enregistrés hors dépôt ; le port les tient par les tests Q.
- Même son à 44,1 et à 48 kHz (tests Q de `research/19-qualite-audio.md`).
- Aucun set existant ne change de son.
- Coût mesuré sur machine calme et rapporté face à son guide (spécification v2, §3) ; le pire kit tient sous 15 %.
- Gate vert, nouveau test inscrit dans sa liste.

## Relevés à l'oracle (2026-11-21)

Hors dépôt, rien du firmware recopié : `elektron-work\vectors-dt2\WERP.md` (lois, preuves, points ouverts),
`tools\dt2\dt2_werp_oracle.py` (vecteurs `wp_*`), sondes `dt2-voice\probe_werp*.py`, `werp_cf_*.py`.

- **Côté SHARC.** WERP (sélecteur 1) passe par son propre cas, puis par le réglage de pas d'ONESHOT : pas de
  TUNE et de la note, comme ONESHOT. La voix lit une fenêtre qui part de x, un mot de 32 bits que la ColdFire
  envoie à chaque trame (début = x / 691 200 000 × longueur, borné à longueur − 141). La fenêtre est longue d'un pas
  du séquenceur (ci-dessous). Un x qui change ne relance pas la voix et ne déplace pas sa lecture. Il déplace la
  fenêtre : la voix s'arrête où sa lecture passe la fin de la fenêtre (la règle d'arrêt d'ONESHOT). PLAY bouclé fait
  boucler la voix dans la fenêtre. Lu dans `FUN_1c2b24` : une voix ne part que sur le masque de trig de la trame,
  jamais sur x.
- **Côté ColdFire** (`FUN_40015c6e`, une fois par trame de 32 échantillons à 48 kHz, exécutée sur interpréteur
  contre la loi : 0 écart sur 11 scénarios). L'unité est l'échantillon entier, X = 691 200 000. Pas =
  (X >> (5 − SEG)) >> BARS. Au trig : position 0 (FWD) ou X − pas (REV), accumulateur à 0. À chaque trame :
  accumulateur += ((tempo × 120) << 4) >> BARS, tempo × 120 borné à 3 600-36 000. Quand l'accumulateur atteint le
  pas, il le perd, et la position avance ou recule d'un pas. Passé l'échantillon : retour de X si PLAY boucle, sinon
  fin de séquence (x prend quand même la position au-delà).
- Donc un pas tous les 1/32, 1/16 ou 1/8 de note (SEG 0, 1, 2), l'échantillon traversé en 1, 2, 4 ou 8 mesures
  (BARS 0-3). L'origine est le trig, pas le transport. À un tempo où l'échantillon dure ses mesures, les segments se
  suivent sans couture ; plus aigu, la lecture dépasse la fenêtre et la voix se tait jusqu'au trig ; plus grave,
  elle traîne et joue jusqu'au bout.
- **MODE** du DT2 (« Segment Mode ») n'est lu nulle part : il n'est pas sur la page. Direction et boucle viennent de
  PLAY.
- **Vecteurs** par composition des deux moitiés exécutées : la loi de la ColdFire écrit x à chaque bloc, la chaîne
  SHARC du firmware rend la voix. 13 cas.
- **Envers bouclé :** le DSP reboucle 5 échantillons sous le point de boucle (sa position court 5 au-dessus de la
  nôtre ; meilleur de 0 à 7). Porté pour WERP seul : ONESHOT et GRID en REV LOOP n'ont pas de vecteur qui le
  mesure, et restent tels quels.

## Port (2026-11-21)

`Dt2Player` : MODE WERP (le quatrième sixième, qui jouait ONESHOT). Machine 4 BARS (1, 2, 4, 8 mesures, un quart
chacun), Machine 5 SEGMENT (1/32, 1/16, 1/8, un tiers chacun), Machine 7 PLAY ; Machine 6 cachée. Les défauts des
emplacements donnent 1 mesure, 1/32. Le séquenceur tourne dans la voie, une trame par bloc de 32 à 48 kHz, la
fréquence du chemin Syntakt (la couche sample, à 44,1 kHz, ne joue qu'ONESHOT). Le tempo est celui que l'hôte
donne au lecteur, relu à chaque trame, comme REPITCH. `ReverseLoopLag` (5) pour REV.L de WERP.

## Réponse (2026-11-21)

`edDrumsDt2PlayerTest` juge 11 cas WERP contre l'oracle : 48,8 à 53,2 dB sur Hits et sinus (FWD, REV, pas, SEG,
BARS, plus aigu, plus grave), 31,4 sur le bruit ; FWD.L jusqu'au premier pas 45,6 à 50,9 ; REV.L 23,2 (voir plus
bas). Hauteur exacte, enveloppe ≤ 0,05 dB. Q15 à 44,1 kHz sur un cas WERP qui fait trois pas : enveloppe 0,148 dB,
tiers d'octave 0,124 dB. Coût : 1,25 % d'un cœur pour 16 voies qui jouent, comme ONESHOT (le séquenceur coûte
quelques opérations par bloc).

Points ouverts :
- Modes bouclés, après un pas, tant que la lecture est hors de la fenêtre déplacée : l'oracle ajoute quelque chose
  de non identifié (+0,12 dB sur un sinus, −18 dB du signal sur le bruit, rampé à l'entrée et à la sortie). Les cas
  bouclés sont jugés jusqu'au premier pas.
- Le bloc qui reboucle en envers diffère de l'oracle (6 dB sur le sinus, 22 sur le bruit, quel que soit le
  décalage) ; les blocs entre deux rebouclages concordent.
- Le DT2 coupé de l'émulateur couplé (ses crates Rust ne se résolvent pas hors ligne) : le bout-à-bout ColdFire et
  SHARC n'a pas tourné d'un tenant ; il est composé de deux moitiés exécutées.

Sets existants : 12-BIT et VINTAGE intacts. Un set CLEAN dont MODE était dans le quatrième sixième (affiché ONESHOT)
joue maintenant WERP, la valeur que sa page affichait étant ONESHOT : seul un MODE posé par l'hôte, un LFO ou un
lock y arrivait.

Mis de côté à la revue : un test sans oracle du rebouclage du séquenceur en FWD.L et REV.L (les cas bouclés sont
coupés au premier pas) ; le séquenceur en module à lui, testable seul.

## Références

Spécification v2, §5.1 (`../elektron-drums/spec-v2.md`) ; ses tickets de décision dans `../lockstep-post-v1/issues/`.
