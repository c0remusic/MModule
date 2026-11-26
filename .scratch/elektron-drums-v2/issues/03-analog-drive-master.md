# ANALOG ≈ au master

Type: AFK puis HITL (écoute)
Status: done (essai dans Live à venir)
Assignee: Claude (session 2026-11-26)
Blocked by: 02

## Quoi

L'étage de saturation du master gagne l'option ANALOG ≈, à côté de l'overdrive du DT2 (défaut) : le modèle du
ticket 02 en stéréo, un DRIVE pour les deux canaux. La page du master ne montre que les réglages du modèle choisi.

## Critère de fin

- Les tests mesurés du ticket 02, sur les deux canaux ; les deux canaux identiques pour une entrée mono.
- Coût visé : 0,2 % d'un cœur.
- Même son à 44,1 et à 48 kHz (tests Q de `research/19-qualite-audio.md`).
- Aucun set existant ne change de son.
- Coût mesuré sur machine calme et rapporté face à son guide (spécification v2, §3) ; le pire kit tient sous 15 %.
- Gate vert, nouveau test inscrit dans sa liste.

## Avancement (2026-11-26)

- `Master::setAnalog` : l'étage de saturation du master passe de l'overdrive du DT2 (défaut) à `AnalogDrive`, un
  canal par voie (0 gauche, 1 droite), sous le même DRIVE. ANALOG ≈ remplace l'étage, sorti seul (pas de part
  sèche) ; à DRIVE 0 il colore encore, comme le DRIVE de l'Analog FX à 0. Le changement prend effet au bloc suivant ;
  un étage qui s'allume part du silence.
- `AnalogDrive` gagne `sample(lane, x)`, un échantillon à la fois pour le master, sur le même noyau que `process`
  (les échantillons de la piste inchangés, mêmes mesures qu'au ticket 02), et `clear(lane, seed)` : les deux canaux
  du master prennent la même graine de souffle, donc une entrée mono sort identique à gauche et à droite.
- Pas de suréchantillonnage au master (il ne l'est jamais, l'overdrive du DT2 non plus).
- Paramètre d'hôte `master_drive_model` (« Drive Model », Overdrive | Analog ≈), ajouté en fin de liste (ID gelé,
  1868 paramètres), dans les Kits avec les autres réglages du master. DRIVE s'affiche en dB du gain d'ANALOG ≈ quand
  il est choisi. Page Master : OVERDRIVE | ANALOG ≈ sur la ligne de titre MASTER.

Mesuré (`edDrumsAnalogDriveTest`, partie master) : les mesures du ticket 02 sur le master (compresseur sec, volume
plein), aux mêmes valeurs ; les deux canaux égaux échantillon par échantillon sous une entrée mono ; l'overdrive du DT2
identique après un passage par ANALOG ≈ ; le master coûte 0,160 % d'un cœur sur l'overdrive, 0,234 % sur ANALOG ≈
(l'étage : 0,07 %, guide 0,2 %). `edDrumsEditorTest` : `master-analog.png`, DRIVE 127 lu « +30.0 dB ».

Écoute : dernière ligne de `C:\dev\elektron\elektron-work\listen-analog\page.html`, la boucle à 44,1 kHz sans
suréchantillonnage, ANALOG ≈ 0 à 127. Écoutée par l'utilisateur le 2026-11-26 : lui va telle quelle.

## Références

Spécification v2, §4 (`../elektron-drums/spec-v2.md`) ; ses tickets de décision dans `../lockstep-post-v1/issues/`.
