# Moteur du Machinedrum comme famille de machines

Type: research
Status: resolved
Assignee: agent
Blocked by: 

## Question

Le moteur du
Machinedrum de MoDule (mdEngine) peut-il servir de famille de machines dans une piste de l'instrument ? Peut-on prendre
la voix brute d'une piste MD avant sa propre chaîne (DSP1 : DIST, SRR, filtre, EQ), ne faire tourner que les pistes MD
utilisées, et à quel coût ? Le DIST et le SRR du MD peuvent-ils servir seuls d'étages drive sur n'importe quelle
piste ?

## Answer

Oui. `EngineT` sert de famille de machines : voix i = piste i. La voix brute existe (`EngineT::voiceBlock()`), et il
faut une variante de `render` sans `fxTrack`. La vélocité, LEV et VOL vivent dans le gain du mixeur (`mixerInput.mix[1]`),
donc l'instrument doit l'appliquer. Une piste inutilisée sur GND-- est sautée par le DSP2 et par TrackFx, mais le
moteur garde un coût fixe d'environ 2 % (tick 68k et boucle DSP2). Chaque piste MD jouée coûte ~0,85 % pour sa voix
et ~0,3 % pour sa chaîne. 16 pistes MD en voix brute font ≈ 15,6 %, hors budget seules ; 4 pistes ≈ 5,4 %. Le master
MD coûte +3-4,7 %. DIST et SRR sont du C++ bit-exact, à ~0,03 % par piste. Il faut une entrée publique dans `TrackFx`
et un lissage de paramètres. Ils sont exacts sur l'algorithme mais, à 48 kHz, sonnent ~8,8 % plus haut en fréquence
(SRR, filtre du DIST), sauf à rééchantillonner. Mesures prises sur une machine chargée, à refaire au calme. Détails :
[research/03-moteur-machinedrum.md](../research/03-moteur-machinedrum.md).
