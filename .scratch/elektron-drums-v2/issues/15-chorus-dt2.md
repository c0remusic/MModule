# Chorus du DT2

Type: AFK puis HITL (écoute)
Status: in-progress (port fait, écoute à venir)
Assignee:
Blocked by: 01

## Quoi

Cinquième type de l'emplacement Chorus, après Bucket Brigade. Réglages DPTH, SPD, HPF, WDTH, VOL, sans synchro au
tempo. Ses envois DEL et REV vont aux emplacements Delay et Reverb, quel que soit leur type, à 0 par défaut.

D'abord : relever à l'oracle (`elektron-work\tools\dt2\dt2_send_oracle.py`) où va le retour du chorus et les lois de
ses réglages.

## Critère de fin

- Jugé contre les vecteurs chorus de `vectors-dt2\sends` par les tests Q ; l'oreille pour le reste.
- Même son à 44,1 et à 48 kHz (tests Q de `research/19-qualite-audio.md`).
- Aucun set existant ne change de son.
- Coût mesuré sur machine calme et rapporté face à son guide (spécification v2, §3) ; le pire kit tient sous 15 %.
- Gate vert, nouveau test inscrit dans sa liste.

## Références

Spécification v2, §5.4 (`../elektron-drums/spec-v2.md`) ; ses tickets de décision dans `../lockstep-post-v1/issues/`.

## Relevés à l'oracle (2026-11-24)

Tout hors dépôt : `elektron-work\dt2-chorus\` (modèle `chorus_model.py`, état d'init `cho_init.json`, données du
firmware) et 13 cas ajoutés à `vectors-dt2\sends` (README à jour).

- **Retour :** le chorus va bien au bus que lit le master, × 2 (VOL/128)², sans retard, le gain glissant sur le bloc
  (échantillon k à ancien + k (neuf − ancien)/32). Les anciens cas pilote le manquaient : l'oracle laissait à 0 le mot
  de route du chorus (`S2`, que le firmware pose à 1 en `0x1c2d63`) ; corrigé, et les `drv_imp_*` d'avant n'ont pas le
  chorus dans `return` quand CHO > 0.
- **Envois :** vers le delay × (CHO DEL/128)², vers la reverb × (CHO REV/128)² (le bus de la reverb du DT2 prend en plus
  × 0,5, entrée des pistes comprise), depuis la sortie brute, glissant sur le bloc. Le carré est lu dans le code,
  mesuré à 64 seulement.
- **Algorithme** (routine `0xb82680`, blocs de 32 à 48 kHz, float32) : passe-haut 1 pôle (HPF, 40 + 960 (k/127)² Hz),
  trois biquads passe-bas fixes (7 kHz, Q 0,8), puis par échantillon une ligne de 512 par canal lue sur un triangle
  (SPD : 2,82 × SPD/127 + 0,28 Hz) passé dans un 1 pôle à 72 Hz, le canal droit WDTH/2 de tour plus loin, la
  profondeur (DPTH) lissée par bloc et par échantillon ; 32 retards de 5 403 à 7 674 µs ; pas de feedback ; sortie
  humide seule (mot de mélange 1). Réglages : mot de trame (bouton << 8) / 32 512, exact pour 0-127.
- Le modèle tient les 15 vecteurs `cho_*` au bit près sur les tables du firmware, vers 125 dB sur les formules.

## Port (2026-11-24)

- `Dt2Chorus` (`source/elektron/edDrums/edDrumsDt2Chorus.*`) : le modèle en C++, float32 dans l'ordre du DSP
  (`/fp:precise`), à toute fréquence. Ses tables viennent des formules ; à une autre fréquence que 48 kHz, chaque K du
  bilinéaire est celui qui donne la même réponse (tan de la fréquence que 48 kHz déformait), exact à 48 kHz. Les 32
  retards sont des constantes mesurées dans le dépôt, choix de l'utilisateur du 2026-11-24 (pas de forme close).
- Sixième type de l'emplacement Chorus, après Multi Voice (le cinquième existait déjà) : **Wide Chorus**, nom choisi
  par l'utilisateur (aucune machine d'origine dans ce que l'hôte montre). Sept réglages ajoutés après `delay_vol`
  (`chorus_dt2_depth`, `_speed`, `_hpf`, `_width`, `_vol`, `_del`, `_rev`, « Wide Depth »… « Wide To Reverb »), aux
  boutons de l'oracle : 127, 32, 0, 127, 100, 0, 0. Pas de synchro.
- Dans `Sends`, il tourne avant le delay et la reverb, que ses DEL et REV nourrissent dans le même bloc ; les autres
  types ne changent pas de place. La rampe du VOL du delay Syntakt (ticket 14) prend la forme mesurée du DT2 (k/32,
  la cible au bloc suivant).
- Q15 (44,1 contre 48 kHz) : enveloppe 0,073 dB (borne 0,2) ; tiers d'octave à 0,22 dB (borne 0,5) tant que le chorus
  est à moins de 15 dB de son maximum, puis bornes mesurées prises par l'utilisateur : 0,93 dB à 10 kHz (borne 1,0,
  jusqu'à −30 dB), 2,4 dB à 12,7 kHz (borne 2,5). Le bilinéaire sans pré-distorsion du DT2 et sa lecture linéaire ne
  se transposent pas exactement.
- Contre l'OS : 125,4 dB au pire sur les 15 cas (borne 115), depuis l'état où l'oracle le démarre
  (`vectors-dt2\sends\cho_port.txt`, fait par `dt2-chorus\port_index.py`).
- Coût : 0,15 à 0,17 % d'un cœur à 44,1 kHz.
- Tests : `edDrumsDt2ChorusTest` (nouveau, au gate), `edDrumsEngineTest` (dt2ChorusSends), `edDrumsProcessorTest`,
  `edDrumsEditorTest` (les sept ont leur contrôle sur Wide Chorus), `edDrumsVst3HostTest` (leurs places).

Points ouverts :
- Écoute de contrôle (HITL).
- Les envois CHO DEL et CHO REV mesurés à 64 seulement ; le mot de mélange à 1 seulement.
- Le pilote applique au bus de la reverb un × 0,5 que Lockstep n'a pas (sa reverb a sa propre loi d'envoi).
