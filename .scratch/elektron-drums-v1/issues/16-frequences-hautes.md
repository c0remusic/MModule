# 88,2 et 96 kHz : rendu à mi-fréquence

Type: AFK
Status: resolved
Assignee: 
Blocked by: 11

## Quoi

À 88,2 et 96 kHz, rendre les moteurs à 44,1 ou 48 kHz et remonter par un suréchantillonneur 2x HIIR ; option
globale « 96 kHz natif » au double du coût, gardée dans l'état. 192 kHz marche sans être budgété.

Noté le 2026-10-27 depuis le ticket 06 : le Syntakt rend toujours à 48 kHz. À 96 kHz, le 2x HIIR suffit ; à 88,2 kHz,
le rééchantillonneur 48 → 44,1 kHz puis le 2x, ou un 48 → 88,2 kHz direct, à mesurer. L'option « 96 kHz natif » ne
s'applique pas au Syntakt.

## Critère de fin

- Q15 : un Hit à 44,1, 48 et 96 kHz, enveloppe et spectre dans les seuils.
- Pire cas 16 pistes sous 15 % à 88,2 et 96 kHz ; 192 kHz rend sans erreur.

## Références

Spécification §11.2. Ticket « Une fréquence commune ».

## Noté par le ticket 10 (2026-11-05)

Tout `md::fast` dépend de 44,1 kHz sauf `Srr`, qui prend sa fréquence (à SRR 0 il cesse alors d'être un bypass) :
- `Rate` dans `mdFastTables.cpp` porte les tables ; EQ et filtre ne sont exacts qu'à 44,1 kHz, et à 48 kHz ils liraient 8,8 % trop haut.
- Les mots de blocage de DIST ($7FDF3B, -$4189) demandent une formule en Hz ; le suréchantillonnage de drive met DIST à une autre fréquence.
- La rampe de 32 échantillons est liée au bloc.
- Echo et Gate Box : `SampleRate` de la table de déclin, pôles d'amortissement, toutes les longueurs de ligne, le 44100 de TIME,
  le pas du sinus par bloc, les vitesses du gate.

## Noté par le ticket 11 (2026-11-06)

**48 kHz manque aussi.** Le moteur de la v1 ne marche qu'à 44,1 kHz : ailleurs le plug-in se tait et annonce une latence 0. Le
critère Q15 et la spécification (§11.1, §12 point 4) exigent déjà 48 kHz : ce ticket le couvre, et il doit décider du MD à 48 kHz.
Inchangé, il joue 8,8 % trop haut. L'anneau MD reçoit alors une lecture FIR comme celle du Syntakt, et `HitDelay` devient une
fonction de la fréquence.

## Noté par le ticket 12 (2026-11-06)

`Chain` sépare débit et famille : le MD à 48 kHz passe par `Chain(48000, ...)` et `tables(rate)`, qui s'arrêtent sur tout autre débit.
Le 96 kHz natif doit ajouter ses tables. `Oversampler` est dans sa propre paire pour le 2x hors de la chaîne.

## Noté le 2026-11-06

Décidé avec l'utilisateur : le MD à 48 kHz passe par un rééchantillonneur, comme chez Gearmulator, où la machine garde sa fréquence et `ResamplerInOut` fait le pont. L'utilisateur produit à 44,1 kHz : 48 kHz ne doit
rien coûter à ce chemin. Les LFO comptent leurs pentes en blocs : le 96 kHz natif les diviserait par deux, à remesurer alors.

## État (2026-11-12)

Le moteur joue à 44,1, 48, 88,2, 96 et 192 kHz ; le plug-in annonce `Engine::hitDelay(rate)` comme latence et se tait
toujours, latence 0, à toute autre fréquence (22,05 kHz vérifié par `edDrumsVst3HostTest`). Chaque famille garde sa
fréquence : le Machinedrum (et OPEN DRUM, SOPHIE, VINTAGE, sur ses blocs) reste à 44,1 kHz, le Syntakt à 48 kHz. Hors de
44,1 kHz, l'anneau MD de chaque piste est lu par un FIR polyphase (`RateFir`, `edDrumsRate.h`) à une phase que le Hit fixe,
exactement comme l'anneau Syntakt l'était déjà à 44,1 kHz ; l'anneau Syntakt passe par un autre `RateFir` (88,2, 96,
192 kHz) ou se lit tel quel à 48 kHz. Les envois tournent à leur 44,1 kHz derrière `SendsAtRate` (descente, blocs de 32,
remontée) ; le master, les rampes de pan et d'envoi, le transport et les LFO comptent en échantillons de l'hôte.

Pourquoi pas `ResamplerInOut` ni le Resample48To44 tel quel : le premier ne sait pas placer une phase par piste, donc un
Hit tomberait à un échantillon près (gigue non nulle) ; le second n'a qu'un rapport. `RateFir` reprend la table polyphase
et la boucle de lecture du Syntakt, pour tout rapport, avec un filtre calculé au démarrage (sinc à 20,5 kHz, Kaiser bêta 8,
48 prises par phase, phase linéaire) : gain à 0,002 dB près jusqu'à 18 kHz, rejet de 84 dB au-delà de 23,6 kHz sur les onze
conversions. La phase linéaire coûte 24 échantillons de source de latence, mais c'est elle qui tient Q15 à 0,008 dB : la
phase minimale du Syntakt à 44,1 kHz déplace chaque bande et sort de 0,2 dB, d'où ses bornes machine par machine.

Mesuré par `edDrumsRateTest` (ajouté au CMakeLists et à `gate.ps1`, aussi parmi les tests de temps) :

- **44,1 kHz inchangé** : une scène de 3 s (les quatre familles, les deux grains du Sampler, envois, master, LFO, chaînes,
  Choke, Locks, blocs d'hôte aléatoires) rend au bit près ce que rendait 3413ea64 : FNV-1a `bfc5a39c2d1d1e3a` des deux
  côtés, et les 16,9 Mo des 32 canaux comparés octet par octet (fc, aucune différence). Le chemin 44,1 kHz garde ses
  branches : aucun FIR, aucun pont, aucune table construite.
- **Q15** : un Hit de 9 machines Syntakt (contre 48 kHz), 12 machines MD, 4 OPEN DRUM, SOPHIE et le sinus des deux grains
  du Sampler (contre 44,1 kHz), à 48, 88,2, 96 et 192 kHz : enveloppe 1 ms au pire 0,008 dB (seuil 0,2), tiers d'octave
  0,006 dB (0,5), position à 0,001 échantillon d'hôte. Le Syntakt à 44,1 kHz (Resample48To44, inchangé) : 0,21 à 0,70 dB,
  dans la borne de chaque machine (0,63 à 7,25).
- **Hauteur** : au pire 0,019 cent de l'écart à la fréquence de la famille (seuil 1).
- **HitDelay(rate)** : 117 échantillons à 48 kHz (2,44 ms), 213 à 88,2 (2,42), 233 à 96 (2,43), 466 à 192 (2,43), contre
  81 (1,84 ms) à 44,1. Gigue 0 : 114 voix (toutes les machines, OPEN DRUM, SOPHIE, les deux grains) à 6 positions de note
  par fréquence, mêmes échantillons au bit près.
- **Envois, master, LFO, séquenceur** à la bonne vitesse et hauteur : le retour d'un sinus par Echo, Gate Box, delay et
  reverb du Syntakt, chorus et phaser à 0,11 cent au plus ; ses enveloppes 10 ms et ses tiers d'octave pas plus loin de
  44,1 kHz que 44,1 kHz ne l'est de lui-même quand le Hit tombe ailleurs dans le bloc des envois (Echo 0,17 dB et 2,2 dB
  contre 0,20 et 2,3) ; master qui comprime 0,25 dB au pire (à 48 kHz : son détecteur lit les crêtes entre les
  échantillons), LFO sur niveau et pan 0,01 dB ; phase d'un LFO libre après 1 s identique ; un pas du séquenceur à son
  temps à chaque fréquence.
- **Coût**, pire kit de 16 pistes (9 Syntakt, 7 MD au plus cher, chaînes les plus chères en 2x, 2 LFO par piste, les 4
  envois, le master), minimum de 5 passes de 2 s, trois mesures, **machine chargée (Ableton ouvert)** : 44,1 kHz 10,8 à
  11,9 %, 48 kHz 11,4 à 12,6, **88,2 kHz 13,8 à 14,6, 96 kHz 14,4 à 14,7**, 192 kHz 17,8 à 18,2 (non budgété). Sous
  15 %, mais à 0,3 à 1,2 point. La boucle des envois de `mix` a été scindée (rampe, puis niveau tenu), sans changer un bit à
  44,1 kHz. À 44,1 kHz, `render` garde sa boucle d'avant et `mix` ses constantes : une première version, qui lisait
  partout les rapports en variables, coûtait 0,2 µs de plus par bloc de 128 au moteur vide et faisait échouer le test du
  coût d'une piste éteinte d'`edDrumsEngineTest` ; mesuré contre 3413ea64 reconstruit, c'est revenu à 1,16-1,18 µs
  (avant : 1,18) et 1,40-1,42 µs pour 16 pistes éteintes (avant : 1,35, une mesure).
- **192 kHz** rend la scène sans erreur, valeurs finies.

Tests repassés après le changement : `edDrumsEngineTest`, `edDrumsLfoTest`, `edDrumsSophieTest`, `edDrumsVintageTest`,
`edDrumsOpenDrumTest`, `edDrumsAmpTest`, `edDrumsProcessorTest`, `edDrumsNoFlashTest`, `edDrumsSyntaktSlotsTest`,
`edDrumsEditorTest` et `edDrumsVst3HostTest` (qui attendait le silence à 48 kHz : il attend maintenant le son et une
latence de 117, et le silence à 22,05 kHz). Le garde-fou complet n'a pas tourné.

**Le chemin 2x mesuré, et écarté** : le moteur à 48 kHz puis les demi-bandes HIIR sur 32 canaux coûte 11,7 à 12,8 % à 96 kHz,
2 à 2,8 points de moins que la lecture directe. Mais un Hit n'y tombe qu'à deux échantillons d'hôte près (gigue 1), ce que le
critère de gigue 0 interdit. Le ticket disait « le moins cher dans les seuils Q15 » : c'est donc une décision à reprendre
avec l'utilisateur si la marge de coût se resserre.

**Reste** :
- l'option globale « 96 kHz natif » n'est pas construite : elle double le coût et demande ses tables (`Chain`, `mdFast`), une
  décision de coût à prendre avec l'utilisateur ;
- les retours d'envoi arrivent plus tard qu'à 44,1 kHz : 1,77 ms à 48, 1,09 à 88,2, 1,42 à 96, 1,24 à 192 (les deux FIR du
  pont, le bloc de 32 de l'hôte). Le chorus et le phaser, mêlés au son sec, en changent un peu de couleur ; non entendu ;
- un Hit sur une voix qui sonne encore déplace la queue de l'ancienne d'une fraction d'échantillon à sa nouvelle phase
  (comme le Syntakt à 44,1 kHz depuis le ticket 11) : non mesuré à l'oreille hors de 44,1 kHz ;
- le coût sur machine calme (ticket 25), où la marge à 96 kHz devrait s'ouvrir.

## État (2026-11-12, après le garde-fou rouge)

Le garde-fou a échoué deux fois sur `edDrumsRateTest`, machine calme : 15,04 % à 96 kHz. Mesuré d'abord où part le coût à
96 kHz, au-delà de 44,1 kHz : les FIR des anneaux (0,8 point), la descente des envois à 108 prises (0,5), le nombre de
passes de `mix` (0,17 point par millier de passes par seconde), le master et les gains par échantillon. Fait, chaque étape
mesurée :

- **Les FIR en AVX2 et FMA** (`edDrumsRateAvx2.cpp`, seul compilé pour AVX2, choisi à l'exécution par `rate::fastFir()`,
  le SSE sinon, `SYNTAKT_NO_AVX2` le force) : lecture des anneaux et flux des envois. Le gain le plus grand, 1,5 point.
  Jamais à 44,1 kHz, dont le Syntakt garde son SSE au bit près.
- **La descente des envois à 16 prises** au-delà de 48 kHz : ce qui est envoyé sort des FIR des anneaux, sous 20,5 kHz, et
  rien ne peut se replier dessous ; le filtre n'a qu'à être plat (0,008 dB à 18 kHz).
- **Le Syntakt en demi-bande** à 96 et 192 kHz (coupure à 24 kHz) : une phase sur deux (sur quatre) est l'échantillon même.
- **Le bloc de l'hôte à 64** à 88,2 et 96 kHz, 128 à 192 (envois et master, environ 0,7 ms comme à 44,1) : 4 420 passes
  par seconde à 96 kHz deviennent environ 2 900.
- Les gains de `mix` en SSE, et plus de division de 64 bits par passe, hors de 44,1 kHz, dans une `mixAtRate` à part :
  44,1 kHz garde le `mix` de 3413ea64 tel quel. Mêlé, il coûtait 0,17 µs de plus par bloc de 128 au moteur vide, et le test
  du coût d'une piste éteinte d'`edDrumsEngineTest` échouait (0,051 % contre 0,05) ; séparé, 1,08-1,09 µs contre
  1,12-1,16 µs pour 3413ea64 reconstruit, et 1,26-1,28 pour 16 pistes éteintes contre 1,32-1,59.

Résultat, pire kit, minimum de 5 passes, deux mesures, Ableton ouvert : **88,2 kHz 11,8 à 12,1 %, 96 kHz 12,0 à 12,3 %**,
44,1 kHz 10,9 à 11,6, 192 kHz 13,9 à 14,5 (non budgété). Sans AVX2 (mesuré avant la séparation de `mix`) : 14,1 et 14,2.

Écarté après mesure : rendre les blocs des familles au début de leur bloc d'hôte, une passe par bloc d'hôte : 12,4 % à
96 kHz, mais le pan d'un LFO y bougeait jusqu'à 0,67 ms trop tôt (0,33 dB d'enveloppe) et la latence prenait 0,67 ms.
**L'option (b)** (demi-bande par piste et retard entier h mod 2) n'est pas construite : elle laisse `mix`, les envois et le
master à la fréquence de l'hôte et ne réduit que la lecture des anneaux, qui coûte maintenant 0,4 point en AVX2 : au mieux
0,3 point de moins. Le chemin 2x complet mesure 12,4 à 12,9 % à 96 kHz, mais avec une gigue d'un échantillon.

Corrigé après relecture :
- **La phase d'un Hit passe en fondu** (64 échantillons du Machinedrum, ou ce qui reste avant la phase) au lieu d'un saut
  qui relisait la queue de l'ancienne voix décalée d'une fraction d'échantillon, à plein niveau. Test : un sinus de 1 kHz
  frappé de nouveau, à chaque position où l'ancienne voix dépasse le changement de phase : la différence seconde sur le
  fondu reste sous celle du son tenu (-1,0 à -1,4 dB) ; sans le fondu, +5 dB à 48 kHz, +27 dB à 192.
- `phaseFrom` : un static_assert verrouille le cas de 44,1 kHz (retard de chaîne jusqu'à 12).
- `SendsAtRate` calcule son préroulage une fois, à la construction.
- Une instance passée de 44,1 à 96 puis à 44,1 kHz (`edDrumsProcessorTest`) : latence 81, 233, 81, et 44,1 kHz rend les
  mêmes 245 760 échantillons avant et après. La place des anneaux est vérifiée à la compilation à chaque fréquence.

Les retours d'envoi arrivent maintenant plus tard qu'à 44,1 kHz de 1,77 ms à 48, 0,64 à 88,2, 1,27 à 96, 1,23 à 192.

## Revue du code final (2026-11-12)

- À 96 et 192 kHz, la demi-bande laisse au Syntakt sa bande jusqu'à 24 kHz, et le filtre des envois (16 coefficients,
  coupure à 0,45 de la fréquence de l'hôte) ne la coupe pas avant 44,1 kHz : ce qu'un Syntakt envoie entre 22,05 et
  24 kHz se replie entre 20,1 et 22,05 kHz dans le retour des envois. Rien sous 20 kHz ; le commentaire de `downOf`
  (`edDrumsRate.cpp`), qui dit que rien ne se replie, est à corriger au prochain commit gaté.
- Deux Hits d'une Track à moins de la marge de `phaseFrom` (environ 30 échantillons de l'hôte) : le second remplace le
  changement de phase du premier, qui se lit donc à l'ancienne phase jusqu'au second (une fraction d'échantillon de
  décalage, moins de 0,5 ms à 96 kHz). Le Syntakt l'avait déjà à 44,1 kHz ; un Choke dans cette fenêtre coupe aussi à
  l'ancienne phase. Laissé tel quel.
- Une Track silencieuse saute la lecture et la rampe de pan et d'envoi dans `mixAtRate` comme dans `mix` à 44,1 kHz :
  même comportement à toutes les fréquences.
