# Machinedrum : harnais contre mdEngine et inventaire des tables

Type: AFK
Status: resolved
Assignee: 
Blocked by: 01

## Quoi

Un harnais qui rend une voix MD portée en C++ et la même voix dans `mdEngine` (oracle), Hit par Hit, et compare
échantillon par échantillon à 44,1 kHz. Une machine factice (GND) le traverse pour le prouver. En même temps,
inventorier les tables que lisent DSP1, DSP2 et l'OS : pour chacune, sa taille, son usage, et si une formule la
régénère au mot près (précédent : le sinus du DSP mixeur). Musashi ne se lie qu'à un seul CPU par exécutable : le
harnais tourne dans son propre processus.

## Critère de fin

- Le harnais passe sur GND et échoue si un échantillon est faussé exprès.
- `research/` de cet effort contient l'inventaire, chaque table marquée « formule trouvée », « à chercher » ou
  « bloquante ».

## Références

Spécification §3.2, §14. Tickets « Port du Machinedrum en C++ », « Instrument sans firmware ».

## Answer

Fait le 2026-10-26.

**Harnais.** `source/elektron/md/mdVoice/` : `md::voice::Voice`, une voix du DSP2 portée en C++ sans émulateur (bloc
d'état de 64 mots, dispatch de `08-dsp2-voice-abi.md` : init au changement de type, update à chaque trig, render à
chaque bloc, par une table init/update/render par type), avec GND-- et GND-IM. `mdVoicePortTest`, dans son propre
processus, donne les mêmes slots au port et au `VoiceEngine` de mdEngine, puis compare chaque échantillon des 16 voix
et chaque mot de leur bloc d'état. Deux phases : 4 000 blocs dont les paquets viennent des fonctions de coefficients de
l'OS (Musashi), réécrits à chaque bloc comme le tick de `HostModel`, avec Hits, boutons et changements de machine ;
puis 1 000 blocs de paquets faits main aux bords (comptes à cheval sur deux blocs, négatifs, $800000, échantillons à
pleine échelle). Le test exige d'atteindre chacun de ces bords : 735 224 échantillons GND-IM non nuls, 69 373 négatifs,
692 744 dans un bloc sans Hit.

- Port juste : 0 différence en échantillons et en mots d'état, en 1 s.
- Échantillon faussé exprès (bloc 2500, voix 7, trame 13) : trouvé à cet endroit.
- Mutation de `limit()` (la limitation à l'écriture d'un accumulateur remplacée par un masque) : aucun échantillon
  ne change, mais la comparaison d'état échoue (`block 4008, voice 1, state word 5: the oracle holds $800000, the port
  $7fffe0`). C'est pourquoi le harnais compare aussi l'état.

Revue d'architecture : l'interface de `Voice` tient pour les 39 machines (une entrée de table par type). Deux
dépendances viendront avec les premières machines qui les lisent, et pas avant : les tables partagées (sinus,
`X:$140000+`), à passer au constructeur ; la mémoire externe de 1 536 mots par piste des P-I, propre à chaque voix.

**Inventaire.** [research/02-tables-md.md](../research/02-tables-md.md) : 79 tables, 14 « formule trouvée » (chacune
générée et comparée mot à mot, dont le sinus de 32 768 mots du DSP2), 52 « à chercher », 13 « bloquante » (formes
d'onde de TRX-BD/SD/XT/RS/XC, excitations de cinq P-I, échantillons des E12, banque UW). OS 1.63 ne charge aucune
table tanh ; la conversion 16 → 12 bits de la flash, qui sert aussi aux Samples de l'utilisateur, reste à retrouver.
Question ouverte dans le document : les petites tables de 24 à 48 constantes du code P-I comptent-elles comme tables
ou comme immédiats du code ?
