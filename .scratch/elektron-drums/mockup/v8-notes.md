# Maquette v8 — retours sur v7

`v8.html` (1600x1000) part de v7 ; v1 à v7 intacts. Deux jeux de variantes : `v8-var-font.html` (police) et
`v8-var-accent.html` (accent des pas). Captures dans `v8-shots/`.

## Retours, point par point

| # | Retour | Changement |
|---|---|---|
| 1 | Coins arrondis partout | 0-2 px sur boîtes, en-têtes, boutons, menus, popovers, pas, badges. Seuls les points (LED, couleurs) restent ronds. Vérifié par script : aucun rayon > 2 px hors cercles |
| 2 | Disposition fixe, deux drives dans une boîte | Rangée 1 : Machine, Drive, Band, Filter ; rangée 2 : Sample layer, Amp, LFO, Sends. Rien ne bouge. Boîte **Drive** : slot A puis slot B, chacun sur une ligne (lettre, menu de type, PRE/POST), puis ses knobs et MIX ; un slot Off = la ligne seule. PRE/POST n'est qu'un état. La Band garde aussi sa place, PRE/POST en état. Bouton ⇄ supprimé (l'ordre A puis B est fixe) |
| 3 | Agrandissement de boîte | Supprimé (clic d'en-tête, Esc, code) |
| 4 | Formes d'onde | Sample layer ouvert, pastille « DT2 milestone », forme d'onde avec début, fin (drapeaux) et boucle (pointillé). Boîte Machine : zone d'affichage au-dessus des knobs. Machine de synthèse : dernier coup en colonnes min/max (3 px), qui s'estompe avec la LED de piste (piste 1, BD MODERN). Machine à sample : sample + marqueurs ; SP TWINSHOT montre ses deux samples (piste 16), ROM du MD début/fin (STRT/END), DT2 début/longueur/boucle |
| 5 | Une seule façon d'assigner (corrigé ensuite) | LFO : **ASSIGN** verrouillé seul (armer, toucher un knob, Esc désarme). Poignées LFO supprimées, onglets LFO 1 / LFO 2 simples. Menu « + » du knob : Velocity et Lock sur les pas sélectionnés, plus de LFO. DEST montre la destination du LFO affiché, « None » s'il n'en a pas |
| 6 | Nom du son | Affiché seulement si le son a été nommé (WINSTON KICK sur la piste 1). Pistes jamais sauvées (3, 6, 8, 12, 13, 15, 16) : rien, le menu machine suffit. Tableau du kit : « — » ; « Load on » du navigateur : nom court de piste |

## Retours arrivés pendant la passe

- **Pas de « % »** : Mix 100, CPU 6.8, zoom 100. Formats `pct` et `mixpct` sans unité.
- **Menus** : `select` dessinés (appearance none, flèche SVG), 24 px, texte centré sur la ligne des boutons voisins.
  Contrôle par script : chaque rangée de boutons et menus a une seule hauteur et un seul centre (tête de bande ramenée
  à 28 px pour le menu machine, barre de sélection du navigateur à 24 px).
- **LIVE** : dans l'en-tête de Steps, à droite des pages, même hauteur que TRIG/ACC/SLIDE ; libellé « LIVE », rouge
  quand il enregistre. Ligne « Playing » supprimée (le transport le dit). Même libellé sur la page SEQ.
- **LFO synchronisé** : bascule **SYNC / HZ** (SYNC par défaut) devant DEST. SYNC : Speed en division (1/64T … 16/1,
  pointées « . », triolets « T ») ; HZ : 0.05 à 50. État gardé par LFO. Modes de déclenchement inchangés.
- **Accent** : barre retirée. Trois variantes dans `v8-var-accent.html` : (a) intensité LED, trig à mi-intensité,
  accent pleinement allumé (chiffres en encre sombre) ; (b) accent en ambre ; (c) hauteur = vélocité, accent = pas
  plein. **v8.html utilise (a)**. Slide : encoche violette sur le bord droit du pas, pareille dans les trois.
- **Valeur dans l'anneau** : script qui, pour chaque knob visible et **chaque valeur que le paramètre peut prendre**
  (min à max par son format : « 40:87 », « -64 », « ×2048 », « 1/64T », « LP64 »…), mesure la boîte d'encre rendue
  et exige 2 px de marge sous le bord intérieur de l'anneau (rayon r-3). La taille de valeur descend d'un pas
  (16, 14, 12, 11) jusqu'à ce que toutes tiennent ; sinon la valeur passe sous le knob. Les knobs d'une même rangée
  prennent la même règle (même taille, ou tous dessous). Résultat : **0 dépassement** sur 21 états (≈ 5 000 valeurs
  par page piste) ; valeurs sous le knob : Sample layer (Mix M↔S), LFO (Multiplier ×2048), panoramiques du mixer
  (32 px), quelques rangées FX et MASTER.
- **Police** : `v8-var-font.html`, même extrait (bande de tête, Machine, Drive, Filter, Steps) trois fois. Mesure des
  chiffres tabulaires (`font-variant-numeric: tabular-nums`, dix « 1 » contre dix « 0 ») : **IBM Plex Sans Condensed
  oui ; Archivo Narrow non ; Saira Semi Condensed non** (Google Fonts ne livre pas `tnum` pour ces deux-là : les
  valeurs sautent en largeur quand on tourne un knob). Les trois passent contraste, 11 px, débordement et anneaux.
  v8.html reste en Barlow Semi Condensed.

## Vérification

Chrome headless (CDP) à 1600x1000, 21 états (piste Syntakt, menu machine, ASSIGN armé, menu du knob, LFO en HZ,
LFO 2, accents (b) et (c), choke, MD sans nom, TWINSHOT, ROM, chaîne mixte, DT2 avec deux drives, prompt de famille,
FX, MASTER, SEQ, KIT, navigateur, sélection multiple) : **0 erreur JS, 0 débordement, 0 texte sous 11 px, 0 texte
sous 4,5:1, 0 cible sous 24 px, 0 rangée désalignée, 0 coin > 2 px, 0 valeur hors anneau.** Tailles rendues : 11,
12, 14, 16, 20 px.

## Questions ouvertes

1. **Ordre du signal** : Sample layer est sous Machine (rangée 2, colonne 1), donc avant Amp à la lecture mais après
   Filter dans l'ordre des rangées. Lisible comme « sources à gauche » ; à confirmer.
2. **Boîte Drive** : avec un slot Off, le bas de la boîte est vide (≈ 120 px). Garder, ou réduire Drive et élargir
   Filter ?
3. **Anneaux de lock et de modulation** : ils sont dessinés à l'intérieur de l'anneau de valeur (r-5,5 et r-7) ; la
   règle des 2 px vise l'anneau de valeur. Sur un knob verrouillé, le pointillé peut frôler la valeur. Les sortir ?
4. **Valeurs à 11 px** dans les knobs de 36 px (enveloppes) : conformes mais petites ; passer ces rangées à 44 px ?
5. **Police** : deux des trois candidates n'ont pas de chiffres tabulaires servis ; garder Plex, ou proposer d'autres
   faces condensées avec `tnum` (Barlow, l'actuelle, en a) ?
6. **Accent (c)** : la vélocité n'a pas encore de réglage par pas visible ; si (c) est choisi, il faut un lieu d'édition.
