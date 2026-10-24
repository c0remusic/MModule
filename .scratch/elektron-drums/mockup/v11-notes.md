# Maquette v11 — passe frontend-design

`v11.html` + `v11.css` + `v11.js` (1600x1000), partis de v10 ; v1 à v10 intacts. Découpée en trois fichiers : aperçu de
l'app n'ouvre pas un HTML de plus de ~160 Kio (v10, 164 703 octets, refusé). Tailles : `v11.html` 37 943, `v11.css`
52 822, `v11.js` 77 147 octets. Captures : `v11-shots/`.

## Plan approuvé

- **Couleur, six valeurs.** Châssis `#26282b` (gris moyen-sombre, jamais quasi noir), panneau `#303337`, puits d'écran
  `#1b1d20`, encre `#e6e8ea`, secondaire `#a9adb3`, accent teal `#3fb3c4` pour l'état seul (trigs, LED de piste, valeurs
  lockées, focus). Tons de contrôle = paliers du panneau (`#3a3d42`, survol `#3e4146`, sélection `#4a4e55`).
- **Identité des boxes par leur teinte**, plus par l'accent : neuf teintes à même clarté et même chroma OKLCH (L 0,78,
  C 0,075), contraste 6,2 à 6,6:1 sur panneau. Machine `#8dc7a7`, sample `#a9c18d`, drive `#dbad86`, band `#97bae7`,
  filtre `#e4a69d`, ampli `#c0ace0`, LFO 1 `#c8b680`, LFO 2 `#daa5c5`, vélocité `#b3be88`, envois `#b3b8be`. Machine,
  delay FX et compresseur quittent le teal (v10 : `--t`) pour la teinte machine.
- **Type.** Saira Semi Condensed pour texte, IBM Plex Sans Condensed tabulaire pour valeurs ; quatre tailles 11 / 12 /
  14 / 18 (20 et 16 du v10 ramenés à 18 et 14).
- **Mise en page.** Grille fixe et ordre du signal du v10 gardés. Seul élément audacieux : tous les écrans (onde ou scope
  machine, sample, courbe et enveloppe du filtre, réponse du band, enveloppe d'ampli, LFO) prennent même traitement
  creusé (puits `#1b1d20`, ombre intérieure noire, liseré clair de 1 px dessous, coins carrés), même hauteur (110 px),
  alignés sur une ligne en rangée 1 et une en rangée 2 : lus comme un seul écran d'instrument. Reste silencieux.

## Changements depuis v10

- **Écran en tête de chaque box** : sous l'en-tête, avant tout contrôle. Rangée 1 de 336 px (deux rangées de knobs
  machine sous l'écran), rangée 2 du reste ; hauteurs de rangée différentes, écrans de même hauteur.
- **Band** : zone vide du v10 devient vraie courbe de réponse, en écran. 3-band : étiquettes de colonne LOW / MID / HIGH
  retirées, libellés de knobs « Low freq », « Mid gain »...
- **Filtre** : courbe (64 %) et enveloppe (36 %) côte à côte dans bande d'écran ; types dessous ; une seule rangée de
  neuf knobs secondaires (Frequency et Resonance passent de 64 à 52 px pour tenir), étiquette ENV retirée (enveloppe
  lisible dans son écran). Key track caché quand type MD (band filter MD sans suivi de clavier) : hypothèse, à confirmer.
- **Ampli** : AHD / ADSR et Reset (AENR) montés dans l'en-tête, comme Pre / Post du band ; enveloppe et Level / Pan /
  Accent sur une seule rangée. Machine MD : pas d'enveloppe, donc pas d'écran (comme Drive et Sends, sans affichage).
- **LFO** : formes et mode de trig sur une ligne, Sync / Hz + destination + Assign sur la suivante ; étiquette DEST
  retirée (menu porte « Destination » en infobulle).
- **Sample layer** : écran d'abord, menu du sample dessous ; badge « DT2 milestone » réduit à « DT2 » encadré.
- **Bouton principal** (Load du prompt, Save du kit, Load on track) : ton sélectionné + filet, plus d'aplat teal.
- **Footer** : boutons « demo: » retirés. États de démo par paramètre d'URL : `v11.html?state=family` (prompt de
  changement de famille sur chaîne modifiée), `v11.html?state=knob` (menu du knob).
- Bande pads + clavier (cachée depuis v4) retirée du HTML.

## Tics génériques retirés

- **Capitales** → casse de phrase : Pre / Post, Base/width, 3-band, Sync / Hz, Assign, Reset, Free / Trig / Hold / One
  / Half, Trig / Accent / Slide, Live, Off / On, Phaser / Flanger, Forward / Reverse / Loop / Ping-pong, With machine /
  Own note, Synth / Sample, Linear / Soft / Hard / Fixed, Multi / Legacy / Off (types de filtre), étiquettes Pattern,
  Chain, Scale, Page, Ratio, Sidechain, Trig condition, Ratchet, Micro-timing, Out, Ping-pong, onglets Master / Seq, Kit
  editor / Sound browser, noms de kits et de sons (« Rainy day », « Winston kick »), en-têtes de groupe du menu machine,
  libellés de knobs (« Pitch sweep », « Fade in/out »). Gardés : noms de machines du firmware (BD MODERN, TRX-BD), noms
  courts MD (PTCH, DEC), codes de type (LP, HP, EQ1, COMB-, MD), AHD / ADSR, conditions de trig (FILL, 1:2), A01.
  `text-transform: uppercase` des menus de knob et de la liste SEQ retiré.
- **Chaînes à points médians** : onglet « KIT - RAINY DAY » → « Rainy day » (infobulle Kit) ; « 034 · Hate Sound » →
  « 034 Hate sound » ; « Step 5 · trig » → « Step 5 » (grille montre déjà le trig) ; origines du navigateur
  (« Syntakt BD MODERN ») ; infos de preview en lignes Rate / Bits / Channels ; titres de types de filtre ; « Drive B ·
  off » → « Drive B (off) » ; résumé de chaîne du prompt séparé par virgules, plus par ▸.
- **Flèches et points de suspension sur boutons** : « Lanes and all tracks ▸ » → « All tracks and lanes » ; Load…,
  Save as…, Import…, Export…, Retype…, Tag…, « Import my Elektron content… » → sans.
- **Unités** retirées des valeurs : réduction de gain, Peak, Latency, Rate (48k), BPM, longueurs du navigateur (unité en
  infobulle ou dans l'en-tête de colonne).
- **Mouvement décoratif** : transitions de survol, `scale` au clic, animation d'ouverture des popovers et du prompt
  retirées. Bougent seulement : scope et LED au hit, playhead, mètres, valeurs.

## Écart assumé

Texte d'un knob = fraction fixe de son diamètre (règle du ticket) : knob primaire de 64 px porte valeur 13,54 px et
libellé 14,77 px, hors échelle 11 / 12 / 14 / 18. Proportion fixe des knobs prime ; aucune autre taille hors échelle.

## Vérification

Chrome headless (CDP) 1600x1000, 20 états (19 du v10 + 3-band / AHD / options du sample) et deux états par URL :
**0 erreur ni avertissement console, 0 débordement, 0 texte sous 11 px, 0 contraste sous 4,5:1, 0 cible sous 24 px,
0 rangée désalignée, 0 coin > 2 px, 0 barre colorée, 0 valeur hors anneau** (≈ 5 000 valeurs par état piste ; pire
marge `1/128` sur 64 px, 18,2 pour 18,8 permis). Nouveau contrôle `scr.js` : écrans visibles de la page piste tous de
110 px, même haut par rangée (7 écrans, 6 sur piste MD) ; capitales restantes = noms et codes du firmware listés plus
haut ; aucun point médian, ▸ ou … visible. Tailles rendues : 11 / 12 / 14 / 18, plus 13,54 / 14,77 des knobs de 64.
