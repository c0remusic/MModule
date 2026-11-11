# Chaîne de piste à emplacements

Type: AFK
Status: resolved
Assignee: 
Blocked by: 10, 11

## Quoi

La chaîne de la spécification §4 : deux drives pré ou post (Off, Syntakt drive, MD DIST, MD SRR ; DT2 Overdrive au
ticket 15), MIX « + » ; BAND avant ou après le filtre (BASE/WIDTH, EQ MD, EQ 3 bandes « + ») ; FILTER (8 types
Syntakt, filtre MD) avec l'enveloppe de filtre commune ; AMP complet (AHD/ADSR, niveau, pan, accent) ; vélocité
assignable avec profondeur ; chaîne par défaut de la famille ; types automatisables avec rampe anti-clic ;
suréchantillonnage global des drives off / 2x / 4x (HIIR SIMD), 2x par défaut.

## Critère de fin

- Une piste dans sa chaîne de famille rend les mêmes échantillons que son moteur seul (Q1 ou tolérance de l'étage).
- Q5 (neutre), Q6, Q7, Q9, Q12 passent.
- Coût de la chaîne à 16 pistes, drives 2x, mesuré.

## 2026-11-06 : fait

`edDrums/edDrumsChain.*` porte la chaîne (`ChainSettings`, `syntaktChain()`, `machinedrumChain()`, `ThreeBand`, `Chain`),
`edDrums/edDrumsOversampler.*` le HIIR SSE, `syntakt/stFast/stFastTrack.*` les étages de piste du Syntakt en forme rapide, formules
et ajustements, rien du firmware : TrackDrive, BaseWidth, Multimode (8 types), TrackAmp (AHD/ADSR), FilterEnvelope. Le moteur gagne
`setChain`, `setOversampling`, `setAccent`, `noteOn(..., accent)`. Le premier choix d'une famille charge sa chaîne, un changement de
famille la recharge sans demander : la question à l'utilisateur appartient à l'interface. Deux drives pré ou post avec MIX, BAND
avant ou après, FILTER (8 types Syntakt et MD) avec enveloppe commune, AMP, cible de vélocité avec profondeur, changement de type
d'étage : un bloc de chauffe inaudible puis 64 échantillons de fondu, soit 96 en tout. Un second changement pendant le fondu attend sa
fin. `edDrumsChainTest` et `stFastTrackTest` entrent au garde-fou, 55 tests.

**La chaîne tourne avant les anneaux.** Un Hit y commence toujours à un bloc : un étage ou une enveloppe au rythme du bloc le
rencontre à l'identique. Hits identiques bit pour bit sur 107 machines à 12 positions, chaînes lourdes off, 2x et 4x à 7 positions
sur 4 machines. L'option de la grille fixe après les anneaux a été modélisée, pas construite : sous l'enveloppe du filtre MD, elle
s'écarte de -15,6 dB du signal. Son coût n'a pas été mesuré. La chaîne Syntakt tourne à 48 kHz avant son FIR, d'où deux instances de
`Chain` et `tables(rate)` pour les étages MD ; pas de seconde histoire FIR. Niveau, vélocité et fondu passent après la chaîne, à
l'anneau. Choke : une rampe de 64 échantillons après la chaîne, la queue de la chaîne part avec elle, jusqu'à ce qu'un Hit de la piste
sonne ; de là, l'ancienne voix descend avant la chaîne, sous la nouvelle.

**Critères.**

| Critère | Seuil | Mesuré |
|---|---|---|
| Q1, chaîne MD contre TrackFx de mdEngine | 0 LSB | 0 LSB, 10 machines, 127 960 échantillons |
| Q3, chaîne Syntakt contre portage exact | 70 / 60 dB | 106,4 dB, pire fenêtre 100,3 dB |
| Q5, emplacements Off | -120 dBFS | MD identique, Syntakt -135 dB, bandes à plat identiques au niveau de la chaîne |
| Q6, drive Syntakt | 2x -50, 4x -80 | 2x -67,0 ; 4x -86,4 sur piste MD, -83,3 post sur piste Syntakt |
| Q6, MD DIST sur piste Syntakt | 2x -50, 4x -80 | **2x -49,6, 4x -61,0, manqués** |
| Q7 | gain ±0,01 dB, THD+N -120 | gain 0,0000 dB ; -130,9 à -121,2 dB |
| Q9 | -3,01 ±0,05 dB | à 0,000 dB |
| Q12, Choke | -50 dB | -53,4 MD, -52,6 Syntakt |
| Q12, changements d'étage à 1x, 2x, 4x | -50 dB | -50,7 au pire, fenêtre 1024 |

DIST est un écrêtage dur, et la recherche déconseille de l'antirepliquer : laissé tel quel, le test exige seulement que chaque facteur
améliore. Q6 vise les drives maison ; **à l'utilisateur de dire s'il s'applique ici.**

Étages Syntakt seuls contre le portage exact : drive 83,5 / 82,6 dB ; BASE/WIDTH ouvert 111,3 / 103,7, sur 5 réglages 59,1 / 55,7, la
loi de pôle est un ajustement ; filtre types 0-6 au pire 60,0 / 55,0 à fréquences basses, **sous Q3**, borné à 59/53 ; type 7 151 dB ;
ampli à 0,02 dB jusqu'à -40 dB ; enveloppe de filtre contre la section 3 : attaque à 10,2 %, decay 2,6 %, délai 4,4 %.

**Latence.** `HitDelay` reste 82 (1,86 ms), gigue 0 sur les chaînes de famille ; les 4 échantillons du filtre MD sortent de l'attente
du Hit. Avec un drive suréchantillonné, gigue 0 mais décalage constant : piste Syntakt +3 à 2x, +4 à 4x par drive, à 48 kHz, faute de
marge. Piste MD compensée, -2 sur TRX-SD à 2x et 4x, non vérifié sur les autres machines. **Choix à confirmer** : monter `HitDelay` de
11 échantillons (0,25 ms pour tous) et compenser les deux familles, ou garder 82 et ce décalage. Gardé 82 pour l'instant, en-tête et
test le disent.

**Coût**, % d'un cœur, minimum de 5 passes de 4 s. **Pas sur machine calme** : jusqu'à 2,97 cœurs pris ailleurs. À refaire au ticket 25.

| Kit | Nu | Famille | Le plus cher 2x | Le plus cher 4x |
|---|---|---|---|---|
| Mixte | 7,08 | 7,79 (+0,71) | 9,39 (+2,31) | 10,85 (+3,77) |
| 16 MD | 8,40 | 9,29 (+0,89) | 10,11 (+1,71) | |
| Syntakt | 4,05 | 4,27 (+0,22) | 5,76 (+1,71) | |
| Alterné | 7,25 | 8,36 (+1,11) | 10,92 (+3,67) | 13,23 (+5,98) |

Le plafond de 15 % tient. **Le guide de 2 % pour la chaîne est dépassé** : mixte, alterné, 16 MD avec MIX 64 (+2,01). Le kit alterné
est le pire : familles entrelacées, les deux chaînes calculent les quatre groupes. Chaîne la plus chère sur 1 piste de 4 : +1,45 ; sur
1 de 16 : +0,50. Pire bloc 914 µs. Mesuré avant la dernière réorganisation, qui ne change pas les échantillons.

**Revues.** Défauts reproduits par un test qui échouait d'abord :
- Un suréchantillonneur par emplacement partagé entre types : une voie à côté d'un autre type relevé s'écartait de -1,7 dB, au réglage
  par défaut. Un par type désormais.
- Voie au repos salie par sa voisine de groupe après un Choke, -7,9 dB d'une chaîne neuve. Le Hit nettoie la voie qu'il réveille. Au
  niveau moteur, un charley coupé reste à -13,8 dB d'un neuf contre -10,6 pour la voix seule : c'est la mémoire de la voix, ticket 11.
- Réglages à deux propriétaires, chaîne perdue en passant par Off. `Chain` est seul propriétaire, `Engine::chain` a disparu.
- Cible de vélocité perdue par tout `setChain` sous un Hit, -5,1 dB ; profondeur d'enveloppe sans effet à 64.
- Hit au `gateOff` périmé, -29,4 dB ; MIX périmé au premier bloc, -49,0 dB ; deux Hits dans un bloc, le dernier en ordre de voie
  gagnait, -12,1 dB ; rampe de coupure au-dessus de 1 ; réglages hors bornes vers les tables.
- `mdHitSkip` effaçait les premiers échantillons d'un Hit à drive relevé. Il ne couvre plus que le retard pur du filtre MD : retrigger MD
  -26,0 dB, Hit coupé -26,0 dB, bornes -25,0. Le retirer entièrement coûtait 6 dB. Retrigger à drive relevé -21,2 MD, -38,7 Syntakt.

**Vélocité sous une queue coupée.** La queue partage le flux de la piste, donc le gain du nouveau Hit. Pire HF sur 14 positions :

| Cas | 127 à 127 | 127 à 64 | 127 à 1 | 1 à 127 |
|---|---|---|---|---|
| MD simple | -26,0 | -24,4 | -23,6 | -37,7 |
| MD coupé | -22,9 | -22,9 | -23,6 | -37,5 |
| Syntakt simple | -39,9 | -40,6 | -42,9 | -41,6 |
| Syntakt coupé | -23,0 | -23,1 | -24,0 | -41,2 |

`tailGain` baisse la queue sous un Hit plus fort (1 à 127 gagne 13 à 17 dB). Sous un Hit plus faible, elle tombe au gain du Hit :
compenser au-dessus de 1 dégrade 127 à 64 de 1,9 dB et ne gagne rien sur 127 à 1, donc borné, pas corrigé.

**Hors de Q12, bornés** : gigue du moteur en marche, mi-pic de BD MODERN de 0,215 échantillon à 46 ms de retrigger contre 0,008 seul, le
passe-haut de BASE/WIDTH laisse la queue hors de zéro et l'anneau ajoute le recouvrement ; les échantillons du Hit ne bougent pas.
`changeAndHit` : la rampe bouge avec le Hit, -16,3 à -12,9 dB dedans, -48,1 dB après (mémoire 5 Hz de la chaîne). Changement de type de
filtre Syntakt : fondu de 64 échantillons (-81,6 et -62,7 dB) au lieu des 32 du firmware (-43,5). SRR à 48 kHz : à 0 ce n'est pas un
bypass, son HF propre est -42,9 dB sur piste Syntakt.

**Ticket 10 se trompait sur l'EQ** : son gain est 1/8, pas 1/2, mesuré, commentaire corrigé. DIST à 0 vaut ×8 ; le gain propre de la
chaîne du firmware est 1/4, normalisé à l'unité par les niveaux d'étage de `edDrumsChain.cpp`.

**Dépôt public.** `AttackSeconds[]`, 18 littéraux lus dans la table d'attaque de la section 7 : retiré avant tout commit, remplacé par un
ajustement à 4 coefficients de ln(secondes), à 13 %. Trois mots du firmware écrits par leur formule : 0x016A09E6 vaut √0,5/64, 0x7fdf3b
et 0x4189 valent `wordOf(0.999)` et `wordOf(0.002)`, mots inchangés. Constantes ramenées au plus court que les tests acceptent, faute
de formule : gain de morph 0,995, taux de pôle 0,01769, `DriveFloor` 0,1535.

**Hypothèses à confirmer.** (1) Un drive de firmware là où son firmware l'a reste au débit de base (drive Syntakt pré sur piste Syntakt,
DIST post sur piste MD), et SRR n'est jamais relevé : sinon Q1 ne tiendrait pas au 2x par défaut. (2) RST est tenu actif à chaque Hit,
défaut du firmware non lu. (3) Défaut de filtre de la famille Syntakt : type 7, x/4. (4) L'enveloppe de filtre repart de 0 au trig, et
amp `on=false` est le bypass neutre. La profondeur 0 du §4 est la valeur affichée : stockée décalée, le neutre vaut 64.

Mutations : 31 attrapées sur 32 pendant les corrections, plus celles des deux reprises ; le survivant, nettoyage après le drapeau du
filtre, est équivalent. **Non vérifié** : écoute, Live, machine calme, l'abandon à un débit inconnu (sans test), 88,2 et 96 kHz, les
débits croisés au-delà du gain, de Q6 et de Q12.

Écartés : table d'emplacements par (place, type) (au plus 1,9 point d'après le calcul, seulement sur kit alterné, 60 à 80 lignes,
ticket 25) ; racine partagée avec `stFastCpVintage` (arrondi différent) ; chemins sans rampe de Multimode et ThreeBand ; `blend` SSE ;
TrackAmp à 4 voies. Aucun mesuré.

## Références

Spécification §4. Ticket « Chaîne de piste ».

## Noté par le ticket 10 (2026-11-05)

- Un étage traite tout le `Block`, or la spec donne à chaque piste ses emplacements. Le plus simple : copier le bloc, traiter la
  copie, reprendre les voies choisies, à mesurer d'abord ; si le coût lâche, `process(Block&, uint16_t _lanes)`, qui saute les groupes
  sans voie choisie et mélange au `store`. Pas de classe de base.
- Les gains du DSP sont gardés, et ne s'annulent que dans l'ordre du firmware : EQ ×1/2, filtre ×1/4 avec 4 échantillons de retard,
  DIST ×8 et plus avant son écrêtage. Remplacer l'EQ par Off ou par l'EQ 3 bandes fait sauter le niveau : ce ticket possède un
  ajustement par emplacement, avec des constantes nommées. Mesurer le gain de DIST plutôt que le supposer.
- DIST, placé en pré sur une voix Syntakt pleine échelle : le décalage boucle à 56 bits, donc un échantillon à |gain × x| ≥ 0,5
  se replie sur le rail opposé. Gardé, c'est le son de l'étage, mais la chaîne doit le savoir.
- `set` ne limite pas : le code qui ajoute enveloppe et LFO reste dans 0 à $3FFF, sinon les tables sont lues hors bornes.
- Point plat de l'EQ : Kit 63, le 64 du boot fait déjà +0,24 dB.

## Noté par le ticket 11 (2026-11-06)

Avant ou après les anneaux, la question reste ouverte. Aujourd'hui `Ring::write` écrit dans l'anneau du bloc de la voix, niveau,
vélocité et fondu compris. Mettre la chaîne avant l'anneau : un Hit déplace la position d'écriture (de ±31 échantillons en MD,
jusqu'à un bloc en Syntakt), et une piste Syntakt peut avoir deux voies à `at` différents, donc un `Block` ne porte pas 16 voies
alignées. Option à mesurer : un bloc de 16 voies lu des anneaux sur une grille fixe, puis la chaîne, sans latence ajoutée
(arithmétique, pas mesuré). Prix : une seconde histoire FIR côté Syntakt, un décalage par voie pour les enveloppes, et le niveau, la
vélocité et le fondu à déplacer après la chaîne. Choke : le fondu à l'anneau laisse la queue de la chaîne, à décider. `m_block`
a été retiré du moteur faute d'usage : ce ticket devra le rendre.

Le `Block` a été sorti dans `elektronBlock.h`.

## 2026-11-06 : hypothèses résolues

Recherche sur le portage exact et l'état de boot de l'OS 1.41, routines de l'OS exécutées sur cet état. Tout ce qui suit en dérive et reste hors du dépôt pour les valeurs brutes.
1. Drives de firmware au débit de base : **confirmé**, ni le Syntakt ni le MD ne suréchantillonnent. `Chain::raised` est juste.
2. RST (AENR) : **confirmé pour l'init** (activé). Il commande aussi le fondu de filtre et l'effacement de BASE/WIDTH. Coupé : identique sur un Hit seul, -32 à -77 dB à Hits rapprochés. L'import de Sounds devra le rendre paramétrable. Le MD n'a pas de BASE/WIDTH : ce cas n'existe qu'en emplacement ajouté.
3. Filtre par défaut : **réfuté**. L'init du firmware est type 0 Lowpass, FREQ 127, DEC et REL 64 ; la chaîne a type 7 OFF, DEC et REL 0. WIDTH varie selon la machine à l'init. Écoute : CY ALLOY (-16,0 dB, niveau +0,75 dB) et SD BASIC rien entendu, BD MODERN 5/8. Type 0 retenu par fidélité à la spécification, à faire.
4. Enveloppe et ampli : FENR vaut **0 après INIT TRACK SOUND**, 1 sur les kits d'usine : descripteur et manuel ont tous deux raison. Le code suit 1 : un champ `reset`, à 0 par défaut, à faire. `on=false` est neutre sur les machines AHD mais **réfuté** sur SY TONE, SY CHORD, SY DUAL VCO et SY BITS, en ADSR à l'init avec voix tenue jusqu'au relâchement : +3,5 à -7,0 dB. Défauts d'ampli différents aussi. À faire.

Trouvé en passant : `Machine::indexDescriptors` garde le premier descripteur d'un id, le firmware le dernier, donc le kit par défaut du portage a AmpMode ADSR et voix tenue sur toutes les machines, +2,8 à +17,0 dB. À corriger à part, la référence bouge.

Q6 sur MD DIST : écarté, l'utilisateur ayant suivi la recommandation, DIST étant l'étage de la machine. `HitDelay` reste sans gigue, décalage de drive traité par le rééchantillonneur à phase minimale.

## 2026-11-06 : défauts de famille faits

`syntaktChain` tient ce que pose l'init de l'OS : filtre type 0, DEC et REL 64, FENR 0, ampli actif en AHD à 0, 64, 127, 100, 32 ; SY BITS en ADSR, voix tenue jusqu'au relâchement (`Chain::holdsVoice`). L'ampli reste actif pour le fondu de l'ancien Hit au retrigger : pire AHD de 72,7 / 56,2 à 84,7 / 87,2 dB contre le portage. Machines tenues : 58,7 dB sur une fenêtre, borné à 58, cause déduite. Une constante de -123 dBFS, non expliquée, est retirée des fenêtres de Q3 ; les deux chiffres sont imprimés.

## État (2026-11-11)

Défauts d'ampli de la famille Syntakt changés à la demande de l'utilisateur : une piste neuve en AHD a ATTACK 0, HOLD 0
et DECAY 126, la plus longue qui se ferme (127 ne se ferme jamais) ; une machine qui démarre en ADSR garde 0, 64, 127,
100, 32. Mesuré par `edDrumsAmpTest` aux défauts des machines, chute à -40 dB : BD MODERN 1 870 ms seule, 1 694 ms avec
l'ampli neuf (90,6 %) ; SD BASIC 185 et 184 ms ; CY ALLOY 1 774 et 1 635 ms (le « CH » du rapport compris comme CY
ALLOY, seule cymbale de la famille). L'ampli d'avant (HOLD 64, DECAY 127) ne rallongeait rien : à ces défauts, c'est le
DECAY 64 de la machine qui fait sonner BD MODERN et CY ALLOY près de deux secondes, et aucun DECAY d'ampli fini ne laisse
la machine finir seule à 95 % près. Les états et kits gardent leurs valeurs : un état à HOLD 64 et DECAY 127 se charge
et joue comme avant (`edDrumsProcessorTest`). `edDrumsChainTest` suit les nouveaux défauts ; son contrôle d'unité de la
chaîne Syntakt mesure les étages sans l'ampli, qui décroît maintenant dès le Hit.
