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
- Interface complète en JUCE, d'après le mockup v12 approuvé (section 10).

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
- Une voix éteinte (enveloppe finie, sous -100 dB) n'est pas rendue et ne coûte rien.
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

Port C++ des renders du DSP2 de l'OS 1.63 (39 machines, sans INP ni RAM-R), des 55 fonctions de coefficients 68k
et du lissage, vérifié contre `mdEngine` (ticket « Port du Machinedrum en C++ »). Ordre : harnais, ROM ou TRX, EFM,
E12, P-I, GND. Voix en entiers bit-exacts, `TrackFx` et la part du master reprise en flottant SIMD. Voix brute avant
le mixeur : la vélocité, LEV et VOL s'appliquent dans l'étage AMP. L'AMD n'est pas reprise. Les machines ROM jouent
la banque UW importée par le propriétaire, ou les samples maison, ou le silence.

## 4. Chaîne de piste

Ticket « Chaîne de piste : emplacements, ordre, routage », amendé par « Effets master ».

Squelette fixe, mono jusqu'au pan :

    machine → drives pré → BAND (avant ou après le filtre) → FILTER → drives post → AMP → envois

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
- **Vélocité** : sur le niveau par défaut, plus une destination assignable par le menu d'un knob, avec profondeur.
- **LFO** : deux par piste, SYNC (division musicale de 1/64 à 16 mesures, pointée, triolet ; phase sur la position
  du transport) ou HZ ; SYNC par défaut.
- **Chaîne par défaut** de la famille au premier choix d'une machine (Syntakt : drive pré, BASE/WIDTH, filtre
  Syntakt ; MD : EQ MD, filtre MD, SRR puis DIST post). Changer de machine dans la famille ne touche pas la chaîne ;
  changer de famille la recharge, en demandant si elle a été modifiée.
- Types de filtre et de drive automatisables, avec rampe anti-clic.
- Une piste dont la chaîne reste celle de sa famille reste fidèle à son firmware ; tout ajout est marqué « + ».

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
  courbe de vélocité), accent, slide, locks sur n'importe quel paramètre de la piste, sans plafond.
- Par pattern : swing 50-80 %, accent, slide ; mutes, fill tenu, chaînes de patterns. Le slide fait glisser les
  paramètres verrouillés vers les valeurs du pas suivant.
- Conditions aléatoires tirées d'une graine par passage : un rendu rejoue ce qu'on a entendu.
- Transport de l'hôte seulement. Pattern choisi par le paramètre hôte « Pattern » ou par program change ; un pattern
  demandé part à la fin du pattern en cours.
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
  avec recherche et filtres, aperçu. Clic écoute, double-clic charge, glisser sur une piste, la box SAMPLE ou un
  emplacement de kit. Types de samples devinés par le nom puis par une analyse simple, corrigeables.

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

Ticket « Interface façon Overbridge ». Référence : mockup v12 (`mockup/v12.html`, `.css`, `.js`,
`mockup/v12-notes.md`) et les exigences du ticket.

- JUCE seul, en C++ : composants dessinés dans `paint`, grille fixe mise à l'échelle, repeinte limitée aux zones qui
  changent. Pas de RmlUi, de WebView ni de JavaScript.
- Une page par piste sans sous-pages, une box par section, disposition fixe ; l'ordre des boxes dit l'ordre du
  signal. Couleur saturée par section (table des teintes de `v12-notes.md`) ; menthe réservée à l'état global.
- Box de pas en bas de chaque page de piste, modes Trig, Vel, Accent, Slide, liste des locks de la sélection ; gestes
  de souris du ticket (clic pose ou retire un trig, Ctrl et Maj sélectionnent, glisser peint).
- Pages du mockup : piste (toutes les boxes à la fois), FX, Master, SEQ (lignes de locks choisies dans la liste des
  paramètres verrouillables), Kit ; onglets Kit editor et Sound browser en tête.
- Menu d'un knob (Vélocité, Lock) ouvert par un clic sur son nom ou un clic droit ; LFO assignés par le bouton
  ASSIGN de leur box seulement.
- Knobs à proportions fixes (texte de valeur, anneau, étiquette en rapport au diamètre), deux tailles (52 et 64 px),
  valeur toujours dans l'anneau ; modulation en arc fin, lock en couleur d'accent, valeur jouée en repère.
- Pas de rangée de pads : bouton play et LED par onglet de piste. CPU en haut à gauche, 0-100 % d'un cœur, repère à
  15 %, ambre au-dessus, rouge au-dessus de 25 %.
- Aucun texte redondant : l'état se lit sur le contrôle, un badge ou une infobulle.

## 11. Exigences non fonctionnelles

### 11.1 Performance

- Pire cas sous 15 % d'un cœur à 16 pistes sonnant ensemble, mesuré sur machine calme, à 44,1 et 48 kHz.
- Budget par poste (ticket « Budget CPU ») : moteurs ≤ 10 %, chaîne ≤ 2, envois ≤ 1 (le layer, ≤ 1,
  arrive à J2).
- Fil audio sans allocation ni verrou, tout préparé avant le rendu ; SIMD sur les voix et les étages communs ;
  FTZ/DAZ dès qu'un étage passe en flottant.
- Toute décision de construction dit son coût mesuré.

### 11.2 Fréquence et latence

- Tous les moteurs à la fréquence de l'hôte, sans rééchantillonneur ; blocs de 32 échantillons ; constantes de bloc
  recalculées depuis la fréquence.
- Exacts à leur fréquence d'origine (Syntakt 48 kHz, MD 44,1 kHz), adaptés ailleurs.
- 88,2 et 96 kHz : rendu à mi-fréquence et suréchantillonneur 2x HIIR ; option « 96 kHz natif ».
- SY TONE à 2x l'hôte ; SY BITS, SY SWARM et les SRR gardent leur fréquence absolue.
- Latence mesurée, de la note de l'hôte au son en sortie, et sa gigue : rapportées avant et après chaque changement
  du chemin des trigs ou du rendu.

### 11.3 Qualité audio

Ticket « Qualité audio » : ne corriger que les ajouts, jamais un étage porté. PolyBLEP, HIIR, ADAA1 sur les
écrêtages durs, SVF TPT, rampes de 64 échantillons sur les coupures ajoutées. Seuils : les tests Q1-Q16 de
`research/19-qualite-audio.md` §11.

### 11.4 Sans firmware

Le plug-in livré n'a pas de dossier `roms`. Les firmwares servent aux tests seulement ; toutes les tables viennent de
formules testées contre le firmware.

## 12. Critères d'acceptation

La v1 est livrée quand tout ce qui suit passe :

1. Chaque étage porté passe contre son oracle : exact à sa fréquence d'origine (Q1), dans la tolérance Q2-Q4
   ailleurs.
2. Chaque machine passe le test « sonne pareil » à 44,1 et à 48 kHz (spectre sous 20 kHz, enveloppe, niveau).
3. Les tests de qualité Q5-Q16 qui concernent la v1 passent.
4. Pire cas 16 pistes sous 15 % d'un cœur, mesuré sur machine calme.
5. Latence et gigue rapportées ; trigs à l'échantillon près.
6. Le plug-in se charge par l'hébergement VST3 de JUCE, comme Live : paramètres, identifiants, sorties, état
   restitué à l'identique, mêmes échantillons quelle que soit la taille de bloc et hors ligne.
7. Aucun fichier de firmware n'est lu par le plug-in livré.
8. Le garde-fou du dépôt est vert.

## 13. Ordre de construction

Moteurs d'abord : ils décident si le budget tient. L'interface avance en parallèle.

1. Réécriture rapide du Syntakt à la fréquence de l'hôte, avec ses tests de tolérance et ses tables régénérées.
2. Port C++ du Machinedrum, harnais d'abord, puis par famille de renders ; Echo et Gate Box.
3. Moteur de l'instrument : 16 pistes, familles mélangées, chaîne à emplacements, AMP, LFO, chokes, envois.
4. Harnais d'oracle du DT2, puis compresseur et overdrive du master (et l'overdrive en drive de piste).
5. Séquenceur interne.
6. Paramètres hôte, relais, sorties, état.
7. Kits, sons de piste, samples, import Elektron, explorateur.
8. Interface JUCE d'après v12.
9. Mesures de fin : CPU, latence, « sonne pareil », Q1-Q16.

Le découpage en tickets d'implémentation suit cette spécification.

## 14. Points ouverts

Aucun ne bloque la construction :

- Nom du produit (fog de la carte).
- Deux seuils de qualité de `research/19-qualite-audio.md` §11, à valider à l'écoute.
- Sidechain du compresseur du DT2 : à reprendre seulement s'il existe dans le firmware, lu au port.
- Réglage à l'oreille du chorus et du phaser/flanger contre le Virus TI.
