# 03 — Moteur du Machinedrum comme famille de machines

Ticket : [03-moteur-machinedrum](../issues/03-moteur-machinedrum.md). Sources : le code du worktree
(`source/elektron/md/mdEngine/`, `source/elektron/md/mdDrums/mdDrumsEngine.*`, identiques à `main` 49cd5008 pour ces
deux dossiers) et un banc écrit pour ce ticket, hors dépôt :
`C:/dev/elektron-work/scratch-arch/research/mdbench/mdbench.cpp` (lié aux `.lib` Release de
`C:/dev/MModule/temp/cmake_vs22`, flash OS 1.63 UW). Suppositions marquées [INFER].

## Réponse courte

Oui. `md::engine::EngineT` sert tel quel de famille de machines : une instance = 16 voix du DSP2 émulé, une par piste
de l'instrument qui choisit une machine MD. La voix brute avant la chaîne de DSP1 existe déjà (`EngineT::voiceBlock()`),
et le mixeur se coupe (`mixOn = false`). Une piste MD inutilisée sur GND-- coûte presque rien, mais le moteur garde un
coût fixe d'environ 2 % d'un cœur dès qu'il existe. Ensuite chaque piste MD jouée coûte environ 0,85 % pour sa voix et
0,25 % de plus pour sa chaîne. Le DIST et le SRR du MD sont du C++ pur, bit-exact avec DSP1, et coûtent environ 0,03 %
par piste. On peut les sortir comme étages drive, mais il faut ouvrir une entrée dans `TrackFx`. Ils restent exacts
sur l'algorithme, mais à 48 kHz ils ne sonnent pas comme sur le MD.

## 1. Architecture : où couper

- `EngineT::render` (`engine/MdEngine.h`) enchaîne `HostModel::renderBlock` (tick de l'OS dans le 68k + DSP2), puis
  `fxTrack` par piste (`TrackFx` : la chaîne de DSP1 en C++), puis `Mixer::process`, puis le master optionnel
  (`MasterEngine`, deuxième DSP56300).
- **Voix brute** : `EngineT::voiceBlock()` rend « each voice's 32 samples of the last block, before its Track's
  effects » (MdEngine.h). Ce sont les mots que le DSP2 émulé envoie, donc exacts par construction. L'oracle
  (`mdEngineFirmwareTest`, README de mdEngine, « Verified ») compare la sortie *après* la chaîne au firmware complet :
  corrélation 1,000000, résidus de -99 à -110 dB. La voix brute n'a pas de comparaison propre, mais c'est l'entrée de
  cette chaîne exacte.
- **Couper la chaîne** : `mixOn = false` saute le mixeur (MoDule le fait déjà, `mdDrumsEngine.cpp` `State`). Pour
  sauter aussi `TrackFx`, il faut une variante de `render` qui n'appelle pas `fxTrack`. Ce changement est minime : la
  boucle `for(t) fxTrack(...)` est séparée. Il touche à mdEngine, donc il faut le signaler. [INFER : ~5 lignes]
- **Ce que la voix brute n'a pas** : velocity, LEV et VOL sont appliqués par le *mixeur* de DSP1, pas par la voix.
  `HostModel::updateMixer` ($20b1e2-$20b302) donne gain = ((LEV²>>8)·VEL>>17)·(VOL²>>17), dans
  `mixerInput(t).mix[1]`. PAN est dans `mix[2]`, les envois dans `mix[3..4]`. La voix est mono, à pleine échelle,
  quelle que soit la vélocité. Pour une dynamique exacte, l'instrument doit appliquer ce gain lui-même (le mot existe,
  calculé par l'OS) ou porter `Mixer::gains`. Le choke et le mute passent aussi par ce gain (`m_choked`, `m_mute` → 0).
- **Ordre de la chaîne MD** (`TrackFx::process`) : AMD → EQ → filtre 1 (passe-haut, FLTF+FLTW) → filtre 2
  (passe-bas, FLTF) → SRR → DIST. Les paramètres sont les mots lissés par l'OS + LFO (`mixerInput(t).fx`, value<<7).
- **Départ des voix** : une voix part au bloc de 32 suivant. TRX-BD et TRX-S2 partent 36 échantillons après, la
  plupart 4 à 8 (CLAUDE.md, `Engine::voiceStart`). MoDule aligne tout sur `HitDelay` = 83 échantillons. L'instrument
  peut reprendre ce mécanisme (`mdDrumsEngine.cpp`, `State::renderBlock`, lanes à retard) ou garder le seul délai par
  machine.
- **Correspondance des pistes** : piste i de l'instrument = voix i du moteur, `HostModel::setMachine/setParam/trigger`.
  Les LFO MD (un par piste MD, cible = n'importe quelle piste MD), les Links et les Chokes restent internes au moteur.
  Pour les étendre aux pistes Syntakt, il faudrait du code maison. [INFER]
- Le moteur rend à 44,1 kHz, en blocs de 32. Le rééchantillonnage est déjà décidé (carte).

## 2. « Ne faire tourner que les pistes utilisées »

- **DSP2** : le harnais saute les voix au code machine 0 ou 1 (GND--) : `kSkipStub`/`kSkipSilent`,
  `VoiceEngine.cpp`. Une voix inutilisée rend 32 zéros sans appeler la machine. La boucle des 16 voix tourne quand
  même.
- **DSP1 (C++)** : `fxTrack` saute une piste silencieuse et stable (`m_settled`).
- **Tick OS (68k)** : il tourne pour les 16 pistes à chaque tick (`HostModel::tick` : poke des 16×24 cibles,
  `updateVoice` par piste, lissage, LFO, niveaux, un mot du master). C'est un coût fixe.
- **Ce qui ne se coupe pas sans patcher** : le nombre de voix de la boucle DSP2 est celui du programme de l'OS. Le
  réduire demande un patch du harnais (boucle `kNextVoice`/`kEndOfBlock`) ; c'est faisable mais pas tenté. [INFER]
- **Zéro piste MD** : le plus simple est de ne pas construire le moteur, ce qui donne un coût nul (construction
  ~45 ms, plus `warmUpVoices` 177 ms et `MasterEngine::warmUp` 33 ms, README de mdEngine et CLAUDE.md). Donc une
  construction paresseuse, hors du fil audio.
- Mémoire : 37 Mo par instance de MoDule entière (CLAUDE.md). Le master seul prend ~9 Mo (`enableMaster`). Le moteur
  sans master [INFER] en prend ~25-30 Mo.

## 3. Coût mesuré

Banc `mdbench.exe`, 10 s d'audio, blocs de 32, Release. Les pistes 0..N-1 gardent les machines par défaut de MoDule
(TRX-BD, -SD, -XT, -CP, -RS, -CB, -CH, -OH, -CY, -MA, -CL, -XC, -B2, -S2, EFM-BD, -SD), toutes frappées à chaque
double croche à 120 BPM. Les autres pistes sont sur GND--. Deux passes : charge machine 63 % puis 27 % (Live et des
navigateurs ouverts), d'où les fourchettes. % d'un cœur :

| pistes MD | total sans master | tick OS | DSP2 (voix) | TrackFx+mix | master MD |
|---|---|---|---|---|---|
| 0  | 1,9-2,4  | 1,1-1,4 | 0,6-0,8  | 0,2     | (+3,0) |
| 1  | 2,8-3,3  | 1,1-1,3 | 1,3-1,5  | 0,4-0,5 | |
| 4  | 5,4-5,8  | 1,3     | 3,0-3,2  | 1,2-1,3 | (+3,4) |
| 8  | 9,6-12,3 | 1,4-1,8 | 6,0-7,8  | 2,3-2,7 | |
| 16 | 20,0-21,8 | 1,9-2,1 | 13,3-14,5 | 4,8-5,2 | (+4,2-4,7) |

- Par piste MD jouée : ~0,85 % pour la voix (DSP2) et ~0,3 % pour sa chaîne + mix. Sans la chaîne MD, environ 0,9 %.
- Coût fixe : ~2 % (tick OS + boucle DSP2 vide), plus ~3-4,7 % si on garde le master MD (Echo, Gate Box, EQ, Dynamix).
- Recoupement : `mdDrumsEngineTest` (dépôt) donne 22,8 % pour 16 pistes + master, frappes au 1/64 s (mesuré ici
  aussi).
- Comparé au budget de 15 % pour l'instrument entier : 16 pistes MD en voix brute ≈ 2 + 16×0,85 ≈ 15,6 %, donc hors
  budget seules. 4 pistes MD ≈ 5,4 %. L'émulation du DSP2 (JIT dsp56300) domine : ~65 % du coût.
- Le coût dépend de la machine : `mdDrumsFirstHitTest` donne un pire bloc DSP par piste de 0,09 à 0,40 ms selon la
  machine. ROM, E12, P-I et GND-IM n'ont pas été mesurés ici (inconnu).
- Pire bloc : 2 à 5 ms dans mon banc dès qu'une piste joue, contre 0,46 ms au pire dans `mdDrumsFirstHitTest`
  (moteur chauffé). Cause non tracée : charge machine, ou bien `setMachine(GND)` / les frappes du banc qui font
  compiler du code au JIT après le `warmUpVoices`. Inconnu, à mesurer avant de conclure sur la gigue.

## 4. DIST et SRR seuls, comme étages drive sur n'importe quelle piste

- **Code** : `TrackFx::srr` (P:$21f-$233 de DSP1) est un sample-and-hold sur un accumulateur de phase, période tirée
  de SRR². `TrackFx::dist` (P:$234-$25d) multiplie par un gain dépendant de DIST (table OS $143777/$143878), décale de
  9 bits à gauche avec saturation 24 bits, puis applique un filtre du 1er ordre (coefficient 0x7fdf3b, table selon
  DIST). C'est du C++ pur, bit-exact avec DSP1, vérifié étage par étage contre le DSP émulé (en-tête de `TrackFx.h`,
  `tools/mdmix`).
- **État** : quelques mots de `State.y` (SRR $1c-$1d, DIST $1e-$21) et les tables `TrackFx::Tables` lues dans
  l'OS (`shared_ptr`, partageables). Le coût DSP est nul.
- **Coût** (banc, par différence de `stopAfter`) : SRR ≈ 0,08-0,09 µs par bloc de 32, DIST ≈ 0,1-0,2 µs. Les deux
  ensemble font ~0,03-0,04 % d'un cœur par piste, ~0,5 % pour 16 pistes. La chaîne complète est à 1,7-2,0 µs par bloc
  (0,23-0,28 %).
- **Ce qu'il faut changer** : `srr()` et `dist()` sont privés. `srr` lit sa source dans le scratch `m_x[2..33]` (la
  sortie du filtre), `dist` lit `m_y[3..]`. Il faut une entrée publique « depuis le SRR » qui copie l'entrée dans
  `m_x[2..33]`, puis appelle `srr` et `dist` (ou `dist` seul, en copiant dans `m_y[3..34]`). Le `stopAfter` actuel
  ne sait que s'arrêter, pas commencer. [INFER : ~15 lignes dans mdEngine, à signaler comme local change dans son
  README]
- **Paramètres** : sur le MD, les mots SRR et DIST viennent du lissage de l'OS (68k). Seuls, il faut soit value<<7
  sans lissage (des marches à chaque changement), soit un lissage maison, signalé comme ajout. Le lissage de l'OS
  passe par le 68k (`kSmooth`), lié au tick.
- **Limites d'exactitude** :
  - À 48 kHz (piste Syntakt), l'algorithme reste bit-exact mais le son diffère. Le SRR tient « 1 échantillon sur k »,
    donc la fréquence réduite grimpe de 48/44,1 (+8,8 %). La coupure du filtre de DIST, à coefficients fixes, monte
    d'autant. Pour un son identique au MD, il faudrait rééchantillonner la piste à 44,1 kHz autour des deux étages
    (coût et latence du rééchantillonneur, ~1-2 % visés par la carte, plus son retard). [INFER sur la portée exacte
    du décalage de coupure]
  - Le DIST dépend du niveau d'entrée (gain puis saturation 24 bits). Il est calé sur les niveaux des voix MD (pleine
    échelle 24 bits avant VOL). Une voix Syntakt doit être mise à l'échelle 24 bits ; le « drive » ressenti dépend de
    ce choix de niveau, qui est un ajout à signaler.
  - Dans le MD, le SRR précède le DIST et suit le filtre. Les sortir seuls change leur contexte, pas leur arithmétique.
  - Je n'ai pas vérifié que DIST=0 et SRR=0 sont transparents bit à bit [INFER : SRR 0 tient chaque échantillon, avec
    un échantillon de retard (le maintien écrit `ys[2]` avant de lire), et DIST 0 garde son filtre du 1er ordre]. À
    tester avant de les proposer comme bypass.

## Inconnues

- Le coût des machines ROM, E12, P-I, GND-IM, et des User Machines, dans ce banc.
- La cause des pires blocs de 2-5 ms du banc (contre 0,46 ms dans `mdDrumsFirstHitTest`).
- Le gain réel d'un patch qui réduirait la boucle DSP2 au nombre de pistes MD utilisées (fixe ~0,6-0,8 %).
- La transparence de DIST=0 et SRR=0, et la mémoire exacte du moteur sans master.
- Les mesures ont été prises sur une machine chargée (27-63 %). Il faut les refaire au calme avant de les inscrire
  dans un budget.
