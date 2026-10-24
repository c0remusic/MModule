# Maquette v12 — couleur de section, façon Overbridge

`v12.html` + `v12.css` + `v12.js` (1600x1000), partis de v11 ; v1 à v11 intacts. Tailles : `v12.html` 40 344,
`v12.css` 57 088, `v12.js` 79 955 octets. Captures : `v12-shots/` (28, dont `track.png`).

## Retour sur v11

Manque de couleur. Dans Overbridge, couleurs bâtissent hiérarchie : chaque section a sa couleur saturée, sur ses
knobs, son écran, ses contrôles sélectionnés ; œil trouve section par couleur. Passe taste du v11 avait tout ramené à
une clarté (L 0,78, C 0,075) : direction abandonnée.

## Teintes mesurées dans les références

Mesure : pixels saturés (écart max-min > 45) des anneaux, remplissages et onglets dans `refs/ob-rytm-1.webp`,
`ob-rytm-2.png`, `ob-rytm-envelopes.png`, `ob-digitakt.png` (script PIL, 60e-90e centile de luminosité). Anneaux des
captures compressés et lissés : teinte reprise telle quelle, saturation reprise des parties pleines (remplissage LFO,
onglet de filtre sélectionné, forme d'onde du sample), pas de la moyenne de l'anneau.

| Section | Mesuré (réf.) | Teinte | Anneau v12 | Piste `-d` | Puits `-w` | Sélection `-s` | Texte `-t` |
|---|---|---|---|---|---|---|---|
| Machine / synth | anneau Synth `#438aa5`, Rytm 1 | 197° | `#3a9cc8` | `#1e3744` | `#21343e` | `#324c5a` | `#86c2dd` |
| Sample | onde `#cee391`, barre `#77a15c`, anneau `#b2d18f` | 75-88° | `#8cc152` | `#324027` | `#2f3b29` | `#46553d` | `#aad17f` |
| Drive | (pas de drive Rytm) anneau filtre Digitakt `#cc9471`, orange poussé | 30° | `#e8903c` | `#483422` | `#403225` | `#5c4938` | `#f0b47c` |
| Band | (pas de band Rytm) bleu entre machine et ampli | 218° | `#5b8de0` | `#26334a` | `#273143` | `#3a4960` | `#9bbaec` |
| Filtre | onglet sélectionné `#f59e86`, anneau `#9f6e63`, remplissage `#855147` | 10-13° | `#e57b64` | `#472f2c` | `#3f2e2c` | `#5b4442` | `#efab9d` |
| Ampli, enveloppes | anneaux `#7f6ea2`/`#9585b8`, enveloppe `#8d88cb`, puits `#34324a` | 242-260° | `#9282dc` | `#333149` | `#302f42` | `#48465f` | `#bcb2e9` |
| LFO 1 | remplissage `#dcb869`, forme sélectionnée `#f5be64`, anneau `#a79061` | 40° | `#e2b44a` | `#463d26` | `#3f3828` | `#5b523c` | `#e8c472` |
| LFO 2 | (absent) rose des filtres Rytm 2 `#e29ac7` | 313° | `#d77fc4` | `#443043` | `#3d2f3e` | `#584559` | `#e6aed9` |
| Vélocité | (absent) jaune-vert, distinct du LFO 1 | 68° | `#b4c450` | `#3b4127` | `#373b29` | `#50563d` | `#c4d176` |
| Envois, mixer | gris-blanc des faders | — | `#c3c8ce` | `#3f4145` | `#393c3f` | `#53575b` | `#cbd0d5` |
| Accent global | onglet Kit Editor `#b3eadb`, onglet BD 1 `#95dbc4` | 164° | `#4fd1ad` | — | — | `#375953` | `#73dbbe` |

Dérivés (`scratchpad/v12/pal.py`) : `-d` = anneau 24 % sur piste sombre `#15171a` ; `-w` = 18 % sur puits `#1b1d20` ;
`-s` = 24 % sur panneau `#303337` ; `-t` = anneau éclairci (HLS, teinte gardée) jusqu'à 4,6:1 sur `-s`, `-w` et panneau.
Anneaux eux-mêmes : 3,8 à 7,5:1 sur panneau, 5,1 à 10,1:1 sur capuchon du knob (graphique, pas texte). Aucun texte
dans couleur d'anneau : tout texte coloré passe par `-t` (remplacé partout dans CSS et HTML, `color:var(--x)` →
`color:var(--x-t)`).

Accent global passe de teal `#3fb3c4` (v11) à menthe `#4fd1ad`, pris sur onglet Kit Editor du Rytm : 33° de la
machine cyan, plus confondu avec elle. Trig `#2a7060` (encre 4,7:1 dessus).

## Où chaque couleur va

- **Knobs** : arc de valeur épais et plein dans couleur de section (`KR.ringW` 0,09 → 0,12 du diamètre, rayon 0,425 →
  0,435 : épaississement vers l'extérieur, voie de modulation et texte de valeur inchangés, valeurs toujours dans
  l'anneau) ; piste non remplie = `-d` de même couleur. Un seul dessin, proportions fixes, comme v10-v11.
- **Écrans** : puits teinté `-w` de la box ; remplissages à 42 % d'opacité (14-18 % en v11), trait plein dans couleur
  de section : filtre saumon, enveloppe d'ampli violette, LFO jaune, sample vert, scope machine cyan, band bleu.
  Bande d'écrans alignée par rangée du v11 gardée (110 px, même haut).
- **Contrôles sélectionnés dans une box** (segments, types de filtre, formes LFO, onglets d'en-tête, Pre / Post,
  AHD / ADSR, liste Parameters du Seq...) : fond `-s`, texte `-t`. Pas de liseré. Exceptions voulues : onglet LFO 2 dans
  couleur LFO 2 ; Assign armé (aplat jaune, mode) et Mute (jaune) gardés.
- **En-têtes de box** : gris neutre `#363a3e`, titre blanc. Couleur dans contenu seulement.
- **Accent menthe** : état global seul : onglet de piste affiché, onglet Kit editor, LED de piste, trigs, valeurs
  lockées, pagination des pas.

## Pas : trig par lock, lock sans trig, mode Vel, glisser

- **Lock sur pas vide** : pas vide sélectionné + knob tourné → pas devient trig portant ce lock (point de lock en haut à
  droite), liste de locks à droite des pas mise à jour pendant geste. **Alt** tenu en tournant → lock sans trig
  (trigless) : pas dessiné en contour menthe de 2 px, sans remplissage.
- **Mode Vel** ajouté dans en-tête des pas : Trig, Vel, Accent, Slide (même hauteur, même ligne de base). Remplace
  « hauteur = vélocité partout » du v8.
  - **Trig** : trigs en blocs pleins (on/off). Glisser verticalement sur trig sélectionné (ou lock sans trig) édite
    valeur du dernier paramètre locké sur ce pas (`st.lk`) ; pendant glissé, hauteur montre valeur de ce lock, valeur
    écrite dans le pas, liste de locks met ce paramètre en tête et en menthe. Survol : curseur de redimensionnement,
    hauteur du lock prévisualisée, bord haut allumé.
  - **Vel** : hauteur de chaque trig = vélocité (accent = plein) ; glisser sur n'importe quel trig édite vélocité,
    valeur dans le pas.
  - **Accent**, **Slide** : comme avant.
  - Clic simple sélectionne (pas vide compris, nécessaire au lock sur pas vide), double-clic bascule trig, comme v11.
    « Clic pour basculer » de la demande lu ainsi : sinon clic sur pas vide créerait trig avant tout knob. À trancher.
- Seuil de 4 px avant glissé : clic reste clic.

## CPU

Lecture CPU quitte bande de tête pour barre du haut, à gauche de l'onglet Kit editor : « CPU 6.8% » (seule valeur à
porter %) + barre 48 x 6 px sur 0-100 % d'un cœur, repère fin à 15 % (budget). Barre grise, ambre `#e8a33c` au-dessus
de 15 %, rouge `#e5534b` au-dessus de 25 % (`setCpu(v)`). Texte centré sur même ligne que onglets voisins.

## Vérification

Chrome headless (CDP) 1600x1000, 26 états (20 du v11 + 6 gestes de pas) et deux états par URL, gestes joués en vrais
événements souris (CDP Input, Alt compris). **AUDIT GREEN** : 0 erreur ni avertissement console, 0 débordement,
0 texte sous 11 px, 0 contraste sous 4,5:1 (texte coloré compris), 0 cible sous 24 px, 0 rangée désalignée, 0 coin
> 2 px, 0 barre colorée, 0 valeur hors anneau (≈ 4 500 valeurs par état piste, pire marge inchangée : « 1/64T » 18,1
pour 18,8 permis sur 64 px). Nouveau contrôle `colour.js` : arc de chaque knob visible = couleur de sa section, piste
= sa teinte sombre, largeur = `KR.ringW` x D, saturation ≥ 0,45 (hors gris des envois) ; puits de chaque écran =
teinte de sa box ; chaque contrôle sélectionné dans une box = `-s` / `-t` de sa box ; en-têtes neutres ; onglets
globaux en menthe. Page piste : 39 knobs (k 8, s 9, p 7, g 6, y 5, a 2, b 2), 7 écrans, 14 sélections. Contrôles des
gestes, tous PASS : lock sur pas 6 vide → trig + point + liste ; Alt → lock sans trig en contour ; survol → curseur
ns-resize + bord ; glissé du lock 110 → 90, valeur dans le pas = liste ; mode Vel → pas 5 à 66 %, pas 1 accent plein ;
glissé de vélocité 84 → 99 ; CPU à gauche de Kit editor, centré, absent de la bande de tête, gris / ambre / rouge,
seul texte avec %. Majuscules restantes = noms de machines et codes du firmware, comme v11.
