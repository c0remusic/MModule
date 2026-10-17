# Une Machine utilisateur jouée par le moteur

Type: task
Status: resolved
Assignee: c0remusic (session du 2026-10-17)
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

## Answer

Construit le 2026-10-17 :

- `md::engine::VoiceEngine` étend les tables d'appel de 1.63 (193 types) à 211 comme X.20, en Y:`$145D38`,
  `$145E0B` et `$145EDE`, et y fait lire le programme (ses bases à P:`$8A` et `$95`, vérifiées) et le harnais ; tout type
  au-delà de 1.63 joue la Machine vide tant qu'aucune Machine utilisateur ne le remplit. `installUserMachine` place le
  code d'un paquet à `$020000 + $E000·n`, relogé, ses imports 1, 6, 7 et 8 sur la disposition de 1.63, et écrit ses
  INIT, TRIG et RENDER au type 195 + n ; `reset()` les garde. Refusés : les imports qu'il ne fournit pas (services
  Sample et RAM, anneaux), NEIGHBOUR et STEREO PAIR, plus de code que la place d'un numéro, un coût déclaré qu'X.20
  refuse.
- `HostModel` joue USR-01 à USR-16 (ids 194 à 209) : les défauts du paquet à l'assignation, ses huit paramètres
  convertis à chaque tick depuis le tableau de la voix après lissage et LFO (`md::mds::convertFine`, une valeur entre
  deux réglages tombant entre leurs mots).
- `EngineT::installUserMachine` compile le code à l'avance (un Hit à ses défauts sur le DSP voix seul, qui reprend sa
  mémoire ensuite, comme `warmUpVoices`) ; `mdDrums::Engine` l'expose. L'hôte nu de `mdsPlay` devient
  `md::mds::BareHost`, partagé avec les tests ; `md::mds::buildPackage` fait un paquet de ses parties.
- **Vérifié** (`mdDrumsEngineTest`, « User Machines ») : une Machine du test (une scie qui décroît, assemblée par le
  test, sa fin de boucle relogée) à USR-02 sur la Track 3 donne sur sa voix les échantillons de l'hôte nu, 0 de
  différent sur 3 008 depuis son Hit ; les autres Tracks, 0 bloc de différent sur 1 440 ; installée et compilée en
  6,7 ms ; un Hit à vélocité 127 sonne 87 échantillons après son déclencheur sur sa sortie (HitDelay 83, plus un départ
  de 4 comme la plupart des Machines, `Engine::voiceStart` par défaut) ; retirée, elle se tait ; import 10 et
  NEIGHBOUR refusés.
- **Latence** note→son de TRX-BD, avant et après : 54 à 85 échantillons sur les blocs du moteur (gigue 31), 85
  précis à l'échantillon (gigue 0), inchangée.
