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

## 2026-10-31 : formes d'onde des TRX, TRX-BD porté

Décision d'Antoine : les formes d'onde échantillonnées de TRX-BD, TRX-SD, TRX-XT, TRX-RS et TRX-XC viennent de la
flash image que son propriétaire importe, comme la banque UW (spécification §11.4 amendée). Ce sont des mots du
programme voix ; `md::fw::dspWords` les lit dans ses enregistrements de chargement, et l'appelant les copie dans
`md::voice::TrxTables`. Le sinus de 32 768 mots vient de la récurrence du boot, que le constructeur de `TrxTables`
calcule.

TRX-BD (type 17 : init $1003DA, update $1003E1, render $100408) est porté dans `mdVoiceTrx.cpp`. `mdVoicePortTest`
vérifie d'abord que le sinus calculé et les deux ondes lues dans le programme sont, mot pour mot, la mémoire du DSP
après son init. Puis TRX-BD joue dans la première phase, avec les paquets des fonctions 68k de l'OS : 235 990
échantillons non nuls, 0 échantillon différent et 0 mot d'état différent. Chaque branche du render est atteinte : le
fondu du bloc qui suit un Hit, l'attaque échantillonnée, le clipper au-dessus et au-dessous, le façonneur au-dessus et
au-dessous, et le niveau limité par un Hit qui l'ajoute au-delà de la pleine échelle. Deux mutations sont attrapées :
le gain de la seconde onde, et le niveau que le clipper reprend. Une troisième (`>=` en `>` au seuil haut du
façonneur) passe, parce qu'elle est équivalente : à égalité, les deux chemins donnent le seuil.

Le render écrit la mémoire de travail commune (Y:$00-$3F, X:$06-$07, X:$20-$5F), qu'un render ROM peut lire ensuite.
Il faut donc rendre toute voix ROM ou TRX à chaque bloc, dans l'ordre des pistes.

Coût mesuré (release, médiane de 9 passes, charge 19 %) : 0,47 µs par voix et par bloc de 32, soit 0,065 % d'un cœur
par voix et 1,0 % pour 16 voix, sous les 0,07-0,3 % estimés.

Reste ouvert pour les TRX :
- TRX-SD, XT, CP, RS, CB, CH, OH, CY, MA, CL, XC, B2 et S2.
- Ce que joue une TRX dont la forme d'onde manque, sans flash image. Dans TRX-BD, l'attaque n'est alors plus que du
  bruit, et le reste sonne.
- Leurs fonctions de coefficients 68k tournent encore dans Musashi.

Revue d'architecture du 2026-10-31, reporté à TRX-SD, qui touche le même dispatch :
- `kPorted` rempli par indice (une fonction `constexpr` qui écrit `a[17] = ...`), au lieu des `{}` comptés à la main.
- Une entrée par famille (`trx::routines(type)`), au lieu de trois déclarations par machine dans
  `mdVoiceInternal.h`.
- Les noms des sondes à côté de leur `enum`, que le test lit, au lieu d'une seconde liste dans le test.

Reporté à l'intégration au produit : une fonction de `md::fw` qui remplit `TrxTables` depuis la flash image, appelée
par le test et par le produit, au lieu de la copie que fait le test. Sans flash image, le produit garde ses
`TrxTables` (ondes à zéro) : `Shared::trx` nul reste une erreur de programmation.
