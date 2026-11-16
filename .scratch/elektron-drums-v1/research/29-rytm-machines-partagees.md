# Rytm : les trente machines partagées avec le Syntakt, mesurées contre le port

Ticket 29. Mesure seule : aucun code du dépôt changé, aucune variante décidée (le choix revient à l'utilisateur).

## Réponse courte

Les 30 routines de contrôle du Rytm MKII OS 1.74 ont été enregistrées sur le banc d'oracle3 (section 3 du Rytm) et
rejouées cas par cas dans le port de Lockstep (`edDrumsAnalog*`, le Syntakt), compilé hors du dépôt. Aucune des 28
machines de son n'est égale au port octet pour octet : toutes écrivent à chaque bloc un mot de niveau qui suit une autre
loi. Mise à part cette loi de niveau, 8 machines sont égales (sept métaux et HH LAB), 9 diffèrent sur 13 à 55 cas, 11
diffèrent sur beaucoup d'autres (BD FM, SILKY, SHARP, ACOUSTIC, SD CLASSIC, FM, NATURAL, RS CLASSIC, les trois synthés). Écarts audibles
établis : la loi de niveau, le clic et le bruit qui suivent LEV sur le Rytm, le modulateur FM. Les autres écarts sont
comptés, leur cause pas toujours établie (section 4). Les deux utilitaires sont égaux.

Lecture du tableau : cas dont les paramètres restent dans 0-127 ; « différents » = au moins un octet comparé diffère ;
« loi de niveau seule » = la seule différence est celle de la section 1 (mot de niveau, et pour les tambours les niveaux
de clic et de bruit, `heads` et `outA`, là où leur loi est vérifiée) ; « autre » = reste. Port tel que Lockstep le joue
(ses tables ajustées) ; entre crochets, avec les tables d'origine du Syntakt quand le compte change. 128+ : comptés à part.

| Machine | 0-127 cas | égaux | différents | dont loi de niveau seule | autre | 128+ cas | 128+ différents |
|---|---|---|---|---|---|---|---|
| BD HARD | 894 | 0 | 894 | 847 | 47 | 183 | 181 |
| BD CLASSIC | 1028 | 0 | 1028 | 990 | 38 | 387 | 377 |
| BD FM | 735 | 0 | 735 | 121 | 614 | 337 | 337 |
| BD PLASTIC | 584 | 0 | 584 | 555 [576] | 29 [8] | 262 | 259 |
| BD SILKY | 688 | 0 | 688 | 0 | 688 | 262 | 262 |
| BD SHARP | 951 | 0 | 951 | 106 | 845 | 452 | 452 |
| BD ACOUSTIC | 581 | 0 | 581 | 472 [497] | 109 [84] | 305 | 304 |
| SD HARD | 894 | 0 | 894 | 839 | 55 | 183 | 181 |
| SD CLASSIC | 1028 | 0 | 1028 | 0 | 1028 | 387 | 379 |
| SD FM | 735 | 0 | 735 | 196 | 539 | 337 | 337 |
| SD NATURAL | 584 | 3 | 581 | 0 | 581 | 262 | 245 |
| SD ACOUSTIC | 688 | 0 | 688 | 650 [688] | 38 [0] | 262 | 261 |
| RS HARD | 951 | 0 | 951 | 938 | 13 | 452 | 435 |
| RS CLASSIC | 581 | 0 | 581 | 0 | 581 | 305 | 287 |
| SY DUAL VCO | 894 | 0 | 894 | 0 | 894 | 183 | 183 |
| SY RAW | 1028 | 0 | 1028 | 0 | 1028 | 387 | 387 |
| SY CHIP | 735 | 0 | 735 | 0 | 735 | 337 | 337 |
| CH CLASSIC | 894 | 0 | 894 | 894 | 0 | 183 | 183 |
| OH CLASSIC | 1028 | 0 | 1028 | 1028 | 0 | 387 | 387 |
| CY CLASSIC | 735 | 0 | 735 | 735 | 0 | 337 | 337 |
| CB CLASSIC | 584 | 0 | 584 | 584 | 0 | 262 | 262 |
| CH METALLIC | 688 | 0 | 688 | 688 | 0 | 262 | 262 |
| OH METALLIC | 951 | 0 | 951 | 951 | 0 | 452 | 452 |
| CY METALLIC | 581 | 0 | 581 | 542 | 39 | 305 | 305 |
| CB METALLIC | 429 | 0 | 429 | 429 | 0 | 324 | 324 |
| CY RIDE | 927 | 0 | 927 | 883 | 44 | 319 | 319 |
| HH BASIC | 827 | 0 | 827 | 785 | 42 | 180 | 180 |
| HH LAB | 945 | 0 | 945 | 945 | 0 | 266 | 266 |
| UT NOISE | 408 | 408 | 0 | – | – | 181 | 0 |
| UT IMPULSE | 447 | 447 | 0 | – | – | 166 | 4 |

Métal (onze machines) : comparés le mot de hauteur, le mot de niveau, les six longs de vitesse des oscillateurs, l'état,
`heads`, `outA` et le bruit ; pas les octets du générateur d'enveloppes, dont la disposition diffère (section 5).

## Méthode

- Enregistreur `elektron-work\tools\analog\rytm_shared_cmp\record_shared.py` (sur `rytmrig.py` et le `reach` étendu de
  `record.py`) : 60 séquences par machine, 32 524 cas, mêmes tirages que `record_rytm.py` (paramètres, bords, hauteur,
  MACSR, longueurs). Chaque cas garde l'instantané avant/après de toute la mémoire que le port a pour région, plus les
  octets que le Rytm a écrits. Vecteurs : `elektron-work\vectors-rytm\shared-{a,b,c,m}.rsv` (`rytm.rav` intact) ; ce que
  le Rytm écrit hors de ces régions, dans `*.unmapped.txt`.
- Correspondances : voix de tambour du Rytm 0, 2, 7 vers les voix 0, 1, 2 du port, trame au décalage `0x5A/0x62/0x6A`
  (CV), `0x168/0x188/0x1A8` (enveloppe), `0x34C/0x35C/0x36C` (niveau) ; état `0x8000FEF8 + 32 v`, `outA`
  `0x80006BF8 + 128 v`, `heads` `0x80007000 + 8 v`. Commutateurs : le Rytm en a 7 par voix (tables `0x401F621C`,
  `0x401F6200`, `0x401F61E4`), appris sur les vecteurs puis confirmés par l'ordre des instructions : Rytm j vers port k
  0→0, 1→1, 2→2, 3→8, 4→3, 5→4, 6→7 ; les commutateurs 5, 6 et 9 du Syntakt n'ont pas d'équivalent (constants sur le
  Syntakt). Métal : voix 4 (charlestons) et 6 (cymbales, cloches) vers la voix 3 du port, mot de hauteur `0x82/0x8E`
  contre `0x3C`, mot de niveau `0x8C/0x9E` contre `0x44`, oscillateurs `0x268/0x280` contre `0x268`.
- Banc de comparaison `elektron-work\tools\analog\rytm_shared_cmp\` (`rytm_shared_cmp.cpp`, `CMakeLists.txt`, build
  `elektron-work\build-rytm-shared`) : compile en lecture seule `edDrumsAnalogShared/Kick/Snare/Synth/Utility/Metal/
  Rates.cpp`, deux passes comme `edDrumsAnalogControlTest` (tables d'origine du Syntakt, puis ajustements du port).
- Équivalence admise : le Syntakt appelle son générateur de bruit avec des réglages à 0 (UT IMPULSE, métal) là où le Rytm
  ne l'appelle pas. [INFER] les deux donnent du silence.

## 1. Loi de niveau (toutes les machines de son)

Le Syntakt lit l'octet de niveau (mot de paramètre 0) dans une table de 18 dB en 127 pas (`0x401E9510` pour les
tambours, `0x402A1BE8` pour le métal) ; le Rytm n'a pas ces tables (cherchées dans toute sa section 3) et écrit
`c · w0²` (`muls.w` puis `mac.l` par une constante : `0x1040C` pour BD HARD, `0x1450F` pour CH CLASSIC, `0xC309` pour
HH LAB...), w0 = valeur << 8 | fin. Mot de niveau mesuré, rapporté à son propre k = 127 :

| k | 0 | 16 | 32 | 64 | 96 | 127 |
|---|---|---|---|---|---|---|
| Syntakt (port) | −18,0 dB | −15,7 | −13,5 | −8,9 | −4,4 | 0 |
| Rytm | silence | −36,0 | −23,9 | −11,9 | −4,9 | 0 |

Mêmes courbes sur toutes les machines où le mot se lit (kicks, SD HARD, SD ACOUSTIC, RS HARD, les onze métaux ; le mot
de SY DUAL VCO dépend d'autres paramètres). Écart à k égal : 10,4 dB à 32, 3,0 dB à 64,
0,5 dB à 96 ; à 0 le Rytm se tait, le Syntakt joue à −18 dB. L'échelle au sommet diffère aussi (BD HARD 65 063 contre
32 767). [INFER] C'est audible si le VCA suit son mot linéairement : un niveau bas est bien plus bas sur le Rytm. Le mot ne
dit pas la loi du VCA du Rytm, que ces vecteurs ne mesurent pas.

Sur les tambours, le Rytm fait aussi passer w0 dans les niveaux de clic et de bruit que la voix reçoit au bloc après le
déclenchement (`heads`, et `outA` pour le clic) ; le Syntakt les tient constants (`0x3F80`, `0x3F800000`). Rapport
Rytm / Syntakt = a · (w0 / 0x3F80)^b, vérifié sur les vecteurs :

| Machines | a | b | écart max |
|---|---|---|---|
| BD HARD, BD CLASSIC, BD FM | 0,500 | 2 | 0,1 % |
| BD PLASTIC, SD NATURAL, SD ACOUSTIC | 0,762 | 2 | 1,5 % |
| BD SHARP, BD ACOUSTIC | 0,51 | 2 | 17 % (BD SHARP), 0,2 % |
| SD HARD | 0,866 | 1 | 0 |
| SD CLASSIC, SD FM, RS CLASSIC | 1,000 | 1 | 0 |
| RS HARD | 1,414 | 1 | 0 |

`outA` suit la même loi au carré (a de 0,5 à 2, b = 2, écart médian 0 à 0,3 %), corps échantillonnés d'ACOUSTIC compris
(532 et 543 cas), et SY CHIP (0,5 · (w0/0x3F80)², sur les 34 cas où le premier long diffère). Pour BD HARD : clic et bruit
à −18 dB de ceux du Syntakt à k = 32, −6 dB à 64, +6 dB à 127. [INFER] Audible : la part du clic dans l'attaque change
avec LEV sur le Rytm, pas sur le Syntakt.

## 2. Modulateur FM de BD FM et SD FM

Le Syntakt calcule le mot de hauteur du modulateur par `modPitchWord` (accord à 1/3, décalage `0xF8555556`), le Rytm par
`pitchWord` (accord à 2/3, décalage `0xF5AAAAAB`, la même routine que la porteuse). Écart : (v − 64) / 2 demi-tons,
v la valeur du paramètre d'accord du modulateur (mot 7 pour BD FM, mot 3 pour SD FM). Vérifié sur 547 cas sur 552 et 447
sur 482 ; les autres sont des CV de quelques unités, où l'arrondi domine. Audible : modulateur égal à v = 64, 32 demi-tons
plus bas à v = 0, 31,5 plus haut à v = 127, donc un autre timbre FM sur presque toute la course.

## 3. Ce qui est égal

- Hauteur : les mots CV de hauteur (mot 0, et mot 1 hors FM) sont égaux sur toutes les caisses, kicks et rims, et sur
  SY CHIP. Le cache de hauteur du Rytm (clé `0x42FB6A78` + 0x18 v, résultat `0x402906B0` + 4 v : constante, hauteur et
  mot d'accord) ne fait aucun écart sur ces vecteurs : la clé couvre ce dont le calcul dépend, et le premier appel le
  manque toujours.
- Bornes : sur les listings lus (BD HARD, BD FM, SD FM), les CV de hauteur sont bornés à 0-0x7FFF par les mêmes
  instructions ; les écarts de 128+ sont comptés à part, comme au ticket 28.
- Tables : toutes celles que le port ajuste ou remplace existent mot pour mot dans la section 3 du Rytm (décroissances
  des kicks, des caisses, des synthés, du bruit, du métal, ride, clic de SHARP, corps d'ACOUSTIC...), sauf
  `kick.longRates`, `rawLevelBelow` et `synthEnvelopeRateBelow`, lues au-delà de 127 seulement. Les décroissances du
  métal sont donc les mêmes.
- Oscillateurs du métal : les six longs de vitesse sortent du même assistant et sont égaux dans 100 % des cas.
- UT NOISE et UT IMPULSE : égaux sur les voix 0, 2, 7.

## 4. Écarts restants (0-127), mesurés, cause établie ou non

- RS HARD, RS CLASSIC, mots 0-1 (6-7 cas) : quand l'enveloppe finit (état −1), le Rytm remet les CV de hauteur à 0 un
  bloc avant le port. [INFER] Inaudible : la voix s'est tue.
- BD HARD, BD CLASSIC, octet d'enveloppe 34 (47 et 38 cas, un par séquence, au bloc après le déclenchement) : cause non
  établie.
- BD SHARP : le niveau passe du mot 3 (Syntakt) au mot 2 (Rytm), le mot 3 du Rytm reste à 0 ; s'y ajoutent les octets de
  niveau 40-42 (761 cas), les commutateurs 4, 7, 8 (761) et l'état 28-30 au déclenchement. [INFER] Mêmes signaux, autre
  câblage ; non établi au-delà du mot de niveau.
- BD SILKY : mot 3 (618) et `heads` (593) suivent une autre loi que celle de la section 1 ; non établie.
- BD PLASTIC, BD ACOUSTIC, SD ACOUSTIC : l'excédent de la passe « ajustements » (21, 25, 38 cas) est celui que les
  ajustements du port font déjà contre le Syntakt (`FittedDiffer`). BD ACOUSTIC garde 26 cas d'état 28-30 au
  déclenchement et 58 du mot 3 : non établi.
- SD HARD : état 12-14 (43 cas), commutateur 3 (11) : non établi.
- SD CLASSIC, SD NATURAL, RS CLASSIC : mots 1 à 3 avec d'autres lois que c · w0² (l'ajustement ne tient que sur 2 à 4 %
  des cas) : non établi.
- SY DUAL VCO (hauteurs 0-1 sur ~200 cas, état 16-18 sur 821), SY RAW (tous les mots, `heads`, commutateurs 1, 2, 7, 8 sur
  ~900), SY CHIP (état 16-18 à chaque bloc : 689) : les synthés diffèrent plus que les autres familles ; cause non
  établie, voix par voix.
- CY METALLIC, CY RIDE, HH BASIC : `outA` (39 à 44 cas, un par séquence, au bloc du clic) ; HH BASIC aussi l'état 8-10.
  Le Rytm y ajoute un `muls.w` que le Syntakt n'a pas. [INFER] Le clic passe par w0, comme pour les tambours.

## 5. Huit voix au lieu de quatre, et le métal

- Les kicks jouent sur les voix 0, 2, 7, les rims sur la 7 seule, chacune avec sa propre trame et sa table de
  commutateurs : les trois voix sont dans les comptes du tableau (une séquence sur trois chacune).
- Le Rytm écrit en plus, sur la voix 7, `0x7FFF` et `0x8000` en trame `0x72/0x74`, que le port n'a pas.
- Le Rytm entretient ce que le Syntakt n'a pas : le cache de hauteur, les dernières CV calibrées (`0x4029067C`,
  `0x40290694`) et des drapeaux de calibration (`0x419CA0B0`, `0x419CA0B8/B9`). Sa calibration saute la mise à jour du
  trim quand la CV a bougé de 16 unités au plus sans redéclenchement (`0x401197F4`). Avec des trims nuls (le banc, comme
  le port) cela ne change rien ; [INFER] sur une unité calibrée, une hystérésis de 16 unités de CV.
- Métal : charlestons (voix 4) et cymbales et cloches (voix 6) sont deux voix sans câblage commutable (aucun appel de
  topologie) avec deux CV au lieu de cinq ; les mots CV 1 à 3 du Syntakt n'y ont pas d'équivalent. Le générateur
  d'enveloppes a 5 canaux (voix 4) et 8 (voix 6) contre 4 et les répartit autrement : au déclenchement, le Rytm remet au
  repos d'autres canaux que le Syntakt (un canal que le Syntakt remet au repos ne l'est au Rytm qu'au bloc suivant), si
  bien qu'aucun appariement octet à octet ne tient d'un pas à l'autre. Les mots de commande et de vitesse viennent des
  mêmes constantes et des mêmes tables (section 3). [INFER] L'écart d'enveloppe se borne au bloc du déclenchement.

## Reste

- Les écarts « non établis » de la section 4 : ce sont les comptes mesurés, sans l'instruction qui les fait. Il faudrait
  lire chaque routine du Rytm contre celle du Syntakt, voix par voix (synthés surtout).
- La loi du VCA du Rytm, donc l'écart en dB réellement entendu pour la loi de niveau et pour les clics : non mesurable
  sur les vecteurs de contrôle.
- Non comparés : les utilitaires sur les voix 1, 3, 5 (BT et toms, qui n'ont pas d'équivalent au Syntakt) et sur les
  voix de métal ; les octets d'enveloppe du métal (section 5).
- Décision de l'utilisateur, par écart audible (loi de niveau, clic et bruit par LEV, modulateur FM) : variante Rytm de
  la machine, ou garder celle du Syntakt.
