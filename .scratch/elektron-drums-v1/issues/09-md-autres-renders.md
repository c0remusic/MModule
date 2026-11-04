# Machinedrum : EFM, E12, P-I, GND et les autres renders

Type: AFK
Status: ready-for-agent
Assignee: 
Blocked by: 08

## Quoi

Les renders restants des 39 à porter (hors INP et RAM-R), dans l'ordre EFM, E12, P-I, GND puis le reste, sur le
patron du ticket 08. P-I (rétroactions longues) reste en entiers : c'est la seule famille qui risque de s'entendre en
flottant.

## Critère de fin

- Chaque render bit-exact contre `mdEngine` (Q1) et « sonne pareil » 44,1/48.
- 16 voix MD au pire cas : coût mesuré.

## Références

Spécification §3.2. Ticket « Port du Machinedrum en C++ ».

## 2026-11-04 : EFM-BD

Les machines EFM ont leur en-tête, `mdVoiceEfm.h`, et leur dispatch, `mdVoiceEfm.cpp`. Elles appellent les aides de
`mdVoiceTrx.h`. `Voice` cherche une machine dans `trx::routines`, puis dans `efm::routines`.

EFM-BD (type 33 : init $102EEF, update $102EF3, render $102F06) est porté dans `mdVoiceEfmBd.cpp`. Deux sinus, échantillon
par échantillon : le premier module le second, tous deux à une hauteur (Y:$01) qu'une enveloppe relève et que Y:$08 fait
décroître. Le premier tourne à Y:$03 de cette hauteur et s'entend lui-même par Y:$06, à un niveau que Y:$05 fait
décroître. Il plie la phase du second par Y:$04, et le second sort au niveau d'ampli, qu'un Hit relève et que Y:$02 fait
décroître.

`mdVoicePortTest` le joue avec les paquets de l'OS : 94 821 échantillons non nuls, 0 échantillon et 0 mot d'état
différents, la probe atteinte. Vingt-deux mutations sur vingt-deux sont prises. Le scénario des bancs a aussi été comparé
à l'oracle : 3000 blocs, 0 différence. Le test ne compare pas la mémoire de travail : les écritures miroir de X:$01-$11
suivent le listing, sans contrôle direct.

Revues du 2026-11-04 :
- Revue de code : aucun écart avec le listing.
- Revue d'architecture : l'en-tête EFM, plutôt que des namespaces `trx::efmBd`. Les aides de modulation (`sineFedBack`,
  `sineStep`, `fmStep`) viennent avec EFM-SD, leur deuxième appelant. L'étage de filtre de fin de EFM-SD a deux variantes
  d'arrondi selon la machine : `mac` ou `macr`.
- `/simplify` : commentaires élargis aux machines EFM ; `kTrx` devient `kPorted` dans le test.

Coût mesuré (release, médiane de 9 passages, par voix et par bloc de 32 échantillons) : EFM-BD 0,43 à 0,44 µs, soit 0,95
à 0,96 % d'un cœur pour 16 voix.
