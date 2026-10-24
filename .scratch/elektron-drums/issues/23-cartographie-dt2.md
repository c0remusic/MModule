# Cartographie du firmware du Digitakt II

Type: research
Status: resolved
Assignee: agent
Blocked by: 

## Question

Avant toute décision qui dépend du Digitakt II (effets master, effets d'envoi, filtres, layering), faire
l'inventaire complet des routines de son firmware (OS 1.16, blob DSP identique en 1.17), comme celui fait d'abord pour
le Syntakt : pour le DSP SHARC+, chaque routine audio (voix du sampler et STRETCH, les 6 filtres avec leur rôle
vérifié, overdrive, bit reduction, SRR, chorus, delay, reverb, compresseur, overdrive et égaliseur du master s'ils
existent, LFO et enveloppes s'ils y sont), son adresse, sa taille en instructions, ses entrées, son état, ses appels,
et qui calcule ses coefficients (ColdFire ou DSP) ; pour le ColdFire, les routines de conversion des paramètres et
leurs tables. Puis : l'oracle est-il prêt (sharcvm appelle-t-il une routine isolée, digikit se construit-il sous
Windows), et combien coûte chaque routine (instructions par bloc mesurées) ?

## Answer

Inventaire fait sur l'image 1.16 (sharcinv de digikit : 1 147 fonctions ; boucle de bloc `0x1c75d8` : 123 fonctions,
12 278 instructions statiques). Chaîne de piste lue dans `0x1c642a` : voix `0x1c4ecf` ou STRETCH `0x1c5576`, puis BR
`0x1cdd8b` (trouvé ; ce n'est pas « niveau »), puis overdrive `0x1cb3d8`, SRR `0x1cdecb` et base-width `0x1ccbd8`, en
pré ou en post, puis filtre, enveloppe `0x1cbb57`, mix et pan. Rôles des six filtres vérifiés (index = type + 1 : le
signe du peigne, −1 ou +1, le prouve). Les coefficients sont calculés sur le DSP par cinq setters. Master : compresseur
`0xb82d41` et le même saturateur, pas d'EQ. LFO, lissage et trame sont sur la ColdFire. Les outils statiques sont prêts
sous Windows ; sharcvm et coupled-run ne le sont pas, donc les coûts restent statiques (environ 6,5 à 8,4 k
instructions par voix active), pas mesurés. Détails : [research/23-cartographie-dt2.md](../research/23-cartographie-dt2.md).
