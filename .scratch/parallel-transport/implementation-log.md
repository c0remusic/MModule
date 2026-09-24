# Implémentation du transport parallèle — journal de reprise

Branche : `feat/transport-step0-dating` (base `release/md-mm-alpha`), HEAD
`415aa404` le 2026-09-23. Submodule `source/dsp56300` : fork
`c0remusic/dsp56300-md-mm`, branche `feat/dma-de-observer` (`27d17af`),
`.gitmodules` pointe sur le fork.

Spec de référence : `docs/design/parallel-transport-spec.md`.

## État par marche

| Marche | Commit | État |
|---|---|---|
| 0a visibilité mémoire | `7dcb5521` | vert |
| 0b TimedLinkRing | `2b2dd28a` | vert |
| 0c disposal daté au pop | `21470b0d` | vert |
| 1 D=1 frame en série | `2e25a78b` | vert (D=40 casse mmSine : datation prouvée) |
| 2a miroirs/seams/SPSC | `ee006203` | vert |
| 2b datation HI08 MD + garde inline | `d5a4ccad` | vert |
| 2c worker DSP2 (`MDMM_TRANSPORT=parallel`) | `520ab7a8`…HEAD | expérimental, opt-in ; reproducteur ADC vert, perf < série |

Le mode série (défaut) = 94 % realtime (pluginTester 30 s), identique à la
baseline du ticket 01. Gates série vertes à chaque commit.

## Mode parallel : où on en est

Règles de gates actuelles (`Hardware::producerTargetCycles`) :
- producteur ≤ position UC publiée (conversion rationnelle exacte
  `hostToDspDeadline`, ZÉRO lead — un mot hôte appliqué en retard dans une
  attente masquée du firmware n'est jamais acquitté → tempête de retries) ;
- producteur ≤ cible de bloc + quantum ;
- allowance hôte **seulement pendant que l'UC est bloqué** sur flux plein
  (`setHostWriteBlocked`, temps UC figé) : tête de flux = `drainStart + clamp`,
  `drainStart = max(échéance, pose précédente)` — enveloppe exacte de
  `writeWordToDsp` série. Hors blocage, aucune avance sur l'UC ;
- pose des mots hôte : au bord de lecture HRX (`setReadRxCallback` →
  `landHostToDspWordOnRead`) et chunk worker borné à l'échéance de tête ;
- mixer ≤ UC + D sur le thread audio (`schedStep`, `waitTransportImpl`),
  **strictement** (plus de `max(cap, pos + 1)`) ; aucune gate
  producteur↔mixer (cycle sinon) ;
- position UC publiée **pendant** la tranche UC (toutes les 32 instructions,
  publié ≤ réel) : le producteur court en même temps que l'UC au lieu de
  démarrer chaque tranche après elle ;
- attente lien du mixer (`linkRxAvailable`) bornée à la gate exacte du
  producteur (`hostToDspDeadline(1, UC)`) : toujours satisfaisable.

Reproducteur : `mdParallelTransportFirmwareTest` (vrai Processor+Controller,
40 s série puis 40 s parallel, garde 15 s/bloc, assertions ADC). ~2 min.
**Vert** depuis le correctif du 2026-09-23 (underflow/overflow 0/0).

Défauts 1-3 (ADC) : une seule cause racine, fermée.
- Vers 12,5 s machine, l'UC pousse une rafale de ~265-270k mots vers DSP2
  (même rafale en série), à ~66 cycles DSP/mot.
- L'ancienne allowance cumulative (`+clamp` depuis la position du producteur
  à chaque push) s'auto-entretenait tant que le flux n'était pas vide : le
  producteur prenait 16-20M cycles (~8000 frames) d'avance sur UC et mixer.
- Devant le mixer, le producteur attend sa synchro (lien, Port C) et consomme
  ~110-125 cycles/mot au lieu de 69 → avance qui s'auto-alimente.
- Conséquences : underflow ADC producteur (lit des frames non encore
  poussées) ; puis l'UC voit l'état DSP2 venu du futur, et **cesse de parler
  aux deux DSP** (compteurs de mots figés) → mixer n'horloge plus son ESSI1 →
  overflow ADC mixer. Le défaut 1 n'était pas un bug de file.
- Réfuté en chemin : compter le flux dans TXDE (sans effet, retiré — la série
  montre toujours TXDE=1 après drain) ; la granularité de pose seule (pas
  suffisante).
- Diagnostics gardés sous `MDMM_TRANSPORT_TRACE` : lignes watchdog `adc0/adc1`
  (calls/hits/behind/ahead, profondeur, overflow/underflow) et ligne
  `[worker]   stream` (profondeur, pushed/landed read/chunk/overdue).

Défaut 4 (perf) : parallèle désormais plus rapide que série, cible pas
atteinte. Indicateur : ratio mur parallèle/série **dans le même run** (le
bruit machine fait varier la série de 93 à 139 % d'un run à l'autre).
HEAD `5fc0a764` ~1,08-1,10 → 0,87-0,94 non épinglé, 0,75 threads sur un
même CCX. Cible spec étape 2 ≈ 0,62. Causes trouvées et corrigées :
- **Attente lien cachée** dans les tranches mixer (`m_signal.waitFor` direct,
  hors `waitTransportImpl`, comptée comme temps mixer) : le producteur ne
  pouvait avancer qu'après la publication UC de fin de tranche, le mixer
  (plafonné UC + D) attendait qu'il traverse toute la tranche. Fix :
  publication UC en cours de tranche.
- **Quasi-deadlock lien** : `max(mixerCap, dsp1Pos + 1.0)` laissait le mixer
  dépasser UC + D ; il réclamait un producteur au-delà de l'UC, que l'UC (même
  thread, bloqué) ne pouvait jamais ouvrir → timeout 5 ms, 76-570 fois par
  run (jusqu'à ~2,9 s de mur). Fix : plafond strict + besoin borné à la gate.
- **Faux partage à ~1 M/s** : grappe d'état lien après `m_linkRing`
  (`m_linkRxWasEnabled[2]` stocké inconditionnellement à chaque tick RX
  96 cycles par les deux threads, `m_linkLastShallow`, `m_dmaEnabled`,
  `m_ucRxDepth`) + compteurs lecture/écriture de `RingBuffer` sur une même
  ligne + entrées `TimedLinkEntry` 64 o à cheval sur deux lignes. Preuve :
  même build, seul l'épinglage change → même CCX 87 %, SMT 102 %, CCX
  différents 114 % ; mixer 4,6 → 7,5 ns/cycle. Après fix : cross-CCX mixer
  ~4,4-5,5, worker 3,1.
- `TransportSignal` : réveil perdu possible (StoreLoad) → fence seq_cst ;
  spin borné 50 µs avant park.
Reste : thread audio saturé (~95 %) par UC + mixer ; mixer 5,0 ns/cycle non
épinglé vs 3,6 même CCX. Piste suivante mesurée : placement (worker dans le
domaine L3 du thread audio → ratio 0,75), décision produit à prendre. Churn
de gate worker élevé (300k-1M/2 s, surtout du spin) — hors chemin critique.
Relâchement de timing accepté (verdict adversarial) : pendant une tranche
UC le producteur peut devancer le mixer figé jusqu'à un quantum (enveloppe
L_lead de la spec) ; mots DSP2→UC visibles en milieu de tranche. Validé :
suites série + `mdAudioFirmwareTest`/`mdMidiTimingFirmwareTest`/
`mdUwFirmwareTest` en `MDMM_TRANSPORT=parallel`.
Défaut trouvé en passant (non corrigé) : la purge sur front d'activation RX
(`linkDisposeAtConsumer`) ne peut plus se déclencher après le premier tick
(appelants seulement avec RE=1).

Diagnostics : `MDMM_TRANSPORT_TRACE=1` → watchdog 2 s (phase thread audio,
chunks, positions, clamps par site), sonde de flux hôte, traces d'attente ;
ligne `wall%` (parts de temps mur : tranches UC — attentes et tranches mixer
imbriquées incluses —, tranches mixer, bloqué par site dont lien, exec/park
worker) et ligne `ns/cycle` (coût par cycle émulé et taille des tranches).
Pour isoler un effet de placement : `SetThreadAffinityMask` sur le thread
audio (au handoff) et le worker (Ryzen 3700X : CPU logiques 0-7 = CCX0).

## Saturation CPU (après f8d8992)

`f8d8992` (2026-09-23) : worker dans la bande MMCSS « Pro Audio » (repli
TIME_CRITICAL ; `MDMM_WORKER_PRIORITY=normal|high` pour les A/B) et banc
hôte `mdParallelTransportBenchmark` (exécutable, pas un ctest). Mesures
3700X, blocs 128 à 48 kHz, 4 appelants : sans charge 82-95 % partout (série
~95 %) ; 12 threads de charge time-critical : worker normal 251 %, worker
time-critical 95 % (série 139 %) ; 12 threads MMCSS : worker MMCSS 170 %
(série 199 %). **Tous les cœurs pris (14 threads) : 353 % contre 144 % en
série.** Placement par CPU Sets essayé puis abandonné (pire sous charge).

Cause : sous saturation l'OS ne planifie pas le worker pendant des ms, et
le thread audio attend DSP2 (DspTime, lien, place du flux hôte) sans rien
faire alors qu'il a le CPU.

Branche `claude/transport-producer-help` : jeton d'exécution de DSP2
(`m_producerExec`). Le worker le prend pour chaque tranche ; le thread
audio le prend en try_lock dans `waitSignalOrHelp` (utilisé par
`waitTransportImpl` et l'attente lien de `linkRxAvailable`) quand la
position publiée du producteur n'a pas bougé depuis
`MDMM_PRODUCER_HELP_US` (défaut 50 µs, 0 = désactivé) et exécute alors les
tranches lui-même jusqu'à satisfaction, ou jusqu'à ce que le worker
reprenne le jeton. Hors jeton, seul `dspCycles[1]` publié est lu (garde et
park du worker). Objectif : sous saturation, retomber au coût série au lieu
de le dépasser. Risque restant : worker préempté en pleine tranche (jeton
tenu), borné par la taille d'une tranche (une frame).
Le compteur `helped=` de la ligne `[worker]` (`MDMM_TRANSPORT_TRACE=1`) dit
combien de tranches le thread audio a exécutées.

Mesuré le 2026-09-24 sur le 3700X (PR #1 adoptée, fast-forward) :
- Gate verte : suite série, et en `MDMM_TRANSPORT=parallel`
  `mdAudioFirmwareTest`, `mdMidiTimingFirmwareTest`, `mdUwFirmwareTest`,
  `mdParallelTransportFirmwareTest`, avec aide (défaut) et sans (`=0`).
- Banc, même exécutable, `MDMM_PRODUCER_HELP_US=0` contre défaut, 2 passes :
  sans charge 87 % contre 87 % ; 12 threads MMCSS 103 % contre 103 % ;
  **16 threads MMCSS (plus de threads que de CPU) 317 % contre 155 %,
  p99 21,9 ms contre 8,7 ms, pire bloc 48-56 ms contre 10-12 ms**.
  L'aide ne coûte rien hors saturation et divise par deux le temps quand
  les cœurs manquent — le cas d'un Live chargé.
- Le « 353 % contre 144 % » ci-dessus ne se reproduit pas sur machine
  calme (12 et 14 threads : parallèle 100-110 %, série 110-150 %). Il était
  mesuré avec un serveur vite égaré qui prenait 3,3 cœurs : la machine
  était en fait sursouscrite, régime où série et parallèle sans aide
  tombent tous deux vers 330-375 %.
- Correctifs après revue adversariale : `MDMM_PRODUCER_HELP_US` n'était lu
  que dans `enableParallelTransport` (restauration d'état projet), donc
  ignoré par une instance neuve et par le banc — lu aussi au constructeur ;
  le worker appelait `producerTargetCycles()` hors jeton, qui lit la tête
  du flux hôte pendant que le thread audio peut la dépiler (course de
  données) — la porte hors jeton est désormais `producerGateHint()`
  (valeurs publiées seules), la porte d'autorité avec l'allowance est
  calculée sous le jeton, et une allowance épuisée est mémorisée
  (position + épisode de blocage, `m_hostWriteBlockEpisode`) pour parker
  au lieu de reprendre le verrou en boucle.
- Constats de revue gardés tels quels : un worker préempté en pleine
  tranche garde le jeton (l'aide ne couvre que le worker non planifié
  entre deux tranches) ; `waitSignalOrHelp` tourne par tranches de 50 µs
  sans parker (le thread audio attend activement pendant un bloc).
- Alternative écartée : un exécuteur « help-first » plus complet (jeton
  Free/Worker/Host, bail rendu aux points sûrs, demande de cession sur
  stall, placement Auto mesuré entre threaded et épinglé), branche locale
  `wip/help-first-executor`. Aux mesures : égalité avec la PR #1 dans tous
  les scénarios, l'épinglage ne gagne jamais, cinq fois plus de code.

## Leçons dures

- Le test firmware `mdAudioFirmwareTest` passe en parallel : il ne déclenche
  PAS le trafic du Controller (status/dump requests, retries wall-clock 2 s)
  qui provoque le burst. Vert trompeur. Seul `mdParallelTransportFirmwareTest`
  (ou pluginTester sur le VST3) le reproduit.
- Chaîne de validation : build CIBLÉ des exécutables de gate, `ctest`
  SEULEMENT si `build_exit=0` (les 4 erreurs préexistantes de mdLibTest
  masquaient des builds rouges → tests sur binaires périmés).
- `hdi08().writeRX` bloque sur ring plein (Lock=true) : jamais depuis le
  worker sans `dataRXFull()`.
- Positions publiées avant chaque attente, des deux côtés, sinon gates
  périmées = attentes qui expirent.

## Commandes

```powershell
$env:GEARMULATOR_MD_FIRMWARE_BIN = "$env:LOCALAPPDATA\Programs\Gearmulator-Elektron\elektron_sps1-1uw_os1.63.bin"
$env:GEARMULATOR_MM_FIRMWARE_BIN = "$env:LOCALAPPDATA\Programs\Gearmulator-Elektron\elektron_sfx6-60_os1.32b.bin"
cmake --build temp\cmake_vs22 --config Release -j 6 --target mdAudioFirmwareTest mmAudioFirmwareTest mdMidiTimingTest mdHostRxTimingTest mdAudioQueueTest mdParallelTransportFirmwareTest
cd temp\cmake_vs22
ctest -C Release -R "^(mdAudioFirmwareTest|mmAudioFirmwareTest|mdMidiTimingTest|mdMidiTimingFirmwareTest|mdHostRxTimingTest|mmSineFirmwareTest|mmSineMidiFirmwareTest|mdAudioQueueTest)$"
ctest -C Release -V -R "^mdParallelTransportFirmwareTest$"
```

pluginTester (VST3 MD dans `bin\plugins\Release\VST3\`, ROM copiée à côté de
la DLL) : `pluginTester.exe -plugin "<...>\Gearmulator MD.vst3" -seconds 30
-blocksize 128 -samplerate 44100` ; `MDMM_TRANSPORT=parallel` pour le mode
threadé. Toujours lancer avec garde-temps (`Start-Process` + `WaitForExit`).
