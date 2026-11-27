# Import des sons Rytm dans la couche sample

Type: AFK
Status: done (essai dans Live à venir)
Assignee: Claude (session 2026-11-27)
Blocked by: 06

## Quoi

Un son Rytm importé met son sample dans la couche sample, par traduction : TUNE ±24 dans les ±5 octaves, STRT/END
en STRT + LEN avec le sens, LEV au carré, fondu de fin. Format importé proposé : le dump SysEx d'un son Rytm (`.syx`),
à confirmer au début du ticket. Le sample lui-même vient du Locker de l'utilisateur, jamais du contenu d'usine.

## Critère de fin

- Test : sons Rytm de référence importés ; couche jouée contre la sortie de la couche du Rytm à l'oracle, par les
  tests Q.
- Gate vert, nouveau test inscrit dans sa liste.

## Décisions (2026-11-27, avec l'utilisateur)

- **Format : le dump d'un son seul** (`.syx`, SysEx 0x53, ou 0x59 pour le tampon de travail), au format que décrit
  libanalogrytm (MIT, lu sur GitHub, rien recopié) : 201 octets, 162 décodés, en-tête `F0 00 20 3C 07`, 7 bits par
  groupes de 8 octets, somme et longueur sur 14 bits. Page SAMPLE aux octets décodés 0x2C TUNE, 0x2E FINE, 0x30 SMP,
  0x32 BR, 0x34 STRT et 0x36 END (16 bits), 0x38 LOOP, 0x3A LEV ; nom en 0x0C, 15 octets. Pas de Kit `.syx`.
- **Le sample se choisit dans le Locker.** Le dump ne porte que le numéro de l'emplacement du +Drive, ni nom ni audio :
  l'import règle les paramètres de la couche, puis demande son sample au Locker.
- **Entrée : le bloc SAMPLE LAYER.** Un `.syx` glissé sur le bloc, ou choisi par son LOAD, s'importe sur la couche de
  la piste.
- **Références fabriquées.** Pas de dumps de l'utilisateur : des dumps synthétiques au format ci-dessus, offsets
  confirmés au banc oracle3 (OS 1.74), et la sortie de la couche du Rytm relevée au banc contre la nôtre.

Décidé ensuite avec l'utilisateur, après les mesures :

- **START et LENGTH restent en 127es.** STRT et END du Rytm (120es avec leur 1/256) se traduisent au plus proche :
  le début peut se décaler jusqu'à 1/254 du sample, la boucle s'allonger ou se raccourcir d'autant.
- **Fin du Rytm pour les sons importés**, en donnée cachée : ni paramètre hôte ni contrôle, gardée dans l'état, les Kits
  et les sons de piste. L'import la pose ; un sample chargé ensuite sur la couche la retire.

## Relevés (2026-11-27)

Au banc oracle3 (`tools\analog\rytm_layer_vectors.py`, `rytm_end_probe.py`, hors dépôt, rien du firmware recopié) :

- La page SAMPLE du dump (octets décodés 0x2C-0x3B) a l'ordre et les largeurs des mots de paramètres de piste que lit
  l'OS 1.74 (+0x10-+0x1F, valeur << 8). Le banc écrit les octets du dump tels quels dans ces mots.
- **Fin sans LOOP** (relevée, absente des lois de « Où le Rytm rend sa couche sample ») : le Rytm ne s'arrête pas à END.
  Chaque bloc dont la fenêtre DMA arrive à moins de 139 échantillons source de END vise le quart du gain où il commence,
  en rampe linéaire sur le bloc. La voix lit au-delà de END et se tait quand la fenêtre le dépasse : environ quatre blocs,
  2,7 ms à 48 kHz. Le code compare à END − 0x8B (`0x4011C424`) puis divise par 4 (`0x4011CEB0`).
  - En avant, cette fenêtre a un bloc d'avance sur la lecture ; à l'envers, elle est la lecture même.
  - Une lecture à l'envers du Rytm sonne comme la nôtre partie 14 échantillons sous la fin, contre 6 pour le DT2.
    Meilleur de 10 à 18 : 44 dB.
- La couche suit le Rytm à 31,5 échantillons près sur chaque cas, qu'elle mène.

## Avancement (2026-11-27)

- `rytm::readSound` (`edDrumsRytmSound.*`) lit le dump : en-tête, 7 bits, somme, longueur. Il refuse une autre machine,
  un Kit, un OS d'avant 1.70, un dump coupé ou modifié. `rytm::layerOf` traduit la page en réglages de la couche :
  - TUNE ±24 avec FINE au 1/128 ;
  - STRT/END en START et LENGTH, END avant STRT à l'envers ;
  - LOOP autour de la portion ;
  - LEV et BR tels quels, la couche ayant les lois du Rytm ;
  - CHROMATIC SAMPLE en KEY TRACK [INFER, d'après libanalogrytm] ;
  - l'enveloppe ouverte.
- `Dt2Player::setRytmEnd`, par `SampleLayers::setRytmEnd` et `Engine::setLayerRytmEnd`, donne la fin du Rytm.
  `Processor::setRytmEnd` la tient dans un atomique par piste, donné au moteur à chaque bloc. Elle est gardée dans
  l'état (`tNN_rytmEnd`), les Kits (Save s'allume quand elle diffère), les Kits de pattern, les sons de piste, l'aperçu et
  l'écoute.
- Éditeur : un `.syx` glissé sur le bloc SAMPLE LAYER, ou choisi par son LOAD, règle les 14 valeurs et la fin. Browse
  s'ouvre ensuite sur le Locker pour son sample (« LOAD ON LAYER n », double-clic), et un toast rappelle l'emplacement
  +Drive.
- Face aux 18 cas de l'oracle (`edDrumsRytmSoundTest`, `RYTM_LAYER_VECTORS`) :
  - Cas aux bornes exactes : 40 à 45 dB, enveloppe à 0,03 dB ; 0,25 dB avec la fin ou BR.
  - TUNE vers le haut : 29 à 31 dB. La table de hauteur du Rytm, tronquée, est jusqu'à 0,4 cent plus basse.
  - BR 48 : 27,6 dB.
  - Cas entre deux 127es : le décalage mesuré est celui que la traduction prédit, à 0,25 échantillon près, puis 14 à
    30 dB.
  - Boucle : 2 400 échantillons au Rytm, 2 419 pour la couche, comme ses 127es le donnent ; niveau à 0,02 dB.
- Tests : `edDrumsProcessorTest` (`rytmEnd` : état, Kit et Save, son de piste) et `edDrumsEditorTest` (dépôt, valeurs,
  Browse, double-clic, `browse-rytm-sound.png`).
- Revue, corrigé : la fin d'un Kit de pattern va au moteur dans le bloc du changement ; une voix à pas lent s'arrête sous
  −120 dB ; un glisser de Browse sur un pad abandonne le choix du sample de la couche ; un `.syx` posé hors du bloc dit où
  le poser ; le nom ne garde que l'ASCII imprimable.
- Laissé :
  - Les cas décalés ont des bornes d'enveloppe lâches (combo 99 dB) ; c'est leur décalage qui les juge.
  - Le Rytm borne la hauteur à ±24 note comprise ; la couche borne TUNE seul, puis KEY TRACK ajoute la note.

## Références

Spécification v2, §7 (`../elektron-drums/spec-v2.md`) ; ses tickets de décision dans `../lockstep-post-v1/issues/`.
