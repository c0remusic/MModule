# L'ANTI-CLICK d'X.20, réglable

Type: task
Status: open
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
