# Le moteur sampler du Digitakt II : ce qu'il fait

Type: research
Status: resolved
Assignee: agent
Blocked by: 

## Question

Ce que fait le moteur sampler du DT2, du manuel et de la cartographie existante (pas de nouveau désassemblage) : ses
modes de lecture (dont STRETCH et ses réglages), ses paramètres de SRC et d'AMP, les formats et fréquences de sample
qu'il lit, sa longueur maximale, ses boucles, la lecture à l'envers, la bit reduction dans la voix, et ce que la voix
reçoit du ColdFire (lissage, LFO). Puis, en face, ce que Lockstep a déjà : la machine Sampler maison (REPEAT GAP,
grains, VINTAGE), le Locker et ses formats. Résultat : un tableau de ce que le DT2 apporte, de ce qui recouvre
l'existant, et de ce qui s'y oppose.

## Answer

Manuel OS 1.17 (2026-09-24) : six machines sample (ONESHOT, WERP, STRETCH, REPITCH, SLICE depuis 1.15, GRID), quatre
modes de PLAY (avant, arrière, et les deux en boucle avec point LOOP), TUNE ±5 octaves, sample verrouillable par pas ;
samples 16 bits 48 kHz mono ou stéréo, 400 Mo par projet, pas de borne par sample. STRETCH n'expose que STRT, LEN et
BARS. BR (16 à 1 bit), overdrive, SRR, base-width et AMP sont dans la chaîne après la voix ; LFO et lissage sur la
ColdFire, la voix ne reçoit que des mots lissés. Le DT2 apporte stéréo, boucles, calage au tempo, tranches, lecteur
long ; il recouvre la chaîne de Lockstep (drives, BAND, AMP, LFO), qu'il n'est pas utile de porter une deuxième fois. Il
s'oppose à la mono et aux 44,1 kHz de Lockstep, à l'unique sample par piste (pas de sample lock), au GRAIN binaire de
Machine 9 et à STRT/END contre STRT/LEN. Recherche :
[research/04-sampler-dt2-fonctions.md](../research/04-sampler-dt2-fonctions.md).
