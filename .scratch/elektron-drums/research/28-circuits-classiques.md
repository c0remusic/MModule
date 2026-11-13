# 28 — Circuits classiques derrière les 30 machines analogiques

Recherche du 2026-11-12, lecture seule. Point de départ : `.scratch/elektron-engines/analog-literature.md` (méthode
d'ajustement, littérature VA) et `research/14-analogiques-sans-machine.md` (routines du CPU #1, fréquences métalliques
lues dans le firmware). Ce document ne les répète pas : il cherche, machine par machine, quel circuit classique chaque
voix évoque, ce qui fait le son de ce circuit, et ce qui doit séparer nos modèles entre eux.

Le problème posé : à l'écoute, plusieurs de nos machines se ressemblent, les grosses caisses surtout. Le contrôle
(CV, pentes, bits) est porté exactement ; le timbre vient de nos modèles de circuit. Si deux modèles partagent la même
topologie avec d'autres constantes, ils sonneront parents quel que soit le réglage. Il faut donc qu'une différence de
**structure** (résonateur contre VCO, conformateur, origine du clic, chemin du bruit) sépare les machines, pas
seulement des constantes.

Étiquettes, comme dans les notes 14 et `analog-literature.md` : **[DOC]** document du fabricant ; **[PAPER]** article
revu ou thèse ; **[CODE]** code source ; **[FORUM]** affirmation d'utilisateur, non vérifiée ; **[SEC]** source
secondaire (presse, revendeur, clone DIY) ; **[INFER]** déduction de ma part. Chaque source porte sa date.

## Réponse courte

- **Attribution.** Elektron ne nomme aucun modèle classique pour ses machines analogiques. Les descriptions
  officielles (Rytm OS 1.30 en 2016, Rytm OS 1.70 en 2023, manuel Syntakt OS 1.42 en 2026) décrivent des topologies
  (« single VCO », « ring modulation and FM between two VCOs », « six oscillators ») et des caractères (« silky »,
  « modern, hard-hitting »), pas des modèles. Le seul témoignage qui compare machine par machine vient d'un
  utilisateur (darenager, Elektronauts, 2020-01-18) : aucune machine n'est « spécifiquement 808/909 », BD HARD est la
  plus proche du 909, BD SILKY du 606/808, les charlestons, cymbales et toms sont proches du 606/808, la cloche est
  808, les caisses claires et rims ne ressemblent à aucune vieille machine. Les fréquences lues dans le firmware
  (note 14, recalculées ici) confirment la piste 12 : CLASSIC = 808, **HH BASIC = 606** (cinq des six oscillateurs à
  1,3 % près, nouveau), METALLIC = paires modulées en anneau (topologie KR-55).
- **Ce qui sépare les grosses caisses classiques** est structurel : le 808 est un **filtre résonant pincé**
  (pont en T, 49,5 Hz calculés), sans VCO, dont la fréquence monte de plus d'une octave pendant ~6 ms à l'attaque puis
  « soupire » d'environ un demi-ton ; son clic vient du bord de l'impulsion de déclenchement elle-même. Le 909 est un
  **VCO triangle libre** passé par un conformateur à diode vers le sinus, avec une enveloppe de hauteur plus profonde
  et plus longue, et une attaque faite d'**une impulsion plus un bruit filtré passe-bas**, sur un chemin séparé. Le
  606 est un pont en T à 60 Hz plus un second à 130 Hz pour le « clic ». Une grosse caisse FM a une attaque
  **inharmonique**. Une grosse caisse acoustique a des modes de membrane inharmoniques (deux peaux couplées vers 40 Hz)
  et un clic de batte large bande.
- **Conséquence pour nos modèles** : les sept grosses caisses Syntakt sont toutes des VCO (manuel), mais rien n'empêche
  nos modèles de leur donner des **conformateurs, des origines de transitoire et des chemins de bruit différents**.
  Les quatre différences structurelles prioritaires sont listées en §8.
- **Aucune mesure publiée** de centroïde spectral ni de temps d'attaque pour ces machines n'a été trouvée. Les
  cibles chiffrées du §7 viennent des articles de circuit (808), du code Plaits (constantes de temps) et des
  descriptions de service ; les autres sont à mesurer sur les références d'écoute du §9.

## 1. Ce qu'Elektron dit de chaque machine

### 1.1 Manuel Syntakt OS 1.42 (2026-09-30), Annexe A.3-A.4, pp. 98-111 [DOC]

https://www.elektron.se/wp-content/uploads/2026/10/Syntakt-User-Manual_ENG_OS1.42_260930.pdf (texte extrait par
`pdftotext`). Ce que la liste des paramètres impose comme topologie, au-delà de ce qu'en dit
`analog-literature.md` §0 :

| Machine | Paramètres qui fixent la structure | Lecture structurelle |
|---|---|---|
| BD HARD | STIM seul, « depth of the sweep is fixed » ; SNAP et TICK, deux transitoires distincts ; WAV sinus / sinus asymétrique / triangle ; HOLD | un VCO, balayage de profondeur fixe, **deux** sources de transitoire |
| BD CLASSIC | STIM, SDEP ; TRAN « tick or noise transient » ; WAVE trois formes ; HOLD | un VCO, balayage réglable, transitoire **clic ou bruit** (au choix) |
| BD FM | deux oscillateurs, chacun son balayage (STIM, FM S) et sa décroissance (DEC, FM D) ; FM A | FM à deux opérateurs avec enveloppes séparées |
| BD PLASTIC | « two interacting oscillators », même TUNE ; TYPE : FM linéaire, ou FM + modulation en anneau ; MOD ; TICK | deux VCO accordés ensemble, FM et/ou anneau |
| BD SILKY | DUST « subtle tape-like static » ; CLIK « soft, oscillator-generated click » ; HOLD | un VCO, clic produit **par l'oscillateur**, bruit de fond permanent |
| BD SHARP | WAVE : sinus, sinus asym., triangle, sinetooth, scie, carré ; oscillateur libre ou remis à zéro au déclenchement ; TICK | un VCO riche en harmoniques, phase libre possible |
| BD ACOUSTIC | TUNE en demi-tons ; IMP « digital transient … a mix of PCM and noise » ; WAVE six formes | VCO + transitoire **numérique** (PCM + bruit) |
| SD HARD | « interval between [the oscillators] is fixed » ; STIM, SDEP ; TICK ; NDEC, NLEV | deux VCO à intervalle fixe, balayage, bruit |
| SD CLASSIC | DET (osc. 2 relatif à 1), BAL (équilibre des deux), SNAP, NDEC, NLEV | deux VCO désaccordables avec équilibre, bruit |
| SD FM | FM à deux opérateurs + bruit (NDEC, NLEV) | FM + bruit |
| SD NATURAL | un oscillateur ; bruit avec HPF, LPF et résonance du LPF ; BAL bruit/oscillateur | un VCO, bruit **filtré et résonant** |
| SD ACOUSTIC | BDEC (corps), HOLD, SDEP, IMP (PCM + bruit), NDEC, NLEV | VCO + transitoire numérique + bruit |
| RS HARD | TUNE « of the oscillators » (pluriel, un seul réglage), STIM, SDEP, TICK, NLEV | plusieurs oscillateurs liés, balayage, bruit |
| RS CLASSIC | TUN1, TUN2, BAL, TICK, NLEV | deux oscillateurs indépendants, équilibre |
| SY DUAL VCO | osc. 1 sinus ou sinesaw, osc. 2 cinq formes ; « + », anneau, FM linéaire, FM + anneau ; BEND « fixed time pitch envelope » qui monte aussi un transitoire | deux VCO, quatre interactions |
| SY RAW | osc. 2 peut moduler l'osc. 1 en anneau « using a square wave » même à LEV2 = 0 | deux VCO, anneau par carré |
| SY CHIP | arpège interne rapide (« 80:s home computers »), formes analogiques, bruit SID numérique, PWM numérique, tables d'ondes enchaînées | VCO analogique séquencé + parts numériques |
| CH, OH CLASSIC | TUNE « of the oscillators », DEC, NCOL « timbral color » | banc d'oscillateurs + filtre de couleur |
| CH, OH METALLIC | TUNE « of the interacting oscillators », DEC | oscillateurs en interaction, pas de couleur réglable |
| HH BASIC | « six oscillators », TONE (+ strident, − sourd), TDEC (transitoire), RST (reset ou libre) | six carrés, filtre bascule, transitoire séparé |
| HH LAB | six fréquences libres OSC 1-6, DEC | six carrés accordables |
| CY CLASSIC | TONE « metallic tone », NCOL, DEC | banc + deux réglages de filtre |
| CY METALLIC | « ring-modulated oscillators », TONE « of the metallic transient », TDEC, DEC | anneau + transitoire à part |
| CY RIDE | TYPE A-D : jeux de fréquences **et** « inter-oscillator logical interaction » ; HITD, DEC ; HIT, TIMB, WASH | trois composantes à niveaux séparés, deux décroissances |
| CB CLASSIC, CB METALLIC | deux oscillateurs, TUNE, DET, DEC | deux carrés désaccordables |
| UT NOISE | bruit blanc, LPF résonant à balayage (SDEP ±, STIM), HPF, ATK, DEC | générateur de bruit filtré |
| UT IMPULSE | impulsion ± ; le manuel conseille de « ping » le filtre de piste (FREQ ~48, RESO haut) pour des toms | **impulsion dans un filtre résonant : la topologie du 808** |

À noter : le manuel Syntakt lui-même décrit, pour UT IMPULSE, le principe du pont en T du 808 (un filtre résonant
excité par une impulsion). C'est le seul endroit où une topologie « 808 » apparaît chez Elektron, et elle passe par le
filtre de piste, pas par une machine BD.

### 1.2 Descriptions de lancement (Analog Rytm)

- **Rytm OS 1.30, 2016** [SEC, citant Elektron] : BBoy Tech Report, 2016-06-22,
  https://www.bboytechreport.com/2016/06/22/elektrons-analog-rytm-gets-a-major-update-1-30 ; liste sur MatrixSynth,
  2016-04-28, https://www.matrixsynth.com/2016/04/analog-rytm-os-130-update.html ; CDM (P. Kirn), 2016-04-29,
  https://cdm.link/elektron-just-added-12-new-machines-to-the-analog-rytm/. Douze machines ajoutées. Résumé :
  - BD PLASTIC : modulation en anneau et FM entre deux VCO, harmoniques « presque plastiques ».
  - BD SILKY : grand « sweet spot », clic de VCO doux pour modeler le transitoire, DUST pour un bruit de bande.
  - BD SHARP : « modern, hard-hitting », un seul VCO à formes choisies, qui peut tourner librement pour que deux coups
    diffèrent légèrement.
  - SD NATURAL : un seul VCO, couleur de bruit réglable, des caisses « acoustiques » aux petites caisses électroniques.
  - Ride : type d'oscillateur choisi, niveaux séparés des composantes de cymbale.
  - CY, CH, OH METALLIC, CB METALLIC : « crisp, metallic », sans détail de structure.
  - HH BASIC : « thin, electronic ».
  - NOISE : bruit blanc, LPF résonant, HPF, enveloppe, balayage automatique du LPF. IMPULSE : impulsion à attaque,
    décroissance et polarité réglables.
- **Rytm OS 1.70, 2023-11-22** [SEC] : Synthtopia, 2023-11-22,
  https://www.synthtopia.com/content/2023/11/22/elektron-cant-stop-releasing-free-updates-for-the-analog-rytm-adds-euclidean-sequencing-more/ ;
  MusicRadar (page non lisible ici). Cinq machines : SY CHIP (écho de la SidStation, première machine d'Elektron),
  SY RAW (venue du Syntakt), BD et SD ACOUSTIC, HH LAB (« laboratoire métallique »). Même mise à jour : forme carrée
  pour BD SHARP, largeur d'impulsion pour CB CLASSIC et CB METALLIC. Le Syntakt a reçu BD/SD ACOUSTIC, HH LAB et
  SY CHIP en OS 1.30 (2024-10-16, gearnews).
- **Rytm 2014, machines d'origine** (BD/SD/RS HARD et CLASSIC, BD/SD FM, CH/OH/CY/CB CLASSIC) : je n'ai trouvé aucune
  description officielle. Polynominal (page de banque d'échantillons, non datée, après 2019) [SEC],
  https://www.polynominal.com/Elektron-Analog-Rhythm : BD CLASSIC « sinewave modulated thru the pitch EG », HARD
  « same with SNAP » ; SD : deux oscillateurs désaccordés + bruit blanc + snap ; RS : deux oscillateurs + impulsion +
  bruit ; CH/OH : six carrés dans un filtrage complexe ; CB : carré « similar to tr808 ».
- **Revue** : Paul Nagle, Sound On Sound, 2014-09, https://www.soundonsound.com/reviews/elektron-analog-rytm [SEC].
  BD CLASSIC et HARD « big, bold, deep », impossible de choisir ; BD FM pour les sons étranges ; caisses claires
  « reminiscent of drum boxes of yore » ; cymbale « unremarkable » ; cloche « as ghastly as tradition demands ».
  Aucune comparaison 808/909.

### 1.3 Forums : ce qui est opinion, pas fait

- **darenager, « Analog sound engines on the Rytm », Elektronauts, 2020-01-18** [FORUM],
  https://www.elektronauts.com/t/analog-sound-engines-on-the-rytm/118392. Le seul relevé machine par machine trouvé :
  aucune machine n'est spécifiquement 808/909 ; « BD hard is probably closest to 909 BD » ; SILKY la plus proche du
  606/808 ; hats, cymbales et toms proches du 606/808 ; cloche 808 ; rim, clap et caisses claires proches d'aucune
  ancienne machine. Il ajoute que les BD du Rytm sont surtout des VCO sinus/triangle balayés, pas la topologie du
  808, et que leurs enveloppes ne permettent pas une imitation Roland convaincante ; pour un 808, il conseille
  l'impulsion dans un filtre résonant.
- **omatic, « Do you rate the AR's BASIC sounds », Elektronauts, 2018-06-22** [FORUM],
  https://www.elektronauts.com/t/do-you-rate-the-ars-basic-sounds-and-if-not-why-not/57906?page=2 : « mostly nowhere
  close to a 909 », plutôt « 70's then 80's », moteur « tuned for distorted techno », manque du punch 808/909 imputé
  aux enveloppes. stutech, même date : les graves dominent les aigus.
- **Schnork, « How to reproduce an 808 kit with Analog Rytm MKII », 2022-03-25** [FORUM],
  https://www.elektronauts.com/t/how-to-reproduce-an-808-kit-with-analog-rytm-mkii/169879 : préfère BD SILKY pour un
  808 ; d'autres conseillent le tom basse (BT), c'est-à-dire encore un VCO grave.
- **Syntakt Science Lab #14 (BD HARD, SILKY), Nils, 2025-12-09** [FORUM],
  https://www.elektronauts.com/t/syntakt-science-lab-14-bd-hard-bd-silky/243117 : les deux « seem to cover some of the
  same ground ». **#21 (BD SHARP, CLASSIC), 2026-03-31**,
  https://www.elektronauts.com/t/syntakt-science-lab-21-bd-sharp-bd-classic/248585 : « bit limited » ; Oxenholme
  (2026-04-10) voit BD SHARP comme un monosynthé analogique basique. Aucun des deux fils ne nomme un classique.
- **saint_stink, « Synthesizing a 909 Kick », 2020-05-25** [FORUM],
  https://www.elektronauts.com/t/synthesizing-a-909-kick/132188 : les moteurs BD du Rytm n'ont pas le transitoire de
  bruit filtré passe-bas du 909 ; il le refait avec la machine NOISE. grayxr (2020-09-30) : le 909 arrondit son
  triangle par une diode.

Le constat des utilisateurs eux-mêmes (« same ground », « limited ») rejoint celui de l'écoute ici : les grosses caisses
du Syntakt sont parentes par construction (un VCO, une enveloppe de hauteur, un transitoire, OVER). Notre marge pour
les séparer tient au circuit de timbre, que nous écrivons.

## 2. Attribution par machine, avec degré de confiance

Confiance : **forte** = fréquences lues dans le firmware ou topologie documentée identique ; **moyenne** = topologie
documentée compatible plus témoignage concordant ; **faible** = par élimination ou par le nom seul.

| Machine (maison) | Évoque | Preuve | Confiance |
|---|---|---|---|
| BD HARD (HARD THUMP) | TR-909 BD | darenager 2020 ; SNAP + TICK = impulsion + bruit du 909 ; balayage profond fixe comme le 909 (dont la décroissance de hauteur n'est pas réglable sur l'original) | moyenne |
| BD CLASSIC (NOISE KICK) | grosse caisse VCO générique « classique » ; TRAN bruit rappelle l'attaque bruitée du 909, la forme symétrique le sinus du 808 | aucun témoignage ; lecture des paramètres | faible |
| BD FM (PHASE KICK) | grosse caisse FM à deux opérateurs (Yamaha DX, Machinedrum EFM-BD, FM drums de Chowning) | manuel (FM, deux décroissances) | moyenne pour la topologie, faible pour un modèle précis |
| BD PLASTIC (TOY KICK) | rien de classique ; FM + anneau entre VCO à la même hauteur, timbre « plastique » | Elektron 2016 | — (pas de cible classique) |
| BD SILKY (SOFT KICK) | 808/606, son « sinus long » | darenager 2020, Schnork 2022 ; DUST = 808 échantillonné sur bande ? [INFER] | moyenne |
| BD SHARP (SNAP KICK) | grosse caisse techno « moderne » à VCO riche (scie, carré) ; proche des modules eurorack et du 909 saturé | Elektron 2016 (« modern, hard-hitting ») | faible |
| BD ACOUSTIC (BODY KICK) | grosse caisse acoustique : batte + membrane | nom, IMP PCM + bruit | moyenne |
| SD HARD (TWIN SNARE) | TR-909 SD : deux VCO à intervalle fixe, enveloppe de hauteur, bruit | intervalle fixe et balayage = 909 (Reid 2002) ; Plaits « synthetic snare » : rapport 1,47 attribué au 909 | moyenne |
| SD CLASSIC (BEAT SNARE) | TR-808 SD : deux « coques » à équilibre réglable (TONE du 808) + bruit « snappy » | DET + BAL = TONE 808 ; Polynominal | moyenne |
| SD FM (WARP SNARE) | caisse claire FM (DX, Machinedrum EFM-SD) | manuel | faible |
| SD NATURAL (SNAP SNARE) | caisse claire acoustique simplifiée, ou petites caisses électroniques (CR-78, Simmons SDS-V : un VCO + bruit filtré par VCF) | Elektron 2016 ; SDS-V voir §4.4 | faible |
| SD ACOUSTIC (BODY SNARE) | caisse claire acoustique : batte + coque + timbre | nom, IMP | moyenne |
| RS HARD (CLICK RIM) | TR-909 RS (trois résonateurs, deux fixes) ? | « oscillators » liés à un seul TUNE ; aucun témoignage | faible |
| RS CLASSIC (RING RIM) | TR-808 RS : deux ponts en T + étage à diode | TUN1/TUN2/BAL ; service 808 | faible à moyenne |
| SY DUAL VCO (TWIN OSC) | percussion de synthé à deux VCO (synthé modulaire, Syndrum) | manuel | — |
| SY RAW (RAW OSC) | machine Syntakt d'origine | Elektron 2023 | — |
| SY CHIP (CHIP ARP) | SID du Commodore 64, SidStation d'Elektron (arpège à vitesse de note, bruit SID) | manuel, Elektron 2023 | forte |
| CH, OH CLASSIC | TR-808 CH/OH | six fréquences 808 lues dans le firmware (note 14) | forte |
| CY CLASSIC | TR-808 CY | mêmes fréquences ; TONE = réglage de tonalité du 808 | forte |
| CY RIDE | TR-808 CY (trois bandes) étendue, avec modes logiques (XOR façon KR-55) | mêmes fréquences ; HIT/TIMB/WASH = trois bandes du 808 [INFER] | moyenne |
| HH LAB | TR-808 (fréquences d'usine) ou n'importe quel banc | mêmes fréquences par défaut | forte |
| **HH BASIC** | **TR-606 CH/OH** | **nouveau, §5.4** : cinq des six fréquences du firmware à 1,3 % de celles du 606 | forte |
| CH, OH METALLIC | paires modulées en anneau (Korg KR-55 ; Plaits « RingModNoise ») | trois paires (grave, ~8,5-13,5 kHz) lues dans le firmware, §5.4 | moyenne |
| CY METALLIC | même chose, plus transitoire séparé | idem | moyenne |
| CB CLASSIC | TR-808 CB | Polynominal ; darenager ; Rytm FPGA 524/774 Hz contre 540/800 sur 808 (note 14) | forte |
| CB METALLIC | cloche à carrés à PWM, désaccord plus large ? | Elektron 2016/2023 | faible |
| UT NOISE | générateur de bruit filtré de synthé (et maracas/bruit des boîtes à rythme) | manuel | — |
| UT IMPULSE | excitateur du pont en T 808 (tom, grosse caisse 808 via le filtre de piste) | manuel | forte pour l'usage |

## 3. Grosses caisses : circuits de référence

### 3.1 TR-808 BD : résonateur pincé

Source primaire : **Werner, Abel, Smith, « A Physically-Informed, Circuit-Bendable, Digital Model of the Roland TR-808
Bass Drum Circuit », DAFx-14, Erlangen, 2014-09** [PAPER],
https://dafx14.fau.de/papers/dafx14_kurt_james_werner_a_physically_informed,_ci.pdf (lu en entier).

- **Ce qui résonne** : un réseau en pont en T (bras capacitifs, pont résistif) autour d'un ampli-op, c'est-à-dire un
  **passe-bande** à fort Q, pas un oscillateur. Centre calculé : **49,5 Hz** ; tableau « typical » de Roland : 56 Hz.
  Une boucle de rétroaction (« feedback buffer », filtre en plateau haut réglé par le potentiomètre DECAY) remet de
  l'énergie dans le pont : moins il atténue, plus la note est longue. Au maximum, plusieurs secondes.
- **Excitation et clic** : la logique de déclenchement fournit une impulsion de **1 ms** d'amplitude 4 à 14 V selon
  l'accent. Un « pulse shaper » (plateau bas passif non linéaire, avec diode) laisse passer les hautes fréquences de
  l'impulsion et « rase » son flanc descendant : le flanc montant vaut toute l'amplitude, le flanc descendant environ
  une chute de diode (0,71 V) quelle que soit l'amplitude. **Ce sont les deux flancs qui pincent le pont** et
  produisent le clic ; il n'y a pas de générateur de bruit.
- **Hauteur** : deux phénomènes que la littérature confond, selon l'article. (1) À l'attaque, un générateur
  d'enveloppe (Q41-Q42) court-circuite une résistance du pont pendant ~5 ms après le flanc descendant : **fréquence
  centrale et Q montent de plus d'une octave pendant ~6 ms**, moins d'une période à la fréquence haute. Ce n'est pas
  perçu comme un glissando mais rend l'attaque « punchier » et « crisper ». À la fin, une impulsion de réamorçage
  (C39, R161, D52) relance le pont pour éviter un saut d'amplitude. (2) Le « soupir » (pitch sigh) : une modulation
  lente et subtile par fuite à travers R161.
- **Mesure indépendante du soupir** : Deruty, « Harmonic and transposition constraints arising from the use of the
  Roland TR-808 bass drum », ISMIR 2024, arXiv 2502.07524 (v2 2026-07-17, CC BY 4.0) [PAPER],
  https://arxiv.org/abs/2502.07524. Sur des échantillons de 808 : f0 modale **49,48 Hz** (presets non saturés),
  balayage initial médian **proche d'un demi-ton**, attaque d'environ **0,4 s** avant un pseudo-sinus de plusieurs
  secondes ; harmoniques présents au début puis éteints.
- **Sortie** : passe-bas passif (TONE), diviseur (LEVEL), passe-haut. Pas de saturation voulue ; les variations entre
  808 viennent des tolérances (condensateurs ±20 %).
- **Pas de « mitraillette »** : un nouveau coup pendant le précédent ajoute de l'énergie au même résonateur, il ne
  redémarre pas une forme d'onde. Le modèle de Werner le reproduit ; c'est un critère d'écoute.
- **Mêmes conclusions, autre source** : Gordon Reid, « Practical Bass Drum Synthesis », Sound On Sound, 2002-02
  [SEC], https://www.soundonsound.com/techniques/practical-bass-drum-synthesis (miroir lu :
  https://csclub.uwaterloo.ca/~pbarfuss/bassdrumsynth.html) : ni bruit, ni générateur de contour séparé, ni VCA de
  décroissance ; l'impulsion plus l'accent fait le battement ; passe-bas après la sommation pour réduire le clic ;
  la hauteur baisse légèrement aux longues décroissances.
- **Code** : Plaits `analog_bass_drum.h` (É. Gillet, 2016, MIT) [CODE],
  https://github.com/pichenettes/eurorack/blob/master/plaits/dsp/drums/analog_bass_drum.h, « 808 bass drum model,
  revisited ». Structure, décrite sans le recopier : un SVF passe-bande comme résonateur, Q croissant avec DECAY ;
  impulsion de 1 ms (hauteur 3 à 10 selon l'accent) décroissant en 0,2 ms, passée-haut ; impulsion de FM de 6 ms qui
  monte la fréquence du résonateur (la montée d'attaque de Werner) ; impulsion de réamorçage de −0,8 décroissant en
  50 ms ; « self FM » par la sortie passe-bas passée dans une diode asymétrique ; passe-bas de tonalité de 4 f0 à
  ~9 octaves au-dessus ; fuite d'excitation directe vers la sortie. Mode « sustain » : un sinus remplace le
  résonateur.
- **Autres modèles** : thèse de Werner (Stanford, 2016, WDF, l'808 BD en fil rouge),
  https://purl.stanford.edu/jy057cz8322 [PAPER] ; blog Wolfram, 2021-12-29 [SEC] (déjà dans `analog-literature.md`).

### 3.2 TR-909 BD : VCO, conformateur, bruit

Aucun article revu ne modélise le 909 BD ; aucune mesure publiée de sa hauteur n'a été trouvée. Ce qui suit vient de
sources secondaires concordantes.

- **Reid, SOS 2002-02** (ci-dessus), figure 13 : **deux chemins mélangés**. Corps : oscillateur (une dent de scie
  selon Reid) dont la hauteur suit EG3 (attaque instantanée, décroissance lente), **conformateur qui retire presque
  tous les harmoniques** vers un sinus, VCA sur EG1 dont l'accent modifie l'amplitude et la pente. Attaque : bruit
  blanc dans un **passe-bas**, additionné à une **impulsion courte**, le tout dans un VCA sur EG2. Pas de chiffre.
- **Forums** [FORUM] (fil « Synthesizing a 909 Kick », 2020) et clones : VCO **triangle** à trigger de Schmitt (pas
  une scie), arrondi par une diode (un passe-bas non linéaire plutôt qu'un écrêtage dur, grayxr 2020-10-01).
  L'électronique du 909 est plus complexe que celle du 808 : conformateur, générateur d'impulsion, générateur de
  bruit (bonedo, cité par la recherche, non ouvert).
- **Commandes d'origine** : Level, Tune, Attack, Decay (Roland, fiche technique TR-909,
  https://support.roland.com/hc/en-us/articles/201921899-TR-909-Technical-Specifications, non ouverte). TUNE règle la
  décroissance de l'enveloppe de hauteur, donc sa durée, pas sa profondeur ; ATTACK règle le niveau de l'impulsion et
  du bruit. Le clone Tiptop BD909 ajoute « Tune Attack » [SEC].
- **Bruit du 909** : Electric Druid (T. Wiltshire), « A look at the TR-909's noise generator », 2019-12-26, mis à jour
  2020-01-11 [SEC, analyse de schéma], https://electricdruid.net/tr-909-noise-generator/ : registre à décalage de
  31 étages (prises 31 et 13), horloge ~300 kHz, blanc jusqu'au-delà de 100 kHz. Un bruit **numérique** à haute
  cadence, filtré ensuite.
- **Code** : Plaits `synthetic_bass_drum.h` (2016, MIT) [CODE], commenté « Inadvertently 909-ish ». Structure :
  triangle tiré de la phase, puis **courbe rationnelle douce vers le sinus**, mélangeable avec un sinus déphasé ;
  phase tenue à un quart de période pendant ~1,3 ms au déclenchement ; enveloppe de FM de ~8 ms (jusqu'à ~40 ms avec
  DECAY) ; corps tenu 1 ms puis exponentiel de ~20 à ~640 ms ; **clic** : impulsion après la tenue, limiteur de pente
  asymétrique, passe-haut, puis **SVF passe-bas ~5 kHz, Q 2** ; **bruit** passe-bas puis passe-haut, enveloppe de
  transitoire ~5 ms ; étage « transistor VCA » qui sature avec l'enveloppe.

### 3.3 TR-606 BD et autres résonateurs

- **D. Schmidt, notes TR-606** (Monash, non datées ; page en 404 le 2026-11-12, extrait d'index de recherche)
  [SEC] : deux ponts en T, IC5A à **60 Hz** (le corps) et IC5B à **130 Hz** (le « clic »), réponse cos(at)·e^(bt) ;
  l'accent change l'amplitude de l'impulsion qui frappe les deux. Donc un 606 = deux résonateurs pincés, plus court
  et plus haut que le 808, sans enveloppe de hauteur notable [INFER pour ce dernier point].
- **Boss DR-110**, Electric Druid, 2023-10-21, mis à jour 2024-01-23 [SEC], https://electricdruid.net/?p=71361 :
  oscillateur amorti (gain juste sous l'oscillation), l'impulsion filtrée passe-bas sert d'excitation **et** va à la
  sortie (« whack » non accordé) ; hauteur et décroissance liées (plus grave = plus long à gain égal). Cette liaison
  hauteur-décroissance est une signature des résonateurs pincés, absente des VCO.
- **Simmons SDS-V** (1981) : aucune source primaire lue. Le clone DIY de syntherjack (2014-06-16) [SEC],
  https://syntherjack.net/wp-content/uploads/2014/04/SDSV-DIY-manual-syntherjack.pdf, décrit : VCO **triangle**
  passé dans un passe-bas passif (« quelque chose comme un sinus »), hauteur modulée par envelopes (BEND) ou LFO ;
  bruit dans un **SSM2044** passe-bas résonant à balayage ; une part du filtre passe par un VCA pour faire le
  **clic**, l'autre se mélange au VCO pour le corps ; trois VCA CA3080. Les commandes d'origine : noise, tone, bend,
  decay, noise tone, click (sequencer.de, non ouvert).

### 3.4 Grosse caisse FM

- **Chowning, « The Synthesis of Complex Audio Spectra by Means of Frequency Modulation », JAES 21(7), 1973** [PAPER,
  non ouvert ici] : l'exemple de tambour y repose sur un rapport porteuse/modulante non entier et un indice qui
  décroît vite, ce qui donne une attaque riche et **inharmonique** qui s'éteint vers un sinus. C'est le trait qui
  sépare une grosse caisse FM d'une grosse caisse à balayage : l'attaque n'est pas un glissando mais un nuage de
  partiels non harmoniques.
- Le manuel Syntakt dit que la FM de SY DUAL VCO « ne suit pas la hauteur à 100 % » : FM **linéaire analogique**,
  donc un décalage de fréquence moyenne quand l'indice varie [INFER, déjà noté dans `analog-literature.md`].

### 3.5 Grosse caisse acoustique

- **Fletcher, Bassett, « Some experiments with the bass drum », JASA 64(6), 1978** [PAPER, cité par la recherche,
  non ouvert] : deux peaux accordées vers **40 Hz**, étroitement couplées par l'air : les résonances se dédoublent en
  deux fréquences voisines. Rossing, Physics Today 45(3), 1992 [PAPER, PDF trop lourd pour l'outil] : un modèle à deux
  masses et un ressort d'air décrit bien les deux premiers modes ; la charge d'air ramène les modes inharmoniques de
  la membrane idéale (rapports de Bessel 1 ; 1,59 ; 2,14 ; 2,30 ; 2,65 ; 2,92) vers une série plus serrée.
- Conséquence [INFER] : une grosse caisse acoustique = **un battement grave double** (deux modes à quelques Hz
  d'écart, d'où une légère pulsation), **quelques modes supérieurs inharmoniques** courts, un **clic de batte** large
  bande de quelques ms (le PCM d'IMP), et une baisse de hauteur faible (modulation de tension, quelques pour-cent),
  pas le grand balayage d'une machine.

## 4. Caisses claires : circuits de référence

### 4.1 TR-808 SD

- **Manuel de service TR-808** (Roland, 1981-06-15) [DOC, via manualslib p. 6],
  https://www.manualslib.com/manual/1294765/Roland-Tr-808.html?page=6 : deux ponts en T, « fundamental » et
  « harmonic » ; VR8 règle le mélange (TONE), VR9 l'amplitude de l'enveloppe « snappy ».
- **Fréquences** : Norgate, « SD 80'81 - Tuning Theory », Norgatronics, 2021-11-10 [SEC, calcul sur les valeurs du
  manuel de service], https://norgatronics.blogspot.com/2021/11/sd-8081-tuning.html : version 1980-81, coques
  **249,6 Hz et 499,0 Hz** ; révision 1981-82, **173,3 Hz et 336,0 Hz** ; manuel : « typical » 238 et 476 Hz. Rapport
  ~2,0 (1,94 sur la révision). Mesures sur RC-808 citées : 177,03 et 315,44 Hz ; bruit passe-bande ~7 kHz (Q 0,39),
  passe-bas ~2,5 kHz sur la coque basse ; enveloppe snappy ~150 ms pour une impulsion de 1 ms (Norgate, 2020-10-21,
  https://ss30m.blogspot.com/2020/10/808-snare-mutations.html).
- **Reid, « Practical Snare Drum Synthesis », SOS 2002-04** [SEC],
  https://www.soundonsound.com/techniques/practical-snare-drum-synthesis : l'enveloppe snappy est ajoutée à
  l'impulsion qui frappe les deux coques **et** commande le VCA du bruit, qui est **passé-haut** avant le mélange.
- **Code** : Plaits `analog_snare_drum.h` (2016, MIT) [CODE], « 808 snare drum model, revisited » : cinq
  résonateurs SVF aux rapports 1,00 ; 2,00 ; 3,18 ; 4,16 ; 5,62 (deux seulement en mode « 808 ») ; bruit redressé
  (positif seulement), passe-bande à ~16 f0 ; somme des coques saturée doucement.

### 4.2 TR-909 SD

- **Reid, SOS 2002-04** : deux oscillateurs et deux conformateurs pour les deux modes (0,1), **~180 et ~330 Hz**
  (rapport 1,83), chacun son VCA et son contour ; une enveloppe de hauteur que Reid juge « anomalous » (une vraie
  caisse claire ne descend pas) ; bruit passe-bas, puis deux branches : passe-haut étroit sur un contour, sans filtre
  sur un autre ; deux contours de bruit différents.
- **Plaits `synthetic_snare_drum.h`** (2016, MIT) [CODE] : deux oscillateurs au rapport **1,47** (« 909 » selon le
  commentaire), sinus distordu, couplage « funny » entre eux (réflexion de phase bruitée) attribué au 909 ; terme de FM
  qui monte les deux hauteurs quelques ms ; bruit dans un SVF passe-bas à résonance croissante avec SNAPPY, puis
  passe-haut ; enveloppe de bruit tenue 40-70 ms puis exponentielle ; corps passe-bas à ~3 f0.
- Les deux rapports publiés (1,83 chez Reid, 1,47 dans Plaits) ne s'accordent pas. À trancher sur un enregistrement
  [INFER : mesurer les deux pics du corps sur un 909 propre].
- Clone Tiptop SD909 [SEC] : bruit binaire à horloge réglable (métallique aux basses horloges), deux triangles
  modulés ensemble.

### 4.3 Caisse claire acoustique

- Rossing (revue « Acoustics of percussion instruments: recent progress », Acoust. Sci. & Tech., 2001, cité par la
  recherche) : paires de modes (0,1) entre ~168/193 Hz (humide) et ~202/227 Hz (sec), puis (1,1) ~333 Hz et (2,1)
  ~468 Hz ; fort couplage entre les deux peaux. **Attribution à la caisse claire non vérifiée** (extrait sans
  contexte). Le timbre (fils) ajoute un bruit large bande déclenché par le mouvement de la peau de résonance, donc
  **modulé par le corps**, pas une enveloppe indépendante [INFER, à vérifier dans Rossing et al., JASA 1992].

### 4.4 Petites caisses électroniques (pour SD NATURAL)

- SDS-V (§3.3) : un VCO + bruit dans un VCF 4 pôles résonant à balayage. C'est la topologie la plus proche de SD
  NATURAL (un oscillateur, bruit HPF + LPF résonant) [INFER].

## 5. Rims, cloches, charlestons, cymbales

### 5.1 Rimshots

- **TR-808 RS/CL**, manuel de service p. 6 [DOC] : la sortie CL vient d'un pont en T multi-rétroaction (IC20) ; pour
  RS, la sortie d'IC20b passe avec celle d'IC21 dans un **étage à diode (D62)**. SyntherJack, 2016-11-24 [SEC],
  https://syntherjack.net/the-perfect-808-rimshot-clave/ : deux générateurs en pont en T mélangés, puis VCA (Q62) très
  court et filtre (Q63). Aucune fréquence publiée trouvée ; la valeur 455/1667 Hz qui circule n'a pas été confirmée.
- **TR-909 RS** [SEC, clone Tiptop RS909 et revendeurs] : **trois** oscillateurs en pont en T, chacun un sinus amorti
  à une fréquence différente, deux à fréquence fixe sur l'original. Fréquences non trouvées.
- Lecture [INFER] : RS CLASSIC (deux hauteurs indépendantes + équilibre) colle au 808 ; RS HARD (un réglage pour
  « les oscillateurs », balayage, bruit) à une famille 909 à rapports fixes. Le trait commun des rims classiques :
  **résonateurs aigus très amortis** (quelques ms) et un étage non linéaire (diode, VCA « swing ») qui rend le son
  sec, pas un VCO long.

### 5.2 Cloches

- **TR-808 CB**, manuel de service p. 6 [DOC] : deux carrés de fréquences différentes (oscillateurs #5 et #6 du banc,
  **540 et 800 Hz** après réglage d'usine, Werner 2014), commutés par des VCA (Q14, Q15), mélangés et filtrés (IC2) ;
  le réseau R82/C34 donne au **début de la décroissance une chute brusque** qui souligne l'attaque : enveloppe en deux
  temps, impact court puis queue.
- Reid, « Synthesizing Cowbells & Claves », SOS 2002-09 [SEC],
  https://www.soundonsound.com/techniques/synthesizing-cowbells-claves : même chaîne ; son patch d'imitation utilise un
  passe-bande à **2,64 kHz**, 12 dB/oct. Article Werner « More Cowbell », AES 137, 2014-10 [PAPER], payant, non lu.
- Rytm : 524/774 Hz lus dans la trame FPGA (note 14). Rapport 808 : 1,48 ; Rytm : 1,48. Même cloche, transposée.

### 5.3 Charlestons et cymbales 808

- **Werner, Abel, Smith, « The TR-808 Cymbal », ICMC-SMC 2014** [PAPER, CC BY 3.0],
  https://speech.di.uoa.gr/ICMC-SMC-2014/images/VOL_2/1453.pdf (lu) :
  - Banc commun aux CB, CY, OH, CH : six inverseurs de Schmitt (un HD14584), fréquences nominales **205,3 ; 369,6 ;
    304,4 ; 522,7 ; 359,4-1149,9 ; 254,3-627,2 Hz**, les deux derniers réglés en usine à 800 et 540 Hz.
  - Mélange passif, puis **deux passe-bande actifs à ~3440 Hz et ~7100 Hz**, qui accentuent les harmoniques hauts et
    écrasent les fondamentales ; chaque flanc d'un carré « frappe » les filtres.
  - Impulsion de 1 ms, « attack smoother » (constante ~0,1 ms), **trois générateurs d'enveloppe** (diode + RC, la
    décroissance d'une bande suit DECAY), **trois VCA « swing »** non linéaires (un amplificateur à émetteur commun
    dont l'alimentation est l'enveloppe : **écrêtage voulu**, variable d'un transistor à l'autre), trois passe-haut de
    Sallen-Key (le troisième d'ordre 3, résonant vers **10,5 kHz**), une tonalité qui atténue surtout la troisième
    bande, une sortie qui dérive (+6 dB/oct).
  - Rendu à 4× suréchantillonnage ; le repliement du VCA est jugé inaudible car les partiels sont déjà serrés et
    inharmoniques.
- Manuel de service p. 6 [DOC] : la bande la plus haute a une décroissance courte, une bande un peu plus basse une
  décroissance réglable, VR4 dose les trois. OH : bande haute, VCA Q27, filtre Q26 ; CH : même source, VCA Q30,
  filtre Q31 ; CH coupe la décroissance d'OH (Q23).
- Comparaison DR-110 (Electric Druid 2023) [SEC] : quatre carrés (305, 444, 794, 1136 Hz) + bruit, deux filtres à
  **3,45 kHz et 7,12 kHz, Q ≈ 6** ; CH ~80 ms, OH ~700 ms ; cymbale = « ting » (attaque ~60 ms, corps ~900 ms) +
  queue de la bande basse ~1,4 s. Mêmes centres que le 808 : ordres de grandeur utilisables pour le 808.
- **Plaits `hi_hat.h`** (2016, MIT) [CODE], « 808 HH » : six carrés aux rapports 1,0 ; 1,304 ; 1,466 ; 1,787 ;
  1,932 ; 2,536 (f0 nominal 414 Hz, appelé à 2 f0) ; SVF passe-bande de 150 Hz à 16 kHz selon TONE ; passe-haut après
  le VCA ; **« SwingVCA » asymétrique** (positif amplifié, négatif écrasé, écrêtage doux) ; décroissance en deux
  pentes optionnelle (charleston étouffé).

### 5.4 Ce que les tables du firmware disent de plus (nouveau)

Les mots de fréquence de la note 14, convertis à l'échelle 2^24/1500 par Hz, tombent sur des entiers exacts
(540,0 ; 800,0 ; 8529,0 ; 13500,0) : l'échelle supposée dans la note 14 est très probablement la bonne.

| Table | Hz (ordre du firmware) | Rapprochement |
|---|---|---|
| CLASSIC (CH, OH, CY, RIDE, LAB) | 540, 800, 522, 304, 370, 205 | 808 : 540, 800, 522,7, 304,4, 369,6, 205,3 (Werner 2014) |
| HH BASIC | 246, 418, 308, 440, 241, 627 | **606 : 245, 306, 365, 415, 437, 619** (Erica Synths, notes de construction, d'après M. Klein, cité 2024-02-09 sur Elektronauts « HH LAB tips », https://www.elektronauts.com/t/hh-lab-tips/207960) [SEC] |
| CH, OH METALLIC | (272, 8529), (633, 9073), (1458, 13500) | trois paires (grave, aigu) ; Plaits `RingModNoise` : (200, 7530), (510, 8075), (730, 10500), « closer to the KR-55 or FM hi-hats » |
| CY METALLIC | (272, 8601), (641, 9161), (1485, 13703) | idem, aigus légèrement plus hauts |

- **HH BASIC ≈ 606** : 246/245, 308/306, 418/415, 440/437, 627/619 ; seul 241 n'a pas d'équivalent (le 606 a 365).
  Écart ≤ 1,3 %. Confiance forte sur la parenté, réserve sur la source des valeurs 606 (secondaire). Le 606 mélange
  ses six carrés sans pré-filtre passe-bande comme le 808 (T. Wiltshire sur synth-diy, 2023-11 [FORUM],
  https://synth-diy.org/pipermail/synth-diy/2023-November/173066.html : « TR-606 uses six squarewave sources,
  mixed » ; DR-110 quatre sources filtrées ; KR-55 paires de six sources modulées en anneau par des XOR 4070).
- **METALLIC = paires en anneau** : la forme des tables (trois fréquences graves, trois partenaires à 8-14 kHz) est
  celle d'un modulateur en anneau par paires, comme le KR-55 (XOR de deux carrés) ou le `RingModNoise` de Plaits
  (produit carré × scie). Le XOR de deux carrés donne les fréquences somme et différence : autour de 8,3-8,8 kHz,
  8,4-9,7 kHz, 12-15 kHz, c'est-à-dire **trois grappes aiguës étroites**, très différentes du nuage 3-7 kHz du 808
  [INFER, calcul].
- **CY RIDE** : TYPE change « fréquences et interaction logique » ; HIT, TIMB, WASH sont trois niveaux, HITD et DEC
  deux décroissances. C'est la structure à trois bandes du 808 (haute courte, médiane réglable, dosage par VR4), avec
  un choix de combinaison logique en entrée (somme comme le 808/606, XOR comme le KR-55) [INFER].

## 6. Synthés et utilitaires

- **SY CHIP** : SID (MOS 6581/8580) et la technique d'arpège des musiques de jeu des ordinateurs 8 bits ; la
  SidStation (Elektron, 1999) en est l'ancêtre maison (Elektron 2023). Traits : arpège à vitesse de note qui donne
  l'illusion d'un accord, bruit SID **pitché** (registre à décalage cadencé à la hauteur de note), PWM. Le manuel
  dit lesquelles de ces parts sont numériques (bruit, PWM) : à porter, pas à modéliser.
- **SY DUAL VCO, SY RAW** : pas de classique unique. La FM linéaire analogique (décalage de hauteur avec l'indice) et
  l'anneau par carré (SY RAW) sont les deux traits documentés.
- **UT NOISE** : bruit blanc + LPF résonant à balayage + HPF. Le bruit du 909 est un LFSR 31 bits à ~300 kHz (§3.2) ;
  celui du DR-110 un LFSR aussi ; celui du 808 un bruit de jonction (non lu ici).
- **UT IMPULSE** : l'excitateur du pont en T. Le manuel Syntakt le présente comme moyen de faire des toms en
  « pinçant » le filtre de piste. C'est exactement la topologie 808 BD/tom (manuel de service 808 p. 6 : toms en pont
  en T, hauteur plus haute au début puis qui descend quand la résonance décroît, par la résistance des diodes D80/D81).

## 7. Tableaux par famille : topologie, traits, cibles mesurables

« Cible » = valeur publiée et sa source. « — » = rien de publié, à mesurer. Les centroïdes spectraux ne sont publiés
nulle part : à mesurer sur les références du §9.

### 7.1 Grosses caisses

| Machine | Topologie de référence | Traits qui définissent le son | Hauteur | Balayage (profondeur, temps) | Décroissance | Attaque / clic | Sources |
|---|---|---|---|---|---|---|---|
| (classique) TR-808 BD | pont en T pincé + rétroaction de décroissance, pas de VCO | sinus amorti, Q monte avec DECAY ; montée > 1 octave à l'attaque ; soupir ; clic = flancs de l'impulsion ; pas de bruit ; superposition sans redémarrage | 49,5 Hz calculé, 56 Hz « typical », 49,48 Hz mesuré | attaque : > 1 oct. pendant ~6 ms ; soupir : ~1/2 ton sur ~0,4 s | jusqu'à plusieurs s | impulsion 1 ms, deux flancs ; passe-bas TONE | Werner 2014 ; Deruty 2024 ; Reid 2002 |
| (classique) TR-909 BD | VCO triangle → conformateur à diode vers sinus ; chemin d'attaque séparé : impulsion + bruit LP | sinus « rond » mais légèrement impair, glissando nettement audible, attaque bruitée et claquante, saturation de VCA | — (à mesurer) | plus profond et plus long que le 808 ; TUNE règle le temps | — | impulsion + bruit LP, niveau = ATTACK | Reid 2002 ; Plaits synthétique (FM 8-40 ms, clic LP 5 kHz Q 2, bruit ~5 ms) |
| (classique) TR-606 BD | deux ponts en T, 60 et 130 Hz | court, plus aigu, « clic » tonal à 130 Hz | 60 Hz | — | court | second résonateur | Schmidt (non daté) |
| (classique) FM | 2 opérateurs, rapport non entier, indice qui chute | attaque inharmonique, queue sinus | — | indice : quelques ms | — | partiels de FM | Chowning 1973 |
| (classique) acoustique | deux peaux couplées + air ; batte | battement double grave, modes hauts inharmoniques courts, clic large bande | ~40 Hz par peau | faible (tension) | — | clic de batte large bande, quelques ms | Fletcher-Bassett 1978 ; Rossing 1992 |
| BD HARD | → 909 | VCO, profondeur fixe, SNAP + TICK, OVER | — | profondeur fixe, STIM | HOLD + DEC | deux transitoires | manuel ; darenager 2020 |
| BD CLASSIC | → VCO générique, entre 808 et 909 | forme symétrique réglable, TRAN clic **ou** bruit | — | STIM, SDEP | HOLD + DEC | clic ou bruit | manuel |
| BD FM | → FM | deux balayages, deux décroissances | — | deux | FM D, DEC | FM | manuel |
| BD PLASTIC | → (aucun) | FM + anneau entre VCO de même TUNE | — | STIM, SDEP | DEC | TICK | manuel ; Elektron 2016 |
| BD SILKY | → 808/606 | clic **produit par le VCO**, DUST, grand sweet spot | — | STIM, SDEP | HOLD + DEC | CLIK (doux) | manuel ; Elektron 2016 ; darenager 2020 |
| BD SHARP | → techno moderne | scie/carré, oscillateur libre | — | STIM, SDEP | HOLD + DEC | TICK | manuel ; Elektron 2016 |
| BD ACOUSTIC | → acoustique | transitoire PCM + bruit, six formes | — | STIM, SDEP | HOLD + DEC | IMP (numérique) | manuel |

### 7.2 Caisses claires

| Machine | Topologie de référence | Traits | Hauteurs | Bruit | Enveloppes | Sources |
|---|---|---|---|---|---|---|
| (classique) TR-808 SD | deux ponts en T + bruit passe-haut | coques à rapport ~2 ; TONE dose les coques ; SNAPPY dose impulsion et bruit | 249,6/499 Hz (1980-81) ; 173/336 Hz (révision) ; 238/476 « typical » | blanc, passé-haut (~7 kHz BP mesuré sur RC-808) | coques : résonance ; bruit ~150 ms | service 808 ; Norgate 2020-21 ; Reid 2002 |
| (classique) TR-909 SD | deux VCO triangle + conformateurs + bruit LP puis deux branches | enveloppe de hauteur, couplage des deux VCO, deux couleurs de bruit | ~180/330 Hz (Reid) ; rapport 1,47 (Plaits) | LFSR 31 bits, LP puis HP étroit + branche large | tenue 40-70 ms puis exp. (Plaits) | Reid 2002 ; Plaits ; Electric Druid 2019 |
| SD HARD | → 909 | intervalle fixe, balayage, TICK | — | NDEC, NLEV | DEC | manuel |
| SD CLASSIC | → 808 | DET + BAL, SNAP | — | NDEC, NLEV | DEC | manuel |
| SD FM | → FM | FM + bruit | — | NDEC, NLEV | FM D, DEC | manuel |
| SD NATURAL | → SDS-V / acoustique simple | un VCO, bruit HPF + LPF résonant | — | filtré, résonant | DEC, NDEC | manuel ; syntherjack 2014 |
| SD ACOUSTIC | → acoustique | PCM + bruit, corps, HOLD | — | NDEC, NLEV | BDEC, HOLD | manuel |

### 7.3 Rims

| Machine | Topologie de référence | Traits | Cibles | Sources |
|---|---|---|---|---|
| (classique) TR-808 RS | deux ponts en T (dont celui des claves) → diode → VCA très court → filtre | sec, aigu, non linéaire | fréquences non publiées | service 808 ; SyntherJack 2016 |
| (classique) TR-909 RS | trois ponts en T, deux fixes, VCA | trois sinus amortis | non publiées | clone RS909 [SEC] |
| RS HARD | → 909 ? | un TUNE, balayage, TICK, bruit | — | manuel |
| RS CLASSIC | → 808 | deux TUNE + BAL, TICK, bruit | — | manuel |

### 7.4 Métal (piste 12)

| Machine | Référence | Fréquences | Filtres | Enveloppes / VCA | Sources |
|---|---|---|---|---|---|
| CH, OH CLASSIC | 808 CH/OH | banc 808 | BP 3,44 et 7,1 kHz, HP Sallen-Key ; bande haute pour les hats | VCA swing ; CH coupe OH ; DR-110 : CH ~80 ms, OH ~700 ms | Werner 2014 ; service 808 ; DR-110 |
| CY CLASSIC | 808 CY | banc 808 | idem + tonalité sur la 3e bande | trois enveloppes, haute courte | Werner 2014 |
| CY RIDE | 808 CY à trois bandes + modes logiques | 808 (TYPE en change) | trois bandes | HITD court, DEC queue | manuel ; Werner 2014 [INFER] |
| HH LAB | 808 par défaut | six libres | — | DEC | manuel ; firmware |
| HH BASIC | **606** | 246, 308, 418, 440, 627 + 241 | 606 : mélange sans BP séparé, TONE ± | TDEC transitoire + DEC | firmware ; Erica/Klein ; Wiltshire 2023 |
| CH, OH METALLIC | paires en anneau (KR-55) | (272, 8529), (633, 9073), (1458, 13500) | — | DEC seul | firmware ; Plaits ; Wiltshire 2023 |
| CY METALLIC | idem + transitoire | (272, 8601), (641, 9161), (1485, 13703) | TONE du transitoire | TDEC + DEC | firmware ; manuel |
| CB CLASSIC | 808 CB | 540/800 (Rytm 524/774) | BP ~2,6 kHz (patch Reid) | deux temps : chute brusque puis queue | service 808 ; Reid 2002 |
| CB METALLIC | carrés à PWM, plus métallique | courbe de hauteur (note 14) | — | DEC | Elektron 2016, 2023 |

## 8. Ce qui doit séparer nos modèles (priorités)

L'écoute dit « trop semblables ». Les sources disent quelles différences sont **structurelles** chez les classiques.
Par famille, dans l'ordre :

**Grosses caisses.**
1. **Le conformateur et la forme du corps.** 808 : sinus amorti d'un résonateur dont Q et hauteur sont liés à la
   décroissance (le DR-110 le montre aussi). 909 : triangle arrondi par diode, donc des impairs faibles mais présents.
   SHARP : scie/carré, riche. SILKY : sinus pur. Si tous nos BD passent par le même sinus après le VCO, ils se
   confondent. Proposition [INFER] : SILKY et CLASSIC sur une topologie **résonateur pincé** (SVF passe-bande excité,
   façon Plaits analogique, montée d'octave de ~6 ms à l'attaque, soupir d'un demi-ton), HARD sur **VCO triangle +
   conformateur à diode** (façon Plaits synthétique), SHARP sur **VCO riche + saturation** sans conformateur.
2. **L'origine du transitoire.** 808 : les flancs de l'impulsion d'excitation dans le résonateur (le clic a donc la
   couleur du résonateur). 909 : impulsion **plus bruit passe-bas** sur un chemin et un VCA séparés. SILKY : clic du
   VCO (une discontinuité de phase au déclenchement). ACOUSTIC : PCM + bruit large bande (à porter du firmware). FM :
   l'attaque est le spectre FM lui-même. Cinq origines différentes, cinq couleurs d'attaque.
3. **La forme du balayage** : 808 = saut bref de l'attaque + soupir lent, pas de glissando ; 909 = exponentielle
   nettement audible ; acoustique = quasi rien ; FM = l'indice qui chute, pas la hauteur.
4. **Le comportement au redéclenchement** : un résonateur pincé accumule (pas de « mitraillette », Werner 2014) ; un
   VCO remis à zéro claque à chaque coup ; un VCO libre (SHARP) varie d'un coup à l'autre.

**Caisses claires.** (1) Le rapport des deux corps : 808 ≈ 2,0 (ponts en T, sinus amortis), 909 ≈ 1,47-1,83 avec
couplage et enveloppe de hauteur ; SD CLASSIC doit sonner deux sinus amortis équilibrés, SD HARD deux VCO couplés qui
descendent. (2) Le chemin du bruit : 808 = bruit passé-haut seul ; 909 = LP puis deux branches de couleur et de durée
différentes ; NATURAL = LPF **résonant** (couleur tonale du bruit) ; ACOUSTIC = bruit modulé par le corps [INFER].

**Rims.** Résonateurs aigus **très amortis** plus un étage non linéaire (diode, VCA swing) qui rend le son sec ;
CLASSIC à deux résonateurs indépendants, HARD à rapports fixes avec balayage.

**Métal.** (1) Trois sources différentes, déjà dans le firmware : banc 808 (CLASSIC, RIDE, LAB), banc 606 (BASIC),
paires en anneau (METALLIC). Nos modèles doivent garder trois **combinateurs** différents : somme filtrée par deux
passe-bande à 3,4 et 7,1 kHz (808), somme simple puis filtre TONE (606), XOR/produit par paires (METALLIC). (2) Le VCA
« swing » qui écrête : il donne la texture granuleuse du 808 (Werner 2014, Plaits). CY RIDE : trois bandes à
enveloppes séparées.

## 9. Références d'écoute (liste seulement, rien téléchargé)

Pour comparer à l'oreille. Les licences sont celles annoncées par les éditeurs, à relire avant tout usage.

Syntakt / Rytm (les machines elles-mêmes) :
- Elektronauts, fils « Syntakt Science Lab » (2023-2026), audio par machine, parfois le projet `.stprj` :
  index https://www.elektronauts.com/t/syntakt-science-lab/235364 ; #14 BD HARD/SILKY (2025-12-09) ; #21 BD
  SHARP/CLASSIC (2026-03-31) ; #19 SD FM/HARD,
  https://www.elektronauts.com/t/syntakt-science-lab-19-sd-fm-sd-hard/246380 ; #9 RS HARD/CLASSIC,
  https://www.elektronauts.com/t/syntakt-science-lab-9-rs-hard-rs-classic/239885 ; #8 BD FM, #16 BD PLASTIC, #23 BD
  ACOUSTIC (par l'index). Écoute, pas redistribution.
- Vidéo de démonstration de l'OS 1.30 du Rytm (Elektron, 2016), relayée par MatrixSynth 2016-04-28 avec repères :
  Ride 0:16, Metallic 1:13, Basic HH 1:54, BD PLASTIC 2:18, BD SHARP 3:16, BD SILKY 4:16, SD NATURAL 5:10, Impulse
  6:01, Noise 6:54. https://www.matrixsynth.com/2016/04/analog-rytm-os-130-update.html
- Pack Elektron « New Blood » (Rytm, 2016), démos : https://www.elektron.se/en/new-blood
- 0b5vr, Scrapbox, une page par machine Syntakt avec clips :
  https://scrapbox.io/0b5vr/Syntakt:_BD_CLASSIC
- Packs Rytm gratuits déjà listés dans la note 14 §3 (Synthycraft 80 one-shots, Lars-Erik Johansson 49 one-shots).

Classiques :
- « Cassette 909 », Bedroom Producers Blog, 155 WAV (frappes propres et passées sur bande), annoncé « 100 %
  royalty free » : https://www.kvraudio.com/news/25795
- « Reel 606 », Noizefield, 110 frappes de TR-606 enregistrées sur magnétophone (2019-02-25) :
  https://www.noizefield.com/?p=34634
- « DS-606 », Soundshock Audio, 28 frappes, annoncé libre de droits : https://soundshockaudio.com/roland-tr-606
- SampleRadar, 167 frappes 808/909 traitées, libres d'usage, sans redistribution :
  https://www.musicradar.com/music-tech/samples/sampleradar-167-free-processed-808-and-909-samples (traitées : à
  écarter pour la mesure de timbre).
- Goldbaby, packs gratuits (Cassette 808, Tape 606, TR-606) listés par Bedroom Producers Blog ; licence non lue.
- Pour la grosse caisse 808, le corpus de Deruty 2024 (Zenodo 10.5281/zenodo.14877286, contenu non vérifié) peut
  donner des mesures déjà faites.

## 10. Inconnues

- Hauteur, profondeur et durée du balayage du 909 BD : aucune mesure publiée trouvée. À mesurer sur Cassette 909
  (frappes propres) : f0 au début et à la fin, constante de temps du glissando, niveau et bande du bruit d'attaque.
- Rapport des deux corps du 909 SD (1,83 chez Reid, 1,47 dans Plaits).
- Fréquences des rims 808 et 909.
- Source primaire des fréquences du 606 (Erica Synths d'après M. Klein) et sens du sixième oscillateur de HH BASIC
  (241 Hz au lieu de 365 Hz).
- Combinateur exact des tables METALLIC (XOR comme le KR-55, ou produit) et rôle de TYPE dans CY RIDE.
- Attribution des modes acoustiques (Rossing 2001 : caisse claire ou grosse caisse) ; articles Fletcher-Bassett 1978
  et Rossing 1992 non ouverts.
- Centroïdes spectraux et temps d'attaque : rien de publié pour aucune de ces machines ; à mesurer.
