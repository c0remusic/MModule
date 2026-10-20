# Construire le choix des sources d'une NFX

Type: task
Status: open
Assignee:
Blocked by: 29, 30

## Question

Construire ce que « Ce que la DSP des voix doit faire pour qu'une NFX entende n'importe quelle Track » établit et ce que
« Choisir les Tracks qu'entend une NFX » fait choisir : les tampons et les descripteurs, le choix gardé où le prototype
l'a mis, la ligne et le menu. Tests : une NFX en Track 12 qui entend la Track 3, bit pour bit avec la copieuse de
`mdDrumsTestMachine.h` ; une source rendue après l'effet, un bloc plus tard ; un set sans choix qui sonne comme
avant ; la latence note→son et le CPU, avant et après.
