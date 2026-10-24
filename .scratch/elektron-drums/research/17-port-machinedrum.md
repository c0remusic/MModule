# 17 — Port du Machinedrum en C++

Ticket : [17-port-machinedrum](../issues/17-port-machinedrum.md). Sources : le code de `C:\dev\MModule` (mdEngine,
mdProtocol, dsp56300), la flash OS 1.63 UW (`elektron_sps1-1uw_os1.63.bin`, dossier de Gearmulator-Elektron), et des
outils écrits pour ce ticket, hors dépôt, dans `C:/dev/elektron-work/scratch-arch/research/md17/` :

- `mdinv.cpp` : construit le `VoiceEngine` de mdEngine (le programme du DSP2 chargé et son init exécutée), lit les
  trois tables de dispatch (Y:$145AF5 init, Y:$145BB6 update, Y:$145C77 render, 193 types, type = id + 1), puis
  parcourt le code atteint depuis chaque entrée avec le désassembleur de `source/dsp56300` (sauts et branches suivis,
  sous-routines suivies). Sorties : `inv.txt`, `ops.txt`, `dump.txt`, `master.txt`.
- `mdcost.cpp` : 16 pistes sur la même machine, frappées à chaque double croche à 120 BPM pendant 4 s, compteur
  d'instructions du DSP2 et temps mural (`cost.txt`).
- `os68k.py` : taille des routines ColdFire de l'OS que mdEngine appelle (`cfdis.routine`, image à $200000).
- `kits.cpp` : machines des 16 Kits d'usine (image patch de la flash).
- `kernel.cpp` : le render de P-I-MA écrit à la main comme un port bit-exact (accumulateurs 56 bits en int64) et en
  flottant, pour mesurer le coût d'un port par instruction DSP. Ce n'est pas un port vérifié.

Le message du coordinateur du 2026-10-24 (règle amendée dans map.md, Notes) est pris en compte : deux estimations,
port bit-exact et port flottant rapide. Les suppositions sont marquées [INFER].

## Réponse courte

Le port est faisable, et il est petit. Le DSP2 porte **43 programmes de voix sonores distincts** (44 entrées render
avec le silence ; 39 à porter sans INP ni RAM-R), soit **11 385 instructions DSP56300 statiques** en tout. Les machines n'ont
presque aucun code partagé : à part ROM, aucun render n'appelle de sous-routine. Côté 68k, il y a **55 fonctions de
coefficients** distinctes (4 714 instructions ColdFire), plus le lissage, la pente de niveau et le LFO (ce dernier est
déjà porté dans `mdDrumsLfo`). Le master tient en **1 215 instructions DSP** (P:$342-$970 du programme B), plus quatre
corps 68k qui calculent ses mots.

Un port **bit-exact en entiers** suffit à tenir le budget. Mesuré sur P-I-MA : 0,30 ns par instruction DSP exécutée,
contre environ 2,7 ns sous le JIT. Une voix MD coûterait donc 0,03-0,15 % d'un cœur (0,07-0,3 % avec une marge ×2
[INFER]), au lieu de 0,4-1,3 % aujourd'hui. Le tick 68k (~1,1-1,4 %) et la boucle DSP2 vide (~0,6-0,8 %)
disparaissent presque. La chaîne DSP1 déjà en C++ (`TrackFx`, ~0,3 %/piste) devient alors le poste le plus cher.

Un port **flottant** gagne peu sur les voix : ×1,5 mesuré en scalaire sur P-I-MA. Le SIMD « à travers les 16 voix »
ne s'applique pas aux voix, parce que 16 pistes jouent en général 16 programmes différents. Il s'applique à `TrackFx`
et au master, qui exécutent le même code pour chaque piste.

Recommandation [INFER] : voix et fonctions de coefficients en entiers, bit-exactes, vérifiées par vecteurs. Puis
`TrackFx` (et le master) en flottant SIMD sur 8 ou 16 pistes, vérifiés à une tolérance mesurée. Effort : ~2-3
semaines d'agent pour le bit-exact entier, ~1 semaine de plus pour le flottant SIMD de la chaîne et du master.

## 1. Inventaire du DSP2 (programme A, section 1 de l'OS 1.63)

Les tables sont lues après l'init du programme (`VoiceEngine`), donc avec les repli que l'init y écrit. Chaque table a
193 types. Le nombre d'instructions compte le code atteint depuis l'entrée, sous-routines comprises (`inv.txt`).

| famille | renders distincts | machines | instr. statiques (init+update+render) | instr. exécutées / voix / bloc (`cost.txt`) |
|---|---|---|---|---|
| GND | 3 (SN, NS, IM) + le silence `$10008F` | 4 | 133 | 217-586 |
| TRX | 14, un par machine | 14 | 3 997 | 626 (XT, XC) - 2 685 (S2) |
| EFM | 8, un par machine | 8 | 1 468 | 1 566 (BD) - 2 742 (CB) |
| E12 | 4 renders (`$103838` ×9, `$103B46` ×5, BD, RS) ; 13 init et 15 update distincts | 16 | 1 171 | 1 210-1 892 |
| P-I | 9, un par machine | 9 | 3 171 | 1 367 (MA) - 2 866 (SD, MT) |
| ROM | 1 (`P:$16C`, mémoire interne) pour 48 ROM + RAM-P1-4 | 52 | 589 | 3 534-3 548 (slot plein), 228 (slot vide) |
| RAM-R | 1 (`$103579`) pour R1-R4 | 4 | 921 | non mesuré (RAM ne joue pas dans MoDule) |
| INP | 3 (GA/GB, FA/FB, EA/EB) | 6 | 544 | 552-940 |
| MID, CTR, GND-SW/PU, NFX, INP-CA/CB | le silence | 30 | 11 | 0 |

- **Total** : 44 entrées render distinctes, 66 triplets (init, update, render) distincts, 11 385 instructions
  statiques. Hors silence, on a 43 renders : TRX 14 + EFM 8 + E12 4 + P-I 9 + ROM 1 + GND 3 + RAM-R 1 + INP 3. Sans
  INP (il n'y a pas d'entrée audio) ni RAM-R (qui ne joue pas dans MoDule), il en reste **39 à porter**.
- **Code partagé** : presque rien. Seul le render ROM appelle des sous-routines (`$15A`, `$3BC`, `$3CF`, en P interne).
  Les autres machines sont autonomes : elles ne partagent que des **tables en lecture seule** : le sinus de 32 768
  entrées à X/Y:$148000 (46 références dans le code, construit par l'init du programme), les tables X:$257-$6FF du flux
  (tanh à X:$280-$680, selon `machinedrum-kit/08-dsp2-voice-abi.md`), les tables X:$140000-$146FFF, la mémoire externe
  des P-I (`$135600`, 1 536 mots par piste) et le répertoire et les échantillons ROM ($147E00, $150000).
- **Données du programme A** : 233 753 mots de P externe, dont ~201 800 de $103DBA à $135206, en blocs de taille
  variable séparés par des blocs de 153 mots. Ce sont les échantillons des E12 [INFER : non lu dans le code des
  renders E12]. Le code des machines n'en est qu'une petite part (~11 400 instructions).
- **Opcodes** (`dump.txt`, `ops.txt`) : 65 % de `move` purs, puis mpy 697, mac 619, add 544, do 360. Arrondi
  explicite rare : mpyr 48 et macr 20, presque tous dans EFM et un peu dans P-I-MA et ROM. Double précision
  (dmac, mpysu, macsu) dans GND-SN, GND-NS, EFM-HH, EFM-CY, P-I-BD, P-I-MT, P-I-ML, P-I-RS, ROM et RAM-R. `ifcc`
  dans TRX-B2, TRX-S2, E12, P-I, INP et ROM. `div`, `normf`, `clb` et `merge` dans RAM-R seulement. Adressage indexé
  `lua`/`mpyi`/modulo dans P-I (guides d'onde). Aucun saut indirect dans les machines.
- **Mode du DSP** : `move #>$80300,sr` à P:$24 (CE et masque d'interruption), puis `andi #$fc,mr`. Aucune machine
  n'écrit SR, MR ni OMR (seul `andi #$fe,ccr`, deux fois, dans RAM-R). Donc : pas de mise à l'échelle, pas de
  saturation arithmétique (SM = 0), et l'arrondi est **convergent** (RM = 0).
- **Coût sur le DSP réel** : les voix d'usine prennent 2 000-3 200 cycles par bloc (`08-dsp2-voice-abi.md`), sur un
  budget de 73 728 cycles. Ici, ROM est la plus chère en instructions exécutées (3 534).

## 2. Routines 68k (section 0, ColdFire, appelées dans Musashi par `MachineRunner`)

| routine | adresse | instr. statiques | déjà en C++ ? |
|---|---|---|---|
| 55 fonctions de coefficients (135 machines, table `$24EF54`) | `$201128`-`$204BA2` | 4 714 en tout : 2-63 GND/INP/RAM, 30-176 TRX, 67-99 EFM, 46-92 P-I, 135-160 E12, 201 ROM (deux fonctions) | non (X.20 : `X20Builtins`, `mds::convertFine`) |
| lissage des 16×24 paramètres | `$10002E0` (SRAM) | 33 | non |
| oscillateur LFO / application / restart / application d'un LFO | `$1000088`, `$1000332`, `$204C94`, `$10001E8` | 185, 79, 73, 60 | oui, bit-exact (`mdDrums/mdDrumsLfo.*`, vérifié par `mdDrumsScreensTest`) |
| pente des 48 niveaux | `$100029E` | 22 | non |
| quatre corps du master (Echo, Gate Box, EQ, Dynamix) | `$20B4AE`, `$20B604`, `$20B74E`, `$20B940` | ~100-170 chacun [INFER : écart entre adresses ; le parcours déborde dans le tick, car leurs sorties sont patchées en `rts` au vol] | non |
| rampe de slide | `$23B25A`, `$23AC2C` | — | oui (`HostModel::slide`) |

Les fonctions de coefficients sont pures : (paramètres, sortie) → nombre de mots (`docs_PROTOCOL.md`). C'est du C
compilé, en ligne droite. On les vérifie de façon presque exhaustive avec `MachineRunner::compute`.

## 3. Chaîne de piste et master

- DSP1 par piste (`TrackFx`, AMD → EQ → filtre 1 → filtre 2 → SRR → DIST) et mixeur (`Mixer`) : déjà en C++,
  bit-exacts. Coût mesuré (recherche 03) : ~0,25-0,3 % par piste et 4,8-5,2 % pour 16 pistes. Une fois les voix
  portées, c'est le **premier poste de coût**.
- Master : `MasterEngine`, un deuxième DSP56300 émulé, 3-4,7 %. 1 215 instructions (`master.txt`), dont 56 en double
  précision (macsu 26, mpysu 16, dmac 14) et 54 boucles `do`. Le port se fait de la même façon que les voix.

## 4. Méthode d'oracle (comme STV3 pour le Syntakt)

L'ABI des voix (`08-dsp2-voice-abi.md`) rend les vecteurs plus simples que sur le Syntakt. Tout l'état d'une voix
tient dans le bloc Y:S..S+$3F (S = $800 + $40·t), le scratch X:0-$1F, la mémoire externe P-I de la piste et la sortie
de 32 mots. Pas besoin d'Unicorn : l'émulateur `source/dsp56300` du dépôt est l'oracle.

1. **Vecteurs DSP2** : on enregistre chaque appel init, update et render. On prend un instantané de l'état ci-dessus
   avant et après, avec R0, R1 et R7. Points d'arrêt sur les sites d'appel P:$8D, $98 et $B4 (déjà détournés par le
   harnais de `VoiceEngine`). Scénarios : paquets de coefficients calculés par `MachineRunner` sur des SYN1-8 et des
   LFO tirés au hasard, frappes, changements de machine (l'init ne tourne qu'au changement de type, l'update à
   chaque frappe, le render à chaque bloc), vélocités et accents. Recettes de bord comme `edge_recipes.py`.
2. **Vecteurs 68k** : `MachineRunner::compute` sur des grilles de SYN1-8 et un drapeau de frappe, mot pour mot.
3. **Couverture** : le JIT ne donne pas de trace par instruction. Il faut l'interpréteur du dsp56300, ou des
   points d'arrêt, pour l'équivalent de `coverage_insn.py` [INFER : à vérifier dans le fork].
4. **Bout en bout** : le port contre `mdEngine` échantillon par échantillon (0 différence attendue), puis contre
   `mdLib` par `mdEngineFirmwareTest`, qui compare déjà mdEngine au firmware complet.

## 5. Points durs du bit-exact

- **Accumulateurs 56 bits** (8:24:24) : mpy fractionnaire (produit << 1), mac sans saturation. **Limitation** à
  chaque `move` d'un accumulateur vers 24 bits, si l'extension est utilisée : $7FFFFF ou $800000. Écrire dans B1
  seul laisse B2 et B0 en place (P-I-MA s'en sert). Arrondi convergent pour mpyr et macr seulement ; ailleurs, on
  tronque. Le JIT de Gearmulator garde l'accumulateur aligné à gauche dans 64 bits (`performance-research.md` §3) :
  c'est le modèle à reprendre.
- **Double précision** (dmac, mpysu, macsu) : phases et produits 48×24 dans GND-SN/NS, EFM-HH/CY, P-I, ROM et le
  master. Ça passe en int64 ou en __int128 [INFER : le coût est négligeable, ces ops sont rares].
- **Modulo et pointeurs** : M7 = $1F sur la sortie, tampons circulaires des P-I à $135600, `lua`/`mpyi` pour
  l'indexage. Les instructions `ifcc` et `do`/`rep` se traduisent directement.
- **État qui survit** : le bruit d'une voix continue entre les frappes (README de mdEngine, « Known difference »), et
  le render tourne à chaque bloc pour chaque piste. Pour rester bit-exact à la frappe suivante, une voix de machine à
  bruit doit faire avancer son générateur même en silence. Soit on calcule le saut du générateur en forme close, soit
  on rend la voix. Le coût suit donc le nombre de pistes assignées, pas seulement celles qui sonnent [INFER : la
  forme du générateur n'a pas été lue].
- **Données venues du firmware** : les tables (sinus, tanh, X:$140000+), les échantillons E12 (~200 k mots dans le
  programme A) et la banque UW des ROM (flash). Avec « aucun firmware chez l'utilisateur », il faut les recalculer
  quand c'est un calcul (le sinus est construit par l'init), ou les livrer, ou demander les échantillons à
  l'utilisateur. C'est l'objet du ticket 18 (sans firmware). E12 et ROM en dépendent directement.
- **User Machines d'X.20** : c'est du code DSP tiers, fourni par l'utilisateur dans ses paquets MDS, et non une
  famille à porter une fois pour toutes. Il faut un port par paquet (les 6 intégrées d'X.20 et les 13 NFX/SYN-F du
  zip d'X.20 B), ou les laisser hors de l'instrument. Ça entre en conflit avec « aucun émulateur au rendu » s'il faut
  les jouer telles quelles.

## 6. Coût CPU attendu par piste

Mesures (`cost.txt`, 16 voix, JIT, voix DSP seule) : 0,74 % pour 16 GND, de 5,8 % (P-I-MA) à 21,2 % (P-I-RS) pour 16
voix jouées. ROM monte à 16-19 %, TRX-BD à 10,5 %, EFM-BD à 6,0 %. Par voix, cela donne 0,04-1,3 %. Le banc
`kernel.cpp` (MSVC /O2, scalaire) mesure P-I-MA à **0,41 µs par voix et par bloc en entiers bit-exacts** (0,057 % d'un
cœur), soit 0,30 ns par instruction DSP exécutée, et à **0,28 µs en flottant** (0,038 %). Le JIT, lui, prend 2,6 µs.

| poste (par piste MD jouée sauf mention) | aujourd'hui (émulé) | port bit-exact entier | port flottant rapide |
|---|---|---|---|
| voix DSP2 | 0,4-1,3 % (~0,85 % en moyenne) | 0,03-0,15 %, 0,07-0,3 % avec marge ×2 [INFER] | ~×1,5 de moins en scalaire ; SIMD seulement entre voix du même programme (kit ROM, E12) |
| chaîne DSP1 + mix | 0,25-0,3 % (déjà C++) | idem | ~0,05-0,1 % en SIMD sur 8 pistes [INFER] : même code pour les 16 pistes |
| fixe : tick OS + boucle DSP2 | ~2 % | < 0,2 % [INFER] : 55 fonctions de ~30-200 instructions par tick | idem |
| master MD | 3-4,7 % | ~0,5-1 % [INFER] | ~0,3-0,6 % [INFER] |
| **16 pistes, sans master** | 20-22 % | ~6-10 % [INFER] | ~2-5 % [INFER] |

Le modèle P-I-MA est simple (une récurrence, un filtre). Les machines à tables (ROM, interpolation et adressage) et à
branches coûteront plus par instruction, d'où la marge ×2. Il faut mesurer une machine de chaque famille dès son port.

## 7. Bit-exact ou flottant : qu'est-ce qui s'entendrait ?

Le flottant rapporte peu sur les voix et coûte la preuve d'exactitude. Là où on l'adopterait, voici les machines à
risque [INFER : rien n'est mesuré ; la tolérance se mesurera contre le port entier, en résidu et à l'oreille] :

- **Sans risque si l'on garde les bons morceaux en entiers** : les phases (accumulateurs modulo 2^24 et lecture du
  sinus) et le bruit (générateur entier), pour toutes les familles. Les limitations deviennent des clamps. En
  float32, une phase perdrait sa précision (mantisse de 24 bits contre 24-48 bits de phase). Il faut donc des phases
  entières ou en double.
- **E12, ROM, GND, TRX, INP** : lecture d'échantillons 12 bits et interpolation, oscillateurs et enveloppes. Les écarts
  restent sous le bruit de quantification 24 bits. Inaudibles [INFER].
- **EFM** (FM à rétroaction) : l'arrondi change la trajectoire du feedback, mais le timbre reste statistiquement le
  même. Pas audible sur une frappe [INFER].
- **P-I** (modèles physiques : guides d'onde et résonateurs à rétroaction, en particulier RC, CC, HH, RS) :
  **risque réel**. En virgule fixe, la troncature amortit les longues queues et fixe des cycles limites. Le flottant
  peut prolonger une résonance ou en changer la fin. C'est à mesurer avant tout passage en flottant.
- **TRX-B2 et TRX-S2** (max, ifcc, écrêtage) : il faut des clamps exacts aux mêmes seuils, sinon la saturation change
  de couleur.
- **DIST et SRR de la chaîne** : saturation 24 bits et maintien. En flottant avec les mêmes clamps, l'écart reste
  infime. L'EQ et les filtres DSP1 en flottant SIMD changent la réponse au bit près seulement [INFER].

## 8. Effort et ordre de port

Calibration : le Syntakt (12 moteurs, étages de piste, contrôle CPU #1, délai et réverbération, ~13 500 lignes) a été
porté bit-exact en ~3 jours de commits (2026-10-22 à 10-24), sur un harnais de vecteurs qu'il a fallu construire. Le
MD est plus gros en nombre de programmes. Mais son ABI de voix est propre et l'oracle est déjà dans le dépôt.

| étape | taille | estimation [INFER] |
|---|---|---|
| harnais de vecteurs DSP2 et 68k, modèle d'ALU 56 bits (`mdAlu.h`) | — | 2-3 jours |
| ROM (1 render + 2 fonctions de coefficients ; 96 pistes sur 256 dans les Kits d'usine) | 589 + 402 | 1-2 jours |
| EFM (8 ; 47 pistes) | 1 468 + 630 | 2-3 jours |
| TRX (14 ; 38 pistes, Kit par défaut de MoDule) | 3 997 + ~1 170 | 4-5 jours |
| E12 (4 renders, 13 init ; 35 pistes) | 1 171 + ~1 515 | 2-3 jours |
| P-I (9 ; 16 pistes) | 3 171 + ~590 | 4-5 jours |
| GND-SN/NS/IM (3 pistes) | 133 + 80 | ½ jour |
| tick : lissage, pente de niveau, ordre de frappe (le LFO est déjà fait) | ~60 | 1 jour |
| master DSP1 + 4 corps 68k | 1 215 + ~550 | 3-4 jours |
| **total bit-exact entier** (39 renders) | ~10 500 DSP + ~4 600 68k + master | **~2-3 semaines d'agent** |
| flottant SIMD de `TrackFx` et du master, harnais de tolérance | — | +1 semaine |
| port flottant des voix | — | +1-2 semaines, faible gain : non conseillé |

Ordre conseillé : harnais → **ROM** (une seule routine, la famille la plus jouée des Kits d'usine, mais elle dépend
des échantillons, ticket 18) → **TRX** (sans données firmware, Kit par défaut, le plus souvent cité) → **EFM** → **E12**
(échantillons) → **P-I** → GND → master. INP et RAM restent hors port : sans entrée audio, et RAM ne joue pas dans
MoDule.

Comptage des 16 Kits d'usine (`kits.cpp`), sur 256 pistes : ROM 96, EFM 47, TRX 38, E12 35, RAM 21, P-I 16, GND 3.
Si les échantillons ne sont pas réglés, on commence par TRX.

## Inconnues

- Le coût réel d'un port par famille. Le banc ne mesure qu'un noyau simple (P-I-MA) et n'est pas vérifié contre
  l'oracle.
- La forme du générateur de bruit, et si l'on peut le sauter en forme close pour une voix silencieuse.
- Si les blocs de données de $103DBA à $135206 sont bien les échantillons E12, et ce que contiennent X:$140000-$146FFF.
- La taille exacte des quatre corps 68k du master (le parcours statique déborde à cause des sorties patchées).
- Si une machine compte sur un débordement de l'accumulateur au-delà de 56 bits, ou sur `asl` sans limitation,
  comme distorsion voulue. Non lu.
- L'audibilité réelle du flottant sur P-I, à mesurer.
- La couverture : il faut vérifier que l'interpréteur du dsp56300 peut tracer instruction par instruction.
- Les mesures ont été prises sur une machine non calme.
