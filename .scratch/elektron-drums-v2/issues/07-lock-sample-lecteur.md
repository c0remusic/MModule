# Lock de sample sur le lecteur du Sampler

Type: AFK
Status: done
Assignee:
Blocked by: 04

## Quoi

Un lock de sample fait jouer à un pas un autre sample du Locker, sur les trois lecteurs du Sampler. Il nomme le
sample par son empreinte : ni banque, ni numéro, ni limite. Il se pose depuis le menu de lock du pas. Lockstep charge
seul, hors du fil audio, les samples que les patterns d'une piste nomment, partagés par empreinte dans le processus ;
le fil audio passe d'un sample chargé à un autre sans allocation. Seuls les locks changent le sample, à leur trig : ni
cible de LFO, ni paramètre hôte. Sample absent : le pas joue le sample de la piste, le lock reste et rejoue dès que la
même empreinte revient ; la ligne d'état nomme ce qui manque. Le lock vit dans l'état, les Kits et les patterns.

## Critère de fin

- Test : un pattern à quatre samples locks joue chacun à son pas, à l'échantillon près, sans allocation sur le fil
  audio.
- Sample absent puis rendu : comportement de la spécification, ligne d'état comprise.
- État sauvé et relu : mêmes locks, même son.
- Latence et gigue rapportées avant et après.
- Gate vert, nouveau test inscrit dans sa liste.

## Avancement (2026-11-19) : tranche 1, moteur et Processor

**Moteur.** `Engine::lockSample` donne au Hit qui suit son sample, sur les trois grains. Le Hit suivant sans lock
reprend le sample propre de la piste (`setSample`). Le moteur place un sample dans la banque seulement quand il change,
sans allocation. Le Hit sonne au même échantillon que si l'hôte avait changé le sample juste avant : latence et gigue
inchangées, `HitDelay` sans gigue.

**Lock.** Un `Lock` porte maintenant une empreinte de 64 bits, sous le paramètre réservé `SampleLock` (0xFFFF). Il
vit donc dans le pool de Locks du pattern, et copier, coller ou faire tourner un pas le garde. `Locks::play` le passe
au moteur à chaque trig, ratchets compris. Un pas sans trig ne change pas le sample.

**Processor.** `takeLockSamples`, appelé par le timer, charge hors du fil audio les samples que les patterns nomment.
Il les prend d'abord à une autre instance du processus qui les tient (cache par empreinte), sinon au Locker : FACTORY,
fichier d'une source ou IN THIS SET. Le fil audio cherche l'empreinte dans une table publiée. Les samples chargés et
les tables restent jusqu'à la fin de l'instance, donc le fil audio ne lit jamais un sample libéré. Un sample absent du
Locker, une fois son scan connu, est nommé dans la ligne d'état (« N SAMPLES MISSING »). Son pas joue le sample de la
piste, et le lock rejoue dès que l'empreinte revient. Si le Locker n'a jamais été scanné, le Processor demande le scan.

**État.** Un lock s'écrit « paramètre:valeur:empreinte ». L'état garde aussi le nom de chaque sample locké, pour la
ligne d'état. Un état d'avant n'a aucun lock de sample.

**Tests.**
- `edDrumsSampleLockTest` (nouveau, au gate) : sur 12-BIT, VINTAGE et CLEAN, piste avec ou sans sample propre,
  quatre Hits lockés jouent, échantillon pour échantillon, ce que joue l'hôte qui change le sample avant chaque Hit.
  Rien n'est alloué. Coût : 16 Hits lockés sur des samples longs dans un même bloc, 286 à 293 µs au pire, sur les
  726 µs du bloc.
- `edDrumsProcessorTest` (`sampleLocks`) : quatre pas lockés jouent ce que joue un moteur piloté directement, sans
  allocation sur le fil audio. L'état les rend. Un sample absent est nommé et son pas joue le sample de la piste,
  jusqu'au retour de son fichier.

`edDrumsVintageTest`, qui borne le temps réel d'un changement de Kit, rejoint la liste des tests de temps du gate : il
avait fait rougir un gate sous 2,4 cœurs de charge.

Un sample FACTORY se locke par son numéro de slot (1 à 32), qu'aucune empreinte de 16 chiffres hex ne prend en
pratique. Un test du Processor le vérifie.

Mis de côté après revue :
- Le cache du processus ne retire pas les empreintes mortes.
- `lockerSample` répète la résolution FACTORY / fichier / IN THIS SET de `loadLibrarySample`, qui passe par
  `loadSample` pour ses références : une fonction commune viendrait avec la tranche 2.
- Chaque édition de pattern fait relire tous les pas à `takeLockSamples` ; un sample introuvable est recherché de
  nouveau à chaque fois.
- Les noms gardés dans l'état ne sont pas élagués quand un lock disparaît.

## Avancement (2026-11-19) : tranche 2, éditeur

Sur une piste Sampler, le menu + LOCK de Seq propose SAMPLE, sous VOICE. Il ajoute une lane SAMPLE. Un clic sur un pas
de cette lane ouvre Browse sur les samples du Locker, avec sa recherche, pour choisir le sample de ce pas. Le pointeur
posé sur une ligne écoute le sample. Un double-clic ou LOCK ON STEP pose le lock, puis Seq revient. Une autre page, ou
une autre piste, abandonne le choix. Le lock va dans le pattern dont la lane a été cliquée. Dans la lane, un pas locké
montre le nom court du sample, sans extension ; la colonne des noms donne le sample propre de la piste (BASE). Un
glisser horizontal sur les locks les retire ; le X de la lane retire la lane.

Un double-clic exige maintenant que ses deux pressions tombent sur la même page. Sinon, le second clic d'un double-clic
sur la lane tomberait sur une ligne de Browse et lockerait un sample au hasard.

Test d'éditeur (`edDrumsEditorTest`) : entrée SAMPLE, double-clic sur la lane sans lock écrit, écoute au survol,
LOCK ON STEP, double-clic sur une ligne, lane qui garde les deux locks, glisser qui les retire, abandon par une autre
page, X de la lane.

Mis de côté après revue :
- Le survol lit le fichier de chaque ligne traversée sur le fil de message.
- La lane SAMPLE reste cliquable si la piste quitte le Sampler ; ses locks n'y jouent pas.
- Les cas `SampleLock` sont répartis dans la grille, `laneSteps`, le dessin et le menu. Un type de lane viendra avec
  la seconde cible (couche, ticket 08).

Le nom court dans les lanes, critère du ticket 08, est fait ici pour la cible machine.

## Références

Spécification v2, §5.3 (`../elektron-drums/spec-v2.md`) ; ses tickets de décision dans `../lockstep-post-v1/issues/`.
