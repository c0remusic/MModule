# Kits, banque et sons de piste

Type: AFK
Status: resolved
Assignee: 
Blocked by: 18

## Quoi

Kit = tout le son (pistes, chaînes, LFO et cibles, envois, sorties, chokes, effets, master), rien du séquenceur.
Banque de 64 kits dans le dossier de données, partagée entre instances, relue avant chaque écriture, écrite par
emplacement (comme `mdDrumsBank`). Un pattern garde un kit à charger ou « garder le kit en cours ». Sons de piste en
bibliothèque partagée. Patterns exportés et importés en fichier.

## Critère de fin

- Kit sauvé puis rechargé : mêmes échantillons. Deux instances écrivent chacune un emplacement sans perdre l'autre.
- Kit ancien à paramètre inconnu : défaut.

## Références

Spécification §8. Ticket « Format des kits et de l'état ».

## État (2026-11-11)

Version réduite faite : banque de 64 kits, un fichier gzip XML par emplacement dans `Kits` du dossier de données
(`edDrumsPlugin/edDrumsKits.*`), relu avant chaque écriture, remplacé par fichier temporaire ; un kit garde tous les
paramètres de son par identifiant (sauf Tempo, Oversampling, Pattern), le sample de chaque piste et un nom. Page Kits de
Lockstep : charger, sauver, renommer, effacer. Pas faits : sons de piste en bibliothèque, kit chargé au départ d'un
pattern, import et export de patterns et de kits, contenu d'usine (ticket 21).

## État (2026-11-11, suite)

Sons de piste faits : `edDrumsPlugin/edDrumsTrackSounds.*`, un fichier gzip XML par son dans `TrackSounds` du dossier
de données, partagé par les instances et suivi par le timer. Un son garde, par suffixe d'identifiant, la machine, Machine
1 à 24, la note, la chaîne, l'ampli, les deux LFO et leurs cibles, les envois, l'accent et le sample de la piste (copie
et empreinte) ; ni la sortie ni les chokes, qui sont le routage du kit. Il se charge sur n'importe quelle piste comme un
kit se charge (relu par l'hôte, aucune édition annoncée, les locks de la piste rendus). L'écoute rend le coup du son sur
un moteur à part, sur le fil du message, et le joue comme l'écoute d'un sample.

Kit au départ d'un pattern fait : `Pattern::kit` (-1 garde le kit en cours, gardé dans l'état et dans un fichier de
pattern). Les kits que les patterns demandent sont lus d'avance sur le fil du message (`Processor::prepareKits`, quand
le choix change, à l'état, à chaque fichier d'emplacement réécrit) ; le fil audio charge le kit à l'échantillon où le
pattern commence (`Events::patternStart` du séquenceur), avant les événements de cet échantillon, sans fichier ni
allocation, ne donne au moteur que les samples qui changent, puis le fil du message le prend pour kit joué. Un kit lu
d'avance remplacé n'est libéré qu'après la fin du bloc qui a pu le lire.

Fichiers faits : export et import d'un kit (`.edkit`, le fichier même d'un emplacement) et d'un pattern (`.edpattern`,
ses pas, ses locks et son kit). Page Kits : bloc PATTERN (LOAD AT ITS START, KEEP THE KIT, IMPORT et EXPORT PATTERN),
bloc TRACK SOUND (SAVE SOUND, LOAD SOUND), IMPORT et EXPORT du kit. Browse : KITS (la banque, double clic ou LOAD KIT
charge) et TRACK SOUNDS (recherche, clic écoute, double clic charge sur la piste affichée, glisser sur une piste).

Critères vérifiés par `edDrumsProcessorTest` (`kits`, `kitFiles`) et `edDrumsEditorTest` : kit sauvé puis rechargé,
mêmes échantillons ; deux instances gardent chacune leur emplacement ; identifiant manquant, défaut ; un son de piste
sauvé de la piste 3 joue les mêmes échantillons sur la piste 7 d'une autre instance ; le kit du pattern 2 est chargé à
l'échantillon 88200, celui de son premier pas, comme le moteur à qui on le dit, sans allocation sur le fil audio ; un kit exporté puis importé est le même
fichier et charge les mêmes valeurs ; un pattern exporté puis importé garde ses pas et son kit.

Pas faits : la liste TRACKS des 16 pistes du kit choisi dans la page Kits (maquette), le contenu d'usine (ticket 21).
