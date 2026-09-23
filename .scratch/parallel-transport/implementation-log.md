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
- mixer ≤ UC + D sur le thread audio (`schedStep`, `waitTransportImpl`) ;
  aucune gate producteur↔mixer (cycle sinon).

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

Défaut restant :
4. Perf : test processor 102 % mur/audio en parallèle vs 93 % en série
   (mesure non tracée, 2026-09-23) — le mode parallèle est encore PLUS LENT
   que la série. Churn de parks et notify par chunk suspects ; pluginTester
   pas remesuré depuis le correctif.

Diagnostics : `MDMM_TRANSPORT_TRACE=1` → watchdog 2 s (phase thread audio,
chunks, positions, clamps par site), sonde de flux hôte, traces d'attente.

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
