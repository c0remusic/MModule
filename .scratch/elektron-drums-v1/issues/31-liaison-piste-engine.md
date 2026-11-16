# Une seule liaison d'une piste vers l'Engine

Type: AFK
Status: done
Assignee: agent (session du 2026-11-16)
Blocked by: 

## Quoi

Revue d'architecture du 2026-11-15 (candidat 3) : trois cartes écrites à la main donnent les valeurs d'une piste aux
setters de l'Engine :

- `Processor::syncSound` (chaque bloc, seulement ce qui a changé, les Locks tenues à part) ;
- `Processor::applyTarget` (une Lock) ;
- `Processor::auditionTrackSound` (une piste entière sur un Engine à elle).

Ajouter Gap Sync a demandé cinq retouches. Déjà fait le 2026-11-15 : `markDue` (la liste « due » une seule fois) et
`sendAudition` appelé par l'audition.

Reste : une fonction qui donne une piste entière à un Engine depuis ses valeurs, utilisée par l'audition et par
`syncSound` là où une valeur est « due », pour qu'un nouveau paramètre de piste ne se touche qu'à un endroit. Éventuellement,
pour `syncShared`, chaque Spec portant sa cible des Sends.

Touche le chemin audio : décidé avec l'utilisateur d'en faire un ticket.

## Critère de fin

- Un paramètre de piste ajouté ne demande qu'une retouche dans la liaison (le dire sur un exemple).
- L'audition d'un son de piste et la piste jouée donnent les mêmes échantillons (test).
- Latence et gigue mesurées avant et après, sans écart ; gate vert.

## Références

Rapport de revue d'architecture du 2026-11-15. `edDrumsProcessor.cpp` : `syncSound`, `applyTarget`,
`auditionTrackSound`, `syncShared`.

## Réponse

Fait le 2026-11-16.

- `Processor::giveTrack` (`edDrumsProcessor.cpp`) est la seule liaison du son d'une piste vers un Engine : tout ce que
  garde un son de piste sauf la machine (Machine 1-24, note, niveau, pan, chaîne, LFO, envois, accent, Gap Sync). Elle
  lit les valeurs par un lecteur : `PlayedValues` (la piste jouée : valeurs de l'hôte, d'un Lock tant qu'il tient,
  seulement ce qui a bougé ou qui est « dû ») ou `KitValues` (les valeurs d'un Kit, toutes). `syncSound` et
  `auditionTrackSound` l'appellent ; `chainValues` et `lfoValues` lisent par les mêmes `chainFrom`/`lfoFrom`.
- Restent hors d'elle, par choix : la machine (son changement précède la lecture des valeurs par défaut,
  `syncSound`) ; la sortie et les chokes, qui ne sont pas du son (un son de piste les laisse, `soundSuffix`) ;
  `applyTarget`, l'aiguillage d'un Lock par genre.
- Exemple : un réglage de piste surveillé de plus (le niveau de la couche sample de la v2, disons) se déclare dans
  `TrackParameters` et se donne d'une ligne dans `giveTrack` ; la piste jouée, l'audition, les Kits et les états le
  suivent sans autre retouche. S'il est verrouillable, son genre de Lock existe déjà (`LockKind`) ou s'ajoute à
  `applyTarget`.
- Test : `edDrumsProcessorTest` (`kitFiles`) joue un son de piste sur la piste 1 d'une instance neuve et compare à
  son audition : 57 088 échantillons à 0,5 LSB au plus, l'arrondi des 16 bits de l'audition. Une mutation (audition à
  la vélocité 90) le fait échouer à 44,8 LSB.
- Latence et gigue : la liaison ne touche ni le chemin du trig ni le rendu. `edDrumsVst3HostTest` vérifie toujours un
  Hit `HitDelay` après sa note où qu'elle tombe, et `edDrumsEngineTest` une gigue nulle (garde-fou du commit).
