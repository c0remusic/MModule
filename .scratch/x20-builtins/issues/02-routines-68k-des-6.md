# Les routines 68k des 6 : les appeler ou les porter

Type: research
Status: open
Assignee:
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
