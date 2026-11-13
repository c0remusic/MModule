# Séquenceur interne

Type: AFK
Status: resolved
Assignee: 
Blocked by: 11

## Quoi

Le séquenceur de la spécification §7 : 128 patterns de 64 pas, longueur et échelle par piste, pas complets (trig,
note, vélocité, durée, 65 conditions, micro-timing, ratchets, accent, slide, locks sans plafond, locks sans note),
swing, mutes, fill, chaînes ; conditions aléatoires à graine ; transport de l'hôte seul ; pattern par paramètre
« Pattern » ou program change, à la fin du pattern en cours ; mode LIVE ; copier, coller, effacer, décaler ; export
MIDI. Les conditions et le micro-timing peuvent reprendre le CPU #1 du Syntakt (`0x401181D4`, `0x4008B882`).

## Critère de fin

- Critère 6 de la spécification : pattern de test rendu deux fois à l'identique, chaque trig à l'échantillon
  attendu, gigue 0, changement de pattern au bon pas, export MIDI relu.
- Coût du séquenceur mesuré.

## Références

Spécification §7, §12. Tickets « Séquenceur interne », « Fonctions de MoDule Seq », « Séquenceurs des machines ».

## Noté par le ticket 11 (2026-11-06)

Le séquenceur vit dans `edDrums`, sans JUCE, et fusionne ses trigs aux notes de l'hôte là où `processBlock` coupe son bloc. Il faudra une API de Locks : le
moteur n'en a pas. L'identité de deux rendus ne tient que depuis la même phase de grille : une voie Syntakt dépend de son bloc à
48 kHz, et les voix éteintes gèlent (spécification §3). Le critère 6 démarre donc ses deux rendus de la même position, moteur neuf.

## Noté par le ticket 13 (2026-11-06)

`Engine::position()` continue au tempo quand l'hôte est arrêté, pour les LFO : ce n'est pas « l'hôte joue ». Porter `isPlaying` depuis `sync`, et lire `position()` en lecture seulement : sa tolérance donne les échantillons
indépendants des blocs du critère 6.

## État (2026-11-08)

Cœur fait (`edDrums/edDrumsSequencer.*`, test `edDrumsSequencerTest`) et branché au plug-in : 128 patterns de 64 pas,
longueur et échelle par piste (1/8 à 2x), swing, micro-timing ±23/24, ratchets qui gardent les locks du pas, onze types
de condition (probabilité et A:B à tout réglage, FILL, PRE, NEI, 1ST et leurs négations) tirées d'une graine, locks sans
plafond, locks sans note, mutes, changement de pattern à la fin du pattern en cours par le paramètre « Pattern » ou un
program change. Les événements tombent au même échantillon quelle que soit la taille de bloc. Le plug-in le passe au
thread audio par trois copies, sans verrou ni allocation. Pas faits : slides (le pas les porte, rien ne les joue), mode
LIVE, chaînes, export MIDI, note et durée par pas ; l'état ne garde pas mutes, fill ni graine.

## État (2026-11-11)

Les deux critères de fin sont tenus. Le critère 6 se vérifie à deux niveaux. Dans `edDrumsSequencerTest`, un pattern qui
mêle longueurs et échelles par piste (12 pas en 2x, 8 pas en 1/2x, 5 pas en 3/4x), swing 60 %, micro-timing, ratchet de 3,
probabilité à graine, cycle 1:2, slide, locks et lock sans note se rend deux fois à l'identique et à six tailles de bloc ;
les 54 Hits tombent chacun à l'échantillon que donne leur temps, l'écart le plus grand vaut 0 échantillon, donc gigue 0 ;
le pattern demandé à 9,3 noires part à 12, fin du passage en cours. Dans `edDrumsProcessorTest`, le même genre de pattern,
sur de vrais paramètres, rend à travers le plug-in les mêmes échantillons deux fois et en blocs d'hôte de 37, 128 et 512.
L'export MIDI est relu deux fois : par un lecteur à nous dans le test du cœur, et par `juce::MidiFile` dans le test du
processeur. Coût mesuré : 1,6 à 2,0 µs par bloc de 512 échantillons pour 16 pistes pleines en 2x avec locks, conditions et
ratchets, soit environ 0,015 % d'un cœur à 44,1 kHz.

Fait depuis le 2026-11-08 :

- Note et durée par pas. La note du pas est un lock de la Note de la piste pour ce Hit (`NoNote` garde celle de la piste),
  la durée (en huitièmes de pas, 0 pour INF, le défaut) donne un événement `off` après le dernier Hit du ratchet, qui
  devient `noteOff` du moteur ; un Hit suivant de la piste annule la fin d'un Hit précédent, un arrêt ou un saut finit tout
  Hit tenu. INF par défaut pour ne pas changer ce qui sonnait déjà ; à revoir si l'on veut le 1/16 d'Elektron.
- Les 65 conditions sont offertes : ALWAYS (le 100 % du mockup), 21 probabilités, FILL, PRE, NEI, 1ST et leurs contraires,
  35 cycles de 1:2 à 8:8. Le cœur les jouait déjà.
- LIVE (bouton de Detail) : une note de l'hôte devient un trig du pas le plus proche avec le micro-timing qui le place au
  moment joué, arrondi vers le bas pour qu'il ne rejoue pas dans le même tour ; un paramètre de piste bougé dans un geste
  devient un lock du pas joué. L'automation de l'hôte, sans geste, n'enregistre rien. Le fil audio passe les notes par une
  file sans verrou, le minuteur les écrit. LIVE n'est pas gardé dans l'état.
- Chaînes : entrées pattern, répétitions, FILL ; la place dans la chaîne se déduit de la position de l'hôte seule, la
  chaîne boucle, un pattern demandé la remplace à la fin du pattern en cours. Le fill d'une entrée vaut pour ses
  conditions FILL.
- Solo à côté des mutes ; mutes, solos, fill, graine, chaîne et chaîne jouée sont gardés dans l'état.
- Longueur commune de 1 à 512 pas (t-ui-spec), sur Detail et Overview.
- Onglets Overview (16 rangées de 32 cases carrées, copies d'une piste courte à 55 %, fin de pattern par rangée, M et S,
  FILL, LENGTH, pages, mêmes gestes que Detail, double clic qui rétablit et ouvre le pas) et Patterns (grille 8 x 16,
  REQUEST, COPY, PASTE, CLEAR, MIDI EXPORT, chaîne avec APPEND ON, EMPTY, PLAY, glisser d'une case dans la chaîne,
  glisser d'une entrée pour la déplacer ou l'ôter, FILL, - et +).
- Copier et coller un pas avec ses locks (STEP), une piste, un pattern ; effacer une piste, un pattern ; décaler une piste.
- Export MIDI d'un pattern en fichier choisi : 960 ticks par noire, notes 36-51 canal 1, sans locks.

Pas fait : le glisser-déposer du MIDI vers l'hôte (la spec le demande, le fichier choisi le remplace pour l'instant) ; le
mode automatique de la longueur commune (retour à zéro) ; la couleur des pistes dans Overview ; les mouvements de knobs
venus d'un contrôleur de l'hôte sans geste ne s'enregistrent pas en LIVE.

## État (2026-11-13)

Trois des quatre restes sont faits ; la couleur des pistes est laissée.

- Glisser-déposer du MIDI vers l'hôte. Glisser le bouton MIDI EXPORT de Patterns écrit le MIDI du pattern choisi dans
  un dossier temporaire à lui (`Lockstep MIDI` sous le dossier temporaire du système, `Processor::patternMidiForDrag`),
  puis le donne à l'hôte par `DragAndDropContainer::performExternalDragDropOfFiles`, une fois par appui. C'est le même
  export que le fichier choisi, et un clic garde ce choix de fichier. `edDrumsProcessorTest` vérifie que le fichier est
  écrit à cet endroit, sous le nom `Pattern A04.mid`, et qu'il est identique octet pour octet à l'export. Le dépôt dans
  Live reste à vérifier à la main.
- Mode automatique de la longueur commune. Un double clic sur LENGTH (Overview) ou sur PAT LEN (Detail) ramène la
  longueur à zéro, c'est-à-dire automatique (`Pattern::AutoLength`). Elle vaut alors le plus petit nombre de mesures de
  16 pas qui contient la piste la plus longue, prise à son échelle (`Pattern::length()`, la formule `plen()` de t.js).
  Le séquenceur, l'export MIDI et l'affichage lisent `length()`. Un glisser part de la longueur jouée, et l'état garde
  le zéro. `edDrumsSequencerTest` donne 16 par défaut, 32 pour une piste de 20, 512 pour 64 pas en 1/8x. Il vérifie aussi
  ce qui joue : une piste de 12 pas sonne à 0, 3 et 6 noires, puis repart à 8 et non à 9. `edDrumsEditorTest` vérifie le
  double clic et le glisser qui suit. Le second appui d'un double clic tombe sur le sélecteur que le premier clic a
  ouvert : il est passé au champ dessous. Le défaut reste 16 fixe, pour ne rien changer à ce qui sonnait. Le mockup part
  en automatique, c'est à trancher.
- Couleur des pistes dans Overview : pas faite. Overview prend la couleur portée par le numéro de chaque rangée, et
  celle-ci vient du choix COLOR (t-ui-spec), que le ticket 22 a laissé de côté. Aucune couleur de piste n'existe ni dans
  l'état ni dans le Processor : la dessiner demande d'abord COLOR.
- LIVE depuis un contrôleur de l'hôte. Un paramètre de piste que l'hôte change sur le fil audio pendant que LIVE est
  actif et que le transport joue devient un lock du pas joué. C'est le chemin de VST3 pendant la lecture. L'écouteur
  pousse le changement dans une file sans verrou (`liveHostChange`) et le minuteur l'écrit. Un tour d'un relais Push est
  pris de même, sur le paramètre qu'il bouge. Sur le fil des messages, un changement sans geste n'enregistre toujours
  rien. `edDrumsProcessorTest` règle le Pan de la piste 4 depuis un autre fil, comme un hôte, et relit le lock au pas 4.
  Limite : VST3 ne distingue pas un contrôleur de l'automation, donc l'automation de l'hôte lue pendant LIVE
  s'enregistre aussi. Rien n'est alloué ni verrouillé sur le fil audio : la file de 256 est réservée à la construction, et
  une file pleine perd le mouvement.
