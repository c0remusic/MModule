# 08 — La couche sample du Rytm dans son firmware

Recherche du 2026-11-15, ticket [08](../issues/08-couche-sample-rytm.md). Lecture seule : manuels d'Elektron, notes du
dépôt, scripts et sorties déjà présents dans `C:\dev\elektron\elektron-work` (aucun désassembleur ni firmware lancé,
rien de `elektron-emu`). Inférences marquées [INFER].

## Réponse courte

- **Ce qui est numérique :** le lecteur de sample seul (16 bits, 48 kHz, mono ; TUNE, FINE, BR, SMP, STRT, END, LOOP,
  LEV), et le bruit des machines. Tout le reste de la voix est analogique : générateur de percussion, overdrive, filtre
  multimode 2 pôles, VCA stéréo (panoramique). Le manuel le dit (« Fully analog signal path », schéma « Analog elements
  are gray; digital are white »).
- **Où le sample entre :** au début de la voix, à côté du générateur de percussion, **avant l'overdrive et avant le
  filtre**. Le manuel place OVR « before the multimode filter » ; le sample passe donc par OVR, le filtre, l'enveloppe
  d'ampli et le VCA, comme la machine. C'est l'ordre que la spec v1 de Lockstep donne déjà (« machine + couche sample →
  drives pré → … FILTER … »).
- **Où il est rendu :** [INFER] sur le ColdFire principal (section 3 de l'OS 1.74), dans le bloc de 32 échantillons à
  48 kHz qui sert déjà les machines, écrit dans l'anneau TDM vers les convertisseurs (16 voies × 32 trames par moitié à
  0x80000000), puis par le FPGA vers un CNA par voix. Rien ne le prouve encore : la routine du lecteur n'a pas été
  trouvée. Le FPGA est l'autre hypothèse.
- **Portable :** la partie numérique, par vecteurs comme les machines du ticket 28 : correspondance des paramètres
  (pas de hauteur, START/END, boucle, sens inverse, loi de BR, courbe de LEV, vélocité et LFO), et l'interpolation si
  elle se fait sur le CPU.
- **Seulement approchable :** tout ce qui suit le CNA (overdrive, filtre, VCA, la somme analogique sample + machine,
  leur rapport de niveau), comme les voix analogiques déjà faites. Lockstep a déjà ses drives pré et son filtre : la
  couche Rytm s'y insère sans modèle à elle.

## 1. Ce que dit Elektron (manuel)

Le manuel MKII OS 1.74 (URL ci-dessous) dépasse la taille que l'outil de lecture accepte (> 10 Mo) ; celui de l'OS 1.70
aussi. Les citations viennent du manuel **Analog Rytm (MKI) OS 1.71**, même ligne d'OS et même moteur de sample, et du
**Quick Guide MKII**, que l'outil a pu lire. Les noms de paramètres du MKII (STRT, LOOP, LEV) diffèrent de ceux du MKI
(STA, LOP, LEV) ; les notes de version 1.74, rédigées pour MKI et MKII, parlent de STA et END.

### Architecture (manuel MKI 1.71, chap. 4, p. 13 ; Quick Guide MKII §3.3)

- Huit voix ; chaque voix a « an identical sample playback engine, overdrive circuit, and multimode filter » ; seuls
  les générateurs de percussion diffèrent d'une voix à l'autre.
- Schéma de la voix : PERCUSSION SOUND GENERATOR et SAMPLE PLAYBACK ENGINE, puis OVERDRIVE, MULTIMODE FILTER (son
  enveloppe), AMP (son enveloppe), PAN, envois DELAY et REVERB, sortie individuelle et mixeur. Le texte extrait du PDF
  ne garde pas les couleurs : la répartition analogique/numérique vient de la liste des caractéristiques (§1.3).
- Les douze pistes « can layer analog percussion sounds and sampled sounds, to distort and filter them » (Quick Guide
  MKII §3.3.1, manuel MKI §5.3.1).
- OVR « sets the overdrive (gain increase boost of the amp), located before the multimode filter in the signal path »
  (manuel MKI, annexe A.4, p. 74).
- Machine NONE (« SYNTH DISABLED ») : la piste ne joue que son sample (annexe D.1, p. 90), toujours par OVR, le filtre
  et l'ampli.
- Huit voix pour douze pistes : BD, SD, BT et LT ont chacune leur voix ; RS-CP, MT-HT, CH-OH et CY-CB partagent la leur,
  et la piste de droite a la priorité (« Track CP will mute track RS… », §8.1, p. 21). Le lecteur de sample étant un
  par voix, deux pistes couplées partagent aussi leur lecteur [INFER].

### Page SAMPLE (manuel MKI 1.71, annexe A.2, p. 72-73 ; notes de version 1.74)

| Paramètre | Ce qu'il fait |
|---|---|
| TUNE | hauteur en demi-tons, -24 à +24 (quatre octaves) |
| FINE | décalage fin, -64 à +63, un demi-ton vers le bas à un demi-ton vers le haut |
| BR | réduction de résolution (« makes it more grainy ») |
| SMP | emplacement parmi les 127 du projet ; OFF ne joue rien |
| STRT (STA) | point de départ, 0-120 sur toute la longueur ; haute résolution depuis l'OS 1.70, réglage SAMPLE POS RES depuis 1.72 |
| END | point de fin, 0-120 ; END < STRT joue le sample à l'envers |
| LOOP (LOP) | OFF : une lecture par trig ; ON : boucle pendant la durée du trig (LEN), bornée par HLD et DEC de l'AMP |
| LEV | niveau du sample (par défaut 100 pour un son neuf, depuis une version antérieure à 1.70) |

Le LFO peut viser tout paramètre des pages SYNTH, SAMPLE, FILTER et AMP (annexe A.5) ; la page SMPL 2 montre la forme
d'onde depuis l'OS 1.70 (MKII).

### Formats, mémoire, échantillonnage

- Transfer convertit tout fichier en « 16 bit, 48 kHz, mono audio files, the Analog Rytm's native audio format »
  (manuel MKI §13.2.3, p. 55). Le sample est donc mono, et le panoramique vient du VCA analogique.
- 127 emplacements par projet, chargés dans la RAM du projet ; banque d'usine et samples utilisateur sur le +Drive
  (1 Go, page produit MKII). La taille de la RAM des samples n'est donnée par aucune des sources lues.
- Le MKII échantillonne depuis AUDIO IN L/R ou en interne (Quick Guide MKII chap. 7), depuis l'OS 1.70 aussi par l'audio
  USB, avec une longueur en pas du séquenceur (notes 1.70).
- Convertisseurs MKII : « 48 kHz, 24-bit D/A and A/D converters » (page produit). Caractéristiques de la voix (manuel
  MKI, chap. 17) : « Fully analog signal path », « 1 x 16-bit sample playback engine », « 1 x sample bitrate
  reduction », overdrive, filtre 2 pôles et VCA analogiques.

### Filtre et ampli (ce que la couche traverse)

Filtre (A.3) : ATK, DEC, SUS, REL, FRQ, RES, ENV, et sept types (LP2, LP1, BP, HP1, HP2, BS, PEAK), « technically all of
them are resonant 2-pole filters ». Ampli (A.4) : ATK, HLD (AUTO ou 1-127), DEC, OVR, DEL, REV, PAN, VOL.

## 2. Ce que les travaux déjà faits en savent

- **Tickets 28 et 29 (`elektron-drums-v1`)** : seulement le contrôle des machines analogiques. Le ticket 28 a porté les
  trois machines propres au Rytm (CP CLASSIC, BT CLASSIC, XT CLASSIC) contre 2 753 vecteurs ; le ticket 29 (prêt, pas
  commencé) compare les trente autres à celles du Syntakt. Ni l'un ni l'autre ne touche la couche sample.
- **`tools/analog/rytmrig.py`, `record_rytm.py`** : le banc (oracle3, section 3 chargée à 0x40000400, table des
  machines, voix → piste `[0, 4, 1, 5, 8, 6, 10, 2]`, 0x54 octets de paramètres par piste). Ce qu'ils apprennent de
  l'aval :
  - le contrôle tourne par bloc de 32 échantillons à 48 kHz, comme sur le Syntakt (`edDrumsRytm.h`) ;
  - les routines du gros tom et des toms écrivent un signal au rythme audio dans un « stream » vers la voix :
    0x80000000 + (index de moitié 0x42F92124 << 11) + 0x20/0x24/0x28 + 0x40 × i, i = 0-31 (`STREAM`, et
    `rytm_specials.txt`, 0x4011B7B4-0x4011B7F2 et 0x4011B882-0x4011B8D6). Soit un anneau TDM de 16 voies de 32 bits sur
    32 trames par moitié (2 Ko), le même schéma que l'anneau CNA du Syntakt (`elektron-engines/cpu1.md` : anneau CNA à
    0x80000000, anneau CAN à 0x80001800, moitié choisie par bloc). Les voies 8, 9 et 10 portent l'enveloppe de clic des voix 1, 3 et 5 ;
  - le bruit des machines est numérique : deux générateurs de la section 3 (0x4011BCBA, 0x4011BDDC) l'écrivent pour la
    voix, qui le reçoit (`record_rytm.py`, `NOISE_A`, `NOISE_B`) ;
  - chaque voix a une calibration (deux paires échelle/décalage, 0x42FB6B50 et suivantes) : l'unité corrige ses CV.
- **`scratch-arch/research/rytm/`** : les sections déballées de l'OS 1.74 (1 : 150 Ko, 2 : chargeur de 42 Ko, 3 : code
  principal de 3 Mo, 5 : 15 octets, 8 : 160 Ko) et les désassemblages des machines spéciales. Les sections 1 et 8 sont
  [INFER] des bitstreams FPGA, comme sur le Syntakt (`elektron-engines/machines.md`). Aucune sortie ne couvre le lecteur
  de sample.
- **`elektron-engines/README.md`** : Rytm MKII = ColdFire + FPGA + voix analogiques, pas de second CPU de voix comme sur
  le Syntakt, pas de DSP.
- **`vectors-rytm`** : `rytm.rav` seul (les vecteurs du ticket 28). **`rytm-samples`** : vide.

## 3. Où le sample est rendu

Ce qui est établi : le ColdFire principal calcule par bloc de 32 à 48 kHz des signaux au rythme audio pour les voix
(clic, bruit) et les écrit dans un anneau TDM de 16 voies vers les CNA. Il n'y a pas d'autre processeur numérique que le
FPGA.

[INFER] Le lecteur de sample tourne au même endroit : une routine de la section 3, appelée par voix dans le bloc audio,
lit la RAM du projet (16 bits), avance d'un pas fixé par TUNE + FINE + la hauteur de la note, applique BR et LEV, et
écrit 32 échantillons dans une voie de l'anneau. Avec 16 voies et 8 voix, une voie de sample par voix plus les voies des
signaux de machine est plausible, mais les voies 0-7 ne sont attribuées par rien de ce qui a été lu. La somme avec le
générateur se fait alors en analogique, à l'entrée de l'overdrive.

L'autre hypothèse : le FPGA lit les samples lui-même (la RAM des samples serait la sienne), le CPU ne lui donnant que le
pas, les bornes et le niveau. Dans ce cas l'interpolation et BR seraient dans le bitstream et ne se porteraient pas.

Pour trancher (une étape à part, banc du ticket 28) : dans la section 3, chercher qui lit SMP, STRT, END et LOOP dans
les paramètres de piste (0x8000F7BA + 0x54 × piste) et ce qui écrit dans l'anneau à 0x80000000 hors des machines. Une
boucle de 32 itérations qui lit une mémoire de 16 bits et y écrit = rendu CPU ; seulement des mots de commande vers les
périphériques du FPGA = rendu FPGA.

## 4. Porter ou approcher

| Partie | Où | Verdict |
|---|---|---|
| Paramètres → pas, bornes, sens, boucle, niveau | section 3 (sûr : le CPU traduit tous les paramètres) | **portable** par vecteurs, comme le contrôle du ticket 28 ; tables par formule |
| Interpolation et réduction BR | section 3 [INFER] ou FPGA | **portable si CPU**, sinon approchée (BR = quantification à mesurer à l'oreille) |
| Lecture 16 bits / 48 kHz, mono | format | **direct** : le Locker de Lockstep fournit déjà les samples ; rééchantillonnage vers 44,1 kHz comme le Syntakt (48 kHz interne) |
| CNA 24 bits | matériel | négligeable |
| Somme sample + générateur, rapport de niveau | analogique | **approché** : aucune machine pour mesurer ; un gain maison réglé à l'oreille |
| Overdrive, filtre 2 pôles, VCA | analogique, aucun circuit dans le firmware | **approché** : déjà les drives pré, le FILTER et l'AMP de Lockstep |

Conséquences pour le ticket 09 : la couche du Rytm n'apporte en propre que son lecteur numérique ; sa place dans la
chaîne (après la machine, avant les drives pré) est celle que la spec v1 réserve déjà au layering. Le coût d'un lecteur
16 bits par piste est faible [INFER : de l'ordre d'un lecteur ROM du Machinedrum, sous 0,1 % d'un cœur par voix].

## Sources (consultées le 2026-11-15)

- Elektron, *Analog Rytm User Manual*, OS 1.71 (MKI), 2024-10-16 :
  https://elektron.se/wp-content/uploads/2024/10/analog_rytm_user_manual_eng_os1.71_241016.pdf (p. 13, 15, 21, 55,
  72-74, 90, chap. 17).
- Elektron, *Analog Rytm MKII Quick Guide* (ENG, 2023-11-22) :
  https://elektron.se/wp-content/uploads/2024/09/AnalogRytmMKIIQuickGuide_ENG_231122.pdf (§3, chap. 7, chap. 8).
- Elektron, page produit Analog Rytm MKII : https://www.elektron.se/en/analog-rytm-mkii (convertisseurs, +Drive,
  sorties).
- Elektron, *Analog Rytm OS 1.74 Release Notes* (copie locale `Downloads\Analog-Rytm_MKII_OS1.74_readme.html`).
- Non lus (trop lourds pour l'outil) : manuel MKII OS 1.74,
  https://www.elektron.se/wp-content/uploads/2026/10/Analog-Rytm-MKII-User-Manual_ENG_OS1.74_260930.pdf, et OS 1.70,
  https://elektron.se/wp-content/uploads/2024/09/AnalogRytmMKIIUserManual_ENG_OS1.70_231122.pdf.
- Dépôt : `.scratch/elektron-drums-v1/issues/28-*.md`, `29-*.md` ; `.scratch/elektron-engines/README.md`,
  `machines.md`, `cpu1.md`, `analog-literature.md` ; `source/elektron/edDrums/edDrumsRytm.h`.
- Hors dépôt : `C:\dev\elektron\elektron-work\tools\analog\rytmrig.py`, `record_rytm.py` ;
  `scratch-arch\research\rytm\rytm_specials.txt` et ses scripts ; `vectors-rytm`, `rytm-samples` (listés).
