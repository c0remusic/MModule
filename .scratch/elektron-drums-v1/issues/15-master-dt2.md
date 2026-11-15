# Master : compresseur et overdrive du Digitakt II

Type: AFK
Status: resolved
Assignee: 
Blocked by: 03, 12

## Quoi

Porter le compresseur `0xb82d41` puis le saturateur `0x1cb3d8`, puis écrêtage et rampe de gain, dans l'ordre et
avec les réglages du firmware (sidechain s'il existe), sur Main seulement, avec vumètre de réduction de gain. Le
saturateur sert aussi de type de drive de piste « DT2 Overdrive ».

## Critère de fin

- Contre les vecteurs du ticket 03 à 48 kHz : Q1 si en entiers, sinon Q2-Q3.
- « Sonne pareil » 44,1/48 ; coût du master mesuré (pas de ligne dans le budget proposé).

## Noté par le ticket 09 (2026-11-05)

Dynamix lit la table de l'OS à $24DA14 (`OsTables::onePole`), sans formule, importée par l'exception de la spec 11.4.
Ce ticket hérite de cette dette tant qu'une formule manque.

## Références

Spécification §6. Ticket « Effets master ».

## Noté par le ticket 12 (2026-11-06)

Ajouter un drive touche l'énumération, `DriveStages`, `withStage` et `setDrive`, un `static_assert` tenant `Types`. `Chain::raised` dit qui
se suréchantillonne, `Chain::MaxDelay` borne la latence : un nouveau drive doit y tenir, 31 + 38 + 12 = 81 sur 82 aujourd'hui.

## État au 2026-11-07

Le master tourne en flottant sur Main, sans firmware à l'exécution : `edDrumsMaster.{h,cpp}`, coupé par défaut
(`Engine::setMasterOn`). Ses lois sont ajustées sur les tables et le code du firmware, ce que l'utilisateur a accepté
le 2026-11-07 comme pour le delay du Syntakt : environ 40 nombres, aucune table telle quelle. Seuil 60p − 60 dB, huit
ratios de 1,5:1 à 20:1, détecteur de crête stéréo filtré par un pôle (passe-bas sous 64, passe-haut au-dessus, 20 Hz
à 20 kHz), relâche puis attaque en dB par échantillon (attaque 0,1 à 100 ms, relâche 10 à 2 000 ms), makeup jusqu'à
+24 dB, mix à six points en dB, +10 dB et écrêtage, saturateur 1 − (1 − |u|)^8, volume au carré.

Contre le firmware, le modèle numpy de ces lois tombe à −100 dB ou mieux sur treize vecteurs, dont trois nouveaux qui
règlent tout et bougent chaque réglage en cours de signal ; le C++ tombe à −150 dB du modèle. L'entrée de Main est
divisée par 3,16 avant le compresseur, là où une piste du DT2 se tient sous son +10 dB : un Main sous le seuil sort
tel quel, mais écrêté à 0 dBFS dès que le master est allumé. Coût : 0,21 à 0,28 % d'un cœur à 48 kHz.

Pas construits : le second bus (pistes hors compresseur), les sources de sidechain autres que l'entrée, les retours
d'effet en dehors de Main. La réduction de gain se lit par `Master::reduction()`, d'un autre fil, en attendant le
vumètre de l'interface. Le saturateur seul, pour le drive de piste « DT2 Overdrive », se sortira avec ce drive.

Reste : « sonne pareil » à 44,1 et 48 kHz à l'écoute.

## État (2026-11-11)

Le vumètre de réduction de gain est sur la page Master : `Master::reduction()`, l'atomique que le fil audio écrit au
début de chaque bloc de 32, est recopié par le processeur à chaque bloc de l'hôte (`Processor::masterReduction`, 0
quand le master est coupé) et lu par l'éditeur à chaque trame : barre de la couleur du bloc qui descend de 0 à −12 dB,
chiffre dessous, trace des 240 dernières trames à droite. `master` dans `edDrumsEngineTest` vérifie qu'elle suit le
Hit : −25,9 dB dans le Hit au réglage serré du test, −2,0 dB une seconde plus tard.

Coût du master, mesuré par `effectsCost` (une piste, blocs de 128, 44,1 kHz, machine chargée par d'autres sessions) :
0,22 à 0,33 % d'un cœur en plus, du même ordre que les 0,21 à 0,28 % mesurés à 48 kHz le 2026-11-07.

Reste ouvert, et non revendiqué ici : « sonne pareil » à 44,1 et 48 kHz à l'écoute de l'utilisateur. Toujours pas
construits : le second bus, les sources de sidechain autres que l'entrée, le saturateur seul en drive de piste.

## Écoute de l'utilisateur (2026-11-15)

Sur la page d'écoute `C:\dev\elektron\elektron-work\listen-all\index.html`, hors dépôt (rendus de `df980a4f` sans image flash) : master allumé et réglé fort (seuil vers -22 dB, ratio 4:1, makeup vers +4 dB, drive vers -9 dB),
le même kit à 44,1 et 48 kHz, bascule en lecture au même instant. Deep Garage et Boom Room : OK, pareils.

Critère « sonne pareil » rempli. Reste ouvert : le drive de piste « DT2 Overdrive » (le saturateur seul, que « Quoi » demande),
jamais construit ; le second bus et les sources de sidechain autres que l'entrée, toujours pas construits.

## Drive de piste Overdrive (2026-11-15)

Les deux emplacements de drive ont un sixième type, « Overdrive » (`DriveType::Overdrive`, après DAC : les choix
enregistrés gardent leur sens) : le saturateur du master seul, sans compresseur ni +10 dB. AMOUNT suit la loi de DRIVE
du master, gain 0,8465 d² + 0,1534 dans la courbe et part mouillée min(8 d, 1), d = AMOUNT / 127 avec le mouvement d'un
LFO ; à 0, la piste sort sèche, à l'échantillon près. Courbe et lois sont une seule fonction partagée avec le master
(`saturate`, `driveGain`, `driveMix` dans `edDrumsMaster.*`). Le drive se suréchantillonne comme les autres (2x par
défaut). Texte d'AMOUNT : OFF à 0, puis le gain en dB (−3,9 dB à 96).

Hypothèse non revérifiée ici : que l'OVERDRIVE de piste du Digitakt II soit ce même saturateur, comme « Quoi » le dit.

Vérifié : `edDrumsChainTest` (`overdrive`) compare la sortie de la chaîne à la courbe du master sur un sinus de 0,8 à
AMOUNT 0, 16, 64 et 127, à −150,5 dB au pire, et exige la piste sèche à 0 ; `edDrumsEditorTest` lit le choix et le
texte d'AMOUNT et rend `sound-overdrive.png` (OVERDRIVE tient sur la ligne de titre du drive).

Critère de fin rempli (vecteurs, « sonne pareil », coût) et « Quoi » construit, sidechain compris tel que l'entrée le
donne. Hors de ce ticket, non construits : le second bus (pistes hors compresseur) et les sources de sidechain autres
que l'entrée. Ticket résolu.
