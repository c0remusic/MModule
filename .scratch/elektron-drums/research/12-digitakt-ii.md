# 12. Faisabilité du Digitakt II

Recherche du 2026-10-24, ticket [12-digitakt-ii](../issues/12-digitakt-ii.md). Rien n'a été exécuté : lecture des
sources, des notes et du code. Marques : **[D]** dit par la source citée, non revérifié ici ; **[INFER]** déduit ici ;
**[O]** inconnu. Les marques [V]/[D]/[O] de digikit sont reprises telles quelles quand elles comptent.

Sources lues (clones sous `C:\dev\research-clones\`, têtes du 2026-10-23) :

- digikit, https://github.com/ElekLabs/digikit, commit `9dee5d5` : `README.md`, `docs/FINDINGS.md`,
  `docs/findings/02-machines-and-parameters.md`, `06-sharc-engine-and-startup.md`, `18-dsp-performance.md`,
  `53-stock-dsp-shape.md`, `packages/emulator/README.md`, `tools/patchkit/pk_sharc.py`,
  `tools/sharc_dev/sharc_core/SUBSET.md`.
- dn2-cfw, https://github.com/naturarum/dn2-cfw, commit `5c95c78` (GPL-2.0) : `README.md`,
  `notes/T3.6-sharc-interpreter.md`, `notes/T3.4-fx-locate.md`, `notes/T1.11-dt2-inplace.md`.
- elektron-emu, `C:\dev\elektron-emu\Digitakt-II\` (sans licence) : `README.md`,
  `source/core/machines/digitakt2/dt2_audio.inc.c`, `dt2_dspi.inc.c` ; `Digitone-II/README.md`,
  `source/core/dsp/dn_gensrc.inc`.
- Survolés : lalzart/dt2-machine-lab, lalzart/digitakt-ii-firmware-research-public (CC0, OS 1.15C),
  karigardarsson/digitakt-ii-research (MIT, dérivé de digikit).

## Réponse courte

Faisable, par portage et non par émulation. Le SHARC du Digitakt II est déjà émulé hors temps réel par digikit
(ColdFire et SHARC+ réels, PCM épinglés, déterministe). Cet émulateur sert d'oracle, et ses outils savent appeler
une routine isolée du DSP. Les routines des filtres, des effets et du rendu des voix sont localisées à l'adresse près
dans l'image DSP. Celle-ci est identique octet pour octet entre les OS 1.16 et 1.17, et ses filtres et effets sont le
même code que ceux du Digitone II. Émuler tout le programme coûterait environ un cœur entier : c'est hors budget. Un
portage C++ routine par routine, comparé à l'oracle, tiendrait dans quelques points de cœur [INFER]. L'exactitude se
prouve contre l'émulateur, pas contre le silicium : les tables de départ de RECIPS/RSQRTS ne sont pas documentées.

## 1. État de digikit

- **Licence** : AGPL-3.0-or-later, avec des termes de la section 7 (`NOTICE`). Une copie modifiée garde NOTICE et la
  mention « digikit by Em D - https://github.com/ElekLabs/digikit », et l'interface d'une œuvre couverte la montre
  dans son About, ses crédits ou son pied de page (`README.md`, section Licence). C'est compatible avec l'AGPL de
  MModule, mais l'interface doit afficher cette mention si du code digikit est repris [INFER].
- **Cibles** : Digitakt II OS 1.17 et Digitone II OS 1.12 pour les patchs. L'émulateur boote aussi DT2 1.16 et DN2 1.11.
  Les blobs DSP sont identiques octet pour octet entre 1.16 et 1.17, et entre 1.11 et 1.12 (`53-stock-dsp-shape.md`,
  `meta.image_sha256`). Seul `Digitakt_II_OS1.16.syx` est sur ce PC, dans
  `%APPDATA%\Elektron Emu\Digitakt II\Firmware\` : il suffit pour le DSP.
- **Émulateur** : en Rust (`packages/emulator/`). Il comprend un cœur ColdFire V4e, le DSP SHARC+ (interpréteur, blocs
  AOT, tier rapide Cranelift et WebAssembly), la boucle couplée, un démon de contrôle JSON-RPC (`digi-control`, avec
  `dsp.peek`, `step`, `at_instr`) et une capture du son avant la sortie. Le dépôt est très actif : 60 notes de
  recherche, des PR jusqu'au 2026-10-23.
- **Temps réel : non.** « The emulator's DSP does not run in real time yet » (README). Il faut 1,584 s de CPU par
  seconde audio sur DN2 1.11, dans la configuration livrée (`18-dsp-performance.md`, gel 911b1fc).
- **Preuves sur l'appareil** : digikit fait tourner son runtime et PLUCK sur un vrai Digitakt II 1.17 (findings 47
  et 51). Ses mesures sur l'émulateur ont donc été recoupées sur l'appareil au moins une fois.

## 2. Le harnais d'oracle

Trois niveaux d'oracle existent déjà. Aucun n'est à écrire de zéro.

1. **Programme entier** : la boucle couplée de digikit boote la .syx de l'utilisateur, joue des scénarios (trig,
   accords, enregistrement) et épingle le PCM par hash (`mise run scenario:all`). Elle se pilote par le démon de
   contrôle (`docs/findings/17-emulator-control.md`). C'est l'équivalent de `mdLib` pour MoDule : un processus à part,
   hors temps réel.
2. **Routine isolée sur l'image** : le cœur concret Python de digikit (`tools/sharc_dev/sharc_run.py`, `run_call` dans
   `tools/patchkit/pk_sharc.py`) exécute une fonction avec des arguments R4/R8/R12. dn2-cfw fait de même en C avec
   sharcvm (`vm.call(entry, R4=.., R8=..)`, `notes/T3.6-sharc-interpreter.md` section 1) : environ 80 M instructions/s
   sur M2 Pro, soit 3 à 8 fois le temps réel pour un effet seul. Il a rendu en WAV le delay, le chorus, la reverb et le
   compresseur d'origine. Ce niveau correspond à ce que fait `mdEngineFirmwareTest` : capturer les entrées et les états
   d'une routine dans le programme entier, puis comparer le port échantillon pour échantillon.
3. **Validation du cœur émulé** : dn2-cfw passe les 2 039 tests C de selache compilés pour SHARC et les helpers
   mathématiques du DN2 contre libm. digikit croise son décodeur avec selache.

Limites :
- **Le silicium n'est pas l'oracle.** Les tables de départ de RECIPS et RSQRTS ne sont pas documentées : 8 quotients
  sur 2 000 de la division du DN2 diffèrent d'un ulp (dn2-cfw T3.6, section 6 point 3). Un port exact à l'émulateur
  peut s'écarter d'un ulp de l'appareil dans toute routine qui divise (le helper `0x1c06ba`) ou prend une racine
  inverse (`0x1c127c`). Pour combler ce trou, il faut un enregistrement d'un vrai Digitakt II. [O] : l'utilisateur en
  a-t-il un ?
- Les modes 40 bits (RND32 = 0) du helper de division et les flottants SHARC (dénormaux à zéro) doivent être reproduits
  en C++ à la main : `-ffp-contract=off`, pas de FMA. elektron-emu le fait déjà pour son propre code
  (`dt2_audio.inc.c`, lignes 1-7).
- Outillage à installer : mise, uv, Rust et Cranelift pour digikit, ou un compilateur C pour sharcvm. Rien n'a été
  construit ici [O].

## 3. elektron-emu n'émule pas le SHARC du Digitakt II

`dt2_dspi.inc.c`, ligne 1 : « i collegamenti col DSP SHARC (non emulato) ». La ColdFire 1.16 tourne pour de vrai, mais
le son vient d'un remplaçant écrit à la main. Ce remplaçant est le port C d'un modèle numpy (`emu/audio.py`) :
`au_render` (`dt2_audio.inc.c`, l. 314-384) lit l'échantillon en interpolation linéaire, le multiplie par un gain, puis
un tanh au-dessus d'un coude de 0,8 sur le master. Il n'a ni filtre, ni effet, ni enveloppe. Il ne sert donc ni
d'oracle ni de base pour le son. Ce qui sert, c'est la capture des trames DSPI2 de la ColdFire, pour savoir ce que le
DSP reçoit. Le dépôt n'a pas de licence (`resources.md` : licence demandée) : lecture seule.

Le plug-in Digitone II d'elektron-emu, lui, traduit le programme SHARC entier en C et le compile chez l'utilisateur
(`Digitone-II/source/core/dsp/dn_gensrc.inc`), en temps réel. C'est la preuve qu'une traduction statique du programme
entier est possible. Son coût CPU n'est pas publié [O], et cette piste revient au ticket 13.

## 4. Où sont les routines (DSP, adresses en short-word, DT2 1.16 = 1.17)

Source : `53-stock-dsp-shape.md` (marques [D] : lu une fois par un agent, à recouper sur les octets) et
`06-sharc-engine-and-startup.md`. Les filtres, la saturation, l'enveloppe, le biquad et les unités d'effet sont
« identiques entre appareils, hash de relocation égal ou octets identiques ». Ils ont donc la même valeur pour le
ticket 13.

### Chaîne de la piste, par bloc de 32 échantillons à 48 kHz

| Étage | Routine | Taille (instructions) |
|---|---|---|
| Dépaquetage de la trame | `0x1c24e9` ×32, vers les fiches de piste `0x2506ec + 0xdc*t` | |
| Rendu de la voix | `0x1c4ecf` (corps `0x1c4f81`, 381), appelé depuis `0x1c6b00` | voir 4.3 |
| Niveau | `0x1cdd8b` (copie ou remplissage SIMD) | 55 |
| BR (bit reduction) | dans la chaîne, adresse non isolée [O] | |
| Overdrive (saturateur) | `0x1cb3d8` | 98 |
| SRR | `0x1cdecb` (sample-and-hold, interpolation) | 51 |
| Filtre | table `0x8055c874`, indexée par type + 1 [INFER], voir 4.1 | 99-173 |
| Chaîne fixe après le filtre | biquad `0x1ccbd8` + reset `0x1ccbca` (« low-pass plus DC blocker » [D]) | 59 + 8 |
| Enveloppe | `0x1cbb57` (fiche de 11 mots, table de taux `0x26ad30`) | 104 |
| Pan, courbes à puissance égale | tables `0x8055c440` / `0x8055c640` dans `FUN_1c71ec` | |

### 4.1 Filtres

Les numéros de type viennent du menu de la ColdFire (`02-machines-and-parameters.md`, « The selector ») :
0 MULTI-MODE (nommé « STATE VARIABLE » en interne), 1 LOWPASS 4, 2 EQUALIZER, 3 COMB-, 4 LEGACY LP/HP, 5 COMB+. La table
`0x8055c874` donne pour les entrées 1 à 6 : `0x1cdbb2`, `0x1cd286`, `0x1cc79e`, `0x1cbf07`, `0x1ccd96`, `0x1cbf07`
(`06`, l. 3361-3371). Si l'entrée vaut type + 1, les deux peignes tombent sur la même routine, et c'est la seule
routine de la liste qui soit un peigne (anneau de 8 192 mots avec rétroaction). D'où, **[INFER]** (forte
présomption, à vérifier à l'oracle) :

| Filtre | Routine | Ce que digikit en a lu |
|---|---|---|
| Multi-mode | `0x1cdbb2` (115) | récursion IIR tout-pôle |
| Lowpass 4 | `0x1cd286` (148) | 9 paires de champs rassemblées, puis une boucle MAC |
| Equalizer | `0x1cc79e` (173) | interpolation dans la table `0x26bb68` (digikit l'avait prise pour un rééchantillonneur) |
| Comb- / Comb+ | `0x1cbf07` (133), le signe en paramètre [INFER] | `DM(I1+M4) = fclip(x + g*taps, 1.0)`, index `(i+1) & 0x1fff` |
| Legacy LP/HP | `0x1ccd96` | reçoit une table de 32 flottants (`0x8045c3c0`) |

Les coefficients se calculent en `2*pi*f/fs` avec fs = 96 000 (`06`, l. 2157). Il faut chercher qui calcule les
coefficients à partir des mots de paramètre : la ColdFire ou un setter DSP [O]. digikit marque encore les rôles des
cinq types comme ouverts (`53`, section Open).

### 4.2 Effets (envois et master)

| Effet | Routine | Coût mesuré (instructions par bloc, DN2, même code) |
|---|---|---|
| Chorus | `0xb82680`, état DDR `0x80459388`, lignes de 512 | 14 457 (statique 7,1k) |
| Delay | `0xb82150`, anneaux de 524 288 flottants, 8 s au plus | 5 424 (statique 6,0k) |
| Reverb | `0xb80fe0`, état `0x80000018` (~0x59114 octets) | 14 563 (statique 17,2k) |
| Compresseur (dynamics du master) | `0xb82d41` -> `0xb82cba` | 16 560 |
| Overdrive du master, clip, rampe de gain | dans `0x1c207b` (master) | [O] |
| Pilote des envois | `0x1c18a6` @ `0x1c3090` | |

Les comptes viennent de dn2-cfw (T3.6, section 5, sharcvm). Les tailles statiques viennent de digikit (`53`).
La mise à jour des paramètres du delay est `0xb84604` : le temps se calcule depuis TIME et le tempo, avec des
constantes 30 et 300, 120.0 et 48 000 (dn2-cfw T3.4). Les paramètres FX ne se verrouillent pas et ne sont pas des
destinations de LFO (`53`).

### 4.3 Moteurs du sampler

- Types de machine (ColdFire, `02`) : 0 SAMPLE (ONESHOT), 1 WERP, 2 STRETCH, 3 REPITCH, 4 SLICED SMP, 5 MIDI,
  6 MANUAL SLICE. La table de sélecteurs `0x2567c0` = `0 1 2 3 4 0 5` ramène les sept types à six rôles DSP.
- Un seul rendu de bloc sert les sélecteurs 0, 1, 3, 4 et 5 : `0x1c4ecf`. STRETCH a le sien, `0x1c5576`. Chaque appel
  rend 64 échantillons à 96 kHz par interpolation polyphasée à 6 prises (table `0x25d940`). Ensuite, `0xb80000` décime
  64 vers 32. Le pas de lecture est un Q31 calculé par quatre helpers de cas (`0x1c4afe`, `0x1c4bf9`, `0x1c4d88`,
  `0x1c4a31`) avec powf `0x1c0d68` (`06`, l. 3346-3407).
- Fiches de voix : 32 de `0x1d8` octets en `0x2412cc`. Stéréo : L sur la voix paire, R sur l'impaire. La voix s'arrête
  quand le rendu efface l'octet `+0x1b8`.
- Côté ColdFire : lissage à un pôle des paramètres avant la trame (`FUN_400d92a2`), mots s16 mis à l'échelle 1/32512 sur
  le DSP, rampe par échantillon de (nouveau - ancien) / 32 dans le bloc (`53`). LFO et conversion des paramètres
  musicaux vers les mots de trame : sur la ColdFire, routines non localisées [O]. Ce travail est de même nature que le
  CPU #1 du Syntakt.
- Les échantillons d'usine sont sur la carte (+Drive), pas dans l'OS (`59-storage-regions.md`). L'instrument jouerait
  les échantillons de l'utilisateur, comme MoDule.

## 5. Coût CPU

- **Programme DSP entier** : 211 000 instructions occupées par bloc sur DT2 1.16, sur un seul trig. C'est un fond
  constant : le compte ne bouge pas avec les 16 touches enfoncées, mais ce scénario n'a pas changé le RMS, donc
  peut-être aucune voix n'a sonné [O] (`06`, « Headroom »). À 1 500 blocs par seconde, cela fait environ 316 M
  instructions SHARC par seconde. Au rythme de digikit (1,584 s pour 502 M instructions, soit environ 3,2 ns par
  instruction) : **environ 1,0 cœur** [INFER]. Avec sharcvm (93 M/s) : 3,4 fois plus lent que le temps réel. Le
  budget de 15 % pour tout l'instrument exclut donc l'émulation.
- **Effets, en port natif** : delay, chorus, reverb et compresseur font environ 51 000 instructions SHARC par bloc,
  soit 77 M/s. Une instruction SHARC fait en général une opération flottante, parfois deux (SIMD, multifonction). En
  C++ scalaire, cela donnerait de l'ordre de 2 à 4 % d'un cœur [INFER, à mesurer].
- **Voix, en port natif** : PLUCK ajoute +561 à +901 instructions par bloc. Une voix sample d'origine fait environ
  4 000 instructions de plus qu'une voix sinus (214 279 contre 210 300, `06` l. 5597). Par piste, avec le filtre, la
  saturation, le SRR et l'enveloppe, comptez de l'ordre de 3 000 à 6 000 instructions par bloc, donc 16 pistes à 72-144
  M instructions SHARC par seconde, environ 2 à 5 % d'un cœur en port natif [INFER]. Le suréchantillonnage 2x (rendu à
  96 kHz puis décimation) est inclus dans ces comptes.
- **Total estimé pour un Digitakt II porté, 16 pistes et effets** : 5 à 10 % d'un cœur [INFER]. Ce n'est pas mesuré :
  à mesurer sur le premier port (le filtre Multi-mode et une voix SAMPLE).

## 6. Effort

Repère : les ports exacts du Syntakt (ColdFire) et du Machinedrum (DSP56300 émulé) existent déjà. Ici, il faut porter
du code SHARC compilé (convention C, `53`) en C++.

| Lot | Contenu | Effort agent [INFER] |
|---|---|---|
| Oracle | construire digikit ou sharcvm sur Windows, booter 1.16, capturer entrées et états par routine | 3-5 jours |
| Feuilles | biquad, saturateur, SRR, enveloppe, BR, niveau, pan, les 5 filtres | 1 jour par routine, environ 2 semaines |
| Voix | `0x1c4ecf` et les helpers de pas, polyphase et décimateur, puis STRETCH `0x1c5576` | 1-2 semaines |
| Effets | chorus, delay, reverb, compresseur, master (overdrive, clip, gain) | 2-3 semaines |
| ColdFire | lissage, LFO, conversion des paramètres vers la trame | 1-2 semaines, selon ce qui reste à localiser |

Total : de l'ordre de 6 à 10 semaines agent. Le gain le plus rapide : les filtres et les effets servent aussi au
Digitone II (ticket 13), donc les porter une fois couvre les deux appareils.

Risques : la plupart des attributions de digikit sont encore [D] ou [O], et ses propres notes se corrigent souvent
([C]). Les lectures ci-dessus peuvent donc se révéler fausses. S'y ajoutent l'écart d'un ulp de RECIPS et RSQRTS
avec le silicium, et la licence (mention digikit dans l'interface si son code est repris ; sharcvm est sous GPL-2.0,
incompatible avec l'AGPL-3.0 si on lie le code [INFER], mais utilisable comme outil d'oracle séparé).

## Inconnues

- L'adresse de BR et le rôle exact de la « chaîne fixe » ; le rôle confirmé de chacun des cinq filtres (le mapping
  type + 1 n'est pas vérifié).
- Qui calcule les coefficients des filtres et le temps du delay à partir des paramètres : la ColdFire ou un setter DSP.
- Le coût mesuré de 16 voix qui sonnent vraiment (le scénario `dt2-chord16` n'a pas changé le RMS).
- Le coût CPU de la traduction SHARC vers C d'elektron-emu (Digitone II).
- Si un Digitakt II réel est disponible pour l'oracle du silicium.
- Le comportement des effets à l'arrêt et au changement de kit : aucun chemin de purge trouvé (`53`).
