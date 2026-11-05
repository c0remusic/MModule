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

- Bit-exact contre `mdEngine` à 44,1 kHz (Q1) sur chaque machine portée, Hits et réglages variés. Pour les ROM, le render
  sur les filtres du DSP ; le produit joue nos filtres, mesurés et à écouter (décision du 2026-11-05, en fin de ticket).
- « Sonne pareil » 44,1/48.
- Coût par voix mesuré, comparé aux ~0,07-0,3 % estimés.

Mesuré le 2026-11-05 pour « Sonne pareil » sous une lecture : voir la section du même jour, en fin de ticket.

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

## TRX-CL

TRX-CL (type 27 : init $102AA9, update $102AB3, render $102ABF) est porté dans `mdVoiceTrxCl.cpp`. Un Hit écrit un clic
de Y:$02 mots à la valeur Y:$08 à la place du premier sinus, qui reprend après lui à la hauteur Y:$03. Le second sinus,
à la hauteur Y:$04, passe par le niveau Y:$07. Le huitième de leur somme suit le niveau Y:$09, qu'un Hit monte et que
Y:$01 fait décroître, puis un étage passe-haut de TRX-CP (Y:$05, Y:$06). La sortie part des deux mots d'historique de
l'étage, donc deux échantillons plus tard.

Deux comportements du DSP sont gardés. Le bloc qui finit le clic avance le premier sinus du nombre de mots restants, pas
de Y:$03. Et la boucle des sinus charge chaque mot lu dans X0, qui est le mot bas du pas : à partir du deuxième
échantillon, le mot bas du pas est le dernier mot du sinus. TRX-XT fait de même, et sa boucle était déjà dans
`mdVoiceTrxXt.cpp` : elle passe dans `mdVoiceTrx.h` sous le nom `sinesFedBack`, que les deux appellent.

`mdVoicePortTest` le joue avec les paquets de l'OS et un paquet fait main : 148 893 échantillons non nuls, 0 échantillon
et 0 mot d'état différents, les cinq probes atteintes. Le paquet fait main met Y:$02 à 32 : le clic finit avec le bloc,
et le DO qui suit a zéro mot. Vingt-quatre mutations sur vingt-cinq sont prises. La dernière déplace le mot d'historique
X:$00-$01 du premier filtre vers X:$70 : aucune voix portée ne relit ces mots, le changement ne s'entend pas. Le scénario des
bancs a aussi été comparé à l'oracle : 3000 blocs, 0 différence.

Revues du 2026-11-04 :
- Revue de code : aucun écart avec le listing. Le port compare Y:$02 sans signe et le DSP avec signe : un mot au bit 23
  lancerait un DO de plusieurs millions de mots sur le DSP. L'OS n'en donne pas ($20B au plus dans le scénario), le port
  ne reproduit pas ce blocage.
- Revue d'architecture : `sinesFedBack` partagée avec TRX-XT, voir plus haut. Rejeté : un cœur commun aux trois boucles
  de sinus (le mot lu change le pas après l'addition, ce qu'un `Store` ne peut pas faire), un décalage sur `envelope`,
  qui n'aurait qu'un appelant.
- `/simplify` : `sinesFedBack` passe par `acc` et `hi` au lieu d'un masque écrit à la main ; TRX-XT lui donne le pas
  directement, sans variable ni `wrap56`, qui ne changeait rien sous `add48`. Gardés : les trois branches du clic et les
  deux passes de mise à niveau, qui suivent le listing.

Coût mesuré (release, médiane de 9 passages, par voix et par bloc de 32 échantillons) : TRX-CL 0,38 à 0,43 µs, soit 0,83 à 0,94 % d'un cœur pour 16 voix.

## TRX-XC

TRX-XC (type 28 : init $102B87, update $102B88, render $102B96) est porté dans `mdVoiceTrxXc.cpp`. Sa routine est celle
de TRX-XT : un script a comparé les mots du listing, l'init et l'update sont identiques, et le rendu après les blocs du
Hit aussi, sauf les trois adresses de fin de DO. Seuls les deuxième et troisième blocs après un Hit changent : un bloc
silencieux, puis une forme d'onde de 32 mots à P:$102C0F, là où TRX-XT joue ses deux formes d'onde. `xc::render` prend
donc ces deux blocs et passe tout le reste à `xt::render` ; le dispatch prend l'init et l'update de TRX-XT.

`mdVoicePortTest` le joue avec les paquets de l'OS : 120 689 échantillons non nuls, 0 échantillon et 0 mot d'état
différents, les deux probes atteintes, 118 blocs silencieux et 112 formes d'onde. Six mutations sur six sont prises. Le
scénario des bancs a aussi été comparé à l'oracle : 3000 blocs, 0 différence.

Revues du 2026-11-04 :
- Revue de code : aucun écart avec le listing, pour toute valeur de Y:$0B sur 24 bits.
- Revue d'architecture : une première version sortait `xt::sweep` et réécrivait le compteur de TRX-XT et TRX-CB. La
  délégation à `xt::render` retire `sweep`, la probe de fondu et ces deux réécritures. Le fondu de TRX-XC passe par la
  probe de TRX-XT et reste comparé au DSP. Rejeté : un rendu commun avec drapeau ou table, un helper pour le compteur
  à trois voies, un helper pour les deux lignes de forme d'onde.
- `/simplify` : rien à changer. La table de dispatch, repoussée machine après machine, est abandonnée : il ne reste que
  deux machines, et le `switch` porte le commentaire de TRX-OH.

Coût mesuré (release, médiane de 9 passages, par voix et par bloc de 32 échantillons) : TRX-XC 0,22 à 0,23 µs, soit 0,49 à 0,51 % d'un cœur pour 16 voix.

## TRX-B2

TRX-B2 (type 29 : init $102C31, update $102C4A, render $102C5A) est porté dans `mdVoiceTrxB2.cpp`. Une phase de 23 bits
avance de Y:$01 plus une enveloppe de hauteur que Y:$0D fait décroître. Elle lit le sinus à travers une forme : chaque
quart de phase est plié par son propre mot (Y:$05 à Y:$08), et MAX tient le résultat à -$1FFF. Le signal perd ses bits
bas (Y:$09, après ajout de Y:$0A), puis passe par une enveloppe qui monte de Y:$10 jusqu'à un demi et descend à un taux
qui change quand un niveau plus lent (Y:$18) est épuisé. S'y ajoutent un bruit creux, un mot sur quatre, et un clic qui
décroît de 0,9 par échantillon.

L'adresse du sinus passe par l'adressage modulo du DSP, M2 = $7FFF, et ce n'est pas un index masqué : l'offset revient
une fois au plus dans la table, et pas du tout s'il est multiple de la période. Avec Y:$08 à 0, le dernier quart donne
l'offset $8000 et lit X:$150000, le premier mot de la banque de samples. Le port lit la même mémoire : `external` passe
de `mdVoiceRom.cpp` à `mdVoiceInternal.h`, et rend 0 ailleurs que dans ce que le port détient. Le test y lit la mémoire
de l'oracle ; hors de la banque, le port et le DSP diffèrent donc en produit. Dans les 6000 blocs de paquets de l'OS,
aucun offset ne sort de la table et aucun ne revient : ces deux cas ne viennent que des paquets faits main.

`mdVoicePortTest` le joue avec les paquets de l'OS et deux paquets faits main, pour les offsets qui reviennent par le bas
et par le haut, et celui d'une période : 193 482 échantillons non nuls, 0 échantillon et 0 mot d'état différents, les
huit probes atteintes. Quarante-six mutations ont été essayées et sept survivent. Deux ne changent rien : `r > $8000`
après le test du multiple, et le bit 0 de B0 que `asl` ne ramène pas dans A1. Les cinq autres visent une égalité exacte
ou un seul bit bas : l'offset -1, le plancher $FFE000 au lieu de $FFE001, les deux seuils à un LSB, et 1 au lieu de 0
dans Y:$10. Refaites plus fortes (seuils à $247AE et $440000, plancher à $FFC001, `acc(1)`), quatre d'entre elles sont
prises ; l'offset -1 a été vérifié à la lecture de `agu.h`. Les branches réécrites par `/simplify` ont été remutées :
huit sur huit prises. Le scénario des bancs a aussi été comparé à l'oracle : 3000 blocs, 0 différence.

Le scénario change avec cette machine. Chaque machine ajoutée rebat les tirages, et la queue de TRX-SD, qui demande 128
blocs sans Hit, est tombée à 0. Les 200 derniers blocs de l'OS frappent donc chaque voix une fois sur une TRX, puis
plus rien : le cas ne dépend plus du tirage. La phase des paquets faits main passe de 1000 à 2000 blocs, car « sample
raccourci sous la voix » est aussi tombé à 0.

Revues du 2026-11-04 :
- Revue de code : aucun écart avec le listing. Hors listing, l'interpréteur compare `r` sans signe et le JIT avec signe
  pour un offset sous -$148000 ; il faut Y:$05 positif au-delà de $520000, que l'OS ne donne pas.
- Revue d'architecture : le balayage remplace une première version à silences réguliers, qui coûtait 20 % des Hits et
  restait un tirage. `external` garde son test de `tables` pour une voix TRX sans les tables ROM. Rejeté : un bruit
  partagé avec TRX-BD (les trous ne sont pas les mêmes), un changement d'`envelope`.
- `/simplify` : `sineAt` passe à un seul test sur le chemin courant ; `shl` remplace le décalage écrit à la main ;
  l'init ne touche plus les mots que l'update écrase ; plus de variable locale pour Y:$10. Le fichier garde la phase en
  X:$40-$5F avant que le signal l'écrase, comme le DSP.

Coût mesuré (release, médiane de 9 passages, par voix et par bloc de 32 échantillons) : TRX-B2 0,44 à 0,45 µs, soit 0,96 à 0,98 % d'un cœur pour 16 voix.

## TRX-S2

TRX-S2 (type 30 : init $102D06, update $102D31, render $102D49) est porté dans `mdVoiceTrxS2.cpp`. Le premier bloc après
un Hit ne calcule qu'une rampe : X:$A0-$BF gardent ce que la dernière voix y a laissé, et ces mots passent quand même à
la sortie, comme sur le DSP. Ensuite, chaque bloc joue deux sinus dont la hauteur monte pendant Y:$34 blocs puis décroît,
et une rafale : un niveau que les mots de bruit au-delà de Y:$12 relèvent, mis à l'échelle par le bruit, tenu deux
échantillons sur deux, passé dans deux passe-haut et deux deux-pôles. La rafale monte pendant Y:$3C blocs puis décroît.
L'update ne fait pas monter Y:$35 comme les autres machines : il l'écrit. Le bruit range ses deux mots d'état inversés
à chaque bloc.

Deux écarts de lecture ont été pris par le test. `twoPoleStage` rangeait les deux derniers mots de sortie dans l'autre
ordre : aucun échantillon ne changeait, seul le mot d'état Y:$31 différait. TRX-CH les range comme TRX-S2 : le paramètre
`_oldestFirst` remplace sa copie écrite à la main. Et les paquets de l'OS mettent Y:$0E et Y:$0F à 0 : une mutation qui
confondait ces deux mots passait. Un paquet fait main les rend aléatoires, et elle est prise.

`mdVoicePortTest` le joue avec les paquets de l'OS et ce paquet fait main : 114 354 échantillons non nuls, 0 échantillon
et 0 mot d'état différents, les sept probes atteintes. Cinquante et une mutations ont été essayées, toutes prises. Le
scénario des bancs a aussi été comparé à l'oracle : 3000 blocs, 0 différence.

Revues du 2026-11-04 :
- Revue de code : aucun écart avec le listing, et l'appel de TRX-CH à `twoPoleStage` fait ce que faisait sa copie.
- Revue d'architecture : l'enveloppe montée, tenue, descente commune est abandonnée. Ni TRX-CL ni TRX-S2 ne reprennent
  celle de TRX-MA. Rejeté : un morceau partagé avec les sinus de TRX-SD (une montée pour deux sinus là-bas, une par sinus
  ici), deux fonctions pour `twoPoleStage`. `sine` prend le numéro du sinus au lieu de six numéros de mots.
- `/simplify` : les deux boucles de la rafale fusionnent, 16 produits en moins par bloc, car les mots impairs étaient
  écrasés sans être lus. Non fait : retirer la branche sur le bruit, que le prédicteur ne peut pas apprendre, parce que
  la probe devrait en sortir, pour un gain que personne n'a mesuré. Hors périmètre : `using Block` et `using probe`,
  répétés dans treize fichiers, qui pourraient passer dans `mdVoiceTrx.h`.

Coût mesuré (release, médiane de 9 passages, par voix et par bloc de 32 échantillons) : TRX-S2 0,94 à 0,97 µs, soit 2,06
à 2,13 % d'un cœur pour 16 voix.

Toutes les TRX sont portées. Reste ouvert :
- La formule des tables de filtre.

## 2026-11-05 : 44,1 et 48 kHz

Le port ne rend qu'à 44,1 kHz : il n'a aucun paramètre de fréquence. Le critère ne peut donc se mesurer que sur le chemin
réel de MoDule à 48 kHz, le rendu à 44,1 kHz puis le rééchantillonneur Legacy de la pile (libresample). Le ticket
« Une fréquence commune » laisse ouverte la décision pour le MD (« Le MD et le DT2 se décident à leur tour ») : ce résultat
ne vaut pas pour un moteur adapté à 48 kHz, qui n'existe pas.

Mesuré en Q15 sur les 17 machines synthétiques portées alors et ROM-01 à -32 : un Hit de 2 s aux valeurs par défaut et à
trois réglages tirés au hasard, 196 Hits, voix brute avant le mixeur, paquets de l'OS.

| Mesure, pire cas | Résultat | Seuil |
|---|---|---|
| Enveloppe sous 18 kHz | 0,118 dB (TRX-MA) | 0,2 dB |
| Tiers d'octave sous 10 kHz | 0,349 dB (TRX-CH) | 0,5 dB |
| Tiers d'octave 10-18 kHz, non jugés | 0,996 dB (TRX-SD) | aucun |

Les 196 Hits passent. Le témoin, les mêmes échantillons joués à 48 kHz sans rééchantillonnage, sort des seuils sur les
196 Hits : la mesure voit donc l'écart de 8,8 %.

Deux pièges de mesure :
- Le délai estimé sur les 0,3 premières secondes tombait sur du silence pour un START tardif (ROM-32) : il part maintenant
  du premier échantillon qui sonne.
- `bands` calcule 2^20 points à chaque fréquence, donc deux grilles. ROM-27 tient un ton à 27,8 Hz sur un bord de bande, qui
  tombait de part et d'autre : 0,61 dB à 31 Hz, contre 0,004 dB sur une grille commune de 1/24 Hz. La mesure retenue
  utilise cette grille commune. `stFastMetrics.h` garde le même défaut pour Q15.

Cette mesure ne remplace pas l'écoute, qui reste à faire. Les outils (`ratecmp`) sont restés hors du dépôt.

## 2026-11-05 : le côté OS sans Musashi, et les tables par formule

Une voix ne demande plus Musashi. `mdVoiceCoefficients.*` porte les fonctions de coefficients de l'OS et son lissage en
C++, par piste : `coefficients` donne le paquet d'une machine, `Smoother` lisse les 24 paramètres d'une piste. Le port
couvre 22 fonctions pour 70 machines : GND--, GND-IM, les 14 TRX, EFM-BD à -CB et les 48 ROM, soit toutes les machines
dont le render est porté. Ni le rendu ni l'initialisation de ce code ne lisent la flash ou l'OS : `mdVoice` ne lie pas
`mdEngine`, et `OsTables` calcule ses tables. L'arithmétique suit le listing 68k, divisions du compilateur comprises,
pour qu'un mot se vérifie ligne à ligne.

Les tables, chacune égale mot à mot à celle du firmware, sans qu'aucun mot du firmware n'entre dans le dépôt :

| Table | Mots | Formule |
|---|---|---|
| Filtres, angles, X:$140100 | 4096 | `round(2^23 · r9(cos(w) / √2))` |
| Filtres, rayons, X:$141100 | 4096 | `round(2^23 · r9(exp(-w) / √2))` |
| Filtres, décalages, X:$140000 et $140080 | 128 + 128 | `round(4096 / ln(10^6) · ln(x))`, x = `√(1 - b²)` puis `b` |
| Hauteur, $24BD94 | 128 | `round(2048 · 2^(i / 127,5))` (déjà trouvée) |
| Décroissance, $24BF94 | 512 | `round(2^23 · exp(-0,29643787 · exp(-12 i / 511)))` |
| Montée, $24C794 | 512 | `round((2^23 - 1) · exp(-517,918697 · exp(-16 i / 511)))` |
| Octaves, $24D794 | 128 | `round(32767 · ln(1 + i))` |

Pour les filtres, `w = 2π · 0,02 · 10^(6 i / 4096) / 44100` : une échelle de 4096 pas de 0,02 Hz à 20 kHz à 44,1 kHz, et
`b = 1 / (1,02 + 5,98 q / 127)`, soit un Q de 0,51 à 3,5 sur 128 pas. Les pôles valent `exp(-σ ± jt)` : les deux tables
se lisent à deux index, ce qui donne σ et t pour une coupure et un Q. `r9` arrondit à 9 décimales : les tables ont été
écrites en décimal avant de devenir des mots de 24 bits. Sans cet arrondi, 16 mots diffèrent d'un LSB, tous à moins de
0,005 LSB d'un demi. Les constantes 0,29643787 et 517,918697 sont ajustées : tout l'intervalle admis tient dans
[0,29643786811 ; 0,29643787573] et [517,91869501 ; 517,91869949]. Aucune signification physique n'a été trouvée pour
elles. Comme au ticket 07, une autre libm pourrait arrondir un mot à 1 LSB près : l'exactitude n'est vérifiée qu'avec le
runtime de MSVC.

Ce qui est prouvé par `mdVoiceCoefficientsTest`, contre le code 68k de l'OS dans Musashi :
- Les quatre tables de l'OS mot à mot. Les tables de filtre passent à `TrxTables`, et `mdVoicePortTest` les compare à la
  mémoire du DSP, sans plus les copier de la flash.
- 3 721 728 paquets mot à mot, 0 différence : chaque paramètre de chaque fonction sur toute la plage de l'OS, 0 à $3FFF,
  les sept autres au hasard, puis 20 000 jeux au hasard par machine, et 2 000 pour chaque ROM, trigger ou non. Le tempo
  est tiré entre 720 et 7200 pour les ROM, seules à le lire, et 20 000 paquets sont pris à chaque bout.
- 16 voix sans `MachineRunner` : 6000 blocs, 1507 Hits, 6036 knobs, les changements de machine et le tempo, contre
  `HostModel` jouant `VoiceEngine`, le chemin de MoDule. Paramètres, échantillons et état Y:S sont identiques à chaque
  bloc, et toute machine dont le sample existe a sonné.

`mdVoicePortTest` reste à 0 différence, tables de filtre comprises.

Mutations : 899 sur `mdVoiceCoefficients.cpp`, chaque constante, opérateur et comparaison, comparées au port dans un
harnais à part, avec le même balayage ; le port est lui-même prouvé égal à l'OS. 854 sont prises. Les 45 restantes ne
changent aucun résultat sur la plage de l'OS :
- 25 ajoutent 1 à un multiplicateur ou un dividende de division par constante, ce qui ne change aucun quotient. Le
  balayage complet du paramètre le prouve, sauf pour le dividende du tempo, où `(N + 1) / t` ne diffère que si `t` divise
  `N + 1`.
- 9 déplacent d'un cran un seuil entre deux branches qui se rejoignent.
- 6 touchent un bit de masque toujours nul.
- 3 changent le `return 0` des machines qui ne calculent qu'au trigger, que `std::max(count, 1)` ramène à 1.
- 2 touchent une boucle de table sans effet (l'index 0 d'`octaves` vaut 0, et l'écriture de trop de `pitch` est écrasée).

Sur 17 mutations du `Smoother` et de `makeFilters`, dans les vrais tests, 16 étaient prises. La dernière retirait
l'écriture de la cible dans `snap`, invisible dans le scénario ; `checkSnap` la prend désormais.

Coût mesuré (release, médiane de 9 passages, machine chargée par d'autres sessions) : 19 à 24 ns par paquet, contre 2,1 à
4,8 µs dans Musashi, et 6 à 12 ns par lissage d'une piste. À 125,3 ticks par seconde, c'est 0,007 % d'un cœur pour 16
voix, contre 0,43 à 0,97 % pour les seuls paquets dans Musashi. La mesure est faite à chaud, sur le chemin du tick, sans
trigger : elle ne dit pas le coût d'un Hit sur un fil audio froid. `makeFilters` prend 220 µs, une fois par `TrxTables`.

Revues du 2026-11-05 :
- Revue de code : aucun bug. Un commentaire faux sur un produit qui passerait 2^31 : il reste sous 1,2·10^9, et le mot
  9 des ROM perd ses conversions signées.
- Revue d'architecture : l'interface convient au ticket 11. Appliqué : les cibles écrites aux événements et non à chaque
  tick, un seul aiguillage `function()` au lieu de deux listes, `kBlocksPerTick` côté produit, les commentaires du
  `Smoother` et de la plage. Noté au ticket 11 : la reprise de `Track` dans le produit, et le chemin MID/CTR.
- `/simplify`, quatre angles : `scaled` retiré au profit de l'écriture du listing ; `efmDecay`, `efmIndex` et `s2Sweep`
  retirent des répétitions et l'écrasement du mot 3 de EFM-CP ; `rom<First>` et des fonctions nommées remplacent les
  lambdas ; le test perd `FunctionCheck`, `updateVoices`, sa seconde boucle de médiane et sa seconde vérification des
  filtres, et vérifie que `coefficientsPorted` et `Voice::ported` donnent les mêmes machines. Efficacité : rien à
  changer.
- Non appliqué :
  - `mdVoiceTestTables.h`, partagé par les deux tests : 45 lignes dupliquées, à extraire quand la session du ticket 09
    aura fusionné, ou à disparaître dans le chargeur de formes d'onde du ticket 11.
  - Le masque à $3FFF : il garde les index dans les tables, bien que le contrat interdise les valeurs au-dessus.
  - Les masques du listing qui n'enlèvent rien, gardés pour la lecture ligne à ligne.

Une machine dont le render arrive doit désormais apporter sa fonction : `checkMachines` échoue sinon.

## 2026-11-05 : GND-SN, GND-NS, EFM-HH et EFM-CY

La fusion avec la branche du ticket 09 (dc651e5b) a donné à `Voice::ported` quatre machines sans fonction, et
`checkMachines` a échoué comme prévu : « machine 1 has a render or a function, not both ». Leurs fonctions ($201130,
$201194, $2026F8, $20280C) sont portées. Le port couvre maintenant 26 fonctions pour 74 machines, toutes celles qui ont
un render. Aucune ne demande de table nouvelle. EFM-HH reprend `efmHead`. EFM-CY a sa propre disposition : ses mots 2 à
4 sont des multiples de la hauteur (361, 461, 691), et ses mots 7 à 9 ces multiples à l'index.

`mdVoiceCoefficientsTest` : 4 326 016 paquets mot à mot contre Musashi, 0 différence, 74 machines, et 6000 blocs de 16
voix sans `MachineRunner`, identiques à `HostModel` sur `VoiceEngine`, chaque machine sonnante. Mutations : les 103
nouvelles sur ces quatre fonctions sont toutes prises, soit 957 sur 1002 pour tout le fichier ; les 45 restantes sont
celles déjà classées plus haut.

Coût : 18 ns par paquet, contre 2,0 µs dans Musashi, à 11 ns le lissage d'une piste, soit 0,006 % d'un cœur pour 16
voix, mesuré à chaud sur le chemin du tick.

Revues du 2026-11-05 : revue de code, aucun écart avec le listing. Revue d'architecture : les quatre tiennent dans
l'aiguillage ; une boucle sur les rapports de EFM-CY, qui cachait les numéros de mots, est déroulée, pour que chaque mot
se vérifie contre le listing, et le commentaire de `efmHead` ne compte plus les machines. `/simplify`, quatre angles :
aucun constat.

## 2026-11-05 : les tables des ROM

La table de décodage (Y:$146000, 4096 mots) a sa formule : c'est l'expansion de la loi A autour du code 2048, pour
A = 36,856. Avec `s = 1 + ln A` et `y = |code - 2048| / 2048`, un mot vaut `round(±2^23 · x)`, où `x = y s / A` sous
`y = 1 / s` et `x = exp(y s - 1) / A` au-dessus. Elle se distingue d'un simple `100^(y - 1)`, dont la pente est proche, de
1,5·10^-4 au plus. `RomTables` la calcule à sa construction, et `mdVoicePortTest` la compare mot à mot à la mémoire du
DSP : 4096 sur 4096, 0 différence, et le test passe sans la copier. Treize mutations de la formule, douze prises ; la
dernière déplace le signe du code 2048, dont le mot vaut 0.

L'interpolateur (Y:$163, 1568 mots) et le décimateur (X:$244, 7 mots) n'ont pas de formule : le travail s'arrête là, et ils ne sont
pas recopiés dans le dépôt. Ce qui a été essayé :
- L'interpolateur se déplie en une seule réponse, 7 rangs de $E0 phases rangés par tronçons de 112 mots, symétrique. Ce
  n'est pas un sinus cardinal fenêtré : ses zéros tombent à 233,8, 473,0 puis 782,0 mots, soit des écarts de 239 et 309,
  alors qu'une fenêtre laisse les zéros du sinus cardinal à intervalles égaux.
- Ce ne sont pas non plus des polynômes par tronçon : du degré 5 au degré 9, l'écart reste le même, de 7 à 255 LSB.
- Deux mots sortent de la courbe (u = 559 et 783, de 0,018 environ), et les raccords de tronçons sont irréguliers.
- Le décimateur a 7 mots alternés plus un centre, sans la forme d'un demi-bande (centre à 0,31).

Tout cela ressemble à des filtres conçus numériquement, avec des outils dont les paramètres sont inconnus.

Décision d'Antoine, le 2026-11-05 : pas d'import, des filtres à nous, pour une seule chaîne ROM avec ou sans flash
image. La spécification §11.4 interdit déjà de reprendre les tables de l'OS. Les ROM du produit ne sont donc plus
bit-exactes, et seulement elles ; le render, lui, le reste : `mdVoicePortTest` lui donne les filtres du DSP.

`RomTables` calcule les siens, depuis une cible et non par ajustement sur les mots du firmware :
- Interpolateur : sinus cardinal à fenêtre de Kaiser sur 7 échantillons, coupure 0,475 du rythme d'entrée, β = 5, dans la
  disposition de la table, chaque phase normalisée au gain de l'OS (-0,62106).
- Décimateur : demi-bande de Kaiser sur 16, β = 3,5, les 7 prises impaires sommées à 0,15972, avec la prise centrale que
  le render tient en dur.

Le premier jeu, coupure 0,475 et β = 4,5, suivait mieux la bande passante mais laissait 3,5 dB d'écart sur les
samples à basse fréquence d'échantillonnage : il faut suivre aussi la transition, -8 dB à 0,5 et -19 dB à 0,6.

`mdVoiceRomFiltersTest` mesure ROM-01 à -32 sur 128 Hits de 2 s, BRR à 0, contre les mêmes Hits sur les filtres du DSP :

| Mesure, pire cas | Résultat |
|---|---|
| Enveloppe | 0,536 dB |
| Tiers d'octave jusqu'à 10 kHz, à moins de 40 dB de la bande la plus forte | 0,512 dB |
| Tiers d'octave de 10 à 18 kHz, non jugés | 0,505 dB |

L'enveloppe dépasse les 0,2 dB de la mesure 44,1/48 ; les filtres ne tiennent pas ce seuil-là. Les limites du test, 0,6
dB, sont une garde contre les régressions, fixées après la mesure : ce n'est pas un verdict sur le son, qui revient à
l'écoute. Sans plancher à -40 dB, une bande à -50 dB atteint 2,5 dB, sur des images de samples à basse fréquence. Le BRR
n'est pas jugé : il quantifie, et un pas pris un échantillon plus tôt déplace une queue de plusieurs dB (3,3 dB par tiers
d'octave, et jusqu'à une fenêtre muette d'un seul côté), quel que soit le filtre.

Treize mutations des filtres : douze prises, et la treizième, le signe du gain, l'est depuis qu'un produit scalaire
négatif fait échouer le test, les mesures étant aveugles à la polarité.

Coût : aucun au rendu ; c'est le même calcul sur d'autres mots.

Revue du 2026-11-05 (code, architecture et simplification ensemble, le changement tenant en quelques lignes) : rien à
corriger. Comme les autres tables, l'exactitude n'est vérifiée qu'avec le runtime de MSVC.

Reste ouvert :
- « Sonne pareil » 44,1/48 : l'écoute et la décision pour le MD à 48 kHz appartiennent à l'utilisateur. Le ticket n'est
  donc pas résolu.
- L'écoute des ROM sur nos filtres contre celles du DSP, BRR compris.
- La conversion 16 vers 12 bits de la flash reste à refaire. Il faudrait essayer la compression A de même constante, ce
  qui n'a pas été fait.
- RAM-P n'est pas porté.
- La pente des niveaux de l'OS ($100029E), la même loi que le lissage, reste dans `HostModel` : elle sert le mixeur, pas
  la voix (ticket 10). Les LFO relèvent du ticket 13 : le `const uint16_t*` de `coefficients` est leur point d'entrée.
- Ce que joue une TRX dont la forme d'onde manque, sans flash image.
