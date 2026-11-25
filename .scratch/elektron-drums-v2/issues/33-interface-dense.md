# Une interface moins dense

Type: HITL
Status: done (essai dans Live à venir)
Assignee: Claude (session 2026-11-25)
Blocked by: aucun

## Quoi

Retour de test dans Live (2026-11-24) : l'interface est très dense.

## Décidé (2026-11-25, avec l'utilisateur, maquettes dans `.scratch/elektron-drums-v2/mockup/`)

Parcours : replier les étages éteints (v1) puis resserrer (v2) laissait des cases noires ; trois mises en page (v3 :
pages, cartes, scène et barre) ; fusions AMP dans VOICE et ENVELOPE dans FILTER (v4) ; retour à une seule page avec
tous les réglages (v5 à v8) ; v9 retenue.

La page Sound de la v9 (`v8-filled.html`, rendu `v9-full.png`), à 1600 × 1000, les touches et pads inchangés :

- Trois colonnes, dans l'ordre du son :
  - gauche, la source : VOICE (le Hit, puis les valeurs de la machine, PITCH, CHOKES, VEL TARGET, VEL AMT), SAMPLE
    LAYER dessous avec ses 14 valeurs ;
  - milieu : AMP, puis la chaîne, EQ (sa courbe à droite de ses valeurs), FILTER (sa courbe, petite, à droite de
    CUTOFF et RESO), son ENVELOPE (six valeurs sur une ligne), DRIVES (PRE ou POST dans leur ligne de titre) ;
  - droite : MOD (LFO 1 puis LFO 2, chacun un sélecteur de forme en sept cases, sans image d'onde, et ses six
    valeurs ; ASSIGN sur la ligne de MOD), SENDS en dernier (quatre valeurs, plus de vumètres).
- Une seule taille de case partout ; pas de trait entre les valeurs, seulement les bandes de couleur des sections.
- Un étage éteint se grise, il ne bouge pas.
- Les blocs d'une colonne se répartissent sur sa hauteur ; aucun élément n'est agrandi pour remplir.
- Plus d'images d'enveloppe (filtre, ampli) ni d'onde de LFO : trois images, le Hit, les courbes du filtre et de l'EQ.
- PITCH : en attendant le ticket 39, la case montre NOTE, et TUNE reste dans les valeurs de la machine.

## Critère de fin

- [x] L'éditeur dessine la page Sound comme `v9-full.png`, vérifié à l'écran à 1:1 (capture de `edDrumsEditorTest`
  comparée à la maquette).
- [x] Chaque paramètre de piste que montrait l'ancienne page a sa case sur la nouvelle : relu dans le code, pas de
  test dédié ; seules MULT, FADE et PHASE disparaissent, des cases sans paramètre.
- [x] Les gestes gardent leur comportement (glisser, clic sur une liste, ASSIGN, sélecteurs).
- [ ] Essai dans Live.
