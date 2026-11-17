# Drive Cycles dist

Type: AFK
Status: done
Assignee: Claude (session 2026-11-17)
Blocked by: 01

## Quoi

Nouveau type de drive, après les types actuels : l'étage VOL+DIST du Model:Cycles, VOL en gain d'entrée de DIST.
Jugé contre les vecteurs de `elektron-work\vectors-cycles` qui le couvrent.

## Critère de fin

- Tests Q contre les vecteurs.
- Même son à 44,1 et à 48 kHz (tests Q de `research/19-qualite-audio.md`).
- Aucun set existant ne change de son.
- Coût mesuré sur machine calme et rapporté face à son guide (spécification v2, §3) ; le pire kit tient sous 15 %.
- Gate vert, nouveau test inscrit dans sa liste.

## Références

Spécification v2, §6 (`../elektron-drums/spec-v2.md`) ; ses tickets de décision dans `../lockstep-post-v1/issues/`.

## Réponse

Fait le 2026-11-17 (Claude). Type `DriveType::CyclesDist` après `Overdrive`, « Cycles dist » dans la liste des drives du
plug-in (maximum du paramètre 5 → 6, comme l'ajout d'Overdrive l'avait fait : une automation d'hôte normalisée sur le
type glisse d'un cran, un set rangé garde son indice). Code dans `edDrumsMaster.*` (`CyclesDist`), câblage dans
`edDrumsChain.*`, test `edDrumsCyclesDistTest` au garde-fou (variable `CYCLES_DIST_VECTORS`).

### L'oracle : la routine `0x40056f58` de l'OS 1.13

Un seul mot, le 19 (« Volume +Dist », 0 à 0x7f00, défaut 0x3c00 = 60), lu lissé ; VOL et DIST ne sont pas deux
réglages mais les deux moitiés du même bouton. Par bloc et par piste :

- cible de gain t = ((w − 0x4000)² ≪ 3) en Q31 si w > 0x3fff, sinon 0 (0,969 à 127) ; gain = b + (1 − b) t avec
  b = 0x13a57c52 / 2³¹ = 0,1534877, les deux mots de l'OS sommant à 1 exactement (les 0,8465 et 0,1534 de l'Overdrive
  DT2 en sont un ajustement) ;
- parts dry/wet : w ≤ 0x3c00 dry 1 ; de 60 à 68 wet = (VOL − 60) / 8, dry = 1 − wet ; au-dessus wet 1 ;
- gain, dry et wet rampent linéairement sur le bloc depuis la valeur du bloc précédent (pas = différence ≫ 5) ;
- par échantillon : u = gain · x, trois passes de u = u (2 − |u|) (|u| en complément à un), sortie = dry · x + wet · u.
  Pas de borne avant la courbe : gain < 1 et |x| < 1 gardent |u| < 1. **C'est le saturateur de l'overdrive du
  Digitakt II** (`saturate`), sous d'autres lois du bouton : le port compose un `Overdrive` avec ses propres gain et
  part (`Overdrive::set(lane, gain, mix)`), rien de nouveau sur le chemin audio.

La moitié basse du bouton n'est pas dans cette routine : le mix `0x400567ba` lit aussi le mot 19 et met la piste à
w / 0x4000 sous 0x4000 (unité au-dessus). Le drive Lockstep couvre la routine seule : AMOUNT ≤ 60 laisse la piste
telle quelle, le niveau de piste de Lockstep tient lieu du volume (décision du ticket 12 de lockstep-post-v1). Le texte
d'AMOUNT lit OFF jusqu'à 60, puis le gain dans la courbe en dB.

Un modèle entier de la routine (EMAC fractionnaire tronquant, en Python) retrouve les vecteurs à 7 LSB près sur les 400
blocs. Les 108 vecteurs de `vectors-cycles` jouent tous à VOL 60, où la routine est un contournement (sortie = entrée à
1 − 2⁻³¹) : ils ne prouvaient rien de la distorsion.

### Vecteurs

`C:\dev\elektron\elektron-work\vectors-cycles-dist\` (hors dépôt, README à côté) : `tools\cycles\vectors_cycles_dist.py`
sur `cyclesbench.py`, lancé par `run_dist.ps1` (cinq processus, garde de 40 min, 5 min en tout). 15 valeurs de VOL (0,
32, 60 à 68 pas à pas, 80, 96, 112, 127) × les six machines au son par défaut, vélocité pleine, 400 blocs ; par cas
l'entrée de l'étage (après gain de piste) et sa sortie.

### Tests Q (`edDrumsCyclesDistTest`, 90 cas)

| Test | Mesuré | Seuil |
|---|---|---|
| Q3 SNR sur le Hit, pire des 90 cas | 137,3 dB | ≥ 70 |
| Q3 pire fenêtre de 10 ms | 90,4 dB | ≥ 60 |
| Formule (chaîne contre `saturate` sous les lois) | −150,5 dB | 1e-6 |
| Dry jusqu'à 60 (chaîne) | échantillons égaux | — |
| 16 lanes contre chaque lane seule | 102 400 échantillons identiques | bit à bit |
| 44,1 contre 48 kHz | échantillons identiques | bit à bit (étage sans mémoire) |

La SNR tient à b : avec 0,1536 (arrondi d'abord écrit) elle tombait à 63 dB ; 1e-4 d'écart sur b coûte 75 dB parce que
la sortie est dominée par le terme linéaire 8 g x.

Aucun set existant ne change : les types gardent leurs numéros, l'Overdrive garde ses lois (`overdrive()` et
`avxKernels()` de `edDrumsChainTest` inchangés et verts).

### Coût

Étage seul, 16 lanes, AVX2 : 0,07 % d'un cœur à 44,1 kHz (le même code que l'Overdrive). Guide de la spécification
v2 §3 : chaîne ≤ 2,0 %. L'original : ≈ 1 100 instructions ColdFire par piste et par bloc pour gain + VOL+DIST.

`edDrumsWorstKitCost` au garde-fou du 2026-11-17 (machine calme, 0,6 cœur ailleurs) : drives les plus chers DAC et
Cycles dist (6), chaîne 3,15 % (guide 2,0, sous le 4 % du ticket 26), total 10,93 %, PASS sous 15 %. Le Cycles dist
coûte l'Overdrive, à égalité dans le bruit du banc (un premier gate, 1,6 cœur ailleurs : chaîne 3,86 %, total 11,38 %,
rouge sur `sophieTest` seul, ignoré faute de `SOPHIE_ORIGINAL_DIR` au configure de ce worktree neuf ; vert reconfiguré).

### Mis de côté

- `Chain::raised()` suréchantillonne le Cycles dist en post dans une chaîne Syntakt dès que le facteur monte, comme
  l'Overdrive ; quand la famille Cycles entrera (ticket 19), sa chaîne par défaut voudra le garder à 48 kHz, « un drive
  d'un firmware là où son firmware l'a ».
- Le texte d'AMOUNT entre 61 et 67 montre le gain (−16 dB) alors que la part wet est partielle ; même forme que
  l'Overdrive sous 16.
- La liste des VOL et les six noms de machines sont dupliqués entre `run_dist.ps1` et le test.
- `limited()` borne désormais le type d'un drive à `DriveType::Count` (un type hors plage indexait les tableaux d'un
  emplacement) : trou préexistant, bouché au passage.
- L'Overdrive DT2 porte 0,8465 et 0,1534 ajustés ; l'OS du Cycles dit b et 1 − b. Aligner le master DT2 serait un
  autre ticket (ses propres vecteurs).
