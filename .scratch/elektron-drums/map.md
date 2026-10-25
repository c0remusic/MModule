# Instrument de batterie Elektron (VST)

Label: wayfinder:map

## Destination

Une spécification de la v1 de l'instrument et une feuille de route ordonnée jusqu'à l'instrument complet, sans
décision ouverte : un seul VST de batterie (rien d'autre que des batteries) dont l'interface est l'instrument, façon
Overbridge, 16 pistes, avec les machines des boîtes Elektron, un séquenceur interne, et au plus 15 % d'un cœur à
16 pistes.

## Notes

- Domaine : portage exact de firmwares Elektron (Syntakt fait : `source/elektron/syntakt/stEngine`, `syntakt::Syntakt`,
  le VST3 ST Drums ; Machinedrum exact dans MoDule : `source/elektron/md/mdEngine`). Notes de portage :
  `.scratch/elektron-engines/` (README, cpu1.md, machines.md, analog-literature.md, performance-research.md).
- Règle d'exactitude (amendée 2026-10-24 : la performance passe avant la fidélité) : chaque étage repris d'un firmware
  est vérifié contre l'oracle ; exact à l'échantillon quand cela ne coûte rien, sinon réécrit pour la vitesse (flottant,
  SIMD sur les voix, contrôle au bloc) et vérifié à une tolérance mesurée contre l'oracle. Les ajouts (EQ 3 bandes,
  effets maison, jonction des effets à 16 pistes, modèles analogiques approximatifs) sont signalés comme tels, dans le
  code comme dans l'interface.
- Performance : 15 % d'un cœur au grand maximum à 16 pistes, pour l'instrument final. Toute décision dit son coût.
  Règles du code audio : aucune allocation ni verrou sur le fil audio (tout préparé avant le rendu) ; SIMD sur les
  voix et les étages communs ; suréchantillonnage des drives commutable (off / 2x / 4x, HIIR), off par défaut ;
  dénormaux coupés (FTZ/DAZ) dès qu'un étage passe en flottant. Références : `research/references-dsp.md`.
- Skills à consulter : grilling et domain-modeling pour les tickets de conversation, research pour les recherches,
  prototype pour l'interface, karpathy-guidelines pour tout code. Mémoire : elektron-drum-sampler-goal.
- Les tickets sont en français.
- Interface en JUCE seul (C++, composants dessinés, repeinte des zones qui changent) : pas de RmlUi, de WebView ni
  de JavaScript ; le HTML ne sert qu'aux mockups (décidé le 2026-10-25, ticket « Interface façon Overbridge »).
- MoDule reste un plug-in à part (le Machinedrum exact, émulé) ; l'instrument part de ST Drums, et le Machinedrum y
  entre comme une famille de voix portée en C++ (le mdEngine de MoDule sert d'oracle).

## Decisions so far

- Destination fixée (2026-10-24) : spécification v1 + feuille de route ; v1 = machines Syntakt et Machinedrum (déjà
  exactes), chaîne à emplacements, envois, interface, séquenceur interne ; Model:Cycles, Digitakt II (avec le layering
  d'un sample, amendé : il attend le lecteur du DT2), couche du Rytm et machines analogiques (approximatives, sans
  machine) en jalons suivants. Le FM DRUM du Digitone II est retiré (hors périmètre, 2026-10-24).
- Machines analogiques : exactes sur leur part numérique (port des routines du CPU #1 qui pilotent les circuits),
  approximatives sur le timbre (modèles de circuits réglés à l'oreille sur des enregistrements publics) ; affinables
  plus tard avec des enregistrements réels.
- Aucun émulateur au rendu, aucun firmware chez l'utilisateur visé (2026-10-24) : chaque moteur est du C++ réécrit,
  prouvé à l'oracle ; le Machinedrum aussi est porté (le mdEngine de MoDule, qui émule DSP56300 et 68k, devient
  l'oracle). Les firmwares servent aux tests ; voir les tickets du port du Machinedrum et de l'instrument sans firmware.
- [Fonctions de MoDule Seq](issues/06-module-seq.md): 16 Patterns de 1-64 pas, 7 échelles, 24 Locks, 65 Conditions, ratchets, micro-timing, Swing/Accent/Slide par Pattern, chaînes, transport esclave de Live ; tout en notes vers MoDule (36-51, 64-114, hello 124-127), pas d'enregistrement live.
- [Faisabilité du Digitakt II](issues/12-digitakt-ii.md): faisable par port C++ des routines SHARC (filtres, effets, voix localisés, partagés avec le Digitone II) contre l'oracle digikit ; émulation ~1 cœur exclue, port estimé 5-10 % [INFER], 6-10 semaines agent ; exact à l'émulateur, pas au silicium.
- [Faisabilité du FM DRUM du Digitone II](issues/13-digitone-ii-fm-drum.md): faisable ; routine SHARC+ `0x1c778b` par voix (tampon de 32 flottants à 48 kHz avant ampli et filtre), oracle sharcvm (GPL-2.0+) et digikit (AGPL) ; 3-5 semaines agent, 2-5 % d'un cœur pour 16 voix [INFER], coût réel à mesurer.
- [Moteur du Machinedrum comme famille de machines](issues/03-moteur-machinedrum.md): oui, voix brute via `EngineT::voiceBlock()` (gain vel/LEV/VOL à appliquer soi-même) ; fixe ~2 % + ~0,85 %/piste MD jouée (16 pistes ≈ 15,6 %, 4 ≈ 5,4 %), master MD +3-4,7 % ; DIST+SRR seuls bit-exacts à ~0,03 %/piste via une entrée publique dans `TrackFx`, mais décalés de 48/44,1 hors 44,1 kHz.
- [Machines analogiques sans machine](issues/14-analogiques-sans-machine.md): 30 routines CPU #1 petites (105-778 instr., aides communes), port exact faisable ; piste 12 = six fréquences 808 lues dans le firmware (modèle Werner) ; aucun enregistrement public isolé à réglages connus (meilleur : Science Lab + `.stprj`) ; modèle boîte blanche mesuré 0,03-0,14 % d'un cœur par voix.
- [Séquenceurs des machines Elektron](issues/07-sequenceurs-elektron.md): MD = 24 ppqn, locks, accent, slide, swing, échelles (lu, mesuré, sans conditions/micro-timing/retrigs) ; Syntakt CPU #1 lisible et portable (conditions 0x401181D4, pas 0x4008B882, micro-timing 1/24 pas, retrigs grille 960/ronde, livraison au bloc de 32) ; Digitakt II manuel seulement (128 pas, FILL séparé, 80 locks).
- [Budget CPU de l'instrument à 16 pistes](issues/01-budget-cpu.md): mesuré (machine chargée) : Syntakt 16 pistes 13,7 % quel que soit le nombre de pistes jouées ; MD ≈ 5 % + 1,05 %/piste active (16 ≈ 21,7 %, dont voix DSP 11,7, chaîne+mix 4,5, maître 3,6, tick 1,9) ; MameHq 17 voies 24 % contre 1,2 % pour un polyphasé 16 coefficients ; EQ+2 drives+layer+envois ~3 % à 16 (drives suréchantillonnés 2× +8,6) ; pire cas ~26 % : le MD est à diviser par ~2,2 (port C++ des voix, SoA de la chaîne), le Syntakt à ramener de 13,7 à ~10 %.
- [Effets du Virus](issues/15-effets-virus.md): port C++ exact possible contre Gearmulator (entrée audio du Virus comme banc) mais routines à localiser dans un programme DSP monolithique sans carte, conversions de paramètres incluses ; seule ROM présente : installeur TI d'OsTIrus (pas d'A/B/C) ; pas de flanger Virus à part ; l'Octatrack (ColdFire + DSP56xxx, OS 1.40C public déjà téléchargé, 44,1 kHz) a phaser, flanger, chorus de piste : meilleur candidat, non tenté.
- [Faisabilité du Model:Cycles](issues/11-model-cycles.md): faisable, cible la plus simple : un ColdFire V4e sans DSP, répartiteur `0x400a7d4a` (6 voix, tables render `0x40118610` / update `0x40118628`, 43 routines, 3 414 instr.) qui tourne seul hors OS (sondé dans Unicorn + EMAC SyntHack) ; 5,6-8,5 k instr./voix/bloc de 32 à 48 kHz ; port exact ≈ 0,35-0,5 %/voix, flottant SIMD 3-5× moins [INFER] ; 4-7 sessions agent.
- [Instrument sans firmware chez l'utilisateur](issues/18-sans-firmware.md): le port du Syntakt ne lit aucun code, seulement des données (mesuré par XOR de pages : S7 ~183 Ko de tables et d'image SRAM, S3 ~26 Ko) ; 5 tables régénérées au mot près par formule, le reste (~170 tables) à régénérer par famille, chacune testée contre le firmware (4-8 sessions [INFER]) ; descripteurs, état SRAM du CPU #1 (143 mots), kits et samples (SP TWINSHOT, UW du MD, DT2) remplaçables ; MD : tables DSP/OS à inventorier avec le ticket 17.
- [Port du Machinedrum en C++](issues/17-port-machinedrum.md): DSP2 = 44 renders (39 à porter sans INP/RAM-R), 11 385 instr. statiques, presque rien de partagé hors tables ; 55 fonctions de coefficients 68k (4 714 instr.), master 1 215 instr. ; bit-exact entier mesuré à 0,30 ns/instr. DSP (JIT ~2,7) → ~0,07-0,3 %/voix, ~6-10 % pour 16 pistes avec `TrackFx` [INFER] ; flottant : ×1,5 seulement sur les voix, SIMD utile sur `TrackFx` et le master (~2-5 % pour 16) ; risque audible en flottant : P-I ; 2-3 semaines agent (+1 pour le flottant SIMD) ; ordre ROM/TRX, EFM, E12, P-I.
- [Qualité audio : pratiques et coûts pour l'instrument](issues/19-qualite-audio.md): ne corriger que les ajouts, jamais un étage porté ; PolyBLEP (analogiques), drives HIIR 2x en SIMD sur les voix (0,012 %/voix en AVX2 contre 0,19 % scalaire, ASR -58 contre -24,5 dB : le +8,6 du ticket 01 tombe à ~0,4 % [INFER]), ADAA1 sur l'écrêtage dur, SVF TPT, Hermite (sinc 8 en HQ), rampes 64 éch. sur coupures ajoutées, FTZ/DAZ ; polyphasé du ticket 01 à phase inversée (THD+N -22 dB, corrigé -73) ; 16 tests de qualité et seuils (Q1-Q16).
- [Chaîne de piste : emplacements, ordre, routage](issues/02-chaine-de-piste.md): squelette fixe machine+sample, 2 drives (pré ou post filtre, 7 types, MIX « + » à 100 % ; AMD du MD abandonnée), BAND (BASE/WIDTH, EQ MD, EQ 3 bandes « + »), FILTER 15 types avec enveloppe commune, AMP, envois post-ampli ; chaîne de la famille par défaut ; mono jusqu'au pan ; suréchantillonnage global, 2x par défaut.
- [Une fréquence commune pour tous les moteurs](issues/16-frequence-commune.md): tous les moteurs à la fréquence de l'hôte, plus aucun rééchantillonneur ; exacts à leur fréquence d'origine, adaptés ailleurs, test « sonne pareil » 44,1/48 kHz par machine ; 88,2/96 kHz rendus à mi-fréquence + HIIR 2x ; lo-fi à fréquence absolue ; blocs de 32 ; tolérance = 16 tests de la recherche qualité ; Syntakt livré en réécriture rapide, port exact gardé comme référence.
- [Source de machine d'une piste](issues/04-source-de-machine.md): une voix mono par piste, de n'importe quelle famille, familles mélangées sans limite ; voix éteintes non rendues ; aucun coût fixe par famille ; changer de machine remet ses défauts et retire ses locks (annulable) ; chokes par piste cible (pas de groupes) et LFO entre pistes.
- [Layering d'un sample dans la piste](issues/05-layering.md): lecteur = moteur sampler du DT2 porté, donc le layering quitte la v1 pour le jalon du Digitakt II ; sommé à la machine avant les drives ; niveau propre, accord suivant la note, début, longueur, boucle, sens, enveloppe propre, décalage 0-100 ms ; éteint par défaut.
- [Paramètres hôte et sorties](issues/10-parametres-hote.md): tout automatisable (~1 260 paramètres, identifiants figés), paramètres de machine génériques à valeur nommée ; machine et famille hors automation ; 64 relais en 8 pages pour Push ; Main + 15 sorties stéréo, piste séparée sans effets d'envoi, retours d'effets routables, master sur Main.
- [Sources des effets d'envoi](issues/20-effets-envoi.md): delay et reverb au choix (Syntakt en v1, Echo et Gate Box du MD avec son port, DT2 à son jalon) ; chorus, phaser et flanger maison réglés à l'oreille contre le Virus TI d'OsTIrus, chorus DT2 en plus à son jalon ; un exemplaire partagé de chaque, retours parallèles sauf delay vers reverb ; master renvoyé à « Effets master », après l'inventaire du DT2.
- [Séquenceur interne](issues/08-sequenceur-interne.md): fonctions de MoDule Seq en interne, longueur et échelle par piste, 128 patterns de 64 pas, transport de l'hôte, pattern par paramètre ou program change ; locks illimités, slide, conditions à graine ; trigs à l'échantillon près ; box de pas + lignes de locks ; mode LIVE ; Push et euclidien plus tard ; export MIDI.
- [Effets master](issues/24-effets-master.md): compresseur puis overdrive du DT2 en v1, portés en avance (premier morceau du port du DT2, harnais d'oracle d'abord), ordre et réglages du firmware ; overdrive analogique du Syntakt « ≈ » au jalon analogique ; chaîne amendée : BR dans la voix DT2, BAND avant/après filtre avec le base-width du DT2.
- [Cartographie du firmware du Digitakt II](issues/23-cartographie-dt2.md): 1 147 fonctions DSP, 123 (12 278 instr.) dans la boucle de bloc ; chaîne voix → BR `0x1cdd8b` (trouvé) → overdrive/SRR/base-width pré ou post → filtre → env. ampli ; 6 filtres vérifiés (index = type+1), coefficients sur le DSP ; master = compresseur + même saturateur, pas d'EQ ; LFO sur ColdFire ; coûts statiques seulement (~6,5-8,4 k instr./voix), oracle dynamique pas prêt (sharcvm DN2 seul, coupled-run sans entrées).
- [Format des kits et de l'état](issues/21-format-des-kits.md): kit = tout le son, patterns à part avec un kit en référence ; banque de 64 kits partagée, patterns dans le projet ; état = kit joué, patterns, globaux, samples ; format versionné par identifiant ; sons de piste ; contenu d'usine maison ; samples de l'utilisateur et contenu Elektron du propriétaire importé (dossier « Elektron », bouton d'import), jamais livré ; explorateur 3 colonnes, par source ou par type (deviné, corrigeable).
- [Ordre des jalons après la v1](issues/22-ordre-des-jalons.md): v1 = Syntakt rapide, MD porté, chaîne, envois, master DT2, séquenceur, kits, interface, sans firmware chez l'utilisateur ; puis J2 Digitakt II, J3 Model:Cycles, J4 analogiques, J5 couche Rytm ; même critère de fin pour tous (oracle/tolérance, 44,1 = 48, ≤ 15 %, gate vert, sans firmware) ; ensuite spécification v1 puis tickets.
- [Interface façon Overbridge](issues/09-interface.md): mockup v12 approuvé, référence de l'interface v1 ; une box par section, disposition fixe, couleur saturée par section ; clic pose un trig, Ctrl/Maj+clic sélectionnent ; JUCE seul.

## Not yet specified

- Couche échantillon du Rytm.
- Nom du produit.

## Out of scope

- Les synthés (Digitone FM TONE, WAVETONE, SWARMER, Analog Four, Monomachine) : l'instrument ne fait que des
  batteries.
- [Le FM DRUM du Digitone II](issues/13-digitone-ii-fm-drum.md) : faisable d'après sa recherche, mais retiré par
  l'utilisateur le 2026-10-24, qui ne s'en servira pas.
