# SLICE du lecteur DT2 et son éditeur

Type: AFK puis HITL (éditeur)
Status: done (essai dans Live à venir)
Assignee: Claude (session 2026-11-26)
Blocked by: 04

## Quoi

MODE du lecteur DT2 gagne SLICE, avec l'éditeur de slices. Vecteurs relevés à l'oracle d'abord, puis port, puis
l'éditeur.

## Critère de fin

- Vecteurs SLICE enregistrés hors dépôt ; le port les tient par les tests Q.
- Test d'éditeur : slices posées, déplacées, retirées, gardées dans l'état.
- Même son à 44,1 et à 48 kHz (tests Q de `research/19-qualite-audio.md`).
- Aucun set existant ne change de son.
- Coût mesuré sur machine calme et rapporté face à son guide (spécification v2, §3) ; le pire kit tient sous 15 %.
- Gate vert, nouveau test inscrit dans sa liste.

## Relevés et décisions (2026-11-26)

Relevé à l'oracle (`dt2-voice\slice_table.py`, `probe_slice.py`, `probe_slice2.py`, hors dépôt, rien du firmware
recopié) :

- La table des cas de `FUN_1c642a` (`0x8055c840`) a six entrées, sélecteurs 0 à 5 ; SLICE est le **sélecteur 5** du
  DSP (cas `0x1c6992`, setter `0x1c4bf9`), pas 6 comme le lit digikit : au-delà de 5, aucun cas ne tourne.
- Le DSP ne voit pas la table de slices. Le ColdFire lui donne une portion en échantillons dans le record de paramètres
  de la voix : début `+0x30`, fin `+0x34`, point de boucle `+0x38` (lu avec LOOP). Le setter les borne comme ONESHOT
  (début à longueur − 141). STRT et LEN ne sont pas lus ; TUNE, la note et PLAY le sont. Le rendu est celui des autres
  modes (`0x1c4ecf`).
- Vecteurs `sl_*` (11 cas) dans `vectors-dt2`, par `tools\dt2\regen_voice_12.ps1` : portions de bord à bord, au
  milieu, aux bornes non rondes, à la fin bornée, transposées, à l'envers, en boucle et en boucle à l'envers, bruit.

Décidé avec l'utilisateur :

- **Points de slice par piste**, dans le Kit et l'état, comme le DT2 les garde avec son preset. Un sample locké sur
  un pas joue les slices de la piste, bornées à sa longueur.
- **Slices contiguës** : une suite de marques (63 au plus, 64 slices), chaque slice finit où la suivante commence ;
  LOOP boucle la slice entière (point de boucle = son début).
- **L'éditeur dans l'image VOICE** de la page Sound : clic pose une marque, glisser la déplace, double-clic la retire ;
  un bouton GRID n découpe en n parts égales ; la slice jouée se surligne. Machine 5 SLICE et 6 LEN comme GRID.

## Avancement (2026-11-26)

- `Dt2Player` : MODE SLICE joue la portion que les slices de la piste donnent (`Slices`, marques en échantillons
  dans `Dt2Samples::Slot::slices`), Machine 5 SLICE la slice jouée, 6 LENGTH combien la suivent, comme GRID ; le
  début borné comme ONESHOT ; LOOP boucle la portion entière. SLICE ne lit pas la note (vecteur `note48` : sur le
  DT2, une note y choisit une slice, ce que fait le ColdFire). REV LOOP reboucle `ReverseLoopLag` sous son début,
  comme WERP (meilleur de 0 à 12) ; le bloc du rebouclage diffère comme celui de WERP, non identifié.
- Face aux 11 vecteurs : 51 à 60 dB sur les Hits, 31 sur le bruit, 28,5 en boucle à l'envers (borne 25, comme WERP
  REV.L à 20) ; enveloppe à 0,1 dB, hauteur à 0 cent. Même rendu qu'ONESHOT : coût par voix inchangé.
- `Engine::setSlices`, `Processor::setSlices` (triées, sans 0 ni doublon, 63 au plus) par un courrier vers le fil
  audio ; gardées dans l'état (`tNN_slices`), les Kits (Save s'allume quand elles diffèrent), les Kits de pattern
  chargés sur le fil audio, les sons de piste, l'aperçu du Hit et l'écoute d'un son.
- Migration : un set d'avant (état < 8, Kit < 6) sur CLEAN avec MODE dans la zone de SLICE, qui jouait ONESHOT,
  passe à ONESHOT, ses Locks de MODE aussi (`takeSliceMode`) : aucun set ne change de son.
- Éditeur dans l'image VOICE : clic pose une marque, glisser la déplace entre ses voisines, double-clic la retire,
  GRID (en bas à droite) découpe en 2 à 64 parts égales ; toutes les marques tracées, la slice jouée surlignée.
- Tests : `edDrumsDt2PlayerTest` (vecteurs, portions), `edDrumsProcessorTest` (`slices` : jeu, tri, état, Kit et Save,
  migration), `edDrumsEditorTest` (poser, déplacer, retirer, GRID 8, état ; `sound-slice.png`).
- Laissé : SLICE/LENGTH lus par l'hôte comptent une seule slice (le texte de l'hôte ne voit pas les slices de la
  piste) ; un glisser marque l'état modifié à chaque mouvement.

## Références

Spécification v2, §5.1 (`../elektron-drums/spec-v2.md`) ; ses tickets de décision dans `../lockstep-post-v1/issues/`.
