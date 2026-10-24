# Maquettes v3a / v3b — notes

Deux variantes du même habillage, tirées de v2 (v2.html inchangé depuis la correction STEPS / LOCK LANES).
Fenêtre fixe 1600x1000, zoom 75-150 % au pied (CSS `zoom` sur la fenêtre). JS vanilla, rien ne sonne.

## Commun à a et b

- **Une page par piste**, toutes les boîtes à la fois, façon Rytm : MACHINE (familles, grille, 8 anneaux,
  Volume ; onglet Sound = chromatique, vélocité, choke), SAMPLE LAYER, FILTER, AMP (étroite : 4 anneaux en
  colonne), DRIVE, BAND, ENVELOPES (Filter|Amp), LFO (1|2), MIXER (5 faders + sortie). Plus de sous-pages.
- **Bande de tête** : nom du son + SAVE/RELOAD/CLEAR (Sound Settings réduit à une ligne), chaîne du signal
  (clic = cerne la boîte), poignées LFO1 / LFO2, info piste et CPU.
- **Onglets de piste** : LED de déclenchement (s'éteint en fondu ; BD 1, SD 2, CH 9 allumés à des intensités
  différentes) et petit ▶ qui joue la piste sans la choisir ; clic sur le nom = choisir. Rangée de pads et
  clavier retirés, la place va aux boîtes.
- **Boîte ouverte** : clic sur un titre de boîte (ou ⤢ sur DRIVE) = la boîte s'agrandit par-dessus ses voisines,
  Esc ferme. DRIVE fermé : chaque slot montre type, PRE/POST, un anneau (quantité) ; ouvert : réordonner,
  retirer, PRE/POST, Amount / Level / Mix.
- **Affecter depuis l'anneau** : au survol, pastille « + » sur l'anneau ; clic (ou clic droit) = petit menu
  LFO 1 / LFO 2 / Lock (a : sur les pas choisis ; b : ajouter une piste de Lock). Ensuite glisser l'arc coloré
  sur l'anneau pour la profondeur. Anneau modulé : arc à la couleur du LFO (jaune LFO 1, rose LFO 2) depuis la
  valeur, et point qui montre la valeur jouée (animé).
- **Secondaire, gardé** : bouton ASSIGN dans la boîte LFO, collant ; touche n'importe quel anneau = destination,
  Esc désarme, bandeau « LFO 1 ASSIGN: touch a knob » dans la bande de tête. Poignées LFO1 / LFO2 à glisser sur
  un anneau. Menu de destination groupé par boîte (Machine, Sample, Drive, Band, Filter, Amp, Mixer).
- **Locks en blanc** (cercle pointillé + point sur le nom, point blanc sur les pas) pour ne pas se confondre
  avec le jaune du LFO 1.
- **LIVE · LOCK REC** dans le transport : en lecture, un anneau tourné écrit sur le pas joué ; le ▶ d'un onglet
  de piste enregistre une trig sur le pas joué (piste choisie, dans la maquette).
- Le geste matériel « tenir une trig et tourner » reste disponible depuis un contrôleur MIDI / Push.

## Différences

| | v3a — sélection collante | v3b — pistes de Lock |
|---|---|---|
| Page piste | boîte STEPS fixe en bas (16 pas, pages 1-4, pattern, LIVE) | pas de boîte de pas, boîtes plus hautes |
| Poser un Lock | clic sur un pas (reste allumé), Ctrl-clic ajoute, Esc ou clic dans le vide relâche ; tout anneau tourné écrit sur ces pas ; bandeau « LOCK: steps 5, 9 » ; liste des Locks de la sélection | onglet SEQ : LOCK LANES, une piste par paramètre, barre par pas dessinée à la souris (Alt-clic efface) |
| Choisir le paramètre | l'anneau lui-même | clic sur un anneau dans la vue piste réduite à côté (même page, à l'échelle), ou menu groupé par boîte |
| Onglet SEQ | patterns, pistes, pas, détail du pas, liste des Locks du pattern | patterns, pas de la piste choisie (sélecteur 16 pistes), détail du pas, LOCK LANES, vue piste réduite |
| États dessinés | pas 5 et 9 choisis, 4 Locks, anneaux Tune / Decay / Start cerclés ; menu « + » ouvert sur Sweep Depth ; LFO2 en cours de glisser sur Filter Frequency ; LFO 1 sur Machine Decay | DRIVE ouvert (4 slots) ; ASSIGN armé ; LIVE armé ; 3 pistes : Filter Frequency, Machine Tune, Drive Slot 1 Amount ; LFO 1 sur Filter Frequency |

## Exact / approximatif / ajouté

Inchangé depuis v2 : « + » sur Analog-approx, EQ 3 bandes, phaser/flanger Own. Noms des paramètres de machine,
courbes, valeurs, chiffres CPU et latence : factices.

## Questions ouvertes

1. a ou b, ou les deux (sélection collante sur la page piste + pistes de Lock dans SEQ) ?
2. Page unique à 1600x1000 : assez lisible à 100 %, ou la taille par défaut doit-elle être 125 % ?
3. LIVE : la trig du ▶ d'onglet va-t-elle à la piste de l'onglet (toutes pistes) ou seulement à la piste choisie ?
4. Couleurs : blanc pour les Locks, jaune / rose pour les LFO — d'accord ?
5. Toujours ouverts depuis v2 : 8e type multimode Syntakt, Legacy LP/HP en un ou deux types, machines du DT2,
   noms Analog-approx.
