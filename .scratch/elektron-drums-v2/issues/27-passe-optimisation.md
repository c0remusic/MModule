# Passe d'optimisation : chaîne, master, lecteur DT2

Type: AFK
Status: done
Assignee: Claude (session 2026-11-18)
Blocked by: 06 (tranche 1)

## Quoi

Proposée à la fin de la tranche 1 du ticket 06. Baisser le coût CPU du moteur de Lockstep sans changer un seul
échantillon de sortie, mesuré avant et après. Cibles, dans l'ordre :

1. la chaîne par piste (4,15 % du pire kit au dernier garde-fou, guide 2,0) ;
2. le master (0,33 à 0,48 % aux garde-fous, guide 0,3) ;
3. le lecteur DT2 de la couche sample (`render96`, `decimate`), la couche à 1,47 % pour un guide de 1,5.

## Critère de fin

- Chaque levier mesuré sur machine calme (charge des autres processus relevée), gardé ou rejeté avec ses chiffres.
- Échantillons identiques au bit : `edDrumsDt2PlayerTest` (DT2_VECTORS), `edDrumsChainTest`, `edDrumsEngineTest`,
  `edDrumsLayerTest`, `edDrumsRateTest` passent sans changement.
- Gate vert, commit et push sur la branche.

## Méthode

Nouveau banc à la main, `edDrumsStageCost` (pas un test du garde-fou). Chaque étage tourne seul sur une entrée fixe
(bruit d'une graine fixe), 16 pistes, Hits toutes les 10 ms. Il donne la moins chère de N passes et une empreinte
FNV-1a de chaque échantillon de sortie. Six étages sont mesurés :

- la chaîne au plus cher, sur les deux chemins ;
- la couche : simple, avec BR et decay, puis un échantillon court joué jusqu'à sa fin, en reverse et en boucle ;
- le master.

Le projet compile en `/fp:fast`, donc tout changement de source peut changer le code émis. L'empreinte est le seul
juge du « au bit près ». A/B : le même banc est bâti sur les sources de HEAD (66ad8413) et sur l'arbre modifié. Les
passes sont alternées, et chaque levier est gardé seulement si toutes les empreintes sont égales.

Profil : l'échantillonneur maison (`elektron-work/scratch-arch/bench/prof/prof.cpp`) est lié au banc dans une cible
temporaire, retirée ensuite. Les mesures et profils sont dans `elektron-work/bench/stage-*`.

## Réponse (2026-11-18)

A/B final, deux tours alternés, 0,2 à 0,4 cœur pris ailleurs. Toutes les empreintes sont égales entre HEAD et l'arbre
modifié. Le moins cher de 9 passes de 2 s :

| Étage | HEAD | Après | Écart |
|---|---|---|---|
| chaîne, chemin Machinedrum (44,1 kHz) | 1,951 % | 1,751 % | -10 % |
| chaîne, chemin Syntakt (48 kHz) | 2,096 % | 2,052 % | -2 % |
| couche, simple | 1,539 % | 1,340 % | -13 % |
| couche, BR et decay | 1,572 % | 1,379 % | -12 % |
| couche, fins, reverse, boucles | 1,181 % | 1,023 % | -13 % |
| master | 0,272 % | 0,189 % | -31 % |

Dans son propre test, `edDrumsLayerTest` mesure la couche à 1,279 % (1,47 au commit de la tranche 1), sous le guide de
1,5 avec de la marge. Le master passe sous son guide de 0,3. La chaîne seule reste autour du guide de 2,0. Le pire kit
du garde-fou se juge au gate ci-dessous.

Au garde-fou du 2026-11-18 (`GATE GREEN`, 77 tests, 2 289 s, jusqu'à 3,00 cœurs pris ailleurs), le pire kit coûte
11,83 % d'un cœur : moteurs 7,33, chaîne 3,81, envois 0,64, master 0,05. Ce pire kit choisit d'autres drives que le banc
(types 4 et 5 : DAC et Overdrive). Le banc reste la mesure de ce ticket. La ligne de la couche du test du pire kit
dit encore « not built yet » ; elle se branchera à la tranche 2 du ticket 06.

### Leviers gardés

- **Lecteur DT2, état de la voix en variables locales.** MSVC ne fait pas d'analyse d'alias par type. Les écritures
  dans `_work` (float) l'obligeaient donc à garder `v.position` en mémoire, avec stockage et relecture à chaque sortie
  à 96 kHz, et à relire `v.pcm` et le noyau. Couche : 1,603 à 1,489 %.
- **Lecteur DT2, les six produits en SSE.** Huit int16 sont chargés et convertis d'un coup, puis multipliés par une
  ligne du noyau mise à 8 floats (deux zéros). La somme reste faite un terme après l'autre, dans l'ordre des prises :
  c'est la même somme. Le chemin rapide demande maintenant 8 échantillons dans l'échantillon, contre 6 avant ; les deux
  positions d'avant la fin passent par la boucle bornée. Couche : 1,489 à 1,359 %.
- **Lecteur DT2, le sens, la boucle et le pas sortis de la boucle des 64 sorties.** Couche : 1,359 à 1,343 %, au bord
  du bruit, gardé parce qu'il simplifie la boucle.
- **ThreeBand, les trois bandes dans une seule boucle d'échantillons.** Trois récurrences sont en vol au lieu d'une.
  Part de ThreeBand dans le profil de la chaîne : 11,4 à 8,9 %.
- **Filtre Machinedrum en AVX2, 4 pistes par vecteur au lieu de 2.** Fait comme DIST au ticket 26 :
  `mdFastTrackFxAvx2.cpp`, `/arch:AVX2 /fp:precise`, choisi par `rate::fastFir()`. Les mots d'une paire immobile sont
  recopiés sur le bloc quand l'autre paire du groupe bouge. `edDrumsChainTest` compare le rendu AVX au rendu SSE, au
  bit près, à 44,1 et 48 kHz : mots fixes et en rampe, paires prises en entier, à moitié ou seules. Chaîne MD : 1,95 à
  1,76 %.
- **Master, ses deux canaux en SSE2 double.** Mêmes opérations par canal : le clamp, la saturation en trois passes et
  le volume. `exp` et `log10` restent ceux de la CRT, appelés à chaque échantillon. Master : 0,275 à 0,189 %.

### Leviers rejetés

- **DIST sur deux groupes de 4 pistes dans une seule boucle AVX**, pour avoir deux récurrences en vol. Plus lent : sa
  part dans le profil de la chaîne passe de 14,2 à 18,4 %. Douze vecteurs d'état plus neuf constantes dépassent les 16
  registres.
- **Nombre de sections du suréchantillonneur connu à la compilation** (8 et 4 au lieu d'un `count` lu à l'exécution).
  Le code émis garde l'historique des all-pass en mémoire d'un échantillon à l'autre, et ce levier visait à le tenir en
  registres. Aucun gain mesuré : 1,76 contre 1,76 % sur le chemin MD, 2,05 contre 2,07 sur le chemin Syntakt.

### Laissés, avec leur part du profil

- `decimate` du lecteur DT2, 16 % de la couche : une récurrence série en double par piste. La gagner demanderait de
  rendre plusieurs pistes ensemble, ce que l'API par piste de la couche ne fait pas.
- Le suréchantillonneur (25 % de la chaîne, déjà en AVX2), DIST (14 %), le drive Syntakt (8 %) et Multimode (12 % du
  chemin Syntakt). Ce sont des récurrences déjà vectorisées : le levier restant serait un portage AVX par étage.
- `setBand` sous LFO : `pow` et `tan` par piste et par bloc, environ 7 % de la chaîne. `tan` ne dépend que de la
  fréquence de la bande et pourrait se mettre en table.
- `scale` des niveaux d'étage (4 %) : des multiplications par des puissances de 2, qui se fondraient dans DIST au prix
  d'une interface d'étage différente de celle du firmware.

### Relevés de revue mis de côté

- Les clamps du master en min/max ne propagent plus un NaN comme `std::clamp`. Sous `/fp:fast`, le compilateur ne le
  garantissait déjà pas.
- La version SSE de `saturate` est maintenant une seule fonction, `saturatePair`, que `Overdrive::process` et
  `Master::sample` appellent tous les deux. Restent la fonction scalaire et la copie AVX d'`Overdrive`.
  `edDrumsChainTest` compare les builds SSE et AVX d'`Overdrive` à la formule ; aucun test ne compare `Master::sample`
  à `saturate`, sauf l'empreinte du banc.
- Le filtre AVX copie `Poles`, `Zeros` et la boucle de rendu du fichier SSE, comme DIST au ticket 26, au lieu d'un
  noyau écrit une fois sur un struct d'opérations comme le suréchantillonneur. `avxKernels` les compare au bit près.

## Références

Ticket 26 (`26-chaine-4x-sous-4.md`), ticket 06 (`06-couche-sample.md`, tranche 1) ; spécification v2, §3.
