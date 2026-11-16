# Répartition du budget CPU entre les jalons

Type: grilling
Status: open
Assignee:
Blocked by: 12

## Question

Le budget est de 15 % d'un cœur au plus à 16 pistes pour l'instrument complet. Les coûts sont maintenant mesurés en
instructions : voix DT2 ONESHOT ~3 600 et STRETCH ~1 800 instructions SHARC par voix et par bloc (« Oracle dynamique
de la voix sampler du Digitakt II »), machines du Cycles 5 300 à 8 300 instructions ColdFire, DRIVE analogique
~0,1 % par piste. Les ramener en temps sur ce PC (port flottant SIMD), et répartir le budget entre lecteur DT2, couche
sample, machines du Cycles, DRIVE analogique et effets du DT2 : ce qui tient, ce qui doit céder, et la mesure qui le
dira au banc.
