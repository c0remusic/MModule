# Syntakt rapide : les onze autres machines

Type: AFK
Status: resolved
Assignee: 
Blocked by: 04, 05

## Quoi

Les onze machines numériques restantes sur le patron du ticket 04. SY TONE rend à 2x l'hôte ; SY BITS et SY SWARM
gardent leur fréquence absolue par échantillonneur-bloqueur. Une machine par commit, chacune avec ses tests.
Découper en plusieurs sessions si besoin, par famille de routines.

## Critère de fin

- Chaque machine passe Q2-Q4 (seuils du ticket 05) contre le port exact à 48 kHz, et « sonne pareil » 44,1/48.
- Q13 : le repliement voulu de BITS et SWARM est préservé.
- Coût des 16 pistes, toutes machines mélangées au pire cas, mesuré et rapporté ; cible de la proposition : ~10 %.

## Références

Spécification §3.1, §11.2. Ticket « Une fréquence commune ».

## Décision du 2026-10-30 : des machines de drum seulement

« C'est une boîte à rythme qu'on construit, pas besoin de modules autre que de drums. » SY TOY et SY BITS sont des
machines de drum (d'après leurs démos) et restent. SY TONE, SY CHORD et SY SWARM sortent de syntakt::fast : SY TONE
(210ad9a1) et SY CHORD (598a6870), déjà poussés, sont retirés par un commit de suppression ; SY SWARM n'est pas
construit. Le port exact (stEngine) les garde, comme oracle. Le ticket compte donc neuf machines : BD MODERN, SD
BASIC, CY ALLOY, PC CARBON, SD VINTAGE, CP VINTAGE, SY TOY, SY BITS, SP TWINSHOT. Q13 ne porte plus que sur BITS.

## Coût mixte mesuré, 2026-10-30

`stFastMixCost` (outil à la main, hors garde-fou, sans firmware), Ryzen 7 3700X, médiane de 7 passes de 10 s, une
instance par machine utilisée (voie t = piste t), sommées voie par voie puis rééchantillonnées une fois par `Host`.
Pire cas : chaque machine sur une piste avec son son le plus cher, les 7 autres sur SP TWINSHOT (deux lecteurs au
plus haut débit sur des slots de bruit de 2^19 − 1 échantillons, HOLD), les 16 pistes relancées toutes les 10 ms
avec leurs 8 paramètres lockés. Kit réaliste : BD×2, SD×2, SD VINTAGE, CP VINTAGE, CY×3, PC×3, SY TOY, SY BITS,
TWINSHOT×2, doubles-croches à 120 BPM.

- Pire cas : 11,0 à 11,1 % d'un cœur à 48 kHz, 11,5 à 11,6 % à 44,1 kHz (blocs de l'hôte 32, 128, 512).
- Kit réaliste : 10,6 à 10,7 % à 48 kHz, 11,0 à 11,2 % à 44,1 kHz.
- Sous le budget de 15 % (marge 3,4 à 4,4 points), au-dessus de la cible ~10 %.
- Un `Host` par machine (neuf rééchantillonneurs) : 16,7 % au pire cas, hors budget. Contrainte reportée au ticket 11.
- Une voie muette coûte presque autant qu'une qui sonne, sauf SP TWINSHOT : le coût suit le nombre de machines
  utilisées (6 à 11 µs par machine et par bloc), pas de pistes. Levier vers 10 % : des instances plus étroites
  (reporté au ticket 11).
- Non mesuré : relancé toutes les 10 ms, un lecteur de TWINSHOT ne lit que ~23k échantillons du début de son slot ;
  ni le bouclage ni le passage d'un slot entier en mémoire.

## Voies étroites, 2026-10-30 : cible atteinte

Décision : viser les 10 %. Chaque machine a `setLanes(n)` (4, 8, 12 ou 16) : ses étages bornés par les voies du bloc
qu'ils écrivent (`Block::lanes`), sa commande par `m_lanes`. Un kit met les pistes d'une machine sur ses premières
voies. `stFastLanesTest` (au garde-fou) : sur les neuf machines, n = 4, 8 et 12 voies donnent les échantillons de 16
bit pour bit, chaque voie ayant sonné, et les voies au-dessus muettes dès le rétrécissement. Un étage qui lit un bloc
d'une autre largeur que celui qu'il écrit arrête le programme (`requireLanes`) : un bloc oublié dans un `setLanes`
fait échouer ce test au lieu de coûter en silence.

`stFastMixCost`, même machine, médiane de 7 passes (compact) et de 3 (`--wide`, l'ancienne disposition) :

- Pire cas : 4,05 à 4,16 % d'un cœur à 44,1 kHz (3,55 % à 48 kHz), contre 10,9 à 11,1 % en `--wide`. Sous compactage,
  le pire est 10 groupes de 4 voies, le plus que 16 pistes sur neuf machines remplissent : une piste sur chacune et
  les 7 autres sur SD VINTAGE, la machine dont un groupe coûte le plus (3,5 µs par bloc).
- Kit réaliste : 3,66 à 3,75 % à 44,1 kHz, contre 10,7 %.
- Un `Host` par machine (neuf rééchantillonneurs) : 8,65 % au pire cas, contre 15,5 %.
- Recherche faite : Surge XT compacte aussi ses voix par groupes de 4, mais recompacte à chaque bloc en recopiant
  l'état des voix ; ici une piste garde sa voie tant qu'elle garde sa machine. AVX2 écarté : aucun banc audio sur
  Zen 2, et 8 voies par vecteur gaspilleraient 7 voies pour une machine sur une seule piste.
