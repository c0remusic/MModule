# Effets du Virus

Type: research
Status: resolved
Assignee: agent
Blocked by: 

## Question

Peut-on reprendre les algorithmes d'effets du Virus (Osirus dans
Gearmulator : chorus, phaser, flanger, delay, reverb, distorsions ?) en extrayant leurs routines DSP56300 et en les
réécrivant en C++ (sans faire tourner de DSP émulé dans l'instrument ; l'émulateur de Gearmulator sert seulement
d'oracle dans les tests), comme le port du Syntakt, avec la ROM de l'utilisateur ? Lesquels existent, où, à quel coût ?
Et à côté : les effets de l'Octatrack (phaser, flanger, chorus) et leur processeur.

## Answer

Faisable en principe (méthode de `TrackFx` : traduction exacte du DSP56300 contre le Virus émulé de Gearmulator,
entrée audio du Virus comme banc), mais coûteux : le programme DSP du Virus est monolithique, sans carte publique des
effets, et convertit lui-même les paramètres. Chez l'utilisateur, seule la famille TI est disponible (`firmware.bin`
d'OsTIrus) ; aucune ROM A/B/C. Effets TI : chorus (Classic, Vintage, Hyper, Air, Vibrato, Rotary), phaser 1-6
étages, pas de flanger à part, delay (dont Tape), 4 reverbs, 25 distorsions, Character, EQ. L'Octatrack (ColdFire +
DSP56xxx, OS public déjà téléchargé, 44,1 kHz) a phaser 2-10 étages, flanger et chorus 2-10 voix comme effets de
piste : meilleur candidat, non tenté. Détails : [research/15-effets-virus.md](../research/15-effets-virus.md).
