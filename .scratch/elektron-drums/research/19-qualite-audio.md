# 19 — Qualité audio : pratiques et coûts pour l'instrument

Ticket : [issues/19-qualite-audio.md](../issues/19-qualite-audio.md). Rédigé le 2026-10-24. Mesures faites sur le PC de
référence (Ryzen 7 3700X, Zen 2), **machine chargée** comme pour le ticket 01 (chiffres sans doute un peu hauts).

Banc : `C:/dev/elektron-work/scratch-arch/research/19/qbench.cpp` (build `build.cmd` : MSVC `/O2 /arch:AVX2`, flottant
précis), sorties brutes `run1.log` à `run3.log`. Mono, 48 kHz, blocs de 128. **Coût = % d'un cœur pour UNE voix**
(×16 pour 16 pistes ; ×32 pour deux drives par piste). Qualité mesurée sur une FFT de 65 536 points, fenêtre
Blackman-Harris à 4 termes (lobes ≈ −92 dB) :

- **ASR** (rapport repliement / signal) : énergie hors des harmoniques vraies (±6 bins) sous Nyquist, rapportée à
  l'énergie sur ces harmoniques. Fondamentale choisie sur un bin premier pour que les repliements ne tombent pas sur
  les harmoniques.
- **THD+N** (interpolateurs, rééchantillonneur) : énergie hors du ton de sortie (±6 bins) / énergie du ton.
- **HF** (clics) : énergie au-dessus de 2 kHz (ton à 200 Hz) ou 8 kHz (ton à 2 kHz) sur 4 096 échantillons autour
  d'une coupure, rapportée à l'énergie totale.

Les coûts scalaires sont ceux d'un premier jet, pas d'un code optimisé ; la §7 montre l'écart avec le SIMD sur les
voix, qui est la règle de la carte.

## 0. Principe : ne corriger que ce que l'instrument ajoute

La carte (Notes) pose : performance avant fidélité, étages repris d'un firmware vérifiés à l'oracle, exacts quand cela
ne coûte rien, sinon réécrits et vérifiés à tolérance. Conséquence pour la qualité :

1. **Le repliement et la quantification des machines Elektron font partie du son** : SY BITS, SRR, réduction de bits,
   FM du Digitone et du Syntakt, DIST du MD, lecture des ROM du MD à leur interpolation propre, sorties à 44,1 kHz du
   MD. Le port les reproduit tels quels ; aucune pratique ci-dessous ne s'applique à un étage porté. Une réécriture en
   flottant doit garder ce repliement (l'oracle le contient : un null test le vérifie).
2. Les pratiques s'appliquent aux **ajouts** de l'instrument : drives et saturation de la chaîne, EQ 3 bandes,
   filtres maison, layer d'échantillon, effets maison, modèles analogiques (ticket 14 : un circuit analogique ne replie
   pas, son modèle ne doit pas replier), rééchantillonnage vers la fréquence de l'hôte, coupures de voix ajoutées par
   l'instrument (layer, vol de voix), gain staging du bus flottant.

## 1. Oscillateurs : PolyBLEP, minBLEP, tables limitées en bande

Ce que ça corrige : un saut (dent de scie, carré, sync) a un spectre infini ; échantillonné tel quel, tout ce qui
dépasse Nyquist se replie en partiels inharmoniques. PolyBLEP (Välimäki, Pekonen, Nam, « Perceptually informed
synthesis of bandlimited classical waveforms using integrated polynomial interpolation », JASA 131(1), 2012 ; et
Välimäki, Huovilainen, « Antialiasing oscillators in subtractive synthesis », IEEE Signal Processing Magazine 24(2),
2007) corrige chaque saut par un résidu polynomial de 2 échantillons ; minBLEP (Brandt, « Hard Sync Without
Aliasing », ICMC 2001) insère un résidu à phase minimale tabulé, plus long et meilleur ; les tables limitées en bande
(une table par octave, « mipmaps ») ne s'appliquent qu'aux formes d'onde fixes.

Mesuré (dent de scie, `qbench` §1) :

| f0 | naïve ASR | PolyBLEP ASR | coût naïve | coût PolyBLEP |
|---|---|---|---|---|
| 997 Hz | −15,9 dB | −32,4 dB | 0,015 % | 0,017 % |
| 2 495 Hz | −11,7 dB | −27,2 dB | 0,016 % | 0,017 % |
| 5 002 Hz | −8,1 dB | −22,2 dB | 0,016 % | 0,017 % |

(L'ASR est une énergie totale non pondérée ; l'essentiel du repliement restant de PolyBLEP est près de Nyquist, peu
audible [INFER : pas de pondération psychoacoustique mesurée ici].)

Batterie : les grosses caisses et toms analogiques sont des sinus ou triangles amortis (peu de repliement) ; **les
charlestons et cymbales des modèles analogiques sont des carrés** (la 808 : six oscillateurs carrés, fréquences lues
au ticket 14) entre ~200 et ~800 Hz dont les harmoniques montent haut : c'est là que le repliement s'entend
(partiels inharmoniques qui ne suivent pas la hauteur, plus nets à SRR désactivé et sur les tunings hauts).

Recommandation : **adopter PolyBLEP** pour tout oscillateur à saut des modèles analogiques (coût ~0,002 % par
oscillateur, négligeable même ×6 par charleston) ; minBLEP ou 2× seulement si l'écoute contre enregistrement le
demande ; **ne pas faire** pour les machines portées (FM, SY, oscillateurs du Syntakt et du MD : leur repliement est
dans l'oracle).

## 2. Drives et saturation : ADAA, suréchantillonnage, HIIR

Ce que ça corrige : une non-linéarité crée des harmoniques au-delà de Nyquist, qui se replient. Deux familles :

- **ADAA** (Parker, Zavalishin, Le Bivic, « Reducing the Aliasing of Nonlinear Waveshaping Using Continuous-Time
  Convolution », DAFx-16, 2016 ; Bilbao, Esqueda, Parker, Välimäki, « Antiderivative Antialiasing for Memoryless
  Nonlinearities », IEEE Signal Processing Letters 24(7), 2017) : on applique la différence divisée de l'antidérivée
  au lieu de la fonction, ce qui équivaut à filtrer par un rectangle continu avant l'échantillonnage. Ordre 1 : un
  demi-échantillon de retard, un cas particulier quand x ≈ x[n−1]. Variante récente sans antidérivée analytique :
  Gabrielli, Squartini, « Simplifying Antiderivative Antialiasing with Lookup Table Integration », DAFx25
  (https://dafx25.dii.univpm.it/wp-content/uploads/2025/09/DAFx25_paper_30.pdf). Pour l'écrêtage dur, aussi BLAMP
  (Esqueda, Välimäki, Bilbao, « Rounding corners with BLAMP », DAFx-16).
- **Suréchantillonnage** : HIIR de Laurent de Soras (http://ldesoras.free.fr/prod.html, WTFPL), demi-bandes IIR
  polyphasées passe-tout (conception Valenzuela-Constantinides, coefficients recalculés ici par la formule d'HIIR).
  Choix des filtres : Kahles, Esqueda, Välimäki, « Oversampling for Nonlinear Waveshaping: Choosing the Right
  Filters », JAES 67(6), 2019, doi 10.17743/jaes.2019.0012
  (https://acris.aalto.fi/ws/portalfiles/portal/34665796/ELEC_Kahles2019_Oversampling_JAES.pdf, libre) : la
  décimation compte plus que l'interpolation.

Mesuré (`qbench` §2) : sinus pleine échelle × gain 8 (saturation lourde, pire cas volontaire), coût à 2,5 kHz ;
coûts scalaires dont **0,042 % de générateur de sinus soustrait** ici.

| Variante | ASR 997 Hz | ASR 2 495 Hz | ASR 5 002 Hz | Coût/voix scalaire |
|---|---|---|---|---|
| tanh rationnel, naïf | −50,7 | −24,5 | −13,2 | 0,027 % |
| tanh ADAA1 (double, log cosh) | −53,5 | −31,6 | −19,6 | 0,16-0,18 % |
| tanh 2× HIIR 8 coef. (bande passante 20,2 kHz) | −86,6 | −58,0 | −31,7 | 0,19 % |
| tanh 2× HIIR 4 coef. (bande passante 16,3 kHz) | −64,0 | −58,0 | −26,1 | 0,14 % |
| tanh 4× (HIIR 8 + 4) | −86,7 | −106,0 | −52,5 | 0,35 % |
| tanh 2× + ADAA1 | −85,7 | −67,6 | −45,0 | 0,41 % |
| écrêtage dur, naïf | −36,4 | −21,2 | −12,1 | 0,019 % |
| écrêtage dur ADAA1 | −46,1 | −28,4 | −18,6 | 0,042 % |
| écrêtage dur 2× + ADAA1 | −68,6 | −53,5 | −41,1 | 0,21 % |

Aller-retour des demi-bandes seules : gain 0,000 dB et THD+N ≤ −135 dB de 1 à 20 kHz (transparentes dans la bande).

Faits :
- **L'ADAA1 seul gagne peu** sur un tanh (3-7 dB) ; il gagne plus sur l'écrêtage dur (7-10 dB) et y est **bon
  marché** (antidérivée polynomiale : +0,02 %). En flottant simple, l'ADAA souffre d'annulation (F(x) − F(x1) divisé
  par un petit écart) : il a été fait en double ici [INFER : un float est possible avec un seuil plus large ou les
  tables de Gabrielli].
- **Le 2× HIIR gagne 25-35 dB** ; 4 coefficients suffisent à 2,5 kHz (même ASR que 8), 8 coefficients gardent la
  bande à 20 kHz.
- **En SIMD, le suréchantillonnage ne coûte presque rien** (§7) : 0,012 % par voix pour tanh 2× HIIR 8 en AVX2,
  8 voix par registre, contre 0,19 % en scalaire. Le scalaire est borné par la latence de la chaîne de passe-tout ;
  8 voix en parallèle la masquent. Le « +8,6 points pour le 2× des drives » du ticket 01 venait d'un FIR scalaire non
  optimisé ; **en SoA sur les pistes, deux drives 2× sur 16 pistes ≈ 32 × 0,012 ≈ 0,4 %** [INFER : extrapolé d'un
  noyau sans le reste de la chaîne ; à confirmer dans le code réel].

Batterie : une grosse caisse saturée (≤ 200 Hz) replie peu même naïve (harmoniques sous Nyquist jusqu'au rang ~100) ;
le repliement s'entend sur les **caisses claires, charlestons et cymbales saturés** (contenu à 2-10 kHz : ASR naïf
−13 à −25 dB, soit des partiels inharmoniques nettement audibles) et sur les transitoires d'attaque.

Recommandation : **adopter** le suréchantillonnage HIIR commutable de la carte (off / 2× / 4×), mais **en SIMD sur
les voix** : à ce coût, **2× peut être le défaut** au lieu d'off [INFER : à trancher après mesure dans la chaîne
réelle ; la latence d'HIIR 8 coef. est de quelques échantillons, à ajouter au `HitDelay` commun] ; **ADAA1 pour les
écrêtages durs maison** (cheap, se combine au 2×) ; **ne pas faire** sur DIST du MD, driveClip du Syntakt ni aucun
étage porté.

## 3. Filtres à fréquence de coupure haute : ZDF/TPT, déformation

Ce que ça corrige : la Chamberlin SVF (forme naïve d'Euler) devient instable au-delà de ~fs/6 avec Q faible et
déforme la réponse ; la discrétisation trapézoïdale (TPT, « zero-delay feedback ») avec prédéformation `g = tan(π
fc/fs)` reste stable jusqu'à Nyquist et exacte à fc. Sources : Zavalishin, « The Art of VA Filter Design », rév.
2.1.0, 2018 (https://www.native-instruments.com/fileadmin/ni_media/downloads/pdf/VAFilterDesign_2.1.0.pdf) ; Simper,
notes techniques Cytomic sur la SVF trapézoïdale (https://cytomic.com/technical-papers, lien non ouvert) ; RBJ Audio
EQ Cookbook pour les biquads de l'EQ.

Mesuré (`qbench` §4), gain à fc pour Q = 0,707 (analogique : −3,01 dB) :

| fc | Chamberlin | TPT |
|---|---|---|
| 2 kHz | −3,01 dB | −3,01 dB |
| 8 kHz | −3,01 dB | −3,01 dB |
| 12 kHz | **diverge** | −3,01 dB |
| 16 kHz | **diverge** | −3,01 dB |

Coût par voix : Chamberlin 0,033 %, TPT 0,029 %, TPT avec `tan` recalculé à chaque échantillon 0,066 %, TPT avec
`tan` tous les 32 échantillons 0,031 %.

Batterie : les filtres passe-haut des charlestons et les passe-bas ouverts à 12-18 kHz sont le cas courant, donc une
forme instable là est exclue ; un filtre modulé par une enveloppe rapide (pluck, wah de caisse claire) veut des
coefficients au moins tous les 32 échantillons.

Recommandation : **adopter TPT/SVF de Simper** pour tout filtre maison et pour les modèles analogiques (même coût que
Chamberlin) ; coefficients au bloc de 32 (à l'échantillon seulement sous modulation audio, ×2) ; biquads RBJ pour
l'EQ (déjà au ticket 01) ; **ne pas faire** sur les filtres portés (le filtre du MD, du Syntakt, ont leur propre
discrétisation et leur caractère haut dans l'oracle).

## 4. Interpolation des échantillons joués à une autre hauteur

Ce que ça corrige : l'interpolation est un filtre ; linéaire et Hermite laissent passer des images (bruit et
partiels parasites) d'autant plus fortes que le contenu est haut ; une sinc fenêtrée les supprime. Sources : Niemitalo,
« Polynomial Interpolators for High-Quality Resampling of Oversampled Audio », 2001
(http://yehar.com/blog/wp-content/uploads/2009/08/deip.pdf) ; J. O. Smith, Digital Audio Resampling Home Page
(https://ccrma.stanford.edu/~jos/resample/). Aucune interpolation fixe n'empêche le repliement **en montant** la
hauteur (contenu > fs/2/ratio) : il faut alors une sinc dont la coupure suit le ratio, ou des copies du sample
pré-décimées par octave.

Mesuré (`qbench` §3), sinus source à 0,5, THD+N :

| ratio, sortie | linéaire | Hermite 4 pts | sinc 8 pts (1 024 phases) |
|---|---|---|---|
| 1,50 → 2 kHz | −58,8 | −84,2 | −76,0 |
| 1,50 → 6 kHz | −39,5 | −55,0 | −75,3 |
| 1,50 → 12 kHz | −26,6 | −35,5 | −76,8 |
| 0,75 → 2 kHz | −46,7 | −65,9 | −73,5 |
| 0,75 → 6 kHz | −26,6 | −35,5 | −76,8 |
| 0,75 → 12 kHz (source 16 kHz) | −11,6 | −14,4 | −30,1 |

Coût par voix mono : linéaire 0,017-0,019 %, Hermite 0,026-0,039 %, sinc 8 0,071-0,078 %.

Batterie : un sample de charleston ou de cymbale baissé d'une quarte (ratio 0,75) a beaucoup d'énergie à 6-12 kHz :
Hermite y donne −35 dB, un souffle et des partiels parasites audibles sur une cymbale exposée ; une grosse caisse
s'en moque. La sinc 8 a un plancher à ~−76 dB (fenêtre et 8 points).

Recommandation : **Hermite par défaut** pour le layer (déjà prototypé) ; **sinc 8 en option « HQ »** (×2-3 le coût,
encore < 0,08 % par voix ; à 16 voix 1,2 %) ou par défaut si le budget le permet après les moteurs ; copies
pré-décimées seulement si l'on permet de monter de plus d'une octave [INFER : à décider avec la bibliothèque
d'échantillons]. **Ne pas faire** sur ROM du MD, samples du Digitakt et du Syntakt : leur interpolation est portée.

## 5. Lissage des paramètres et des locks (zipper, clics)

Ce que ça corrige : un paramètre qui saute (gain, coupure) à la cadence du contrôle produit un « zipper » ou un clic.
Les machines portées ont **leur propre lissage** (les routines de lissage du 68k du MD, celles du CPU #1 du Syntakt) et
leur cadence de contrôle (bloc de 32) : les locks y sautent comme sur la machine, c'est le caractère (et l'oracle).

Mesuré : un lisseur à un pôle par échantillon coûte 0,011 % par paramètre lissé et par voix ; une rampe linéaire au
bloc revient au même ordre [INFER].

Recommandation : **adopter** pour les seuls paramètres des ajouts (gain et pan de piste, envois, EQ, drive, layer,
coupure des filtres maison) : rampe linéaire sur le bloc de 32, ou un pôle de ~5 ms pour les gains ; **ne pas faire**
pour les paramètres des machines portées (l'OS les lisse, ou pas, comme sur la machine).

## 6. Attaques et coupures de voix sans clic (choke, retrigger, vol de voix)

Ce que ça corrige : couper un signal non nul fait un échelon, donc un large spectre (clic). Mesuré (`qbench` §6),
sinus pleine échelle coupé à sa crête, puis rampe linéaire de N échantillons à 48 kHz :

| Rampe | HF, sinus 200 Hz (> 2 kHz) | HF, sinus 2 kHz (> 8 kHz) |
|---|---|---|
| 0 (coupure sèche) | −23,4 dB | −29,7 dB |
| 16 (0,33 ms) | −38,4 | −54,8 |
| 32 (0,67 ms) | −43,7 | −61,2 |
| 64 (1,33 ms) | −52,1 | −67,1 |
| 128 (2,67 ms) | −56,3 | −73,4 |
| 256 (5,33 ms) | −62,9 | −79,8 |

Batterie : une grosse caisse coupée par un retrigger ou un choke est le pire cas (basse fréquence, forte amplitude).
Sur la machine, le choke et le retrigger du MD claquent (MoDule l'a reproduit ; l'ANTI-CLICK de X.20 à 64 ou 128
échantillons est une option, off par défaut). Une rampe de 32-64 échantillons garde l'attaque (< 1,4 ms) et retire
15-30 dB de clic.

Recommandation : machines portées : **garder leur comportement** (choke et retrigger comme sur la machine) avec
l'option ANTI-CLICK de MoDule (64/128) proposée globalement ; ajouts (layer, vol de voix d'un layer polyphonique,
mute de piste, changement de machine à chaud) : **rampe de 64 échantillons par défaut** (coût négligeable : un gain
par voix, ~0,01 %) ; jamais de rampe à l'attaque d'un Hit (elle émousse la transitoire ; une attaque qui part de zéro
ne claque pas).

## 7. SIMD sur les voix : ce que coûte vraiment la qualité

`qbench` §7, AVX2, 8 voix par registre, même noyau que §2 :

| Noyau | Scalaire, par voix | AVX2 8 voix, par voix |
|---|---|---|
| tanh rationnel | 0,027 % | 0,0007 % |
| tanh 2× HIIR 8 coef. | 0,19 % | **0,012 %** |

Le facteur 16 vient des 8 voies et de la chaîne passe-tout dont la latence n'est plus sur le chemin critique. C'est
l'argument pour la SoA des chaînes de piste (R6 de performance-research.md) : sans elle, le 2× reste trop cher pour
être le défaut.

## 8. Dénormaux

Ce que ça corrige : un flottant qui décroît vers zéro (queue de reverb, filtre IIR après le silence, enveloppe
exponentielle) passe en sous-normal, que certains processeurs traitent par microcode (pénalité de dizaines à
centaines de cycles par opération sur Intel, Intel 64 and IA-32 Optimization Reference Manual [INFER : chiffre de la
littérature, pas mesuré]). Remède : FTZ/DAZ dans MXCSR au début du rendu (JUCE `ScopedNoDenormals`), ou un bruit
de −300 dB.

Mesuré (`qbench` §5, 16 pôles maintenus sous-normaux, z ≈ 2-4·10⁻⁴⁰) : 0,028-0,055 % sans FTZ contre 0,026-0,042 %
avec, **pas de pénalité nette sur ce Zen 2** (l'écart est dans le bruit d'une machine chargée).

Recommandation : **adopter FTZ/DAZ** au début de chaque bloc audio (coût nul, assurance pour les processeurs Intel et
pour les étages en flottant ; la carte le prévoit déjà). Les étages entiers portés n'en ont pas besoin.

## 9. Marge et gain staging sur 16 pistes et le master

Faits : 16 grosses caisses en phase sommées = +24 dB au-dessus d'une seule ; le bus flottant ne sature pas, la
sortie de l'hôte, si (au-delà de 0 dBFS, selon l'hôte). Les moteurs portés saturent à leur propre arithmétique (mixeur
du MD en 24 bits avec Dynamix, mix du Syntakt en virgule fixe) : c'est leur caractère et l'oracle.

Recommandation [INFER : pratique courante, pas de source primaire consultée] : chaque famille de machines sort à son
niveau propre, mesuré à l'oracle (pas de normalisation) ; le bus de l'instrument en flottant sans écrêtage interne ;
le master de l'instrument avec un limiteur doux **optionnel**, off par défaut (le MD a Dynamix) ; des crêtes affichées
(la Telemetry de MoDule le fait déjà) ; la somme des envois et des 16 pistes reste en flottant jusqu'à la sortie.
Aucun gain caché : un réglage neutre de la chaîne ajoutée (EQ à 0, drive off) doit nuller contre le moteur seul.

## 10. Indépendance à la fréquence de l'hôte

Décision existante : chaque moteur tourne à sa fréquence (MD 44,1 kHz, Syntakt 48 kHz), un rééchantillonnage unique
vers l'hôte. Les ajouts (filtres TPT prédéformés, HIIR, lissages en ms) se calculent à la fréquence du moteur ou de
l'hôte et ne dépendent pas d'elle si leurs coefficients sont dérivés de fs (§3).

**Trouvé en vérifiant le prototype polyphasé du ticket 01** (`protoBench.cpp`, 147/160, 16 coefficients par phase) :
son coût (1,2 % à 17 voies) reste valable, mais **sa formule de phase est inversée** (`n = (k − 8) + ph/147` au lieu
de `− ph/147`), ce qui en fait un mauvais filtre. Mesuré (`qbench` §8) :

| Ton (48 → 44,1 kHz) | Prototype 01 tel quel | Phase corrigée |
|---|---|---|
| 1 kHz | THD+N −22,4 dB | −73,3 dB |
| 10 kHz | −0,8 dB | −74,2 dB |
| 18 kHz | +10,2 dB | −68,0 dB, gain −0,89 dB |
| 20 kHz | +14,6 dB | −41,1 dB, gain −2,93 dB |
| 23 kHz (replié à 21,1 kHz) | −9,6 dB | −9,6 dB |

Corrigé, il passe bien la bande jusqu'à ~16 kHz, mais **ne rejette que 9,6 dB** de ce qui est entre 22,05 et 24 kHz
(replié en 20-22 kHz). Le contenu des Syntakt à 48 kHz y existe (cymbales, bruit, SRR). Recommandation : un
polyphasé plus long (32 coefficients) ou à coupure plus basse (~20 kHz), à mesurer [INFER : ×2 le coût, ~2,4 % à
17 voies ; en SIMD bien moins] ; seuils en §11.

## 11. Mesures de qualité à ajouter aux tests, seuils proposés

Tous les seuils sont des propositions [INFER] calibrées sur les mesures ci-dessus ; un test qui les mesure écrit sa
valeur, pas seulement passe/échoue.

| # | Test | Mesure | Seuil proposé |
|---|---|---|---|
| Q1 | Étage porté, exact | null test contre l'oracle, échantillon par échantillon | 0 LSB (comme `mdEngineFirmwareTest`) |
| Q2 | Étage porté réécrit en flottant (linéaire : mix, EQ du moteur, envois) | résidu RMS du null, rapporté à la pleine échelle ; erreur max | ≤ −100 dBFS RMS, ≤ −90 dBFS max (24 bits ≈ −144, donc marge) |
| Q3 | Étage porté réécrit (non linéaire : drive, filtre résonant, FM) | SNR contre l'oracle sur un Hit entier ; spectre du résidu par tiers d'octave | SNR ≥ 60 dB global, aucun tiers d'octave au-dessus de −60 dB relatif ; sinon écoute A/B documentée |
| Q4 | Voix entière réécrite | enveloppe (RMS sur 1 ms) contre l'oracle ; hauteur (pic du spectre) | écart ≤ 0,1 dB d'enveloppe sur la partie > −40 dB, hauteur ≤ 1 cent |
| Q5 | Chaîne ajoutée au neutre | null contre le moteur seul | ≤ −120 dBFS (bypass vrai) |
| Q6 | Drives maison, suréchantillonnage on | ASR, sinus 2,5 kHz × 8 (`qbench` §2) | 2× : ≤ −50 dB ; 4× : ≤ −80 dB ; off : non vérifié (caractère) |
| Q7 | Demi-bandes HIIR | aller-retour 1-20 kHz | gain ±0,01 dB, THD+N ≤ −120 dB |
| Q8 | Oscillateurs des modèles analogiques | ASR dent de scie / carré à 2,5 kHz | ≤ −25 dB (PolyBLEP) |
| Q9 | Filtres maison | gain à fc (TPT, Q 0,707) de 20 Hz à 20 kHz ; stabilité sous balayage de fc et Q | −3,01 ± 0,05 dB ; aucune sortie non finie |
| Q10 | Layer d'échantillon | THD+N sinus 6 kHz en sortie, ratios 0,75 et 1,5 | Hermite ≤ −35 dB ; sinc ≤ −70 dB |
| Q11 | Rééchantillonneur vers l'hôte | gain 20 Hz-16 kHz ; THD+N 1-16 kHz ; rejet de 22,05-24 kHz | ±0,1 dB ; ≤ −70 dB ; ≥ 60 dB de rejet (le prototype corrigé : 9,6 dB, échoue) |
| Q12 | Coupures ajoutées (layer, mute, vol de voix) | HF d'un 200 Hz pleine échelle coupé à sa crête (`qbench` §6) | ≤ −50 dB (rampe ≥ 64 échantillons) |
| Q13 | Repliement voulu préservé | null contre l'oracle sur SRR et BITS à fond (le flottant ne doit pas « nettoyer ») | Q1 ou Q2 selon l'étage |
| Q14 | Dénormaux | temps d'un bloc après 10 s de silence suivant un Hit fort, contre un bloc actif | ≤ 1,2× |
| Q15 | Indépendance à la fréquence | un Hit rendu à 44,1, 48, 96 kHz : enveloppe et spectre < 18 kHz | enveloppe ±0,2 dB, spectre ±0,5 dB par tiers d'octave |
| Q16 | Gain staging | crête des 16 pistes et du master, scénario « 16 BD en phase » | aucune valeur non finie ; crête rapportée, pas de clip interne caché |

Outils : la FFT et les métriques ASR/THD+N/HF de `qbench.cpp` sont réutilisables telles quelles dans un test
(dépendance zéro) ; balayage sinusoïdal et spectrogramme pour l'œil, pas pour le seuil.

## Inconnues

- Coût réel de chaque pratique dans la chaîne de l'instrument (ici noyaux isolés, scalaires sauf §7 ; machine
  chargée).
- Pondération perceptive (NMR de Kahles et al., A-pondération) : non mesurée, les ASR sont non pondérés.
- Pénalité des dénormaux sur Intel : non mesurée (pas de machine Intel).
- Latence ajoutée par HIIR 8 coef. au `HitDelay` commun : non mesurée [INFER : quelques échantillons].
- Rééchantillonneur à 32 coefficients : qualité et coût non mesurés.
- Seuils Q3-Q4 : à valider à l'écoute sur un premier étage réécrit (aucun n'existe encore).
