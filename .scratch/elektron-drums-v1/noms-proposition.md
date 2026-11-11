# Noms des paramètres : proposition (ticket 27)

Proposition pour le ticket `issues/27-noms-des-parametres.md`, révisée le 2026-11-11 après la réponse de l'utilisateur.
Rien n'est appliqué : ce document attend son avis. Les ID restent gelés (`edDrumsParameterIds.txt`) ; seuls changent les
noms montrés, dans l'hôte et dans l'éditeur.

Décidé par l'utilisateur le 2026-11-11 (détail dans le ticket, section « Décisions ») : noms en anglais ; noms maison,
sans marque ni nom de machine ; HPF et LPF gardés ; « Machine 1 » à « Machine 24 » gardés dans l'hôte. Refusés : des
noms de machine faits de préfixes codés (VA-BD, FM-SD, 12B-RC) et des noms de famille abstraits (STEEL, HYBRID, SHARD).
Demandé : des noms qui disent l'identité de chaque machine et de chaque famille, ce qu'elle sonne ou comment elle fait
son son, pour qu'on la choisisse par son nom ; et surtout un nom clair pour chacun des contrôles de chaque machine
(Machine 1-8, et 9-24 là où la machine en a), qui dit ce qu'il fait sur cette machine-là.

Sources lues : `edDrumsPlugin/edDrumsParameters.cpp` (noms d'hôte de la chaîne, des LFO, des envois, des effets partagés,
du master), `edDrumsPlugin/edDrumsProcessor.cpp` (piste, Machine 1-24, liste des machines), `edDrums/edDrumsEngine.h`
(noms des machines Syntakt, OPEN DRUM, SOPHIE), `md/mdProtocol/mdmachines.cpp` (noms Machinedrum et leurs huit
paramètres, table de l'OS 1.63), `md/mdVoice/mdVoiceCoefficients.cpp` (ce que le moteur fait de chaque paramètre MD),
`syntakt/stEngine/*.cpp` (en-têtes des moteurs Syntakt), `opendrum/openDrum.c`, `sophie/sophieMachine.h` et
`sophie/README.md`, `edDrumsPlugin/edDrumsEditorScreen.cpp` (étiquettes de l'éditeur), la maquette Lockstep
(`.scratch/elektron-drums/concepts/HANDOFF.md`, `t_src/t-ui-spec-head.md`).

Le sens de chaque contrôle vient du code du moteur quand il le dit, sinon de ce que je connais des manuels des machines
d'origine. La colonne « confiance » le dit : **sûr** (le code ou la doc du port le montre), **probable** (le nom d'origine
et ce que le moteur en fait concordent), **subtil — à vérifier** (l'utilisateur l'a écouté et n'entend presque
rien : le nom reste le meilleur que j'aie, et le contrôle est peut-être un défaut du port, section 7).

Écoute du 2026-11-11 : l'utilisateur a écouté les 56 points incertains de la première version sur la page
`C:\dev\elektron-work\control-listen\index.html` et en a exporté ses notes (`control-listen-notes (2).json`). Chaque
ligne écoutée le dit (« Écouté : … » ou « confirmé à l'écoute ») : un « ok » rend le nom sûr ; une description donne le
nom, sûr quand elle le tranche, probable sinon. Il ne reste aucun contrôle de machine incertain ; une ligne des effets partagés le reste (Echo Mono, non écoutée).

---

## 1. Règles de nommage

### 1.1 Langue : anglais (décidé)

L'interface existante et la maquette Lockstep sont en anglais, et c'est le vocabulaire que l'utilisateur lit sur ses
machines et dans Live.

### 1.2 Nom court (cellule, liste des machines)

- **10 caractères visés, 12 au plus pour un contrôle ; 10 au plus pour un nom de machine.** Une étiquette de cellule est
  écrite à 3,2 mm, l'éditeur compte 1,72 mm par caractère (`edDrumsEditorScreen.cpp:434`) ; les cellules font de 35,3 mm
  (bloc FX, ligne 2448) à 38,5 mm (blocs de Sound, ligne 495) ; 12 caractères laissent la place à la valeur de base
  qu'une cellule verrouillée écrit à droite de son nom (ligne 446). Le nom d'une ligne de locks dans Seq est coupé à 12
  (ligne 1574). Browse coupe un nom de machine à 10 dans le LCD des pistes (ligne 1920) : SP TWINSHOT (11) y est déjà
  tronqué aujourd'hui.
- Majuscules, comme aujourd'hui.

### 1.3 Nom long (hôte, infobulle, lignes de locks)

- Format : `T01 <Bloc> <Fonction>` pour un paramètre de piste, `<Bloc> <Fonction>` pour un paramètre partagé. Title Case.
- **Les 10 premiers caractères après « T01 » doivent suffire à le reconnaître.** Push montre une dizaine de caractères
  (à vérifier sur Push : Live abrège lui-même un nom trop long). Défaut actuel : « Filter Env Delay », « Filter Env
  Attack », « Filter Env Decay » commencent tous par « Filter Env ». Proposé : « Filter Delay », « Filter Attack »,
  « Filter Decay », « Filter Env Amt ».
- 16 caractères au plus après « T01 ».
- **Machine 1-24 restent « T01 Machine 1 » … dans l'hôte (décidé).** Leurs noms d'hôte sont fixés à la création du
  plug-in (`edDrumsProcessor.cpp:364-366`, `473-475`) et ne peuvent pas suivre la machine. Les noms par machine de la
  section 4 sont donc des noms de cellule ; leur nom long, pour l'infobulle et les lignes de locks de Seq, est le nom
  court en Title Case, abréviations écrites en entier (FM AMOUNT devient « FM Amount », HPF RESO « HPF Resonance »).
  Aujourd'hui une ligne de locks de Seq écrit « MACHINE 3 » (`edDrumsEditorScreen.cpp:1296`, `1550`) : elle devrait lire
  le nom de la machine jouée.

### 1.4 Unités

Aucune unité dans le nom : la valeur la porte (`valueText` écrit Hz, dB, ms, %, Q, notes). « LFO1 Hz » et « Delay Free
Hz » deviennent « LFO1 Free Rate » et « Delay Free Time » (la valeur reste en Hz).

### 1.5 Noms, pas verbes

Un contrôle se nomme par ce qu'il règle (DECAY, CUTOFF). Un interrupteur se nomme par ce qui est vrai quand il est allumé
(SYNC, AMP ENV) ; ses deux états gardent leurs mots (PRE/POST, HZ/SYNC).

### 1.6 Une fonction, un mot, partout

| Fonction | Mot court | Remplace |
|---|---|---|
| hauteur de la voix | TUNE | PTCH, TUNE, PTC1 |
| réglage fin de hauteur | FINE TUNE | FINE |
| durée de la chute du son | DECAY | DEC, DECAY |
| durée de la chute d'une composante | <COMPOSANTE> DECAY (NOISE DECAY, FM DECAY, RING DECAY…) | NDEC, MDEC, RDEC, CDEC, SDEC… |
| chute de hauteur à l'attaque | SWEEP | RAMP, SWEEP, PITCH AMT |
| durée de cette chute | SWEEP TIME (le mot de l'utilisateur) | RDEC, PITCH DEC |
| forme d'une courbe | <X> SHAPE | DEC SLOPE, PITCH SLP |
| transitoire d'attaque | CLICK | CLIC, TICK, CLICK, STRT du TRX-BD |
| saturation | DRIVE | DIST, OVERDRIVE, DRIVE |
| écrêtage dur | CLIP | CLIP (distinct de DRIVE) |
| réduction de bits | BIT CRUSH | BRR, BR, BIT REDUX, CRUSH |
| réduction de fréquence d'échantillonnage | DOWNSAMPLE | SR REDUX, MD SRR |
| passe-haut, passe-bas | HPF, LPF (décidé) | HP, HPF, LP, LPF |
| fréquence d'un filtre réglable | CUTOFF | FREQ, FILTER |
| résonance | RESO | RES, RESO, HPQ (HPF RESO) |
| modulation FM | FM AMOUNT, FM FREQ, FM DECAY, FM FDBK, FM ENV | MOD, MFRQ, MDEC, MFB, FB, MOD ENV |
| répétitions d'un coup | REPEATS, REPEAT GAP | RTRG, RTIM, REPEAT |
| destination d'une modulation | TARGET | DEST, Target |
| vitesse d'un LFO ou d'un effet | RATE (Note Rate synchronisé, Free Rate libre) | SPEED, HZ, Speed |
| forme d'onde d'un LFO | WAVE | SHAPE, Shape |
| niveau de retour d'un effet | LEVEL | LEV, MIX (Chorus, Phaser) |

Les mots déjà clairs restent (LEVEL, PAN, ATTACK, HOLD, SUSTAIN, RELEASE, TONE, NOISE, MIX, WIDTH). Abréviations
gardées : AMT dans les noms longs, FDBK, FREQ, RESO, ENV, LFO, FM, S&H.

---

## 2. Noms de famille : l'identité

Un nom de famille dit comment la famille fait son son ; on le lirait comme titre de groupe dans Browse (aujourd'hui la
liste n'a pas de titres : `edDrumsEditorScreen.cpp:3315-3326`) et dans les infobulles. 12 caractères au plus. Un nom
proposé, un autre choix, une ligne pour dire pourquoi.

| Famille (aujourd'hui) | Proposé | Autre choix | Pourquoi |
|---|---|---|---|
| GND du Machinedrum | **BASIC** | RAW TONES | Les générateurs nus : sinusoïde, bruit, impulsion. |
| TRX du Machinedrum | **CIRCUIT** | ANALOG | Des circuits de boîte à rythmes analogique modélisés : ronds, chauds, saturables. |
| EFM du Machinedrum | **FM PERC** | CLICKY FM | Percussion en FM à deux opérateurs : attaques claquantes, métal, cloches. |
| E12 du Machinedrum | **CRUNCH 12** | LOFI 12 | Des échantillons en 12 bits : le grain des boîtes à rythmes échantillonnées des années 80. (Le nom donné en exemple par l'utilisateur.) |
| P-I du Machinedrum | **PHYSICAL** | STRUCK | Des modèles physiques : peaux, bois et métal frappés, une mailloche, une main qui saisit. |
| ROM du Machinedrum | **ROM BANK** | FACTORY | La banque d'échantillons d'usine, par numéro. |
| Syntakt | **MODERN DIGI** | CLEAN FM | Synthèse numérique d'aujourd'hui : nette, percutante, des partiels et de la FM réglables. |
| OPEN DRUM | **VA DRUMS** | SYNTH KIT | Analogique virtuel : oscillateurs, FM, filtre, et un transitoire échantillonné pour l'attaque. |
| SOPHIE | **HYPER FM** | METAL FM | FM métallique extrême, du kick qui gronde à l'éclat de verre. |
| Sampler | **SAMPLER** | — | Générique. |

Effets et étages nommés aujourd'hui d'après une machine :

| Aujourd'hui | Proposé | Autre choix | Pourquoi |
|---|---|---|---|
| Echo (« MD Echo ») | **DUB ECHO** | TAPE ECHO | Un écho dont le temps est modulé et la boucle filtrée : il pleure et s'assombrit à chaque retour. |
| Gate Box (« MD Gate Box ») | **GATED VERB** | 80S GATE | Une réverbération que coupe une porte : le gros son de caisse claire des années 80. |
| délai du Syntakt | **STEREO DLY** | PING DELAY | Délai net, ping-pong et largeur stéréo. |
| réverbération du Syntakt | **LUSH VERB** | SHELF VERB | Longue, avec étagère d'aigus. Caractère confirmé à l'écoute le 2026-11-11. |
| mode « DP/4 » du phaser | **90S RACK** | DUAL PHASE | Un phaser de multi-effet en rack des années 90 : délais gauche et droit, phase inversable, LFO en paliers. |
| Chorus, Phaser, Flanger « + » | gardés | — | Aucun nom de marque montré. |
| Master (compresseur, drive) | gardés (COMPRESSOR, MASTER) | BUS COMP | Aucun nom de marque montré. |
| « DT2 » (Sample, J2) | **GRAIN** | — | Ce qu'il fera : lire un échantillon par grains. |
| drive « Syntakt », « MD DIST », « MD SRR » | **SATURATE**, **DISTORT**, **DOWNSAMPLE** | WARM, CRUNCH, DECIMATE | Gain doux dans un écrêtage (`syntaktDriveGain`) ; gain fort dans la table de distorsion (`mdDistGain`) ; échantillon tenu à une fréquence plus basse (`srrHz`). |
| filtre « Syntakt », « Machinedrum » | **MULTIMODE**, **WINDOW** | SMOOTH, GRITTY | LP, HP et cinq EQ ; un passe-haut et un passe-bas une largeur au-dessus, une fenêtre de fréquences. |
| bande « Base-Width », « MD EQ », « 3 Band » | **CUTS**, **PEAK**, **3 BAND** | B/W, BELL | Un HPF et un LPF ; une cloche ; trois bandes. |

Les choix « Delay Source » et « Reverb Source » deviennent DUB ECHO / STEREO DLY et GATED VERB / LUSH VERB ; « Phaser
Mode » devient PHASER / FLANGER / 90S RACK.

---

## 3. Noms des machines : l'identité

Décidé : les noms des machines suivent la règle. Chaque machine prend un nom qui dit son caractère, 10 caractères au
plus, uniques sur les 67 de la liste ; la section 4 donne pour chacune le nom, un autre choix, une ligne sur son son et
la confiance du nom. Quand une famille compte un instrument de chaque sorte, son mot de caractère revient (LOFI pour
CRUNCH 12, REAL pour PHYSICAL, FM pour FM PERC, VA pour VA DRUMS) : c'est le son qu'on lit, pas un code.

Vérifié dans le code : l'état et les Kits gardent l'**index** de la machine, pas son nom (`edDrumsProcessor.cpp`,
`choiceOf`) ; un set rechargé joue la même machine. Un son de piste sauvé écrit le nom de sa machine dans `machineName`
(`edDrumsProcessor.cpp:1842`), que Browse affiche et cherche (`edDrumsTrackSounds.cpp:49`,
`edDrumsEditorScreen.cpp:2967`) : les sons sauvés avant montreraient l'ancien nom jusqu'à leur prochaine sauvegarde,
sans effet sur le son. Le choix « Machine » de l'hôte liste ces noms : Live montrera les nouveaux.

---

## 4. La table

### 4.1 Piste : machine, niveau, sorties

| Nom actuel (hôte / cellule) | Court | Long | Ce qu'il fait | Confiance |
|---|---|---|---|---|
| T01 Machine / (titre) | — | T01 Machine | La machine que joue la piste. | sûr |
| T01 Machine 1-24 / M1-M24 ou nom de la machine | nom de la machine (4.8-4.16) | T01 Machine 1 … 24 (hôte, décidé) | Les contrôles de la machine. | sûr |
| T01 Note / NOTE | — | T01 Note | La note que joue un coup de la piste, que suit le filtre (KEY). | probable |
| T01 Level / LEVEL | — | T01 Level | Niveau de la piste. | sûr |
| T01 Pan / PAN | — | T01 Pan | Position stéréo. | sûr |
| T01 Output / OUT | OUT | T01 Out | Main ou Out 01-15. | sûr |
| T01 Chokes / CHOKES | — | T01 Chokes | Les pistes qu'un coup de celle-ci coupe. | sûr |
| T01 Accent / ACCENT | — | T01 Accent | Ce qu'un accent ajoute à la vélocité (128 + 2 x valeur). | sûr |

### 4.2 Piste : Drive 1 et 2 (la ligne vaut pour les deux)

| Nom actuel | Court | Long | Ce qu'il fait | Confiance |
|---|---|---|---|---|
| Drive 1 Type / TYPE, choix Off/Syntakt/MD DIST/MD SRR | TYPE, choix OFF/SATURATE/DISTORT/DOWNSAMPLE | T01 Drive 1 Type | Le genre de saturation. | sûr |
| Drive 1 Post / PLACE (PRE/POST) | PLACE | T01 Drive 1 Place | Avant ou après le filtre. | sûr |
| Drive 1 Amount / AMOUNT | — | T01 Drive 1 Amount | Gain en dB, ou fréquence tenue pour DOWNSAMPLE. | sûr |
| Drive 1 Mix / MIX | — | T01 Drive 1 Mix | Part du signal saturé. | sûr |

### 4.3 Piste : Band (proposé : EQ)

| Nom actuel | Court | Long | Ce qu'il fait | Confiance |
|---|---|---|---|---|
| Band Type / TYPE, choix Off/Base-Width/MD EQ/3 Band | TYPE, choix OFF/CUTS/PEAK/3 BAND | T01 EQ Type | Le genre d'égaliseur. | sûr |
| Band After Filter / PLACE | PLACE | T01 EQ Place | Avant ou après le filtre. | sûr |
| Band 1-2 / BASE, WIDTH (CUTS) | HPF, LPF | T01 EQ 1 … EQ 6 (hôte, ne suit pas le type) | Passe-haut et passe-bas (la maquette les montre en Hz). | sûr |
| Band 1-2 / EQF, EQG (PEAK) | PEAK FREQ, PEAK GAIN | idem | Fréquence et gain de la cloche. | sûr |
| Band 1-6 / LOW F, LOW G, MID F, MID G, HIGH F, HIGH G | — | idem | Fréquence et gain des trois bandes. | sûr |

### 4.4 Piste : Filter

| Nom actuel | Court | Long | Ce qu'il fait | Confiance |
|---|---|---|---|---|
| Filter Type / TYPE, choix Off/Syntakt/Machinedrum | TYPE, choix OFF/MULTIMODE/WINDOW | T01 Filter Type | Le filtre de la piste. | sûr |
| Filter Mode / MODE, LP/HP/EQ1-EQ5/OFF | — | T01 Filter Mode | La réponse du MULTIMODE ; EQ1 à EQ5 sont des cloches de largeurs différentes d'après le dessin de l'éditeur. | probable |
| Filter Freq / FREQ | CUTOFF | T01 Filter Cutoff | Fréquence de coupure. | sûr |
| Filter Res / RESO | — | T01 Filter Reso | Résonance. | sûr |
| Filter Width / WIDTH | — | T01 Filter Width | WINDOW seulement : écart en octaves entre passe-haut et passe-bas. | sûr |
| Filter Key Track / KEY | KEY TRACK | T01 Filter Key | La coupure suit la note. | sûr |
| Filter Env Delay / DELAY | — | T01 Filter Delay | Attente de l'enveloppe de filtre. | sûr |
| Filter Env Attack / ATTACK | — | T01 Filter Attack | | sûr |
| Filter Env Decay / DECAY | — | T01 Filter Decay | | sûr |
| Filter Env Sustain / SUSTAIN | — | T01 Filter Sustain | | sûr |
| Filter Env Release / RELEASE | — | T01 Filter Release | | sûr |
| Filter Env Reset / ENV RESET | ENV RETRIG | T01 Filter Retrig | Un coup relance l'enveloppe depuis 0. | probable |
| Filter Env Depth / ENV | ENV AMT | T01 Filter Env Amt | Ce que l'enveloppe ajoute à la coupure, ±63. | sûr |

### 4.5 Piste : Amp et vélocité

| Nom actuel | Court | Long | Ce qu'il fait | Confiance |
|---|---|---|---|---|
| Amp On / AMP | AMP ENV | T01 Amp Env | Allume l'enveloppe d'amplitude de la chaîne ; éteinte, la voix garde la sienne. | sûr |
| Amp Mode / MODE (AHD/ADSR) | — | T01 Amp Mode | | sûr |
| Amp Attack, Hold, Decay, Sustain, Release | — | T01 Amp Attack … | | sûr |
| Vel Target / VEL DEST | VEL TARGET | T01 Velocity Target | Ce que la vélocité module en plus du niveau. | sûr |
| Vel Depth / VEL AMT | — | T01 Velocity Amt | De combien, à vélocité 127. | sûr |

### 4.6 Piste : LFO 1 et 2

| Nom actuel | Court | Long | Ce qu'il fait | Confiance |
|---|---|---|---|---|
| LFO1 Sync / SYNC | — | T01 LFO1 Sync | Suit le tempo ou tourne en Hz. | sûr |
| LFO1 Speed / RATE | RATE | T01 LFO1 Note Rate | Durée d'un cycle en notes. | sûr |
| LFO1 Hz / HZ | RATE | T01 LFO1 Free Rate | Vitesse libre ; une seule des deux cellules RATE paraît à la fois. | sûr |
| LFO1 Feel / FEEL | — | T01 LFO1 Feel | Droit, pointé, triolet. | sûr |
| LFO1 Shape / WAVE | WAVE | T01 LFO1 Wave | | sûr |
| LFO1 Mode / MODE | TRIG MODE | T01 LFO1 Trig Mode | Ce qu'un coup fait à l'LFO : rien, le relance, le fige, un cycle, un demi-cycle. | sûr |
| LFO1 Depth / DEPTH | — | T01 LFO1 Depth | | sûr |
| LFO1 Track / TRACK | ON TRACK | T01 LFO1 On Track | La piste dont il module le paramètre. | sûr |
| LFO1 Target / DEST | TARGET | T01 LFO1 Target | Le paramètre modulé. | sûr |

### 4.7 Piste : envois

| Nom actuel | Court | Long | Confiance |
|---|---|---|---|
| Delay Send / DELAY | — | T01 Send Delay | sûr |
| Reverb Send / REVERB | — | T01 Send Reverb | sûr |
| Chorus Send / CHORUS | — | T01 Send Chorus | sûr |
| Phaser Send / PH/FL | PHASER | T01 Send Phaser | sûr |

Une section par famille, un bloc par machine : son nom proposé, un autre choix, une ligne sur son son et la confiance du nom, puis chacun de ses contrôles dans l'ordre Machine 1, 2, 3… Les machines non jouables ici (GND-SW, GND-PU, NFX, INP, MID, CTR, RAM) ne sont pas offertes et ne sont pas listées. « — » : contrôle que la machine n'utilise pas.

### 4.8 BASIC (Machinedrum GND)

#### SILENCE (aujourd'hui GND---)

Autre nom : MUTE. Ne sonne pas : une piste réservée. Confiance du nom : sûr.

Aucun contrôle.

#### SINE (aujourd'hui GND-SN)

Autre nom : PURE TONE. Une sinusoïde, avec sa chute de hauteur : sous-grave, tonalité de test. Confiance du nom : sûr.

| # | Actuel | Proposé | Ce qu'il fait | Confiance |
|---|---|---|---|---|
| 1 | PTCH | TUNE | Hauteur du son. | sûr |
| 2 | DEC | DECAY | Durée de la chute du son. | sûr |
| 3 | RAMP | SWEEP | Chute de hauteur à l'attaque : le « piou » du coup. | probable |
| 4 | RDEC | SWEEP TIME | Durée de cette chute de hauteur. | probable |

#### NOISE (aujourd'hui GND-NS)

Autre nom : HISS. Un bruit blanc qui décroît. Confiance du nom : sûr.

| # | Actuel | Proposé | Ce qu'il fait | Confiance |
|---|---|---|---|---|
| 1 | DEC | DECAY | Durée de la chute du son. | sûr |

#### IMPULSE (aujourd'hui GND-IM)

Autre nom : CLICK. Une impulsion : un clic sec, à filtrer. Confiance du nom : probable.

| # | Actuel | Proposé | Ce qu'il fait | Confiance |
|---|---|---|---|---|
| 1 | UP | CLICK RISE | Écouté : un clic. Durée de la montée du clic. | probable |
| 2 | UVAL | CLICK PEAK | Écouté : un clic. Niveau en haut de la montée. | probable |
| 3 | DOWN | CLICK FALL | Écouté : un clic. Durée de la descente du clic. | probable |
| 4 | DVAL | CLICK END | Écouté : un clic. Niveau en bas de la descente. | probable |


### 4.9 CIRCUIT (Machinedrum TRX)

#### ROUND KICK (aujourd'hui TRX-BD)

Autre nom : WARM KICK. Grosse caisse d'analogique modélisée : sinusoïde ronde, harmoniques et écrêtage pour la durcir. Confiance du nom : probable.

| # | Actuel | Proposé | Ce qu'il fait | Confiance |
|---|---|---|---|---|
| 1 | PTCH | TUNE | Hauteur du son. | sûr |
| 2 | DEC | DECAY | Durée de la chute du son. | sûr |
| 3 | RAMP | SWEEP | Chute de hauteur à l'attaque : le « piou » du coup. | probable |
| 4 | RDEC | SWEEP TIME | Durée de cette chute de hauteur. | probable |
| 5 | STRT | CLICK | Phase de départ de l'onde : plus haut, plus de clic à l'attaque. | probable |
| 6 | NOIS | NOISE | Bruit mêlé au ton. | probable |
| 7 | HARM | HARMONICS | Harmoniques ajoutées à la sinusoïde : plus de mordant. | probable |
| 8 | CLIP | CLIP | Écrêtage dur : plus fort, plus carré. | probable |

#### ANA SNARE (aujourd'hui TRX-SD)

Autre nom : TIGHT SNR. Caisse claire d'analogique modélisée : deux tons et le bruit du timbre. Confiance du nom : probable.

| # | Actuel | Proposé | Ce qu'il fait | Confiance |
|---|---|---|---|---|
| 1 | PTCH | TUNE | Hauteur du son. | sûr |
| 2 | DEC | DECAY | Durée de la chute du son. | sûr |
| 3 | BUMP | PITCH BUMP | Saut de hauteur bref à l'attaque. | probable |
| 4 | BENV | BUMP DECAY | Durée de ce saut. | probable |
| 5 | SNAP | SNARES | Bruit du timbre : du tom à la caisse claire. | probable |
| 6 | TONE | TONE | Couleur, du sourd au brillant. | probable |
| 7 | TUNE | TUNE 2 | Écouté : un accord. Accord d'une seconde composante. | sûr |
| 8 | CLIP | CLIP | Écrêtage dur : plus fort, plus carré. | probable |

#### ANA TOM (aujourd'hui TRX-XT)

Autre nom : ROUND TOM. Tom d'analogique modélisée, à chute de hauteur, saturable. Confiance du nom : probable.

| # | Actuel | Proposé | Ce qu'il fait | Confiance |
|---|---|---|---|---|
| 1 | PTCH | TUNE | Hauteur du son. | sûr |
| 2 | DEC | DECAY | Durée de la chute du son. | sûr |
| 3 | RAMP | SWEEP | Chute de hauteur à l'attaque : le « piou » du coup. | probable |
| 4 | RDEC | SWEEP TIME | Durée de cette chute de hauteur. | probable |
| 5 | DAMP | DAMPING | Amortit la résonance : son plus mat, plus court. | probable |
| 6 | DIST | DRIVE | Saturation de la voix. | probable |
| 7 | DTYP | DRIVE TYPE | Genre de saturation. | probable |
| 8 | — | — | Non utilisé par la machine. | — |

#### ROOM CLAP (aujourd'hui TRX-CP)

Autre nom : BIG CLAP. Clap d'analogique modélisée, avec sa pièce réglable. Confiance du nom : probable.

| # | Actuel | Proposé | Ce qu'il fait | Confiance |
|---|---|---|---|---|
| 1 | CLPY | CLAPS | Nombre de mains : du coup sec au clap épais. | probable |
| 2 | TONE | TONE | Couleur, du sourd au brillant. | probable |
| 3 | HARD | HARDNESS | Dureté de l'attaque (de la mailloche pour les modèles physiques). | probable |
| 4 | RICH | DENSITY | Écouté : la densité du clap. | sûr |
| 5 | RATE | CLAP GAP | Écart entre les mains. | probable |
| 6 | ROOM | ROOM | Niveau de la pièce. | probable |
| 7 | RSIZ | ROOM SIZE | Taille de la pièce. | probable |
| 8 | RTUN | ROOM TUNE | Couleur de la pièce. | probable |

#### ANA RIM (aujourd'hui TRX-RS)

Autre nom : RIMSHOT. Rimshot d'analogique modélisée, bref et boisé. Confiance du nom : probable.

| # | Actuel | Proposé | Ce qu'il fait | Confiance |
|---|---|---|---|---|
| 1 | PTCH | TUNE | Hauteur du son. | sûr |
| 2 | DEC | DECAY | Durée de la chute du son. | sûr |
| 3 | DIST | DRIVE | Saturation de la voix. | probable |

#### COWBELL (aujourd'hui TRX-CB)

Autre nom : ANA BELL. Cloche à vache d'analogique modélisée : deux carrés désaccordés. Confiance du nom : probable.

| # | Actuel | Proposé | Ce qu'il fait | Confiance |
|---|---|---|---|---|
| 1 | PTCH | TUNE | Hauteur du son. | sûr |
| 2 | DEC | DECAY | Durée de la chute du son. | sûr |
| 3 | ENH | PRESENCE | Écouté : plus fort et moins filtré. | probable |
| 4 | DAMP | DAMPING | Amortit la résonance : son plus mat, plus court. | probable |
| 5 | TONE | TONE | Couleur, du sourd au brillant. | probable |
| 6 | BUMP | PITCH BUMP | Saut de hauteur bref à l'attaque. | probable |

#### TIGHT HAT (aujourd'hui TRX-CH)

Autre nom : CLOSED HAT. Charleston fermé d'analogique modélisée : bruit et métal filtrés. Confiance du nom : probable.

| # | Actuel | Proposé | Ce qu'il fait | Confiance |
|---|---|---|---|---|
| 1 | GAP | TAIL | Écouté : le son s'allonge. | probable |
| 2 | DEC | DECAY | Durée de la chute du son. | sûr |
| 3 | HPF | HPF | Passe-haut : retire le grave. | probable |
| 4 | LPF | LPF | Passe-bas : retire l'aigu. | probable |
| 5 | MTAL | METAL | Part métallique : du bruit au métal. | probable |

#### OPEN HAT (aujourd'hui TRX-OH)

Autre nom : LONG HAT. Charleston ouvert, même circuit que TIGHT HAT. Confiance du nom : probable.

| # | Actuel | Proposé | Ce qu'il fait | Confiance |
|---|---|---|---|---|
| 1 | GAP | TAIL | Écouté : le son boucle, sa queue s'allonge. | probable |
| 2 | DEC | DECAY | Durée de la chute du son. | sûr |
| 3 | HPF | HPF | Passe-haut : retire le grave. | probable |
| 4 | LPF | LPF | Passe-bas : retire l'aigu. | probable |
| 5 | MTAL | METAL | Part métallique : du bruit au métal. | probable |

#### ANA CYMBAL (aujourd'hui TRX-CY)

Autre nom : WASH CYM. Cymbale d'analogique modélisée : un corps et des aigus accordables. Confiance du nom : probable.

| # | Actuel | Proposé | Ce qu'il fait | Confiance |
|---|---|---|---|---|
| 1 | RICH | TUNE | Écouté : seule la hauteur change ; le moteur en tire la fréquence des six partiels (`trxCy`, « tone »). | subtil — à vérifier |
| 2 | DEC | DECAY | Durée de la chute du son. | sûr |
| 3 | TOP | TOP LEVEL | Niveau des aigus. | probable |
| 4 | TTUN | TOP TUNE | Accord des aigus. | probable |
| 5 | SIZE | SIZE | Taille de l'instrument : plus grand, plus grave et plus long. | probable |
| 6 | PEAK | RESO | Écouté : un filtre résonant, qui sature à 127. | probable |

#### MARACAS (aujourd'hui TRX-MA)

Autre nom : SHAKER. Maracas d'analogique modélisée : des grains de bruit secoués. Confiance du nom : probable.

| # | Actuel | Proposé | Ce qu'il fait | Confiance |
|---|---|---|---|---|
| 1 | ATT | ATTACK | Montée du secouement. | probable |
| 2 | SUS | SUSTAIN | Tenue du secouement. | probable |
| 3 | REV | REVERSE | Écouté : un mouvement inversé. | sûr |
| 4 | DAMP | DAMPING | Amortit la résonance : son plus mat, plus court. | probable |
| 5 | RATL | RATTLE | Grains qui roulent. | probable |
| 6 | RTYP | RATTLE TYPE | Genre de ces grains. | probable |
| 7 | TONE | TONE | Couleur, du sourd au brillant. | probable |
| 8 | HARD | HARDNESS | Dureté de l'attaque (de la mailloche pour les modèles physiques). | probable |

#### CLAVES (aujourd'hui TRX-CL)

Autre nom : WOOD CLICK. Claves d'analogique modélisée : un ou deux tons boisés très brefs. Confiance du nom : probable.

| # | Actuel | Proposé | Ce qu'il fait | Confiance |
|---|---|---|---|---|
| 1 | PTCH | TUNE | Hauteur du son. | sûr |
| 2 | DEC | DECAY | Durée de la chute du son. | sûr |
| 3 | DUAL | 2ND TONE | Niveau d'un second ton. | probable |
| 4 | ENH | ENHANCE | Écouté : ne change presque rien. | subtil — à vérifier |
| 5 | TUNE | 2ND TUNE | Accord du second ton. | probable |
| 6 | CLIC | CLICK | Transitoire d'attaque. | probable |

#### ANA TOM 2 (aujourd'hui TRX-XC)

Autre nom : TIGHT TOM. Second tom d'analogique modélisée, aux mêmes réglages que ANA TOM (écouté : un tom). Confiance du nom : probable.

| # | Actuel | Proposé | Ce qu'il fait | Confiance |
|---|---|---|---|---|
| 1 | PTCH | TUNE | Hauteur du son. | sûr |
| 2 | DEC | DECAY | Durée de la chute du son. | sûr |
| 3 | RAMP | SWEEP | Chute de hauteur à l'attaque : le « piou » du coup. | probable |
| 4 | RDEC | SWEEP TIME | Durée de cette chute de hauteur. | probable |
| 5 | DAMP | DAMPING | Amortit la résonance : son plus mat, plus court. | probable |
| 6 | DIST | DRIVE | Saturation de la voix. | probable |
| 7 | DTYP | DRIVE TYPE | Genre de saturation. | probable |
| 8 | — | — | Non utilisé par la machine. | — |

#### DIRTY KICK (aujourd'hui TRX-B2)

Autre nom : HARD KICK. Seconde grosse caisse, tenue, avec clic, bruit et saleté : plus agressive. Confiance du nom : probable.

| # | Actuel | Proposé | Ce qu'il fait | Confiance |
|---|---|---|---|---|
| 1 | PTCH | TUNE | Hauteur du son. | sûr |
| 2 | DEC | DECAY | Durée de la chute du son. | sûr |
| 3 | RAMP | SWEEP | Chute de hauteur à l'attaque : le « piou » du coup. | probable |
| 4 | HOLD | HOLD | Tenue avant la chute : kick plus long. | probable |
| 5 | TICK | CLICK | Transitoire d'attaque. | probable |
| 6 | NOIS | NOISE | Bruit mêlé au ton. | probable |
| 7 | DIRT | GRIT | Salissure ajoutée, distincte de DRIVE (confirmé à l'écoute). | sûr |
| 8 | DIST | DRIVE | Saturation de la voix. | probable |

#### SINE SNARE (aujourd'hui TRX-S2)

Autre nom : SINE SD. Caisse claire à corps de sinusoïde et bruit réglable en couleur et en genre (écouté). Confiance du nom : sûr.

| # | Actuel | Proposé | Ce qu'il fait | Confiance |
|---|---|---|---|---|
| 1 | PTCH | TUNE | Hauteur du son. | sûr |
| 2 | DEC | DECAY | Durée de la chute du son. | sûr |
| 3 | NOIS | NOISE | Bruit mêlé au ton. | probable |
| 4 | NDEC | NOISE DECAY | Durée du bruit. | probable |
| 5 | POWR | POWER | Écouté : aucune différence. | subtil — à vérifier |
| 6 | TUNE | BODY TUNE | Écouté : très très peu de différence. | subtil — à vérifier |
| 7 | NTUN | NOISE TUNE | Couleur du bruit. | probable |
| 8 | NTYP | NOISE TYPE | Genre du bruit. | probable |


### 4.10 FM PERC (Machinedrum EFM)

#### FM KICK (aujourd'hui EFM-BD)

Autre nom : CLICKY FM. Grosse caisse FM : une attaque claquante, un corps qui peut tinter. Confiance du nom : probable.

| # | Actuel | Proposé | Ce qu'il fait | Confiance |
|---|---|---|---|---|
| 1 | PTCH | TUNE | Hauteur du son. | sûr |
| 2 | DEC | DECAY | Durée de la chute du son. | sûr |
| 3 | RAMP | SWEEP | Chute de hauteur à l'attaque : le « piou » du coup. | probable |
| 4 | RDEC | SWEEP TIME | Durée de cette chute de hauteur. | probable |
| 5 | MOD | FM AMOUNT | Profondeur de la FM : plus d'harmoniques, plus de métal. | probable |
| 6 | MFRQ | FM FREQ | Fréquence du modulateur : change le caractère du métal. | probable |
| 7 | MDEC | FM DECAY | Durée de la FM : brillant à l'attaque seulement, ou tout du long. | probable |
| 8 | MFB | FM FDBK | Le modulateur se module lui-même : plus rugueux, vers le bruit. | probable |

#### FM SNARE (aujourd'hui EFM-SD)

Autre nom : BRIGHT SNR. Caisse claire FM, bruit à part, très brillante. Confiance du nom : probable.

| # | Actuel | Proposé | Ce qu'il fait | Confiance |
|---|---|---|---|---|
| 1 | PTCH | TUNE | Hauteur du son. | sûr |
| 2 | DEC | DECAY | Durée de la chute du son. | sûr |
| 3 | NOIS | NOISE | Bruit mêlé au ton. | probable |
| 4 | NDEC | NOISE DECAY | Durée du bruit. | probable |
| 5 | MOD | FM AMOUNT | Profondeur de la FM : plus d'harmoniques, plus de métal. | probable |
| 6 | MFRQ | FM FREQ | Fréquence du modulateur : change le caractère du métal. | probable |
| 7 | MDEC | FM DECAY | Durée de la FM : brillant à l'attaque seulement, ou tout du long. | probable |
| 8 | HPF | HPF | Passe-haut : retire le grave. | probable |

#### FM TOM (aujourd'hui EFM-XT)

Autre nom : BELL TOM. Tom FM, avec chute de hauteur et clic. Confiance du nom : probable.

| # | Actuel | Proposé | Ce qu'il fait | Confiance |
|---|---|---|---|---|
| 1 | PTCH | TUNE | Hauteur du son. | sûr |
| 2 | DEC | DECAY | Durée de la chute du son. | sûr |
| 3 | RAMP | SWEEP | Chute de hauteur à l'attaque : le « piou » du coup. | probable |
| 4 | RDEC | SWEEP TIME | Durée de cette chute de hauteur. | probable |
| 5 | MOD | FM AMOUNT | Profondeur de la FM : plus d'harmoniques, plus de métal. | probable |
| 6 | MFRQ | FM FREQ | Fréquence du modulateur : change le caractère du métal. | probable |
| 7 | MDEC | FM DECAY | Durée de la FM : brillant à l'attaque seulement, ou tout du long. | probable |
| 8 | CLIC | CLICK | Transitoire d'attaque. | probable |

#### FM CLAP (aujourd'hui EFM-CP)

Autre nom : ZAP CLAP. Clap FM, sec et métallique. Confiance du nom : probable.

| # | Actuel | Proposé | Ce qu'il fait | Confiance |
|---|---|---|---|---|
| 1 | PTCH | TUNE | Hauteur du son. | sûr |
| 2 | DEC | DECAY | Durée de la chute du son. | sûr |
| 3 | CLPS | CLAPS | Nombre de mains. | probable |
| 4 | CDEC | CLAP DECAY | Durée des mains. | probable |
| 5 | MOD | FM AMOUNT | Profondeur de la FM : plus d'harmoniques, plus de métal. | probable |
| 6 | MFRQ | FM FREQ | Fréquence du modulateur : change le caractère du métal. | probable |
| 7 | MDEC | FM DECAY | Durée de la FM : brillant à l'attaque seulement, ou tout du long. | probable |
| 8 | HPF | HPF | Passe-haut : retire le grave. | probable |

#### FM RIM (aujourd'hui EFM-RS)

Autre nom : RIM+SNARE. Rimshot FM mêlé d'une caisse claire réglable à part. Confiance du nom : probable.

| # | Actuel | Proposé | Ce qu'il fait | Confiance |
|---|---|---|---|---|
| 1 | PTCH | TUNE | Hauteur du son. | sûr |
| 2 | DEC | DECAY | Durée de la chute du son. | sûr |
| 3 | MOD | FM AMOUNT | Profondeur de la FM : plus d'harmoniques, plus de métal. | probable |
| 4 | HPF | HPF | Passe-haut : retire le grave. | probable |
| 5 | SNAR | SNARE | Niveau de la caisse claire mêlée. | probable |
| 6 | SPTC | SNARE TUNE | Sa hauteur. | probable |
| 7 | SDEC | SNARE DECAY | Sa durée. | probable |
| 8 | SMOD | SNARE FM | Sa FM. | probable |

#### FM COWBELL (aujourd'hui EFM-CB)

Autre nom : FM BELL. Cloche FM : de la cloche à vache au gong. Confiance du nom : probable.

| # | Actuel | Proposé | Ce qu'il fait | Confiance |
|---|---|---|---|---|
| 1 | PTCH | TUNE | Hauteur du son. | sûr |
| 2 | DEC | DECAY | Durée de la chute du son. | sûr |
| 3 | SNAP | CHOKE | Écouté : le son raccourcit. | probable |
| 4 | FB | FM FDBK | Réinjection du modulateur : plus rugueux. | probable |
| 5 | MOD | FM AMOUNT | Profondeur de la FM : plus d'harmoniques, plus de métal. | probable |
| 6 | MFRQ | FM FREQ | Fréquence du modulateur : change le caractère du métal. | probable |
| 7 | MDEC | FM DECAY | Durée de la FM : brillant à l'attaque seulement, ou tout du long. | probable |
| 8 | — | — | Non utilisé par la machine. | — |

#### FM HAT (aujourd'hui EFM-HH)

Autre nom : TIN HAT. Charleston FM à trémolo : métal fin et vibrant. Confiance du nom : probable.

| # | Actuel | Proposé | Ce qu'il fait | Confiance |
|---|---|---|---|---|
| 1 | PTCH | TUNE | Hauteur du son. | sûr |
| 2 | DEC | DECAY | Durée de la chute du son. | sûr |
| 3 | TREM | TREMOLO | Battement d'amplitude. | probable |
| 4 | TFRQ | TREM RATE | Vitesse du battement. | probable |
| 5 | MOD | FM AMOUNT | Profondeur de la FM : plus d'harmoniques, plus de métal. | probable |
| 6 | MFRQ | FM FREQ | Fréquence du modulateur : change le caractère du métal. | probable |
| 7 | MDEC | FM DECAY | Durée de la FM : brillant à l'attaque seulement, ou tout du long. | probable |
| 8 | FB | FM FDBK | Réinjection du modulateur : plus rugueux. | probable |

#### FM CYMBAL (aujourd'hui EFM-CY)

Autre nom : FM CRASH. Cymbale FM, filtrable par le bas. Confiance du nom : probable.

| # | Actuel | Proposé | Ce qu'il fait | Confiance |
|---|---|---|---|---|
| 1 | PTCH | TUNE | Hauteur du son. | sûr |
| 2 | DEC | DECAY | Durée de la chute du son. | sûr |
| 3 | FB | FM FDBK | Réinjection du modulateur : plus rugueux. | probable |
| 4 | HPF | HPF | Passe-haut : retire le grave. | probable |
| 5 | MOD | FM AMOUNT | Profondeur de la FM : plus d'harmoniques, plus de métal. | probable |
| 6 | MFRQ | FM FREQ | Fréquence du modulateur : change le caractère du métal. | probable |
| 7 | MDEC | FM DECAY | Durée de la FM : brillant à l'attaque seulement, ou tout du long. | probable |
| 8 | — | — | Non utilisé par la machine. | — |


### 4.11 CRUNCH 12 (Machinedrum E12)

#### LOFI KICK (aujourd'hui E12-BD)

Autre nom : CRUNCH BD. Grosse caisse échantillonnée en 12 bits, avec un clic à part. Confiance du nom : sûr.

| # | Actuel | Proposé | Ce qu'il fait | Confiance |
|---|---|---|---|---|
| 1 | PTCH | TUNE | Hauteur du son. | sûr |
| 2 | DEC | DECAY | Durée de la chute du son. | sûr |
| 3 | SNAP | CLICK | Écouté : très peu de changement. | subtil — à vérifier |
| 4 | SPLN | CLICK DECAY | Écouté : très peu de changement. | subtil — à vérifier |
| 5 | STRT | START | Point de départ dans l'échantillon : coupe l'attaque. | sûr |
| 6 | RTRG | REPEATS | Nombre de répétitions du coup : roulement, flam. | probable |
| 7 | RTIM | REPEAT GAP | Écart entre ces répétitions. | probable |
| 8 | BEND | BEND | Glissement de hauteur pendant le son. | probable |

#### LOFI SNARE (aujourd'hui E12-SD)

Autre nom : CRUNCH SD. Caisse claire 12 bits. Confiance du nom : sûr.

| # | Actuel | Proposé | Ce qu'il fait | Confiance |
|---|---|---|---|---|
| 1 | PTCH | TUNE | Hauteur du son. | sûr |
| 2 | DEC | DECAY | Durée de la chute du son. | sûr |
| 3 | HP | HPF | Passe-haut sur l'échantillon : retire le grave. | probable |
| 4 | RING | RING | Écouté : très peu de changement. | subtil — à vérifier |
| 5 | STRT | START | Point de départ dans l'échantillon : coupe l'attaque. | sûr |
| 6 | RTRG | REPEATS | Nombre de répétitions du coup : roulement, flam. | probable |
| 7 | RTIM | REPEAT GAP | Écart entre ces répétitions. | probable |
| 8 | BEND | BEND | Glissement de hauteur pendant le son. | probable |

#### LOFI HITOM (aujourd'hui E12-HT)

Autre nom : HIGH TOM. Tom aigu 12 bits. Confiance du nom : sûr.

| # | Actuel | Proposé | Ce qu'il fait | Confiance |
|---|---|---|---|---|
| 1 | PTCH | TUNE | Hauteur du son. | sûr |
| 2 | DEC | DECAY | Durée de la chute du son. | sûr |
| 3 | HP | HPF | Passe-haut sur l'échantillon : retire le grave. | probable |
| 4 | HPQ | HPF RESO | Résonance du passe-haut : un pic à sa fréquence. | probable |
| 5 | STRT | START | Point de départ dans l'échantillon : coupe l'attaque. | sûr |
| 6 | RTRG | REPEATS | Nombre de répétitions du coup : roulement, flam. | probable |
| 7 | RTIM | REPEAT GAP | Écart entre ces répétitions. | probable |
| 8 | BEND | BEND | Glissement de hauteur pendant le son. | probable |

#### LOFI LOTOM (aujourd'hui E12-LT)

Autre nom : LOW TOM. Tom grave 12 bits. Confiance du nom : sûr.

| # | Actuel | Proposé | Ce qu'il fait | Confiance |
|---|---|---|---|---|
| 1 | PTCH | TUNE | Hauteur du son. | sûr |
| 2 | DEC | DECAY | Durée de la chute du son. | sûr |
| 3 | HP | HPF | Passe-haut sur l'échantillon : retire le grave. | probable |
| 4 | HPQ | HPF RESO | Résonance du passe-haut : un pic à sa fréquence. | probable |
| 5 | STRT | START | Point de départ dans l'échantillon : coupe l'attaque. | sûr |
| 6 | RTRG | REPEATS | Nombre de répétitions du coup : roulement, flam. | probable |
| 7 | RTIM | REPEAT GAP | Écart entre ces répétitions. | probable |
| 8 | BEND | BEND | Glissement de hauteur pendant le son. | probable |

#### LOFI CLAP (aujourd'hui E12-CP)

Autre nom : CRUNCH CP. Clap 12 bits. Confiance du nom : sûr.

| # | Actuel | Proposé | Ce qu'il fait | Confiance |
|---|---|---|---|---|
| 1 | PTCH | TUNE | Hauteur du son. | sûr |
| 2 | DEC | DECAY | Durée de la chute du son. | sûr |
| 3 | HP | HPF | Passe-haut sur l'échantillon : retire le grave. | probable |
| 4 | HPQ | HPF RESO | Résonance du passe-haut : un pic à sa fréquence. | probable |
| 5 | STRT | START | Point de départ dans l'échantillon : coupe l'attaque. | sûr |
| 6 | RTRG | REPEATS | Nombre de répétitions du coup : roulement, flam. | probable |
| 7 | RTIM | REPEAT GAP | Écart entre ces répétitions. | probable |
| 8 | BEND | BEND | Glissement de hauteur pendant le son. | probable |

#### LOFI RIM (aujourd'hui E12-RS)

Autre nom : CRUNCH RS. Rimshot 12 bits. Confiance du nom : sûr.

| # | Actuel | Proposé | Ce qu'il fait | Confiance |
|---|---|---|---|---|
| 1 | PTCH | TUNE | Hauteur du son. | sûr |
| 2 | DEC | DECAY | Durée de la chute du son. | sûr |
| 3 | HP | HPF | Passe-haut sur l'échantillon : retire le grave. | probable |
| 4 | RATL | RATTLE | Écouté : n'agit vraiment qu'à 127 : le moteur la lit sur une courbe exponentielle (`curve`, e^(9(x-1))), presque nulle avant le haut. | sûr |
| 5 | STRT | START | Point de départ dans l'échantillon : coupe l'attaque. | sûr |
| 6 | RTRG | REPEATS | Nombre de répétitions du coup : roulement, flam. | probable |
| 7 | RTIM | REPEAT GAP | Écart entre ces répétitions. | probable |
| 8 | BEND | BEND | Glissement de hauteur pendant le son. | probable |

#### LOFI COW (aujourd'hui E12-CB)

Autre nom : CRUNCH CB. Cloche à vache 12 bits. Confiance du nom : sûr.

| # | Actuel | Proposé | Ce qu'il fait | Confiance |
|---|---|---|---|---|
| 1 | PTCH | TUNE | Hauteur du son. | sûr |
| 2 | DEC | DECAY | Durée de la chute du son. | sûr |
| 3 | HP | HPF | Passe-haut sur l'échantillon : retire le grave. | probable |
| 4 | HPQ | HPF RESO | Résonance du passe-haut : un pic à sa fréquence. | probable |
| 5 | STRT | START | Point de départ dans l'échantillon : coupe l'attaque. | sûr |
| 6 | RTRG | REPEATS | Nombre de répétitions du coup : roulement, flam. | probable |
| 7 | RTIM | REPEAT GAP | Écart entre ces répétitions. | probable |
| 8 | BEND | BEND | Glissement de hauteur pendant le son. | probable |

#### LOFI HAT (aujourd'hui E12-CH)

Autre nom : CRUNCH CH. Charleston fermé 12 bits. Confiance du nom : sûr.

| # | Actuel | Proposé | Ce qu'il fait | Confiance |
|---|---|---|---|---|
| 1 | PTCH | TUNE | Hauteur du son. | sûr |
| 2 | DEC | DECAY | Durée de la chute du son. | sûr |
| 3 | HP | HPF | Passe-haut sur l'échantillon : retire le grave. | probable |
| 4 | HPQ | HPF RESO | Résonance du passe-haut : un pic à sa fréquence. | probable |
| 5 | STRT | START | Point de départ dans l'échantillon : coupe l'attaque. | sûr |
| 6 | RTRG | REPEATS | Nombre de répétitions du coup : roulement, flam. | probable |
| 7 | RTIM | REPEAT GAP | Écart entre ces répétitions. | probable |
| 8 | BEND | BEND | Glissement de hauteur pendant le son. | probable |

#### LOFI OPEN (aujourd'hui E12-OH)

Autre nom : CRUNCH OH. Charleston ouvert 12 bits. Confiance du nom : sûr.

| # | Actuel | Proposé | Ce qu'il fait | Confiance |
|---|---|---|---|---|
| 1 | PTCH | TUNE | Hauteur du son. | sûr |
| 2 | DEC | DECAY | Durée de la chute du son. | sûr |
| 3 | HP | HPF | Passe-haut sur l'échantillon : retire le grave. | probable |
| 4 | STOP | LENGTH | Écouté : durée du charleston ouvert, trop court sous 96. | sûr |
| 5 | STRT | START | Point de départ (confirmé à l'écoute). | sûr |
| 6 | RTRG | REPEATS | Nombre de répétitions du coup : roulement, flam. | probable |
| 7 | RTIM | REPEAT GAP | Écart entre ces répétitions. | probable |
| 8 | BEND | BEND | Glissement de hauteur pendant le son. | probable |

#### LOFI RIDE (aujourd'hui E12-RC)

Autre nom : CRUNCH RD. Ride 12 bits. Confiance du nom : sûr.

| # | Actuel | Proposé | Ce qu'il fait | Confiance |
|---|---|---|---|---|
| 1 | PTCH | TUNE | Hauteur du son. | sûr |
| 2 | DEC | DECAY | Durée de la chute du son. | sûr |
| 3 | HP | HPF | Passe-haut sur l'échantillon : retire le grave. | probable |
| 4 | BELL | BELL | Niveau de la cloche de la ride. | probable |
| 5 | STRT | START | Point de départ dans l'échantillon : coupe l'attaque. | sûr |
| 6 | RTRG | REPEATS | Nombre de répétitions du coup : roulement, flam. | probable |
| 7 | RTIM | REPEAT GAP | Écart entre ces répétitions. | probable |
| 8 | BEND | BEND | Glissement de hauteur pendant le son. | probable |

#### LOFI CRASH (aujourd'hui E12-CC)

Autre nom : CRUNCH CC. Crash 12 bits. Confiance du nom : sûr.

| # | Actuel | Proposé | Ce qu'il fait | Confiance |
|---|---|---|---|---|
| 1 | PTCH | TUNE | Hauteur du son. | sûr |
| 2 | DEC | DECAY | Durée de la chute du son. | sûr |
| 3 | HP | HPF | Passe-haut sur l'échantillon : retire le grave. | probable |
| 4 | HPQ | HPF RESO | Résonance du passe-haut : un pic à sa fréquence. | probable |
| 5 | STRT | START | Point de départ dans l'échantillon : coupe l'attaque. | sûr |
| 6 | RTRG | REPEATS | Nombre de répétitions du coup : roulement, flam. | probable |
| 7 | RTIM | REPEAT GAP | Écart entre ces répétitions. | probable |
| 8 | BEND | BEND | Glissement de hauteur pendant le son. | probable |

#### LOFI BRUSH (aujourd'hui E12-BR)

Autre nom : BRUSH. Balais 12 bits. Confiance du nom : probable.

| # | Actuel | Proposé | Ce qu'il fait | Confiance |
|---|---|---|---|---|
| 1 | PTCH | TUNE | Hauteur du son. | sûr |
| 2 | DEC | DECAY | Durée de la chute du son. | sûr |
| 3 | HP | HPF | Passe-haut sur l'échantillon : retire le grave. | probable |
| 4 | REAL | GAIN | Écouté : le son est juste plus fort. | probable |
| 5 | STRT | START | Point de départ dans l'échantillon : coupe l'attaque. | sûr |
| 6 | RTRG | REPEATS | Nombre de répétitions du coup : roulement, flam. | probable |
| 7 | RTIM | REPEAT GAP | Écart entre ces répétitions. | probable |
| 8 | BEND | BEND | Glissement de hauteur pendant le son. | probable |

#### LOFI TAMB (aujourd'hui E12-TA)

Autre nom : TAMBOURINE. Tambourin 12 bits. Confiance du nom : probable.

| # | Actuel | Proposé | Ce qu'il fait | Confiance |
|---|---|---|---|---|
| 1 | PTCH | TUNE | Hauteur du son. | sûr |
| 2 | DEC | DECAY | Durée de la chute du son. | sûr |
| 3 | HP | HPF | Passe-haut sur l'échantillon : retire le grave. | probable |
| 4 | HPQ | HPF RESO | Résonance du passe-haut : un pic à sa fréquence. | probable |
| 5 | STRT | START | Point de départ dans l'échantillon : coupe l'attaque. | sûr |
| 6 | RTRG | REPEATS | Nombre de répétitions du coup : roulement, flam. | probable |
| 7 | RTIM | REPEAT GAP | Écart entre ces répétitions. | probable |
| 8 | BEND | BEND | Glissement de hauteur pendant le son. | probable |

#### LOFI TRI (aujourd'hui E12-TR)

Autre nom : TRIANGLE. Triangle 12 bits (confirmé à l'écoute). Confiance du nom : sûr.

| # | Actuel | Proposé | Ce qu'il fait | Confiance |
|---|---|---|---|---|
| 1 | PTCH | TUNE | Hauteur du son. | sûr |
| 2 | DEC | DECAY | Durée de la chute du son. | sûr |
| 3 | HP | HPF | Passe-haut sur l'échantillon : retire le grave. | probable |
| 4 | HPQ | HPF RESO | Résonance du passe-haut : un pic à sa fréquence. | probable |
| 5 | STRT | START | Point de départ dans l'échantillon : coupe l'attaque. | sûr |
| 6 | RTRG | REPEATS | Nombre de répétitions du coup : roulement, flam. | probable |
| 7 | RTIM | REPEAT GAP | Écart entre ces répétitions. | probable |
| 8 | BEND | BEND | Glissement de hauteur pendant le son. | probable |

#### LOFI SHAKE (aujourd'hui E12-SH)

Autre nom : SHAKER. Shaker 12 bits. Confiance du nom : probable.

| # | Actuel | Proposé | Ce qu'il fait | Confiance |
|---|---|---|---|---|
| 1 | PTCH | TUNE | Hauteur du son. | sûr |
| 2 | DEC | DECAY | Durée de la chute du son. | sûr |
| 3 | HP | HPF | Passe-haut sur l'échantillon : retire le grave. | probable |
| 4 | SLEW | SLEW | Confirmé à l'écoute. | sûr |
| 5 | STRT | START | Point de départ dans l'échantillon : coupe l'attaque. | sûr |
| 6 | RTRG | REPEATS | Nombre de répétitions du coup : roulement, flam. | probable |
| 7 | RTIM | REPEAT GAP | Écart entre ces répétitions. | probable |
| 8 | BEND | BEND | Glissement de hauteur pendant le son. | probable |

#### LOFI BONGO (aujourd'hui E12-BC)

Autre nom : BONGO. Bongo ou conga 12 bits (confirmé à l'écoute). Confiance du nom : sûr.

| # | Actuel | Proposé | Ce qu'il fait | Confiance |
|---|---|---|---|---|
| 1 | PTCH | TUNE | Hauteur du son. | sûr |
| 2 | DEC | DECAY | Durée de la chute du son. | sûr |
| 3 | HP | HPF | Passe-haut sur l'échantillon : retire le grave. | probable |
| 4 | BC | HPF 2 | Écouté : sonne comme un passe-haut, en plus de HPF. | probable |
| 5 | STRT | START | Point de départ dans l'échantillon : coupe l'attaque. | sûr |
| 6 | RTRG | REPEATS | Nombre de répétitions du coup : roulement, flam. | probable |
| 7 | RTIM | REPEAT GAP | Écart entre ces répétitions. | probable |
| 8 | BEND | BEND | Glissement de hauteur pendant le son. | probable |


### 4.12 PHYSICAL (Machinedrum P-I)

#### REAL KICK (aujourd'hui P-I-BD)

Autre nom : SKIN KICK. Grosse caisse en modèle physique : une peau frappée par une mailloche. Confiance du nom : probable.

| # | Actuel | Proposé | Ce qu'il fait | Confiance |
|---|---|---|---|---|
| 1 | PTCH | TUNE | Hauteur du son. | sûr |
| 2 | DEC | DECAY | Durée de la chute du son. | sûr |
| 3 | HARD | HARDNESS | Dureté de l'attaque (de la mailloche pour les modèles physiques). | probable |
| 4 | HAMR | STRIKE | Force du coup de mailloche. | probable |
| 5 | TENS | TENSION | Tension de la peau : hauteur et rebond. | probable |
| 6 | DAMP | DAMPING | Amortit la résonance : son plus mat, plus court. | probable |
| 7 | — | — | Non utilisé par la machine. | — |
| 8 | — | — | Non utilisé par la machine. | — |

#### REAL SNARE (aujourd'hui P-I-SD)

Autre nom : SKIN SNARE. Caisse claire en modèle physique, sa résonance réglable à part. Confiance du nom : probable.

| # | Actuel | Proposé | Ce qu'il fait | Confiance |
|---|---|---|---|---|
| 1 | PTCH | TUNE | Hauteur du son. | sûr |
| 2 | DEC | DECAY | Durée de la chute du son. | sûr |
| 3 | HARD | HARDNESS | Dureté de l'attaque (de la mailloche pour les modèles physiques). | probable |
| 4 | RING | RING | Résonance qui sonne après la frappe. | probable |
| 5 | TENS | TENSION | Tension de la peau : hauteur et rebond. | probable |
| 6 | RVOL | RING LEVEL | Niveau de cette résonance. | probable |
| 7 | RDEC | RING DECAY | Durée de cette résonance. | probable |
| 8 | — | — | Non utilisé par la machine. | — |

#### REAL TOM (aujourd'hui P-I-MT)

Autre nom : SKIN TOM. Tom en modèle physique : taille, position de frappe. Confiance du nom : probable.

| # | Actuel | Proposé | Ce qu'il fait | Confiance |
|---|---|---|---|---|
| 1 | PTCH | TUNE | Hauteur du son. | sûr |
| 2 | DEC | DECAY | Durée de la chute du son. | sûr |
| 3 | HARD | HARDNESS | Dureté de l'attaque (de la mailloche pour les modèles physiques). | probable |
| 4 | HAMR | STRIKE | Force du coup de mailloche. | probable |
| 5 | TUNE | TUNE 2 | Accord d'un second mode (confirmé à l'écoute). | sûr |
| 6 | DAMP | DAMPING | Amortit la résonance : son plus mat, plus court. | probable |
| 7 | SIZE | SIZE | Taille de l'instrument : plus grand, plus grave et plus long. | probable |
| 8 | POS | POSITION | Où la mailloche frappe la peau : du centre au bord. | probable |

#### MALLET (aujourd'hui P-I-ML)

Autre nom : WOOD BAR. Instrument à lames ou peau tendue frappé à la mailloche (confirmé à l'écoute). Confiance du nom : sûr.

| # | Actuel | Proposé | Ce qu'il fait | Confiance |
|---|---|---|---|---|
| 1 | PTCH | TUNE | Hauteur du son. | sûr |
| 2 | DEC | DECAY | Durée de la chute du son. | sûr |
| 3 | HARD | HARDNESS | Dureté de l'attaque (de la mailloche pour les modèles physiques). | probable |
| 4 | TENS | TENSION | Tension de la peau : hauteur et rebond. | probable |

#### SHAKER (aujourd'hui P-I-MA)

Autre nom : SEED SHAKE. Maracas en modèle physique : des grains dans une coque. Confiance du nom : probable.

| # | Actuel | Proposé | Ce qu'il fait | Confiance |
|---|---|---|---|---|
| 1 | GRNS | GRAINS | Nombre de grains. | probable |
| 2 | DEC | DECAY | Durée de la chute du son. | sûr |
| 3 | GLEN | GRAIN TIME | Durée d'un grain. | probable |
| 4 | — | — | Non utilisé par la machine. | — |
| 5 | SIZE | SIZE | Taille de l'instrument : plus grand, plus grave et plus long. | probable |
| 6 | HARD | HARDNESS | Dureté de l'attaque (de la mailloche pour les modèles physiques). | probable |
| 7 | — | — | Non utilisé par la machine. | — |
| 8 | — | — | Non utilisé par la machine. | — |

#### REAL RIM (aujourd'hui P-I-RS)

Autre nom : WOOD RIM. Rimshot en modèle physique, sa résonance réglable. Confiance du nom : probable.

| # | Actuel | Proposé | Ce qu'il fait | Confiance |
|---|---|---|---|---|
| 1 | PTCH | TUNE | Hauteur du son. | sûr |
| 2 | DEC | DECAY | Durée de la chute du son. | sûr |
| 3 | HARD | HARDNESS | Dureté de l'attaque (de la mailloche pour les modèles physiques). | probable |
| 4 | RING | RING | Résonance qui sonne après la frappe. | probable |
| 5 | RVOL | RING LEVEL | Niveau de cette résonance. | probable |
| 6 | RDEC | RING DECAY | Durée de cette résonance. | probable |

#### REAL RIDE (aujourd'hui P-I-RC)

Autre nom : BRASS RIDE. Ride en modèle physique, qu'une main peut saisir. Confiance du nom : probable.

| # | Actuel | Proposé | Ce qu'il fait | Confiance |
|---|---|---|---|---|
| 1 | PTCH | TUNE | Hauteur du son. | sûr |
| 2 | DEC | DECAY | Durée de la chute du son. | sûr |
| 3 | HARD | HARDNESS | Dureté de l'attaque (de la mailloche pour les modèles physiques). | probable |
| 4 | RING | RING | Résonance qui sonne après la frappe. | probable |
| 5 | AG | BODY | Écouté : plus de volume et de longueur. | probable |
| 6 | AU | TONE | Écouté : un filtre. | probable |
| 7 | BR | BRIGHTNESS | Écouté : un filtre. | probable |
| 8 | GRAB | LENGTH | Écouté : allonge le son. | sûr |

#### REAL CRASH (aujourd'hui P-I-CC)

Autre nom : BRASS CRSH. Crash en modèle physique, qu'une main peut saisir. Confiance du nom : probable.

| # | Actuel | Proposé | Ce qu'il fait | Confiance |
|---|---|---|---|---|
| 1 | PTCH | TUNE | Hauteur du son. | sûr |
| 2 | DEC | DECAY | Durée de la chute du son. | sûr |
| 3 | HARD | HARDNESS | Dureté de l'attaque (de la mailloche pour les modèles physiques). | probable |
| 4 | RING | RING | Résonance qui sonne après la frappe. | probable |
| 5 | AG | BODY | Écouté : trop peu de changement pour juger ; nom repris de REAL RIDE. | subtil — à vérifier |
| 6 | AU | TONE | Écouté : trop peu de changement pour juger ; nom repris de REAL RIDE. | subtil — à vérifier |
| 7 | BR | BRIGHTNESS | Écouté : un filtre. | probable |
| 8 | GRAB | LENGTH | Écouté : allonge le son. | sûr |

#### REAL HAT (aujourd'hui P-I-HH)

Autre nom : BRASS HAT. Charleston en modèle physique, de fermé à ouvert. Confiance du nom : probable.

| # | Actuel | Proposé | Ce qu'il fait | Confiance |
|---|---|---|---|---|
| 1 | PTCH | TUNE | Hauteur du son. | sûr |
| 2 | DEC | DECAY | Durée de la chute du son. | sûr |
| 3 | CLSN | CLOSED | Écouté : trop peu de changement pour juger. | subtil — à vérifier |
| 4 | RING | RING | Résonance qui sonne après la frappe. | probable |
| 5 | AG | BODY | Écouté : trop peu de changement pour juger ; nom repris de REAL RIDE. | subtil — à vérifier |
| 6 | AU | TONE | Écouté : un filtre. | probable |
| 7 | BR | HPF | Écouté : un passe-haut. | probable |
| 8 | CLOS | LENGTH | Écouté : durée du son. | sûr |


### 4.13 ROM BANK et Sampler

#### ROM-01 … (aujourd'hui ROM-01 … ROM-32)

Autre nom : nom de l'échantillon. Les échantillons de la banque d'usine, par numéro. Montrer le nom de l'échantillon que la banque porte, s'il en porte un (à vérifier dans l'image). Confiance du nom : probable.

| # | Actuel | Proposé | Ce qu'il fait | Confiance |
|---|---|---|---|---|
| 1 | PTCH | TUNE | Hauteur du son. | sûr |
| 2 | DEC | DECAY | Durée de la chute du son. | sûr |
| 3 | HOLD | HOLD | Tenue au plein niveau avant la chute. | probable |
| 4 | BRR | BIT CRUSH | Réduit la résolution : grain numérique. | probable |
| 5 | STRT | START | Point de départ dans l'échantillon : coupe l'attaque. | sûr |
| 6 | END | END | Point de fin dans l'échantillon. | sûr |
| 7 | RTRG | REPEATS | Nombre de répétitions du coup : roulement, flam. | probable |
| 8 | RTIM | REPEAT GAP | Écart entre ces répétitions. | probable |

#### SAMPLER (aujourd'hui SAMPLER)

Autre nom : —. L'échantillon de la piste sur le lecteur des ROM. Étiquettes actuelles TUNE, DECAY, HOLD, BR, START, END, RTRG, RTIM. Confiance du nom : sûr.

| # | Actuel | Proposé | Ce qu'il fait | Confiance |
|---|---|---|---|---|
| 1 | TUNE | TUNE | Hauteur du son. | sûr |
| 2 | DECAY | DECAY | Durée de la chute du son. | sûr |
| 3 | HOLD | HOLD | Tenue au plein niveau avant la chute. | sûr |
| 4 | BR | BIT CRUSH | Réduit la résolution : grain numérique. | probable |
| 5 | START | START | Point de départ dans l'échantillon : coupe l'attaque. | sûr |
| 6 | END | END | Point de fin dans l'échantillon. | sûr |
| 7 | RTRG | REPEATS | Nombre de répétitions du coup : roulement, flam. | probable |
| 8 | RTIM | REPEAT GAP | Écart entre ces répétitions. | probable |


### 4.14 MODERN DIGI (Syntakt)

#### PUNCH KICK (aujourd'hui BD MODERN)

Autre nom : CLEAN KICK. Grosse caisse numérique moderne : attaque nette, corps modulable. Confiance du nom : probable.

| # | Actuel | Proposé | Ce qu'il fait | Confiance |
|---|---|---|---|---|
| 1 | TUNE | TUNE | Hauteur du son. | sûr |
| 2 | SWEEP | SWEEP | Chute de hauteur à l'attaque. | probable |
| 3 | PUNCH | PUNCH | Force de l'attaque. | probable |
| 4 | DECAY | DECAY | Durée de la chute du son. | sûr |
| 5 | SQR MOD | SQUARE MOD | Modulation par une onde carrée (confirmé à l'écoute). | sûr |
| 6 | TRI MOD | TRI MOD | Modulation par un triangle (confirmé à l'écoute). | sûr |
| 7 | MOD ENV | MOD ENV | Enveloppe de ces modulations (confirmé à l'écoute). | sûr |
| 8 | OVERDRIVE | DRIVE | Saturation de la voix. | sûr |

#### DIGI SNARE (aujourd'hui SD BASIC)

Autre nom : FM SNARE 2. Caisse claire numérique à partiels et FM réinjectée. Confiance du nom : probable.

| # | Actuel | Proposé | Ce qu'il fait | Confiance |
|---|---|---|---|---|
| 1 | TUNE | TUNE | Hauteur du son. | sûr |
| 2 | SWEEP | SWEEP | Chute de hauteur à l'attaque. | probable |
| 3 | PUNCH | PUNCH | Force de l'attaque. | probable |
| 4 | DECAY | DECAY | Durée de la chute du son. | sûr |
| 5 | INHARM | INHARMONIC | Écarte les partiels de la série harmonique : du ton au métal. | probable |
| 6 | MOD FDBK | FM FDBK | Réinjection de la modulation : plus rugueux. | probable |
| 7 | MOD ENV | FM ENV | Enveloppe de la modulation : brillant à l'attaque seulement, ou tout du long. | probable |
| 8 | OVERDRIVE | DRIVE | Saturation de la voix. | sûr |

#### SHINY CYM (aujourd'hui CY ALLOY)

Autre nom : BELL CYM. Cymbale numérique, métal scintillant. Confiance du nom : probable.

| # | Actuel | Proposé | Ce qu'il fait | Confiance |
|---|---|---|---|---|
| 1 | TUNE | TUNE | Hauteur du son. | sûr |
| 2 | SHIMMER | SHIMMER | Scintillement des aigus (confirmé à l'écoute). | sûr |
| 3 | PUNCH | PUNCH | Force de l'attaque. | probable |
| 4 | DECAY | DECAY | Durée de la chute du son. | sûr |
| 5 | RADIO | RADIO | Écouté : trop peu de changement pour juger. | subtil — à vérifier |
| 6 | MOD | FM AMOUNT | Profondeur de modulation (table « Depth » du port). | probable |
| 7 | MOD ENV | FM ENV | Enveloppe de la modulation : brillant à l'attaque seulement, ou tout du long. | probable |
| 8 | OVERDRIVE | DRIVE | Saturation de la voix. | sûr |

#### TONAL PERC (aujourd'hui PC CARBON)

Autre nom : WOOD PERC. Percussion accordée : bois, verre, métal selon les partiels. Confiance du nom : probable.

| # | Actuel | Proposé | Ce qu'il fait | Confiance |
|---|---|---|---|---|
| 1 | TUNE | TUNE | Hauteur du son. | sûr |
| 2 | SWEEP | SWEEP | Chute de hauteur à l'attaque. | probable |
| 3 | PUNCH | PUNCH | Force de l'attaque. | probable |
| 4 | DECAY | DECAY | Durée de la chute du son. | sûr |
| 5 | INHARM | INHARMONIC | Écarte les partiels de la série harmonique : du ton au métal. | probable |
| 6 | MOD | FM AMOUNT | Profondeur de modulation. | probable |
| 7 | MOD ENV | FM ENV | Enveloppe de la modulation : brillant à l'attaque seulement, ou tout du long. | probable |
| 8 | OVERDRIVE | DRIVE | Saturation de la voix. | sûr |

#### OLD SNARE (aujourd'hui SD VINTAGE)

Autre nom : SOFT SNARE. Caisse claire à trois sinusoïdes et bruit : le son des boîtes à rythmes analogiques. Confiance du nom : probable.

| # | Actuel | Proposé | Ce qu'il fait | Confiance |
|---|---|---|---|---|
| 1 | TUNE | TUNE | Hauteur du son. | sûr |
| 2 | SWEEP | SWEEP | Chute de hauteur à l'attaque. | probable |
| 3 | PUNCH | PUNCH | Force de l'attaque. | probable |
| 4 | DECAY | DECAY | Durée de la chute du son. | sûr |
| 5 | INHARM | INHARMONIC | Écarte les partiels de la série harmonique : du ton au métal. | probable |
| 6 | COMPLEX | COMPLEX | Écouté : trop peu de changement pour juger. | subtil — à vérifier |
| 7 | MOD ENV | FM ENV | Enveloppe de la modulation : brillant à l'attaque seulement, ou tout du long. | probable |
| 8 | OVERDRIVE | DRIVE | Saturation de la voix. | sûr |

#### OLD CLAP (aujourd'hui CP VINTAGE)

Autre nom : CRACKLE CP. Clap de crépitement et de bruit blanc dans un résonateur (`stCpVintage.cpp:6`). Confiance du nom : probable.

| # | Actuel | Proposé | Ce qu'il fait | Confiance |
|---|---|---|---|---|
| 1 | TUNE | TUNE | Hauteur du son. | sûr |
| 2 | SPACING | CLAP GAP | Écart entre les bouffées du clap. | probable |
| 3 | PUNCH | PUNCH | Force de l'attaque. | probable |
| 4 | DECAY | DECAY | Durée de la chute du son. | sûr |
| 5 | BALANCE | BALANCE | Équilibre entre crépitement et bruit blanc. | probable |
| 6 | BODY | BODY | Niveau du ton mêlé au clap. | probable |
| 7 | BODY ENV | BODY DECAY | Durée de ce ton. | probable |
| 8 | OVERDRIVE | DRIVE | Saturation de la voix. | sûr |

#### TOY SYNTH (aujourd'hui SY TOY)

Autre nom : FM BLIP. Petite FM à cinq opérateurs : blips, cloches, jouets. Confiance du nom : probable.

| # | Actuel | Proposé | Ce qu'il fait | Confiance |
|---|---|---|---|---|
| 1 | TUNE | TUNE | Hauteur du son. | sûr |
| 2 | FORM | TUNE 2 | Écouté : sonne comme une hauteur, en plus de TUNE. | probable |
| 3 | PUNCH | PUNCH | Force de l'attaque. | probable |
| 4 | DECAY | DECAY | Durée de la chute du son. | sûr |
| 5 | IMPACT | LPF | Écouté : un passe-bas. | probable |
| 6 | BRIGHT | BRIGHTNESS | Brillance. | probable |
| 7 | PART DEC | PART DECAY | Durée des partiels. | probable |
| 8 | OVERDRIVE | DRIVE | Saturation de la voix. | sûr |

#### BIT SYNTH (aujourd'hui SY BITS)

Autre nom : CHIPTUNE. Quatre oscillateurs à formes d'onde, réduits en bits et en fréquence : son de console. Confiance du nom : probable.

| # | Actuel | Proposé | Ce qu'il fait | Confiance |
|---|---|---|---|---|
| 1 | TUNE | TUNE | Hauteur du son. | sûr |
| 2 | DETUNE | DETUNE | Désaccord des oscillateurs. | probable |
| 3 | BALANCE | BALANCE | Équilibre entre les deux paires d'oscillateurs. | probable |
| 4 | DECAY | DECAY | Durée de la chute du son. | sûr |
| 5 | WAVE | WAVE | Fondu entre deux formes d'onde (`stSyBits.cpp:6`). | probable |
| 6 | SR REDUX | DOWNSAMPLE | Réduit la fréquence d'échantillonnage : aliasing. | sûr |
| 7 | BIT REDUX | BIT CRUSH | Réduit la résolution. | sûr |
| 8 | OVERDRIVE | DRIVE | Saturation de la voix. | sûr |

#### DUO SAMPLE (aujourd'hui SP TWINSHOT)

Autre nom : TWO SHOTS. Deux lecteurs d'échantillons mêlés, le premier passe-bande (`stSpTwinshot.cpp:7`). Confiance du nom : sûr.

| # | Actuel | Proposé | Ce qu'il fait | Confiance |
|---|---|---|---|---|
| 1 | TUNE 1 | TUNE 1 | Hauteur du premier échantillon. | sûr |
| 2 | SAMPLE 1 | SAMPLE 1 | Choix du premier (64). | sûr |
| 3 | FILTER 1 | BAND 1 | Passe-bande du premier. | probable |
| 4 | DECAY 1 | DECAY 1 | Durée du premier. | sûr |
| 5 | TUNE 2 | TUNE 2 | Hauteur du second. | sûr |
| 6 | SAMPLE 2 | SAMPLE 2 | Choix du second. | sûr |
| 7 | MIX | BLEND | Fondu entre les deux. | sûr |
| 8 | OVERDRIVE | DRIVE | Saturation de la voix. | sûr |


### 4.15 VA DRUMS (OPEN DRUM)

#### VA DRUM (aujourd'hui OD DRUM)

Autre nom : BODY DRUM. Grosse caisse ou tom de synthèse : onde, FM, transitoire échantillonné. Confiance du nom : sûr.

| # | Actuel | Proposé | Ce qu'il fait | Confiance |
|---|---|---|---|---|
| 1 | TUNE | TUNE | Hauteur. | sûr |
| 2 | DECAY | DECAY | Durée de la chute. | sûr |
| 3 | PITCH DEC | SWEEP TIME | Durée de la chute de hauteur. | sûr |
| 4 | PITCH AMT | SWEEP | Profondeur de la chute de hauteur. | sûr |
| 5 | FM AMT | FM AMOUNT | Profondeur de la FM. | sûr |
| 6 | FM FREQ | FM FREQ | Fréquence du modulateur. | sûr |
| 7 | CLICK | CLICK | Niveau du transitoire d'attaque. | sûr |
| 8 | DRIVE | DRIVE | Saturation. | sûr |
| 9 | FINE | FINE TUNE | Accord fin. | sûr |
| 10 | WAVE | WAVE | Forme d'onde de l'oscillateur (6). | sûr |
| 11 | FM WAVE | FM WAVE | Forme d'onde du modulateur. | sûr |
| 12 | FM/MIX | MOD MODE | Le second oscillateur module (FM) ou s'ajoute (MIX). | probable |
| 13 | ATTACK | ATTACK | Montée du volume. | sûr |
| 14 | DEC SLOPE | DECAY SHAPE | Courbe de la chute. | probable |
| 15 | PITCH SLP | SWEEP SHAPE | Courbe de la chute de hauteur. | probable |
| 16 | PWM | PULSE WIDTH | Largeur d'impulsion de l'onde. | sûr |
| 17 | FILTER | CUTOFF | Fréquence du filtre de la voix. | sûr |
| 18 | RESO | RESO | Résonance de ce filtre. | sûr |
| 19 | FILT TYPE | FILTER TYPE | Type de ce filtre (7). | sûr |
| 20 | FILT DRIVE | FILTER DRIVE | Saturation dans le filtre. | sûr |
| 21 | CLICK WAVE | CLICK WAVE | Choix du transitoire (14). | sûr |
| 22 | CLICK FREQ | CLICK TUNE | Hauteur du transitoire. | probable |

#### VA SNARE (aujourd'hui OD SNARE)

Autre nom : SYN SNARE. Caisse claire de synthèse : un ton, un bruit filtré, un transitoire. Confiance du nom : sûr.

| # | Actuel | Proposé | Ce qu'il fait | Confiance |
|---|---|---|---|---|
| 1 | TUNE | TUNE | Hauteur. | sûr |
| 2 | DECAY | DECAY | Durée de la chute. | sûr |
| 3 | PITCH DEC | SWEEP TIME | Durée de la chute de hauteur. | sûr |
| 4 | PITCH AMT | SWEEP | Profondeur de la chute de hauteur. | sûr |
| 5 | NOISE | NOISE TUNE | Couleur du bruit (sa fréquence). | probable |
| 6 | MIX | NOISE MIX | Part du bruit face au ton. | probable |
| 7 | FILTER | CUTOFF | Fréquence du filtre de la voix. | sûr |
| 8 | DRIVE | DRIVE | Saturation. | sûr |
| 9 | FINE | FINE TUNE | Accord fin. | sûr |
| 10 | WAVE | WAVE | Forme d'onde de l'oscillateur (6). | sûr |
| 11 | ATTACK | ATTACK | Montée du volume. | sûr |
| 12 | DEC SLOPE | DECAY SHAPE | Courbe de la chute. | probable |
| 13 | REPEAT | REPEATS | Répétitions du coup (confirmé à l'écoute). | sûr |
| 14 | PITCH SLP | SWEEP SHAPE | Courbe de la chute de hauteur. | probable |
| 15 | PWM | PULSE WIDTH | Largeur d'impulsion de l'onde. | sûr |
| 16 | RESO | RESO | Résonance de ce filtre. | sûr |
| 17 | FILT TYPE | FILTER TYPE | Type de ce filtre (7). | sûr |
| 18 | FILT DRIVE | FILTER DRIVE | Saturation dans le filtre. | sûr |
| 19 | CLICK | CLICK | Niveau du transitoire d'attaque. | sûr |
| 20 | CLICK WAVE | CLICK WAVE | Choix du transitoire (14). | sûr |
| 21 | CLICK FREQ | CLICK TUNE | Hauteur du transitoire. | probable |

#### VA CYMBAL (aujourd'hui OD CYMBAL)

Autre nom : SYN CYMBAL. Cymbale de synthèse : trois oscillateurs en FM. Confiance du nom : sûr.

| # | Actuel | Proposé | Ce qu'il fait | Confiance |
|---|---|---|---|---|
| 1 | TUNE | TUNE | Hauteur. | sûr |
| 2 | DECAY | DECAY | Durée de la chute. | sûr |
| 3 | MOD1 FREQ | FM1 FREQ | Fréquence du premier modulateur. | probable |
| 4 | MOD2 FREQ | FM2 FREQ | Fréquence du second. | probable |
| 5 | MOD1 AMT | FM1 AMOUNT | Profondeur du premier modulateur. | probable |
| 6 | MOD2 AMT | FM2 AMOUNT | Profondeur du second. | probable |
| 7 | FILTER | CUTOFF | Fréquence du filtre de la voix. | sûr |
| 8 | DRIVE | DRIVE | Saturation. | sûr |
| 9 | FINE | FINE TUNE | Accord fin. | sûr |
| 10 | WAVE | WAVE | Forme d'onde de l'oscillateur (6). | sûr |
| 11 | MOD1 WAVE | FM1 WAVE | Onde du premier modulateur. | probable |
| 12 | MOD2 WAVE | FM2 WAVE | Onde du second. | probable |
| 13 | ATTACK | ATTACK | Montée du volume. | sûr |
| 14 | DEC SLOPE | DECAY SHAPE | Courbe de la chute. | probable |
| 15 | REPEAT | REPEATS | Répétitions du coup (confirmé à l'écoute). | sûr |
| 16 | PWM | PULSE WIDTH | Largeur d'impulsion de l'onde. | sûr |
| 17 | RESO | RESO | Résonance de ce filtre. | sûr |
| 18 | FILT TYPE | FILTER TYPE | Type de ce filtre (7). | sûr |
| 19 | FILT DRIVE | FILTER DRIVE | Saturation dans le filtre. | sûr |
| 20 | CLICK | CLICK | Niveau du transitoire d'attaque. | sûr |
| 21 | CLICK WAVE | CLICK WAVE | Choix du transitoire (14). | sûr |
| 22 | CLICK FREQ | CLICK TUNE | Hauteur du transitoire. | probable |

#### VA HIHAT (aujourd'hui OD HIHAT)

Autre nom : SYN HAT. Charleston de synthèse, ouvert ou fermé, trois oscillateurs en FM. Confiance du nom : sûr.

| # | Actuel | Proposé | Ce qu'il fait | Confiance |
|---|---|---|---|---|
| 1 | TUNE | TUNE | Hauteur. | sûr |
| 2 | CLOSED | CLOSED DECAY | Chute du charleston fermé. | sûr |
| 3 | OPEN DEC | OPEN DECAY | Chute du charleston ouvert. | sûr |
| 4 | MOD1 FREQ | FM1 FREQ | Fréquence du premier modulateur. | probable |
| 5 | MOD1 AMT | FM1 AMOUNT | Profondeur du premier modulateur. | probable |
| 6 | FILTER | CUTOFF | Fréquence du filtre de la voix. | sûr |
| 7 | DRIVE | DRIVE | Saturation. | sûr |
| 8 | OPEN | OPEN | Ouvert ou fermé. | sûr |
| 9 | FINE | FINE TUNE | Accord fin. | sûr |
| 10 | WAVE | WAVE | Forme d'onde de l'oscillateur (6). | sûr |
| 11 | MOD1 WAVE | FM1 WAVE | Onde du premier modulateur. | probable |
| 12 | MOD2 WAVE | FM2 WAVE | Onde du second. | probable |
| 13 | MOD2 FREQ | FM2 FREQ | Fréquence du second. | probable |
| 14 | MOD2 AMT | FM2 AMOUNT | Profondeur du second. | probable |
| 15 | ATTACK | ATTACK | Montée du volume. | sûr |
| 16 | DEC SLOPE | DECAY SHAPE | Courbe de la chute. | probable |
| 17 | RESO | RESO | Résonance de ce filtre. | sûr |
| 18 | FILT TYPE | FILTER TYPE | Type de ce filtre (7). | sûr |
| 19 | FILT DRIVE | FILTER DRIVE | Saturation dans le filtre. | sûr |
| 20 | CLICK | CLICK | Niveau du transitoire d'attaque. | sûr |
| 21 | CLICK WAVE | CLICK WAVE | Choix du transitoire (14). | sûr |
| 22 | CLICK FREQ | CLICK TUNE | Hauteur du transitoire. | probable |
| 23 | PWM | PULSE WIDTH | Largeur d'impulsion de l'onde. | sûr |


### 4.16 HYPER FM (Sophie)

#### HYPER FM (aujourd'hui SOPHIE)

Autre nom : METAL FM. Percussion FM métallique, six topologies : du kick qui gronde à l'éclat de verre. Confiance du nom : sûr.

| # | Actuel | Proposé | Ce qu'il fait | Confiance |
|---|---|---|---|---|
| 1 | MODEL | MODEL | Topologie FM : Fuse, Stack, Split, Shard, Pipe, Boom. | sûr |
| 2 | TUNE | TUNE | ±24 demi-tons. | sûr |
| 3 | COLOR | COLOR | Timbre de la FM, 0-100 % (le détail : à écouter). | probable |
| 4 | METAL | METAL | Part inharmonique. | probable |
| 5 | FEEDBACK | FM FDBK | Réinjection d'opérateur. | probable |
| 6 | SWEEP | SWEEP | Chute ou montée de hauteur, ±100 %. | sûr |
| 7 | DECAY | DECAY | 0,03 à 4 s. | sûr |
| 8 | CRUSH | BIT CRUSH | Réduction de résolution. | probable |
| 9 | DRIVE | DRIVE | Saturation. | sûr |
| 10 | CUTOFF | CUTOFF | 40 Hz à 16 kHz. | sûr |
| 11 | RESO | RESO | Résonance. | sûr |
| 12 | FILTER | FILTER TYPE | LPF, HPF, Notch, DJ, BPF. | sûr |
| 13 | RING TIME | COMB TIME | Le « Ring » est une ligne à retard réinjectée (README) : un résonateur en peigne, 0,5 à 30 ms. | sûr |
| 14 | RING FDBK | COMB FDBK | Sa réinjection : plus long, plus métallique. | sûr |
| 15 | RING MIX | COMB MIX | Sa part dans le son. | sûr |
| 16 | RING TONE | COMB TONE | Sa couleur. | sûr |
| 17 | METAL DEC | METAL DECAY | Durée de la part métallique, ou off. | sûr |
| 18 | FB MODE | FDBK MODE | Raw ou Smooth. | sûr |
| 19 | TAIL | TAIL MODE | Hit : la queue garde le réglage du coup ; Live : elle suit les réglages. | sûr |
| 20 | BODY | KICK BODY | Joue le corps de grosse caisse à la place de la FM. | sûr |

### 4.17 Effets partagés

| Bloc | Nom actuel (hôte / cellule) | Court | Long | Ce qu'il fait | Confiance |
|---|---|---|---|---|---|
| Délai | Delay Source / SOURCE, MD Echo/Syntakt | SOURCE, DUB ECHO/STEREO DLY | Delay Source | | sûr |
| Délai | Delay Output, Sync | — | Delay Out, Delay Sync | | sûr |
| Délai | Delay Duration / DURATION | TIME | Delay Note Time | Temps en notes. | sûr |
| Délai | Delay Free Hz / HZ | TIME | Delay Free Time | Temps libre, montré en Hz. | sûr |
| DUB ECHO | Echo Time / TIME | — | Echo Time | | sûr |
| DUB ECHO | Echo Mod / MOD | MOD DEPTH | Echo Mod Depth | Modulation du temps : le pleurage. | probable |
| DUB ECHO | Echo Mod Freq / MOD FREQ | MOD RATE | Echo Mod Rate | | probable |
| DUB ECHO | Echo Feedback / FEEDBACK | — | Echo Feedback | | sûr |
| DUB ECHO | Echo Filter Freq / FLT FREQ | HPF | Echo HPF | Base du filtre de boucle. | probable |
| DUB ECHO | Echo Filter Width / FLT WIDTH | BANDWIDTH | Echo Bandwidth | Largeur au-dessus de la base. | probable |
| DUB ECHO | Echo Mono / MONO | — | Echo Mono | Entrée ou sortie mono : à écouter. | incertain |
| DUB ECHO | Echo Level / LEVEL | — | Echo Level | | sûr |
| STEREO DLY | Delay Time, Ping Pong, Width, Feedback | TIME, PING-PONG, WIDTH, FEEDBACK | Delay Time … | | sûr |
| STEREO DLY | Delay High Pass / HPF, Low Pass / LPF | HPF, LPF | Delay HPF, Delay LPF | | sûr |
| STEREO DLY | Delay Reverb Send / TO REVERB | — | Delay To Reverb | | sûr |
| Réverb. | Reverb Source / SOURCE, MD Gate Box/Syntakt | SOURCE, GATED VERB/LUSH VERB | Reverb Source | | sûr |
| Réverb. | Reverb Output | — | Reverb Out | | sûr |
| GATED VERB | Gate Box Dry Vol / DRY VOL | DRY LEVEL | Gated Dry Level | | probable |
| GATED VERB | Gate Box Pre-Delay, Decay, Damp | PRE-DELAY, DECAY, DAMPING | Gated Pre-Delay … | | sûr |
| GATED VERB | Gate Box High Pass, Low Pass | HPF, LPF | Gated HPF, Gated LPF | | probable |
| GATED VERB | Gate Box Gate / GATE | — | Gated Gate | La porte qui coupe la queue. | probable |
| GATED VERB | Gate Box Level | — | Gated Level | | sûr |
| LUSH VERB | Reverb Pre-Delay, Decay, Shelf Freq, Shelf Gain | PRE-DELAY, DECAY, SHELF FREQ, SHELF GAIN | Reverb Pre-Delay … | | sûr |
| LUSH VERB | Reverb High Pass, Low Pass | HPF, LPF | Reverb HPF, Reverb LPF | | sûr |
| CHORUS | Chorus Rate / SPEED ; Chorus Speed / SPEED | RATE, RATE | Chorus Free Rate, Chorus Note Rate | | sûr |
| CHORUS | Chorus Output, Sync, Depth, Delay, Feedback | — | Chorus Out … | | sûr |
| CHORUS | Chorus Level / MIX | LEVEL | Chorus Level | Niveau de retour. | sûr |
| CHORUS | Chorus Shape / SHAPE | WAVE | Chorus Wave | | sûr |
| PHASER | Phaser Mode / MODE, Phaser/Flanger/DP/4 | TYPE, PHASER/FLANGER/90S RACK | Phaser Type | | sûr |
| PHASER | Phaser Rate / SPEED ; Phaser Speed / SPEED | RATE, RATE | Phaser Free Rate, Phaser Note Rate | | sûr |
| PHASER | Phaser Output, Sync, Stages, Depth, Freq, Spread, Feedback | — | Phaser Out … | | sûr |
| PHASER | Phaser Level / MIX | LEVEL | Phaser Level | | sûr |
| 90S RACK | Phaser DP4 Phase / DP4 PHASE | WET PHASE | Rack Wet Phase | Polarité du signal traité. | probable |
| 90S RACK | Phaser DP4 Delay L, R, Fdbk / DP4 DLY L, R, FB | DELAY L, DELAY R, DELAY FDBK | Rack Delay L … | | sûr |
| 90S RACK | Phaser DP4 S&H / DP4 S&H | S&H RATE | Rack S&H Rate | Paliers aléatoires de l'LFO, ou off. | sûr |

### 4.18 Master et global

| Nom actuel (hôte / cellule) | Court | Long | Confiance |
|---|---|---|---|
| Master On / MASTER | — | Master On | sûr |
| Master Threshold, Ratio, Attack, Release, Makeup, Mix | — | Comp Threshold, Comp Ratio … | sûr |
| Master Filter / FILTER | SC FILTER | Comp SC Filter (filtre du détecteur) | sûr |
| Master Drive, Master Volume | — | — | sûr |
| Oversampling, Tempo, Pattern | — | — | sûr |

---

## 5. Où un nom de marque ou de machine est montré aujourd'hui

Chemins sous `source/elektron/`.

**Noms d'hôte et leurs choix** (`edDrumsPlugin/edDrumsParameters.cpp`)
- `:16` choix du Drive : « Syntakt », « MD DIST », « MD SRR ».
- `:368` choix du Band : « MD EQ » (et « Base-Width », vocabulaire Elektron).
- `:372` choix du filtre : « Syntakt », « Machinedrum ».
- `:417` Delay Source : « MD Echo », « Syntakt ». `:418` Reverb Source : « MD Gate Box », « Syntakt ».
- `:430-432` huit noms « Gate Box … ».
- `:449` Phaser Mode : « DP/4 ». `:453-455` et `:475` cinq noms « Phaser DP4 … ».

**Liste des machines** (choix « Machine » de l'hôte, Browse, titre de Sound, LCD, toast)
- `edDrums/edDrumsEngine.h:80-81` les neuf noms du Syntakt ; `:96-97` les quatre OD ; `:104` « SOPHIE ».
- `md/mdProtocol/mdmachines.cpp:33-48`, `:68-75` les noms GND, TRX, EFM, E12, P-I, ROM, que
  `edDrumsPlugin/edDrumsProcessor.cpp:67-81` (`machineName`) donne à la liste.
- Lus par `edDrumsPlugin/edDrumsEditorScreen.cpp:466`, `:1487`, `:1920`, `:2351`, `:2851`, `:3118`, `:3326-3331`.

**Étiquettes de l'éditeur** (`edDrumsPlugin/edDrumsEditorScreen.cpp`)
- `:25-34` contrôles des machines Syntakt (noms du manuel du Syntakt) ; `:64-67` contrôles des machines MD, tirés de la
  table de l'OS (PTCH, DEC…) ; `:36` ceux du Sampler (BR, RTRG, RTIM).
- `:742` « MD EQ » ; `:839` « DT2 J2 » ; `:2504-2505` cinq « DP4 … » ; `:3264` « IMPORT MY ELEKTRON CONTENT » ;
  `:3527` « ELEKTRON CONTENT » ; `:3528` « UW BANK ».

**Fenêtres et messages** (`edDrumsPlugin/edDrumsEditor.cpp`)
- `:1045-1046` « Import my Elektron content … a Machinedrum flash image » ; `:1089-1090` « UW bank from … », « no UW
  image » ; `:1094` « … Elektron files not read ».

**Nom du plug-in** : `edDrumsPlugin/CMakeLists.txt:5` « Elektron Drums Dev », montré par l'hôte (`PRODUCT_NAME`,
ligne 24 ; `edDrumsProcessor.h:66`), et nom du dossier de données (`edDrumsProcessor.cpp:1004`).

**Préréglages** : aucun préréglage d'usine nommé dans le code ; les Kits et sons importés gardent les noms des fichiers
de l'utilisateur.

Total : 30 endroits, dont 13 dans les noms et choix de l'hôte. Non montrés, hors règle : le « MD » interne du dessin du
filtre (`edDrumsEditorScreen.cpp:235`, `:879`), les balises XML « ElektronDrums… » (`edDrumsProcessor.cpp:1480`,
`edDrumsKits.h:23`, `edDrumsTrackSounds.h:21`), qui ne doivent pas changer (un état sauvé ne se rechargerait plus), le
dossier « Elektron/Machinedrum.words » (`edDrumsElektronImport.cpp:31`).

---

## 6. Ce qui reste à décider

1. **Les noms proposés** : chaque nom de famille et de machine a un autre choix ; l'utilisateur prend l'un, l'autre ou
   le sien.
2. **Les 13 contrôles subtils** de la section 7 : comparer chacun à la machine d'origine. S'il s'y entend, c'est un
   défaut du port à corriger, pas une question de nom ; s'il ne s'entend pas non plus, garder le nom proposé ou le cacher.
3. **Import et nom du plug-in** : « Import my Elektron content », « UW BANK », le message qui demande une image flash du
   Machinedrum, et « Elektron Drums Dev ». Nommer le format des fichiers de l'utilisateur l'aide à savoir quoi donner ; la
   règle vise les paramètres. Le nom du plug-in change le dossier de données : à traiter à part, avec une migration.
4. **Band devient EQ** : accepté avec les recommandations, à confirmer.
5. **ROM-01 … ROM-32** : garder le numéro, ou montrer le nom de l'échantillon si l'image de la banque en porte un (à
   vérifier dans l'image).
6. **À mesurer avant d'appliquer** : ce que Push montre d'un nom long et comment Live l'abrège.

---

## 7. Contrôles subtils : défauts de port possibles

L'utilisateur n'entend presque rien en tournant ces 13 contrôles. Un contrôle que la machine d'origine rend audible et
que le port ne rend pas serait un défaut, pas une question de nom. Pour chacun : ce que le port en fait (code lu), et ce
que l'original est documenté faire quand je le sais. Le moyen de trancher existe dans le dépôt : l'oracle du firmware
complet (`mdLib`, comparé par `mdEngineFirmwareTest`) pour les machines du Machinedrum, `stEngineOracleTest` pour celles
du Syntakt ; il faudrait y rendre un coup avec ce contrôle à 0 puis à 127 et comparer l'écart de l'original à celui du
port. Je n'ai pas vérifié si leurs scénarios actuels balaient ces contrôles.

| Machine (nom proposé) | Contrôle | Nom gardé | Écouté | Ce que le port en fait | Ce que fait l'original |
|---|---|---|---|---|---|
| TRX-CY (ANA CYMBAL) | 1 RICH | TUNE | seule la hauteur change | `trxCy` lit Machine 1 comme le « tone » d'où il tire la fréquence des six partiels (`mdVoiceCoefficients.cpp:221-237`) : une hauteur est ce que le code fait. | La table de l'OS le nomme RICH (richesse) et la machine n'a pas de PTCH ; je ne sais pas si l'original change aussi la densité. Si non, le nom TUNE est le bon et rien n'est à corriger. |
| TRX-CL (CLAVES) | 4 ENH | ENHANCE | ne change presque rien | Écrit le mot 6 = (valeur >> 8) + $40, une plage étroite ($40 à $7F) (`mdVoiceCoefficients.cpp:285`) : un petit effet est attendu du code. | Inconnu. |
| TRX-S2 (SINE SNARE) | 5 POWR | POWER | aucune différence | Écrit les mots 6, 10 et 11, ces deux derniers au carré de la valeur (`mdVoiceCoefficients.cpp:314`, `320-321`). | Inconnu. La table des machines la marque `false` (`mdmachines.cpp:39`), sans dire pourquoi. À comparer en priorité : trois mots écrits pour aucun son. |
| TRX-S2 (SINE SNARE) | 6 TUNE | BODY TUNE | très très peu | Écrit les mots 3 et 4 (`mdVoiceCoefficients.cpp:317-318`). | Inconnu. |
| E12-BD (LOFI KICK) | 3 SNAP | CLICK | très peu | Un niveau, mot 4 (`e12Bd`, `mdVoiceCoefficients.cpp:544-551`). | Inconnu. |
| E12-BD (LOFI KICK) | 4 SPLN | CLICK DECAY | très peu | Une durée de chute lue dans la table des décroissances, mot 5 (`e12Bd`). | Inconnu ; si SNAP est faible, sa durée l'est aussi : les deux vont ensemble. |
| E12-SD (LOFI SNARE) | 4 RING | RING | très peu | Un niveau, mot 11 (`e12Mix`, `mdVoiceCoefficients.cpp:568-575`), le même code que E12-BR REAL (entendu plus fort) et E12-BC. | Inconnu. Le même mot s'entend sur E12-BR : une différence d'échantillon plutôt qu'un défaut, à confirmer. |
| P-I-CC (REAL CRASH) | 5 AG | BODY | trop peu pour juger | Un niveau, mot 6 (`piMetal`, `mdVoiceCoefficients.cpp:747-755`), le même code que P-I-RC où AG s'entend. | Inconnu. Même code que la ride : si la ride l'entend et pas la crash, l'écart est dans le programme de la voix ou dans le réglage du coup écouté. |
| P-I-CC (REAL CRASH) | 6 AU | TONE | trop peu pour juger | Une rangée d'étagère, mot 7 (`piMetal`), comme sur P-I-RC où AU s'entend. | Inconnu ; même remarque. |
| P-I-HH (REAL HAT) | 3 CLSN | CLOSED | trop peu pour juger | Un niveau, mot 3 (`piHh`, `mdVoiceCoefficients.cpp:783-790`). | Inconnu. |
| P-I-HH (REAL HAT) | 5 AG | BODY | trop peu pour juger | Un niveau, mot 6 (`piMetal`). | Inconnu ; même code que la ride. |
| CY ALLOY (SHINY CYM) | 5 RADIO | RADIO | trop peu pour juger | Un octet de la machine dans `stCyAlloy.cpp` ; je n'ai pas tracé lequel. | Inconnu. |
| SD VINTAGE (OLD SNARE) | 6 COMPLEX | COMPLEX | trop peu pour juger | Un octet de la machine dans `stSdVintage.cpp` ; je n'ai pas tracé lequel. | Inconnu. |

Les « inconnu » : ni le code ni la doc du dépôt ne disent ce que fait l'original, et je ne cite pas un manuel de mémoire.
