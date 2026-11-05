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

## 2026-11-05 : EFM-HH

EFM-HH (type 39 : init $10332E, update $103332, render $103349) est porté dans `mdVoiceEfmHh.cpp`. Un sinus à Y:$03 de la
hauteur Y:$01 s'entend par Y:$06 et, à un niveau que Y:$05 fait décroître, courbe par Y:$04 un second sinus qui s'entend
par Y:$07. Ce second sinus joue au niveau d'ampli, qu'un Hit relève et que Y:$02 fait décroître, multiplié par Y:$0C, un
mot non signé, et limité. Un troisième sinus, de pas Y:$0B, le module en anneau : Y:$09 de ce sinus plus Y:$0A, limité.
Le résultat traverse le filtre à un pôle (Y:$08), arrondi sur ses 32 mots.

`mdVoicePortTest` le joue avec les paquets de l'OS et un paquet fait main : 103 684 échantillons non nuls, 0 échantillon et
0 mot d'état différents, la probe du niveau limité atteinte. Le mot 6 de l'OS vaut toujours 0, ce qui cache le retour du
premier sinus : le paquet fait main, aux mots tirés au hasard, le fait entendre. Une mutation qui remplace ce mot par 0 ne
tombe qu'au bloc 6011, dans la phase des paquets faits main. Le scénario des bancs a aussi été comparé à l'oracle :
3000 blocs, 0 différence.

Sur 37 mutations, 32 sont prises. Cinq passent, et elles ne changent rien :
- Les mots bas des retours, Y:$17 et Y:$19, gardés d'un Hit à l'autre : la phase basse vaut 0 au Hit, donc aucune retenue.
- `lim` au lieu de `hi` sur le niveau du modulateur et sur celui de l'ampli : ils ne croissent jamais au-delà de la pleine
  échelle positive.
- `lim` au lieu de `hi` sur le produit de sortie : il ne dépasse la pleine échelle que pour -1 fois -1, que ce test
  n'atteint pas.

Revues du 2026-11-05 :
- Revue de code : aucun écart avec le listing, y compris les écritures en mémoire de travail. Le DSP ne copie pas Y:$08 en
  X:$08 : les deux boucles de copie suivent ce trou, le filtre lisant Y:$08 directement.
- Revue d'architecture : aucun helper de `mdVoiceEfm.h` n'est encore mérité, et EFM-HH garde des locales comme EFM-SD, ses
  deux sinus à retour n'ayant aucun équivalent. La question se rouvrira avec EFM-CY, dernière machine de la famille. Le
  commentaire d'en-tête nomme maintenant la hauteur du second sinus et les deux limites.
- `/simplify` : rien à changer dans le rendu. Le test renomme `TrxEdge` en `PacketEdge`, sa table tenant aussi les EFM, et
  le commentaire de TRX-MA retrouve sa ligne. Efficacité : gains restants d'au plus 2 %, par des helpers privés, écartés.
  Le premier passage `/simplify` a été fait par un seul agent sur les quatre angles, et il a donc été refait avec quatre
  agents, comme la consigne le demande.

Coût mesuré (release, médiane de 9 passages, par voix et par bloc de 32 échantillons) : EFM-HH 0,58 µs, soit 1,29 % d'un
cœur pour 16 voix.

## 2026-11-05 : EFM-CY

EFM-CY (type 40 : init $1033F1, update $1033F4, render $10340F) est porté dans `mdVoiceEfmCy.cpp`. Il travaille par étages
sur tout le bloc, dans la mémoire interne X:$21-$60 et Y:$21-$60. Quatre sinus lus dans la table, aux pas Y:$06-$09 et au
gain Y:$0A, tombent d'un même niveau que Y:$0B fait décroître. Chacun courbe ensuite la phase d'un sinus à lui, aux
hauteurs Y:$01-$04, qui s'entend par Y:$0C. La sortie est leur moyenne au niveau d'ampli, qu'un Hit relève et que Y:$05
fait décroître, fois deux fois Y:$0E, un mot non signé, limité, à travers deux filtres à un pôle (Y:$0D).

`mdVoicePortTest` le joue avec les paquets de l'OS et un paquet fait main : 78 195 échantillons non nuls, 0 échantillon et
0 mot d'état différents, la probe du niveau limité atteinte. Le mot $0E de l'OS ne dépasse pas $400000, ce qui cache la
limite de l'ampli : le paquet fait main le met à pleine échelle, le mot 5 aussi, et tient les autres sous $8000 par un
nouveau champ `bits` de `PacketEdge`. Le scénario des bancs a aussi été comparé à l'oracle : 3000 blocs, 0 différence.

Un écart reste, hors de ce que l'OS produit : un pas de table de $8000 ou plus. Les mots 6 à 9 de l'OS ne dépassent pas
$5736. Au-delà, l'adressage modulo du DSP sort de la table et lit ailleurs, là où le port reste dedans. Le commentaire
d'en-tête le dit, et le ticket le garde ici.

Mutations : 36 prises sur 42, puis 11 sur 12 après la réécriture de `/simplify`. Ce qui passe ne change rien, et une partie a
conduit à retirer le code :
- `lim` au lieu de `hi` sur les quatre mots de l'étage de niveau : inaccessible, retiré.
- Le masque de `b` : le mot suivant ne lit que ses 24 bits bas, retiré.
- `lim` au lieu de `hi` sur le niveau et sur l'ampli : ils ne croissent jamais depuis ceux du Hit.
- `lim` au lieu de `hi` sur le produit de sortie : il ne dépasse la pleine échelle que pour -1 fois -1.

Revues du 2026-11-05 :
- Revue de code : aucun écart avec le listing pour les paquets de l'OS, y compris le contenu final de X:$21-$60 et Y:$21-$60
  relu à la main.
- Revue d'architecture, famille EFM close : aucun helper de plus n'est mérité. Compte fait, la charge et le stockage des
  phases coûteraient 11 lignes de plus, la strophe du modulateur 2, la paire niveau et décroissance 0, la boucle `sx` en
  gagnerait 1. Les boucles de copie du paquet en gagneraient 6, mais toucheraient cinq fichiers déjà validés : écarté.
- `/simplify` : `kTable` reprend `TrxTables::SineAddr`, les tampons deviennent `sine1` à `sine4`, le niveau `modLevel`. Les
  quatre boucles `bentSine` n'en font plus qu'une : chaque lecture attend la précédente, et des chaînes successives
  laissent le processeur n'en mener qu'une à la fois. Le coût passe de 1,91 à 1,42 µs pour les mêmes mots. La fusion des
  deux filtres, 8 % de plus, est écartée : elle casserait `onePole`, commun. `word()` reste : les mots d'une autre machine
  peuvent atteindre Y:$0F avant son Hit.

Un trou de test reste, pour toutes les machines. `mdVoicePortTest` compare les échantillons et l'état, jamais la mémoire de
travail : une écriture fausse ne s'y voit que si une machine ROM la lit par un pointeur périmé. Ces écritures reposent donc
sur la revue de code, machine par machine.

Coût mesuré (release, médiane de 9 passages, par voix et par bloc de 32 échantillons) : EFM-CY 1,42 µs, soit 3,13 % d'un
cœur pour 16 voix, la plus chère des machines portées.

## 2026-11-05 : GND-SN et GND-NS

GND-SN (type 2 : update $1000A4, render $1000AD) et GND-NS (type 3 : init $1000DD, update $1000E4, render $1000EA) sont
portés dans `mdVoiceGnd.cpp`. GND-SN joue un sinus au quart de la pleine échelle, à la hauteur Y:$01 plus une enveloppe qui
décroît une fois par bloc, à un niveau d'ampli de 48 bits que Y:$02 fait décroître. GND-NS joue le bruit de Fibonacci à un
niveau de 48 bits que Y:$01 fait décroître, dont il n'entend que le mot haut. Tous deux gardent leur bloc en X:$00-$1F.

`mdVoicePortTest` les joue avec les paquets de l'OS et deux paquets faits main, aux mots tirés au hasard : 47 371 et
38 014 échantillons non nuls, 0 échantillon et 0 mot d'état différents. Le scénario des bancs a aussi été comparé à
l'oracle : 3000 blocs, 0 différence pour chacun.

Sur 25 mutations, 22 sont prises. Trois passent, et elles ne changent rien : `hi` au lieu de `lim` sur Y:$06, sur la sortie
de GND-SN, un quart de pleine échelle, et sur celle de GND-NS, qui ne déborde que pour -1 fois -1.

Revues du 2026-11-05 :
- Revue de code : aucun écart avec le listing, y compris le contenu final de X:$00-$1F.
- Revue d'architecture : `gnd::routines(type)` aurait la forme de TRX et d'EFM, en déplaçant GND-IM. Écarté : ce déplacement
  sort de ce changement, et `rom::` tient déjà le même rang dans le dispatch. La vérification devenue morte dans `find`
  est retirée.
- `/simplify` : la boucle du sinus devient `trx::sines`, et le niveau de 48 bits un seul `int64_t`, ce qui retire 15 lignes.
  Les deux machines vont en fin de `kPorted` : en tête, elles auraient sorti EFM-BD et EFM-SD du balayage à 16 voix.

Coût mesuré (release, médiane de 9 passages, par voix et par bloc de 32 échantillons) : GND-SN 0,18 µs, GND-NS 0,13 µs.

## 2026-11-05 : E12

Les 16 machines E12 (types 49 à 64, quatre renders : $103838, $103945, $103A10, $103B46) sont portées dans
`mdVoiceE12.cpp`. Elles lisent un échantillon de 12 bits, position sur 48 bits, interpolé sur 4 points par une B-spline
cubique. E12-BD n'a pas de filtre, neuf machines finissent par un passe-haut à deux pôles dont le gain suit les pôles, et
six mélangent un second échantillon avant un passe-haut fixe. Un retrigger ramène la position au départ.

Les échantillons et leur répertoire sont les données du propriétaire (spec 11.4 amendée le 2026-11-05) : un bloc
`E12Bank`, de $103D7B à $13516D, que l'appelant fournit par `Shared`, sans quoi ces machines se taisent. Le rendu ne lit
jamais la flash image. Les tables sont par formule, testées mot à mot contre le DSP : décalages $103C7B et $103CFB, la
formule de $140000 avec Q de 0,51 à 14, et interpolation $147200, à une exception près. Le DSP tient 1 de moins à k = 11,
qu'aucune forme essayée ne donne : `makeInterpolation` corrige ce cas, et `OsTables::curve` fixe ses deux premiers mots. Ces
trois nombres restent dans le code comme les autres constantes du listing. Les fonctions de coefficients d'E12 sont portées,
6,74 millions de paquets à 0 différence contre Musashi.

`mdVoicePortTest` joue les 16 machines avec les paquets de l'OS et des paquets faits main : 0 échantillon et 0 mot d'état
différents, de 19 570 à 70 141 échantillons non nuls par machine, chaque probe atteinte.

Sur 44 mutations, 32 prises au premier passage, surtout parce que le test ne voyait pas la mémoire de travail. Après
l'ajout ci-dessous et le nettoyage, les trois qui passent encore sont : le bord exact de la fin de l'échantillon, et celui de
la fin du second, que les paquets n'atteignent pas, et `tapWord` à $100 au lieu de $200, dont la plage n'est pas lue.

**La mémoire de travail est maintenant comparée.** `mdVoicePortTest` compare X:$000-$1FF et Y:$000-$0FF après chaque bloc,
pour toutes les machines, avec une preuve par un mot corrompu. Cela ferme le trou noté à EFM-CY et a trouvé deux défauts
anciens, qu'aucun échantillon ne montrait :
- TRX-CB, avec un paquet fait main dont les indices de filtre sortent des tables : le DSP lit la banque UW, le port lisait
  0, et le mot resté en scratch finissait lu par un pointeur périmé de ROM-47. `filterWord`, `pole` et `offsetPole` lisent
  maintenant par `external()`.
- 726 lectures par pointeur périmé donnaient au test un mot que le produit n'avait pas : le crochet du test lisait ce que
  le produit ne calculait pas. `external()` sert maintenant le sinus et les ondes de TRX-CH, et masque les adresses à
  $1FFFFF comme le JIT. Il en reste une, Y:$1427A8, dans l'excitation de P-I-BD, tant que P-I n'est pas porté.

Le crochet lit X ou Y selon l'accès : il lisait toujours Y.

Hors de ce que l'OS produit, `word()` rend encore 0 hors de certaines tables (sine masqué, ondes de BD, SD, RS et CH) : même
classe de défaut, sur des mots que l'OS ne donne pas.

Revues du 2026-11-05 :
- Revue de code : aucun écart avec le listing. $1038A1-$1038AB est mort, et `clr b ifmi` à $10384E ne peut pas agir.
- Revue d'architecture : `E12Bank` est un bloc de taille fixe et un pointeur simple, `filter` prend ses coefficients. `TrxTables`
  garde son nom, à revoir à part : quatre familles le lisent.
- `/simplify` : `renderTwo` remplace deux renders identiques à un gain près, `kRoutines` le switch, et les fonctions de
  coefficients partagent `e12Body`, `retriggers` et `retriggerTime`, dont ROM. `%` dans `loadWindow` coûtait 65 % : une
  comparaison le remplace. Les chemins rapides restent, mesurés. Le gain de 9 à 14 % dans `mul` et `lim`, communs à toutes
  les machines, viendra dans son propre commit.

Coût mesuré (release, par voix et par bloc de 32 échantillons, machine chargée) : E12-BD 0,47 µs, render commun 0,64 à
0,66 µs, E12-RS 0,99 µs, mélangées 1,00 µs.
