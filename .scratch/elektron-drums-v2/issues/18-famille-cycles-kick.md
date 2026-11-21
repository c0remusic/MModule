# Famille Cycles avec KICK seul

Type: AFK
Status: done
Assignee: Claude (session 2026-11-20)
Blocked by: 17

## Quoi

Troisième famille, Cycles, à côté de Syntakt et MD, avec sa première machine, KICK. PTCH, DEC, COLR, SHPE, SWEP,
CONT dans Machine 1-6, PUNCH et GATE dans Machine 7-8 ; plages et défauts des fiches de paramètres de l'OS 1.13. Chaîne
par défaut de la famille : Cycles dist en post, pas de filtre. Vélocité : gain linéaire après la voix. GATE allumé : la
voix tient tant que la note tient (note-off de l'hôte ou durée du trig). Les deux LFO de Lockstep ; le LFO du Cycles
n'est pas porté. Port SIMD, 16 pistes sur les voies, 48 kHz puis `fast::Host`.

## Critère de fin

- Port jugé contre les vecteurs KICK de `vectors-cycles` par les tests Q.
- GATE : la voix tient et se relâche à la note-off, à l'échantillon près.
- Hits exacts, latence et gigue inchangées.
- Coût visé : ≈ 0,1 % d'un cœur par piste active.
- Même son à 44,1 et à 48 kHz (tests Q de `research/19-qualite-audio.md`).
- Aucun set existant ne change de son.
- Coût mesuré sur machine calme et rapporté face à son guide (spécification v2, §3) ; le pire kit tient sous 15 %.
- Gate vert, nouveau test inscrit dans sa liste.

## Relevés à l'oracle (2026-11-20)

Hors dépôt : `elektron-work\cycles-kick\KICK.md` (loi), `kick_exact.py` (les routines de l'OS en entiers Python : 24
vecteurs sur 24 au bit près), `kick_model.py` (la loi en flottants, tables refaites par formule : 97,7 à 131 dB,
`score_float.txt`), `formulas.json` (les ajustements), `gen_curves.py` (qui écrit `cyFastKickCurves.inc` du dépôt) ;
vecteurs GATE neufs dans `vectors-cycles-gate` (`tools\cycles\vectors_cycles_gate.py`).

- Quatre opérateurs sinus. L'op 0 se module par son dernier échantillon (indice 4 F, SHPE). L'op 2, onde à cinq
  harmoniques sous sa propre enveloppe (1 vers 0,75), est modulé par l'op 1. Leur mélange (gains COLR et SHPE) passe
  sous l'enveloppe d'index (CONT, tenue un bloc de plus) et module la phase de l'op 3, à la note, que le sweep (SWEP)
  monte au trig. Puis le drive (PUNCH : ×2 dans un écrêtage dur, gain qui creuse à 0,5 et revient), un clic (rampe de
  479 échantillons × niveau SHPE, passe-haut 250 Hz, passe-bas 3 441 Hz), l'enveloppe d'amplitude (DEC), sortie / 2.
- La hauteur doit rester dans les entiers de l'OS : demi-tons en octaves par × 0x0AAAAAAB / 2³¹ (la note 60 tombe sur
  le pas 511 de 2 048, pas 512), puis une chaîne de produits Q31 tronqués. En flottant, 54 à 60 dB perdus.
- Trig vu au bloc qui suit le masque (la voix y descend à 0), son au bloc d'après, comme le contrat des machines de
  `syntakt::fast`. Vélocité hors de la voix (gain linéaire après, déjà celui du chemin Syntakt).
- GATE allumé : l'ampli tient ; un note-off dans un bloc sans trig libère au bloc suivant, la décroissance part du
  bloc d'après. Éteint, le note-off ne change rien.
- SHPE au-delà de ~0x4c00 : la rétroaction de l'op 0 devient chaotique (β jusqu'à 2,75 rad) ; aucune autre
  implémentation que les entiers de l'OS ne suit échantillon par échantillon. Jugé par tiers d'octave.

## Port (2026-11-20)

- `source/elektron/cycles/cyFast/` (`cycles::fast::Kick`, bibliothèque `cyFast`) : machine de `syntakt::fast`,
  16 voies, blocs de 32 à 48 kHz, `Host` pour 44,1 kHz. Voix par voie en flottant 32 bits, phases en accumulateurs
  24 bits et incréments dans les entiers de l'OS. Courbes des paramètres en constantes ajustées
  (`cyFastKickCurves.inc`, ~280 nombres : nœuds et morceaux sur sept tables, formules fermées pour COLR et DEC ;
  choix de l'utilisateur le 2026-11-20), aucune table du firmware.
- Famille **FM DRUMS** (« FM PERC » est déjà la famille EFM du Machinedrum dans le navigateur), machine **FMD KICK**
  (« FM KICK » est l'EFM-BD). Ajoutée à la fin de la liste des machines : aucun choix enregistré ne bouge.
- Machine 1-8 : PTCH (40 à 88, ±24 demi-tons), DEC, COLR, SHPE, SWEP, CONT, PUNCH, GATE (allumés à 64) ; défauts des
  fiches de l'OS (64, 28, 10, 16, 16, 24, 0, 0). Chaîne par défaut : Cycles dist en post à 60 (au repos), sans filtre,
  à 48 kHz sur la chaîne du Syntakt ; suréchantillonné comme les autres drives (choix de l'utilisateur).
- Moteur : `Family::FmDrums` sur le chemin des voies (`onLanes`), source après le lecteur DT2, Machine 1-8 droit à la
  voix (ni OVERDRIVE ni HOLD du Syntakt).

## Réponse (2026-11-20)

`cyFastKickTest` (au garde-fou ; `CYCLES_VECTORS`, `CYCLES_GATE_VECTORS`) : 21 cas contre l'oracle, 82,6 à 121,5 dB,
pire fenêtre de 10 ms ≥ 76 dB (bornes 60 et 50) ; SHPE au maximum, chaotique, à 0,71 dB par tiers d'octave (borne
1 dB). GATE : note-off au bloc 40, au bloc du trig, tenu, jamais, GATE éteint, au même niveau que le reste. 16 voies
et 4 voies jouent les échantillons d'une seule. Q15 : enveloppe 0,018 dB, tiers d'octave 0,327 dB. Moteur
(`edDrumsEngineTest`) : un Hit sur FMD KICK aux 12 positions, mêmes échantillons, gigue 0, début à −1 échantillon de
HitDelay comme BD MODERN ; couche à HitDelay (`edDrumsLayerTest`) ; même son à 44,1 et 48 kHz (`edDrumsRateTest`).
Coût : 0,14 % d'un cœur par voie qui joue sur machine calme (0,19 % sous charge ; guide ≈ 0,1 %) ; scalaire par voie,
l'op 0 bouclant sur son échantillon.

Points ouverts :
- Note autre que 60, mots changés pendant une note (le lisseur de l'OS n'est pas porté : la voix lit les mots tels
  qu'ils arrivent), retrig pendant que la voix sonne : écrits d'après le code, sans vecteur.
- Le second masque lu comme note-off : déduit du code de la boucle audio, pas d'un OS démarré.
- Coût au-dessus du guide ; une version SIMD sur les voies reste possible (les lectures de tables y seraient
  scalaires).

Sets existants : rien ne change (famille ajoutée à la fin de la liste, mêmes indices).

## Références

Spécification v2, §6 (`../elektron-drums/spec-v2.md`) ; ses tickets de décision dans `../lockstep-post-v1/issues/`.
