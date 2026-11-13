# Machines analogiques : Rytm et pistes 9-12 du Syntakt (J4)

Type: AFK puis HITL (écoutes)
Status: ready-for-agent
Assignee: 
Blocked by: 16

## Quoi

Deux familles nouvelles dans `edDrums` : les machines analogiques du Rytm MKII (OS 1.74 : grosses caisses, caisses
claires, rimshots, clap, toms, charleys, cymbales, cowbell, SY DUAL VCO, SY CHIP, SY RAW, bruit, impulsion) et celles
des pistes 9-12 du Syntakt. Décidé par l'utilisateur le 2026-11-12 : modèle maison, réglé contre le firmware.

## Comment

- **Contrôle exact.** Ce que le firmware calcule pour une voix (paramètres vers tensions de commande, enveloppes,
  hauteurs exponentielles, vélocité, lissage, fréquences des oscillateurs métalliques) est porté en C++ et comparé aux
  routines du firmware exécutées en émulateur, par vecteurs, comme les machines numériques du Syntakt. Les vecteurs et
  le firmware restent hors dépôt (`C:\dev\elektron\elektron-work\`).
- **Timbre maison.** Le firmware ne contient pas les circuits : chaque voix est un modèle de circuit classique écrit
  ici (ticket 14 et sa recherche : Werner pour les métaux), sans aucune donnée du firmware. Réglage final à l'oreille
  de l'utilisateur, par écoutes de contrôle.
- **Point de départ.** Un brouillon non relu existe hors dépôt (`elektron-work\gemini\edDrumsRytm.*`,
  `edDrumsSyntaktAnalog.*`, 36 machines Rytm) : son test vérifie seulement que chaque machine sonne et quelques sens
  de variation, rien contre le firmware ni contre un enregistrement. Le relire, garder ce qui tient, retirer de ce qui
  entre au dépôt toute donnée, adresse, mot ou nom tiré du firmware.
- **Noms.** Noms maison selon le ticket 27 : aucune marque, aucun nom de machine, des noms qui décrivent le son.

## Critère de fin

- Contrôle : chaque routine portée égale le firmware sur ses vecteurs.
- Chaque machine sonne, se fige au silence, et tient le pire kit sous le budget CPU de la spécification à 44,1 et
  48 kHz (brouillon : 3,6 % d'un cœur pour 16 voix, filtre et overdrive compris).
- Hits exacts, latence et gigue inchangées.
- Écoutes de contrôle envoyées et notes de l'utilisateur appliquées.
- Gate vert.

## État

Pas commencé. À lancer après le ticket 16, avant le ticket 18.

## État (2026-11-12)

Étape 1 (les machines des pistes 9 à 12 du Syntakt) est faite ; l'étape 2 (le Rytm) n'est pas commencée.

**Contrôle.** Les trente routines du CPU principal qui pilotent les voix analogiques, leurs dix aides communes et la
mise en place de la piste 12 sont portées en C++ (`source/elektron/edDrums/edDrumsAnalog*.cpp`, sans adresse, mot,
table ni nom de machine du firmware). Elles sont comparées aux routines originales exécutées en émulateur, par 31 971
vecteurs gardés hors du dépôt (`C:\dev\elektron\elektron-work\vectors-analog`, enregistreur `tools\analog\record.py`,
qui vérifie désormais que chaque instruction EMAC exécutée est émulée). Avec les mots de l'original remis dans les tables, chaque cas dont les valeurs de
paramètre restent dans 0-127 est identique, sur les trente machines (551 à 1 155 cas chacune) et la mise en place
(660 cas) ; les dix aides le sont sur 30 000 cas. Les tables sont par formule, exactes quand une formule existe (exp2,
gains, fréquences métalliques en Hz entiers), sinon ajustées : 1 à 5 pas de mantisse, deux courbes de décroissance de
caisse claire à 185 et 1 741 en Q31 ; le test vérifie chaque borne contre l'original. Avec les ajustements en place,
quelques cas diffèrent par machine (0 à 442), comptés et bornés par le test.

Les valeurs de 128 ou plus (verrous ou modulations hors plage) sont comptées à part : l'original y lit les tables
voisines, le port de la piste 12 borne la valeur à 0-127 (20 à 82 cas par machine métallique). Les quatre
enregistrements du firmware (clic du kick dur, corps du kick et de la caisse acoustiques, attaque de la caisse
naturelle) sont remplacés par des échantillons maison faits par formule, réglés sur des mesures seulement (longueur,
crête, centroïde, décroissance). Les 784 cas sur 895 où BODY SNARE diffère viennent de son corps maison seul ; les 89
de SNAP SNARE : 39 de l'attaque maison, 50 des ajustements.

**Timbre.** Chaque voix est un modèle de circuit écrit ici, qui joue ce que le contrôle calcule (tensions, commutateurs,
enveloppes, flux numériques) ; le bruit est le nôtre, sur les réglages du contrôle. La famille ANALOG joue dans
Lockstep sur les blocs du Machinedrum à 44,1 kHz, le contrôle cadencé à 32 échantillons de 48 kHz ; 30 machines aux
noms maison et huit commandes chacune (PHASE KICK n'a pas de huitième, CLICK HIT pas de 4 à 7 : la routine ne les lit
pas). Les oscillateurs libres de la piste 12 avancent d'un coup au réveil d'une voix figée.

**Mesuré.** 360 frappes passées par le moteur donnent les échantillons de la voix seule à partir de HitDelay (81),
gigue nulle ; chaque voix est relâchée une fois ses enveloppes finies et sa sortie sous -100 dB ; rien n'est alloué ;
le kit le plus cher (WARP SNARE à fond, CLICK RIM, BAND RIDE, BEAT HAT) coûte 4,3 % d'un cœur à 44,1 kHz, 4,6 % à 48,
4,8 % à 88,2, 5,0 % à 96.

**À écouter** (`C:\dev\elektron\elektron-work\control-listen\analog\analog-0.wav` à `analog-29.wav`, réglages par
défaut puis quatre variantes) : tout le réglage final, et en particulier le niveau de SIX CLOSED (crête 0,205 par
défaut). Les notes de l'utilisateur ne sont pas encore appliquées : le ticket reste ouvert.

**Après relecture (2026-11-12).** Les tables que le port construit à leur premier usage le sont désormais avec le moteur
(`analog::warmUp`, appelé par son constructeur), plus jamais sur le fil audio à la première frappe : edDrumsAnalogTest
compte qu'aucune ne se construit pendant les frappes des trente machines. Le tampon d'un tick de la voix est
dimensionné pour 192 kHz, au-delà la voix ne joue rien. Les pistes 9 à 11 jouent toutes la voix 0 : leurs routines ne
diffèrent que par les numéros de commutateurs et les mots de la trame, que VoiceIo range pareil pour les trois (les
vecteurs des trois passent sur le même code). edDrumsProcessorTest sauve et restaure un état avec trois pistes
analogiques et un verrou : mêmes échantillons, le verrou joué sur son pas. `analog-28.wav` (NOISE GEN, filtres
refaits) est rendu à nouveau.

**Timbres (écoute de l'utilisateur, 2026-11-12).** « Pas mal, mais certaines ont très peu de différences de timbre,
particulièrement les kicks. » Chaque machine des kicks, des caisses claires et caisses de rim, et du métal a désormais
une structure de circuit à elle, tirée de `.scratch/elektron-drums/research/28-circuits-classiques.md` : résonateur
frappé et VCO triangle à conformeur pour les kicks, deux sinus amortis contre deux VCO couplés et un passe-bas
résonant pour les caisses claires, résonateurs amortis et étage non linéaire pour les rims, et pour le métal trois
combinaisons (banc de six carrés par les passe-bande à 3,4 et 7,1 kHz avec le VCA à balancement, banc simplement
sommé, paires en anneau). Le contrôle et la carte des commandes vers les mots ne changent pas. edDrumsAnalogTest
mesure l'écart de timbre entre chaque paire d'une famille aux réglages par défaut (spectre par tiers d'octave et
enveloppe, en dB) et refuse toute paire sous une ligne fixe par famille : la moitié de sa médiane mesurée avant sa
reprise (kicks 3,75 dB, caisses claires et rims 12,7, métal 10,6, synthés 9,0), et non la moitié de la médiane
courante, qu'une reprise aurait pu faire baisser en rapprochant les paires éloignées. Médianes : kicks 7,5 puis
22,5 dB, caisses claires et rims 25,4 puis 28,4 dB, métal 21,2 puis 17,7 dB. Au métal, toutes les paires dépassent
10,6 dB (la plus proche, SIX CYMBAL et BAND RIDE, à 12,1) ; TUNED HAT a désormais son propre chemin, sans la bande à
7,1 kHz, et SIX CYMBAL les passe-haut par bande du 808. La médiane du métal baisse pourtant, et tout l'écart est dans
le spectre : SIX CLOSED, SIX OPEN, SIX CYMBAL et BAND RIDE partagent le banc de six carrés du 808 et ses passe-bande,
comme le circuit d'origine (note 28), ce qui les tient proches en spectre ; la ligne fixe par paire est la garde. WARP SNARE et BODY SNARE passent de peu (14,3 dB pour un
seuil de 14,2 à l'outil de mesure, 14,5 dans edDrumsAnalogTest) : leurs structures diffèrent (grain FM contre échantillon de corps et modes ; 4,2 dB de spectre et
10,1 dB d'enveloppe), mais le DECAY 48 de WARP SNARE et le corps maison décroissent tous deux en 35 ms environ. Aucun
réglage n'a été rapproché du seuil : le DECAY par défaut de WARP SNARE est à l'oreille de l'utilisateur, comme le
décalage d'enveloppe du VCA à balancement du métal et la montée d'environ 140 ms du ride. RING HAT a pris un DECAY
de 10, une longueur de charleston fermée.
