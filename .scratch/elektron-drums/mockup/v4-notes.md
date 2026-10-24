# Maquette v4 — notes

`v4.html` (1600x1000) part de v3a et reprend tout v3 : pastille « + » sur les anneaux, boîtes qui s'ouvrent au
clic sur l'en-tête, anneau LFO / ASSIGN / poignées à glisser, LIVE · LOCK REC, ▶ et LED par onglet de piste, AMP
étroite. `v4-var-fidelity.html` : MACHINE + FILTER + LFO rendus trois fois (plat, texturé, photoréel approché).
v3a, v3b et leurs notes restent intacts.

## Changé depuis v3

- **Deux modes de Lock à la fois.** Page piste : boîte STEPS fixe en bas, sélection collante (v3a). Onglet SEQ :
  sélecteur 16 pistes, pas, détail du pas, LOCK LANES (5 pistes dessinées) et vue piste réduite cliquable (v3b).
  Même sélection, mêmes Locks des deux côtés. Menu « + » : LFO 1, LFO 2, Lock sur les pas choisis, piste de Lock.
- **Vrais noms.** MACHINE affiche les 8 paramètres de la machine choisie (table JS) :
  - Syntakt, les 12 machines numériques : ordre A-H du manuel (annexe A), noms longs et valeurs par défaut des
    descripteurs du firmware (`0x4022D59C`, groupe = id de machine, dump `elektron-work/scratch-arch/plugin/descriptors.txt`).
  - Machinedrum, toutes les machines TRX / EFM / E12 / P-I / GND / ROM : noms d'écran de `mdmachines.cpp`.
    Familles non confirmées (Rytm, Model:Cycles, DT2, Digitone II, Analog-approx) : noms marqués « ? ».
- **Filtre.** « ST 8? » et l'ancienne liste LP2/LP1/BP/... étaient faux. Pistes numériques Syntakt (manuel 10.5.7) :
  LP, HP, EQ1-EQ5, OFF (8 types, 0-7 dans le port). DT2 : MULTI, LP4, EQ, COMB-, LEGACY, COMB+. RESO devient
  GAIN sur un EQ, comme sur la machine.
- **Enveloppes, LFO, AMP aux noms Syntakt.** Attack Time, Hold Time / Env. Delay, Decay Time, Sustain Level,
  Release Time ; mode AHD / ADSR, ENV RESET. LFO : Speed, Multiplier, Fade In/Out, Start Phase, Depth.
  AMP : Volume, Pan, Portamento Time, Portamento (Accent et Vel→Vol, inventés, retirés).
- **FX** : DELAY et REVERB (Syntakt | DT2 : mêmes noms dans les deux manuels), CHORUS (DT2, 12.4),
  PHASER / FLANGER (Octatrack B.5 / B.6 | Own +, PHASER ↔ FLANGER change les anneaux), SENDS, FX RETURNS.
- **MASTER** : COMPRESSOR DT2 (THR, ATK, REL, MUP, SCF, MIX, VOL, 8 ratios, SCS, mètre de réduction gradué),
  OVERDRIVE DT2 (MOVD + courbe), MAIN OUT (mètre gradué, fader), MIXER 16 voies (pan, M/S, fader, mètre stéréo,
  sortie Main / Out 01-15).
- **KIT** : banque de 64 slots (courant cerné, ● si modifié), nom + Rename, Save / Save as / Reload / Clear,
  Import / Export, état, tempo avec le pattern ou le kit, résumé 16 lignes (famille, machine, sample, filtre,
  drives, LFO, sortie).
- **Densité** : grille de 4 px (marges et écarts 8), anneaux 48 (principaux) / 36 (secondaires) / 32 (mixer),
  textes à 11 px minimum.

## Non confirmé

- Ordre des 8 anneaux Syntakt : celui du manuel (A-H). Le firmware liste les ids 18-23, 26, 25 : à recouper.
- SY TOY : PART dans les descripteurs, PAR dans le manuel.
- Plages : TUNE ±24 demi-tons (0x28-0x58) ; TYPE de SY CHORD 0-37 ; PNCH tout ou rien. Lus dans les descripteurs,
  pas sur la machine.
- Valeurs par défaut des machines MD : factices (le tableau ne donne que les noms).
- GND-SW, GND-PU, TRX-S2 : absents d'OS 1.63, noms tirés de MDX X.20 (titre marqué « ? »).
- Overdrive DT2 : seul MOVD existe sur le master ; sa courbe est dessinée, pas mesurée.
- Compresseur : SCS IN LR / IN L / IN R = entrée sidechain de l'hôte ? (marqué « ? »).
- Delay et reverb Syntakt : routage FX (direct ou FX bus) non montré, le plug-in n'a pas de bus FX.
- Noms des machines Rytm, Model:Cycles, DT2, Digitone II, Analog-approx : toujours factices.
- SAMPLE LAYER, BAND EQ, Phaser Own : ajouts du plug-in, sans modèle firmware.

## Questions ouvertes

1. Garder les deux modes de Lock, ou l'un des deux devient secondaire ?
2. Libellés des anneaux : noms longs (actuels) ou noms courts de la machine (SWEP, PNCH) ?
3. Fidélité : plat (a), texturé (b) ou photoréel (c) ? (c) en vrai = rendu 3D en filmstrips, actifs lourds.
4. Toujours ouverts depuis v3 : taille par défaut (100 ou 125 %), trig du ▶ d'onglet en LIVE, couleurs Lock / LFO.
