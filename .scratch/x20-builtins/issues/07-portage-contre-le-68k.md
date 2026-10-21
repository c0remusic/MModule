# Le portage C++ contre les routines d'X.20 dans un 68k

Type: task
Status: resolved
Assignee: c0remusic (session du 2026-10-21)
Blocked by:

## Question

`x20::compute` suit le portage Python de research/02, lui-même vérifié contre les routines d'X.20 lancées dans Musashi
(0 différence sur 120 576 appels) ; le C++ ne l'a pas été. Un test à part (Musashi sert une classe de CPU par
exécutable : pas lié à `mdEngine`), qui charge l'OS 68k de `MD_X20_OS` (`md::fw::parseX20Os`), lance les tables
d'X.20 et ses six routines comme `run68k.cpp` de research/02, et compare à `x20::compute` : les quatre modes
d'accordage, chaque paramètre balayé, des vecteurs aléatoires. Les mots que la routine laisse (NFX-CO mot 6) et le
dixième mot de NFX-CO et NFX-UC, que le portage Python ne comparait pas, compris. Sauté sans `MD_X20_OS`.

## Answer

Fait le 2026-10-21 : `mdX20PortTest` (au garde-fou), un exécutable sans moteur (le portage, le lecteur de l'OS et
Musashi). Il charge l'OS 68k de `MD_X20_OS`, lance l'initialiseur statique d'X.20 et son constructeur de tables
d'accordage à TUNING 0, pointe le contexte sur la table de decay d'OS 1.63 (tirée du flash de
`GEARMULATOR_MD_FIRMWARE_BIN`), puis appelle chaque routine et `x20::compute` sur les mêmes mots : les quatre modes,
chaque paramètre sur ses 128 réglages, 2 000 vecteurs aléatoires par mode. Onze mots comparés, ceux que la routine
laisse compris. 72 576 appels, 0 différence, le dixième mot de NFX-CO et NFX-UC compris.
