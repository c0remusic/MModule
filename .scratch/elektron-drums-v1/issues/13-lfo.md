# Deux LFO par piste, SYNC ou HZ

Type: AFK
Status: resolved
Assignee: 
Blocked by: 11

## Quoi

Deux LFO par piste. SYNC (défaut) : division musicale de 1/64 à 16 mesures, pointée, triolet, phase sur la position
du transport de l'hôte, sauf en mode de trig TRIG où elle redémarre au trig ; vitesse et multiplicateur des LFO
Elektron traduits en division. HZ : vitesse en Hz. Cible sur la piste ou sur un paramètre d'une autre piste.
Avancent au bloc.

## Critère de fin

- Phase identique d'un rendu à l'autre pour une même position de transport ; tempo changé, période suivie.
- Un LFO qui vise une autre piste la module, sans coût pour une piste sans LFO actif.

## 2026-11-06 : fait

`edDrums/edDrumsLfo.*` porte `Lfo` (7 formes, 5 modes du Syntakt pour les deux familles, 11 divisions droites, pointées et triolets,
`lfoRateOfSyntakt`, `lfoRateOfMachinedrum`) et `LfoBank` (LFO, sommes, cibles en attente, testé sans firmware). `Engine` garde `taken`,
`takeMoves` et le transport (`setPosition`, `position`) ; `edDrumsProcessor::sync` donne tempo et position, un transport arrêté laisse les
LFO courir au tempo. Cibles : niveau et pan à la piste, SYN1-8 à la machine (`Track::modulate`), le reste par `Chain::modulate` et
`offset`, en 1/128 de valeur. `edDrumsLfoTest` et `edDrumsProcessorTest` entrent au garde-fou, 2 tests.

| Critère | Seuil | Mesuré |
|---|---|---|
| Mêmes échantillons quels que soient les blocs de l'hôte | identiques | identiques, 3 cibles, deux familles, blocs 40000, 128, 32, aléatoires 1-700 |
| Même phase à même position | égale | 0 d'écart, 0,00218 de cycle hors grille de bloc |
| Tempo changé, période suivie | ±256 échantillons | 22048,0 contre 22050 à 120 BPM, 33075,2 contre 33075 à 80 |
| LFO sur une autre piste | module | les 6 combinaisons de familles, gain 0,000 à 3,938 |
| Une cible vaut sa valeur | identique | 88 cas : Hit sous un carré tenu égal à un Kit à la valeur déplacée |
| Hit à profondeur 0 | identique | 107 machines × 12 positions |
| Hit de LFO TRIG | identique | 4 machines × 12 positions |
| Q12, niveau, carré à pleine profondeur | -50 dB | -53,1 MD, -55,7 Syntakt |
| Q12, fréquence | -50 dB | Syntakt 8 blocs -56,6 et -55,6 ; MD 24 blocs -61,5, -53,0 au pire |
| Q12, deux LFO sur une cible, LFO d'une autre famille, retarget | -50 dB | pire -52,0 ; -53,1 ; -58,0 et -54,6 |
| Allocations | 0 | 0 sur 2000 blocs, 32 LFO |

Latence et gigue inchangées : `HitDelay` 82, gigue 0.

**La pente est limitée à la cible**, pas au LFO : `Track::taken` suit `modulation` par pas, `Chain::moveLimit` donne 24 blocs (17 ms) au filtre
MD et 8 ailleurs, 4 blocs (2,9 ms) à niveau, pan et SYN. Le filtre MD en a besoin : 8 blocs -32,8 dB, 16 -48,6, 24 -53,0. Un sinus de
profondeur 127 sur le filtre MD tourne au triangle au-delà de 9 Hz environ, par calcul. **Un Hit règle tout ce qui vise sa piste** (`takeMoves(track, true)`) :
HOLD et FREE deviennent identiques au bit près. Le saut coûte 0,7 et 2,3 dB sur un retrigger avec queue, le retrigger domine.

**SYN1-8 bougent au bloc**, pas au tick : écart de 352 à 36 échantillons selon le Hit. Coût +0,18 sur 16 MD, +0,02 sur kit mixte, comparé bloc
contre tick, sous 6 à 8 cœurs de charge. Détecteur de voix calme : un bloc n'est calme que si la voix et la sortie sont sous le seuil, sinon
un filtre fermé par LFO tuait la voix tenue. **Prix** : une voix tenue sous un filtre fermé pour de bon ne s'éteint plus et coûte.

**Coût**, kit mixte, chaînes les plus chères, drives 2x : sans LFO 9,46 % ; FLTF et 3 bandes +0,53 ; 2 bandes +0,74 ; SYN1 et SYN2 +0,39 ; niveau et
pan +0,18 ; 16 MD SYN1 et SYN2 +0,31. **Pas sur machine calme : 2,2 à 9,3 cœurs pris ailleurs**, chiffres indicatifs. Le kit le plus cher avec LFO n'est
pas mesuré : 13,44 % sans eux, le plafond de 15 % tient sur ce qui l'est.

Revues, défauts reproduits par un test qui échouait d'abord : voix éteinte par un filtre fermé ; décalage périmé de `md::voice::Track` après un
aller-retour de famille ou par Off ; pente par LFO, deux LFO allaient deux fois plus vite, retarget en un pas ; HOLD pris après le Hit ; enveloppe de
filtre figée à profondeur neutre, -5,0 dB ; verrous Syntakt partageant valeur jouée et valeur en attente ; `lfoDivisionOf(HUGE_VAL)`, caché par
`/fp:fast` ; tempo ou position non finis. Immédiats de la routine de l'OS retirés de `lfoRateOfMachinedrum`, remplacés par un ajustement à 16 %.

**Reste moins bien que demandé.** Réouverture de filtre : pic 0,520 contre 0,150 tenu, le filtre résonne, aucune borne. BASE/WIDTH clique à -49,0 et
-49,7, ses pôles sautent au bloc, borné à mesuré + 1. DIST, SRR et drive n'ont pas de clic : à leur bord -32,0, -18,2, -49,4 contre -31,7, -18,2, -48,2
tenus, c'est leur son propre. `Processor::engine()` reste non const, le test règle un LFO faute de paramètre hôte. Deux mutants survivent : `setPan` lisant la somme, le pan ayant sa propre rampe de 64 échantillons ; `lfoDivisionOf` sous `/fp:fast`, vu rouge
seulement en `/fp:precise`. Une fenêtre de Q12 a été déplacée de 42 blocs pour « deux carrés sur le filtre MD » : au bord -44,5 dB sans signal à mesurer,
sur la transition -60,4 et -61,3.

**Hypothèses à confirmer.** Formes et modes du Syntakt pour les deux familles : le mélange à deux formes du MD et sa SAW double ne sont pas repris. FADE et
phase de départ absents. Une mesure vaut 4 noires quelle que soit la signature de l'hôte. Traduction vers la division la plus proche, ±15,5 % de période au pire.
ONE et HALF s'arrêtent à 0, sauf RMP à HALF à 1, règle comparée aux 12 arrêts de la routine. Une reprise TRIG ne saute que sur sa propre piste.

Non vérifié : écoute, Live (aucun paramètre hôte n'atteint un LFO, ticket 18), 88,2 et 96 kHz, machine calme.

## Références

Spécification §4. Ticket « Chaîne de piste » (amendement LFO).

## Noté par le ticket 11 (2026-11-06)

`Track::update` est la couture où un LFO s'insère. Le moteur n'a aucune position de transport : ce ticket doit lui en donner une.
`alignTicks` met chaque piste sur sa propre phase de tick au Hit : un LFO qui vise une autre piste tombe donc sur une autre phase.

## Noté par le ticket 12 (2026-11-06)

`Chain::offset(lane, target)` est le seul endroit où une valeur se décale, `setDrive`, `setBand`, `setFilter` les seuls à recalculer ses
mots, `stepEnvelopes` le chemin par bloc : la modulation y entre. Ne pas découper `configure` d'avance : mesurer d'abord le coût des mots
de `ThreeBand` recalculés à chaque bloc. Un type d'étage modulé se fondrait en 96 échantillons : ne pas le moduler.
