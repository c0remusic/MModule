# Une Machine utilisateur jouée par le moteur

Type: task
Status:
Blocked by:

## Question

Première tranche : `mdDrums::Engine` (et `EngineT` de mdEngine) joue une Machine utilisateur sur une Track, comme
« Où tourne le code d'une Machine utilisateur » l'a décidé. Installer un paquet MDS1 (`md::mds::Package`,
`mdEngine/tools/mds/`) à un numéro USR-01 à USR-16 : son code relogé dans `$020000-$0FFFFF` du DSP voix, les tables
d'appel copiées à 211 entrées comme X.20 (types 195 à 210), ses imports posés sur la disposition de 1.63 (table de
sinus `$148000`, mémoire externe `$135600 + $600·t`, brouillons X:`$000` et Y:`$000`, [research/04](../research/04-voice-dsp-memory.md)),
ses huit paramètres convertis dans Y:(R6+1..8) à chaque changement ; INIT à l'assignation, TRIG au Hit, RENDER à
chaque bloc par le répartiteur. Installer se fait hors du thread audio et compile le code à l'avance. Tests dans
`mdDrumsEngineTest` : une Machine utilisateur de test (un paquet construit par le test, aucune des 43) sonne comme
`mdsPlay` la joue, les Tracks intégrées restent identiques au bit près, son départ mesuré comme `Engine::voiceStart`.
Latence note→son et gigue rapportées avant et après.
