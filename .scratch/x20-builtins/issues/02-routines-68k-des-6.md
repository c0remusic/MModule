# Les routines 68k des 6 : les appeler ou les porter

Type: research
Status: resolved
Assignee: c0remusic (session du 2026-10-21)
Blocked by:

## Question

`HostModel` appelle les routines d'OS 1.63 (machine, LFO, lissage) dans un 68k émulé sur l'image 1.63. Les routines de
coefficients des 6 sont dans l'OS 68k d'X.20, une autre compilation (research 01 §9 : 22 à 702 octets chacune, aux
adresses de la table du §9), qui lisent des tables RAM (`$2cdb00`, `$2cdc6c`) et un contexte par Track plus grand
(`+$8c`, `+$90`) qu'X.20 construit au boot. Établir :

- ce que chacune lit et écrit, et d'où viennent ces tables (construites par quelles routines du boot d'X.20, ou
  calculables depuis les données de l'OS) ;
- les deux voies et leur coût : (a) charger ces routines et leurs tables dans la mémoire du 68k de `HostModel` (ou un
  second 68k) et les appeler comme celles de 1.63 ; (b) les porter en C++ bit pour bit, comme `mdDrumsLfo` ;
- ce que donnent les mots que la DSP reçoit, pour qu'un test compare la voie choisie à l'appel de la routine d'X.20 ;
- les noms et défauts des 8 paramètres de chacune (§9 en donne les noms).

Un document `research/02-routines-68k-des-6.md`, marques [V] [M] [I], avec une recommandation. Rien de commité depuis
l'OS X.20 ; les sondes lisent `MD_X20_OS`.

## Answer

[research/02](../research/02-routines-68k-des-6.md) : les six routines sont petites et ne demandent rien au boot
d'X.20. Elles lisent les 8 mots de la Track, le mode d'accordage (contexte `+$8c`) et la table de decay de 1.63
(`+$90`, que `HostModel` lit déjà). `$2cdb00` est la base de trois tables d'accordage, calculables depuis le seul
réglage TUNING ; `$2cdc6c` est le tempo fois 24 (NFX-EV seule). Un portage Python est bit pour bit avec les routines
d'X.20 lancées dans Musashi : 0 différence sur 120 576 appels. Voie (a), un second 68k sur l'OS X.20 : environ 1 Mo par
moteur, une séquence de chargement propre, une quinzaine d'adresses d'X.20 B, 1,5 à 2 jours ; voie (b), un portage C++
testé contre la routine d'X.20 sur `MD_X20_OS` : environ 1,5 jour. Recommandation : (b). Noms et défauts des 8
paramètres de chacune dans le document.