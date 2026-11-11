# Samples, import Elektron, explorateur (moteur)

Type: AFK
Status: resolved
Assignee: 
Blocked by: 08, 19

## Quoi

Bibliothèque de samples (WAV, AIFF, FLAC) dans le dossier de données, partagée ; un kit les référence par empreinte
et nom, l'état en garde une copie. Import « Importer mon contenu Elektron » ou dossier glissé : chaque fichier
reconnu par empreinte et format ; banque UW extraite de la flash image du MD (seulement les samples, jamais le code
ni les tables) ; rapport de ce qui est trouvé et de ce qui manque. Types devinés par le nom puis par une analyse
simple, corrigeables. Côté moteur seulement : l'interface de l'explorateur est au ticket 24.

## Critère de fin

- Une machine ROM joue un sample de l'utilisateur et la banque UW importée ; sans elle, un sample maison ou le
  silence.
- L'import d'un dossier test rapporte trouvés et manquants ; aucun firmware n'est copié dans les données.

## Références

Spécification §8, §11.4. Ticket « Format des kits et de l'état ».

## État (2026-11-08)

Une entrée Sampler joue le sample de la piste par le lecteur ROM du MD (grain MD seul, 12 bits, 1,29 s au plus à 44,1
kHz), avec ou sans flash image, par l'emplacement ROM-33 + piste : ROM-33 à 48 ne sont plus des machines. WAV, AIFF et
FLAC se chargent par glisser sur un pad, un LCD ou le bloc Sample ; l'état garde une copie de chaque sample. Pas faits :
bibliothèque partagée, empreintes, import du contenu Elektron, explorateur.

## État (2026-11-11)

Bibliothèque de samples faite (`edDrumsPlugin/edDrumsSampleLibrary.*`) : dossiers ajoutés par l'utilisateur, listés
dans `Samples/folders.txt` du dossier de données, scannés sur un fil à part, empreinte par taille et premiers 64 Ko,
type deviné par le nom. Page Browse de Lockstep : arbre par dossier et par type, recherche, écoute au clic sur une voix
à part, chargement par double-clic ou glisser sur une piste. Pas faits : import du contenu Elektron, analyse et
correction des types, retrait d'un dossier.

## État (2026-11-11, suite)

L'import du contenu Elektron est fait (`edDrumsPlugin/edDrumsElektronImport.*`). Le bouton IMPORT MY ELEKTRON CONTENT
de Browse, IMPORT MY CONTENT du panneau Menu, ou un dossier glissé sur Browse, parcourent le dossier choisi sur un fil à
part, sous-dossiers compris. Une flash image du MD (8 Mo, OS 1.63) y donne les mots que la `Library` du moteur en tire,
et rien d'autre : ondes de TRX, mots des P-I avec la table $24DA14, mots E12, répertoire et échantillons de la banque UW
(`ImageWords`, `Library::importImage`). Ils sont écrits dans `Elektron/Machinedrum.words` du dossier de données (1,9 Mo
pour l'image de test) ; l'image n'est jamais copiée. Au chargement, le Processor lit ce fichier avant de chercher une
image ; la `Library` qui en sort est celle de l'image, échantillon par échantillon (testé). Un import prend effet au
chargement suivant de l'instrument : le moteur n'est pas reconstruit en cours de route. Les WAV, AIFF et FLAC du dossier
(packs d'usine, exports Transfer) sont comptés, ceux que la bibliothèque connaît déjà par empreinte à part, et le
dossier rejoint la bibliothèque s'il en apporte de nouveaux. Les fichiers propres à Elektron (`.syx`, `.dtprj`,
`.dtsnd`, `.stprj`, `.stsnd`) sont comptés, pas lus. Le rapport : un toast à la fin de l'import, et les trois cellules
de ELEKTRON CONTENT du Menu (UW BANK : IMPORTED, AT NEXT LOAD, MISSING ou NOT IMPORTED ; FACTORY PACKS : n FOUND ou
MISSING ; TRANSFERS : n NOT READ ou MISSING). La `Library` du moteur reçoit un constructeur depuis ces mots, et celui depuis
l'image passe désormais par eux.

Types : un nom qui ne dit rien laisse place à une analyse simple, sur le fil du scan : passages par zéro du premier quart
de seconde, au-delà d'un vingtième du crête, et durée (sous 200 Hz KICK, sous 600 Hz TOM, sous 4,5 kHz SNARE, au-delà
CYMBAL au-dessus de 0,6 s, sinon HAT). Un clic sur la ligne TYPE du panneau d'un sample ouvre le sélecteur des huit
types ; le choix est gardé par empreinte dans `Samples/types.txt`, relu avant chaque écriture et suivi par les autres
instances. La ligne dit GUESSED tant que l'utilisateur n'a pas choisi.

Retrait : REMOVE FOLDER, au-dessus de ADD FOLDER dans SAMPLES, BY SOURCE, actif quand le segment d'un dossier est choisi.
Le dossier quitte `folders.txt`, ses entrées disparaissent aussitôt, rien d'autre ne change sur le disque.

Tests : `edDrumsSampleLibraryTest` (nouveau, dans la gate) sur des fichiers qu'il écrit dans un dossier temporaire :
types par nom et par son, type corrigé gardé après un rescan et dans une nouvelle bibliothèque, dossier retiré sans
entrée restante et fichiers intacts, import d'un dossier (2 samples dont 1 connu, 2 fichiers Elektron non lus, 1 faux
fichier de 8 Mo refusé, rien d'écrit), fichier de mots relu tel qu'écrit et refusé tronqué ; avec
`GEARMULATOR_MD_FIRMWARE_BIN`, l'image copiée dans le dossier temporaire puis importée, sa `Library` égale à celle de
l'image. `edDrumsEditorTest` règle un TYPE et retire le dossier à la souris.

Pas faits : les packs d'usine et les exports Transfer ne se distinguent pas, faute de liste d'empreintes connues ; la
banque UW importée ne se liste pas comme samples dans Browse (segment UW BANK de la maquette) ; les formats propres à
Elektron ne sont pas lus.

## État (2026-11-11), grain VINTAGE

Le Sampler a deux grains, Machine 9 (GRAIN, deux segments dans la tête du bloc Machine) : 12-BIT, le lecteur ROM d'avant,
et VINTAGE, le caractère PCM d'époque de `edDrums/edDrumsVintage.*` (le même que le drive DAC du ticket 12). VINTAGE lit
le sample entier jusqu'à 10 s à 48 kHz (`Sample::MaxSourceLength`, 480 000 valeurs ; le lecteur ROM reste à 1,29 s) à une
horloge que PTCH règle (+-24 demi-tons, 3/8 par cran), chaque valeur tenue jusqu'au tic suivant sans interpolation, puis
le convertisseur (Machine 10 BITS, 6 à 12 ; Machine 11 COMPANDING, par segments), puis le passe-bas de deux pôles
(Machine 12 OUTPUT FILTER, 100 Hz à 20 kHz), puis HOLD (jusqu'à 2 s) et DEC (chute de 60 dB en 20 ms à 10 s, aucune à 127).
STRT et END bornent la lecture ; BRR, RTRG et RTIM ne s'appliquent pas à ce grain. Machine 9 à 12 ne prennent pas de lock ;
les locks et les LFO de PTCH, DEC, HOLD, STRT et END agissent, leur somme bornée aux valeurs d'un Kit (END ne lit jamais
au-delà du sample). Passer en VINTAGE un Sampler d'avant (Machine 10 à 12 à 0) lui donne les valeurs d'un Sampler neuf dans
le même clic. Un Choke, un sample changé ou un grain changé arrêtent la voix à la fin de sa rampe. Un Sampler neuf est en 12-BIT ; un état ou un kit d'avant (Machine 9 à
24 à 0) aussi, et sonne à l'identique quels que soient BITS, COMPANDING et OUTPUT FILTER (test). Le premier échantillon
sonne `Engine::HitDelay` (81) après la note, où que tombe la note. Le moteur lit les valeurs du sample là où elles sont
(`Sample::source`, partagé et jamais modifié) : rien n'est copié sur le fil audio ; le Processor garde chaque source qu'il
peut donner au moteur et ne la libère que sur le fil des messages, quand plus rien ne la tient. Un changement de Kit de
16 samples de 10 s sous leurs voix tient en 110 µs au pire dans un bloc (borne du test 500 µs). L'écoute d'un son de piste
en VINTAGE dure son sample à sa hauteur (10,9 s au plus) ; Browse dit « 12-BIT GRAIN PLAYS 1.29 S ». Le lecteur ROM d'une
piste en VINTAGE prépare toujours ses mots : ses valeurs lissées et ses locks sont ceux que lit VINTAGE, les séparer n'est
pas un changement local.

Mesuré (`edDrumsVintageTest`) : à 8 kHz, la valeur tenue change 7 999 fois par seconde, 15 999 à +12 (x 2,0001) ; une
sinusoïde de 500 Hz joue 500,17 Hz puis 1 000,33 Hz à +12. Coût, 16 pistes en Sampler sans chaîne, bloc de 32 : lecteur
ROM 66,6 µs, VINTAGE 18,8 µs (1,18 µs par voix). Rien n'est alloué sur le fil audio.

Pas fait : charger la ROM d'une machine d'époque que l'utilisateur possède ; les noms des grains et de leurs paramètres
attendent la table du ticket 27.

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

## État (2026-11-11), le LOCKER

L'extension de stockage proposée dans `../stockage-proposition.md`, approuvée le même jour avec ses quatre
recommandations, est faite. Elle s'appelle LOCKER et prend, dans l'arbre LIBRARY de Browse, la place de SAMPLES, BY
SOURCE (même rang, même vert). Ses segments sont ALL, puis une source chacune (jusqu'à sept), FACTORY après les dossiers,
IN THIS SET quand le set en porte, et `+` qui ouvre le choix de dossier. Une source choisie par son segment montre sa
fiche dans la colonne de droite (chemin, nombre de samples, place, types les plus nombreux), avec RENAME et REMOVE
SOURCE ; un sample choisi montre la sienne, avec TYPE, RENAME, et KEEP IN LOCKER pour un sample du set. En bas de
LIBRARY : la place copiée et le nombre de sources, puis ADD FOLDER et IMPORT PACK.

Deux gestes. ADD FOLDER (comme un dossier glissé sur Browse, et `+`) lie le dossier : rien n'est copié. IMPORT PACK
copie les WAV, AIFF et FLAC que le LOCKER n'a pas encore, par empreinte, dans `Locker/Imported/<nom>/` du dossier de
données, à leur place dans le pack ; la source IMPORTED compte leur place, et son retrait efface sa copie seule, au
deuxième clic, la fiche disant ce qui sera effacé. Une source LINKED part au premier clic, ses fichiers restent. Les deux
gestes passent par le même parcours, sur un fil à lui (`LockerImport`, l'ancien `ElektronImport` renommé) : une flash
image trouvée donne la source FACTORY (ses seuls mots, `Locker/Factory.words`, au chargement suivant), et un toast dit
ce qui a été trouvé (samples, déjà connus, copiés, banque d'usine, fichiers non lus). Le parcours et les copies ne
touchent jamais le fil audio ; KEEP IN LOCKER écrit son WAV sur le fil des messages.

Index : `Locker/sources.txt` (genre, chemin, nom montré, une ligne par source), relu avant chaque écriture et suivi par
les autres instances ; `Locker/types.txt` et `Locker/names.txt` par empreinte, si bien qu'aucun fichier n'est renommé sur
le disque et qu'un type ou un nom se donne aussi à un sample d'usine. Un dossier de données d'avant le LOCKER est repris
une fois : `Samples/folders.txt` devient des sources LINKED, `Samples/types.txt` est copié, `Elektron/Machinedrum.words`
est déplacé en `Locker/Factory.words`.

FACTORY est une source comme les autres : ALL et SAMPLES, BY TYPE la comprennent désormais (ses 32 emplacements y sont
OTHER tant que l'utilisateur n'a pas choisi). La retirer efface ses mots ; elle n'est retirable que si le LOCKER les
tient (une banque lue dans `roms` ou par la variable ne l'est pas). Un set garde la copie et l'empreinte des samples de
l'utilisateur ; un sample d'usine n'y est que `factory="n"`, jamais d'audio, dans l'état comme dans un Kit. IN THIS SET
liste les samples des pistes qu'aucune source n'a par empreinte, une fois le LOCKER lu ; KEEP IN LOCKER écrit le sample
en WAV dans la source IMPORTED « FROM SETS », et les pistes qui le tenaient portent l'empreinte de ce fichier.

Le Menu n'a plus le bloc ELEKTRON CONTENT ni IMPORT MY CONTENT : il garde WINDOW et AUDIO. Le bouton IMPORT MY ELEKTRON
CONTENT et le segment FACTORY à part ont disparu. Les textes montrés ne nomment plus de marque : « FACTORY BANK NOT IN
THE LOCKER » à la place de « The factory samples need the Machinedrum's flash image », « Factory 05, not in the Locker »
pour une piste dont la banque manque.

Vérifié : `edDrumsSampleLibraryTest` (source LINKED ajoutée, renommée puis retirée, fichiers intacts ; IMPORT PACK copie
3 samples, WAV, AIFF et FLAC, un doublon et un texte laissés, 51 973 octets comptés, retrait qui efface la copie seule ;
type, nom de sample et nom de source repris par un rescan et une deuxième instance ; listes et mots d'avant repris ; avec
l'image, ses seuls mots écrits, 1 864 Ko, FACTORY à 32 emplacements, retirée avec ses mots) ; `edDrumsProcessorTest`
(un Kit réécrit sur l'emplacement 5 ne tient que `factory="5"` ; un état ouvert sans la source de son sample le montre
sous IN THIS SET, le charge sur une autre piste, et KEEP en fait un sample de FROM SETS) ; `edDrumsNoFlashTest` (sans
image, la piste sur l'emplacement 5 se tait, garde sa référence, et l'éditeur dit « Factory 05, not in the Locker » ;
Browse ne nomme aucune marque) ; `edDrumsEditorTest` à la souris (LOCKER dans l'arbre, dossier glissé, SEARCH, double
clic, glisser, TYPE, RENAME d'un sample et d'une source, LOAD ON TRACK, FACTORY, IN THIS SET et KEEP IN LOCKER, IMPORT
PACK, REMOVE SOURCE en deux clics pour une copie et en un pour un lien ; Menu sans ELEKTRON CONTENT ; 544 textes dessinés
sur les cinq listes de Browse, les segments du LOCKER et le Menu, aucun ne contenant ELEKTRON, MACHINEDRUM ni le mot UW).

Pas fait : ADD FOLDER et IMPORT PACK ouvrent le choix de dossier natif, que le test ne peut pas cliquer : il passe par le
glisser et par `Processor::addToLocker`. FACTORY ne se renomme pas (son nom n'est dans aucune liste). Le rapport du
dernier parcours (`Processor::lastImport`, `imports`) reste, puisque le toast le lit. Les formats propres aux machines
d'origine (projets, sons, SysEx) restent comptés et non lus.

## État (2026-11-11)

Corrections de la relecture du LOCKER. IMPORT PACK a désormais des bornes (`LockerImport::Bounds`) : au plus autant de
fichiers que le scan en liste (`SampleLibrary::MostFiles`, 20 000), un fichier d'au plus 160 Mo (le plus que lit
`readSampleFile`, 10,9 s de 8 canaux en flottants 32 bits à 384 kHz, fait 134 Mo) et 4 Go en tout ; ce qui dépasse est
compté (`Report::left`) et le toast le dit. Chaque fichier est copié à côté (`.part`) un mégaoctet à la fois, puis
renommé : une instance fermée pendant la copie arrête le parcours au mégaoctet suivant, efface le fichier coupé, et le
destructeur attend la fin du travail sans limite de temps (mesuré : 5 ms pendant une copie de 150 Mo). Un dossier
d'Imported que la liste ne connaît pas et qui contient un fichier est repris comme source IMPORTED au chargement
(`SampleLibrary::adoptImported`) ; le dossier vide d'un import qui n'a rien copié est effacé. La migration du dossier de
données écrit toujours sa marque, avec les fichiers qui n'ont pas pu être copiés, seuls réessayés ensuite : un fichier
effacé par l'utilisateur, ni `Factory.words` après REMOVE, ne revient plus. KEEP IN LOCKER ne fuit plus le flux si le
writer échoue, efface le fichier raté et signale à l'hôte que le set a changé (`withNonParameterStateChanged`). Un son de
piste ancien dont un LFO vise une autre piste suit la machine que le set y joue. Tests : `edDrumsSampleLibraryTest`
(bornes, reprise, copie interrompue), `edDrumsNoFlashTest` (migration avec un échec), `edDrumsProcessorTest` (LFO vers
T05, KEEP bloqué puis réussi), `edDrumsEditorTest` : verts. Le garde-fou du dépôt n'a pas tourné.
