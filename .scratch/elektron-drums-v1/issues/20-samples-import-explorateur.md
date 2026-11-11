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
