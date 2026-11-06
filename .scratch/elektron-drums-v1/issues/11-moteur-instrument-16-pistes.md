# Moteur de l'instrument : 16 pistes, familles mélangées

Type: AFK
Status: resolved
Assignee: 
Blocked by: 04, 08

## Quoi

Le moteur au cœur du plug-in, parti de ST Drums : 16 pistes, chacune une voix mono de n'importe quelle famille
(Syntakt rapide, MD porté), trigs à l'échantillon près, voix éteinte non rendue, aucun coût fixe par famille.
Changer de machine : rampe de 64 échantillons, défauts de la nouvelle, locks de l'ancienne retirés. Chokes par
cible. Notes 36-51 jouent les pistes. Sorties Main et 15 stéréo, chaque piste sur l'une d'elles. Étage AMP minimal
(niveau, pan, vélocité) en attendant la chaîne.

Noté le 2026-10-27 depuis le ticket 06 : les machines de `syntakt::fast` voient un trig au début d'un bloc de 32
échantillons et le font sonner au bloc suivant, comme la machine. Rien ne place encore un trig à l'échantillon près
dans un bloc. CY ALLOY, rendue à 48 kHz puis rééchantillonnée, place son trig sur sa propre grille de blocs à
48 kHz : à 44,1 kHz, jusqu'à 28 échantillons de l'hôte (0,66 ms) de son instant idéal, selon le bloc. Ce ticket
règle les deux, et ajoute la latence du rééchantillonneur (24 échantillons à 44,1 kHz) au délai commun des Hits.
Depuis, tout le Syntakt rend à 48 kHz par `fast::Host` ; `stFastResampleTest` mesure ce placement sur un cycle de
147 blocs (de −0,75 à +28,4 échantillons, gigue 0,66 ms).

Noté le 2026-10-30 depuis la mesure de coût mixte du ticket 06 (`stFastMixCost`, Ryzen 7 3700X) : 16 pistes sur les
neuf machines au pire cas coûtent 11,3 à 11,6 % d'un cœur à 44,1 kHz quand les instances (une par machine utilisée, voie t
= piste t) sont sommées voie par voie puis rééchantillonnées une seule fois ; un `Host` par machine, soit neuf
rééchantillonneurs, coûte 15,7 à 16,7 %. Contrainte : un rééchantillonneur par famille, après la somme des pistes, jamais
un par machine (`Host<M>` accepte déjà toute source de `renderBlock()`, une somme comprise). Autre fait mesuré : une
voie muette coûte presque autant qu'une voie qui sonne (toutes les machines sauf SP TWINSHOT calculent leurs 16
voies en SIMD), si bien que le coût suit le nombre de machines utilisées, pas de pistes : 6 à 11 µs par machine et
par bloc. « Voix éteinte non rendue » demande donc des instances plus étroites (4 voies pour une machine sur 4 pistes
ou moins) ou le saut d'une instance muette ; au pire cas, 8 machines portent une piste chacune, 65 des 71 µs.

Fait le 2026-10-30 dans le ticket 06 : chaque machine de `syntakt::fast` a `setLanes(n)` (4, 8, 12 ou 16) et ne
calcule que ses voies 0 à n − 1, les autres muettes et figées (`stFastLanesTest` : n voies donnent les échantillons
de 16, bit pour bit). Ce ticket possède l'attribution piste → voie : les pistes d'une machine sur ses premières
voies, `setLanes` au multiple de 4 au-dessus, comme `stFastMixCost` le fait déjà. Une piste qui change de machine
libère sa voie : la voix figée d'une voie reprise repart où elle s'est arrêtée, d'où le changement de machine de ce
ticket (défauts, rampe) sur la voie qu'elle reçoit, et le compactage des voies restantes s'il en libère un groupe. Le
rééchantillonneur reste à 16 voies (une par piste, après la somme). La borne vit sur chaque `Block` ; si un bloc
oublié dans un `setLanes` venait à coûter, la forme sûre à la compilation est un nombre de voies passé à chaque étage
(environ 100 appels).

## Critère de fin

- Un Hit tombe à l'échantillon de sa note, gigue 0, latence mesurée.
- Une piste éteinte ne coûte rien (mesuré) ; une famille absente ne coûte rien.
- Choke et changement de machine sans clic (Q12).
- Plug-in chargé par l'hébergement VST3 de JUCE : sorties sur leurs canaux, mêmes échantillons quelle que soit la
  taille de bloc.

## Noté par le ticket 09 (2026-11-05)

- Les lignes à retard de P-I sont par voix : `Voice(_shared, _index)` prend le numéro de la Track, et la voix t de MD est
  la Track t. Une allocation de voix qui les découple change où P-I écrit.
- « Une voix éteinte n'est pas rendue » contredit la mémoire de travail partagée : des renders lisent ce qu'un autre a
  laissé en scratch (voir `Scratch` dans `mdVoice.h`), P-I-CC et P-I-RC par exemple. Sauter une voix change ce qu'entend
  la suivante. À mesurer avant de garder cette optimisation.
- Un changement de machine sans Hit n'est pas joué par `mdVoicePortTest` : ce ticket doit le couvrir.
- `Shared` regroupe tables, banques, scratch et lignes. Son découpage attend ce ticket, qui saura ce que le moteur
  partage réellement.

## 2026-11-06 : fait

`source/elektron/edDrums/` (sans JUCE) porte le moteur, `edDrums::Engine` : 16 pistes, chacune une machine MD (les 107 que l'OS
1.63 définit et que `mdVoice` joue) ou Syntakt (les 9 de `syntakt::fast`), plus `Library`, la liste des machines et leurs défauts.
`edDrumsPlugin/` est le VST3, nom de travail « Elektron Drums Dev », sans éditeur. `mdVoice/mdVoiceTrack.*` reprend le `Track` du
test de coefficients : `mdVoiceCoefficientsTest` pilote désormais ce code-là, 0 différence. `edDrumsEngineTest` et
`edDrumsVst3HostTest` entrent au garde-fou. Session à 48 kHz : le plug-in se tait et annonce une latence 0 (ticket 16).

**Latence** : un Hit sonne 82 échantillons après sa note à 44,1 kHz (1,86 ms), gigue 0, sur les 107 machines MD et les 9 Syntakt,
Hits identiques bit pour bit quelle que soit leur position dans le bloc. C'est une latence constante annoncée à l'hôte. Le MD rend
dans un anneau par piste où chaque voix commence à son échantillon ; le Syntakt prend ses voies au Hit et les rend à la fin du bloc
ou à la coupure, phase FIR par piste en 1/147 d'échantillon. `voiceStart` fait attendre les machines qui sonnent tard : ROM 8,
TRX-S2 33, TRX-BD 34, TRX-XT 38. Les attaques plus lentes (de +1 à +352 échantillons à 1 % du pic, machine par machine) sont le son
de la machine, pas de la latence, comme TRX-XT, TRX-XC et TRX-CB sur le Machinedrum.

**Coût**, minimum de 5 passes de 4 s, deux mesures, 16 pistes en Hits soutenus : 16 MD au pire cas 8,65 à 8,85 % d'un cœur, Syntakt
4,07 à 4,19 %, mixte 7,34 à 7,46 %, sans Hit 0,03 %. Le maximum de 15 % tient, et les guides aussi (moteurs ≤ 10). **Ces chiffres ne
sont pas sur machine calme** : 1,5 à 3,7 cœurs pris par d'autres processus ; à refaire avec l'acceptation (ticket 25). Pire bloc
mixte : 1 110 µs après le chemin rapide de `Ring::write`, 3 100 à 3 800 avant ; un bloc de 32 dure 726 µs, machine chargée.

**Q12** : choke à 64 échantillons -53,4 dB (MD), -52,6 dB (Syntakt) ; changement de machine couvert, y compris sans Hit. **Trois cas
restent sous le seuil de -50 dB**, tous hors du geste normal, bornés par le test et à ne pas lire comme réussis :
- Hit et choke au même échantillon : -24,3 dB (MD), -41,4 dB (Syntakt), rien après la rampe. Le son coupé n'a jamais vraiment sonné.
- Voie Syntakt reprise par un nouveau Hit quand les 16 voies d'une machine sont prises : -43,2 dB, contre -26,0 avant correction. La
  machine fait elle-même le fondu de 32 échantillons, pas 64. Borne -42.

**Voix éteinte.** Décision contre la phrase de la spécification telle qu'elle était : une voix calme est figée, et son Hit suivant
n'est donc pas celui d'un moteur qui l'aurait rendue. Mesuré à 125 et 30 BPM, sur la voix seule, en dB du signal, après
`alignTicks` : TRX -17,5 et -23,9 ; EFM -100,6 et -18,0 ; P-I -22,2 et **-5,8** ; ROM -48,9 et -35,2 ; E12 -129,2 et -119,5 ;
GND -348 et -343. Ce qui n'est jamais rendu reste sous -102 dB aux deux tempos. Sur le pire cas, P-I à 30 BPM, ni le scratch partagé
seul (-28,6) ni les ticks seuls (-12,8) n'expliquent l'écart : il reste l'état de la voix figée, par déduction, sans mesure directe.
Les bornes valent pour une graine. Spécification §3 amendée. Décision à confirmer par l'utilisateur : aucune écoute n'a jugé -5,8 dB. Fenêtres : MD 8192 blocs (5,9 s, P-I-HH se tait jusqu'à 6170 blocs),
Syntakt 512 blocs à 48 kHz (341 ms, SP TWINSHOT se tait 353 blocs sur un sample à trou). Un sample à trou plus long, ralenti, serait
coupé : ticket 20.

`Track::alignTicks` (hors brief) cale les ticks de la piste sur le Hit. Une Track de l'OS garde ses ticks à l'échelle du moteur :
un Hit dépendait donc de sa position. Avec l'alignement chaque Hit est indépendant de sa position, mais la piste ne suit plus le
calendrier de ticks de l'oracle : de -8 à -30 dB selon la famille à 125 BPM contre les ticks fixes. Choix à confirmer aussi.

La question de `Shared` (ticket 09) est close : `Engine` en possède un, les tables y sont en pointeurs lecture seule. Les voies
Syntakt se prennent au Hit, la plus basse libre, et `setLanes` suit ; il n'y a pas de compactage explicite des voies qui sonnent. Bug trouvé en revue : `Library` lisait la table des machines de l'OS au pas de 50 au
lieu de 86, et son test la comparait à elle-même. `md::fw::readMachineTable` est la seule marche, partagée avec `MachineRunner` ; le
test contrôle ids et noms de paramètres contre `mdProtocol`, 135 machines sur les 140 qu'il nomme (5 absentes de l'OS 1.63, listées).
`findFlashImage` et `FlashImageName` passent aussi dans `md::fw`, pour que `edDrums` ne lie pas MoDule. Aucune donnée du firmware dans
le dépôt : `Library` lit les défauts dans la flash à l'exécution, sans flash le plug-in se tait. `/fp:strict` sur `mdVoiceRom.cpp`,
`mdVoiceTrx.cpp` et `mdVoiceCoefficients.cpp`, qui arrondissent des doubles vers les tables de l'OS.

Écartés : banc VST3 partagé (trois copies, commit à part) ; `Load` de `edDrumsCost` ; `importRomBank` ; Machine persistée en famille
+ id (ticket 18) ; borne d'index de Lock ; flams sous 57 échantillons ; partage du travail scratch ; chaîne SIMD par voie (ticket 12).

## Références

Spécification §3, §9, §11.2. Ticket « Source de machine d'une piste ».

## Noté par le ticket 08 (2026-11-05)

`Track` et `updateVoices`, dans `mdVoiceCoefficientsTest.cpp`, sont ce que `HostModel` fait autour du code de l'OS. Ce
ticket les reprend dans le produit, et `checkVoices` doit alors piloter ce code-là : si le moteur écrit son propre `Track`,
la preuve échantillon par échantillon ne couvre plus le produit. À garder en le faisant :
- les Locks d'un Hit s'appliquent avant toute mise à jour de voix dans ce bloc (`HostModel::renderBlock`) ;
- le chemin de trigger du LFO passe avant la fonction de coefficients (`HostModel::updateVoice`) ;
- un Hit de machine MID ou CTR donne à la voix son type seul, et le DSP lui fait jouer les routines de GND-- : la voix
  d'avant s'arrête (`HostModel::updateVoice`). `coefficients` et `Voice` lèvent une exception sur ces machines : il faut
  ce chemin avant le fil audio, pas un simple filtre, sinon la voix d'avant continue de sonner ;
- `OsTables::decay` remplace la table que `HostModel` lit dans l'OS pour les User Machines.

## Noté par le ticket 10 (2026-11-05)

- Rien ne saute une piste muette dans `md::fast` : 0,4 µs par piste pour la chaîne, et 3 µs pour l'Echo et la Gate Box même sans
  envoi. Règle exacte par construction, pour une paire de voies et un étage : entrée marquée silencieuse, y1, y2, x1, x2 et c1 à
  0, `floor(a) == 0` ; la mesure donne 0,3 à 0,8 s pour y arriver après un bruit à 0,5. Echo : entrée et écritures nulles sur une
  passe de ligne, 2,2 s, pointeurs maintenus. Gate Box : sur 8192 + 2663 échantillons, `gate()` maintenu. Il faut un masque d'entrée
  par `Block` ; rétrécir un préfixe de voies gèlerait une fin, inexact ici, et la voix t du MD doit rester sur la piste t.
- `Block` dit « 48 kHz » dans `stFastStages.h`, faux pour le MD : ses quelques lignes passent dans un en-tête neutre quand le moteur
  lie les deux familles.
- La chaîne d'une piste Syntakt tourne-t-elle avant ou après son rééchantillonneur ? Avant, les étages MD vivent à deux fréquences
  dans un processus, et `tables()` devient `tables(rate)`, à trancher ici.

## 2026-11-06 : correction après écoute

Les chiffres de la voix éteinte et de `alignTicks` étaient mal décrits, ici et dans le commit. Le scénario `skip` de `edDrumsEngineTest` ne joue aucun rythme
à 125 ou 30 BPM : le tempo n'y règle que le mot de tempo, les Hits tombent au hasard, 1 sur 6000 ou 24000 blocs (environ 4,4 et 17 s), sons et machines
tirés au sort, 16 pistes, et `alignTicks` n'y est pas appelé. Remesuré sur rendus réels :
- Un Hit n'est touché qu'après la queue de sa voix plus 5,94 s de silence ; 98 machines sur 98 identiques à 6,0 s d'écart ou moins. Un pattern normal ne gèle jamais.
- Après gel, sons par défaut : les machines à bruit tirent un autre bruit (environ +3 dB de résidu, pas de changement de niveau), les autres autour de -50 dB.
- `alignTicks` sur pattern fixe : seul P-I diffère (-12,3 à -26 dB), TRX, EFM, E12, ROM et GND sont identiques ; les autres familles ne bougent que sous un balayage de PTCH.

Écoute à l'aveugle du même jour, 8 essais par paire, sortie à 44,1 kHz : P-I-SD 3/8, TRX-CB 5/8, EFM-SD 5/8 ; ROM-17 et a1 « rien entendu » ; a2 et a3 inachevées. Rien de prouvé
entendu : gel et `alignTicks` confirmés par l'utilisateur.

## 2026-11-06 : rééchantillonneur à phase minimale

`Resample48To44` passe du filtre à phase linéaire (53 coefficients par phase, retard 24 échantillons) à son jumeau à phase minimale, tronqué à 48 coefficients par phase. Une table générée par `stFastResampleTaps.py` remplace le calcul au démarrage. `HitDelay` passe de 82 à 81 échantillons, 1,84 ms, gigue 0, et c'est désormais le Machinedrum qui le fixe : 31 + 38 + 12. Un Hit Syntakt retient sa voix de ce qui reste après le retard de sa chaîne, de sorte qu'un drive suréchantillonné arrive à l'heure : il restait 3 et 4 échantillons de retard, il reste 1 en 2x et 4x, le bord de l'attaque.

Écoute. L'aveugle a porté sur le filtre tronqué à 44 coefficients, sur BD MODERN, CY ALLOY, SD BASIC et une charleston CY ALLOY : rien entendu, sauf BD MODERN Hit 6 à 1/8, étiquette inversée possible. Le filtre à 48 en diffère de -80 dB sur un Hit, de 0,002 dB jusqu'à 18 kHz, avec le même retard. SY BITS, CP VINTAGE et PC CARBON n'ont pas été écoutés : ce sont les pires à la mesure de Q15. Une paire à l'aveugle est prête pour SY BITS Hit 2, CP VINTAGE Hit 6 et BD MODERN Hit 6. Si une différence s'y entend, on revient au filtre linéaire.

Q15 ne tient plus 0,2 dB d'enveloppe par tranches de 1 ms. Pire Hit, en dB, puis 10 ms sous 18 kHz, non jugé : SY BITS 7,15 et 2,68, CP VINTAGE 4,13 et 0,16, CY ALLOY 3,78 et 0,12, SD BASIC 2,13 et 0,06, PC CARBON 1,07 et 0,07, SP TWINSHOT 0,92 et 0,37, SY TOY 0,75 et 0,04, SD VINTAGE 0,73 et 0,04, BD MODERN 0,52 et 0,02. Les bornes par machine, mesure plus 0,1 dB, détectent un changement ; ce ne sont pas des critères de qualité. Elles dépendent de l'alignement au nombre entier d'échantillons de Q15 : un décalage de 2, 3, 4 ou 5 fait passer SY BITS de 11,4 à 7,1, 15,8 et 19,9 dB. Le spectre de SP TWINSHOT monte à 1,085 dB à cause du même alignement, 0,244 avec un décalage de 2, et sa borne passe à 1,19 ; le premier diagnostic, un repliement de la bande coupée, était faux : à 48 coefficients la bande coupée gagne 10 dB et le chiffre ne bouge pas.

Rester à 44 coefficients aurait coûté 10 dB de bande coupée (-76,5 contre -86,5 dB à 30-40 kHz) et quatre bornes élargies. À 48, Q11 donne une réjection de -70,3 dB et un THD+N de -91,4 dB, contre -71,3 et -87,3 en linéaire ; les bornes du test de LFO reviennent à leurs valeurs.

Reste moins bon qu'avant : un second Hit dans la fenêtre de phase, jusqu'à 77 échantillons après le premier contre 57, laisse le premier à -18,1 dB du pic au lieu de -22,7, sur environ 45 échantillons de son attaque ; les échantillons du second sont à -107,8 dB au lieu d'être exacts, cause inconnue. SD BASIC vers PC CARBON sur un Hit immédiat : -68,8 contre -75,0 dB, borne -46.

`Chain::delay` est plafonné à `Chain::MaxDelay` : pendant le déplacement d'un drive de pré à post en plein fondu, les deux places comptaient, jusqu'à 20 échantillons en 4x. Une voix peut alors arriver jusqu'à 8 échantillons en retard, le temps du fondu. Le retard de chaîne est lu au noteOn : un `setChain` dans les 59 échantillons qui suivent déplace ce Hit d'au plus 12 échantillons, sur le MD comme sur le Syntakt.

Coût à 44,1 kHz, 16 voies : 1,47 µs par bloc hôte contre 1,60 à 1,70. Le coût du moteur ne bouge pas au-delà du bruit, machine chargée de 1,9 à 7,5 cœurs par d'autres programmes.
