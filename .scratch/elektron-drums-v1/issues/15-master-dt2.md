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

## Références

Spécification §6. Ticket « Effets master ».
