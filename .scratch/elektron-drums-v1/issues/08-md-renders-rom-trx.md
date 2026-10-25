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
