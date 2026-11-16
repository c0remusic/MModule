# Les machines du Model:Cycles dans Lockstep

Type: grilling
Status: resolved
Assignee:
Blocked by: 07

## Question

L'ordre des six machines du Cycles (KICK, SNARE, METAL, PERC, TONE, CHORD), leurs vecteurs et leur coût sont connus
(« État réel de l'OS du Model:Cycles et ordre de ses machines »). Comment entrent-elles dans Lockstep : leur famille
et leur place dans Machine 1-24, leurs réglages (PTCH, DEC, COLR, SHPE, SWEP, CONT, PUNCH, GATE) et leur page, ce
qu'elles prennent de leur chaîne d'origine (gain de piste, VOL+DIST, vélocité appliquée après la voix) et ce que
Lockstep leur donne déjà (drives, filtres, LFO), et le port visé (flottant SIMD comme le Syntakt rapide, jugé contre
les vecteurs) avec son coût estimé.

## Answer

Décidé avec l'utilisateur le 2026-11-16 (grilling).

- **Famille Cycles**, la troisième à côté de Syntakt et MD : KICK, SNARE, METAL, PERC, TONE, CHORD. Changer de machine
  dans la famille ne touche pas la chaîne ; changer de famille recharge la chaîne par défaut (spec v1).
- **Réglages :** PTCH, DEC, COLR, SHPE, SWEP, CONT dans Machine 1-6, PUNCH et GATE dans Machine 7-8 ; plages et défauts
  des fiches de paramètres de l'OS (« État réel de l'OS du Model:Cycles et ordre de ses machines ») ; la page ne
  montre que ces réglages.
- **Chaîne par défaut :** VOL+DIST du Cycles devient le type de drive « Cycles dist », après les types actuels, en post
  dans la chaîne de la famille ; VOL y reste le gain d'entrée qui pousse DIST, comme sur la machine ; le niveau de
  piste de Lockstep reste à part, après la chaîne. Pas de filtre par défaut (le Cycles n'en a pas) ; ceux de Lockstep
  restent disponibles, marqués « + ».
- **Vélocité :** en gain linéaire après la voix, loi du Cycles (elle n'entre pas dans la voix, mesuré). VEL DEST, sur une
  piste Cycles, garde ses cibles de chaîne et gagne les réglages de la machine (PTCH, DEC, COLR, SHPE, SWEP, CONT) :
  c'est la destination de vélocité du Cycles, sans deuxième paire. Les autres familles ne changent pas ; donner leurs
  réglages de voix à VEL DEST serait un ticket à part.
- **GATE :** allumé, la voix tient tant que la note tient (note-off de l'hôte ou durée du trig dans Seq) ; éteint, elle
  joue son déclin entier.
- **LFO :** les deux LFO de Lockstep, pas de port du LFO du Cycles.
- **Port :** règle du Syntakt rapide : flottant SIMD, 16 pistes sur les voies, 48 kHz puis `fast::Host`, jugé contre
  les 108 vecteurs par les tests Q et à l'oreille, sans bit-exact. Coût estimé ≈ 0,1 % d'un cœur par piste active
  [INFER : 5 300 à 8 300 instructions ColdFire par voix et par bloc, port SIMD 3 à 5 fois sous le port exact] ; mesuré
  au banc.
- **Hors de ce jalon :** l'import de sons et de kits Cycles (`.mc-snd`) ; les sons d'usine restent exclus.
