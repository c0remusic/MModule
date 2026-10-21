# Premier son : GND-SW sur une Track

Type: prototype
Status: resolved
Assignee: c0remusic (session du 2026-10-21)
Blocked by: 01, 02, 03

## Question

GND-SW, la plus simple des 6 (ni voisine ni entrée), joue sur une Track de `mdDrums::Engine` depuis l'OS X.20 de
l'utilisateur, par la voie que recommandent « Le code DSP des 6 dans le programme des voix » et « Les routines 68k des
6 ». Un rendu WAV que l'utilisateur écoute à côté de ce qu'il connaît d'X.20 (ou de sa description), avec ses 8
paramètres balayés ; un test qui vérifie que rien d'autre ne change (les 112 Machines de 1.63 et les Machines
utilisateur, échantillon pour échantillon). Ce que la tranche apprend décide de la façon de faire les 5 autres.

## Answer (prototype)

Construit le 2026-10-21 (849eac3), à juger par l'utilisateur à l'oreille (`gnd-sw.wav`, que `mdX20BuiltinsTest` écrit
avec `MD_X20_WAV` : les défauts, puis chaque paramètre à 0, 64 et 127) :

- `VoiceEngine::installBuiltins` : le code d'X.20 à ses adresses, les entrées de GND-SW (type 5) prises des tables
  d'X.20, réécrites après chaque `reset()` ; `x20::compute`, le portage C++ des six routines ; `HostModel` en tire les
  9 mots de GND-SW à chaque tick ; `EngineT::installX20Builtins` chauffe son code (plus de 28 ms au premier Hit).
- `mdX20BuiltinsTest` (au garde-fou) : GND-SW sonne (crête 0,240) ; les 105 Machines audio de 1.63, seize à la fois,
  frappées deux fois : 0 bloc sur 420 différent, GND-SW installée ou non.
- Pas encore : GND-SW dans le plug-in (paramètre Machine, navigateur, chargement de l'OS) ; le portage C++ comparé aux
  routines d'X.20 dans un 68k (seul le portage Python l'a été).
## Décision

Le 2026-10-21, l'utilisateur, à l'écoute de `gnd-sw.wav` : « pas testé x20 mais ça semble ok ». Sans X.20 sous la
main pour comparer, la voie tient : les 5 autres se font de la même façon (le code d'X.20 à ses adresses, le portage
C++ de leur routine, la chauffe), chacune écoutée de même.
