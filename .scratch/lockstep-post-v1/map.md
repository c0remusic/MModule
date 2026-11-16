# Lockstep après la v1

Label: wayfinder:map

## Destination

Une spécification post-v1 (`spec-v2.md`) et une feuille de route ordonnée, sans décision ouverte, pour les jalons qui
suivent la v1 de Lockstep, dans cet ordre : DRIVE analogique du Syntakt (option ANALOG ≈ du drive Syntakt et de la saturation du
master), Digitakt II (sampler, layering,
6 filtres, SRR, BR, base-width, delay, reverb et chorus), Model:Cycles, couche sample du Rytm, compagnon Push. La
construction se fait ensuite dans une carte de construction à part, comme `../elektron-drums-v1/`.

## Notes

- Point de départ : la v1 livrée (`../elektron-drums-v1/map.md`, ses tickets), sa spécification
  (`../elektron-drums/spec-v1.md`) et la carte de planification v1 (`../elektron-drums/map.md`) avec ses décisions,
  qui tiennent sauf ticket contraire ici : ordre des jalons (ticket 22, amendé : l'overdrive passe en tête), layering
  (05), DT2 (12, 23), Cycles (11), effets d'envoi (20) et master (24).
- Exactitude (2026-11-15) : la règle du Syntakt rapide pour chaque nouveau moteur. Réécriture flottante/SIMD jugée
  contre l'oracle à une tolérance mesurée (tests Q de `../elektron-drums/research/19-qualite-audio.md`), et à
  l'oreille ; pas de bit-exact qui coûte du CPU. Aucun émulateur au rendu, aucun firmware chez l'utilisateur.
- Contenu : le sampler joue les samples de l'utilisateur (le Locker), jamais le contenu d'usine d'Elektron. Rien du
  firmware dans le dépôt, sauf des constantes mesurées ; les firmwares et les vecteurs restent hors du dépôt
  (`C:\dev\elektron\elektron-work`).
- Effets du DT2 : ajoutés comme types des envois existants, après les types actuels, pour que les sets gardent leur
  choix.
- Overdrive analogique du Syntakt : pas de machine pour mesurer, donc modèle approximatif signalé comme tel (« ≈ »).
- Budget : 15 % d'un cœur au plus à 16 pistes pour l'instrument complet, DT2 et Cycles compris ; chaque décision dit
  son coût.
- Firmwares sur ce PC : Digitakt II 1.17 et Analog Rytm MKII 1.74 (`Downloads`), vecteurs `vectors-dt2`,
  `vectors-rytm`, `rytm-samples`. Model:Cycles 1.13 dans `%APPDATA%\Elektron Emu`, sections
  extraites dans `model-cycles-1.13`.
- Skills : grilling et domain-modeling pour les tickets de conversation, research pour les recherches, prototype pour
  l'interface, karpathy-guidelines pour tout code. Tickets en français.

## Decisions so far

- [Circuit de l'overdrive analogique du Syntakt](issues/01-circuit-overdrive-syntakt.md): le Syntakt n'a pas
  d'overdrive de master ; l'étage visé est le DRIVE du bloc Analog FX, insert stéréo analogique DRIVE, filtre, AMP sur
  un bus à part avant le mix, entrées routées une à une ; un seul réglage DRIVE (un CV) ; circuit inconnu, pas
  d'enregistrement isolé : modèle « ≈ » à l'oreille d'après les avis.
- [Le moteur sampler du Digitakt II : ce qu'il fait](issues/04-sampler-dt2-fonctions.md): six machines sample
  (ONESHOT, WERP, STRETCH, REPITCH, SLICE, GRID), boucles, ±5 octaves, sample par pas, 16 bits 48 kHz mono ou stéréo ;
  BR, overdrive, SRR, base-width et AMP sont dans la chaîne, que Lockstep a déjà : seule la voix est à porter ; s'oppose
  à la mono, aux 44,1 kHz, à l'unique sample par piste, au GRAIN binaire de Machine 9 et à STRT/END.
- [Place du Digitakt II dans Lockstep](issues/05-place-du-dt2.md): pas de famille DT2 ; le Sampler gagne un lecteur DT2
  (GRAIN à trois valeurs, sets migrés), MODE à six machines, la couche le lit en ONESHOT ; mono, 48 kHz puis
  `fast::Host` ; BR, SRR et base-width en types de chaîne ; ordre : lecteur, layering, filtres, BR/SRR/base-width,
  WERP/SLICE/STRETCH, envois.
- [Couche du Rytm et layering du DT2 : une couche par piste ou deux](issues/09-une-ou-deux-couches.md): une seule
  couche sample sur toute piste, lecteur DT2 en ONESHOT, réglages du layering v1 plus un BR propre ; le Rytm entre
  par traduction à l'import, son lecteur seulement comme choix de cette couche si « Où le Rytm rend sa couche
  sample » le montre distinct.
- [Analog FX du Syntakt dans Lockstep : place, modèle, réglages](issues/02-modele-overdrive-master.md): pas de bus
  Analog FX ; le DRIVE analogique devient l'option ANALOG ≈ du type « Syntakt drive » et de la saturation du master
  (DT2 par défaut), modèle approché jugé à l'oreille et par cinq tests mesurés, ~0,1 % par piste.
- [Plusieurs samples par piste](issues/11-plusieurs-samples-par-piste.md): un lock de sample par pas, sur les
  trois lecteurs et la couche sample, qui nomme un sample du Locker par empreinte ; chargés seuls, sans réserve ni
  numéros ; ni LFO ni hôte ; sample absent : celui de la piste.
- [La couche sample du Rytm dans son firmware](issues/08-couche-sample-rytm.md): seul le lecteur est numérique (16
  bits, 48 kHz, mono, TUNE, FINE, BR, SMP, STRT/END, LOOP, LEV), sommé à la voix avant overdrive et filtre analogiques,
  donc à la place du layering ; rendu sur le ColdFire principal [INFER], routine pas trouvée ; portable par vecteurs
  pour la traduction des paramètres, le reste approché par les étages de Lockstep.
- [Où le Rytm rend sa couche sample](issues/10-lecteur-rytm-localise.md): sur le ColdFire, sommée en numérique
  au bruit et au clic ; interpolation identique au DT2, BR différente (pas continu, dans la voix) : la BR de la couche
  sample suit la loi du Rytm, celle du type de chaîne BR la loi du DT2 ; pas de lecteur à part.
- [OS du Model:Cycles](issues/06-os-model-cycles.md): déjà sur ce PC, 1.13 ; section 3 extraite hors dépôt
  (`elektron-work\model-cycles-1.13`), hachage et adresses du ticket 11 retrouvés.
- [Oracle dynamique de la voix sampler du Digitakt II](issues/03-oracle-voix-dt2.md): l'oracle tourne hors dépôt
  (ONESHOT et STRETCH, 21 vecteurs) ; ~3 600 instructions SHARC par voix et par bloc (STRETCH ~1 800) ; décimateur
  équivalent à celui du Rytm, LEV = 2 × (LEV/128)².
- [État réel de l'OS du Model:Cycles et ordre de ses machines](issues/07-etat-os-cycles.md): ordre KICK, SNARE,
  METAL, PERC, TONE, CHORD confirmé ; 108 vecteurs hors dépôt depuis le code de l'OS par morceaux (pas de boot
  complet) ; 5 300 à 8 300 instructions par voix et par bloc ; la vélocité agit après la voix.
- [Les machines du Model:Cycles dans Lockstep](issues/12-machines-cycles-dans-lockstep.md): famille Cycles à six
  machines, réglages dans Machine 1-8, chaîne par défaut « Cycles dist » (VOL+DIST) en post, vélocité en gain après
  la voix et VEL DEST étendu aux réglages de la machine, LFO de Lockstep, port SIMD ≈ 0,1 % par piste.
- [Répartition du budget CPU entre les jalons](issues/13-budget-des-jalons.md): 15 % jugés sur le pire kit ;
  guides moteurs 10, couche sample 1,5, chaîne 2,5, envois 0,7, master 0,3 % ; dépasser un guide se dit, dépasser
  le plafond s'optimise ; banc `edDrumsWorstKitCost` au garde-fou.

## Not yet specified

- Effets du DT2 en envois : réglages exposés, ce que chacun partage avec les types actuels, ordre dans les choix.
- Compagnon Push (Max for Live) : sa forme dépend du séquenceur tel qu'il sera ; à cadrer après les moteurs.
- Rédaction de `spec-v2.md` et découpe en tickets de construction, à la fin.

## Out of scope

- Import de sons et de kits Model:Cycles (`.mc-snd`) : hors de ce jalon, décidé avec « Les machines du Model:Cycles
  dans Lockstep » ; les sons d'usine restent exclus.

- Euclidien et aléatoire (notés « plus tard » dans la spec v1) : un effort à part.
- Les synthés (Digitone, Analog Four, Monomachine) et le FM DRUM du Digitone II, comme en v1.
