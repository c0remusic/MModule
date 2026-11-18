# La chaîne à 4x sous 4 % d'un cœur

Type: AFK
Status: ready-for-agent
Assignee: Claude (session 2026-11-17)
Blocked by: 01

## Quoi

Au garde-fou du ticket 01, la chaîne du pire kit coûte 5,86 % d'un cœur à 4x (guide 2,0 %), 2,76 % à 2x. Le pire kit
total mesure 13,71 % : la couche sample du ticket 06 (guide 1,5 %) le porterait à 15,2 %, au-dessus du plafond. Décidé
avec l'utilisateur le 2026-11-17 : le pire kit se juge à 4x, et la chaîne s'optimise avant la couche.

Chaque drive suréchantillonne seul (un `Oversampler` par type de drive) : deux drives par piste font deux paires
montée/descente sur 16 pistes. Leviers à mesurer, aucun prouvé :

- moins de sections all-pass dans le second étage du 4x, sous le test de qualité existant des drives ;
- un seul suréchantillonnage pour un segment de chaîne au lieu d'un par drive, si le son reste celui d'aujourd'hui.

Aucune dégradation automatique de qualité (spec v2, §3) : un levier qui change le son se mesure par les tests de
qualité des drives, et l'écoute tranche s'ils bougent.

## Critère de fin

- La chaîne du pire kit à 4x sous 4 % d'un cœur au garde-fou, le pire kit total sous 12 %.
- Les tests de qualité des drives et de l'oversampling passent sans seuil relâché.
- Gate vert.

## Avancement (2026-11-17)

Première étape commitée, critère pas encore atteint. Les drives suréchantillonnés ont maintenant des versions AVX2
(suréchantillonneur par 8 pistes, Overdrive et Dist par 4 en doubles), choisies par `rate::fastFir()` et bâties en
`/fp:precise` : `edDrumsChainTest` les compare aux versions SSE, échantillon pour échantillon (2 048 000 échantillons,
égaux). Au banc : le suréchantillonneur 4x passe de 0,52 à 0,29 % d'un cœur par drive, la chaîne 4x avec Overdrive de
2,04 à 1,09 %.

Au garde-fou du 2026-11-17 (charge 2 à 5 cœurs, Max, Chrome) : chaîne 4,35 %, total 12,63 %. Encore au-dessus des
cibles (4 % et 12 %). À remesurer sur machine calme avant d'ouvrir un autre levier ; les deux leviers de « Quoi »
restent à essayer.

### Les deux leviers mesurés (2026-11-17, plus tard)

La chaîne du pire kit, seule sur 16 pistes à 4x (DIST avant, drive Syntakt après, trois bandes, filtre Machinedrum,
priorité haute) : 2,0 % d'un cœur. Dont les deux suréchantillonnages 0,61, le calcul de DIST à 4x 0,75, celui du drive
Syntakt 0,40, bandes et filtre 0,6.

- **Moins de sections au second étage du 4x** : 2 au lieu de 4 tiennent Q6 (-83,9 et -82,2 dB, seuil -80 ; -86,4 et
  -83,3 avant) et Q7, le retard mesuré passe de 3,56 à 3,04 échantillons (4 annoncés). Gain : 0,292 à 0,279 % par
  drive, deux mesures alternées chacune, soit 0,03 point sur la chaîne. Rejeté : rien à gagner contre 2,5 dB d'alias.
  Avec 3 sections, `edDrumsChainTest` échoue (« a raised lane hears the type beside it ») : le noyau suppose un nombre
  pair de sections, sans conséquence tant que les deux étages en ont 8 et 4.
- **Un seul suréchantillonnage par segment** : ne s'applique pas au pire kit, dont les deux drives sont séparés par la
  bande et le filtre. Le rendre commun suréchantillonnerait aussi bande et filtre, et changerait le son.

Au banc dans le moteur (`edDrumsWorstKitCost` et une sonde à chaîne fixe), avec Blender à 3,5 cœurs : la chaîne à 4x
mesure de 2,2 à 5,4 % d'une passe à l'autre, et le pire kit choisit des types différents à chaque passe. Le critère
« sous 4 % au garde-fou » ne se juge pas sur cette machine chargée. Reste à faire : remesurer au calme ; si la chaîne
dépasse encore 4 %, le seul gros poste restant est le calcul de DIST à 4x (récurrence série par piste, déjà en AVX
sur 4 pistes).

### Au garde-fou lancé sur machine calme (2026-11-17, 23:08)

Lancé après 139 min d'attente sous 1,5 cœur (0,5 cœur au départ : Live 12 ouvert, rien d'autre). `GATE GREEN`,
74 tests, 2 225 s. Pire kit (le moins de 5 passes de 4 s) : moteurs 8,47 %, **chaîne 3,35 %** (sous les 4 % visés),
envois 0,61 %, master 1,24 %, **total 13,67 %** (au-dessus des 12 % visés, sous le plafond de 15). Mais le banc a vu
jusqu'à 15,1 cœurs pris ailleurs pendant ses passes : Blender (l'application de l'utilisateur) a repris son rendu
entre le départ du garde-fou et le test ; au même moment le master mesure 1,24 % contre 0,3 de guide, signe d'une
mesure dérangée. Le critère « chaîne sous 4 % » tient ; « total sous 12 % » reste à juger sur une passe de
`edDrumsWorstKitCost` sans autre charge, qui n'a pas encore eu lieu. Aucun code changé depuis 9934ffd9.

Garde-fous suivants, toujours sous Blender : arbre fusionné du ticket 17 (00:21, 8,7 cœurs pris ailleurs) : chaîne
3,81 %, total 12,20 % ; ticket 04 (10:56, 10,5 cœurs) : chaîne 4,01 %, total 11,80 %. Le total passe sous 12 une fois
sur deux, la chaîne oscille autour de 4 : rien de tranché sans machine calme.

## Références

Ticket 01 (`01-pire-kit-au-garde-fou.md`, sa réponse) ; spécification v2, §3.
