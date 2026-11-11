# Samples, import Elektron, explorateur (moteur)

Type: AFK
Status: ready-for-agent
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
