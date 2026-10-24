# Séquenceurs des machines Elektron

Type: research
Status: resolved
Assignee: agent
Blocked by: 

## Question

Comment se comportent les
séquenceurs des machines Elektron que l'instrument reprend, au niveau où un séquenceur interne doit les reproduire :
le séquenceur de l'OS du Machinedrum (qui tourne dans mdLib), celui du CPU #1 du Syntakt (section 3), et ce que les
manuels du Digitakt II et du Syntakt documentent (trig conditions, micro-timing, retrigs, locks, slides, fills, pattern
chains). Ce qui est lisible dans le code, ce qui ne l'est que dans les manuels.

## Answer

Machinedrum OS 1.63 : séquenceur 24 ppqn sans conditions, micro-timing ni retrigs ; trigs, 64 lignes de locks mises en
scène un pas en avance, accent (128 + 2 × quantité), slide (rampe par horloge), swing (pas × mot / 16384), échelles
1x/2x/3/4x/3/2x, chaînage engagé 7 horloges avant la fin : tout lu dans le code ou mesuré. Syntakt OS 1.41, CPU #1 :
tick 24 ppqn 0x4008C924, évaluation d'un pas en avance 0x4008B882 (conditions 0x401181D4 : 0-20 %, 100 %, FILL, PRE,
NEI, 1ST, LST, A:B et !A:B ; PROB de piste ; micro-timing en 24es de pas réduit par le quantize ; swing ; locks ; sound
lock ; retrigs sur une grille de 960 par ronde), événements horodatés livrés au bloc de 32 échantillons : lisible et
portable. Digitakt II (128 pas, FILL séparé, 80 locks) : manuel seulement. Song mode, engagement d'un pattern en file
et euclidien du Syntakt : code présent, non lu.

Recherche : [research/07-sequenceurs-elektron.md](../research/07-sequenceurs-elektron.md)
