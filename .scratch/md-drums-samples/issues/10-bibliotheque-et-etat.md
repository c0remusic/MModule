# La bibliothèque des Samples et l'état du set

Type: task
Status: resolved
Assignee: c0remusic (session du 2026-10-17)
Blocked by: 09

## Question

Troisième tranche, comme « Où vivent les Samples » l'a décidé : une bibliothèque des Samples de l'utilisateur dans le
dossier de données, un fichier par Machine ROM, partagée par les instances comme le Bank et suivie quand une autre y
écrit ; l'état du plug-in embarque les Samples que son Kit joue ; chaque instance joue sa copie, prise quand elle en
reçoit un, charge un Kit du Bank ou un set. Tests : un set restauré sans bibliothèque joue ses Samples ; deux
instances, deux Samples sur ROM-33, chacune le sien ; un Kit du Bank joue ce que la bibliothèque a ; un état sans
Sample reste celui d'avant.

## Answer

Construit le 2026-10-17 :

- `mdDrums::SampleLibrary` (`mdDrums/mdDrumsSampleLibrary.*`) : `ROM-01.smp` à `ROM-48.smp` dans le dossier `Samples`
  des données, chacun le Sample tel que le message `Sample` de MoDule le tient (0x07 : la ROM, longueur, boucle, rate,
  le nom du fichier, puis les mots en 7 bits) ; partagée par les instances comme le Bank, suivie quand une autre y
  écrit.
- Chaque instance joue sa copie, tenue par son Device : elle prend celle de la bibliothèque quand elle charge un
  fichier (`Processor::loadSampleFile`, qui l'y range) ou un Kit du Bank (`Processor::takeSamples`, pour les Machines
  ROM que le Kit joue). Ranger un Sample dans la bibliothèque ne change aucun son par soi-même.
  `Processor::removeSample` rend celui d'usine, ou le silence sur ROM-33 à ROM-48.
- L'état du Device porte un message `Sample` pour chaque Sample de l'utilisateur sur une Machine ROM que son Kit
  joue : un set sonne comme enregistré là où ils ne sont pas rangés. Ouvert, il donne à chaque Machine ROM le Sample
  qu'il porte, ou celui d'usine. Un état sans Sample reste celui d'avant.
- La prévisualisation du Hit dans l'éditeur demande avec les Samples de l'instance.
- Vérifié : `mdDrumsProcessorTest` (« kept Samples » : un WAV chargé sur ROM-33 entre dans la bibliothèque et joue ;
  un set ouvert avec une bibliothèque vide joue sa copie et la laisse vide ; un Kit du Bank prend celui de la
  bibliothèque, différent ; retiré, ROM-33 se tait ; deux instances, deux Samples sur ROM-33 ; un set dont le Kit ne
  joue aucun Sample de l'utilisateur n'en porte aucun), `mdDrumsBankTest` (une bibliothèque suit le Sample et le
  retrait d'une autre ; un fichier qui n'est pas un Sample ignoré).
- Reste au lâcher (« Où lâcher ») : choisir une Machine ROM dans le navigateur prend-il le Sample de la bibliothèque,
  comme un USR ; et ce que l'éditeur montre du nom.
