# Des noms de paramètres qui disent ce qu'ils font

Type: HITL
Status: resolved
Assignee: 
Blocked by: 

## Quoi

Demandé par l'utilisateur le 2026-11-11 : chaque paramètre de moteur doit porter un nom qui dit clairement ce qu'il fait au son. Les noms repris des machines d'origine (Machinedrum, Syntakt, DP/4, Virus, OPEN DRUM, Sophie, etc.) sont souvent des abréviations opaques (PTCH, DEC, HP, RAMP, SNAP…) ou des noms de marque.

Règle : tout paramètre importé d'une machine Elektron ou d'une autre machine doit évoquer cette machine sans citer son nom. Pas de « Machinedrum », « Syntakt », « DP/4 », « Virus », « Elektron » dans les noms montrés à l'utilisateur ; un nom de famille ou de type à nous qui rappelle la machine (son époque, sa façon de sonner, sa forme) à la place.

## Contraintes

- Les ID des paramètres sont gelés (`edDrumsParameterIds.txt`) : seuls les noms montrés changent, dans l'hôte et dans l'éditeur. Un set, un Kit ou un son de piste sauvés avant doivent se recharger à l'identique.
- Les noms de l'hôte restent lisibles sur l'écran de Push (« T01 » puis le nom, court).
- L'éditeur Lockstep montre des noms courts dans ses cellules : chaque paramètre a un nom court (cellule) et un nom long (hôte, infobulle).
- À trancher avec l'utilisateur : les noms des machines elles-mêmes (TRX-BD, EFM-SD, BD MODERN…) et des familles suivent-ils la même règle ?

## Fait quand

- Une table de proposition (famille, machine, paramètre : nom actuel, nom court, nom long, ce que fait le paramètre) est approuvée par l'utilisateur.
- Les noms approuvés sont appliqués à l'hôte et à l'éditeur ; aucun nom montré ne contient un nom de marque ou de machine (test qui parcourt les noms).
- Les états et Kits sauvés avant se rechargent à l'identique (test).

## Décisions (2026-11-11)

Réponse de l'utilisateur à la première proposition (`../noms-proposition.md`) :

- Les noms sont en anglais.
- Les noms sont des noms maison : aucune marque ni nom de machine, ni dans les paramètres, ni dans les machines, ni dans les familles.
- HPF et LPF sont gardés (pas LOW CUT et HIGH CUT).
- Dans l'hôte, les contrôles de machine restent « T01 Machine 1 » à « T01 Machine 24 » : un nom d'hôte ne peut pas suivre la machine choisie. Leurs noms par machine vivent dans l'éditeur (cellule, infobulle, lignes de locks de Seq).
- Les autres recommandations sont acceptées : 10 caractères visés et 12 au plus pour un nom court, 10 pour un nom de machine ; un mot par fonction partout ; pas d'unité dans les noms ; des noms longs dont les 10 premiers caractères après « T01 » se distinguent.
- Refusés : des noms de machine faits de préfixes codés (VA-BD, FM-SD, 12B-RC…) et des noms de famille abstraits (STEEL, HYBRID, SHARD…). Les noms de machine et de famille disent l'identité : ce que la machine sonne ou comment elle fait son son, pour qu'on la choisisse par son nom.
- Le cœur du ticket : chacun des contrôles de chaque machine (Machine 1-8, et 9-24 là où elle en a) porte un nom clair qui dit ce qu'il fait sur cette machine-là (sur une grosse caisse : TUNE, SWEEP, SWEEP TIME, DECAY, CLICK, NOISE, BODY, DRIVE ; pas PTCH, DEC, RAMP, HP).

La proposition révisée selon ces décisions est `../noms-proposition.md` ; elle attend l'approbation des noms.

Deuxième réponse de l'utilisateur, le même jour :

- Le plug-in s'appelle Lockstep (et non plus « Elektron Drums Dev ») ; son dossier de données suivra, avec une migration de l'ancien.
- Band devient EQ.
- Les emplacements ROM et les machines ROM ne servent plus : ils quittent la liste ; les samples de la banque d'usine seront extraits ou lus autrement, comme samples du Sampler.
- « Elektron content » et « UW BANK » laissent place à une extension de stockage, dans l'esprit d'un disque de samples intégré, qui gère tous les samples et toutes les banques au même endroit (import, banques d'usine lues dans l'image de l'utilisateur, dossiers) ; son nom évoque sans citer de marque.
- Les contrôles et noms marqués « incertain » sont rendus pour écoute ; l'utilisateur décrit ce qu'il entend, et le nom en découle.

## État (2026-11-11), machines ROM retirées

Les machines ROM-01 à ROM-32 ont quitté la liste Machine (99 choix, 67 désormais) ; les échantillons de la banque d'usine
sont devenus des samples du Sampler. Version 5 de l'état, version 3 des Kits. Un état antérieur à la version 5, ou un Kit
antérieur à la version 3, dont une piste jouait ROM-n passe sur le Sampler avec l'emplacement d'usine n : Machine 1 à 8
sont reprises telles quelles, car le Sampler est le même lecteur ROM avec les mêmes huit réglages (PTCH, DEC, HOLD, BRR,
STRT, END, RTRG, RTIM) ; Machine 9 à 12 prennent les valeurs par défaut du Sampler (grain 12 bits), puisque ROM-n n'en
avait pas ; un sample utilisateur que la piste gardait sous ROM-n est remplacé par la référence à l'emplacement.

Le sample de la piste n'est qu'une référence (`<sample track="2" factory="5"/>`, empreinte « factory:5 ») : ni l'état ni
le dépôt ne contiennent d'audio d'usine. Le moteur lit les mots là où la banque les tient, avec l'entrée de répertoire
de ROM-n (`Sample::factory`, `Engine::setSample`) ; les valeurs 16 bits décodées servent au grain VINTAGE et à l'écoute.
Mesuré : l'emplacement 5 sur le Sampler rend les mêmes 61 000 échantillons que ROM-05 (edDrumsEngineTest). Sans image,
la piste se tait, garde sa référence dans l'état et l'éditeur affiche « Factory 05, no flash image »
(edDrumsNoFlashTest).

Browse : SAMPLES, BY SOURCE gagne un segment FACTORY après ceux des dossiers, avec les 32 emplacements de l'image (ou de
ses mots importés), nommés FACTORY 01 à 32 puisque la banque ne porte pas de noms ; un clic les écoute, un double clic
ou un glisser les charge sur le Sampler. Ils ne figurent ni sous ALL ni sous SAMPLES, BY TYPE (choix fait ici : ALL
reste la liste des dossiers de l'utilisateur), et leur TYPE ne se modifie pas.

Pas fait : les noms des machines et des familles (ticket à venir) ; l'extension de stockage qui remplacera « Elektron
content » et « UW BANK ».

## État (2026-11-11), le plug-in s'appelle Lockstep

Le plug-in s'appelle Lockstep : `ED_DRUMS_NAME` dans `edDrumsPlugin/CMakeLists.txt` donne le nom que montre l'hôte
(`PRODUCT_NAME`, `Processor::getName`), le nom du bundle (`Lockstep.vst3` au lieu de `Elektron Drums Dev.vst3`) et celui
du dossier de données. L'éditeur ne montrait pas le nom du plug-in : rien à y changer. La classe VST3 ne change pas :
JUCE la fait des seuls codes `C0rm` et `Eldr`, puisque `source/juce.cmake` met `JUCE_VST3_CAN_REPLACE_VST2=0` (avec 1,
le nom entrerait dans l'identifiant). Mesuré : le hachage de la classe que donne l'hébergement VST3 de JUCE vaut
3065139503 pour le bundle construit sous l'ancien nom comme pour Lockstep ; edDrumsVst3HostTest l'exige, avec le nom
« Lockstep ». Live verra donc la même entrée renommée après son analyse des plug-ins, et les sets la retrouvent ; il faut
retirer l'ancien `Elektron Drums Dev.vst3` du dossier VST3 que Live lit, sinon deux bundles portent la même classe.

Le dossier de données passe de `Documents/c0remusic/Elektron Drums Dev` à `Documents/c0remusic/Lockstep`
(`GEARMULATOR_DATA_ROOT` à la place de Documents pour les tests). Au chargement, sur le fil des messages, avant toute
lecture du dossier, `Processor::migrateData` copie chaque fichier de l'ancien dossier que le nouveau n'a pas (Kits,
TrackSounds, Samples, Elektron, roms, à toute profondeur), puis écrit `Migrated.txt` dans le nouveau : ensuite plus rien
n'est copié, pour qu'un Kit effacé par l'utilisateur ne revienne pas. L'ancien dossier n'est jamais touché. Une copie qui
échoue n'écrit pas la marque, et le chargement suivant reprend. edDrumsNoFlashTest le vérifie dans une racine de test :
6 fichiers copiés à l'identique dans un dossier neuf, l'ancien intact, un second chargement qui ne copie rien (ni le Kit
effacé, ni un fichier ajouté à l'ancien), un fichier déjà présent dans le nouveau gardé.

Restent montrés avec « Elektron », laissés à l'extension de stockage : « Import my Elektron content » (fenêtre de choix,
`edDrumsEditor.cpp`), « … Elektron files not read » (`edDrumsEditor.cpp`), « IMPORT MY ELEKTRON CONTENT » et « ELEKTRON
CONTENT » (`edDrumsEditorScreen.cpp`).

## État (2026-11-11), les emplacements Syntakt jouent ce que dit leur nom

Les huit emplacements Machine d'une machine Syntakt suivent désormais l'ordre de sa page SYN, celui des noms affichés :
l'emplacement k jouait jusqu'ici l'index k - 1 de la voix (identifiant interne 17 + k), si bien que DECAY jouait SHIM,
OVERDRIVE jouait DECAY, MOD ENV jouait HOLD et RADIO de CY ALLOY jouait MENV. L'ordre est TUNE (18), SWEEP ou SHIMMER
(21), PUNCH (23), DECAY (25), les deux paramètres propres à la machine (19 et 20), MOD ENV (22), OVERDRIVE (26), tel
que les descripteurs de la section 3 le donnent et que `syntakt::Syntakt` nomme les identifiants
(`Engine::syntaktSlotOf`). OVERDRIVE devient l'étage de drive qui suit la voix sur le Syntakt (`TrackDrive`, à gain
unité et exact à 0) ; HOLD (24) n'est sur aucune page : l'amp tient la voix, et Machine 9 le garde pour un set d'avant.

Mesuré contre l'original (edDrumsSyntaktSlotsTest, écart à la valeur 0 en forme d'onde / en tiers d'octave, à 64 et 127) :
RADIO de CY ALLOY 3.0 / 4.0 et 2.2 / 4.3 dB, comme l'original ; COMPLEX de SD VINTAGE -1.9 / 5.5 et 1.0 / 9.6 dB contre
-1.9 / 5.5 et 0.8 / 9.6 ; OVERDRIVE de BD MODERN -2.4 / 4.3 et 4.2 / 8.4 dB, comme l'original. Avant : RADIO 3.5 / 1.2,
COMPLEX -4.6 / 7.1, OVERDRIVE 15.3 / 13.2 dB.

Version 6 de l'état, version 4 des Kits et des sons de piste, version 2 des fichiers de pattern. Un état, un Kit ou un son
plus ancien est remis dans l'ordre à la lecture (`Processor::takePageOrder`) : chaque valeur va à l'emplacement qui la joue
maintenant, HOLD en Machine 9, OVERDRIVE à 0 ; une LFO visant Machine 1-8 d'une piste Syntakt suit sa valeur (celle qui
visait HOLD ne vise plus rien) ; les Locks des patterns, ceux de l'état et ceux d'un fichier importé, aussi, avec les
machines du Kit que le pattern charge s'il en charge un. Les valeurs par défaut sont celles d'avant remises dans l'ordre
(PUNCH et OVERDRIVE à 0). Vérifié échantillon pour échantillon entre la construction d'avant et celle d'après : le Kit
d'usine 01, un état de version 5 (trois pistes Syntakt, un Lock sur l'ancienne Machine 4, une LFO sur l'ancienne
Machine 2, HOLD à 127), un Kit de version 3 et les valeurs par défaut de chaque machine rendent les mêmes échantillons.

Pas fait : les noms eux-mêmes restent ceux du manuel en attendant l'approbation de la table ; un Lock sur la cible d'une
LFO (Lock de type LFO) n'est pas remis dans l'ordre.

## État (2026-11-11), le LOCKER remplace « Elektron content »

L'extension de stockage est faite (ticket 20, `../stockage-proposition.md`) : elle s'appelle LOCKER et tient, dans
Browse, les dossiers liés, les packs importés, la banque d'usine (source FACTORY) et les samples d'un set (IN THIS SET).
Les textes laissés à ce chantier ont disparu : « Import my Elektron content » et « … Elektron files not read »
(`edDrumsEditor.cpp`), « IMPORT MY ELEKTRON CONTENT » et le bloc « ELEKTRON CONTENT » du Menu, avec UW BANK, FACTORY
PACKS et TRANSFERS (`edDrumsEditorScreen.cpp`), « The factory samples need the Machinedrum's flash image »
(`edDrumsProcessor.cpp`, désormais « FACTORY BANK NOT IN THE LOCKER »). Les noms internes (balise `ElektronDrums` des
états, dossier `Elektron Drums Dev` de la migration) restent. `edDrumsEditorTest` parcourt les 544 textes dessinés sur
Browse et le Menu : aucun ne contient ELEKTRON, MACHINEDRUM ni le mot UW.

Pas fait : la page Sound montre encore « MACHINEDRUM » et « SYNTAKT » dans des valeurs de paramètres
(`edDrumsParameters.cpp`, « Off|Syntakt|Machinedrum » et voisines) ; elles attendent la table de noms de ce ticket, et le
balayage ne couvre donc que Browse et le Menu.

## État (2026-11-11), les noms appliqués

La table approuvée (`../noms-proposition.md`) est appliquée, revue ligne à ligne contre le code d'aujourd'hui. Un seul
endroit tient les noms montrés des machines, de leurs familles et de leurs contrôles : `edDrumsPlugin/edDrumsNames.*`.
Les noms internes du moteur (`syntaktMachineNames`, les noms d'OPEN DRUM et de SOPHIE, la table de l'OS dans
`mdmachines.cpp`) ne sont plus montrés et restent tels quels.

Machines et familles : les 67 machines de la liste portent leur nom maison, 10 caractères au plus, aucun deux fois
(ROUND KICK, LOFI HITOM, PUNCH KICK, VA HIHAT, HYPER FM…) ; leur famille (BASIC, CIRCUIT, FM PERC, CRUNCH 12, PHYSICAL,
MODERN DIGI, VA DRUMS, HYPER FM, SAMPLER) s'affiche sur la carte de Browse, ligne FAMILY. Les lignes de la table ont été
reprises dans l'ordre actuel : Machine 1-8 d'une machine Syntakt dans l'ordre de sa page SYN (OVERDRIVE devient DRIVE,
l'étage après la voix), Machine 9 nommée HOLD (aucune page ne la montre, la liste TARGET des LFO si) ; plus de machines
ROM ; le Sampler nomme Machine 1-12 TUNE, DECAY, HOLD, BIT CRUSH, START, END, REPEATS, REPEAT GAP, GRAIN, BITS,
COMPANDING et OUTPUT LPF (et non OUTPUT FILTER, 13 caractères). Les lignes « subtil — à vérifier » gardent leur nom
proposé.

Contrôles par machine, dans l'éditeur seulement : cellules du bloc Machine, liste TARGET des deux LFO (Machine 1-24
nommées par la machine de la piste visée), lignes de locks de Seq et liste de + LOCK. L'éditeur n'a pas d'infobulles :
rien à y nommer. L'hôte garde « T01 Machine 1 » à « T01 Machine 24 ».

Chaîne, effets, master : DRIVE OFF/SATURATE/DISTORT/DOWNSAMPLE/DAC, FILTER OFF/MULTIMODE/WINDOW, Band devient EQ
(OFF/CUTS/PEAK/3 BAND, cellules HPF, LPF, PEAK FREQ, PEAK GAIN), CUTOFF, ENV AMT, KEY TRACK, ENV RETRIG, AMP ENV,
VEL TARGET, LFO RATE/TRIG MODE/ON TRACK/TARGET, PHASER pour l'envoi, DUB ECHO/STEREO DLY, GATED VERB/LUSH VERB,
PHASER/FLANGER/90S RACK et ses WET PHASE, DELAY L, DELAY R, DELAY FDBK, S&H RATE, SC FILTER ; le bloc Sample écrit
GRAIN J2 au lieu de DT2 J2. Noms d'hôte changés, ID inchangés (`edDrumsParameterIds.txt` non touché). Écarts à la
table, pour que les 10 premiers caractères après « T01 » restent distincts : « Reverb Freq » et « Reverb Gain » (et non
Reverb Shelf Freq/Gain, déjà confondus avant), « Phaser Rate » garde son nom (« Phaser Free Rate » se confondrait avec
« Phaser Freq »), « Rack Left Delay », « Rack Right Delay », « Rack Delay Fdbk ». Echo Mono garde son nom.

Fichiers : un son de piste garde son attribut `machineName`, mais Browse montre et cherche le nom actuel de sa machine,
lu de son index (`Processor::trackSoundMachine`). Ni les états ni les Kits ne contiennent de nom.

Vérifié : `edDrumsEditorTest` parcourt les 1530 paramètres de l'hôte (144 223 noms, choix et textes de valeur) et
20 598 textes dessinés sur Sound (chaque machine, chaque page), les cartes de Browse, Seq, FX (chaque source, type et
synchro), Master, Kits : aucun nom de marque ni ancien nom ou code de machine ; les 10 premiers caractères après « T01 »
sont uniques ; 544 contrôles nommés en 12 caractères au plus là où la machine d'origine en nomme un ; chaque nom tient
dans sa cellule. `edDrumsProcessorTest` : un son de piste sauvé sous l'ancien nom (BD MODERN) se charge et joue
échantillon pour échantillon comme celui sauvé maintenant ; les tests d'états et de Kits anciens
(`edDrumsSyntaktSlotsTest`, `edDrumsProcessorTest`) passent. `edDrumsVst3HostTest` : 1530 paramètres, ID inchangés.
Rendus regardés (Sound, Seq, Browse, FX) : aucun nom coupé.

Pas fait : rien de la table. Les noms d'origine restent dans le code interne et les commentaires.
