# Spécification v2 — Lockstep après la v1

Rédigée le 2026-11-16 à partir de la carte `../lockstep-post-v1/map.md` et de ses tickets résolus. Chaque décision vit
dans son ticket ; cette spécification les rassemble pour la construction et renvoie au ticket pour le détail. Elle
s'ajoute à la spécification v1 (`spec-v1.md`), qui tient partout où celle-ci ne dit rien. Là où elles diffèrent, le
ticket le plus récent fait foi.

Les tickets cités sont ceux de `../lockstep-post-v1/issues/`, par leur titre.

## 1. Objet

Les jalons qui suivent la v1 de Lockstep, dans cet ordre :

1. le DRIVE analogique du Syntakt ;
2. le Digitakt II ;
3. le Model:Cycles ;
4. la couche sample du Rytm (la couche elle-même se construit avec le DT2, section 7) ;
5. le compagnon Push.

Chaque jalon reprend le critère de fin de la v1 : jugé contre son oracle à une tolérance mesurée, sans bit-exact qui
coûte du CPU ; même son à 44,1 et à 48 kHz ; le pire kit sous 15 % d'un cœur ; garde-fou vert ; aucun firmware chez
l'utilisateur.

## 2. Règles communes

- **Exactitude :** chaque nouveau moteur suit la règle du Syntakt rapide : réécriture flottante en SIMD, 16 pistes
  sur les voies, à 48 kHz puis `fast::Host` vers l'hôte, jugée par les tests Q de
  `research/19-qualite-audio.md` et à l'oreille. Aucun émulateur au rendu.
- **Contenu :** le sampler joue les samples de l'utilisateur (le Locker), jamais le contenu d'usine d'Elektron. Rien
  du firmware dans le dépôt, sauf des constantes mesurées ; firmwares, oracles et vecteurs restent dans
  `C:\dev\elektron\elektron-work`.
- **Compatibilité :** tout nouveau type (drive, filtre, BAND, envoi) s'ajoute après les types existants ; aucun set
  ne change de son. Les migrations se font à la lecture.
- **Marque « ≈ » :** un modèle approché, sans machine pour le mesurer, reste affiché comme tel.

## 3. Budget CPU

Ticket « Répartition du budget CPU entre les jalons », corrigé par « Les effets du Digitakt II en envois ».

- Le plafond de 15 % se juge sur le **pire kit** : 16 pistes sur les machines les plus chères, en maximisant les
  machines distinctes (le coût suit les machines utilisées, pas les pistes) ; les 16 couches sample allumées ; chaque
  emplacement de chaîne au type le plus cher, ANALOG compris ; envois et master au plus cher. Machine calme,
  44,1 kHz, blocs de 128.
- Guides par bloc (somme 15 %) : moteurs ≤ 10 % ; couche sample ≤ 1,5 % ; chaîne ≤ 2,0 % ; envois ≤ 1,2 % ;
  master ≤ 0,3 %.
- Un ticket qui dépasse son guide le dit avec sa mesure, et passe si le pire kit tient. Un ticket qui fait passer le
  pire kit au-dessus de 15 % s'optimise avant son commit. Aucune dégradation automatique de qualité.
- Le banc `edDrumsWorstKitCost` joue le pire kit (minimum de 5 passes de 4 s), donne le pourcentage par bloc et au
  total, et entre au garde-fou comme test de temps.

## 4. J1 — DRIVE analogique du Syntakt

Tickets « Circuit de l'overdrive analogique du Syntakt » et « Analog FX du Syntakt dans Lockstep : place, modèle,
réglages ».

- Le Syntakt n'a pas d'overdrive de master : l'étage visé est le DRIVE de son bloc Analog FX. Lockstep ne prend pas le
  bus Analog FX (il recouvre les effets master existants) : ni son filtre, ni son AMP, ni le routage par piste.
- Le DRIVE analogique devient une **option ANALOG ≈** :
  - du type de drive « Syntakt drive », à côté de DIGITAL (l'OVER exact, qui reste le défaut) : mono, sur les voies
    SIMD, suréchantillonnage global ;
  - de l'étage de saturation du master, à côté de l'overdrive du DT2 (le défaut) : stéréo, un DRIVE pour les deux
    canaux.
  La page ne montre que les réglages du modèle choisi.
- Modèle approché, d'après la recherche : saturation douce qui dépend du niveau d'entrée ; passe-haut de couplage
  dont la coupure monte avec DRIVE ; compression ; bruit qui monte avec le gain.
- Tests mesurés : sortie monotone en DRIVE ; THD croissante avec le niveau ; courbe de compression ; grave à DRIVE 64
  et 127 ; bruit selon DRIVE. Coût estimé : 0,1 % d'un cœur par piste qui l'utilise, 0,2 % au master.

## 5. J2 — Digitakt II

Tickets « Le moteur sampler du Digitakt II : ce qu'il fait », « Place du Digitakt II dans Lockstep », « Plusieurs
samples par piste », « Oracle dynamique de la voix sampler du Digitakt II », « Les effets du Digitakt II en envois ».

### 5.1 Le lecteur DT2 du Sampler

- Pas de famille DT2. Le Sampler gagne un troisième lecteur, **DT2**, à côté de 12-BIT et VINTAGE, qui restent tels
  quels. GRAIN (Machine 9) devient un choix de trois ; un set existant migre à la lecture (0-63 : 12-BIT, 64-127 :
  VINTAGE).
- Avec le lecteur DT2, MODE choisit ONESHOT, GRID, REPITCH, WERP, SLICE ou STRETCH. Sémantique du DT2 : STRT + LEN,
  LOOP, PLAY à quatre modes, TUNE ±5 octaves. La page ne montre que les réglages du lecteur choisi.
- Mono (un sample stéréo est sommé, comme le Locker le fait), 48 kHz puis `fast::Host`.
- Lois mesurées à l'oracle : interpolation polyphasée à 6 prises et 256 phases, 64 échantillons à 96 kHz, puis
  décimation 2:1 récursive ; LEV = 2 × (LEV/128)² × courbe de vélocité, rampé sur le bloc ; hauteur
  2^(((TUNE − 64) + (note − 60))/12), bornée à ±64 demi-tons ; STRETCH rendu directement à 48 kHz, grains tous les
  2 048 échantillons avec un fondu de 128.
- Oracle : `elektron-work\tools\dt2\dt2_voice_oracle.py`, 21 vecteurs dans `vectors-dt2`. Coût de l'original :
  ~3 600 instructions SHARC par voix et par bloc en ONESHOT, ~1 800 en STRETCH.
- Restent à relever à l'oracle avant leur port : WERP, REPITCH, GRID, SLICE ; l'ordre des valeurs de PLAY ; le facteur
  de niveau de STRETCH.

### 5.2 Chaîne

- Les six filtres du DT2 deviennent des types de FILTER (spécification v1, section 4).
- BR et SRR du DT2 deviennent des types de drive, son base-width un type de BAND, après les types actuels.

### 5.3 Samples par pas

- Un **lock de sample** fait jouer à un pas un autre sample du Locker, sur les trois lecteurs du Sampler et sur la
  couche sample. Il nomme le sample par son empreinte : ni banque, ni numéro, ni limite. Il se pose en glissant un
  sample sur un pas, ou depuis le menu de lock du pas.
- Lockstep charge seul, hors du fil audio, les samples que les patterns d'une piste nomment, partagés par empreinte
  dans le processus. Le fil audio passe d'un sample chargé à un autre, sans allocation.
- Seuls les locks changent le sample, à leur trig : ni cible de LFO, ni paramètre hôte.
- Sample absent : le pas joue le sample de la piste ; le lock reste et rejoue dès que la même empreinte revient ; la
  ligne d'état nomme ce qui manque.
- Deux cibles sur une piste Sampler, SAMP machine et SAMP couche ; un glisser vise celle de la page affichée.
- Dans les lanes de Seq, un pas locké montre le nom court du sample.

### 5.4 Envois

- Chorus du DT2 : cinquième type de l'emplacement Chorus, après Bucket Brigade. Réglages DPTH, SPD, HPF, WDTH, VOL,
  sans synchro au tempo. Ses envois DEL et REV vont aux emplacements Delay et Reverb, quel que soit leur type, à 0 par
  défaut.
- Delay du DT2 : celui du Syntakt (« Oracle des effets d'envoi du Digitakt II » : écart −97 à −128 dB, deux
  constantes près). Pas de nouveau type : le type Syntakt gagne VOL ; un son DT2 importé prend aussi le retour × 0,5
  et la loi de FDBK du DT2.
- Reverb du DT2 : un autre algorithme que celle du Syntakt. Elle devient le type **DT2 reverb** de l'emplacement
  Reverb, après les types actuels ; réglages PRE, DEC, FREQ, GAIN, HPF, LPF, VOL.
- Jugés par l'oracle des envois (`elektron-work\tools\dt2\dt2_send_oracle.py`, 29 vecteurs dans
  `vectors-dt2\sends`) et les tests Q, l'oreille pour le reste. Coût de l'original par bloc de 32 : chorus ~14 400,
  reverb ~14 560 instructions SHARC.

## 6. J3 — Model:Cycles

Tickets « OS du Model:Cycles », « État réel de l'OS du Model:Cycles et ordre de ses machines », « Les machines du
Model:Cycles dans Lockstep ».

- **Famille Cycles**, la troisième à côté de Syntakt et MD : KICK, SNARE, METAL, PERC, TONE, CHORD (ordre confirmé dans
  l'OS 1.13).
- Réglages : PTCH, DEC, COLR, SHPE, SWEP, CONT dans Machine 1-6, PUNCH et GATE dans Machine 7-8 ; plages et défauts des
  fiches de paramètres de l'OS.
- Chaîne par défaut de la famille : le type de drive **Cycles dist** (l'étage VOL+DIST, VOL en gain d'entrée de DIST)
  en post ; pas de filtre par défaut.
- Vélocité : gain linéaire après la voix, comme sur le Cycles. VEL DEST gagne, sur une piste Cycles, les réglages de
  la machine comme cibles.
- GATE allumé : la voix tient tant que la note tient (note-off de l'hôte ou durée du trig). Les deux LFO de Lockstep ;
  le LFO du Cycles n'est pas porté.
- Port SIMD jugé contre les 108 vecteurs de `elektron-work\vectors-cycles`. Coût de l'original : 5 300 à 8 300
  instructions ColdFire par voix et par bloc ; estimé ≈ 0,1 % d'un cœur par piste active.
- Hors de ce jalon : l'import de sons et de kits Cycles.

## 7. Couche sample (J2) et couche du Rytm (J4)

Tickets « La couche sample du Rytm dans son firmware », « Couche du Rytm et layering du DT2 : une couche par piste ou
deux », « Où le Rytm rend sa couche sample ». La couche se construit au J2, juste après le lecteur DT2 qu'elle lit
(« Place du Digitakt II dans Lockstep ») ; le J4 n'y ajoute que ce qui vient du Rytm : l'import de ses sons.

- Une **couche sample** par piste, sur toute piste, machine Rytm comprise, lue par le lecteur DT2 en ONESHOT et ses
  quatre modes de PLAY. Sommée à la machine avant les drives pré, puis toute la chaîne.
- Elle garde les réglages du layering de la planification v1 : niveau, accord et fin, début, longueur, boucle, sens,
  enveloppe (attaque, maintien, déclin), décalage de 0 à 100 ms, suivi de note ; éteinte par défaut et sans coût ;
  retrig avec rampe anti-clic ; tout verrouillable et modulable.
- Elle a sa propre BR, sur le sample seul, avant LEV, à la **loi du Rytm** : pas de quantification continu, environ
  9,9 − 0,073 × BR bits équivalents, BR 0 contourné. Le type de drive BR garde la loi du DT2.
- Le Rytm rend sa couche sur son ColdFire, avec l'interpolation du DT2 : pas de lecteur Rytm à part. Un son Rytm
  entre par traduction à l'import : TUNE ±24 dans les ±5 octaves, STRT/END en STRT + LEN avec le sens, LEV au carré,
  fondu de fin.
- Sur une piste Sampler, deux samples jouent : celui du lecteur et celui de la couche (deux cibles de lock, section
  5.3).

## 8. J5 — Compagnon Push

Ticket « Compagnon Push ».

- Un device Max for Live posé devant Lockstep sur sa piste, pour Push 2 et Push 3 en mode contrôleur ; ni Push 1, ni
  Push 3 autonome.
- Pads : les 16 pas de la piste affichée sur deux rangées de 8 (trig posé ou retiré à la vélocité du pad), puis les
  16 pistes (sélection, mute). Un pas tenu arme un lock que les encodeurs règlent ; sans pas tenu, les encodeurs
  restent les relais, que le compagnon relaie lui-même. Mode LIVE et choix de pattern en second temps.
- Liaison : présentation par des notes MIDI, comme MoDule Seq, puis lecture et écriture des patterns par UDP local,
  Lockstep écoutant sur un port propre à l'instance. Le compagnon ne joue pas les notes : le séquenceur interne joue à
  l'échantillon près.
- Rien sur l'écran de Push : les pas se lisent par la couleur des pads, les réglages en mode Device. Live garde
  transport, automation, clips et arrangement.
- Armé, le compagnon prend `Button_Matrix`, `Track_Controls` et `Track_Select_Buttons`, et les lâche désarmé ; il suit
  `pad_layout` et repeint. Un seul compagnon arme Push à la fois.

## 9. Ordre de construction

Dans chaque jalon, oracle et vecteurs d'abord, puis le port, puis l'interface.

1. Banc `edDrumsWorstKitCost` au garde-fou (section 3), pour que chaque jalon mesure son coût.
2. J1 : option ANALOG ≈ du drive Syntakt, puis du master.
3. J2, dans l'ordre de « Place du Digitakt II dans Lockstep » : lecteur DT2 (ONESHOT, GRID, REPITCH) ; couche sample
   et sa BR à la loi du Rytm ; locks de sample ; filtres du DT2 ; BR, SRR, base-width ; WERP, SLICE et son éditeur,
   STRETCH ; VOL du delay Syntakt, chorus et reverb du DT2.
4. J3 : famille Cycles, Cycles dist, VEL DEST étendu.
5. J4 : import des sons Rytm dans la couche sample.
6. J5 : compagnon Push, puis l'écoute UDP de Lockstep.

## 10. Points ouverts

- Chorus et reverb du DT2 : où va le retour du chorus, lois de leurs réglages, longueurs du réseau de la reverb (à
  relever à l'oracle avant leur port).
- Les machines du DT2 non passées à l'oracle : WERP, REPITCH, GRID, SLICE ; la stéréo ; le lissage et les LFO du
  ColdFire.
- Un état démarré du Model:Cycles avec son Kit d'usine (lèverait les hypothèses de ses vecteurs) : demande de
  télécharger les sources d'Unicorn, donc l'accord de l'utilisateur.
- La différence de grain entre la BR du Rytm et celle du DT2, à confirmer à l'oreille.
