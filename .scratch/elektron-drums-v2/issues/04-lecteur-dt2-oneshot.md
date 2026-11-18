# Lecteur DT2 du Sampler en ONESHOT

Type: AFK
Status: done
Assignee: Claude (session 2026-11-18)
Blocked by: 01

## Quoi

Le Sampler gagne un troisième lecteur, DT2, à côté de 12-BIT et VINTAGE qui restent tels quels. GRAIN (Machine 9)
devient un choix de trois ; un set existant migre à la lecture (0-63 : 12-BIT, 64-127 : VINTAGE). Avec le lecteur DT2,
MODE vaut ONESHOT dans ce ticket ; STRT + LEN, LOOP, PLAY à quatre modes, TUNE ±5 octaves. Mono (un sample stéréo est
sommé), 48 kHz puis `fast::Host`. Lois de l'oracle : interpolation polyphasée à 6 prises et 256 phases, 64 échantillons
à 96 kHz puis décimation 2:1 récursive ; LEV = 2 × (LEV/128)² × courbe de vélocité, rampé sur le bloc ; hauteur
2^(((TUNE − 64) + (note − 60))/12), bornée à ±64 demi-tons. La page ne montre que les réglages du lecteur choisi.

D'abord : relever à l'oracle (`elektron-work\tools\dt2\dt2_voice_oracle.py`) l'ordre des valeurs de PLAY.

## Critère de fin

- Jugé contre les vecteurs ONESHOT de `vectors-dt2` par les tests Q.
- Un set v1 relu garde son lecteur et son son.
- Même son à 44,1 et à 48 kHz (tests Q de `research/19-qualite-audio.md`).
- Aucun set existant ne change de son.
- Coût mesuré sur machine calme et rapporté face à son guide (spécification v2, §3) ; le pire kit tient sous 15 %.
- Gate vert, nouveau test inscrit dans sa liste.

## Décisions et relevés (2026-11-18)

Décidé avec l'utilisateur avant d'écrire :

- **Chemin Syntakt.** Le lecteur DT2 est une machine `syntakt::fast` à 16 voies (`Dt2Player`, modèle SP TWINSHOT :
  scalaire par voie, samples donnés par l'appelant), une `Source` de plus du moteur. Une piste Sampler dont GRAIN
  choisit DT2 passe sur ce chemin : voies, alignement de `HitDelay` par phase, chaîne à 48 kHz, lecture vers 44,1 par
  `Resample48To44`, tout existant. Ses réglages de chaîne passent de `m_mdChain` à `m_stChain` (mêmes types) au
  changement de grain, sans retour aux défauts. Écarté : un lecteur scalaire dans le chemin Machinedrum avec un
  rééchantillonneur par piste, qui refaisait l'alignement sous-échantillon.
- **GRAIN en tiers :** 0-42 12-BIT, 43-85 VINTAGE, 86-127 DT2. Version d'état 7 et de Kit 5 ; un ancien GRAIN migre à
  la lecture (< 64 → 21, ≥ 64 → 64), comme `takeRoms` et `takePageOrder` : aucun set ne change de son.
- **Page du grain, nommé CLEAN** (jamais DT2 à l'écran : la règle des noms maison, que `edDrumsEditorTest` fait
  respecter) : Machine 1 TUNE (v − 64 demi-tons, ±5 octaves), 3 MODE (ONESHOT seul dans ce ticket), 4 LOOP, 5 STRT,
  6 LEN, 7 PLAY (FWD, REV, FWD LOOP, REV LOOP, l'ordre du manuel : la sonde de la ColdFire donne les deux drapeaux,
  pas leur codage) ; 2 et 8 cachés. Les emplacements sont ceux du lecteur 12-BIT dont les défauts conviennent (PTCH 64,
  HOLD 0, BRR 0, STRT 0, END 127, REPEATS 0 → FWD) : un set qui passe au grain CLEAN par un Kit, l'hôte ou Push joue
  avant, en ONESHOT, sans que l'éditeur ait à réécrire quoi que ce soit ; DECAY (127) et REPEAT GAP (64) restent
  inoccupés. STRT et LEN aux emplacements de START et END : un LFO ou un lock qui les visait les vise encore. Le LEV du DT2 (2 × (LEV/128)² × vélocité) n'est pas porté : niveau et
  vélocité de Lockstep viennent après la chaîne (spec v1). Le gain DC du noyau (0,4045) est compensé : un sample à
  pleine échelle joue à pleine échelle, comme sur les deux autres lecteurs.

Relevé à l'oracle (`vectors-dt2`, scripts `dt2_kernel_*.py`, `dt2_decim*.py` dans le scratchpad de la session, rien
du firmware recopié) :

- **Position.** La sortie m du tampon 96 kHz lit la source à `start + 2 + m × pas`, les six prises aux index
  `floor − 2` à `floor + 3`, le noyau centré sur la troisième ; `start = STRT/120 × longueur` (borné à longueur − 141),
  quel que soit le pas (1,97 à 2,05 mesuré sur 15 cas). Lecture à l'envers et boucle : recherche 03.
- **Noyau.** Pas un sinus fenêtré (Kaiser, Lanczos, Hann : 24 dB au mieux), ni une conception aux moindres carrés
  classique (28 dB) : son premier lobe négatif s'étend jusqu'à ±2,6 sans second passage par zéro. Retenu : une
  B-spline cubique **paire** sur [−3, 3], nœuds tous les 0,25, **14 coefficients** ajustés aux moindres carrés
  linéaires au tampon 96 kHz du cas `os_noise_tune_frac` (pas 0,5108, toutes les phases), 256 phases au plus proche.
  Face au tampon 96 kHz de l'oracle : 47 à 50 dB sur les Hits (unisson, ±12, note 72, STRT 30), 46 sur le bruit ;
  réponse à ±0,05 dB jusqu'à 18 kHz, +0,09 à 20, +0,37 dB à 23 kHz (sous le décimateur) ; gain DC 0,4045. Une
  B-spline asymétrique n'apporte que 2 dB (centre de masse 0,013) : le noyau est symétrique.
- **Décimateur.** Identifié au tampon 96 kHz (FIR de 64 prises, 104 dB) : `H(z) = ½ [A(z²) + z⁻⁷]`, la branche
  impaire un retard pur, la branche paire un **passe-tout d'ordre 4** (|E| = 0,5 à ±0,001 de 0 à Nyquist, retard de
  phase 3,5), ses quatre coefficients — les « quatre coefficients en mémoire » de la recherche 03 — ajustés :
  0,4810, −0,0985, 0,0332, −0,0119 (61 dB sur la branche identifiée). Un demi-bande à deux branches passe-tout
  (ajusté sur la réponse en amplitude seule) donnait la même amplitude mais 35 dB dans le temps : faux. La branche
  impaire sort la paire d'il y a 4 (`z⁻⁷` = 3 paires et demie) ; une voix arrêtée écrit des zéros, la queue du
  décimateur ne sonne pas.
- **Fin.** Le dernier bloc qui sonne est rendu avec un fondu linéaire (63 − m)/63 sur ses 64 sorties, puis la voix
  s'arrête. Corrigé par le ticket 05 : un bloc sonne tant que sa dernière sortie plus un échantillon (le pas s'il est
  plus grand) reste dans la fin ; le « 64 échantillons quel que soit le pas » écrit ici ne tenait qu'au pas 0,5.
- **Envers.** Première lecture à `fin − 6` (relevé 4796,06 pour 4800), le pas négatif, le même noyau.
- **Modèle entier** (noyau, positions, fin, décimateur, en Python, `dt2_model2.py`) face à la colonne `voice` des 17
  cas ONESHOT : Hits 50 à 64 dB (unisson 55,6, −12 55,2, STRT 30 64,1, envers 59,7, boucle 50,7, niveaux 50,2),
  sinus 48 à 53 dB, bruit 34, impulsion 31 (l'approximation du noyau sur le large bande). Au-dessus de ce que Q4
  demande (enveloppe 0,1 dB, hauteur 1 cent) ; le test C++ reprend ces cas et ces chiffres comme seuils.

## Avancement (2026-11-18, 01:40, non commité)

Fait, dans l'arbre de travail, sans commit ni garde-fou : `edDrumsDt2Player.h/.cpp` (la machine `syntakt::fast` à
16 voies : noyau, positions, fin, boucle, envers, demi-bande) et `edDrumsDt2PlayerTest.cpp` (inscrit dans
`edDrums/CMakeLists.txt` et dans la liste du garde-fou, `DT2_VECTORS` ajouté à `gate.ps1`), qui passe : les 14 cas
ONESHOT à 47,9-58,9 dB (bruit 34,2, impulsion 31,1), enveloppe ≤ 0,023 dB, hauteur exacte ; 16 voies et 4 voies
égales à 1 ; Q15 à 44,1 kHz : enveloppe 10 ms à 0,069 dB, tiers d'octave à 0,191 dB ; coût 1,55 % d'un cœur pour
16 voies qui jouent (≈ 0,1 % par voie, le guide du Sampler). `Chain::settings()` ajouté pour le déplacement de chaîne.

Suite (2026-11-18, 02:30) : intégration faite. Le moteur a une `Source` de plus (`Dt2Source`, le lecteur après les
neuf machines), `sourceOf` y envoie le Sampler, `syntaktPath` (famille Syntakt ou grain DT2) remplace
`family == Syntakt` dans noteOn, setParameter, takeMoves et lock ; `playSyntakt` donne au lecteur ses index 0-5 ;
`Track::stChain` dit quelle Chain tient les réglages et `moveChain` les déplace au changement de grain (et au retour
sur une machine Machinedrum), les mouvements des LFO avec ; `takeLane` et `setSample` donnent à la voie le slot
de sample de la piste. GRAIN en tiers (`vintage::grainOf`, `GrainValues`), migration `takeGrain` (version d'état 7,
Kit 5, sons de piste compris), noms de la page DT2 (`names::control` avec le grain), textes (`samplerText` avec le
grain : demi-tons, PLAY, ONESHOT, %), trois segments dans VOICE. Tests : migration dans `edDrumsProcessorTest`
(état 6 et Kit 4 : 100 → 64, 10 → 21 ; état 7 inchangé), Hit du grain CLEAN à `HitDelay` + onset du lecteur (83
mesuré, 81 + 2) et chaîne portée à travers le changement de grain dans `edDrumsEngineTest`, le grain CLEAN à 48,
88,2, 96 et 192 kHz dans `edDrumsRateTest`, les tests qui demandaient VINTAGE par 100 ou 127 demandent
`GrainValues[1]` (100 est devenu CLEAN, ce que `edDrumsRateTest` a montré). Revue de code faite ; deux points mis de
côté : le grain passé à la main dans trois API de noms et de textes ; PLAY d'abord sur Machine 2 héritait DECAY 127
(REV LOOP), réglé en posant PLAY sur Machine 7 (REPEATS 0).

Revue d'architecture (2026-11-18, 03:30) : trois défauts corrigés — `takePageOrder` comparait aux versions courantes
et aurait rejoué la permutation des pages sur un état 6 (versions en littéraux désormais, test avec une piste Syntakt
dans l'état 6) ; la voix ROM restait périmée au retour du grain CLEAN (la voix Machinedrum reçoit maintenant chaque
valeur et chaque mouvement quel que soit le grain) ; la hauteur était figée au trig (pas recalculé à chaque bloc : LFO
et locks sur TUNE agissent). Plus : le lecteur relit son slot de sample à chaque bloc (un sample remplacé sous une voix
qui s'éteint ne laisse pas de pointeur libéré), la table du noyau est bâtie une fois par processus, le test d'éditeur
clique les trois segments, la page se reconstruit au changement de grain. Mis de côté : le même fait « piste sur le
chemin Syntakt » porté par `m_stMask`, `stChain` et `syntaktPath` ; `m_dt2Slot` à côté des trois tableaux VINTAGE ;
les boucles de mouvements de `moveChain` et `setMachine` ; le texte de fréquence du drive DAC sur une piste CLEAN ;
une automation d'hôte enregistrée sur Machine 9 avant les tiers (127 = VINTAGE) rejoue CLEAN.

## Réponse (2026-11-18)

Fait : le Sampler a un troisième grain, **CLEAN**, le lecteur DT2 en ONESHOT (`edDrumsDt2Player.*`), sur le chemin
Syntakt ; GRAIN en tiers avec migration (état 7, Kit 5) ; page, noms et textes par grain. Les décisions, relevés et
mesures sont dans les sections ci-dessus. Critère de fin : les 14 cas ONESHOT de `vectors-dt2` jugés par
`edDrumsDt2PlayerTest` (47,9-58,9 dB sur Hits et sinus, enveloppe ≤ 0,023 dB, hauteur exacte) ; un set v1 relu
garde son lecteur (`takeGrain`, `edDrumsProcessorTest`) ; même son à 44,1 et 48 kHz (Q15 : 0,07 dB d'enveloppe,
0,19 dB par tiers d'octave ; `edDrumsRateTest` à 88,2, 96 et 192 kHz aussi) ; aucun set existant ne change de son
(migration testée, VINTAGE et 12-BIT inchangés). Coût : 1,55 % d'un cœur pour 16 voies qui jouent, ≈ 0,1 % par voie ;
le pire kit au garde-fou du 2026-11-18 (Blender à 10,5 cœurs) : 11,80 % au total, moteurs 7,37, chaîne 4,01. Gate
vert, 76 tests, `edDrumsDt2PlayerTest` dans sa liste (`DT2_VECTORS`). Hors de ce ticket : GRID, REPITCH, WERP,
SLICE, STRETCH (05, 11, 12, 13), la couche sample (06).

## Références

Spécification v2, §5.1 (`../elektron-drums/spec-v2.md`) ; ses tickets de décision dans `../lockstep-post-v1/issues/`.
