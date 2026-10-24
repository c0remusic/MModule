# Faisabilité du Digitakt II

Type: research
Status: resolved
Assignee: agent
Blocked by: 

## Question

Le Digitakt II tourne sur un SHARC : état de
digikit (ElekLabs, AGPL), possibilité d'un harnais d'oracle, emplacement des routines de ses filtres (Multi-mode,
Lowpass 4, Legacy LP/HP, Comb-, Comb+, Equalizer), de ses effets (overdrive, BR, SRR, chorus, delay, reverb,
compresseur) et de ses moteurs sampler ; effort et coût CPU.

## Answer

Faisable par portage C++ routine par routine, pas par émulation. Émuler tout le DSP (211k instructions par bloc)
coûterait environ un cœur ; un port est estimé à 5-10 % d'un cœur pour 16 pistes et les effets [INFER]. L'émulateur de
digikit (AGPL, ColdFire et SHARC+ réels, hors temps réel) sert d'oracle, et sharcvm de dn2-cfw sait appeler une routine
isolée. Filtres, effets et rendu des voix sont localisés à l'adresse près, dans un code partagé avec le Digitone II.
elektron-emu n'émule pas le SHARC du Digitakt II (son remplaçant est un sampler sans filtre ni effet). Effort : 6 à 10
semaines agent. Limite : exact à l'émulateur, pas au silicium (graines RECIPS/RSQRTS non documentées).
Recherche : [research/12-digitakt-ii.md](../research/12-digitakt-ii.md).
