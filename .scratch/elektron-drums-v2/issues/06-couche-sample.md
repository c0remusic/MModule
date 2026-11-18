# Couche sample par piste

Type: AFK puis HITL (écoute)
Status: ready-for-agent
Assignee:
Blocked by: 04, 26

## Quoi

Une couche sample par piste, sur toute piste, machine Rytm comprise, lue par le lecteur DT2 en ONESHOT et ses
quatre modes de PLAY, sommée à la machine avant les drives pré, puis toute la chaîne. Réglages du layering de la
planification v1 : niveau, accord et fin, début, longueur, boucle, sens, enveloppe (attaque, maintien, déclin), décalage
de 0 à 100 ms, suivi de note ; retrig avec rampe anti-clic ; tout verrouillable et modulable. Éteinte par défaut et
sans coût. Sa propre BR, sur le sample seul, avant LEV, à la loi du Rytm : pas de quantification continu, environ
9,9 − 0,073 × BR bits équivalents, BR 0 contourné.

## Critère de fin

- Couche éteinte : aucun échantillon changé, aucun coût mesurable.
- Hits exacts : la couche sonne à `HitDelay` comme la machine, décalage compris ; latence et gigue rapportées avant et
  après.
- BR : bits équivalents mesurés sur une rampe, à ±0,5 bit de la loi.
- Coût visé : couche ≤ 1,5 % avec les 16 allumées (pire kit).
- Écoute de contrôle de la BR face à celle du DT2 (point ouvert de la spécification).
- Même son à 44,1 et à 48 kHz (tests Q de `research/19-qualite-audio.md`).
- Aucun set existant ne change de son.
- Coût mesuré sur machine calme et rapporté face à son guide (spécification v2, §3) ; le pire kit tient sous 15 %.
- Gate vert, nouveau test inscrit dans sa liste.

## Décisions (2026-11-18, avec l'utilisateur)

- Le lecteur DT2 de la couche tourne à la fréquence de la chaîne de sa piste (44,1 kHz sur le chemin Machinedrum,
  48 kHz sur le chemin Syntakt), pas à 48 kHz suivi d'un rééchantillonneur.
- Quatorze paramètres hôte par piste, ajoutés à la fin avec des ID figés : ON, TUNE, FINE, PLAY, START, LENGTH, LOOP,
  LEVEL, BR, ATTACK, HOLD, DECAY, OFFSET, NOTE TRACK.
- Le LOAD du bloc de la couche charge le sample propre de la couche : une piste Sampler en tient deux.
- Découpage : tranche 1 le moteur, tranche 2 paramètres hôte, état et éditeur, tranche 3 locks, LFO et écoute.

## Avancement (2026-11-18) : tranche 1, le moteur

`SampleLayers` (`edDrumsLayer.*`), une instance par chemin, chacune à sa fréquence. Deux voix du lecteur DT2 par piste
(`Dt2Player` gagne un rendu par voie, `start`, `renderLane`, et sa fréquence de sortie, `setOutputRate`, mêmes
échantillons qu'avant à 48 kHz) : un Hit sur la couche qui sonne fond l'ancienne voix sur un bloc pendant que la
nouvelle part. Après le lecteur : BR (loi du Rytm, troncature), enveloppe attaque-maintien-déclin (attaque
2 × (v/127)² s ; maintien et déclin aux lois de VINTAGE), niveau (v/127)². Sommée à la machine avant la chaîne, dans
`renderMachinedrum` et `renderSyntakt`. Son premier échantillon attend le départ propre de la machine (`voiceStart` sur
le chemin Machinedrum, le bloc après celui du trig sur le chemin Syntakt), puis son décalage, par une ligne à retard par
voix. Éteinte : `hit` ne fait rien, `render` sort aussitôt.

Relevé en route : sur le chemin Machinedrum, un Hit sur une piste qui sonne démarre le run de son ring de 24 échantillons
après la fin de l'ancien à 7 avant (44,1 kHz, chaîne nue). L'ancien contenu y saute : un trou de zéros au-delà de la
fin, un recouvrement en deçà. Pour la couche qui continue d'un run à l'autre, le recouvrement est compensé (autant de
silence devant sa suite) et le trou tient le dernier échantillon écrit (`Ring::hold`), seulement quand la couche sonne :
les sets sans couche ne changent pas. Sur le chemin Syntakt, l'ancienne voix fond dans le bloc du trig, le dernier de
l'ancien run, comme la voix de la machine. Le trou existe aussi pour les voix Machinedrum elles-mêmes, inchangé ici.

## Avancement (2026-11-18) : tranche 2, paramètres, état, éditeur

**Paramètres d'hôte.** 14 par piste, ajoutés après `chorus_xover` avec des IDs gelés, piste par piste (`t01_layer_on` à
`t16_layer_note`, 224 lignes de `edDrumsParameterIds.txt`). Leurs défauts sont ceux de `LayerSettings{}`, donc un set
existant sonne comme avant. Leur texte suit les lois du moteur : TUNE en demi-tons, FINE en cents, PLAY sous les noms du
lecteur DT2, BR en bits gardés, les temps de l'enveloppe, OFFSET en ms. `Processor::giveTrack` donne la couche entière au
moteur dès qu'une de ses valeurs bouge, comme la chaîne.

**Second sample par piste.** Le Processor tient maintenant deux slots de sample par piste : celui de la machine en
`t`, celui de la couche en `layerSlot(t) = 16 + t`. Ses tableaux, sa boîte aux lettres vers le thread audio et ses sources
tenues passent à 32 slots ; un slot de couche va au moteur par `setLayerSample`. Le LOAD du bloc charge le sample propre
de la couche, sans changer la machine de la piste.

Le sample de la couche suit dans l'état, les Kits (enregistrés, chargés, préparés pour un pattern, comparés par
`kitDiffers`), les sons de piste et IN THIS SET du Locker. Il s'écrit dans un élément `layerSample` à part : les états et
Kits d'avant n'en ont pas et se lisent sans changement.

**Éditeur.** Le bloc SAMPLE LAYER a deux lignes de sept cellules actives. Ligne 1 : TUNE, FINE, PLAY, START, LENGTH,
LOOP, KEY TRACK. Ligne 2 : LAYER, LEVEL, BR, ATTACK, HOLD, DECAY, OFFSET. Les treize autres que LAYER sont grisées tant
que la couche est éteinte, comme l'enveloppe de l'AMP. LOAD et CLEAR portent sur le sample de la couche.

**Tests.**
- `edDrumsProcessorTest` gagne `layer()` :
  - la couche éteinte se tait, allumée elle sonne (crête 0,25) ;
  - LEVEL 0 la tait ;
  - l'état la rend aux mêmes échantillons ;
  - un son de piste porte son sample sur une autre piste ;
  - un Kit le garde, et `kitDiffers` le voit partir.
- Les IDs gelés, la couverture « chaque paramètre atteint le moteur » et le compte VST3 comptent les 14.
- Le test de l'éditeur exclut la couche du menu des Locks, qui vient à la tranche 3.

Le test du Processor tient plusieurs `Processor` de 88 Ko sur la pile à la fois, ce que ne fait jamais un hôte. Il
débordait le 1 Mo par défaut avec ces paramètres en plus, et reçoit 8 Mo de pile.

Un fichier déposé sur le bloc SAMPLE LAYER va dans le slot de la couche.

Mis de côté après revue :
- Le Locker et Browse chargent toujours sur la machine (LOAD, double-clic, glisser) ; leur surbrillance « joué » ne lit
  que le sample de la machine.
- L'écoute d'un son de piste dans Browse dure ce que dure le sample de la machine, même si la couche dure plus.
- Une piste sans machine montre une couche active qu'elle ne joue pas (déjà mis de côté à la tranche 1).
- Le rectangle de dépôt répète les constantes du bloc, définies dans `edDrumsEditorScreen.cpp`.

Reste pour la tranche 3 : les locks, les cibles des LFO, l'écoute de BR, et des réglages pris au Hit plutôt qu'au départ
de la voix.

## Références

Spécification v2, §7 (`../elektron-drums/spec-v2.md`) ; ses tickets de décision dans `../lockstep-post-v1/issues/`.
