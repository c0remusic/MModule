# Spécification v1 — instrument de batterie Elektron (VST3)

Rédigée le 2026-10-25 à partir de la carte `map.md` et de ses 24 tickets résolus. Chaque décision vit dans son
ticket ; cette spécification les rassemble pour la construction et renvoie au ticket pour le détail. Là où elle
s'écarte d'un ticket, le ticket amendé le plus tard fait foi.

## 1. Objet

Un seul plug-in VST3 de batterie, rien d'autre que des batteries, dont l'interface est l'instrument, façon
Overbridge. 16 pistes, chacune une voix mono d'une famille de machines Elektron, avec sa chaîne d'effets, deux LFO,
des envois partagés, un master, un séquenceur interne et des kits. Il tourne sans aucun firmware chez l'utilisateur
et coûte au plus 15 % d'un cœur au pire cas, 16 pistes sonnant ensemble.

Point de départ dans le code : ST Drums (`source/elektron/syntakt/stDrums`, `stDrumsPlugin`, moteur
`stEngine`). MoDule reste un plug-in à part ; son `mdEngine` sert d'oracle au port du Machinedrum.

## 2. Périmètre

### Dans la v1

- Familles de voix : les 12 machines numériques du Syntakt (réécriture rapide) et les machines du Machinedrum
  (port C++), mélangées librement sur les 16 pistes.
- Chaîne de piste à emplacements (section 4), avec les types Syntakt, MD, l'EQ 3 bandes ajouté et l'overdrive du
  DT2.
- Envois : delay et reverb du Syntakt, Echo et Gate Box du MD, chorus et phaser/flanger maison (section 5).
- Master : compresseur et overdrive du Digitakt II, portés en avance (section 6).
- Séquenceur interne (section 7), kits, état, sons de piste, samples et explorateur (section 8).
- Paramètres hôte, relais Push et 16 sorties (section 9).
- Interface complète en JUCE, d'après la maquette Lockstep approuvée (section 10).

### Après la v1 (ticket « Ordre des jalons après la v1 »)

- J2 Digitakt II : moteur sampler, layering d'un sample dans la piste (ticket « Layering »), 6 filtres, SRR, BR,
  base-width, delay, reverb et chorus du DT2.
- J3 Model:Cycles. J4 machines analogiques du Syntakt et du Rytm, overdrive analogique du Syntakt au master. J5
  couche sample du Rytm.
- Plus tard : compagnon Push (Max for Live), euclidien et aléatoire.

L'interface v1 réserve déjà la place de ces jalons (box SAMPLE, types de filtre DT2) ; un élément sans moteur est
caché ou grisé, jamais factice.

### Hors périmètre

Les synthés (Digitone FM TONE, WAVETONE, SWARMER, Analog Four, Monomachine) et le FM DRUM du Digitone II. Pas de mode
song (l'hôte arrange), pas d'horloge libre, pas d'enregistrement de notes hors du mode LIVE.

## 3. Moteurs de voix

Règle commune (ticket « Source de machine d'une piste ») :

- Une voix mono par piste ; un trig redémarre la voix ; pas de polyphonie dans une piste.
- Une voix éteinte (enveloppe finie, sous -100 dB) n'est pas rendue et ne coûte rien. Elle gèle : son Hit suivant
  repart de l'état figé, et diffère de celui d'un moteur qui l'aurait rendue (amendé le 2026-11-06, ticket 11 :
  seulement après 5,9 s de silence sur le MD, chiffres corrigés dans le ticket, rien d'entendu à l'aveugle).
- Aucun coût fixe par famille : les routines d'OS d'une famille (conversion des paramètres, LFO, lissage du MD)
  tournent par piste, pour les seules pistes qui la jouent.
- Changer de machine : rampe de 64 échantillons sur la voix, paramètres de machine aux défauts de la nouvelle,
  locks des paramètres de l'ancienne retirés (annulable) ; chaîne, ampli, LFO, envois et autres locks gardés.
- Chokes par cible : une piste nomme la ou les pistes qu'elle coupe, toutes familles ; aucune par défaut.
- Un LFO vise sa piste par défaut et peut viser un paramètre d'une autre piste.

### 3.1 Syntakt

Réécriture rapide (flottant, SIMD, paramétrée par la fréquence) des 12 machines numériques, vérifiée à tolérance
contre le port exact en entiers, qui reste dans le dépôt comme référence (`stEngine`). Cible : de 13,7 % mesurés à
~10 % pour 16 pistes au plus (ticket « Budget CPU »). Tables régénérées par formule, chacune testée contre le
firmware (ticket « Instrument sans firmware ») ; une table sans formule bloque sa machine ou la rend approximative
et signalée.

### 3.2 Machinedrum

Port C++ des renders du DSP2 de l'OS 1.63 (39 renders à porter sur 44, qui couvrent toutes les machines hors
INP et RAM-R), des 55 fonctions de coefficients 68k
et du lissage, vérifié contre `mdEngine` (ticket « Port du Machinedrum en C++ »). Ordre : harnais, ROM ou TRX, EFM,
E12, P-I, GND. Voix en entiers bit-exacts, `TrackFx` en SIMD (doubles sur les mots du DSP, le flottant ratant Q3 sur les
bandes faibles), Echo et Gate Box réécrits, la boucle de l'Echo en entiers (ticket 10 de la construction, 2026-11-05). Voix brute avant
le mixeur : la vélocité, LEV et VOL s'appliquent dans l'étage AMP. L'AMD n'est pas reprise. Les machines ROM jouent
la banque UW importée par le propriétaire, ou les samples maison, ou le silence.

## 4. Chaîne de piste

Ticket « Chaîne de piste : emplacements, ordre, routage », amendé par « Effets master ».

Squelette fixe, mono jusqu'au pan :

    machine + couche sample → drives pré → BAND (avant) → FILTER → BAND (après) → drives post → AMP → envois

BAND après le filtre se place juste après lui, avant les drives post (décidé le 2026-10-25, ticket « Chaîne de
piste »). La couche sample (J2) entre après la machine et avant les drives pré : elle est mélangée à la voix et passe
par toute la chaîne (amendé le 2026-11-07, la disposition de l'interface suit cet ordre).

- **Drives** : deux emplacements, chacun pré ou post filtre. Types : Off, Syntakt drive, MD DIST, MD SRR, DT2
  Overdrive (en v1 avec le master), DT2 SRR (J2). Chaque type garde ses réglages d'origine, plus un MIX sec/traité
  « + », à 100 % par défaut. Suréchantillonnage global off / 2x / 4x, 2x par défaut (HIIR, SIMD sur les voix).
- **BAND** : BASE/WIDTH du Syntakt, EQ du MD, EQ 3 bandes « + » (plateau grave, cloche médium à Q fixe, plateau
  aigu ; fréquence et gain par bande, ±18 dB), base-width du DT2 (J2). Bouton avant/après le filtre, avant par
  défaut.
- **FILTER** : 8 types du multimode Syntakt et le filtre du MD en v1, les 6 filtres du DT2 à J2. Enveloppe de filtre
  du Syntakt (délai, ADSR, profondeur, suivi de clavier) commune à tous les types ; sur le filtre MD elle déplace toute
  la bande, profondeur 0 par défaut, et le suivi de clavier est caché.
- **AMP** : enveloppe AHD ou ADSR, niveau, pan, accent. Pour le MD l'enveloppe reste dans la voix ; l'étage garde
  niveau, pan, accent.
- **Vélocité** : sur le niveau par défaut, plus une destination assignable dans le bloc AMP (VEL DEST), avec
  profondeur (VEL AMT).
- **LFO** : deux par piste, SYNC (division musicale de 1/64 à 16 mesures, pointée, triolet ; phase sur la position
  du transport, sauf en mode de trig TRIG où elle redémarre au trig) ou HZ ; SYNC par défaut. La vitesse et le
  multiplicateur des LFO Elektron se traduisent en division musicale.
- **Chaîne par défaut** de la famille au premier choix d'une machine (Syntakt : drive pré, BASE/WIDTH, filtre
  Syntakt ; MD : EQ MD, filtre MD, SRR puis DIST post). Changer de machine dans la famille ne touche pas la chaîne ;
  changer de famille la recharge, en demandant si elle a été modifiée.
- Types de filtre et de drive automatisables, avec rampe anti-clic (amendé le 2026-11-06, ticket 12 : un bloc de
  chauffe inaudible puis 64 échantillons de fondu ; le fondu du firmware, 32, reste celui d'un morph 2-6).
- La chaîne tourne avant les anneaux, niveau, vélocité et fondu après elle (ticket 12). Un drive suréchantillonné sur
  piste Syntakt décale ses Hits d'une valeur fixe, gigue 0 (chiffres dans le ticket).
- Une piste dont la chaîne reste celle de sa famille reste fidèle à son firmware ; tout ajout est marqué « + ».
- La pente d'un LFO se limite à sa cible : 24 blocs sur le filtre MD, 8 sur les autres valeurs de chaîne, 4 sur niveau, pan et SYN
  (ticket 13). Un Hit règle ce qui vise sa piste.

## 5. Effets d'envoi

Ticket « Sources des effets d'envoi ».

- Un exemplaire de chaque effet, partagé par les 16 pistes, retour stéréo, en parallèle, sauf l'envoi du delay vers
  la reverb du Syntakt, gardé.
- Delay et reverb : sélecteur de source par effet ; Syntakt (exacts) et, avec le port du MD, Echo et Gate Box.
- Chorus et phaser/flanger (un effet à deux modes) : maison en flottant, réglés à l'oreille contre le Virus TI
  d'OsTIrus, marqués « + ».
- Chaque retour sort sur Main ou sur une sortie choisie.

## 6. Master

Ticket « Effets master ». Compresseur `0xb82d41` puis saturateur `0x1cb3d8` du Digitakt II, puis écrêtage et rampe
de gain, dans l'ordre du firmware, réglages du firmware (sidechain compris s'il existe), vumètre de réduction de gain.
Premier morceau du port du DT2 : le harnais d'oracle sharcvm/digikit se monte d'abord. Le master agit sur Main
seulement.

## 7. Séquenceur interne

Ticket « Séquenceur interne », sur « Fonctions de MoDule Seq » et « Séquenceurs des machines Elektron ».

- 128 patterns (8 banques de 16), 64 pas au plus par piste, longueur et échelle par piste ; une longueur commune
  fixe le changement de pattern.
- Un pas porte : trig, note, vélocité, durée, condition (65), micro-timing ±23/24 de pas, ratchet (1-8, vitesse,
  courbe de vélocité), accent, slide, locks sur n'importe quel paramètre de la piste, sans plafond. Un pas peut
  aussi porter des locks sans trig (lock sans note). Retirer un trig retire ses locks ; un lock sans note qui n'a
  plus de lock disparaît.
- Par pattern : swing 50-80 %, accent, slide ; mutes, fill tenu, chaînes de patterns dont chaque entrée peut jouer en
  fill. Le slide fait glisser les
  paramètres verrouillés vers les valeurs du pas suivant.
- Conditions aléatoires tirées d'une graine par passage : un rendu rejoue ce qu'on a entendu.
- Transport de l'hôte seulement. Pattern choisi par le paramètre hôte « Pattern » ou par program change ; un pattern
  demandé part à la fin du pattern en cours.
- Chaînes (décidé le 2026-11-07) : une suite d'entrées, chacune un pattern et un nombre de répétitions. La position dans
  la chaîne se déduit de la position de lecture de l'hôte, de sorte qu'un arrêt puis un départ rejouent le même endroit ;
  la chaîne boucle à sa fin ; un pattern demandé par le paramètre « Pattern » ou un program change remplace la chaîne à la
  fin du pattern en cours. La chaîne est gardée dans l'état du projet, pas dans le kit.
- Trigs à l'échantillon près, sans gigue ; enveloppes et LFO avancent au bloc.
- MIDI entrant : notes 36-51 jouent les pistes 1-16 avec vélocité ; en mode LIVE, notes et mouvements de knobs
  s'enregistrent sur les pas.
- Édition : copier/coller de pas, pistes et patterns, effacer, décaler une piste. Export d'un pattern en fichier MIDI
  par glisser-déposer (notes 36-51, sans les locks).

## 8. Kits, état, samples

Ticket « Format des kits et de l'état ».

- Un kit garde tout le son : par piste machine et famille, paramètres, chaîne, ampli, LFO et cibles, envois, sortie,
  chokes ; plus les effets d'envoi et le master. Rien du séquenceur.
- Un pattern garde trigs et locks, plus un kit à charger à son départ ou « garder le kit en cours » (défaut).
- Banque de 64 kits dans le dossier de données, partagée entre instances, relue avant chaque écriture (comme
  MoDule). Patterns dans le projet de l'hôte, export et import en fichier.
- État : copie du kit joué et son emplacement, 128 patterns, réglages globaux (suréchantillonnage, mode 96 kHz),
  copie des samples utilisés.
- Format versionné et compressé, valeurs rangées par identifiant de paramètre figé ; un paramètre inconnu prend son
  défaut.
- Sons de piste (machine, chaîne, ampli, LFO) en bibliothèque partagée, chargeables sur n'importe quelle piste.
- Contenu d'usine maison : ~16 kits et quelques dizaines de sons ; rien d'Elektron n'est livré.
- Samples de l'utilisateur (WAV, AIFF, FLAC) dans le dossier de données. Contenu Elektron du propriétaire (banque UW
  lue dans sa flash image, packs d'usine, exports Transfer) importé par le bouton « Importer mon contenu Elektron »
  ou un dossier glissé, reconnu par empreinte ; l'import dit ce qu'il a trouvé et ce qui manque. Un kit référence
  ses samples par empreinte et nom.
- Explorateur (Sound Browser) en trois colonnes : arbre (Kits, Sons de piste, Samples par source ou par type), liste
  avec recherche et filtres, aperçu. Clic écoute, double-clic charge, glisser sur un onglet de piste ou un
  emplacement de kit. En v1, un sample se joue sur l'entrée Sampler (section 10) ; la box SAMPLE (layering) reste grisée
  jusqu'à J2. Types de samples devinés par le nom puis par une analyse simple, corrigeables.

## 9. Paramètres hôte et sorties

Ticket « Paramètres hôte et sorties ».

- Tout réglage de piste, d'effet et du master est automatisable : ~75 par piste, ~1 260 au total, identifiants
  figés (ajouter, jamais renuméroter).
- Paramètres de machine génériques « T01 Machine 1 » à « 8 », valeur affichée avec le texte de la machine.
- Machine et famille non automatisables.
- 64 relais vers la piste affichée, 8 pages de 8 qui suivent les boxes (Machine, Drive, Band, Filter, Amp, LFO 1,
  LFO 2, Sends), sur le modèle de MoDule.
- Sorties : Main et 15 sorties stéréo ; chaque piste sur Main ou l'une des 15. Une piste séparée sort avec sa
  chaîne, sans les envois, et alimente quand même les envois.

## 10. Interface

Ticket « Interface façon Overbridge », amendé le 2026-11-07. Référence : la maquette Lockstep approuvée par l'utilisateur
ce jour-là, `concepts/t.html`, avec `concepts/t-ui-spec.md`, `concepts/t-style-guide.md` et `concepts/HANDOFF.md`, qui
font foi pour les positions, les couleurs et les gestes. Le mockup v12 ne reste la référence que des teintes par section.

- JUCE seul, en C++ : composants dessinés dans `paint`, grille fixe mise à l'échelle, repeinte limitée aux zones qui
  changent. Pas de RmlUi, de WebView ni de JavaScript. Le matériel vient de plaques rendues, découpées avec `drawImage`.
- Une unité matérielle : écran, sept touches à droite (Sound, Seq, Master, FX, Kits, Browse, Menu), Page et Follow, puis
  16 pistes sous l'écran, chacune avec LCD, pad, pas et bande de lock. Aucun knob, tout se règle à l'écran à la souris.
- Souris seule : ni clic droit, ni modificateur. Un clic sélectionne, pose ou ouvre ; un glissé règle.
- Pages : Sound (une page par piste sans sous-pages, un bloc par section, l'ordre des blocs dit l'ordre du signal), Seq
  (Detail, Overview, Patterns), Master (pistes, retours, Main et master), FX, Kits, Browse. Menu n'est pas une page :
  un panneau sur la page en cours, taille de fenêtre, suréchantillonnage, 96 kHz natif, import du contenu Elektron.
- Couleur saturée par section (table des teintes de `v12-notes.md`) ; ivoire réservé à l'état global, rouge au lock.
- Cellule de valeur : nom, valeur, curseur fin. Une valeur se règle en glissant de gauche à droite ; les formes, faders
  et barres gardent leur axe. Une liste de trois choix et plus ouvre un sélecteur, une liste de deux bascule.
- Chaque forme se règle par ses poignées : enveloppes, courbe du filtre, Band, Drive, LFO. Une forme tenue suit la souris
  sans lissage.
- Pas : en Seq Detail, modes VEL, ACCENT, SLIDE. Clic pose ou retire un trig, à la vélocité maximale ; glisser
  horizontal peint, vertical règle la vélocité. Lignes de locks choisies avec +TRIG dans les paramètres verrouillables,
  champs du pas (note, durée, condition, micro, ratchet, vitesse, courbe) en bas de page.
- Lock : un clic sur la bande sous un pas le met en édition, tout contrôle bougé écrit alors un lock sur les pas choisis.
  Les noms verrouillables passent en rouge clair, un lock posé en rouge. Un lock sur un pas sans trig est le lock sans note
  de la section 7.
- LFO assignés par le bouton ASSIGN de leur bloc seulement, les cibles teintées à sa couleur. Sur un curseur, un point
  suit la valeur jouée pendant la lecture.
- Choix de machine : une seule entrée Sampler, avec un réglage de grain, au lieu de 48 ROM. Grains : MD (12 bits, le lecteur
  ROM) et Digitakt II ; E12 à l'essai. ROM-01 à 48 ne paraissent plus dans l'explorateur, ils restent seulement pour relire un
  kit Machinedrum et pour la banque d'une image. Ce que le kit garde par piste et combien de samples une piste lit :
  ouverts, ticket 20.
- Une couleur par piste au choix, pour s'y retrouver : la lumière de sa touche, barre LED et halo.
- Longueur commune réglable en pas (1 à 512), sur Overview et Detail : chaque piste boucle sur sa longueur, tout repart à la fin.
- CPU en haut à droite, après le BPM, 0-100 % d'un cœur, repère à 15 %, ambre au-dessus, rouge au-dessus de 25 %.
- Toute machine qu'on accorde affiche la note de sa fondamentale en plus de sa fréquence, avec l'écart en cents.
- Aucun texte redondant : l'état se lit sur le contrôle, un badge ou une infobulle.

## 11. Exigences non fonctionnelles

### 11.1 Performance

- Pire cas sous 15 % d'un cœur à 16 pistes sonnant ensemble, mesuré sur machine calme, à 44,1, 48, 88,2 et 96 kHz
  (88,2 et 96 en mode mi-fréquence). 192 kHz et le mode « 96 kHz natif » marchent sans être budgétés.
- Budget par poste : proposition du ticket « Budget CPU », non adoptée comme règle, guide de mesure seulement :
  moteurs ≤ 10 %, chaîne ≤ 2, envois ≤ 1 (le layer, ≤ 1, arrive à J2). Le master DT2 (~16 500 instructions
  pondérées par bloc, ticket « Effets master ») n'y a pas de ligne : son coût se mesure au port.
- Fil audio sans allocation ni verrou, tout préparé avant le rendu ; SIMD sur les voix et les étages communs ;
  FTZ/DAZ dès qu'un étage passe en flottant.
- Toute décision de construction dit son coût mesuré.
- 15 % est un plafond, pas une cible (règle de l'utilisateur, 2026-10-25 : « si on peut faire mieux on fait
  mieux »). Chaque étage vise le coût le plus bas qu'on sache atteindre à qualité égale, et un gain mesuré se garde
  même quand le budget tient déjà.

### 11.2 Fréquence et latence

- Tous les moteurs à la fréquence de l'hôte, sans rééchantillonneur ; blocs de 32 échantillons ; constantes de bloc
  recalculées depuis la fréquence.
- Exception (décidée le 2026-10-27, ticket 06) : **le Syntakt rend toujours à 48 kHz**, sa fréquence, et un
  rééchantillonneur 48 → 44,1 kHz le ramène à l'hôte (aux autres fréquences, voir le ticket « Fréquences hautes »). Le
  Syntakt dépend de sa fréquence de part en part : repliement de CY ALLOY (bord de Carson jusqu'à 1,28 MHz), retard
  d'un échantillon des rétroactions, verrou de période 2 à Nyquist (BD MODERN Hit 13, 16 dB d'enveloppe entre 44,1 et
  48 kHz). Rendu à 48 kHz, il sonne comme la machine à toute fréquence. Le rééchantillonneur passe Q11, plat à
  0,1 dB jusqu'à 18 kHz (polyphasé à phase minimale, 48 coefficients par phase, décidé le 2026-11-06 après une écoute à
  l'aveugle, ticket 11). Son retard dépend de la fréquence, 2,4 échantillons à 44,1 kHz jusqu'à 5 kHz, 5,2 à 16 kHz ; le
  délai commun des Hits en compte 3. Le MD et le DT2 se décident à leur tour.
- Les étages restés en entiers (voix du MD, port exact du Syntakt gardé comme référence) sont exacts à leur
  fréquence d'origine (Syntakt 48 kHz, MD 44,1 kHz) et adaptés ailleurs. Le Syntakt livré et les étages réécrits
  (`TrackFx`, Echo, Gate Box, master) sont vérifiés à tolérance, même à leur fréquence d'origine.
- 88,2 et 96 kHz : rendu à mi-fréquence et suréchantillonneur 2x HIIR ; option « 96 kHz natif ».
- SY TONE à 2x l'hôte ; SY BITS, SY SWARM et les SRR gardent leur fréquence absolue.
- Trigs à l'échantillon près, sans gigue (ticket « Séquenceur interne »).
- Latence mesurée, de la note de l'hôte au son en sortie, et sa gigue : rapportées avant et après chaque changement
  du chemin des trigs ou du rendu. Règle reprise du `CLAUDE.md` du dépôt (exigence de latence de MoDule), pas d'un
  ticket de la carte.

### 11.3 Qualité audio

Ticket « Qualité audio » : ne corriger que les ajouts, jamais un étage porté. PolyBLEP, HIIR, ADAA1 sur les
écrêtages durs, SVF TPT, rampes de 64 échantillons sur les coupures ajoutées. Seuils : les tests Q1-Q16 de
`research/19-qualite-audio.md` §11.

### 11.4 Sans firmware

Aucun firmware n'est nécessaire chez l'utilisateur, et le plug-in livré n'a pas de dossier `roms`. Les firmwares
servent aux tests ; toutes les tables viennent de formules testées contre le firmware, sauf l'interpolateur et le
décimateur des ROM du MD, sans formule connue, qui sont des filtres à nous, proches de ceux de l'OS, mesurés contre eux
(décidé le 2026-11-05, ticket 08 de la construction) : les ROM ne sont donc pas bit-exactes. Une flash image du MD importée
par son propriétaire sert seulement à en extraire des échantillons : la banque UW, et les formes d'onde échantillonnées
de TRX-BD, TRX-SD, TRX-XT, TRX-RS et TRX-XC (mots du programme voix, amendé le 2026-10-31), les échantillons et le
répertoire des machines E12, et les excitations des P-I avec leurs mots de modes (amendé le 2026-11-05), jamais son
code ni ses tables, à une exception près : la table de l'OS à $24DA14, sans formule connue, que P-I lit et que l'Echo,
la Gate Box et Dynamix lisent aussi, importée tant qu'une formule manque (amendé le 2026-11-05). L'import ne lit la
flash image qu'une fois et écrit ces données dans le dossier de données : le rendu ne la lit jamais. Écart du 2026-11-07 :
les défauts de machine sont maison, avec ou sans image, tous les SYN à 64 pour le MD ; le plug-in lit encore l'image au
démarrage, l'écriture dans le dossier de données attend le ticket 20. Sans image, 32 machines portées du MD et du Syntakt
sonnent entières, dont TRX-CH, OH et CY sur leurs carrés, 10 jouent sur des données faites maison, TRX-BD, SD, XT, RS
et XC et les P-I BD, SD, MT, ML et RS, les autres se taisent et le
disent (`MachineData`). Le silence est provisoire : les machines à données se refont en maison, une famille à la fois.

## 12. Critères d'acceptation

La v1 est livrée quand tout ce qui suit passe :

Son :

1. Chaque étage resté en entiers est exact contre son oracle à sa fréquence d'origine (Q1). Le Syntakt livré et les
   étages réécrits en flottant passent la tolérance Q2-Q4 contre l'oracle, à toutes les fréquences.
2. Chaque machine passe le test « sonne pareil » à 44,1 et à 48 kHz (spectre sous 20 kHz, enveloppe, niveau).
3. Les tests de qualité Q5-Q7, Q9, Q11 et Q12-Q16 passent (Q8 attend J4, Q10 J2 ; Q11 juge le rééchantillonneur
   des machines qui rendent à leur fréquence d'origine, §11.2).
4. Pire cas 16 pistes sous 15 % d'un cœur, mesuré sur machine calme, à 44,1, 48, 88,2 et 96 kHz.
5. Gigue des trigs : 0 échantillon. Latence de la note au son mesurée et publiée.

Fonctions :

6. Séquenceur : un pattern de test qui exerce longueur et échelle par piste, conditions à graine, micro-timing,
   ratchets, slide, locks et locks sans note se rend deux fois à l'identique, et chaque trig tombe à l'échantillon
   attendu ; changement de pattern à la fin du pattern en cours ; export MIDI relu.
7. Kits et état : un kit sauvé puis rechargé, et un état restitué par l'hôte, rendent les mêmes échantillons ; deux
   instances partagent la banque sans perdre l'écriture de l'autre ; un kit ancien à paramètre inconnu se charge
   avec son défaut ; l'import Elektron rapporte ce qu'il a trouvé et ce qui manque.
8. Paramètres et sorties : chaque paramètre hôte se règle et se relit ; identifiants inchangés d'une version à
   l'autre ; les relais suivent la piste affichée ; chaque piste et chaque retour sort sur la sortie choisie.
9. Interface : chaque bloc de la maquette Lockstep a son composant, chaque paramètre son contrôle ; les gestes de
   `concepts/t-ui-spec.md` marchent comme décrit.
10. Hébergement (ajout repris de MoDule, `mdDrumsVst3HostTest`, pas d'un ticket) : le VST3 se charge par
    l'hébergement VST3 de JUCE, comme Live ; mêmes échantillons quelle que soit la taille de bloc et hors ligne.

Livraison :

11. Aucun firmware n'est nécessaire chez l'utilisateur ; une flash image importée ne sert qu'aux échantillons de 11.4.
12. Le garde-fou du dépôt est vert, sans test de sa liste sauté.

## 13. Ordre de construction

Moteurs d'abord : ils décident si le budget tient. L'interface avance en parallèle.

1. Réécriture rapide du Syntakt à la fréquence de l'hôte, avec ses tests de tolérance et ses tables régénérées.
2. Port C++ du Machinedrum, harnais d'abord, puis par famille de renders ; Echo et Gate Box.
3. Moteur de l'instrument : 16 pistes, familles mélangées, chaîne à emplacements, AMP, LFO, chokes, envois.
4. Harnais d'oracle du DT2, puis compresseur et overdrive du master (et l'overdrive en drive de piste).
5. Séquenceur interne.
6. Paramètres hôte, relais, sorties, état.
7. Kits, sons de piste, samples, import Elektron, explorateur.
8. Interface JUCE d'après la maquette Lockstep.
9. Mesures de fin : CPU, latence, « sonne pareil », Q1-Q16.

Le découpage en tickets d'implémentation : [elektron-drums-v1/map.md](../elektron-drums-v1/map.md), 25 tickets.

## 14. Points ouverts

Bloquent la livraison, à lever pendant la construction :

- Seuils Q3-Q4 de `research/19-qualite-audio.md` §11, à valider à l'écoute : ce sont eux qui jugent le Syntakt
  livré et les étages réécrits en flottant.
- Harnais d'oracle du DT2 : sharcvm et digikit ne tournent pas encore (compilateur C et chaîne Rust à installer) ;
  sans lui, ni compresseur ni overdrive du master ne se vérifient.
- Tables du MD (DSP1, DSP2, OS) pas encore inventoriées : une table sans formule bloque sa machine ou la rend
  approximative et signalée.
- Garde-fou : il compte un test sauté comme passé (9 tests du Syntakt sautés le 2026-10-25, variables `SYNTAKT_*`
  absentes) ; à corriger avant le premier ticket qui touche au Syntakt.

Ne bloquent pas :

- Nom du produit (fog de la carte).
- Sidechain du compresseur du DT2 : à reprendre seulement s'il existe dans le firmware, lu au port.
- Réglage à l'oreille du chorus et du phaser/flanger contre le Virus TI.
