# 07 — État réel de l'OS du Model:Cycles et ordre de ses machines

Recherche du 2026-11-16, ticket [07](../issues/07-etat-os-cycles.md), suite du ticket 11 de la planification v1
(`elektron-drums/research/11-model-cycles.md`). Section 3 de l'OS 1.13 (`elektron-work\model-cycles-1.13\`,
SHA-256 `cc99d4f0…cb99ee98`), lue et exécutée sur place ; scripts hors dépôt dans
`C:\dev\elektron\elektron-work\tools\cycles\` (`cyclesbench.py`, `vectors_cycles.py`, `analyse_cycles.py`,
`cost_cycles.py`, `run_all.ps1`), vecteurs dans `C:\dev\elektron\elektron-work\vectors-cycles\`. Inférences marquées
[INFER].

## Réponse courte

- **Ordre confirmé : 0 KICK, 1 SNARE, 2 METAL, 3 PERC, 4 TONE, 5 CHORD**, celui du manuel. Trois preuves
  indépendantes de l'indice deviné : les fiches de paramètres de l'OS portent le numéro de machine à côté du nom
  (« Snare Color » : machine 1, etc.) ; le mot ALG que ces fiches décrivent est celui que lit le répartiteur, et l'indice
  du rendu exécuté est celui d'ALG (relevé au banc) ; le son de chaque indice est celui de son nom (§3).
- **État réel :** pas un boot complet (digiemu exige son Unicorn patché, non construit ici, et le construire demande de
  télécharger les sources d'Unicorn). L'état vient de l'OS lui-même : chaque son est bâti avec les valeurs par défaut de
  ses fiches de paramètres, chargé par les routines de chargement de l'OS, puis chaque bloc passe par le lissage, la
  modulation de vélocité, le module LFO, le répartiteur, le gain de piste et VOL+DIST de l'OS, dans l'ordre de la tâche
  audio. Les sons sont musicaux cette fois : KICK à 70 Hz qui chute de 40 dB en 580 ms, TONE au do 4, etc.
- **Vecteurs :** 108 cas (6 machines × 18), 1 000 blocs chacun (0,67 s), sortie de voix et sortie de piste, plus un
  WAV par cas ; format et régénération dans le `README.md` du dossier.
- **Coût mesuré** (instructions ColdFire, update + render d'une voix, par bloc de 32) : KICK 7 444, SNARE 7 166,
  METAL 6 439, PERC 6 087, TONE 5 305, CHORD 8 303. Il ne dépend presque pas de ce que joue la voix : à 141 ms du
  trig, son éteint ou non, il bouge de moins de 1 %.

## 1. L'ordre : ce que dit l'OS

**Les fiches de paramètres.** Une table de 76 fiches de 0x38 octets à `0x4010dce0` (lue par les accesseurs
`0x4005a76a`, `0x4005a7aa`, `0x4005a660`) décrit chaque paramètre : +0 machine ou page, +4 mot dans le bloc de piste,
+8 min, +0xc max, +0x10 défaut, +0x18 CC MIDI, +0x2c nom, +0x30 groupe, +0x34 nom court. `0x4005a7aa(i)` rend le
champ +0 de la fiche i s'il vaut 0 à 5 ; `0x4000bf9e(liste, m)` rassemble les fiches dont ce champ vaut m : les
paramètres propres à la machine m. Les fiches 46-75 sont, par blocs de cinq, COLR, SHPE, SWEP, CONT (mots 11 à 14) et
une Amp Decay (mot 18) :

| champ machine | noms des fiches | CC |
|---|---|---|
| 0 | Kick Color, Kick Shape, Kick Sweep, Kick Contour | 16-19 |
| 1 | Snare Color … Snare Contour | 16-19 |
| 2 | Metal Color … | 16-19 |
| 3 | Perc Color … | 16-19 |
| 4 | Tone Color … | 16-19 |
| 5 | Chord Color … | 16-19 |

La fiche « Algorithm » (nom court ALG, page 7) est le **mot 9**, de 0 à 0x500 : six valeurs, la machine en octet
haut. La liste « DrumSelect » (`0x401177e4`, voisine de « Save sound » [INFER : le classement des sons]) suit le même ordre : Kick, Snare, Metal, Perc, Tone,
Chord.

**Le répartiteur lit ce même mot.** `0x400a7d4a` lit l'octet `18(a2)` avec a2 = bloc lissé + 0xe + 0x42 t, soit l'octet
haut du mot 9 de la piste t, le borne à 0-5 et le traduit par la table `0x40118640`, l'identité 0..5 (lu dans
l'image). Le moteur choisi indexe le rendu `0x40118610` et l'update `0x40118628`. La disposition des mots est
cohérente : l'update de KICK lit COLR, SHPE, SWEP, CONT, PAW et DEC aux mots 11, 12, 13, 14, 15 et 18, ceux des fiches ;
le répartiteur prend GATE au mot 16 (`32(a2)` → voix + 0x24c).

**Au banc.** Avec ALG = m << 8 dans le son, la voix exécute, au bloc 3 (après le changement de machine, qui se fait au
trig, §2), le rendu d'indice m pour les six m : `0x400aa3c4`, `0x400ab6e8`, `0x400aa712`, `0x400aacc2`,
`0x400aa930`, `0x400ab24c`.

## 2. Le banc

`cyclesbench.py` : Unicorn 2.1.4 d'origine, CPU « ANY », section 3 à `0x40000400`, 256 MB de DDR et la SRAM.
L'EMAC passe entière par l'EMAC Python de SyntHack (MIT) : les sites sont trouvés par descente récursive depuis toutes
les routines appelées, en suivant aussi les adresses de code chargées par `lea`/`pea`/`move.l #` (appels par registre)
et la table des formes d'onde du LFO (`0x40115e94`, six fonctions appelées par pointeur, trouvée parce que la
vérification l'avait signalée). Vérification : un crochet de bloc note tout bloc de traduction exécuté hors du code
atteint par la recherche ; il n'y en a eu aucun sur les 18 passes et les 12 passes de coût. Sans cela, l'EMAC
d'Unicorn d'origine exécute les MAC avec chargement de travers (adresse masquée à 0 : faute constatée au premier
essai à `0x400a82d0`).

État, dans l'ordre :

1. `0x4000045c` (copie des images SRAM), mise à zéro du BSS à partir de `0x4019b590` comme `0x400004b2` ;
   `0x400564e2` puis la boucle de `0x4005974c` : pointeur `+0x318` de chaque voix posé, puis `0x400a7ab8` (posé
   avant : la première passe de l'OS efface 0x30 octets à l'adresse 0, non mappée ici). `0x400582c4` (délai et
   réverbération) n'est pas appelé.
2. Un son par piste, à la disposition que lit l'OS : 33 mots de paramètres à +0x14 (mot k = paramètre de mot k),
   destination de vélocité +0x58, quantité +0x5c, courbe +0x60. Les mots viennent des fiches : défauts des pages 7
   (Drum, Amp) et 17 (LFO), puis ceux de la machine (ses quatre fiches et l'Amp Decay qui les suit), ALG = m << 8.
   Chargé par `0x40058a0a(son, t)` (copie dans les cibles `0x800015a0 + 0xe + 0x42 t`, état du lisseur posé sur la
   cible par `0x400582d0`) et `0x40058c1c(son, t)` (destination et quantité de vélocité aux cibles +0x1dc / +0x1f4).
3. Le trig, comme le fait la file d'événements (`0x40058d30`-`0x400591a4`) : vélocité au mot `0x40fe4cdc[t]`, gain
   `0x40fe4cd0[t] = 0x40056dfe(vélocité, courbe)` ; note à `0x80001830[t] = note << 16` (événement +0x1c, bit 0x10 des
   drapeaux), premier argument de l'update, ajoutée à la hauteur en demi-tons 16.16 par `0x400a7e6c`.
4. Chaque bloc, l'ordre de `0x4005979e` : `0x40058474(cibles)` → bloc lissé `0x8000100c` ; `0x40056e24(lissé,
   cibles)` ; `0x40091ab2(lissé, [0x40149310], trigs)` ; `0x400a7d4a(0x80001858, lissé, trigs, gates)` ; puis
   `0x40056d9c` (gain de piste), `0x4008f1f0` (copie des gains, un memcpy) et `0x40056f58` (VOL+DIST). Le mix, le
   délai, la réverbération et l'étage de sortie ne tournent pas.

Constats du banc :

- **Un trig sonne au bloc suivant** : masque au bloc 1, premier échantillon non nul au début du bloc 2 (échantillon
  64 ou 65 ; 97 pour CHORD). Le changement de machine attend aussi le trig : le répartiteur ne le
  fait qu'avec le drapeau voix + 0x38, recopié du trig du bloc précédent.
- **La vélocité n'entre pas dans la voix** (avec une modulation de vélocité neutre) : les cas vélocité 100, 64 et 32
  ont la même sortie de voix, octet pour octet, que le défaut (127) ; elle agit après, par le gain de piste, linéaire :
  crête de piste × 100/127, 64/127, 32/127 à 0,1 % près.
- **GATE éteint, le masque de gate ne change rien** : gate tenu 1 bloc ou 64 blocs, même sortie.
- **Une voix ne dépend pas de sa piste** : les mêmes réglages sur la piste 1 d'une passe et les pistes 3 à 6 d'une autre donnent la même sortie, y
  compris SNARE et METAL : le bruit part du même état sur chaque voix [INFER : graine par voix identique, non lue].
- **La note règle la hauteur absolue** : avec la note 60 et PTCH au centre, TONE sonne à 258 Hz (le do 4, à la
  résolution de 11,7 Hz près) et KICK à 70 Hz ; PTCH ± 12 donne l'octave exacte (KICK 140,6 / 35,2 Hz, TONE 527 /
  129 Hz). Sans note (0), PTCH n'a plus d'effet sur KICK (mesuré : PTCH − 12 donne la sortie du défaut ; la hauteur reste à
  la borne basse de `0x400a7e6c` [INFER]) : c'est ce qui rendait le
  sondage du ticket 11 non musical, avec ses mots de paramètres nuls.

## 3. Le son de chaque indice

`analyse_cycles.py`, cas par défaut : spectre de 4 096 échantillons depuis l'attaque (Hann), platitude spectrale
300 Hz-16 kHz (1 = bruit blanc), pics principaux, chute à −40 dB (RMS sur 10 ms).

| ALG | nom | crête | −40 dB | platitude | spectre | lecture |
|---|---|---|---|---|---|---|
| 0 | KICK | 0,23 | 580 ms | 0,000 | 70 Hz, rien au-dessus de −45 dB | sinus grave balayé |
| 1 | SNARE | 0,31 | 70 ms | **0,483** | corps 270 / 527 / 785 Hz + bruit large | bruit + corps accordé |
| 2 | METAL | 0,16 | 110 ms | 0,033 | amas inharmonique 3,36-5,26 kHz (rapports 1 ; 1,01 ; 1,09 ; 1,11 ; 1,47 ; 1,56) | métallique |
| 3 | PERC | 0,18 | 240 ms | 0,000 | 293 Hz, partiels à 1,24 et 1,52 × (et 140 Hz à −18 dB) | percussion accordée, non harmonique |
| 4 | TONE | 0,12 | 640 ms | 0,000 | 258 Hz et harmoniques impairs (3, 5, 7, 9, 11) | onde tonale |
| 5 | CHORD | 0,13 | > 667 ms | 0,000 | 258, 316, 387 Hz à 0,6 dB les uns des autres (1 ; 1,23 ; 1,5) | accord (tierce, quinte) |

Seule l'ALG 1 est un bruit (platitude 0,48 contre ≤ 0,03 ailleurs) ; seule l'ALG 2 met son énergie dans un amas
inharmonique aigu ; l'ALG 3 est accordée mais inharmonique. L'ordre du manuel est donc aussi celui de l'oreille, sans
recours à l'indice deviné.

## 4. Les vecteurs

Dans `vectors-cycles\` (hors dépôt), par machine trois passes de six pistes, chacune avec un changement sur le son par
défaut : défaut, PTCH ± 12, DEC 0x0800 / 0x6000, PUNCH ; COLR, SHPE, SWEP à 0 et au maximum (SHPE de CHORD : 0x2500,
sa borne) ; CONT à 0 et au maximum, vélocités 100, 64, 32, gate d'un bloc. Trig au bloc 1, gate tenu des blocs 1 à 64.
Par cas : `<machine>_<cas>.voice.i32` (sortie du répartiteur pour la piste, 32 int32 par bloc, petit-boutiste),
`.track.i32` (après gain et VOL+DIST), `.voice.wav` ; par machine un JSON : les mots du son, les masques, le rendu
exécuté, les 33 mots lissés que la voix a lus au bloc 3, les SHA-256, les comptes d'instructions. Régénération : 
`run_all.ps1` (six processus, 13 à 17 min chacun, garde de 40 min), puis `analyse_cycles.py`.

Hypothèses de ces vecteurs, à lever avec un état booté :

- note 60 [INFER : la note d'un trig du pad ou du séquenceur sans transposition ; TONE au do 4 le corrobore] ;
- courbe de vélocité 127 (vélocité pleine sur le niveau) et modulation de vélocité neutre (destination 0, quantité
  0x4000) [INFER : les défauts de +0x58/+0x60 d'un son ne sont pas dans les fiches] ;
- LFO aux défauts de ses fiches (profondeur au centre, donc sans effet [INFER]) ;
- sens du second masque du répartiteur (voix + 0x3c) : supposé tenue de note ; sans effet avec GATE éteint.

## 5. Le coût

Mesuré au banc par un crochet de code Unicorn : chaque instruction exécutée compte une fois, une instruction EMAC
(piégée) aussi. « Voix » = de l'empilement des arguments de l'update (`0x400a7e08`) au retour du rendu (`0x400a7e20`),
routines partagées comprises. Six pistes sur le son par défaut, trig au bloc 1 ; blocs 2-11 et 212-221 (141 ms après
le trig).

| machine | voix, blocs 2-11 (moyenne / max) | blocs 212-221 | par échantillon | ticket 11 (sans état) |
|---|---|---|---|---|
| KICK | 7 444 / 7 572 | 7 394 | 233 | 7 717 |
| SNARE | 7 166 / 7 254 | 7 155 | 224 | 7 440 |
| METAL | 6 439 / 6 498 | 6 428 | 201 | 6 715 |
| PERC | 6 087 / 6 147 | 6 076 | 190 | 6 363 |
| TONE | 5 305 / 5 421 | 5 288 | 166 | 5 581 |
| CHORD | 8 303 / 8 378 | 8 310 | 259 | 8 548 |

Hors voix, par bloc pour les six pistes : répartiteur lui-même 1 628 (3 842 au bloc du changement de machine, six
`0x400a7ab8`) ; lissage + modulation + LFO 2 873 ; gain + copie + VOL+DIST 6 697 (≈ 1 100 par piste). Une piste jamais
déclenchée reste sur KICK (le changement de machine attend un trig) et coûte 7 393 : le même prix qu'un KICK qui joue.
METAL et SNARE, éteints à −40 dB avant le bloc 212, coûtent encore ce qu'ils coûtaient à l'attaque : rien ne s'arrête
quand le son meurt. Les comptes sont 3 à 5 % sous ceux du sondage du ticket 11 (branches d'enveloppe différentes avec
de vrais réglages). L'estimation en µs du ticket 11 tient : ≈ 2,1-3,3 µs par voix et par bloc pour un port exact [INFER,
même rapport que le Syntakt].

## Ce qui reste

- **Un boot réel** (digiemu, factory kit, point d'arrêt sur `0x4005979e`) pour lever les hypothèses du §4 et avoir des
  sons d'usine : il faut l'Unicorn patché de digiemu, à construire depuis les sources d'Unicorn 2.1.4 (téléchargement à
  autoriser) avec Visual Studio. Non tenté.
- Le mix, le délai, la réverbération et l'étage de sortie (`0x400567ba`, `0x40057488`, `0x400579c4`, `0x400565c6`)
  n'ont pas de vecteurs ; leur init `0x400582c4` n'est pas appelée.
- Le second masque du répartiteur et le LFO modulant ne sont pas couverts par les cas (GATE éteint, profondeur nulle).

## Sources

- Section 3 de l'OS 1.13, lue et exécutée sur place ; aucune donnée du firmware recopiée ici.
- SyntHack OS `C:\dev\elektron\Synthack-OS\tools\emu\emac.py` (MIT) ; `elektron-work\tools\oracle3.py` et `cfdis.py`.
- digiemu (`MODELS.md`, `docs/UNICORN.md`, `devices/model-cycles.toml`), lu pour l'état du boot, non exécuté.
- Recherche 11 de la planification v1, `elektron-drums/research/11-model-cycles.md`.
