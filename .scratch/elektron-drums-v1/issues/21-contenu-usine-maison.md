# Contenu d'usine maison

Type: HITL
Status: ready-for-human
Assignee: 
Blocked by: 19, 20

## Quoi

~16 kits et quelques dizaines de sons de piste faits par nous, et un petit jeu de samples maison (faits par nous ou
CC0) pour les machines ROM ; rien d'Elektron. Programmés avec l'utilisateur, qui juge à l'oreille.

## Critère de fin

- Banque de départ livrée et chargée à la première ouverture ; l'utilisateur l'a validée.
- Licence de chaque sample notée.

## Références

Spécification §8. Ticket « Format des kits et de l'état ».

## Écoute de l'utilisateur (2026-11-15)

Le brouillon de seize kits (`C:\dev\elektron\elektron-work\factory-draft\`, son `README.md`, rendu le 2026-11-13
sans image flash, outil et description `tool\kits.json`), écouté sur la page d'écoute `C:\dev\elektron\elektron-work\listen-all\index.html`, hors dépôt : quatorze kits OK, sans retouche
(Boom Room, Steel Floor, Wire Funk, Rust Belt, Dust Tape, Glass Shards, Wood And Skin, Market Day, Six Operators,
Static Field, Open Circuit, Hyper Metal, Deep Garage, Breakline), puis Night Shift et Crossroads : les seize OK.

Reste : livrer la banque, chargée à la première ouverture ; les sons de piste ; les samples
maison ou CC0 pour le Sampler (aucun kit du brouillon ne l'utilise) et la licence de chacun.

## Banque livrée (2026-11-15)

Décision de l'utilisateur : une nouvelle instance s'ouvre sur Boom Room avec les seize patterns.

Les seize kits validés et leurs patterns (`.edkit`, `.edpattern`, 152 Ko, faits par le plug-in lui-même par l'outil du
brouillon, rien d'Elektron) sont dans les données du plug-in (`edDrumsPlugin/resources/factory/`, cible
`edDrumsFactoryAssets`). `Processor::factoryStart`, à la construction :

- première ouverture, c'est-à-dire pas encore de dossier Kits dans le dossier de données : les seize kits remplissent
  les emplacements 01 à 16 de la banque. Une ouverture suivante ne la remplit plus, même vidée ;
- toute nouvelle instance : patterns A01 à A16 = ceux d'usine, chacun chargeant à son départ l'emplacement de même
  numéro (où la première ouverture a mis son kit) ; le kit joué est Boom Room, lu dans les données du plug-in, son
  emplacement 01 si la banque l'y tient encore. Un set restauré remplace le tout (`setStateInformation` vide les 128
  patterns avant de lire les siens).

`ED_DRUMS_BLANK_START`, que les tests posent (comme `GEARMULATOR_DATA_ROOT`), garde l'ancien départ : instance vide,
banque laissée telle quelle ; `edDrumsProcessorTest`, `edDrumsEditorTest` et `edDrumsVst3HostTest` partent ainsi.
`edDrumsProcessorTest` (`factory`, dans un dossier de données à lui, sans la variable) vérifie les seize noms en
emplacements 1-16 et le 17 vide, Boom Room joué (piste 1 SOFT KICK, niveau 77) depuis l'emplacement 01, A01-A16 sur
leurs emplacements, A17 sans kit ; puis, l'emplacement 4 vidé, qu'une instance suivante ne le remplit pas.

Reste : les sons de piste ; les samples maison ou CC0 pour le Sampler et la licence de chacun.

## Sons de piste livrés (2026-11-15)

118 sons de piste, chacun une piste jouée d'un des seize kits validés, enregistrée par `saveTrackSound` (outil hors
dépôt `elektron-work\listen-all\tool\listenfx.exe --sounds`, sans image flash) ; deux égaux à un précédent écartés.
Nommés « Kit Machine » (« Boom Room SOFT KICK »), un numéro ajouté quand un kit joue deux fois la même machine
(« Hyper Metal HYPER FM 2 ») : livrés tels quels, décision de l'utilisateur. 182 Ko, dans
`edDrumsPlugin/resources/factory/sounds/`. Première ouverture du dossier TrackSounds (pas encore de dossier) : ils le
remplissent ; ensuite jamais plus, même retirés. `edDrumsProcessorTest` (`factory`) compte les 118 et y trouve Boom
Room SOFT KICK sur sa machine, puis vérifie qu'un son retiré ne revient pas à l'ouverture suivante.

Reste : les samples maison ou CC0 pour le Sampler (aucun kit ni son ne l'utilise) et la licence de chacun.
