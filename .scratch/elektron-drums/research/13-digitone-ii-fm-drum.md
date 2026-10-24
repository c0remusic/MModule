# 13. Faisabilité du FM DRUM du Digitone II

Recherche du 2026-10-24, environ 1 h 30, lecture seule. Sources primaires lues :

- **digikit** (ElekLabs/digikit, AGPL-3.0+ avec termes de section 7), clone superficiel du 2026-10-24 dans
  `C:\dev\elektron-work\scratch-arch\ext\digikit` : `docs/findings/02`, `04`, `06`, `18`, `53`.
- **dn2-cfw** (naturarum/dn2-cfw, GPL-2.0+), clone dans `C:\dev\elektron-work\scratch-arch\ext\dn2-cfw` : `README.md`,
  `FIRMWARE.md`, `tools/sharcvm/`, `notes/prior-art-2026-10.md` (section 6 : le relevé de dn2_firmware_explore, AGPL).
- **elektron-emu** (`C:\dev\elektron-emu\Digitone-II`, sans licence : faits seulement) : `README.md`,
  `source/core/dsp/dn_dsp.h`, `dn_gensrc.inc`, `dn_rtmain.c`, `cli/dsptool.c`.
- **Manuel** du Digitone II OS 1.12 (elektron.se, `Digitone-2-User-Manual_ENG_OS1.12_261008.pdf`, téléchargé et
  converti dans `C:\dev\elektron-work\scratch-arch\ext\manual\`) : A.2.2 FM DRUM (p. 93-96), annexe C (CC et NRPN).

Marques : **[D]** dit par la source citée (dont les marques [D]/[V]/[O] de digikit, qui ne sont pas toutes revérifiées
par un second agent chez eux) ; **[INFER]** ma déduction ; rien n'a été exécuté ici.

## Réponse courte

Faisable, sur le modèle de MoDule et de `mdX20PortTest` : le FM DRUM est **une routine SHARC+ de la DSP**
(`0x1c778b`), appelée une fois par voix et par bloc de 32 échantillons, qui écrit un tampon mono de 32 flottants à
48 kHz avant le gain, l'enveloppe d'ampli, le filtre et la chaîne de piste du Digitone II. C'est exactement la coupe
que veut l'instrument (la voix seule, avant la chaîne). Le portage en C++ se vérifie contre un oracle qui existe sous
licence compatible : le **sharcvm** de dn2-cfw (GPL-2.0+, interpréteur C capable d'appeler une routine seule) pour la
routine, l'**émulateur couplé de digikit** (AGPL, ColdFire + SHARC+) pour la référence complète du trig au tampon de
voix. Ce qui manque : la disposition de l'enregistrement de voix du FM DRUM, ses conversions de paramètres et ses
tables (transitoires, bruit), et une mesure de son coût par voix. Effort [INFER] : 3 à 5 semaines d'agent, confiance
moyenne-basse ; coût CPU [INFER] : 2 à 5 % d'un cœur pour 16 voix, à mesurer.

## 1. Processeurs et firmware

- **Puces** : ColdFire MCF54415 (OS, séquenceur, interface) et SHARC+ ADSP-21569 (toute la synthèse et le mix)
  [D, fils PCB d'Elektronauts cités dans `.scratch/elektron-engines/resources.md`]. Horloge du cœur SHARC 1 GHz (CGU
  20 MHz x 100 / 2), 666 667 cycles par trame, 1 500 trames par seconde [D, dn2-cfw `notes/prior-art-2026-10.md`
  l. 590-596, d'après dn2_firmware_explore].
- **Lien** : par bloc de 32 échantillons à 48 kHz, le ColdFire envoie une trame SPI de 2 688 octets et en reçoit 2 748
  (`FUN_40025e36` → `FUN_400cf7be(0xa80, 0x80005e60, 0xabc, 0x800053a4)`) [D, digikit `04-coldfire-dsp-link.md`
  l. 324-327]. Côté DSP, mots de la trame : `2+2t` note (note << 8), `34`/`36` masques note-on/off, `116+2t` niveau,
  `148+2t` machine, `180+2t` filtre, emplacements par voix à `218+146t` [D, prior-art l. 576-578]. Les « pistes » de la
  trame sont des **voix** : le ColdFire alloue les 16 emplacements en tourniquet [D, digikit 06 l. 5652-5659].
- **Firmware** : section 7 du `.syx` = flux de boot ADI du SHARC, compressé aPLib, 836 956 octets [D, prior-art
  l. 541-547]. **Le blob DSP est identique entre 1.11 et 1.12** [D, digikit `53-stock-dsp-shape.md` l. 7-8]. Confirmé
  ici : la DLL native qu'elektron-emu a compilée sur ce PC à partir de l'OS 1.11 s'appelle
  `%APPDATA%\Elektron Emu\Digitone II\Native\dsp-336e340aa0cd-n1.dll`, et `336e340a...` est le SHA-256 de la section 7
  de **1.12** que dn2_firmware_explore exige [D, prior-art l. 505-507]. Donc tout ce qui suit vaut pour 1.11 (installé
  sur ce PC : `%APPDATA%\Elektron Emu\Digitone II\Firmware\Digitone_II_OS1.11.syx`) et 1.12 (servi par Elektron).
- **Charge mesurée sur la machine** : 63-65 % plate que des notes jouent ou non ; les moteurs d'origine font presque
  tout leur travail sans note [D, prior-art l. 590-596] ; dn2-cfw mesure 50-60 % (CP2D, `README.md`). L'émulateur de
  digikit compte 335 000 instructions occupées sur 666 667 par bloc, une voix ou seize : « 16 voices do not show on
  DN2 » [D, digikit 06 l. 5408-5417].

## 2. Le FM DRUM dans le code

**Côté ColdFire** [D, digikit 02 l. 519-528, 1283, 1326-1328, 1622] :
- Cinq types de machine : 0 FM TONE, 1 WAVETONE, **2 FM DRUM**, 3 SWARMER, 4 MIDI. Descripteurs de 0x2c octets à
  `0x42432b24`.
- Table des paramètres T1 (`0x401f83a4`, 321 lignes de 60 octets en 1.12). Le FM DRUM a 4 pages et utilise les slots
  **25 à 54** (30 paramètres) ; son filtre par défaut est LOWPASS 4.
- Le type de la piste part dans la trame au mot `0x94 + 2t` (`0x20000` pour FM DRUM après un TRIG).
- Le ColdFire lisse les valeurs avant la trame (un pôle, `FUN_400d92a2`). La DSP convertit les mots s16 en flottants
  une fois par bloc (échelle 1/32512) et les rampe par échantillon de (nouveau - ancien) / 32 dans le bloc
  [D, digikit 53 l. 51-53].

**Côté SHARC+** [D, digikit 06 l. 5650-5680 et 53 l. 37-50 ; prior-art l. 565-575] :
- `FUN_1c2712` (copie de la trame, appelée par `0x1c9e76`) appelle à `0x1c3044` le moteur `FUN_1c8ef1` (1 352
  instructions, R4 = `0x241298`). C'est un pipeline d'étages, chacun une boucle sur les 16 emplacements de voix.
- Aiguillage par type : `R2 = DM(I0 - 12)` (`0x1c9056`), `compu(R2, 5)` (`0x1c905d`), table d'étages de préparation
  `0x8052db90`, entrée FM DRUM `0x1c920b`.
- **Rendu du FM DRUM : `0x1c778b`, appelé à `0x1c95f9`**, un appel par voix qui rend un bloc entier ; état par voix
  à `+0x54c8` du bloc moteur, pas **0x2e8** octets. Structures de paramètres par emplacement : `0x234` octets depuis
  `0x25569c` (`0x2554b8` dans le relevé de dn2_firmware_explore), avec `+0x1b4` le type, `+0x1dc` le filtre, `+0x22c`
  le niveau, `+0x20c..+0x228` l'ADSR.
- Ordre mesuré sur un tampon de voix (`SHARC_WWATCH`, un bloc) : effacement (`0xb804e5`), synthèse FM (types 0-3),
  gain x1,585 (`0x1c949b`), enveloppe d'ampli (`FUN_b80c39`), filtre, chaîne de piste (overdrive, réduction de bits),
  puis `0x1c9aea` suréchantillonne chaque tampon vers un bloc de 64 flottants à **96 kHz** (`0x257ee8 + 0x100 v`) que
  lisent les envois et le master.
- **Le point de coupe** : le tampon de voix `0x804acf90 + 0x80 v`, 32 flottants à 48 kHz. Le crochet de digikit
  `FUN_b8251b` (55 instructions, feuille) passe juste après la synthèse : ce qu'il voit est le signal **avant** le gain,
  l'enveloppe d'ampli et la porte, le filtre et la chaîne. Un FM TONE patché sans type enregistré reste
  bit-identique au stock (62 176 échantillons actifs) [D].
- La fin de note appartient au moteur (l'enveloppe d'ampli libère la voix) ; la trame ne porte pas de front de
  note-on propre (les mots de hauteur et de vélocité sont écrits au trig et jamais effacés) [D, digikit 06 l. 5682-5688].
- Primitives partagées entre Digitakt II et Digitone II, réutilisables : biquad de bloc `b81368`/`b8135a`, saturateur
  `1cdb56`, générateur d'enveloppe `b80345` (enregistrement de 11 mots, table de pentes `0x26ad30`), les cinq filtres,
  `powf` `1c0c76`, racine inverse `1c1226`, division `1c06ba` [D, digikit 53 l. 66-79].
- Taille de la routine `0x1c778b` [INFER] : le moteur suivant (SWARMER) commence à `0x1c85ad`, soit 0xE22 = 3 618
  mots courts plus loin. Une instruction SHARC+ prend 1 à 3 mots courts, donc 1 200 à 3 600 instructions si l'intervalle
  ne contient que cette routine ; vraisemblablement environ 1 500, appels non compris. Non mesuré.
- Suréchantillonnage : le FM TONE fait tourner ses opérateurs sur 64 échantillons et décime à 32 (`0xb82728`). Pour le
  FM DRUM, **inconnu** : la table de digikit ne cite le décimateur que pour le FM TONE [D, 53 l. 49].

## 3. Les paramètres (manuel 1.12, A.2.2 et annexe C)

Architecture de la voix, d'après le schéma du manuel : trois opérateurs FM (A, B, C) avec enveloppes de modulation →
wavefolder → niveau du corps (enveloppe BODY) ; bruit et transitoire (niveaux, enveloppe de bruit, filtre B/W) ;
« operator B in algorithm 5-7 » ; le tout vers le filtre.

| Page | Knobs A-H | CC |
|---|---|---|
| SYN1 | TUNE, STIM (durée du balayage de hauteur), SDEP (profondeur), ALGO, OP.C (onde de C), OP.AB (onde de A et B), FDBK, FOLD (wavefolding du corps seul) | 40-47 |
| SYN2 | RATIO A, DEC A, END A, MOD A, RATIO B, DEC B, END B, MOD B | 48-55 |
| SYN3 | HOLD (corps), DEC (corps, dernière valeur infinie), PH.C (remise en phase de C : 0-89 degrés, 90 au sommet, 91 OFF), LEV (corps), NRST (graine de bruit remise à chaque note), NRM (C module le bruit en anneau) | 56-63 |
| SYN4 | NHLD, NDEC (bruit, dernière valeur infinie), TRAN (choix du transitoire), TLEV, BASE, WDTH (filtre bruit/transitoire), GRAN (densité des grains, du blanc au granuleux), NLEV | 70-77 |

NRPN 1:73 à 1:104 pour les mêmes 32 knobs (la haute résolution ne passe que par NRPN : annexe C). 8 + 8 + 6 + 8 = 30
paramètres, ce qui recoupe les slots 25-54 de digikit [D + INFER pour la correspondance knob à slot].

## 4. Harnais possible

| Brique | Licence | Ce qu'elle donne | Pour nous |
|---|---|---|---|
| sharcvm de dn2-cfw (`tools/sharcvm/`) | GPL-2.0+ (compatible AGPL) | interpréteur SHARC+ en C : registres 40 bits, MR 80 bits, DAG, piles de boucles, branchements différés, contrôlé sur les 2 039 tests C de selache ; `vm.call(entry, args)` appelle une routine seule ; rend déjà les effets d'origine isolés (`render_fx.py`, `fullblock.py`) | **l'oracle de la routine** : appeler `0x1c778b` (et la préparation `0x1c920b`) sur un enregistrement de voix construit, comme `mdX20PortTest` appelle les routines de X.20 dans un 68k. Demande les images mémoire après boot (`load_dn2`, `out/T3.2/mem`), à refaire avec leurs outils depuis le `.syx` 1.12 ; le décodeur passe par Python (digikit) |
| émulateur couplé de digikit (`packages/emulator`, Rust) | AGPL-3.0+, section 7 : NOTICE, mention « digikit by Em D - https://github.com/ElekLabs/digikit » dans l'À propos | ColdFire + SHARC+ du DN2 1.11/1.12, scénarios à PCM épinglés (`trig1-taps`, `kb-chord16`), `SHARC_WWATCH` sur les tampons de voix, `--regions` (temps par fonction), 1,58 s de CPU DSP par seconde audio en natif | **la référence de bout en bout** (molette → lissage ColdFire → trame → tampon de voix), et la mesure du coût de `0x1c778b`. Construction sous Windows non vérifiée (mise, uv, Rust) |
| elektron-emu | aucune | traduction SHARC+ → C compilée sur la machine, temps réel pour tout le DSP | faits seulement ; la preuve qu'un code flottant SHARC compilé en C sur x86 tourne en temps réel |

Exactitude du flottant [INFER] : le SHARC+ calcule en flottant IEEE 32 bits sans FMA dans ce code (multiplication et
addition séparées), dénormaux mis à zéro ; la division `1c06ba` bascule le bit 16 de MODE1 [D, digikit 53], qui est
RND32, le choix entre arrondi 32 et 40 bits [INFER, d'après la PRM]. Un port C++ en `float` sans contraction FMA,
FTZ/DAZ actifs et dans le même ordre d'opérations peut donc être bit-exact, sauf là où le code calcule en 40 bits ou
utilise les germes RECIPS/RSQRTS, à reproduire table comprise. Indice favorable [D, digikit 18, « State at freeze »] :
leurs cinq configurations (AOT natif, noyaux Cranelift, part interprétée de 0,4 à 5,6 %) donnent le même PCM.

## 5. Effort

[INFER], en temps d'agent, sur le modèle du port du Syntakt et de X.20 :

1. Oracle : images mémoire 1.12, appel de `0x1c920b` puis `0x1c778b` dans sharcvm, tampon de voix sorti par bloc ;
   recoupement avec digikit sur un trig (même tampon à `0x804acf90`) : 3-5 jours.
2. Carte : enregistrement de voix FM DRUM (`+0x54c8`, 0x2e8 octets ; la structure de 0x234), les 30 paramètres
   (s16 x 1/32512 puis courbes ou tables), le front de note-on, le générateur de bruit et sa graine (NRST), les données
   des transitoires (TRAN : où elles vivent est **inconnu**), le suréchantillonnage éventuel : 3-5 jours.
3. Port C++ de la routine et de ses appelés (enveloppes `b80345`, `powf`, `rsqrt`, division, wavefolder, filtre du
   bruit), comparaison échantillon par échantillon : 1-2 semaines.
4. Côté ColdFire : le lisseur `FUN_400d92a2` et l'échelle des valeurs (à porter depuis le ColdFire, un oracle ColdFire
   existe déjà pour le Syntakt) ; l'allocation de voix n'est pas utile (une voix par piste) ni les LFO du Digitone :
   1-3 jours.

Total : 3 à 5 semaines. Risques : le décodage d'une routine compilée de quelques milliers d'instructions sans symboles ;
les tables lues dans la DDR ; le passage en 40 bits ; la sortie d'une voix dépendant d'état partagé entre emplacements
(« stock engines do nearly all their work with no note playing » : à vérifier pour le FM DRUM, sinon une voix seule
n'est pas l'unité).

## 6. Coût CPU

- Mesuré, sur la machine : le DSP entier tourne à plat (50-65 % d'un SHARC+ à 1 GHz), quel que soit le nombre de voix ;
  aucune source ne donne le coût du seul FM DRUM.
- [INFER] Trois opérateurs, un wavefolder, du bruit filtré et deux enveloppes, en flottant scalaire C++ à 48 kHz :
  30 à 60 ns par échantillon et par voix, soit **2,3 à 4,6 % d'un cœur pour 16 voix** (x2 si les opérateurs tournent à
  96 kHz comme ceux du FM TONE). Le port ne rend que les voix actives, contrairement au stock.
- La voix sort à 48 kHz : la même famille de fréquence que le Syntakt, pas de rééchantillonnage de plus.
- Mesure à faire : `--regions` de digikit sur un scénario FM DRUM (temps par fonction de `0x1c778b`), ou le compte
  d'instructions par appel dans sharcvm (`hits`, mode profil).

## Inconnues

- Le coût réel de `0x1c778b` par voix et par bloc, et s'il suréchantillonne.
- La disposition de son état (0x2e8 octets) et la conversion de chacun des 30 paramètres.
- Où sont les transitoires (TRAN) et le générateur de bruit ; si une voix dépend d'un état partagé.
- Si une routine C++ en `float` reste bit-exacte partout (mode 40 bits, germes de réciproque).
- Si l'émulateur de digikit se construit sous Windows.
