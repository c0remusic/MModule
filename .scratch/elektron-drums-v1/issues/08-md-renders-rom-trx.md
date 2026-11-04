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

Découpe du 2026-11-03, un pur déplacement : `mdVoiceTrx.cpp` ne garde que `TrxTables` et `trx::routines` ;
chaque machine a son fichier (`mdVoiceTrxBd.cpp`, `Sd`, `Xt`, `Cp`, `Rs`), et les fonctions partagées passent dans
`mdVoiceTrx.h`, `inline`, avec les déclarations des routines. Le code déplacé est identique à la ligne près, hors
indentation et `inline`. `mdVoicePortTest` donne la même sortie qu'avant, mot pour mot. Le `switch` de
`trx::routines` reste : cinq cas, une table n'apporterait rien tant qu'il n'y en a pas davantage.

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

## 2026-11-02 : TRX-SD porté, et un défaut du JIT de l'émulateur

TRX-SD (type 18 : init $10074C, update $10075B, render $100776) est porté dans `mdVoiceTrx.cpp`. Chaque bloc, le render
calcule deux sinus, qui suivent une enveloppe de hauteur : elle monte pendant onze blocs après un Hit, puis décroît.
Les deux sinus sont mélangés avec un terme croisé, puis passent par l'enveloppe d'amplitude. Un bruit de Fibonacci traverse
deux filtres à deux pôles identiques, avec sa propre enveloppe, puis s'ajoute au son. Pendant 128 blocs après un Hit,
une onde échantillonnée de 4096 mots s'y ajoute encore.

Le render lit quatre tables :
- le sinus en Y:$148000, identique au sinus X que `TrxTables` calcule déjà ;
- l'onde, en P:$100881 ;
- deux tables de coefficients de filtre, en X:$140100 et X:$141100, de 4096 mots chacune. Une valeur du paquet y
  choisit un angle de pôle et un rayon de pôle.

Ces trois dernières sont chargées par le programme voix. `md::fw::dspWords` les lit dans la flash image, et le test
vérifie qu'elles sont, mot pour mot, la mémoire du DSP. Pour les coefficients de filtre, cette copie est provisoire,
comme pour les tables des ROM : il faut leur formule.

Le dispatch suit la revue d'architecture du 2026-10-31 :
- les types GND sont dans une table de cinq ;
- les ROM et les TRX sont trouvés par famille, les TRX par `trx::routines(type)` ;
- les noms des sondes sont à côté de leur `enum`.

Prouvé par `mdVoicePortTest`, contre `VoiceEngine` : 5000 blocs, 199 995 échantillons TRX-SD non nuls, 0 échantillon
et 0 mot d'état différents. Chaque branche sondée est atteinte : hauteur qui monte, hauteur qui décroît, onde, fin
après l'onde, niveau limité par un Hit. Quatre mutations sont attrapées : l'index du filtre, le décalage du terme
croisé, l'index de l'onde et le seuil de l'enveloppe de hauteur.

**Le JIT du dsp56300 corrompt X1:X0 au `DO #$1F` de TRX-SD ($100792).** Juste avant le `DO`, X vaut le pas de phase
($A5:000000 dans le cas mesuré). Dès la première itération, il vaut $100:000020, quels que soient le paquet et les
instructions du corps de boucle, toutes remplacées une à une par des `nop`. L'interpréteur de l'émulateur, lui, garde
X, et le port lui est égal sur 4000 blocs. L'optimiseur du JIT n'y est pour rien : coupé, le défaut reste. Le
`DO #xx` d'un DSP56300 n'écrit pas X : le défaut vient de l'émulateur, pas de l'OS.

Conséquences :
- L'oracle du test tourne désormais sur l'interpréteur (`VoiceEngine::setInterpreter`). Le test entier prend 6 s.
- Le crochet des pointeurs égarés suit l'interpréteur, qui lit 0 au-delà de sa mémoire, alors que le JIT masque
  l'adresse à 2M mots. Sur un seul cas des paquets faits main (ROM-42, un seul échantillon, hauteur maximale), le JIT
  sonnait et l'interpréteur se tait. Les deux comportements sont des artefacts de l'émulateur.
- MoDule fait tourner `VoiceEngine` sur le JIT. Son TRX-SD joue donc probablement ce défaut : le pas des sinus ne
  dépend plus de PTCH. Mesuré dans `VoiceEngine` seul, pas dans le plug-in. À corriger dans le fork
  c0remusic/dsp56300-md-mm, comme travail à part.

Coût mesuré (release, médiane de 9 passes, machine à 0,5 cœur sur 16) : 0,62 µs par voix et par bloc de 32, soit
0,086 % d'un cœur par voix et 1,4 % pour 16 voix. C'est dans les 0,07 à 0,3 % estimés. TRX-BD, mesuré à nouveau dans
les mêmes conditions : 0,47 µs.

Revue d'architecture du 2026-11-02 : le découpage par famille tient pour les douze TRX à venir. Ce qui est reporté :
- À l'intégration au produit : la recette des `TrxTables` vit dans le test seul. Chaque table coûte un champ, une
  adresse, une copie et une vérification. Une fonction qui les remplit, appelée par le test et par le produit.
- `testHooks::probe` appelle `hooks()`, défini dans `mdVoiceRom.cpp`, à chaque échantillon des boucles sondées. Une
  variable `inline` dans `mdVoiceInternal.h` en ferait un chargement et un test.
- Quand une autre famille arrive (EFM, E12) : enchaîner les familles dans `find`, chacune rendant nullptr pour les
  types qui ne sont pas les siens.

## 2026-11-02 : TRX-XT porté

TRX-XT (type 19 : init $101882, un simple RTS ; update $101883 ; render $101891) est porté dans `mdVoiceTrx.cpp`.
Chaque bloc, le render calcule un sinus balayé par une enveloppe de hauteur qui décroît, puis applique l'enveloppe
d'amplitude, puis un écrêteur dont les niveaux bougent d'un pas au-delà de ses deux seuils. Particularité du code de
l'OS : dans la boucle du sinus, le mot de poids faible du pas est, dès le deuxième échantillon, le dernier mot de
sinus lu, car la boucle le charge dans X0, que l'addition utilise. Les trois blocs qui suivent un Hit sont à part :
le premier fait décroître le dernier échantillon (x 0,9 par échantillon), comme celui de TRX-BD ; les deux suivants
jouent chacun 32 mots d'une onde échantillonnée (P:$10190F, 64 mots), multipliés par un mot du paquet.

L'écrêteur ressemble à celui de TRX-BD sans lui être égal : le signal n'est pas divisé par deux, un seuil atteint
compte comme dépassé, et les seuils ne bougent pas. Il n'est donc pas partagé.

Prouvé par `mdVoicePortTest`, contre `VoiceEngine` sur l'interpréteur : 5000 blocs, 197 446 échantillons TRX-XT non
nuls, 0 échantillon et 0 mot d'état différents. Chaque branche sondée est atteinte : fondu, onde A, onde B, écrêtage
au-dessus, sur le seuil haut, au-dessous. Le cas « sur le seuil » n'arrive pas avec les paquets de l'OS ; un paquet
fait main (TRX-XT muet, seuil haut à 0) le produit. Quatre mutations sont attrapées par les échantillons : le X0
gardé constant dans la boucle du sinus, `>=` remplacé par `>` au seuil haut, les deux ondes échangées, et le dernier
échantillon pris un rang trop tôt.

Le défaut du JIT au `DO` ne touche pas TRX-XT : avec l'oracle sur le JIT et TRX-XT seul joué pendant 4000 blocs,
aucune différence. Le TRX-XT de MoDule sonne donc comme celui de la machine.

Coût mesuré (release, médiane de 9 passes) : 0,21 µs par voix et par bloc de 32, soit 0,029 % d'un cœur par voix et
0,46 % pour 16 voix.

Revues du 2026-11-02 :
- La revue de code ne trouve aucun écart avec le listing du DSP.
- La revue d'architecture a fait appliquer trois changements. Les deux ondes de TRX-XT, contiguës, sont une seule
  table. L'enveloppe d'amplitude, trois copies identiques (deux dans TRX-SD, une dans TRX-XT), est la fonction
  `envelope`. Le fondu après un Hit, identique dans TRX-BD et TRX-XT, est `fadeOut`. Le commentaire de l'écrêteur dit
  en quoi il diffère de celui de TRX-BD.
- Reporté à la prochaine machine : le scénario du test. Chaque machine ajoutée à `kMachines`, et chaque paquet fait
  main tiré avant ceux de GND-IM et des ROM, diminue la part de ces derniers et déplace toute la suite aléatoire. Un
  commit de machine finira par rendre le test rouge sur un bord ROM sans rapport avec elle. À faire : un seul tirage
  qui choisit le genre de paquet fait main dans une liste, un poids fixe pour les ROM dans la phase de l'OS, et une
  boucle sur les types TRX pour le compte des échantillons et le contrôle des bords.

Découpe du 2026-11-03, un pur déplacement : `mdVoiceTrx.cpp` ne garde que `TrxTables` et `trx::routines` ;
chaque machine a son fichier (`mdVoiceTrxBd.cpp`, `Sd`, `Xt`, `Cp`, `Rs`), et les fonctions partagées passent dans
`mdVoiceTrx.h`, `inline`, avec les déclarations des routines. Le code déplacé est identique à la ligne près, hors
indentation et `inline`. `mdVoicePortTest` donne la même sortie qu'avant, mot pour mot. Le `switch` de
`trx::routines` reste : cinq cas, une table n'apporterait rien tant qu'il n'y en a pas davantage.

Reste ouvert pour les TRX :
- TRX-CP, RS, CB, CH, OH, CY, MA, CL, XC, B2 et S2.
- La formule des deux tables de filtre.

## 2026-11-02 : le défaut du JIT corrigé

Le `DO` n'y était pour rien. Le JIT perdait une écriture de X1 ou X0 quand le même bloc lisait ensuite X entier. Le pool
de registres fusionne alors les moitiés dans le registre X entier, puis effaçait leur marque « écrit ». Or ce registre
entier n'est qu'un cache de lecture, jamais rangé en mémoire. À la fin du bloc, rien ne rangeait X, et le DSP gardait
la valeur d'avant le bloc. Même chose pour Y. Le render de TRX-SD fait exactement ça : `move a1,x1`, `move a0,x0`,
`add x,b`, puis le `DO #$1F`, qui termine le bloc. Les `nop` mis dans le corps de boucle ne pouvaient donc rien changer.

Correction dans c0remusic/dsp56300-md-mm (`mmodule`, 60876a47, `JitRegPoolRegPair::get`) : les moitiés restent
marquées écrites après la fusion. Le test unitaire `xy_partialsThenWhole` de `dsp56kTestRunner` échoue sans la
correction (sur X1) et passe avec.

`mdVoicePortTest` repasse sur le JIT pour la phase de l'OS (4000 blocs), comme MoDule fait tourner le DSP voix, et sur
l'interpréteur pour les paquets faits main. Pendant la phase JIT, le crochet des pointeurs égarés masque l'adresse à
la taille de la mémoire, comme le JIT. Sur le dsp56300 précédent, le test échoue au bloc 67 ; sur 60876a47, il passe,
en 1,9 s au lieu de 6.

MoDule (branche claude/confident-bassi-cfe7b4) prend 60876a47 : son TRX-SD avance ses sinus au pas que PTCH donne,
au lieu du pas resté dans X.

## 2026-11-02 : scénario du test refait, TRX-CP porté

Le scénario de `mdVoicePortTest`, reporté au port de TRX-XT, est refait avant TRX-CP. Dans la phase de l'OS, un
changement de machine tire une ROM une fois sur deux, quelles que soient les autres machines portées. Dans la phase
des paquets faits main, la moitié des Hits va aux ROM ; l'autre moitié tire un genre dans `EdgeKind` (bords de GND-IM,
TRX-XT sur son seuil). Les types TRX portés sont une seule liste, `kTrx`, dont le test tire les machines et vérifie
que chacune sonne. Ajouter une machine ne change plus la part des bords ROM. Après la refonte, tous les bords et
toutes les sondes sont atteints.

TRX-CP (type 20 : init $10194F, update $101966, render $101972) est porté dans `mdVoiceTrx.cpp`. Chaque bloc, le
render tire un premier bruit de Fibonacci, le passe dans deux filtres à deux pôles en cascade et le garde en Y:$60.
Il tire ensuite un second bruit, celui du clap. Son niveau monte d'un mot du paquet à chaque bloc où l'un des trois
compteurs (Y:$0C, puis $0D, puis $0E) court encore, puis décroît au taux de cette salve. Ce second bruit passe dans
deux passe-haut en cascade, 1/2 - z^-1 + 1/2 z^-2 sur les mêmes pôles. Il passe ensuite dans deux autres filtres à
deux pôles, avec deux échantillons de retard, car leur entrée commence aux deux derniers mots du bloc précédent. La
sortie vaut le quart de la somme du clap et du premier bruit filtré, ce dernier à un niveau qui décroît d'un bloc à
l'autre. Chaque filtre lit ses coefficients dans les tables X:$140000-$1420FF : un mot de base plus un décalage tiré
de deux tables de $80 mots, puis l'angle et le rayon du pôle.

Prouvé par `mdVoicePortTest`, la phase de l'OS sur le JIT : 5000 blocs, 118 910 échantillons TRX-CP non nuls,
0 échantillon et 0 mot d'état différents. Toutes les sondes sont atteintes : les trois salves, la décroissance
finale, et le niveau limité à une salve (80 fois). Six mutations, cinq attrapées par les échantillons : la somme
divisée par deux au lieu de quatre, histoire du second passe-haut inversée, niveau non limité, mauvaise table de décalage, histoire du
troisième filtre inversée. La sixième, le taux de la deuxième salve lu en Y:$12 au lieu de Y:$11, est équivalente.
L'update met Y:$11 à Y:$13 tous à Y:$03, et le paquet de l'OS ne dépasse jamais 15 mots (mesuré sur 2000 jeux de
boutons au hasard). Les mots de base valent au plus $6DF et les décalages au plus $40 : les tables ne sont jamais
dépassées.

Coût mesuré (release, médiane de 9 passes) : 0,95 µs par voix et par bloc de 32, soit 0,13 % d'un cœur par voix et
2,10 % pour 16 voix.

Revues du 2026-11-02 :
- La revue de code ne trouve aucun écart avec le listing. Elle relève des lignes de plus de 120 colonnes et un
  en-tête de fichier sans TRX-CP, corrigés.
- La revue d'architecture a fait appliquer quatre changements :
  - le bruit de Fibonacci (`fibonacci`) et la paire de filtres à deux pôles (`twoPoles`), identiques dans TRX-SD et
    TRX-CP, sont partagés ;
  - le calcul des coefficients (`poles`) aussi : TRX-SD est le même calcul, avec des décalages fixes de -3 et -$241 ;
  - les trois tables de filtre sont une seule table, `filters`, X:$140000-$1420FF, puisque les décalages de TRX-CP
    franchissent les bords entre elles. Un index de TRX-SD qui passe sous zéro lit désormais la table voisine, comme
    le DSP, au lieu de 0. Les paquets de l'OS n'y vont pas, donc ce cas n'est pas prouvé par le test ;
  - la liste des machines du test se déduit de `kTrx`.

## 2026-11-02 : TRX-RS porté, et /simplify sur tout mdVoice

TRX-RS (type 21 : init $101AB0, update $101AB8, render $101ACD) est porté dans `mdVoiceTrx.cpp`. Chaque bloc, le
render calcule un sinus au pas PTCH / 32. Il le replie au-delà d'un seuil (Y:$03), dans les deux sens, d'un niveau qui
grandit de Y:$04 à chaque échantillon passé le seuil, puis le divise par deux. Pendant les 24 blocs qui suivent un
Hit, il ajoute une onde échantillonnée (X:$101BA0, $300 mots), puis applique l'enveloppe d'amplitude. Viennent ensuite
deux passe-haut en cascade, en double précision : chaque accumulateur garde la sortie précédente sur 48 bits, et
chaque sortie est rangée en mot long. C'est le passe-haut de TRX-CP, gardé sur 48 bits. La sortie vaut le double du
second filtre, avec deux échantillons de retard. L'update fixe les pôles (Y:$06 = $200, Y:$07 = 0) et la demi-échelle
(Y:$05 = $400000). Le paquet de l'OS ne fait que 5 mots, donc rien d'autre ne les change.

Prouvé par `mdVoicePortTest` : 5000 blocs, 161 081 échantillons TRX-RS non nuls, 0 échantillon et 0 mot d'état
différents, toutes les sondes atteintes. Sept mutations, six attrapées par les échantillons : l'addition au lieu de la
soustraction au repli, le mot bas de l'accumulateur oublié, un seul terme a1 au lieu de deux, le pas divisé par 16, une
onde décalée d'un mot, le retard de deux échantillons ramené à un. La septième (la cellule zéro du second filtre qui
lit le mot limité au lieu des bits bruts de A1) ne diffère que si l'accumulateur du premier dépasse 48 bits. Un
paquet fait main au niveau maximal (sans décroissance, seuil près de 0) ne l'y a jamais mené, avec des pôles fixes :
la mutation est équivalente en pratique, et ce paquet n'a pas été gardé.

Coût mesuré (release, médiane de 9 passes) : 0,53 µs par voix et par bloc, 1,17 % pour 16 voix.

Revues du 2026-11-02 :
- Revue de code : aucun écart de comportement avec le listing. Lignes trop longues et en-tête corrigés.
- Revue d'architecture : `Pole` (la lecture des tables) séparé de `poles` (les coefficients), parce que TRX-RS
  a ses propres coefficients ; le lambda `feedback` renommé pour ne plus masquer `poles` ; un commentaire qui relie
  le filtre de TRX-RS à celui de TRX-CP ; un autre qui dit pourquoi Y:$13 et Y:$14 survivent à un Hit.
- Reporté au commit qui suit : découper `mdVoiceTrx.cpp` en un fichier par machine, plus un en-tête interne pour les
  fonctions partagées. C'est un pur déplacement de code, à faire avant TRX-CB.

`/simplify` (demandé le 2026-11-02, désormais à chaque machine) a tourné sur tout `mdVoice/`, quatre angles. Appliqué :
- Fonctions partagées :
  - `sines` (la boucle de sinus de TRX-BD et TRX-RS) ;
  - `hitLevel` (le niveau ajouté à un Hit, dans TRX-BD, TRX-SD et TRX-RS) ;
  - `highPassZeros` et `highPassTaps` (TRX-CP et TRX-RS) ;
  - `mulSU`, `mulUU` et `dmac` dans `mdVoiceDsp.h`, pour le sinus du boot et le render ROM.
- Doublons supprimés : `sx` et `isRom`, qui existaient en deux exemplaires chacun.
- Le nom des sondes est un `switch` (`probeName`) au lieu d'une liste parallèle à l'enum : un nom ne peut plus aller
  à la mauvaise sonde. Le test échoue si une sonde n'a pas de nom.
- Code mort ou dérivable : deux copies de 0 dans l'update de TRX-BD, une branche dupliquée et une écriture morte dans
  le décimateur ROM.
- Coût : variables locales au lieu de relectures mémoire (bruit de Fibonacci, filtres à deux pôles, passe-haut),
  écritures de X:$42-$43 sorties de la boucle de TRX-RS, demi-échelle de TRX-RS fusionnée au repli. TRX-CP passe de
  0,95 à 0,77 µs par voix et par bloc ; TRX-RS ne change pas.
- Test : `firstDifference` générique au lieu de trois copies de 41 Mo, une seule fonction de remplissage depuis la
  mémoire de l'oracle, `reserve` sur les enregistrements.

`mdVoicePortTest` repasse à l'identique : mêmes comptes d'échantillons pour chaque machine, 0 différence. Le scénario
des bancs (16 voix aux réglages par défaut) a aussi été comparé à l'oracle pour TRX-CP et TRX-RS : 3000 blocs, 0
différence.

Non appliqué, noté pour plus tard :
- Lectures de filtre hors table : le port lit 0 là où le DSP lit la mémoire voisine. Les paquets de l'OS n'y vont pas.
  La correction passerait par le crochet `stray` des ROM, étendu à la mémoire X.
- Une table de dispatch par type, au lieu du `switch` de `trx::routines` et des listes répétées : à voir avec la
  découpe du fichier.
- Un paquet fait main générique pour tous les TRX (mots au hasard, certains à 0), à la place de `XtOnThreshold`. Il
  rendrait visibles les lectures hors table ci-dessus.
- Petits gains : bornes vérifiées une fois par bloc pour les ondes, ordre des tests dans `yRead` (ROM).

Découpe du 2026-11-03, un pur déplacement : `mdVoiceTrx.cpp` ne garde que `TrxTables` et `trx::routines` ;
chaque machine a son fichier (`mdVoiceTrxBd.cpp`, `Sd`, `Xt`, `Cp`, `Rs`), et les fonctions partagées passent dans
`mdVoiceTrx.h`, `inline`, avec les déclarations des routines. Le code déplacé est identique à la ligne près, hors
indentation et `inline`. `mdVoicePortTest` donne la même sortie qu'avant, mot pour mot. Le `switch` de
`trx::routines` reste : cinq cas, une table n'apporterait rien tant qu'il n'y en a pas davantage.

## TRX-CB

TRX-CB (type 22 : init $101EA0, update $101EBB, render $101EC4) est porté dans `mdVoiceTrxCb.cpp`. Chaque bloc, le
render joue deux sinus : le premier à Y:$03 moins un désaccord, le second à Y:$04 plus ce désaccord, qui décroît de
Y:$02 à chaque bloc. Les deux phases sont masquées après chaque pas, et donc gardées masquées dans l'état, comme pour
TRX-SD. Chaque sinus devient un carré au-delà de son seuil, dans un sens comme dans l'autre ($7FFCB9 au-dessus,
$800347 sous l'opposé, le second test l'emporte). Il est ensuite multiplié par son niveau (Y:$05, Y:$06) fois
l'enveloppe Y:$0F, qui décroît de Y:$01. Chacun passe ensuite par les deux passe-haut de TRX-CP : c'est la même boucle
DSP ($101FA7 contre $101A3F), à des registres près. La sortie est le quart de leur somme, deux échantillons en retard.
L'update ajoute le niveau d'un Hit à Y:$0F, avec limitation, et met Y:$0E à 3. Les trois blocs suivants : un fondu du
dernier échantillon (x 0,9), puis deux blocs muets.

Le paquet de l'OS fait 11 à 12 mots (Y:$01-$0B) : Y:$0C et Y:$0D, les décalages des filtres, restent à $7F depuis
l'init, et les seuils sont toujours positifs.

`mdVoicePortTest` le joue avec les paquets de l'OS et un paquet fait main de plus, sinus immobiles et seuils à 0, où
la voix neuve tombe pile sur le seuil : 111 213 échantillons non nuls, 0 échantillon et 0 mot d'état différents, les
six probes atteintes. Neuf mutations sur dix sont prises. Celle du seuil (`>=` au lieu de `>`) ne l'était que par le
paquet fait main, qui la prend au bloc 4009. La dixième, le décalage du second filtre lu en Y:$0C au lieu de Y:$0D, est
équivalente : les deux valent toujours $7F. Le scénario des bancs (16 voix aux réglages par défaut) a aussi été
comparé à l'oracle, pour TRX-CB et TRX-CP : 3000 blocs, 0 différence.

Revues du 2026-11-03 :
- Revue de code : aucun écart avec le listing.
- Revue d'architecture :
  - `sinesMaskedAfter` (le sinus masqué après le pas) rejoint `sines` dans `mdVoiceTrx.h`, et TRX-SD l'utilise. C'est
    une fonction à part plutôt qu'un booléen, puisque la place du masque change l'état comparé.
  - `highPassStage` porte les deux passe-haut avec leurs adresses et leur état, pour TRX-CP et TRX-CB.
  - Le compte à rebours de TRX-CB est écrit comme celui de TRX-XT, et `clipSquare` remplace `square`.
- `/simplify` :
  - Un seul passage par sinus (carré et niveau dans la fonction qui reçoit chaque mot) au lieu de trois.
  - Les coefficients et les deux dernières entrées de `highPassStage` en variables locales.
  - Les paquets faits main de TRX-XT et TRX-CB décrits dans une table (type, nombre de mots, mots à 0).
- Non appliqué, noté pour plus tard :
  - Un compte à rebours partagé avec TRX-XT : les blocs qui suivent le fondu diffèrent, et la fonction ne rendrait
    qu'un numéro.
  - La table de dispatch par type : six cas maintenant, à faire avec la prochaine machine.
  - Des adresses nommées pour `highPassStage`, et une probe par bloc plutôt que par échantillon, pour toutes les
    machines à la fois.

Coût mesuré (release, médiane de 9 passages, par voix et par bloc de 32 échantillons) : TRX-CB 0,64 µs, soit 1,42 %
d'un cœur pour 16 voix. TRX-CP passe de 0,77 à 0,72 µs avec `highPassStage`.

## TRX-CH

TRX-CH (type 23 : init $10201F, update $102034, render $10204D) est porté dans `mdVoiceTrxCh.cpp`. Chaque bloc, le
render prend un bruit de Fibonacci (Y:$13-$14) multiplié par Y:$05, et y ajoute six ondes. Chacune est lue dans son
propre tampon modulo, à son gain : Y:$08, puis Y:$0B à Y:$0F, que l'update recopie depuis Y:$08. Les tampons sont en
X:$102600 ($72 mots), $102700 ($66), $102780 ($56), $102800 ($50), $102880 ($3A) et $1028C0 ($34) ; les positions
sont en Y:$21-$26. Le tout passe ensuite par un filtre à deux pôles appliqué deux fois (Y:$03, Y:$04), comme ceux de
TRX-CP, puis par deux biquads passe-haut (Y:$06 et Y:$07, puis Y:$11 et Y:$12, la boucle DSP en $10215E et $1021A1).
La sortie en prend la moitié, au niveau Y:$10. Un Hit y ajoute $6CCCCD (les autres TRX ajoutent $50C335). Le niveau
reste fixe tant que Y:$09 décompte, descend de Y:$01 à chaque échantillon tant que Y:$0A décompte, puis décroît par
Y:$02.

Les six ondes ne sont pas contiguës dans le firmware : `TrxTables::ChWaves` donne leur disposition, et le test les
copie et les vérifie une à une, par une plage dans `copy` et `checkTable`.

Le paquet de l'OS fait 9 à 11 mots (Y:$01-$0A). Y:$11 et Y:$12, le troisième filtre, restent donc à $30 et 0 depuis
l'init. Y:$09 vaut au plus 1, et la rampe ne peut pas passer sous -1 : son pas vaut au plus $126.

`mdVoicePortTest` le joue avec les paquets de l'OS : 126 623 échantillons non nuls, 0 échantillon et 0 mot d'état
différents, les quatre probes atteintes. Les treize mutations sont prises. Douze le sont par les échantillons ; la
treizième (Y:$09 décompté de 2) l'est par la probe de la rampe, que la voix n'atteint plus. Le scénario des bancs (16
voix aux réglages par défaut) a aussi été comparé à l'oracle : 3000 blocs, 0 différence.

Revues du 2026-11-03 :
- Revue de code : aucun écart avec le listing. Elle doutait de la rampe : le DSP limite le niveau comme le port
  (`move b,y1`, l'accumulateur entier), et la rampe ne descend de toute façon pas sous -1.
- Revue d'architecture : le découpage tient. Le commentaire du biquad donne l'adresse de sa boucle DSP, pour que TRX-OH
  et TRX-CY puissent dire s'ils la reprennent ; la note sur $6CCCCD passe de `hitLevel` à l'appel.
- `/simplify` :
  - Le bruit et les six ondes en un seul passage par échantillon, au lieu de sept passages sur le bloc.
  - La décroissance sans accumulateur redondant ; une seule expression de sortie pour les trois phases.
  - `biquad` renommé `highPassBiquad`.
  - `copy` et `checkTable` du test prennent une plage, au lieu de deux boucles propres à TRX-CH.
- Non appliqué, noté pour plus tard :
  - Le bloc « deux pôles et son état » partagé (quatre copies : TRX-SD, TRX-CP deux fois, TRX-CH). Chaque copie range
    les mots de scratch et d'état dans son propre ordre, et la fonction aurait besoin d'options d'ordre.
  - `highPassBiquad` dans `mdVoiceTrx.h` dès que TRX-CY reprend la même boucle (TRX-OH joue le code de TRX-CH lui-même).
  - Les deux biquads en un seul passage, et des pôles gardés d'un bloc à l'autre.
  - La table de dispatch, avec la prochaine machine, après avoir vérifié que les types qui restent se suivent.

Coût mesuré (release, médiane de 9 passages, par voix et par bloc de 32 échantillons) : TRX-CH 0,96 µs, soit 2,11 %
d'un cœur pour 16 voix.

## TRX-OH

Le code de TRX-OH (type 24 : init $1021F4, update $102209, render $102222) est celui de TRX-CH, mot pour mot. Sur les
469 mots, seuls 14 diffèrent, et ce sont les adresses de fin de ses boucles `DO`, puisque le code est ailleurs. Il lit
les mêmes ondes en X:$102600 ; seuls les paquets de l'OS le distinguent de TRX-CH. `trx::routines` donne donc au type
24 les routines de TRX-CH. Les deux types diffèrent : une voix qui passe de l'un à l'autre lance l'init, comme sur le
DSP.

`mdVoicePortTest` le joue avec les paquets de l'OS : 141 312 échantillons non nuls, 0 échantillon et 0 mot d'état
différents. Donner au type 24 les routines de TRX-CB fait échouer le test au bloc 154. Avec une machine de plus dans le
tirage, la fin de l'onde de TRX-SD (128 blocs sans autre Hit) n'était plus atteinte. La première phase du test passe
donc de 4000 à 6000 blocs, et le test dure 4,6 s, loin de son délai de 120 s.

Revue du 2026-11-03 (code et architecture ensemble, le changement tenant en quelques lignes) : rien à corriger dans le
code ; les listes des machines portées (`mdVoice.h`, `CLAUDE.md`) complétées. `/simplify` : rien à simplifier.

## TRX-CY

TRX-CY (type 25 : init $1023C9, update $1023EB, render $1023FA) est porté dans `mdVoiceTrxCy.cpp`. Son code est à lui,
mais il est fait de trois briques déjà portées. Chaque bloc, le render additionne les six ondes de TRX-CH, sans son
bruit, à ses propres gains (Y:$02 à Y:$07), ses positions en Y:$15-$1A. Il les fait passer par deux voies :
- les deux passe-haut de TRX-CP par Y:$11 et Y:$0C, puis le filtre deux pôles appliqué deux fois par Y:$08 et Y:$0E ;
- les mêmes passe-haut par Y:$09 et Y:$0F, puis les deux pôles par Y:$0A et Y:$10.

Chaque filtre deux pôles prend la sortie des passe-haut deux échantillons en retard. Chaque voie est multipliée par son
niveau (Y:$13, Y:$14), qu'un Hit relève de $50C335 et qui décroît d'un bloc à l'autre par Y:$0D et Y:$01. La sortie est
la moitié de leur somme, pondérée par Y:$0B et Y:$12. L'update recopie Y:$0C dans Y:$0E, Y:$0F et Y:$10.

Le paquet de l'OS fait 14 mots (Y:$01-$0D). Y:$11 ($680) et Y:$12 ($400000) ne viennent donc que de l'init.

`mdVoicePortTest` le joue avec les paquets de l'OS : 152 710 échantillons non nuls, 0 échantillon et 0 mot d'état
différents, les deux probes atteintes (les deux niveaux limités à un Hit). Onze mutations sur douze sont prises. La
douzième écrit a1 au lieu de g dans Y:$01 de la mémoire partagée. Elle est équivalente en pratique : le test compare
les blocs d'état de chaque voix, pas la mémoire partagée, et toute routine qui lit Y:$01 l'écrit d'abord. Le
scénario des bancs a aussi été comparé à l'oracle pour TRX-CY, TRX-CH et TRX-CP : 3000 blocs, 0 différence.

Revues du 2026-11-03 :
- Revue de code : aucun écart avec le listing.
- Revue d'architecture :
  - Les ondes de TRX-CH passent dans `addChWaves` (`mdVoiceTrx.h`), que TRX-CH et TRX-CY appellent ; TRX-CY remplit
    son tampon de zéros avant, ce qui vaut la première onde du DSP, stockée sans addition.
  - Les deux blocs deux pôles de TRX-CP rangent leurs mots comme ceux de TRX-CY : `twoPoleStage` passe dans
    `mdVoiceTrx.h`, et TRX-CP l'appelle deux fois. TRX-CH et TRX-SD gardent le leur, rangé autrement.
- `/simplify` : le niveau de chaque voie et la sortie en un seul passage ; les commentaires remis à 120 colonnes.
- Non appliqué : `addChWaves` onde par onde avec le retour au début une fois par bloc (gain non mesuré).

Coût mesuré (release, médiane de 9 passages, par voix et par bloc de 32 échantillons) : TRX-CY 1,12 µs, soit 2,47 %
d'un cœur pour 16 voix ; TRX-CH 0,83 µs et TRX-CP 0,74 µs après le passage à `addChWaves` et `twoPoleStage`.

## TRX-MA

TRX-MA (type 26 : init $1028F4, update $102914, render $10291F) est porté dans `mdVoiceTrxMa.cpp`. Le bruit de TRX-CH
passe par trois étages passe-haut de TRX-CP. Le premier (Y:$0A, Y:$10) garde le haut du bruit. Le bruit commande aussi
un suiveur de niveau, lu à l'envers dans le bloc : un mot au-delà du seuil Y:$06 relance une montée vers sa taille, puis
le niveau retombe par Y:$0C. Le deuxième étage (Y:$07, Y:$08) filtre ce suiveur, et le huitième du résultat passe par le
troisième (Y:$09, Y:$0F). La sortie du premier étage s'y ajoute à un niveau qui monte de Y:$0B par bloc depuis le Hit.
L'enveloppe du Hit monte pendant Y:$01 blocs, tient pendant Y:$03, descend pendant Y:$04, puis la voix se tait. La chaîne
tourne même muette : l'update ne remet ni le bruit ni les filtres à zéro.

Le DSP a ici un défaut qu'il faut garder. L'historique d'entrée du deuxième étage tombe sur les deux premiers mots du
suiveur, X:$22-$23, et l'étage lit donc ses 32 mots de X:$24 à X:$43. Le suiveur s'arrête à X:$41 : X:$42 et X:$43
gardent ce que la dernière voix y a écrit, TRX-MA ou une autre. La mémoire partagée compte donc pour le son de cette
machine. Le port lit la même mémoire, et le test le prouve : mettre ces deux mots à zéro fait échouer `mdVoicePortTest`.

`mdVoicePortTest` le joue avec les paquets de l'OS et trois paquets faits main : 196 105 échantillons non nuls, 0
échantillon et 0 mot d'état différents, les six probes atteintes. Les paquets de l'OS ne vont que rarement au bout de
l'enveloppe, jusqu'à 519 blocs quand un Hit arrive tous les 32 blocs en moyenne : deux paquets la raccourcissent. Le
troisième met le seuil et le bruit à zéro sur une voix déjà TRX-MA, ce qui donne un mot exactement au seuil. Vingt-quatre
mutations sur vingt-cinq sont prises. La dernière remplace `a <= b` par `a < b` dans le suiveur : il faudrait une
égalité exacte sur 48 bits, que le scénario n'atteint pas. La condition a été vérifiée à la lecture du listing. Le
scénario des bancs a aussi été comparé à l'oracle : 3000 blocs, 0 différence.

Revues du 2026-11-04 :
- Revue de code : aucun écart avec le listing.
- Revue d'architecture : `countDown` passe dans `mdVoiceTrx.h`, et TRX-CH et TRX-CP l'appellent aussi ; le commentaire
  de `highPassStage` ne liste plus les machines. Rejeté : fusionner le `ramp` de TRX-MA et celui de TRX-CH (ordre du pas,
  décalage et limitation diffèrent), mettre les adresses de `highPassStage` dans une structure.
- `/simplify` : `follow` a un niveau d'imbrication de moins ; les écritures de X:$66-$67, que le deuxième étage écrase
  sans qu'on les lise, sont retirées ; `hooks()` passe en ligne, car le suiveur appelle une probe à chaque échantillon.
- À trancher après CL et S2 : une enveloppe montée, tenue, descente commune, si l'une d'elles la reprend.

Coût mesuré (release, médiane de 9 passages, par voix et par bloc de 32 échantillons) : TRX-MA 0,93 à 0,99 µs, soit 2,04
à 2,19 % d'un cœur pour 16 voix.

Reste ouvert pour les TRX :
- TRX-CL, XC, B2 et S2.
- La formule des tables de filtre.
