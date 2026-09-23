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
| 2c worker DSP2 (`MDMM_TRANSPORT=parallel`) | `520ab7a8`…`415aa404` | expérimental, opt-in |

Le mode série (défaut) = 94 % realtime (pluginTester 30 s), identique à la
baseline du ticket 01. Gates série vertes à chaque commit.

## Mode parallel : où on en est

Règles de gates actuelles (`Hardware::producerTargetCycles`) :
- producteur ≤ position UC publiée (conversion rationnelle exacte
  `hostToDspDeadline`, ZÉRO lead — un mot hôte appliqué en retard dans une
  attente masquée du firmware n'est jamais acquitté → tempête de retries) ;
- producteur ≤ cible de bloc + quantum ;
- plus d'allowance cumulative : chaque push hôte accorde `+clamp` (100 000
  cycles) depuis la position courante du producteur (enveloppe exacte de
  `writeWordToDsp` série) — c'est ce qui a fermé le deadlock du burst ;
- mixer ≤ UC + D sur le thread audio (`schedStep`, `waitTransportImpl`) ;
  aucune gate producteur↔mixer (cycle sinon).

Reproducteur : `mdParallelTransportFirmwareTest` (vrai Processor+Controller,
40 s série puis 40 s parallel, garde 15 s/bloc, assertions ADC). ~2 min.

Défauts ouverts, mesurés par ce test (parallel) :
1. Overflow de la timeline ADC du **mixer** (1,1M frames / 1,9M) : le mixer
   ne draine pas sa file — probablement cap mixer ≤ UC+D combiné au skip idle
   UC (l'UC saute des frames, le mixer suit par slices d'une frame ?).
   À instrumenter en premier : position du mixer vs cible de bloc au fil du
   rendu, et `readAt` (frame demandée vs tête de file).
2. Underflow ADC du **producteur** (15k frames) : l'allowance cumulative le
   laisse dépasser la marge `g_hostAudioInputSafetyFrames` (64). Plafonner
   l'allowance à cible + 64 frames, ou faire lire l'ADC en retard borné.
3. Run « 38 % » = les deux receivers décrochés (overflow 1,37M chacun) :
   faux chiffre, à ne jamais prendre pour une perf.
4. Perf quand ça marche : 41-82 % realtime (pluginTester), 136 % dans le
   test processor — churn de parks (20 % des chunks) et notify par chunk.

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
