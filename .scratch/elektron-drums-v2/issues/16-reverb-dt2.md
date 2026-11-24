# Reverb du DT2

Type: AFK puis HITL (écoute)
Status: in-progress (port fait, écoute à venir)
Assignee:
Blocked by: 01

## Quoi

Type « DT2 reverb » de l'emplacement Reverb, après les types actuels ; réglages PRE, DEC, FREQ, GAIN, HPF, LPF,
VOL.

D'abord : relever à l'oracle les lois de ses réglages et les longueurs de son réseau.

## Critère de fin

- Jugé contre les vecteurs reverb de `vectors-dt2\sends` par les tests Q ; l'oreille pour le reste.
- Même son à 44,1 et à 48 kHz (tests Q de `research/19-qualite-audio.md`).
- Aucun set existant ne change de son.
- Coût mesuré sur machine calme et rapporté face à son guide (spécification v2, §3) ; le pire kit tient sous 15 %.
- Gate vert, nouveau test inscrit dans sa liste.

## Références

Spécification v2, §5.4 (`../elektron-drums/spec-v2.md`) ; ses tickets de décision dans `../lockstep-post-v1/issues/`.

## Relevés à l'oracle (2026-11-24)

Hors du dépôt, dans `elektron-work\dt2-reverb` : `reverb_model.py`, modèle float32 exact au bit près sur les 23
vecteurs `rev_*` et sur la reverb des `drv_imp_*`.

- Routine 0xb80fe0, par bloc de 32 à 48 kHz : pré-délai sur la moyenne des deux canaux, passe-bas puis passe-haut à
  un pôle, huit passe-tout en série, puis réseau de huit lignes. Les lignes 3 et 7 sont lues sur un oscillateur en
  quadrature à travers une interpolation de Lagrange cubique. Chaque ligne porte une étagère ; deux mélanges
  Householder 4×4 suivent.
- Longueurs : l'émulateur de l'oracle charge mal les mots longs de l'init 0xb8027c, d'où des longueurs fausses dans
  les vecteurs d'avant. Longueurs du firmware (choix de l'utilisateur) : passe-tout 53 73 113 163 251 367 491 809,
  lignes 2399 3359 4721 7004 2111 3109 4507 6704. Vecteurs `rev_fw_*` refaits avec elles.
- Entrée du bus du DT2 (`drv_imp_all`, exact) : piste et chorus × 0,5 (s/128)², délai × (DEL REV/128)².
  Retour : brut × (VOL/128)².
- Le chorus n'est pas touché par le défaut des mots longs : seule l'init des longueurs de reverb en fait.

## Port (2026-11-24)

- `Dt2Reverb` (`edDrumsDt2Reverb.*`), float32 dans l'ordre du DSP, compilé en `/fp:precise`.
- Tables du DT2 remplacées par 16 constantes ajustées (choix de l'utilisateur : formules, pas de tables du
  firmware). Lois : T60(DEC), échelle des passe-tout, K des passe-tout, pôles FREQ ; le fichier
  `reverb_formula.py` les décrit. Filtre d'entrée = base-width du DT2 (0x1ccbd8, même table) : sa loi
  `dt2BaseWidthPole`, déjà dans dépôt, remplace l'ajustement de l'agent (revue d'architecture).
- À 44,1 kHz (choix de l'utilisateur) : longueurs × 44,1/48, au premier le plus proche pour les statiques. Même
  période d'oscillateur, profondeur arrondie à l'échantillon. Étagère gardée au premier ordre (choix de
  l'utilisateur).
- Troisième type de l'emplacement Reverb, nommé Dense Verb par l'utilisateur. Sept réglages ajoutés en fin de liste
  (`reverb_dt2_*`), aux boutons de l'oracle (16 31 64 50 32 96, VOL 110).
- Retour × 0,5 (VOL/128)² : la moitié que le bus du DT2 donne aux envois de piste et au chorus, que les nôtres ne
  prennent pas. Le REV du délai Syntakt y entre au carré, comme dans Lush Verb.

Mesuré (`edDrumsDt2ReverbTest`, références de `dense_refs.py` hors dépôt) :

- Port contre modèle à formules, 36 cas à 48 et 44,1 kHz : identique (aucun écart).
- Formules contre tables du DT2, 16 cas à 48 kHz : tiers d'octave de la queue à 0,359 dB (borne 0,5), T60 par
  octave à 0,98 % (borne 1).
- Q15, 44,1 contre 48 kHz, 15 réglages, bornes mesurées prises par l'utilisateur :
  - T60 par octave à 2,24 % (borne 3), sur 10 s dès DEC 100 ;
  - fenêtres de 50 ms dès 200 ms à 1,38 dB (borne 2) ;
  - tiers d'octave jusqu'à 10 kHz à 1,47 dB (borne 1,5) ;
  - à 12,7 et 16 kHz : 2,14 dB (borne 2,5).
- Coût : 0,138 % d'un cœur à 44,1 kHz.

## Points ouverts

- L'écoute.
- Biais de +0,2 à +0,4 dB sous 2 kHz à 44,1 kHz, non expliqué (énergie totale égale).
- Deux bornes tiennent à 2 % de marge (T60 0,98/1, tiers d'octave 1,47/1,5) : test déterministe, mais un changement
  d'outils pourrait les franchir sans changer le son.
