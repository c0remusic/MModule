# Interface : page de piste et knobs en JUCE

Type: AFK
Status: ready-for-agent
Assignee: 
Blocked by: 11

## Quoi

La page de piste du mockup v12 en JUCE seul : barre du haut (onglets Kit editor et Sound browser, CPU), onglets de
piste avec play et LED, toutes les boxes de la piste à disposition fixe, couleurs de section de `v12-notes.md`.
Knobs dessinés dans `paint` à proportions fixes (52 et 64 px), valeur dans l'anneau, arc de modulation, lock en
accent, valeur jouée en repère ; menu Vélocité / Lock par clic sur le nom ou clic droit ; ASSIGN des LFO. Repeinte
limitée aux zones qui changent. Les boxes suivent les paramètres à mesure qu'ils existent.

## Critère de fin

- Capture de la page comparée boîte par boîte aux positions du mockup (sur le modèle de `mdDrumsSkinTest`).
- Une trame sans changement ne repeint rien ; coût de l'interface sur le fil audio : nul.

## Références

Spécification §10. Ticket « Interface façon Overbridge », `../elektron-drums/mockup/v12*`.

## État (2026-11-08)

L'éditeur JUCE suit la maquette Lockstep (spec section 10 amendée), pas le mockup v12 que ce ticket nomme encore :
`edDrumsPlugin/edDrumsEditor*`, plaques en ressources. Pages Sound, Seq Detail, Master, FX, Menu ; Kits et Browse inertes
là où rien n'existe. Écarts restants : valeurs en 0-127 et non en unités, ASSIGN, COLOR et CPU absents, police
Bahnschrift à la place de Barlow.

## État (2026-11-11)

Bloc Amp de la page Sound : les cellules de l'enveloppe (MODE, ATTACK, HOLD, DECAY, SUSTAIN, RELEASE) et sa courbe sont
grisées tant que AMP est sur OFF, ce qui est le cas des pistes Machinedrum, OPEN DRUM et SOPHIE dont la voix garde son
enveloppe ; grisées, elles ne se tirent plus, comme les drives éteints. Bloc Machine sur SOPHIE : 20 noms sur les trois
pages (MODEL à TAIL, BODY en 20), et l'image du bloc montre le son d'usine que tiennent Machine 1-20, ou EDITED ; un
clic ouvre la liste des 16 sons d'usine, dont le choix écrit Machine 1-20 comme des gestes. Rendus `sound-sophie.png`
et `sound-sophie-p3.png` de `edDrumsEditorTest`. Les valeurs de SOPHIE restent en 0-127, sans unités.

## État (2026-11-12)

La page Sound suit la variante 2 « Synthèse » de `u.html` (note `concepts/u-html-synthese-v19.md`), ajustée à la grille
de l'éditeur : trois groupes nommés SOURCE, TIMBRE et AMP & MOD sur 9, 8 et 7 des 24 colonnes, blocs à partir de y 30,
5 mm entre deux blocs d'une colonne, rangée du bas alignée à y 185. VOICE (l'ancien bloc Machine) a le rang 1 (titre en
4,0 mm, filet plein), EQ et SENDS le rang 2 (titre en 2,8 mm gris). NOTE et CHOKES quittent la ligne de titre de l'écran
pour le bloc VOICE. Les listes qui commandent un bloc passent sur sa ligne de titre : TYPE et PLACE des drives et de
l'EQ, TYPE du filtre, MODE de l'ampli, OUT des sends. Les drives n'ont plus de sinusoïde. Le filtre a une sous-section
ENVELOPE (ENV RETRIG, ENV AMT à gauche de sa courbe, puis DELAY à RELEASE). SAMPLE LAYER tient tout sur une page, sans
onglets LAYER / OPTIONS, MIX M/S en cellule de la deuxième rangée. Les onglets LFO 1 / LFO 2 sont remplacés par deux
cartes sous la courbe, qui choisissent le LFO montré et portent un point quand il vise une cible avec une profondeur ;
les dix cellules de chaque LFO restent sur la page. Les libellés VEL TARGET et VEL AMT sont gris comme les autres. Les
temps de l'ampli s'affichent selon MODE (AHD ou ADSR), les autres cachés sans être retirés. Chaque paramètre garde une
commande : couverture inchangée (75, 75 et 91 paramètres de piste, FX 61, Master 10).

Écarts assumés avec la maquette : l'image de VOICE garde ses 84 mm mais n'a pas d'oscilloscope (aucun moteur ne fournit
l'aperçu) ; pas d'ASSIGN ni de COLOR ; chaque send montre sa valeur sous sa barre en petit texte. TYPE d'un drive et
TARGET d'un LFO restent actifs quand le bloc est éteint, puisque ce sont eux qui l'allument : avant, on ne pouvait plus
les changer une fois sur OFF. Rendus `sound*.png` et `sound-1280.png` de `edDrumsEditorTest`. Le coût d'un
rafraîchissement complet de Sound passe de 10,2 ms à 8,4 ms.

## État (2026-11-13)

Valeurs en unités. Le texte d'un paramètre vient d'un seul endroit, le texte d'hôte de chaque paramètre JUCE
(`parameters::valueText`, `machineText`, `rateText` dans `edDrumsParameters.cpp`), que l'éditeur, les relais pour Push
et l'hôte partagent. Note en nom de note (C4 = 60), accent en dB, profondeurs de vélocité et de LFO en % signés,
fréquences libres des LFO et du delay en Hz (OFF à 0). Machine 1-24 suivent la machine de la piste : TUNE du Syntakt et
des kicks ANALOG en demi-tons depuis 64, TUNE de VA DRUMS en nom de note et FINE en cents, HYPER FM par ses propres lois
(coupure en Hz), le Sampler comme avant. Les machines du Machinedrum restent en 0-127, comme sur la machine : chaque loi
est dans le code de sa voix. `edDrumsProcessorTest` (units) compare un échantillon de ces textes au code qui joue la
valeur : la note, l'accent, les profondeurs, les fréquences libres, et au moins une commande par famille.

Limites : le TUNE du Syntakt est relatif à la note, et à la note 60 BD MODERN joue tout TUNE sous 28 comme 28 (la hauteur
est bornée à la note 0) : le texte n'est exact qu'au-dessus. VA DRUMS montre la note sans ses Hz, que la spécification
demande aussi. HYPER FM garde les noms de modèles de l'original (FUSE…), que le test des marques laisse passer.

CPU dans la barre du haut, en dernier comme dans la maquette : la part du temps d'un bloc que prend `processBlock`,
mesurée au compteur du processeur sur le fil audio, sans verrou ni allocation, lissée sur 0,3 s ; repère à 15 %, ambre
au-dessus, rouge au-dessus de 25 ; repeinte seulement quand le pourcentage entier change. `edDrumsEditorTest` vérifie
qu'une trame sans rien de neuf ne repeint rien sur les six pages, et une lecture du CPU entre 0 et 1 après 400 blocs.

Restent : ASSIGN, COLOR, la police Barlow (téléchargement non donné).
