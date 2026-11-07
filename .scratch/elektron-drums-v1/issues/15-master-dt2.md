# Master : compresseur et overdrive du Digitakt II

Type: AFK
Status: ready-for-agent
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
