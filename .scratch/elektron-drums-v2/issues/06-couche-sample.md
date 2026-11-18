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

## Références

Spécification v2, §7 (`../elektron-drums/spec-v2.md`) ; ses tickets de décision dans `../lockstep-post-v1/issues/`.
