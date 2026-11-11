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
