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

## 2026-11-04 : EFM-XT

EFM-XT (type 35 : init $10304E, update $103052, render $103065) est porté dans `mdVoiceEfmXt.cpp`. Sa boucle est celle
d'EFM-BD, mot pour mot, avec son état deux mots plus haut : `bd::pair` sort cette boucle d'EFM-BD, avec l'adresse de
l'état en paramètre, et EFM-BD passe par un tampon de mots. Le résultat traverse le filtre à un pôle d'EFM-SD (Y:$09), qui
passe dans `mdVoiceEfm.h`, tous ses mots arrondis. Un Hit met les deux phases à Y:$0A et ajoute $400000 au niveau.

`mdVoicePortTest` le joue avec les paquets de l'OS : 104 603 échantillons non nuls, 0 échantillon et 0 mot d'état
différents, la probe atteinte. Onze mutations sur onze sont prises. Le scénario des bancs a aussi été comparé à
l'oracle : 3000 blocs, 0 différence.

Revues du 2026-11-04 :
- Revue de code : aucun écart avec le listing.
- Revue d'architecture : `pair` reste dans `bd`, EFM-SD ne pouvant pas l'appeler. EFM-CP n'y touchera qu'à son tour.
- `/simplify` : le compteur de `onePole` reste un entier, car le listing d'EFM-RS arrondit ses deux premiers mots
  seulement.

Coût mesuré (release, médiane de 9 passages, par voix et par bloc de 32 échantillons) : EFM-XT 0,53 µs, soit 1,17 à 1,18 % d'un cœur pour 16 voix.

## 2026-11-05 : EFM-CP

EFM-CP (type 36 : init $1030EC, update $1030F0, render $103104) est porté dans `mdVoiceEfmCp.cpp`. Sa boucle sort le sinus
d'EFM-BD avant l'ampli, son état trois mots plus haut : `bd::sines` donne ce sinus, et `bd::pair` lui ajoute l'ampli pour
EFM-BD et EFM-XT. EFM-CP applique son propre niveau : un Hit le pose à $50C335 avec un compte de claps (Y:$0A). Tant qu'il
reste des claps, le niveau décroît par Y:$0B, et chaque passage sous $66666 le ramène à $50C335 ; après le dernier, il
décroît par Y:$02. Le résultat traverse le filtre à un pôle (Y:$09), sans arrondi.

`mdVoicePortTest` le joue avec les paquets de l'OS et deux paquets faits main : 150 513 échantillons non nuls,
0 échantillon et 0 mot d'état différents, les deux probes atteintes. Le premier paquet donne une enveloppe de hauteur, que
les mots 7 et 8 de l'OS, toujours à 0, cachent, et un niveau négatif. Le second amène le niveau à $66666 exactement, le
plus bas mot qui ne déclenche pas de clap. Le scénario des bancs a aussi été comparé à l'oracle : 3000 blocs, 0 différence.

Sur 23 mutations, 21 sont prises. Deux passent, et elles ne changent rien :
- L'init à 1 au lieu de 0 : l'update suit toujours l'init et récrit ce mot.
- `hi` au lieu de `lim` sur le niveau : le niveau part de $50C335 ou de 0, et un produit ne dépasse donc jamais la pleine
  échelle. `lim` reste pour suivre le listing.

Revues du 2026-11-05 :
- Revue de code : aucun écart avec le listing, y compris les écritures en mémoire de travail. EFM-CP n'écrit ni X:$02 ni
  X:$12, contrairement à EFM-BD.
- Revue d'architecture : `sines` et `pair` gardés, le trou de l'état laissé à l'appelant décrit dans `mdVoiceEfm.h`. Un
  étage d'ampli commun est refusé : EFM-BD ne limite pas, EFM-CP limite et compte.
- `/simplify` : `decay` et la variable du niveau suivant retirés ; le choix de Y:$0B ou Y:$02 se déduit de `claps`. La
  boucle `sx`, en quatre copies, reste : un helper sortirait de ce changement. `trx::countDown` n'est pas repris : il
  limite, là où le DSP range B1 sans limiter (un compte de $800000).

Coût mesuré (release, médiane de 9 passages, par voix et par bloc de 32 échantillons) : EFM-CP 0,50 µs, soit 1,11 % d'un cœur pour 16 voix. EFM-BD reste à
0,39 à 0,41 µs avec ses deux boucles.

## 2026-11-05 : EFM-RS

EFM-RS (type 37 : init $1031A3, update $1031A8, render $1031BB) est porté dans `mdVoiceEfmRs.cpp`. Il joue deux paires de
sinus, chacune un sinus qui s'entend lui-même et qui courbe un second sinus. Les mots Y:$01 à Y:$04 règlent la première
paire et Y:$05 à Y:$08 la seconde. Chaque second sinus a un niveau qu'un Hit pose à $50C335 et que Y:$09 ou Y:$0A fait
décroître. La sortie est la seconde paire plus la première à Y:$0B, à travers le filtre à un pôle (Y:$0C), dont le DSP
arrondit les deux premiers mots seulement.

`mdVoicePortTest` le joue avec les paquets de l'OS et un paquet fait main : 78 820 échantillons non nuls, 0 échantillon et
0 mot d'état différents. Les mots 2 et 6, 4 et 8 de l'OS sont toujours égaux, ce qui cachait toute confusion entre les
deux paires : le paquet fait main, aux mots tirés au hasard, les sépare. Le scénario des bancs a aussi été comparé à
l'oracle : 3000 blocs, 0 différence.

Mutations : 30 prises sur 34 avant `/simplify`, puis 16 sur 16 sur les adresses et la somme du code final. Quatre passent,
et elles ne changent rien :
- L'init à 1 au lieu de 0 : l'update suit toujours l'init et récrit ces mots.
- Les mots bas du retour, Y:$18 et Y:$1A, gardés d'un Hit à l'autre : la phase basse vaut 0 au Hit, donc ces mots ne
  produisent aucune retenue, et le premier échantillon les remplace.
- `lim` au lieu de `hi` sur le niveau : le niveau ne croît jamais depuis celui du Hit.

Revues du 2026-11-05 :
- Revue de code : aucun écart avec le listing, y compris les écritures en mémoire de travail.
- Revue d'architecture : rien ne passe dans `mdVoiceEfm.h`. `sineFedBack`, `fmStep` et `sineStep` sont déjà la couture
  commune, et le modulateur d'EFM-RS n'a ni niveau ni enveloppe de hauteur. Le niveau tient dans un mot : `step` rend un
  mot, et le masque du porteur, sans effet, est retiré. EFM-BD et EFM-SD gardent ce masque, sans effet chez eux aussi.
- `/simplify` : `Pair` porte ses mots et `step` ne prend plus qu'une paire ; une lambda remplace la fonction, que MSVC
  n'inlinait pas, et X:$1D s'écrit après la boucle. Le coût passe de 0,70 à 0,55 µs. Les autres répétitions (`sx`,
  $50C335, le niveau qui décroît) touchent des machines déjà validées et restent hors de ce changement.

Coût mesuré (release, médiane de 9 passages, par voix et par bloc de 32 échantillons) : EFM-RS 0,55 µs, soit 1,21 % d'un
cœur pour 16 voix.

## 2026-11-05 : EFM-CB

EFM-CB (type 38 : init $103272, update $103277, render $10328C) est porté dans `mdVoiceEfmCb.cpp`. Il joue deux paires de
sinus aux hauteurs Y:$01 et Y:$02, sans filtre. Chaque paire a un sinus à Y:$03 de sa hauteur, tous deux au même niveau
que Y:$05 fait décroître, qui courbe par Y:$04 un second sinus à retour (Y:$06). La sortie est la moyenne des deux paires
fois la somme de deux niveaux, Y:$07 et Y:$09 de mots que Y:$08 et Y:$0A font décroître. Le DSP ne limite ni cette somme
ni son produit : la somme déborde et repart de l'autre côté de l'échelle.

`mdVoicePortTest` le joue avec les paquets de l'OS et un paquet fait main : 112 995 échantillons non nuls,
0 échantillon et 0 mot d'état différents. Les mots de l'OS ne font jamais déborder la somme. Le paquet fait main épingle
les mots 7 et 8 à pleine échelle, ce qui tient un niveau à $5A8279. Il a fallu ajouter un second mot épinglé à `TrxEdge` :
le mot 7 seul laissait le niveau décroître dès le deuxième échantillon, et le débordement ne s'entendait jamais, le
premier échantillon valant 0. Le scénario des bancs a aussi été comparé à l'oracle : 3000 blocs, 0 différence.

Sur 42 mutations, 35 sont prises. Sept passent, et elles ne changent rien :
- L'init à 1 au lieu de 0 : l'update suit toujours l'init et récrit ces mots.
- Les mots bas du retour, Y:$17 et Y:$19, gardés d'un Hit à l'autre : la phase basse vaut 0 au Hit, donc aucune retenue.
- `lim` au lieu de `hi` sur les trois niveaux : ils ne croissent jamais depuis ceux du Hit.
- `lim` au lieu de `hi` sur le produit de sortie : il ne dépasse la pleine échelle que pour -1 fois -1, que la somme et la
  moyenne n'atteignent pas ensemble dans ce test.

Revues du 2026-11-05 :
- Revue de code : aucun écart avec le listing, y compris les écritures en mémoire de travail.
- Revue d'architecture : `Pair`, `loadPair` et `storePair` restent locaux, comme dans EFM-RS. Une pièce partagée ajouterait
  environ 7 lignes pour deux utilisateurs. Le commentaire d'en-tête disait un niveau par paire : il est corrigé, le niveau
  étant commun aux deux.
- `/simplify` : l'aller-retour `acc(hi(...))` de la somme est retiré. Le tampon `out` reste : écrire `_out` dans la boucle
  coûte 11 % de plus sans recopier les mots du paquet dans des locales, le bloc pouvant recouvrir `y`. Efficacité : aucun
  appel ni rechargement de `y` dans la boucle. Le seul gain restant se trouve dans `round()` partagé, 5,5 % sans `wrap56`,
  mesuré sur EFM-CB seul : ses autres appelants ne sont pas audités, il reste donc hors de ce changement.

Coût mesuré (release, médiane de 9 passages, par voix et par bloc de 32 échantillons) : EFM-CB 0,56 µs, soit 1,23 % d'un cœur pour 16 voix.
