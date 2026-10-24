# 11 — Faisabilité du Model:Cycles

Ticket : [issues/11-model-cycles.md](../issues/11-model-cycles.md). Recherche du 2026-10-24, firmware Model:Cycles
OS 1.13. Fichiers de travail (hors dépôt) : `C:/dev/elektron-work/scratch-arch/research/mc/` (sections extraites,
désassemblages, `tools/probe.py`, `tools/lin.py`, `tools/xref.py`, `tools/closure.py`, `tools/wavstats.py`).
Toutes les adresses sont celles de la section 3 d'OS 1.13 ; suppositions marquées [INFER].

## Réponse courte

Oui, et c'est la cible la plus simple après le Syntakt : un seul ColdFire, pas de DSP, les six machines dans un bloc
de code compact (43 routines, 3 414 instructions, `0x400a7ab8`-`0x400aba00` plus `0x4008f1f0`) appelé par un
répartiteur unique `0x400a7d4a`. Ce répartiteur tourne **seul, sans l'OS**, dans Unicorn 2.1.4 stock + l'EMAC Python
de SyntHack OS (MIT) : boot réduit à la copie SRAM `0x4000045c`, init des 6 voix, puis un appel par bloc de 32
échantillons à 48 kHz ; il rend des échantillons pour les six machines (mesuré ici, `probe.py`). Coût mesuré en
instructions ColdFire : 5 600 à 8 500 par voix et par bloc. Estimation sur PC : port exact ≈ 2,2-3,4 µs par voix et
par bloc (≈ 0,35-0,5 % d'un cœur par voix), port flottant SIMD ≈ 3-5 fois moins [INFER].

## 1. Firmware et processeur

- Fichier : `model-cycles_OS1.13.syx`, 890 016 octets, SHA-256 `44fe586269631a0ca7da25a3383fc6733c314809505fc3cc52f1e0ed9800640c`
  (le même que digiemu `devices/model-cycles.toml`), dans `%APPDATA%\Elektron Emu\Model Cycles\Firmware\`.
- Extrait avec digiemu `python -m emu.extract` (GPL-2.0+) : section 2 bootstrap (26 602 o, dest. `0x04000000`),
  **section 3 MAIN OS** (1 744 192 o, chargée à `0x40000400`, SHA-256
  `cc99d4f0175d34d1e91d046e6ec85a5e8ab58ab9edbb3c24406acd48cb99ee98` = le profil d'elektron-emu
  `Model-Cycles/source/core/src/profiles.inc.c`), section 4 updater (31 744 o, `0x80000400`), section 5 META
  (« 210525 16:37:28 »). **Pas de section 7** : rien d'autre que le CPU principal.
- Processeur : ColdFire V4e MCF5441x (MCF54418, celui du Digitakt MK1), 128 MB de DDR, 64 KB de SRAM à `0x80000000`,
  EMAC (digiemu `devices/model-cycles.toml`, `MODELS.md` ; elektron-emu émule un `cfv4e`). Sortie SSI0 + eDMA canal 50,
  48 kHz, 24 bits ; codec I2C à 0x1A.
- Pas de DSP : la « ligne ready » à `0x8C000000` d'elektron-emu (`e_mc.inc.c`) est le pannel FlexBus, pas un DSP.

## 2. Comment elektron-emu le fait tourner

`C:\dev\elektron-emu\Model-Cycles\source\core\` (C, commentaires en italien, dérivé du cœur Digitakt II) : l'OS
entier boote et tourne dans Unicorn 2.1.4 patché (14 patches dans `source/patches/`, dont trois corrigent l'EMAC :
MAC avec load, fractionnaire signé, saturation `emac-sat` trouvée sur le Cycles même, `0x400565c6`). Une tranche de
5 ms émulés (200 000 instructions, `E_IPS` = 40 M instr/s supposés) par tour, audio pris dans la file TX de l'SSI
(`engine.c:44-50`). Pas de prise par piste : seule la sortie stéréo. Mesure de l'auteur : 4,70 s de CPU pour 6,1 s
émulées (patches README). **Dépôt sans licence** (aucun fichier LICENSE, README muet ; le README des patches parle de
GPL-2.0+ pour « le dépôt », texte hérité d'un autre dépôt [INFER]) : faits seulement, aucun code repris.

## 3. Le chemin audio d'OS 1.13 (lu dans le désassemblage)

Bloc de 32 échantillons à 48 kHz (`moveq #$20` partout), tâche audio de l'RTOS (appel à `0x40059382`, entre deux
sémaphores `0x40002ae0` / `0x40002912` [INFER : rôle]). Le bloc `0x4005979e` :

| Étape | Adresse | Rôle |
|---|---|---|
| lissage des paramètres | `0x40058474` | rend le pointeur du bloc lissé (Model-TG : cibles `0x800015a2 + 66t + 2k`, mots lissés `0x8000101a + 66t + 2k`) |
| | `0x40056e24`, `0x40091ab2` | préparation (41 instr.) ; gros module 2 234 instr. / 18 routines, LFO et modulations [INFER] |
| **voix** | **`0x400a7d4a`** | les 6 voix, sorties `0x80001858 + 128 t` (32 int32), divisées par 2 à la fin |
| gain par piste | `0x40056d9c` | 6 gains rampés depuis `0x40fe4c90` |
| | `0x4008f1f0` | (27 instr., aussi appelé par CHORD) |
| distorsion par piste | `0x40056f58` | mot 26 du bloc piste (> 0x3fff : quantité au carré), VOL+DIST [INFER] |
| | `0x40056ea8` | (56 instr.) |
| mix | `0x400567ba` | 6 pistes : niveau, pan, envois delay/reverb (boucle sur `d2` jusqu'à 0x18) ; sommateurs EMAC `0x40056610` (8 canaux), `0x40056698` (7), `0x40056716` (6) vers le bus stéréo entrelacé `0x8000b990` |
| delay | `0x4005802e` (préparation), `0x40057488` (192 instr.) | retour au canal 6 `0x80001b58` |
| reverb | `0x400579c4` (404 instr., 113 EMAC) | retour au canal 7 `0x80001bd8` |
| sortie | `0x400565c6` (×4, MACSR 0x80), `0x40056bf6`, clamp `0x40056d00`, TX `0x40056d1c` | |

### Le répartiteur des voix `0x400a7d4a`

Arguments (pile) : sortie, bloc de paramètres lissés, masque des trigs, masque des gates. MACSR `0xA0` pendant les
voix, `0x20` au retour. Pour chaque piste t (0-5) :

- état de voix à `0x42308828 + 0x31c t` (796 octets, en DDR) ; mot 0 = moteur ; `+0x318` pointe un tampon SRAM
  `0x8000bd98 + 0x30 t` ;
- octet machine = `params + 0xe + 18 + 0x42 t` (mot 9 de la piste, Model-TG `0x8000101a + 66 t + 2k`), borné 0-5,
  traduit par la table `0x40118640` (identité 0..5) ; s'il change : `0x400a7ab8` (init de voix, 148 instr.) ;
- `0x400a7d16` (14 instr.), puis **update** `[0x40118628 + 4 m](x, voix, params)` et **render**
  `[0x40118610 + 4 m](sortie, voix)`.

| m | Machine | render | update | Repères |
|---|---|---|---|---|
| 0 | KICK | `0x400aa3c4` | `0x400aa08c` | clic `0x400a9daa` (Model-TG) appelé par ce render |
| 1 | SNARE [INFER] | `0x400ab6e8` | `0x400ab3b0` | mixeur 2 entrées `0x400a9884`, filtres `0x400a9cf6`, `0x400a9d4e` |
| 2 | METAL [INFER] | `0x400aa712` | `0x400aa49c` | mixeur `0x400a9884`, filtre `0x400a9ebe` |
| 3 | PERC [INFER] | `0x400aacc2` | `0x400aa998` | mixeur `0x400a9884` |
| 4 | TONE | `0x400aa930` | `0x400aa7b8` | pas de mixeur `0x400a9884` (Model-TG : partagé par KICK, SNARE, METAL, PERC), donc TONE ou CHORD |
| 5 | CHORD | `0x400ab24c` | `0x400aae88` | oscillateur `0x400a8722` (Model-TG) chargé par ce render |

Sûrs : 0, 4, 5 (par Model-TG et élimination). 1-3 suivent l'ordre du manuel (KICK SNARE METAL PERC TONE CHORD) ; non
vérifiés à l'oreille, les sons du sondage n'étant pas musicaux (§4). Routines partagées entre moteurs : `0x400a8adc`,
`0x400a9120`, `0x400a9252`, `0x400a9302`, `0x400a9430`, `0x400a9504`, `0x400a967a`, `0x400a91ea`, `0x400a9ca8` ;
paramètres : `0x400a7e6c`, `0x400a7f66`, `0x400a8080`.

### Boot utile

`0x4000045c` copie les images SRAM de la fin de la section 3 : `0x4019b590-0x401a2a50` → `0x80000000` (reste à
`0x80008000` mis à zéro), `0x401a2a50-0x401aa140` → `0x80008000` (reste à `0x80010000` à zéro) ; `0x400004b2` met à
zéro `0x4019b590-0x423380b0` (bss DDR). Même schéma que le boot du CPU #2 du Syntakt (`stBoot`).

## 4. Le sondage : le répartiteur sans l'OS (mesuré)

`tools/probe.py` : section 3 à `0x40000400`, `0x4000045c` exécuté, pointeurs `+0x318` posés puis `0x400a7ab8` sur les
6 voix (comme la boucle `0x4005974c`, qui pose le pointeur après l'init : la première init efface 0x30 octets à
l'adresse 0, sans effet sur la carte réelle [INFER]), machine m sur les 6 pistes, trig à tous au bloc 1, gate 200
blocs. Les 223 instructions EMAC des 43 routines (`closure.py`) passent par l'EMAC Python de SyntHack
(`tools/emu/emac.py`, trappes ILLEGAL) ; le reste en Unicorn stock. Comptes par `UC_HOOK_CODE` sur les blocs 2-11 :

| m | instructions / bloc (6 voix) | par voix et par bloc | par échantillon |
|---|---|---|---|
| 0 KICK | 46 299 | 7 717 | 241 |
| 1 | 44 639 | 7 440 | 233 |
| 2 | 40 288 | 6 715 | 210 |
| 3 | 38 176 | 6 363 | 199 |
| 4 TONE | 33 486 | 5 581 | 174 |
| 5 CHORD | 51 290 | 8 548 | 267 |

Le code tourne sans faute ni accès hors mémoire, mais **les paramètres ne sont pas ceux de l'OS** (mots SRAM du boot,
lissage et module `0x40091ab2` non exécutés) : les sorties sont faibles et non musicales (crêtes 0,004 à 0,05 FS,
`wavstats.py`). Les comptes valent donc ordre de grandeur ; des réglages réels peuvent les changer (branches
d'enveloppes, CHORD selon ses notes). Contrôle de vraisemblance [INFER] : 46 k instructions pour 6 KICK sur un
MCF54418 à 250 MHz (166 667 cycles par bloc) à IPC ≈ 0,5 donnent ≈ 55 % du bloc, soit ≈ 9 % par voix, cohérent avec
les 9-10,4 % « of a block » que Model-TG (`docs/INTERNALS.md`) donne au KICK.

## 5. Harnais d'oracle

Faisable comme pour le Syntakt, et plus simple (un CPU, pas de mailbox). Ce qui existe déjà : la coupe (voix seule,
sortie par piste avant le mix, à `0x80001858 + 128 t`), le boot réduit, la liste des sites EMAC, l'appel du
répartiteur. Ce qui manque pour des vecteurs musicaux :

1. **Un état réel à l'entrée de `0x400a7d4a`** : faire tourner l'OS entier jusqu'à un Kit (digiemu, GPL-2.0+, joue les
   sons d'usine du Cycles dès le premier boot, `MODELS.md`), y poser un point d'arrêt, enregistrer bloc de paramètres,
   masques, 6 états de voix (6 × 796 o), SRAM (64 KB) et sorties — l'équivalent de `tools/oracle.py` / `vectors.py`.
   Alternative : décoder les mots de paramètres (Model-TG donne la disposition) et faire jouer le harnais seul.
2. L'EMAC exacte : l'EMAC Python de SyntHack sert d'oracle lent ; elektron-emu montre que le Cycles touche la
   saturation OMC (`0x400565c6`) — étage de sortie, pas les voix (MACSR 0xA0 dans les voix : fractionnaire, sans OMC).
3. Côté contrôle (lissage `0x40058474`, `0x40056e24`, `0x40091ab2`), gain, distorsion, mix, delay, reverb : à
   enregistrer de même si on les veut.

## 6. Effort et coût CPU

Règle amendée (2026-10-24) : performance avant fidélité. Les deux voies :

| | Port exact (comme `stEngine`, entiers + EMAC émulée en C++) | Port flottant rapide (SIMD sur les voix, contrôle au bloc) |
|---|---|---|
| Code à porter | 43 routines / 3 414 instructions pour les 6 machines ; + contrôle (~2 300 instr.), étages de piste (~300), mix/delay/reverb (~800) | les mêmes, réécrits |
| Vérification | 0 mot différent sur vecteurs (outillage Syntakt réutilisable : `vectors.py`, `coverage_insn.py` à adapter à la section 3) | tolérance mesurée contre l'oracle (et le port exact comme oracle rapide) |
| Effort [INFER] | harnais + vecteurs 1 session ; 6 moteurs 2-3 sessions (le Syntakt : 12 moteurs) ; contrôle 1-2 ; étages et effets 1 | + 1-2 sessions pour la réécriture et les tolérances, en partant du port exact |
| CPU voix [INFER] | ≈ 2,2-3,4 µs par voix et par bloc (5,6-8,5 k instr. ColdFire, à ~0,4 ns l'instruction : c'est le rapport que donne le Syntakt, 2,8 µs par voix, `01-budget-cpu.md` §2) ; 6 voix ≈ 2-3 % ; 16 voix ≈ 5,5-8 % | ≈ 3-5 fois moins : 16 voix ≈ 1,5-2,5 % |
| Fixe | delay + reverb + mix du Cycles non mesurés ; comparable au CPU #1 du Syntakt (2,5-4,3 %) [INFER], ou remplacés par la chaîne commune de l'instrument | idem |

Avantages pour l'instrument : même cadence que le Syntakt (32 échantillons, 48 kHz), donc pas de rééchantillonnage pour
cette famille ; ColdFire + EMAC comme le Syntakt, donc `stEmac.h` et l'outillage d'oracle servent tels quels ou presque.

## Inconnues

- Ordre exact SNARE / METAL / PERC des indices 1-3 (à confirmer à l'oreille avec un état réel).
- Disposition des 32 mots de paramètres par piste et ce que fait `0x40091ab2` ; plage de chaque mot.
- Coût réel avec des réglages musicaux ; coût des étages hors voix (non comptés).
- Coût natif réel : aucune routine portée ni mesurée en C++ ; les µs ci-dessus sont des extrapolations.
- Les 512 presets d'usine ne sont pas dans l'OS (elektron-emu README) ; digiemu dit que les sons d'usine des motifs
  jouent dès le premier boot (des fichiers `factory/*.mc-snd` sont nommés dans la section 3, `0x1309b7`) : à
  vérifier, utile pour des vecteurs réalistes.

## Sources

- `C:\dev\elektron-emu\Model-Cycles\` (README, `source/core/src/engine.c`, `e_mc.inc.c`, `profiles.inc.c`,
  `source/patches/README.md`), commit `03961c6`, sans licence.
- digiemu `C:\dev\digiemu\MODELS.md`, `devices/model-cycles.toml`, `emu/extract.py` (GPL-2.0+).
- Model-TG (MIT), https://github.com/TinyGregAudio/Model-TG, `docs/INTERNALS.md` (lu en ligne le 2026-10-24).
- SyntHack OS `C:\dev\Synthack-OS\tools\emu\emac.py` (MIT).
- Désassemblage de la section 3 (cfdis.py) : `lin_audio.txt` (`0x40056524-0x40056e00`), `lin_block.txt`,
  `dis_voices.txt`, `reach_all.txt` dans le dossier de travail.
