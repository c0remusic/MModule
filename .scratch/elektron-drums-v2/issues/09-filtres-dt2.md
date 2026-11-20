# Les six filtres du DT2 en types de FILTER

Type: AFK
Status: done
Assignee:
Blocked by: 01

## Quoi

Les six filtres du DT2 deviennent des types de FILTER, après les types actuels (spécification v1, section 4).
Relevés à l'oracle s'il manque des vecteurs. La page ne montre que les réglages du type choisi.

## Critère de fin

- Chaque filtre jugé contre ses vecteurs par les tests Q (réponse et résonance).
- Même son à 44,1 et à 48 kHz (tests Q de `research/19-qualite-audio.md`).
- Aucun set existant ne change de son.
- Coût mesuré sur machine calme et rapporté face à son guide (spécification v2, §3) ; le pire kit tient sous 15 %.
- Gate vert, nouveau test inscrit dans sa liste.

## Fait (2026-11-20)

**Sources.** Lois de `vectors-dt2\FILTERS.md` et modèles Python `tools\dt2\filters\` (elektron-work), relevés sur le
code du DSP par `dt2_filter_oracle.py` ; 153 vecteurs `flt_*`. Les tables du firmware n'entrent pas dans le dépôt :
ce sont des formules, arrondies comme le firmware arrondit les siennes (7 décimales, le cosinus de LEGACY à 5).

**Types.** Six types de FILTER ajoutés après les existants (`FilterType::Dt2Multimode` à `Dt2CombPlus`) : aucun set
existant ne change de son. Noms choisis par l'utilisateur : « State var », « Equalizer », « Lowpass 4 », « Comb- »,
« Legacy LP/HP », « Comb+ » (aucune marque, `edDrumsEditorTest` passe). La page ne montre que les réglages du type :
FREQ (PITCH pour les peignes, CUTOFF pour LEGACY), RESO / GAIN / FEEDBACK, et la quatrième case partagée entre
MORPH, LPF, WIDTH ou le mode de LEGACY (LP, HP, DRY). Lowpass 4 n'a que deux réglages.

**Décisions.**
- `div` et `sqrt` des setters reproduits comme le DSP les calcule (graine masquée puis pas de Newton en float), choix de
  l'utilisateur : sans dépendance, le moins d'écart. Le fichier se compile en `/fp:precise` (`-ffp-contract=off`).
- Chaque filtre lance son setter par bloc de 32 échantillons comme le DT2, ses mots rampent sur le bloc ; un Hit remet
  l'état à zéro et les rampes repartent de leur propre valeur. Le setter ne tourne que si un réglage a changé.
- Le suivi de clavier est linéaire (100 = un demi-ton par demi-ton) et s'applique aussi aux peignes ; l'enveloppe
  déplace FREQ comme sur le MD.
- À 44,1 kHz, MULTI-MODE, LOWPASS 4 et LEGACY calculent leur filtre à 48 kHz puis le transportent : pôles au même
  endroit (bilinéaire à même coupure pour LOWPASS 4), zéros ajustés en module seul, du continu à 18 kHz, en erreur
  relative (ajuster aussi la phase donnait jusqu'à 34 dB d'écart). LEGACY y tourne en SVF, ce qui règle aussi sa
  précision float32 aux coupures basses. LOWPASS 4 suit l'horloge des blocs 48 kHz du DT2 pour son lissage.
  EQUALIZER et les peignes gardent leurs fréquences en hertz.

**Mesures** (`edDrumsDt2FiltersTest`, inscrit au gate, DT2_VECTORS) :
- Q3 contre l'OS, SNR / pire fenêtre de 10 ms : peignes 99,1 / 86,0 dB, EQUALIZER 108,4 / 88,6, LEGACY 89,7 / 87,7,
  LOWPASS 4 90,8 / 74,0, MULTI-MODE 87,2 / 87,2, changements de type 111,9 / 107,8. Seuils 70 et 60 dB. 152 mots de
  coupure sur 153 égaux à ceux de l'OS.
- 16 voies jouent, bit pour bit, ce que joue chaque voie seule, sans toucher les autres, à travers des Hits et des
  changements de type. La revue de code a trouvé que LEGACY ne remettait le mix à zéro au changement de type que sur la
  voie 0 (priorité de `>>` sur `&`) ; ce test le voit désormais.
- Q15, 44,1 contre 48 kHz, 16 réglages par filtre, enveloppe 10 ms / tiers d'octave jusqu'à 16 kHz : EQUALIZER
  0,08 / 0,48 dB et LEGACY 0,07 / 0,25 passent les seuils 0,2 / 0,5. L'utilisateur a pris les bornes mesurées pour
  les autres : MULTI-MODE 0,54 / 0,67 (une seule voie, coupure au sommet, 22 kHz), LOWPASS 4 0,30 / 0,66 (résonance
  forte, près de 16 kHz), COMB- 0,34 / 1,11, COMB+ 0,27 / 1,56. Le test borne à 0,55 / 0,7, 0,3 / 0,7, 0,35 / 1,15
  et 0,3 / 1,6.
- Coût sur 16 voies à 44,1 kHz, réglages fixes / FREQ modulée à chaque bloc : MULTI-MODE 0,10 / 0,62 % d'un cœur,
  EQUALIZER 0,10 / 0,21, LOWPASS 4 0,51 / 1,47, COMB- 0,33 / 0,47, LEGACY 0,13 / 0,83, COMB+ 0,33 / 0,48.
- Pire kit : il prend désormais LOWPASS 4. Chaîne 4,13 % (guide 2 %, déjà dépassé avant ce ticket à 3,35 %), total
  11,65 %, sous 15 %.

**Points ouverts.**
- Peignes à 44,1 kHz : l'interpolation linéaire de la boucle tombe à une autre fraction qu'à 48 kHz. Corriger le
  passe-bas de boucle déplaçait les dents (jusqu'à 12 dB d'écart dans les graves à fort retour) : laissé.
- Suivi de clavier mesuré au seul MULTI-MODE à kt 100 ; sa loi sur les autres types et valeurs reste à vérifier.
- Aucune valeur par défaut au changement de type : State var garde WIDTH, dont 127 vaut un passe-haut.
- La chaîne dépasse son guide de 2 % ; à reprendre au ticket 27 (passe d'optimisation).

## Références

Spécification v2, §5.2 (`../elektron-drums/spec-v2.md`) ; ses tickets de décision dans `../lockstep-post-v1/issues/`.
