# GRID et REPITCH du lecteur DT2

Type: AFK
Status: done
Assignee: Claude (session 2026-11-18)
Blocked by: 04

## Quoi

MODE du lecteur DT2 gagne GRID et REPITCH, avec la sémantique du DT2. Vecteurs relevés à l'oracle d'abord (aucun
n'existe encore), puis port.

## Critère de fin

- Vecteurs GRID et REPITCH enregistrés hors dépôt ; le port les tient par les tests Q.
- Même son à 44,1 et à 48 kHz (tests Q de `research/19-qualite-audio.md`).
- Aucun set existant ne change de son.
- Coût mesuré sur machine calme et rapporté face à son guide (spécification v2, §3) ; le pire kit tient sous 15 %.
- Gate vert, nouveau test inscrit dans sa liste.

## Relevés à l'oracle (2026-11-18)

Scripts hors dépôt : cas ajoutés à `elektron-work\tools\dt2\dt2_voice_oracle.py` et `regen_voice_05.ps1` ; sondes
`elektron-work\dt2-voice\probe_24e9_type.py`, `probe_grid*.py`, `probe_end*.py`. Rien du firmware recopié.

- **Le banc écrivait mal les entiers des boutons.** `FUN_1c24e9` copie, pour tous les types de machine, le mot de trame
  >> 8 de chaque bouton en entier : le premier à +0x18 et +0x24, le deuxième à +0x1c, le troisième à +0x14 et +0x20
  (`probe_24e9_type.py`). L'oracle ne posait que +0x14 : GRID jouait la tranche par défaut, quels que soient ses réglages.
  Corrigé dans l'oracle ; les vecteurs ONESHOT et STRETCH n'en dépendent pas (tranches et BARS lus ailleurs).
- **REPITCH** (sélecteur 3) : pas = (fin − début) / (2^BARS × 4 temps au tempo, en sorties à 96 kHz), début et fin ceux
  d'ONESHOT (STRT, LEN), TUNE et note non lus. Mesuré : le pas stocké vaut la formule à l'unité près sur 7 cas (BARS 0
  et 1, 120 et 240 BPM, STRT 30 LEN 60, TUNE 76, note 72, envers). BARS est un entier sur la machine (2^BARS par
  décalage) ; Lockstep donne aussi 1/8 à 1/2 mesure.
- **GRID** (sélecteur 4, « SLICED SMP ») : 4 << G tranches égales sur tout le sample (G l'entier du troisième bouton,
  64 au plus), la tranche jouée l'entier du premier (à partir de 1), le deuxième le nombre de tranches qui la suivent
  (0 : une) ; le pas d'ONESHOT, TUNE lu. Pas de calage au passage par zéro observé : les débuts tombent à k × L / n.
- **Arrêt, toutes machines** (corrige ce que le ticket 04 disait) : un bloc sonne tant que la position de sa dernière
  sortie plus un échantillon (le pas s'il est plus grand) reste dans la fin (`probe_end2.py`, pas 0,25 à 2) ; le dernier
  qui sonne est fondu. Le « 64 échantillons quel que soit le pas » du ticket 04 tenait au pas 0,5 et, au pas 1, sur la
  queue silencieuse du Hit seulement : GRID à +12 l'a démenti.
- **Envers depuis un début intérieur** (tranche, STRT > 0) : la lecture descend jusqu'à 26,5-54,5 échantillons sous le
  début (32 retenu), jamais sous 0, d'où un bloc de plus qu'à l'endroit ; ce dernier bloc, fondu, lit sous le début une
  valeur que les sondes n'ont pas identifiée (sous le fondu un quasi-constant de 0,11 : ni le sample, ni des zéros, ni
  la fin de la tranche). Le test juge ces 5 cas jusqu'à ce bloc et vérifie leur longueur.

## Port (2026-11-18)

`Dt2Player` gagne les deux modes. MODE sur Machine 3 (un sixième chacun, l'ordre du Digitakt II : ONESHOT, GRID,
REPITCH, puis WERP, SLICE, STRETCH, qui jouent ONESHOT et s'affichent ONESHOT jusqu'aux tickets 11 à 13 ; leurs
sixièmes restent réservés). Machine 4 : LOOP sur ONESHOT, GRID (4, 8, 16, 32 ou 64 tranches) sur GRID, BARS (1 à
16 mesures, 0 la valeur par défaut, une mesure) sur REPITCH ; Machine 5 et 6 : START et LENGTH, ou la tranche jouée
et le nombre de tranches jouées sur GRID. REPITCH ne lit ni TUNE ni la note et lit le tempo de l'hôte à chaque bloc
(`Engine::setTempo` vers le lecteur). La règle d'arrêt et la lecture inverse sont celles relevées ci-dessus. La page
CLEAN nomme ses réglages par MODE (`vintage::SamplerPage`, `names::control`, `samplerText`) ; un clic sur CLEAN depuis
un autre grain remet MODE à ONESHOT (Machine 3 était HOLD).

## Réponse (2026-11-18)

Fait. `edDrumsDt2PlayerTest` juge 35 cas contre l'oracle (14 ONESHOT, 7 REPITCH, 12 GRID, 2 inverses depuis un début
intérieur) : 47,9 à 62 dB sur Hits et sinus, enveloppe ≤ 0,1 dB, hauteur exacte ; les 5 lectures inverses depuis un début
intérieur jusqu'à leur dernier bloc (voir plus haut) ; même son à 44,1 et 48 kHz (Q15 inchangé : 0,07 dB d'enveloppe,
0,19 dB par tiers d'octave) ; 16 et 4 voies égales à 1. Coût : 1,46 % d'un cœur pour 16 voies qui jouent, comme
ONESHOT. Aucun set existant ne change de son : 12-BIT et VINTAGE intacts ; un set du ticket 04 sur CLEAN joue ONESHOT
tant que sa Machine 3 est à 0, qui était la seule valeur que sa page proposait. Mis de côté : la borne de lecture
inverse choisie dans un intervalle, le contenu non identifié de ce dernier bloc, un changement de tempo sous une voix
REPITCH non vérifié à l'oracle, une automation de GRAIN vers CLEAN qui garde une HOLD non nulle comme MODE.

## Références

Spécification v2, §5.1 (`../elektron-drums/spec-v2.md`) ; ses tickets de décision dans `../lockstep-post-v1/issues/`.
