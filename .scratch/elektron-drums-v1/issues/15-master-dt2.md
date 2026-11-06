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
