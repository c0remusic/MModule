# Oracle dynamique de la voix sampler du Digitakt II

Type: task
Status: resolved
Assignee: agent (session du 2026-11-16)
Blocked by: 

## Question

La cartographie du DT2 (`../elektron-drums/issues/23-cartographie-dt2.md`) a laissé l'oracle dynamique pas prêt :
sharcvm ne tourne que pour le Digitone II, coupled-run sans entrées. Le harnais du master (v1, ticket 03 de
`elektron-drums-v1`) appelle déjà des routines du DT2. Rendre appelables, hors temps réel et sous garde de durée, la
voix `0x1c4ecf` et STRETCH `0x1c5576` avec un sample et des réglages donnés, et enregistrer des vecteurs dans
`elektron-work\vectors-dt2` ; mesurer leur coût en instructions par voix et par bloc. Résultat attendu : l'oracle
existe (où, comment l'appeler), les premiers vecteurs, le coût mesuré.

## Answer

Fait le 2026-11-16 : [research/03-oracle-voix-dt2.md](../research/03-oracle-voix-dt2.md) (image DSP de l'OS 1.16,
identique en 1.17, sur le cœur SHARC+ de digikit, depuis l'instantané d'init du harnais du master ; tout hors dépôt).

- **L'oracle existe :** `C:\dev\elektron\elektron-work\tools\dt2\dt2_voice_oracle.py`. Il appelle bloc après bloc la
  chaîne par voix telle que le firmware l'appelle, une voix jouant un sample donné (WAV 16 bits ou synthétique, mono,
  48 kHz) avec TUNE, note, STRT, LEN, boucle, sens, LEV, vélocité et sa courbe, machine, BARS, BPM. Voix `0x1c4ecf`
  (ONESHOT) ou STRETCH `0x1c5576`. Mode rapide (voix seule après le premier bloc), identique au bit près, 0,2 s par
  bloc. Garde de durée intégrée (`guard.ps1`). Régénération : `regen_voice.ps1`, environ 15 min.
- **Vecteurs :** 21 cas dans `elektron-work\vectors-dt2` (16 ONESHOT : unisson, ±12, impulsion, bruit, STRT/LEN, envers,
  boucle, quatre LEV, vélocité ; 5 STRETCH), plus `laws.json` et un `README.md` qui donne le format.
- **Coût mesuré** (instructions SHARC par voix et par bloc de 32) : ONESHOT 3 613 à 3 620 en lecture (jusqu'à 3 977 à
  l'envers), dont 916 pour le décimateur ; STRETCH 1 751 à 1 865.
- **Décimateur :** récursif, plat à ±0,01 dB jusqu'à 20 kHz. La voix entière à l'unisson suit celle du Rytm à 0,35 dB
  près jusqu'à 23 kHz : pas de variante de décimateur pour la couche sample (point laissé ouvert par « Où le Rytm rend
  sa couche sample »).
- **Lois relevées :** LEV = 2 × (LEV/128)² × vélocité, rampé sur le bloc (le Rytm : (LEV/127)²) ; hauteur
  2^(((TUNE − 64) + (note − 60))/12), bornée à ±64 demi-tons ; STRETCH rendu directement à 48 kHz, hauteur
  indépendante du rapport de temps, grains tous les 2 048 échantillons avec un fondu de 128.
- **Reste ouvert :** l'ordre des valeurs du réglage PLAY (l'oracle prend les deux drapeaux sens et boucle) ; d'où vient
  le facteur de niveau × 0,4047 de STRETCH ; STRETCH au-delà d'un rapport 1 et son premier bloc ; WERP, REPITCH, GRID,
  SLICE, la stéréo, la BR, les filtres, le lissage et les LFO du ColdFire, non exercés.
