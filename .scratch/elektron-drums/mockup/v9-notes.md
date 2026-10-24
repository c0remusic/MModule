# Maquette v9 — choix du v8 appliqués

`v9.html` (1600x1000) part de v8 ; v1 à v8 intacts. Captures : `v9-shots/`.

## Changements

| # | Choix | Fait |
|---|---|---|
| 1 | Police | Saira Semi Condensed pour textes et libellés (marque « elektron » comprise, 800 droit : Saira n'a pas d'italique). IBM Plex Sans Condensed en chiffres tabulaires pour valeurs qui changent : knobs, valeurs sous knob, numéros et conditions de pas, faders, CPU, BPM et position, valeurs de la liste SEQ et des lignes de lock, n° de slot de kit, colonne # du tableau de kit, durées et compteurs du navigateur, matrice des envois. Deux faces chargées, aucune autre rendue (vérifié par script) |
| 2 | Accent | Variante (c) seule : hauteur du remplissage = vélocité, pas accentué plein. Variantes (a) et (b), paramètres `?acc`, `?font`, `?excerpt`, `?stepsonly` retirés. Slide : encoche sur bord droit, inchangée |
| 3 | Couleur de trig | Teal de l'accent, assombri à `#2d6670` pour garder encre lisible (5,3:1 ; 4,5:1 sous tête de lecture). Mini-grilles SEQ idem |
| 4 | Sélection | Fond plus clair (`--ctlon`), texte encre. Rien d'autre |

## Marqueurs retirés (point 4)

- Point coloré devant chaque titre de box (`.hd h2:before`), toutes boxes.
- Barre 2 px sous élément sélectionné : segments (`.seg` : PRE/POST, bascule de bande, SYNC/HZ, AHD/ADSR, ratio, pingpong, phaser/flanger, oversampling…), familles (`.fam`), modes de trig LFO (`.vseg`), bascules (`.tog`, dont ENV RESET), grille machines (`.mb`, aussi dans menu machine), patterns (`.pat`), liste de kits (`.slots`, `.kitlist`), types de filtre (`.ftypes`, y compris MD et DT2), formes LFO (`.shapes`), boutons icône de l'aperçu (`.ibtn`), menu de choke (`.cpop`).
- Barre 2 px à gauche : familles du menu machine (`.mpop .fl`), arbre du navigateur (`.tn2`), ligne qui joue dans le navigateur (`.brow.play` : nom en teal à la place), liste des paramètres SEQ (`.pr.on`, bord jaune).
- Bord gauche orange des slots de drive (`.dslot`, `.slot`, `.slotc`).
- Anneau teal des puces de tags (`.chip.on`) ; anneau jaune de nouvelle lane de lock (`.lane.new` : fond clair).
- Barre teal 3 px sous pas en lecture (`.st.play`) : pas joué un ton plus clair.
- Indicateur de page SEQ : barre teal → barre encre.
- Onglets du haut (`.mtab`) et sous-onglets de box (`.hd .tabs`) : sélection passée d'un fond plus sombre à un fond plus clair.

Gardés, pas des marqueurs de sélection : contour blanc du pas sélectionné, repère gris des temps (1, 5, 9, 13), barre de micro-timing (encre maintenant), LED des onglets de piste, point de lock, point « modifié » du slot de kit, contour de dépôt (glisser), clignotement d'ASSIGN.

## Vérification

Chrome headless (CDP) à 1600x1000, 19 états (ceux du v8 moins les deux variantes d'accent) : **0 erreur JS, 0 débordement, 0 texte sous 11 px, 0 texte sous 4,5:1, 0 cible sous 24 px, 0 rangée désalignée, 0 coin > 2 px, 0 valeur hors anneau, 0 barre ou bord coloré** (nouveau script : ombre interne décalée ou bord ≥ 2 px de couleur saturée, pseudo-élément coloré dans un en-tête ; passé sur le v8, il en trouve 2 à 10 par état, dans chacun des 19). Corrigés en route : Transport SEQ (Saira plus haute, +3 px), menus Type du navigateur (texte sous la flèche).

## Questions ouvertes

1. **Valeurs sous le knob** : chiffres Plex plus larges que Barlow ; knobs dont la valeur passe dessous 18 → 27 sur page piste, 7 → 28 sur FX (enveloppes du filtre et de l'ampli, knobs de 36 px). Passer ces knobs à 40 px remettrait les valeurs dedans en 11 px.
2. **Vélocité par pas** : visible par hauteur, toujours sans lieu d'édition (mode ACC ou ligne VEL ?).
3. **Encoche de slide** : reste violette, seule couleur hors teal sur la grille ; passer en encre ?
4. Questions 1 à 3 du v8 (ordre Sample layer, bas vide de Drive, anneaux de lock) toujours ouvertes.
