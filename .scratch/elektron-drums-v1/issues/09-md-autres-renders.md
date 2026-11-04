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
  `sineStep`, `fmStep`) viennent avec EFM-SD, leur deuxième appelant.
- `/simplify` : commentaires élargis aux machines EFM ; `kTrx` devient `kPorted` dans le test.

Coût mesuré (release, médiane de 9 passages, par voix et par bloc de 32 échantillons) : EFM-BD 0,43 à 0,44 µs, soit 0,95
à 0,96 % d'un cœur pour 16 voix.

## 2026-11-04 : EFM-SD

EFM-SD (type 34 : init $102F68, update $102F6F, render $102F85) est porté dans `mdVoiceEfmSd.cpp`. Un bruit passe par un
filtre à un pôle (Y:$09). Puis, échantillon par échantillon, les deux sinus d'EFM-BD jouent à la hauteur Y:$01, sans
enveloppe, et le second sort au niveau d'ampli, plus le bruit à Y:$07, à un niveau que Y:$08 fait décroître. Le résultat
passe par un second filtre à un pôle (Y:$0A). EFM-SD limite sa sortie, là où EFM-BD écrit A1 tel quel.

Les aides de modulation passent dans `mdVoiceEfm.h`, avec EFM-BD et EFM-SD pour appelants : `sineFedBack`, `sineStep`,
`fmStep`, et `storeL`, qui range un mot L et son miroir. Le coût d'EFM-BD baisse, voir plus bas.

`mdVoicePortTest` le joue avec les paquets de l'OS : 83 390 échantillons non nuls, 0 échantillon et 0 mot d'état
différents, la probe atteinte. Vingt mutations sur vingt sont prises. Le scénario des bancs a aussi été comparé à
l'oracle : 3000 blocs, 0 différence.

Revues du 2026-11-04 :
- Revue de code : aucun écart avec le listing.
- Revue d'architecture : `onePole` passe dans `mdVoiceEfm.h` avec EFM-XT, avec un nombre de mots arrondis : EFM-SD et
  EFM-CP utilisent `mac` partout, EFM-XT, EFM-HH et EFM-CY `macr` partout, et EFM-RS `macr` sur ses deux premiers
  mots seulement. La paire d'opérateurs reste écrite machine par machine : quatre machines sur huit seulement la
  répètent, et un repli demanderait dix paramètres.
- `/simplify` : le retour d'action passe d'un couple de mots à un entier, car un produit descendu d'un octet n'atteint
  jamais la limite des mots L.

Coût mesuré (release, médiane de 9 passages, par voix et par bloc de 32 échantillons) : EFM-SD 0,62 à 0,63 µs, soit 1,36 à 1,38
% d'un cœur pour 16 voix. EFM-BD passe à 0,39 à 0,40 µs, soit 0,87 à 0,89 %.
