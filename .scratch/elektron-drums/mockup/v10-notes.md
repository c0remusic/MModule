# Maquette v10 — toutes valeurs dans anneau

`v10.html` (1600x1000) part de v9 ; v1 à v9 intacts. Captures : `v10-shots/`.

## Un seul dessin de knob, deux tailles

Chaque longueur = fraction fixe du diamètre D. Knob secondaire et primaire = même dessin à l'échelle.

| Élément | Ratio de D | D = 52 (secondaire) | D = 64 (primaire) |
|---|---|---|---|
| Texte de valeur (Plex Sans Condensed 500, chiffres tabulaires) | 11/52 = 0,2115 | 11 px | 13,54 px |
| Libellé (Saira) | 12/52 = 0,2308 | 12 px | 14,77 px |
| Rayon de l'anneau (axe) | 0,425 | 22,1 | 27,2 |
| Épaisseur de l'anneau | 0,09 | 4,7 | 5,8 |
| Arc de modulation : rayon / épaisseur | 0,3425 / 0,035 | 17,8 / 1,8 | 21,9 / 2,2 |
| Pointeur (point) | 0,05 | 2,6 | 3,2 |
| Trait de valeur jouée : de / à / épaisseur | 0,37 / 0,49 / 0,035 | | |
| Disque intérieur | 0,38 | | |

Ratio du texte choisi pour que valeur la plus longue tienne à 11 px dans knob de 52 : boîte de la valeur à 2 px au moins
du bord intérieur de l'arc de modulation (élément le plus intérieur de l'anneau, plus strict que l'anneau lui-même).
Aucune valeur rétrécie seule, aucune sous le knob ; recherche de taille et repli « sous le knob » du v9 retirés.

## Knobs agrandis

- 32 et 36 px → 52 (secondaire) : enveloppes du filtre et de l'ampli, Sample layer, Band EQ, LFO, FX, Master, pans du
  mixer, SEQ, navigateur.
- 44 et 48 px → 64 (primaire) : machine, drive, Base/Width, Frequency/Resonance, Level de l'ampli, Speed et Depth du LFO,
  knobs principaux de FX et Master.
- Cellules : machine en quarts de rangée, grilles FX/Master en pleine colonne (libellés à 14,77 px sans troncature) ;
  rangées de knobs centrées verticalement (52 et 64 côte à côte).

## Formats raccourcis

- Mix M↔S : `40:87` → `40` (part de la machine).
- Multiplicateur LFO : `×1024` → `1024`.
- Divisions gardées (`1/16T`, `1/128`), Hz et ms déjà sans unité (`1.0k` gardé).

## Lock et modulation, sans anneau ni point

- Modulation (LFO ou vélocité) : arc plein fin, couleur du modulateur, juste dans l'arc de valeur, de valeur à valeur +
  profondeur (profondeur négative : autre sens).
- Valeur jouée (après modulation) : petit trait en travers de l'anneau.
- Lock sur pas sélectionnés : valeur en teal, arc à valeur lockée ; plus d'anneau pointillé, plus de point après libellé.
  Valeur de base revient à la désélection.

## Vérification

Chrome headless (CDP) 1600x1000, mêmes 19 états que v9 : **0 erreur JS, 0 débordement, 0 texte sous 11 px, 0 contraste
sous 4,5:1, 0 cible sous 24 px, 0 rangée désalignée, 0 coin > 2 px, 0 barre colorée, 0 valeur sous un knob** (v9 : 27
page piste, 28 FX). Script : chaque knob visible, chaque valeur de son paramètre (≈ 5 000 par état piste), largeur
rendue par SVG même (chiffres tabulaires). Pire marge : `1/128` sur 64 px, 18,2 px pour 18,8 permis ; `1024` sur 52 px,
12,5 pour 14,9.
