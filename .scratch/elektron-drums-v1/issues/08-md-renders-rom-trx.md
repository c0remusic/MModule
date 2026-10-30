# Machinedrum : renders ROM et TRX en C++

Type: AFK
Status: ready-for-agent
Assignee: 
Blocked by: 02

## Quoi

Porter en C++ entier le render ROM (partagé par les 52 machines ROM) et les renders TRX, avec leurs fonctions de
coefficients 68k et le lissage de l'OS, par piste. Voix brute avant le mixeur. Les machines ROM lisent un sample
fourni (banque UW importée, sample de l'utilisateur ou maison), jamais la flash image au rendu.

## Critère de fin

- Bit-exact contre `mdEngine` à 44,1 kHz (Q1) sur chaque machine portée, Hits et réglages variés.
- « Sonne pareil » 44,1/48.
- Coût par voix mesuré, comparé aux ~0,07-0,3 % estimés.

## Références

Spécification §3.2. Ticket « Port du Machinedrum en C++ ».

## 2026-10-30 : ROM portées, bit-exactes

Les 48 machines ROM (types 129-160 et 177-192) partagent un seul init ($13D), update ($15A) et render ($16C) dans le
programme de DSP2 ; ils sont portés instruction par instruction dans `source/elektron/md/mdVoice/mdVoiceRom.cpp`.
L'appelant possède ce que les 16 voix partagent (`md::voice::Shared`) : les tables du DSP, la banque telle que le DSP
la tient (répertoire et codes 12 bits, comme `fw::loadRomBankFromFlash` les construit) et la mémoire de travail
interne du DSP. Le port ne lit jamais la flash ni l'OS au rendu.

Ce qui est prouvé par `mdVoicePortTest`, contre `VoiceEngine` : 5000 blocs de 16 voix, 2500 Hits, 911 114 échantillons
ROM non nuls, 0 échantillon différent et 0 mot d'état différent (Y:S et X:S, 64 mots chacun). Première phase avec les
paquets des fonctions 68k de l'OS (ROM-01, -02, -17, -32, et deux numéros vides), seconde phase avec des paquets aux
extrêmes et des échantillons ajoutés à la banque (pleine échelle, 0 à 3 échantillons, une boucle, un échantillon
raccourci sous la voix). Chaque bord nommé et chaque branche sondée est atteint (inversion, fin en plein bloc,
retrigger, boucle, départ après la fin, hauteur extrême dans les deux sens, remplissage impair et pair, dernier
échantillon tenu, arrêt à l'envers au début, retrigger inversé à première partie impaire). Deux mutations sont
attrapées : une sur un mot d'état que nul rendu ne lit, une sur l'arrondi convergent.

Deux faits du DSP que le port reproduit :
- La mémoire de travail est commune aux voix : un retrigger inversé dont la première partie a une longueur impaire
  laisse un pointeur non écrit, qui vaut le mot laissé par le rendu précédent. Il faut rendre toutes les voix ROM à
  chaque bloc, dans l'ordre des pistes ; une voix rendue seule (préécoute) n'est inexacte que sur ce bord.
- Ce pointeur est un échantillon lu comme une adresse ; l'émulateur la masque à 2M mots et tombe sur des données E12.
  C'est un artefact : le port lit 0 hors de ce qu'il tient, le test passe la mémoire de l'oracle par un crochet de
  test (`mdVoiceInternal.h`). 33 398 mots passent par ce crochet dans la première phase (surtout les deux derniers mots
  de l'anneau de décodage, sans effet) ; sans lui, la première divergence tombe au bloc 495 (ROM-01 inversée, RTRG 51).

Garde : une boucle plus courte que l'avance d'un bloc (128 échantillons pour un échantillon à 44,1 kHz au pas le plus
haut) met la position après la longueur ; le remplissage du DSP compte alors 2^24 passes et le DSP cale. Le port rend
ce bloc silencieux et garde l'état tel que le DSP l'avait écrit, jusqu'au Hit suivant. Le chargeur de l'OS refuse les
boucles de moins de 151 échantillons, donc la banque UW n'y arrive qu'au-delà de 52 kHz ; un Sample de l'utilisateur
peut y arriver.

Coût mesuré (release, médiane de 3 × 9 passes, autres processus à 18-29 % de charge) : 4,4 µs par voix et par bloc de
32, soit 0,61 % d'un cœur par voix et 9,8 % pour 16 voix, au-dessus des 0,07-0,3 % estimés.

Reste ouvert :
- Les tables (décodage 12 bits, interpolateur, décimateur) sont encore copiées de la mémoire du DSP : il faut leurs
  formules, comme le ticket 07 l'a fait pour le Syntakt.
- RAM-P n'est pas porté (même update et render, inits à lui, tampons de RAM-R).
- La fonction de coefficients 68k des ROM tourne encore dans Musashi.
- « Sonne pareil » 44,1/48 n'est pas vérifié.
- Les TRX restent à porter.
