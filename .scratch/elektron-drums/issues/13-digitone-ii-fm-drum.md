# Faisabilité du FM DRUM du Digitone II

Type: research
Status: resolved
Assignee: agent
Blocked by: 

## Question

Firmware et processeur du
Digitone II, et la machine FM DRUM : harnais possible, routines, effort et coût.

## Answer

Faisable. Le FM DRUM est une routine SHARC+ (`0x1c778b`, ADSP-21569 à 1 GHz, section 7 identique en 1.11 et 1.12),
appelée par voix et par bloc de 32, qui écrit un tampon mono de 32 flottants à 48 kHz avant le gain, l'ampli, le filtre
et la chaîne : la coupe voulue. Oracle sous licence compatible : sharcvm de dn2-cfw (GPL-2.0+, appelle une routine
seule) et l'émulateur couplé de digikit (AGPL, attribution dans l'À propos). 30 paramètres sur 4 pages (manuel 1.12,
A.2.2). Effort [INFER] 3-5 semaines d'agent ; coût [INFER] 2-5 % d'un cœur pour 16 voix, non mesuré. Inconnus : coût
réel, état de voix (0x2e8 octets), transitoires et bruit, exactitude en 40 bits.
Recherche : [research/13-digitone-ii-fm-drum.md](../research/13-digitone-ii-fm-drum.md).
