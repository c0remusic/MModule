# SNARE, METAL, PERC, TONE, CHORD

Type: AFK puis HITL (écoute)
Status: done
Assignee: Claude (session 2026-11-21)
Blocked by: 18

## Quoi

Les cinq autres machines de la famille Cycles, dans l'ordre de l'OS 1.13, sur le squelette du ticket 18.

## Critère de fin

- Chaque machine jugée contre ses vecteurs de `vectors-cycles` par les tests Q.
- Écoute de contrôle envoyée, notes de l'utilisateur appliquées.
- Même son à 44,1 et à 48 kHz (tests Q de `research/19-qualite-audio.md`).
- Aucun set existant ne change de son.
- Coût mesuré sur machine calme et rapporté face à son guide (spécification v2, §3) ; le pire kit tient sous 15 %.
- Gate vert, nouveau test inscrit dans sa liste.

## Relevés à l'oracle (2026-11-21)

Hors dépôt : `elektron-work\cycles-snare-metal\` (SNARE.md, METAL.md, `sm_exact.py`, `sm_model.py`, `formulas_sm.json`)
et `elektron-work\cycles-perc-tone-chord\` (PERC.md, TONE.md, CHORD.md, `exact3.py`, `float3.py`, `formulas3.json`).
Modèles entiers au bit près sur les 144 vecteurs (18 de `vectors-cycles` et 6 GATE neufs par machine,
`vectors-cycles-gate-<machine>`).

- Le changement de machine lance l'init de la voix (0x400a7ab8) au premier trig : tout est remis à zéro, et le note-off
  du bloc du trig est perdu. Un trig suivant sur la même machine ne la relance pas.
- SNARE : son bruit est l'opérateur 0 du KICK, qui se module lui-même, chaotique à tout SHPE : en flottant il s'écarte
  de 2 à 6 dB par tiers d'octave ; porté dans les entiers de l'OS. METAL : son opérateur 2 aussi, en entiers.
- PERC : ses tables de rapports veulent 1e-9 (la phase intègre l'erreur) : lues en entiers dans la chaîne de hauteur.
- TONE : deux opérateurs à 96 kHz, puis le décimateur demi-bande de l'OS (quatre mots). SWEP au-delà de ~0x4800 :
  rétroaction chaotique (swep7f).
- CHORD : 31 ondes, données du flash que le boot copie en SRAM ; fondu entre deux ondes choisies par SWEP ; accords
  en demi-tons, désaccords, filtre deux pôles sur l'enveloppe de sweep.
- Velocity n'atteint jamais la voix. PTCH : la plage de l'enregistrement de l'OS, 40 à 88, pour les six machines.

Constantes (choix de l'utilisateur, 2026-11-22) : les courbes de paramètres ajustées entrent au dépôt (~3 450 nombres
sur les cinq machines, générés par `gen_curves_sm.py`, `gen_curves_pt.py`, `gen_curves_chord.py` d'elektron-work) ;
des fits dix fois plus lâches faisaient diverger le bruit du SNARE (bandes de 4 à 9,6 dB) et coûtaient 20 à 30 dB à
PERC. Les ondes du CHORD ne sont pas au dépôt : lues dans l'OS de l'utilisateur au chargement.

## Port (2026-11-22)

- `source/elektron/cycles/cyFast/` : `Snare`, `Metal`, `Perc`, `Tone`, `Chord`, ce qu'elles partagent avec `Kick` dans
  `cyFastFm.*` (courbes, tables, lookups de l'OS en entiers, chaîne de hauteur, opérateur à rétroaction en entiers,
  attaques, gain de punch). KICK rend les mêmes échantillons qu'avant (empreintes de 96 rendus de 16 voies).
- `cyFastChordWaves.*` : lit les 32 pointeurs d'ondes et les deux images que le boot copie (0x4000045c) dans la section 3
  de l'OS 1.13 (`section_3_MAIN_OS.bin`, vérifiée par taille et FNV-1a). Lockstep la cherche dans le dossier `roms` de
  ses données (`Library::findChordWaves`) ; sans elle FMD CHORD joue le silence et le dit (MachineData None). Le lecteur
  .syx viendra plus tard (choix de l'utilisateur).
- FM DRUMS : FMD KICK, FMD SNARE, FMD METAL, FMD PERC, FMD TONE, FMD CHORD, ajoutées à la fin de la famille (dernière de
  la liste) : aucun choix enregistré ne bouge. Défauts : enregistrements de l'OS.

## Réponse (2026-11-22)

Contre l'oracle (SNR / pire 10 ms ; bornes 60 et 50 dB) :
- SNARE `cyFastSnareMetalTest` : 99,5 à 117,8 dB ; swep7f chaotique, 0,70 dB par tiers d'octave (borne 1).
- METAL : 73,8 à 103,8 dB.
- PERC `cyFastPercToneTest` : 93,3 à 127,9 dB.
- TONE : 100,7 à 129,5 dB ; swep7f chaotique, 3,47 dB par tiers d'octave, borne 4,75 : l'OS lui-même, sa phase décalée
  de 1 à 7 LSB, tombe à 2,95-4,74 dB de son vecteur.
- CHORD `cyFastChordTest` : 121,4 à 134,4 dB (ondes de l'OS).
- 16 et 4 voies jouent les échantillons d'une ; Q15 (44,1 contre 48 kHz, mesuré sous 18 kHz) : enveloppe ≤ 0,151 dB,
  tiers d'octave ≤ 0,195 dB.
- Coût par voie qui joue (machine non calme) : SNARE 0,153 %, METAL 0,140 %, PERC 0,101 %, TONE 0,161 %, CHORD 0,104 %
  (guide ≈ 0,1 %).
- Moteur : un Hit sur chacune aux 12 positions (`edDrumsEngineTest`, CHORD avec `CYCLES_SECTION3`), CHORD débutant 32
  échantillons après HitDelay (son bloc de trig muet, comme l'OS) ; même son à 44,1 et 48 kHz (`edDrumsRateTest`, sauf
  CHORD).

Écoute (2026-11-23) : l'utilisateur, sur `elektron-work\listen-cycles\page.html` (76 sons des six machines, oracle,
port à 48 kHz et à 44,1 kHz côte à côte) : « ça sonne bien ».

Points ouverts :
- Revenir sur une machine FM DRUMS après une autre ne relance pas l'init de la voix, comme l'OS le ferait (KICK aussi).
- Sans vecteur : notes autres que 60, mots changés pendant une note, retrig sur une voix qui sonne, CHORD SHPE 1 et 2 et
  les fondus montant/descendant.
- Test Q15 du METAL et du CHORD mesuré sous 18 kHz (celui du KICK sur toute la bande) ; fenêtre du CHORD 3,3 s.
- Lecteur .syx de l'OS du Cycles.

## Références

Spécification v2, §6 (`../elektron-drums/spec-v2.md`) ; ses tickets de décision dans `../lockstep-post-v1/issues/`.
