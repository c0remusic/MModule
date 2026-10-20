# Construire le choix des sources d'une NFX

Type: task
Status: resolved
Assignee: c0remusic (session du 2026-10-20)
Blocked by: 29, 30

## Question

Construire ce que « Ce que la DSP des voix doit faire pour qu'une NFX entende n'importe quelle Track » établit et ce que
« Choisir les Tracks qu'entend une NFX » fait choisir : les tampons et les descripteurs, le choix gardé où le prototype
l'a mis, la ligne et le menu. Tests : une NFX en Track 12 qui entend la Track 3, bit pour bit avec la copieuse de
`mdDrumsTestMachine.h` ; une source rendue après l'effet, un bloc plus tard ; un set sans choix qui sonne comme
avant ; la latence note→son et le CPU, avant et après.

## Answer

Construit le 2026-10-20 comme research/14 §7 le recommande :

- `VoiceEngine` : le stub à P:`$C40` remplace l'anneau ; Track t rend dans Y:`$1000` + 32·t, Y:`$7F0` pointe son
  descripteur Y:`$7A0` + 4·t, une User Machine reçoit d'abord dans son tampon le bloc de sa quatrième source
  (P:`$C80`). `setSources`, `sources`, `defaultSources` ; `reset()` les garde.
- `mdDrums::Engine::setSources` ; une NEIGHBOUR prend le délai de sa première source. L'aperçu frappe les sources
  choisies (`HitPreview::Request::sources`).
- Le message Sources de MoDule (`$08`), l'état du Device (un message par Track hors défaut), `Controller::setSources`,
  `Bank.machines` (64 octets de plus par Slot, `$80` + la Track, rien pour un Kit sauvé sans).
- Vérifié : `mdDrumsEngineTest` (les copieuses d'avant inchangées ; une copieuse en Track 12 qui entend Track 3, une,
  deux et quatre en arrière, le bloc même ; en Track 3 qui entend Track 12, le bloc d'avant ; sur `mdDrums::Engine`,
  calée à 0 sur sa source avant elle, à 32 sur une source après), `mdDrumsDeviceTest` (l'état garde et rend le choix,
  un état sans donne le défaut), `mdDrumsBankTest`, `mdDrumsSkinTest` (le menu, le Kit sauvé et rechargé).
- Coût et latence : `mdDrumsEngineTest`, 16 Tracks et les effets master, 21,2 % d'un cœur après, 21 % avant (le
  stub coûte 11 cycles par voix, research/14 §4) ; le Hit sonne toujours `HitDelay` (83) échantillons après sa note, et
  une NEIGHBOUR sur sa source d'avant reste calée à 0.
