# Maquette v6 — passe de goût

`v6.html` (1600x1000) part de v5, mêmes fonctions. v1 à v5 restent intacts. Règles : skill MIT
[Leonxlnx/taste-skill](https://github.com/Leonxlnx/taste-skill), `SKILL.md` et surtout `references/app-screens.md`
(route « app screen », écrans où l'on travaille), lus sans installer ni lancer ses scripts. Règles web et marketing
ignorées (écarts de section, titres, scènes de défilement, quotas d'animation, images).

Vérifiée en Chrome headless à 1600x1000 sur onze états (piste, piste MD, prompt de famille, SWAP, BAND en POST
avec DT2 B-W, piste DT2, FX, MASTER, SEQ, KIT, Sound Browser) : aucun débordement (en-têtes de boîte compris),
aucun texte sous 11 px, aucune erreur JS. Seule la grille des machines MD
défile dans sa boîte, comme en v4 et v5. Captures avant/après dans `v6-shots/` (`before-*.png` = v5,
`after-*.png` = v6). Tailles mesurées sur la page rendue : 11, 12, 14, 16, 20 px.

## Règles appliquées

| Règle du skill | Avant (v5) | Après (v6) |
|---|---|---|
| Un fond, des surfaces par pas de ton, pas de lignes ni d'ombres au repos | Boîtes bordées `#3a3d42`, barres séparées par des filets noirs, ombre de fenêtre | Fond `#17181a`, boîte `#232427`, en-tête `#2b2c2f`, puits `#1a1b1d`. Plus de bordure de boîte : gouttière de 8 px et pas de ton. Barres du haut et du bas, onglets de piste séparés par 1 px de fond, pas de filet noir |
| Une encre, deux gris éloignés | `#dcdfe3` + `#8d939b` + `#5d636b`, libellés de knob presque en encre | Encre `#e8e9eb`, gris `#a2a6ac` (≥ 4,5:1 sur la boîte), gris désactivé `#7a7e85` réservé à l'inactif |
| Valeurs vivantes en pleine encre | Libellés et valeurs au même gris clair | Libellés de knob et de fader en gris, valeurs en encre : l'œil lit d'abord les nombres |
| Un seul accent retenu | Teal sur chaque onglet actif, chaque bouton choisi, la marque « Drums » en jaune | Teal pour le bouton principal (Load, Save), la tête de lecture et les pas joués. Onglets actifs : changement de luminosité seulement, comme le demande la route sombre |
| Codes couleur par boîte (approuvés) gardés mais harmonisés | Teintes saturées de luminosités inégales (`#39b5c8`, `#e2876f`, `#e8c25a`…) | Mêmes teintes, désaturées à une luminosité commune (`--t #62b3bf`, `--s #d28d7c`, `--y #d6be72`…). Chaque en-tête porte une pastille de 7 px dans sa couleur, la même que dans la bande SIGNAL |
| Un seul langage de sélection | Types de filtre remplis de saumon, formes de LFO remplies de jaune, familles soulignées, machines entourées, segments à liseré | Partout : ton plus clair, texte en encre, barre de 2 px dans la couleur de la boîte. Inactif : ton de contrôle, texte gris |
| Pas de lueur, pas de verre, pas de dégradé | Lueur sur ASSIGN armé, sur la LED de piste, sur le knob cible du glisser LFO ; dégradé vert-jaune-rouge des vu-mètres ; dégradé sur les pas sélectionnés | Lueurs retirées (cible : contour de 1 px). Vu-mètres en aplat vert. Pas sélectionné : contour blanc seul |
| Aplats des courbes ≤ 15 % | Remplissages à 25-30 %, fonds teintés (LFO jaune-brun, enveloppe violette) | Remplissages à 14 %, tous les graphes sur le même puits neutre |
| Cinq tailles au plus, deux familles au plus | Dix tailles (9 à 22 px), Barlow dans les badges et la marque, poids 300 à 800 | 11 / 12 / 14 / 16 / 20 px. Barlow Semi Condensed partout, Barlow italique 800 pour le seul mot-symbole. Poids 400, 500, 600 |
| Chiffres tabulaires | Chiffres proportionnels : les valeurs bougent en tournant un knob | `font-variant-numeric: tabular-nums` sur tout le document |
| Un titre par boîte, en casse de phrase | `MACHINE`, `SAMPLE LAYER`, `LOCK LANES`… | `Machine`, `Sample layer`, `Lock lanes` : c'est aussi la casse d'Overbridge (`refs/ob-rytm-2.png`). Boutons de la bande : `Save`, `Reload`, `Clear`, `Signal`, `Mod` |
| Pas de zébrure, en-têtes de tableau en casse de phrase | Tableau du kit zébré, en-têtes `TRACK`, `FAMILY` espacés | Filet de 1 px entre lignes, en-têtes `Track`, `Family` sans espacement |
| Contenu réel, pas de bouche-trou | « Param 7 » sur Model:Cycles ; navigateur : « preset sounds: 12 » sur chaque son ; tableau du kit : pistes MD avec « ST DRIVE » et un filtre Syntakt | « Param 7 » retiré (knob vide, grisé). Navigateur : l'OS d'origine de chaque machine. Tableau : pistes MD avec filtre MD et MD SRR, MD DIST |
| Pas de bande vide | Bande SIGNAL masquée mais gardée sur FX, MASTER, SEQ, KIT : 34 px vides | Bande retirée hors de la page piste ; les pages prennent la hauteur |

## Décisions corrigées (v5 les contredisait)

- **Drives : 6 types, plus de MD AMD ni de DT2 BR** (ticket 02, deux amendements). Avant : 8 types, AMD + AMF dans
  le slot, chaîne MD chargée AMD pré + SRR post, toast « DIST: no third slot ? ». Après : Off, Syntakt drive,
  MD DIST, MD SRR, DT2 Overdrive, DT2 SRR. Chaîne MD : EQ MD, filtre MD, SRR puis DIST, tous deux post, ce qui
  montre SWAP. Une piste MD neuve affiche « Chain: firmware-exact ». Question ouverte 1 de v5 close.
- **BR du DT2 dans la voix DT2.** Il quitte les drives et devient le premier knob de la boîte MACHINE quand une
  machine DT2 est montrée (`Bit Reduction ?`, avant Tune ; page SRC, toujours en premier).
- **BAND : bouton PRE/POST et quatrième type** (ticket 02 amendé). Avant : trois types, toujours avant le filtre.
  Après : `BASE/WIDTH | MD EQ | 3-BAND + | DT2 B-W`, et `PRE | POST` dans l'en-tête de la boîte (PRE par défaut ;
  le sous-titre suit : « before / after the filter »). En POST, la bande SIGNAL place Band après Filter. Le type
  natif dépend de la famille (Syntakt : BASE/WIDTH, MD : MD EQ, DT2 : DT2 B-W) ; un autre type, ou POST, porte
  « + ».
- **Chaîne par défaut du DT2** (v5 : slots Off, BASE/WIDTH du Syntakt). Après : DT2 Overdrive en A pré, DT2 SRR en
  B post, DT2 B-W pré, filtre MULTI ; « Chain: firmware-exact ». **À confirmer** : la position de l'overdrive et du
  SRR par rapport au filtre dans la voix DT2 n'est pas dans les tickets ; la maquette en choisit une.
- **Trigs à l'échantillon près : invisible** (ticket 08). v5 n'en montrait rien, v6 non plus : pas de réglage, pas
  de mention. Le micro-timing reste en 384es de pas.
- **MASTER** (tickets 24, 10). Avant : boîte Overdrive « Digitakt II master » seule, ordre non dit. Après :
  compresseur DT2, puis overdrive DT2, puis écrêtage, ordre fixe du firmware. Compressor « Digitakt II · first on
  Main », Overdrive « Digitakt II · then clip », note de l'overdrive (même saturateur que l'overdrive de piste,
  Main seulement), et dans Main out : « Master Comp ▸ OD ▸ Clip ». L'overdrive analogique du Syntakt (modèle
  « approx ») est montré grisé dans l'en-tête, `Syntakt: later` : il arrive avec le jalon des machines analogiques.
  On/Bypass passe dans la boîte.
- **Effets d'envoi** (ticket 20). Avant : chorus « DT2 » seul, phaser « Octatrack-style | Own + ». Après : delay
  `Syntakt | MD echo | DT2`, reverb `Syntakt | MD gate box | DT2`, chorus `Own + | DT2`, phaser/flanger `Own +` seul
  (un effet, deux modes). Le DT2 est grisé : il arrive à son jalon. Notes réécrites (Syntakt porté exact, maison
  réglé contre le Virus TI d'OsTIrus).
- **Retours d'effets** (ticket 10) : chaque retour a sa sortie (Main ou une des 15) sous son fader.

## Écarts au skill, et pourquoi

- **Libellés de page en capitales** (`DEST`, `ENV`, `RATIO`, `PRE/POST`, `AHD/ADSR`, `FREE/TRIG/HOLD`, noms de
  machines). Le skill veut la casse de phrase et au plus un libellé capital espacé. Ce sont les noms des écrans
  Elektron et du firmware : les changer trahirait la machine. Gardés en capitales, sans espacement, en gris.
- **Knobs ombrés et anneaux de valeur colorés.** Convention de plug-in audio ; le skill n'en parle pas. Visage plat
  gardé, anneau dans la couleur de la boîte.
- **Couleur par boîte.** Le skill veut une teinte par graphe et quatre teintes au plus par écran ; la page piste en
  compte huit (une par étage de la chaîne). Code approuvé en v2-v5 : gardé, désaturé, même luminosité.
- **Violet** de AMP et du phaser : le skill bannit les accents violets. Ici, couleur de code, pas d'accent ; adoucie.
- **Bordure de 1 px sur les menus et champs**, **ombres** sur le menu « + », le toast et la boîte agrandie :
  autorisés par la route app (contrôles, éléments flottants).
- **Rouge des trigs** (`#8a4b44`) et ambre des trigs verrouillés : convention Elektron, gardés à la chroma de la
  palette.
- **Barre colorée de 2 px à gauche des slots de drive.** Le skill tolère une barre d'accent une fois ; ici elle dit
  « slot actif » contre « Off » (gris).
- **Mot-symbole et barre d'icônes**, cadre façon Overbridge : demandés par le ticket 09, gardés.

## Ce qui cloche encore

1. **Densité.** La page piste reste un mur de knobs à poids égal : 50 anneaux colorés se disputent l'œil. Il
   faudrait que seuls les knobs « pri » portent la couleur et que les secondaires passent en gris.
2. **Huit teintes.** Même harmonisées, elles restent nombreuses ; violet (AMP) et bleu (BAND) se distinguent mal en
   petit.
3. **Pas verrouillés.** Les aplats ambre de la boîte Steps pèsent plus que le reste de la page ; un trait ou un
   coin marqué suffirait.
4. **Familles non confirmées.** Rytm, Model:Cycles, Digitakt II, Digitone II et Analog-approx affichent encore des
   noms suivis de « ? » : honnête, mais ce n'est pas du contenu réel.
5. **Sound Browser** presque vide : 26 cartes sur 1600x900, aucune hiérarchie, aucune préécoute visible.
6. **Kit** : sous la boîte Kit, le résumé flotte ; le tableau des pistes pourrait monter.
7. **Bande SIGNAL** chargée : nom du son, trois boutons, sept étages, deux poignées LFO, fidélité et CPU sur 34 px.
