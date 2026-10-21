# L'ANTI-CLICK d'X.20, réglable

Type: task
Status: resolved
Assignee: c0remusic (session du 2026-10-20)
Blocked by:

## Question

[research/13](../research/13-anti-click.md) décrit l'ANTI-CLICK d'X.20 : un réglage global OFF, 64 ou 128 ; le TRIG
d'un Hit retenu 2 ou 4 blocs par la DSP des voix pendant que l'ancienne voix joue encore ; la voix brute de la Track
baissée en ligne droite de 1 à 0 pendant l'attente, avant ses effets ; aucun fondu pour les 20 intégrées continues
d'X.20 ni pour les Machines utilisateur qui posent CONTINUOUS RETRIGGER.

Décidé par l'utilisateur le 2026-10-20 :

- un réglage OFF / 64 / 128 (« anticlick avec reglage ») ;
- les Machines adoucies comme X.20 : toutes, sauf les 20 intégrées continues et les Machines utilisateur CONTINUOUS
  RETRIGGER ;
- tous les Hits alignés : avec 64 ou 128, chaque Hit attend autant, et MoDule annonce la latence à l'hôte, qui compense ;
  OFF garde la latence d'aujourd'hui (83 échantillons) ;
- un réglage global, comme X.20 : dans la configuration du plug-in, pour toutes les instances, pas dans le set.

À construire : le TRIG retenu (`HostModel`), la rampe sur la voix brute avant `fxTrack` (`EngineT::render`), le délai
de chaque Hit allongé de 64 ou 128 et la latence annoncée (`Engine::HitDelay`, le Processor), le tampon de délai agrandi
(research/13 : de 128 à 256), l'entrée du Menu, les tests (le fondu contre la rampe d'X.20, un Hit qui arrive pendant
l'attente, la latence annoncée et tenue à l'échantillon).

## Answer

Construit le 2026-10-20 :

- `HostModel::setFadeBlocks` (0, 2 ou 4) : un Hit d'une Machine qu'X.20 adoucit (`HostModel::fades` : pas ses 20
  intégrées continues, ni une Machine utilisateur CONTINUOUS RETRIGGER, la Machine que le Hit amène décidant) attend
  2 ou 4 blocs avec ses Locks, son accent et son slide ; la voix garde ses mots pendant l'attente, comme X.20 détourne son
  DMA ; sa sortie brute descend la rampe du mixer d'X.20, `in · (g − i·s)` tronqué comme le `mpy` du DSP ; un Hit pendant
  l'attente remplace celui qui attend et le compte continue ; le Link est frappé à la note, comme toujours.
- `mdDrums::Engine::setFade` : un Hit adouci garde son délai pour le bloc où il part, tout autre Hit attend autant que le
  fondu ; chaque Hit sonne `hitDelay()` = 83 + 64 ou 128 échantillons après sa note, sur Main et sur son Out. L'anneau
  des voies passe à 256 échantillons avec le fondu, et reste à 128 sans.
- Le Device annonce la latence (`getInternalLatencyMidiToOutput`), la capture du Hit part d'autant plus tard ;
  `Processor::setAntiClick` écrit la configuration du plug-in (`antiClick`), le Device la prend sous son verrou, le Plugin
  relit la latence (`Plugin::refreshDeviceLatency`) et l'hôte l'apprend. Chaque Device la lit à sa création.
- Le Menu : « Anti-click: off », « 64 samples », « 128 samples ».
- Vérifié : `mdDrumsEngineTest` (à 64 et 128, le premier Hit de TRX-SD attend 2 ou 4 blocs ; frappé de nouveau, ses
  échantillons pendant l'attente sont ceux de la voix sans second Hit fois la rampe d'X.20, 0 différent ; le Hit part
  ensuite ; un troisième Hit pendant l'attente ne change rien ; TRX-BD n'attend jamais ; sur `mdDrums::Engine`, TRX-BD et
  TRX-SD frappés ensemble arrivent tous deux 128 échantillons plus tard) ; `mdDrumsProcessorTest` (à 44,1 kHz la latence
  annoncée passe de 83 à 211 et la note sonne 128 plus tard ; à 48 kHz 139 de plus, à la gigue du rééchantillonneur
  près ; OFF les remet). Off, tous les tests d'avant passent inchangés.
- CPU : `mdDrumsEngineTest` avant et après, trois fois chacun en alternance, 22,5 à 25,8 % d'un cœur des deux côtés (la
  machine chargée) : aucun écart mesurable.
- Le réglage n'est relu par les autres instances qu'à la création de leur Device.
